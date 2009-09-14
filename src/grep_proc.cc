/**
 * @file grep_proc.cc
 */

#include "config.h"

#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "grep_proc.hh"

#include "time_T.hh"

using namespace std;

grep_proc::grep_proc(pcre *code,
		     grep_proc_source &gps,
		     int &maxfd,
		     fd_set &readfds)
    : gp_pcre(code),
      gp_code(code),
      gp_source(gps),
      gp_pipe_offset(0),
      gp_child(-1),
      gp_child_started(false),
      gp_maxfd(maxfd),
      gp_readfds(readfds),
      gp_last_line(0),
      gp_sink(NULL),
      gp_control(NULL)
{
    const char *errptr;

    this->gp_code_extra = pcre_study(code, 0, &errptr);

    assert(this->invariant());
}

grep_proc::~grep_proc()
{
    this->cleanup();
}

void grep_proc::handle_match(int line,
			     string &line_value,
			     int off,
			     int *matches,
			     int count)
{
    int lpc;
    
    if (off == 0) {
	fprintf(stdout, "%d\n", line);
    }
    fprintf(stdout, "[%d:%d]\n", matches[0], matches[1]);
    for (lpc = 1; lpc < count; lpc++) {
	fprintf(stdout,
		"(%d:%d)",
		matches[lpc * 2],
		matches[lpc * 2 + 1]);
	fwrite(&(line_value.c_str()[matches[lpc * 2]]),
	       1,
	       matches[lpc * 2 + 1] -
	       matches[lpc * 2],
	       stdout);
	fputc('\n', stdout);
    }
}

void grep_proc::start(void)
{
    assert(this->invariant());

    if (this->gp_child_started || this->gp_queue.empty()) {
	return;
    }

    auto_fd out_fd[2], err_fd[2];

    /* Get ahold of some pipes for stdout and stderr. */
    if (auto_fd::pipe(out_fd) < 0) {
	throw error(errno);
    }

    if (auto_fd::pipe(err_fd) < 0) {
	throw error(errno);
    }

    if ((this->gp_child = fork()) < 0) {
	throw error(errno);
    }

    if (this->gp_child != 0) {
	fcntl(out_fd[0], F_SETFL, O_NONBLOCK);
	fcntl(out_fd[0], F_SETFD, 1);
	this->gp_line_buffer.set_fd(out_fd[0]);

	fcntl(err_fd[0], F_SETFL, O_NONBLOCK);
	fcntl(err_fd[0], F_SETFD, 1);
	this->gp_err_pipe      = err_fd[0];
	this->gp_child_started = true;

	FD_SET(this->gp_line_buffer.get_fd(), &this->gp_readfds);
	FD_SET(this->gp_err_pipe, &this->gp_readfds);
	this->gp_maxfd = std::max(this->gp_maxfd,
				  std::max(this->gp_line_buffer.get_fd(),
					   this->gp_err_pipe.get()));
	this->gp_queue.clear();
	return;
    }

    /* In the child... */
    
    /*
     * First, restore the default signal handlers so we don't hang around
     * forever if there is a problem.
     */
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);

    /* Get rid of stdin, then */
    close(STDIN_FILENO);
    open("/dev/null", O_RDONLY);
    /* ... wire up the pipes. */
    dup2(out_fd[1].release(), STDOUT_FILENO);
    /* dup2(err_fd[1].release(), STDERR_FILENO); */

    this->child_init();
    
    char   outbuf[BUFSIZ * 2];
    string line_value;

    stdout = fdopen(STDOUT_FILENO, "w");
    /* Make sure buffering is on, not sure of the state in the parent. */
    if (setvbuf(stdout, outbuf, _IOFBF, BUFSIZ * 2) < 0) {
	perror("setvbuf");
    }
    line_value.reserve(BUFSIZ * 2);
    while (!this->gp_queue.empty()) {
	grep_line_t start_line = this->gp_queue.front().first;
	grep_line_t stop_line  = this->gp_queue.front().second;
	bool        done       = false, got_first_hit = false;
	int         line;

	this->gp_queue.pop_front();
	if (start_line == -1) {
	    start_line = this->gp_last_line;
	}
	for (line = start_line;
	     (stop_line == -1 || line < stop_line) && !done;
	     line++) {
	    line_value.clear();
	    done = !this->gp_source.grep_value_for_line(line, line_value);
	    if (!done) {
		pcre_context_static<10> pc;
		pcre_input pi(line_value);
		
		while (this->gp_pcre.match(pc, pi)) {
		    pcre_context::iterator pc_iter;
		    pcre_context::match_t *m;
		    
		    if (pi.pi_offset == 0) {
			fprintf(stdout, "%d\n", line);
		    }
		    m = pc.all();
		    fprintf(stdout, "[%d:%d]\n", m->m_begin, m->m_end);
		    for (pc_iter = pc.begin(); pc_iter != pc.end(); pc_iter++) {
			fprintf(stdout,
				"(%d:%d)",
				pc_iter->m_begin,
				pc_iter->m_end);
			fwrite(pi.get_substr(pc_iter),
			       1,
			       pc_iter->length(),
			       stdout);
			fputc('\n', stdout);
		    }
		}
		
	    }

	    if (((line + 1) % 10000) == 0) {
		/* Periodically flush the buffer so the parent sees progress */
		this->child_batch();
	    }
	}

	fprintf(stdout, "%d\n", line - 1);
	this->child_term();
    }

    exit(0);
}

void grep_proc::cleanup(void)
{
    if (this->gp_child != -1 && this->gp_child != 0) {
	int status;

	kill(this->gp_child, SIGTERM);
	while (waitpid(this->gp_child, &status, 0) < 0 && (errno == EINTR)) {
	    ;
	}
	assert(!WIFSIGNALED(status) || WTERMSIG(status) != SIGABRT);
	this->gp_child         = -1;
	this->gp_child_started = false;

	if (this->gp_sink) {
	    this->gp_sink->grep_end(*this);
	}
    }

    if (this->gp_err_pipe != -1) {
	FD_CLR(this->gp_err_pipe, &this->gp_readfds);
	this->gp_err_pipe.reset();
    }

    if (this->gp_line_buffer.get_fd() != -1) {
	FD_CLR(this->gp_line_buffer.get_fd(), &this->gp_readfds);
    }

    this->gp_pipe_offset = 0;
    this->gp_line_buffer.reset();

    assert(this->invariant());

    if (!this->gp_queue.empty()) {
	this->start();
    }
}

void grep_proc::dispatch_line(char *line)
{
    int start, end, capture_start;

    assert(line != NULL);

    if (sscanf(line, "%d", this->gp_last_line.out()) == 1) {
	/* Starting a new line with matches. */

	assert(this->gp_last_line >= 0);
    }
    else if (sscanf(line, "[%d:%d]", &start, &end) == 2) {
	assert(start >= 0);
	assert(end >= 0);

	/* Pass the match offsets to the sink delegate. */
	if (this->gp_sink != NULL) {
	    this->gp_sink->grep_match(*this, this->gp_last_line, start, end);
	}
    }
    else if (sscanf(line, "(%d:%d)%n", &start, &end, &capture_start) == 2) {
	assert(start >= 0);
	assert(end >= 0);

	/* Pass the match offsets to the sink delegate. */
	if (this->gp_sink != NULL) {
	    this->gp_sink->grep_capture(*this,
					this->gp_last_line,
					start,
					end,
					&line[capture_start]);
	}
    }
    else {
	fprintf(stderr, "bad line from child -- %s\n", line);
    }
}

void grep_proc::check_fd_set(fd_set &ready_fds)
{
    assert(this->invariant());

    if (this->gp_err_pipe != -1 && FD_ISSET(this->gp_err_pipe, &ready_fds)) {
	char buffer[1024 + 1];
	int  rc;

	rc = read(this->gp_err_pipe, buffer, sizeof(buffer) - 1);
	if (rc > 0) {
	    static const char *PREFIX = ": ";

	    buffer[rc] = '\0';
	    if (strncmp(buffer, PREFIX, strlen(PREFIX)) == 0) {
		char *lf;

		if ((lf = strchr(buffer, '\n')) != NULL) {
		    *lf = '\0';
		}
		if (this->gp_control != NULL) {
		    this->gp_control->grep_error(&buffer[strlen(PREFIX)]);
		}
	    }
	}
	else if (rc == 0) {
	    FD_CLR(this->gp_err_pipe, &this->gp_readfds);
	    this->gp_err_pipe.reset();
	}
    }

    if (this->gp_line_buffer.get_fd() != -1 &&
	FD_ISSET(this->gp_line_buffer.get_fd(), &ready_fds)) {
	try {
	    static const int MAX_LOOPS = 100;

	    int    loop_count = 0;
	    size_t len;
	    char   *line;

	    while ((loop_count < MAX_LOOPS) &&
		   (line = this->gp_line_buffer.read_line(this->gp_pipe_offset,
							  len)) != NULL) {
		line[len] = '\0';
		this->dispatch_line(line);
		loop_count += 1;
	    }

	    if (this->gp_sink != NULL) {
		this->gp_sink->grep_end_batch(*this);
	    }

	    if ((off_t) this->gp_line_buffer.get_file_size() ==
		this->gp_pipe_offset) {
		this->cleanup();
	    }
	}
	catch (line_buffer::error & e) {
	    this->cleanup();
	}
    }

    assert(this->invariant());
}
