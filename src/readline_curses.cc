/**
 * @file readline_curses.cc
 */

#include "config.h"

#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>

#ifdef HAVE_PTY_H
#include <pty.h>
#endif

#ifdef HAVE_UTIL_H
#include <util.h>
#endif

#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif

#include <string>

#include "auto_mem.hh"
#include "readline_curses.hh"

using namespace std;

static int             got_line    = 0;
static sig_atomic_t    got_timeout = 0;
static sig_atomic_t    got_winch   = 0;
static readline_curses *child_this;
static sig_atomic_t    looping = 1;
static const int HISTORY_SIZE = 256;

readline_context *readline_context::loaded_context;
set<string> *readline_context::arg_possibilities;

static void sigalrm(int sig)
{
    got_timeout = 1;
}

static void sigwinch(int sig)
{
    got_winch = 1;
}

static void sigterm(int sig)
{
    looping = 0;
}

static void line_ready_tramp(char *line)
{
    child_this->line_ready(line);
    add_history(line);
    got_line = 1;
    rl_callback_handler_remove();
}

static int reliable_send(int sock, char *buf, size_t len)
{
    int retval;
    
    while ((retval = send(sock, buf, len, 0)) == -1) {
	if (errno == ENOBUFS) {
	    fd_set ready_wfds;

	    FD_ZERO(&ready_wfds);
	    FD_SET(sock, &ready_wfds);
	    select(sock + 1, NULL, &ready_wfds, NULL, NULL);
	}
	else if (errno == EINTR) {
	    continue;
	}
	else {
	    break;
	}
    }

    return retval;
}

char *readline_context::completion_generator(const char *text, int state)
{
    static vector<string> matches;

    char *retval = NULL;

    if (state == 0) {
	int len = strlen(text);

	matches.clear();
	if (arg_possibilities != NULL) {
	    set<string>::iterator iter;

	    for (iter = arg_possibilities->begin();
		 iter != arg_possibilities->end();
		 ++iter) {
		fprintf(stderr, " cmp %s %s\n", text, iter->c_str());
		if (strncmp(text, iter->c_str(), len) == 0) {
		    matches.push_back(*iter);
		}
	    }
	}
    }

    if (!matches.empty()) {
	retval = strdup(matches.back().c_str());
	matches.pop_back();
    }

    return retval;
}

char **readline_context::attempted_completion(const char *text,
					      int start,
					      int end)
{
    char **retval = NULL;

    if (loaded_context->rc_possibilities.find("*") != loaded_context->rc_possibilities.end()) {
	fprintf(stderr, "all poss\n");
	arg_possibilities = &loaded_context->rc_possibilities["*"];
	rl_completion_append_character = ' ';
    }
    else if (start == 0) {
	arg_possibilities              = &loaded_context->rc_possibilities["__command"];
	rl_completion_append_character = ' ';
    }
    else {
	char   *space;
	string cmd;

	rl_completion_append_character = 0;
	space = strchr(rl_line_buffer, ' ');
	assert(space != NULL);
	cmd = string(rl_line_buffer, 0, space - rl_line_buffer);
	fprintf(stderr, "cmd %s\n", cmd.c_str());

	vector<string> &proto = loaded_context->rc_prototypes[cmd];

	if (proto.empty()) {
	    arg_possibilities = NULL;
	}
	else if (proto[0] == "filename") {
	    return NULL; // XXX
	}
	else {
	    fprintf(stderr, "proto %s\n", proto[0].c_str());
	    arg_possibilities = &(loaded_context->rc_possibilities[proto[0]]);
	    fprintf(stderr, "ag %p %d\n",
		    arg_possibilities,
		    (int)arg_possibilities->size());
	}
    }

    retval = rl_completion_matches(text, completion_generator);
    if (retval == NULL) {
	rl_attempted_completion_over = 1;
    }

    return retval;
}

readline_curses::readline_curses()
    : rc_active_context(-1),
      rc_child(-1)
{
    struct winsize ws;
    int            sp[2];

    if (socketpair(PF_UNIX, SOCK_DGRAM, 0, sp) < 0) {
	throw error(errno);
    }

    this->rc_command_pipe[RCF_MASTER] = sp[RCF_MASTER];
    this->rc_command_pipe[RCF_SLAVE]  = sp[RCF_SLAVE];

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
	throw error(errno);
    }

    if (openpty(this->rc_pty[RCF_MASTER].out(),
		this->rc_pty[RCF_SLAVE].out(),
		NULL,
		NULL,
		&ws) < 0) {
	throw error(errno);
    }

    if ((this->rc_child = fork()) == -1) {
	throw error(errno);
    }

    if (this->rc_child == 0) {
	char buffer[1024];
	
	this->rc_command_pipe[RCF_MASTER].reset();
	this->rc_pty[RCF_MASTER].reset();

	signal(SIGALRM, sigalrm);
	signal(SIGWINCH, sigwinch);
	signal(SIGINT, sigterm);
	signal(SIGTERM, sigterm);

	dup2(this->rc_pty[RCF_SLAVE], STDIN_FILENO);
	dup2(this->rc_pty[RCF_SLAVE], STDOUT_FILENO);

	setenv("TERM", "vt52", 1);

	rl_initialize();
	using_history();
	stifle_history(HISTORY_SIZE);
	
	/*
	 * XXX Need to keep the input on a single line since the display screws
	 * up if it wraps around.
	 */
	strcpy(buffer, "set horizontal-scroll-mode on");
	rl_parse_and_bind(buffer); // NOTE: buffer is modified

	child_this = this;
    }
    else {
	this->rc_command_pipe[RCF_SLAVE].reset();
	this->rc_pty[RCF_SLAVE].reset();
    }
}

readline_curses::~readline_curses()
{
    if (this->rc_child == 0) {
	exit(0);
    }
    else if (this->rc_child > 0) {
	int status;

	kill(this->rc_child, SIGTERM);
	this->rc_child = -1;

	while (wait(&status) < 0 && (errno == EINTR)) {
	    ;
	}
    }
}

void readline_curses::start(void)
{
    if (this->rc_child != 0) {
	return;
    }

    map<int, readline_context *>::iterator current_context;
    fd_set rfds;
    int    maxfd;

    assert(!this->rc_contexts.empty());

    rl_completer_word_break_characters = (char *)" \t\n"; /* XXX */

    current_context = this->rc_contexts.end();

    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    FD_SET(this->rc_command_pipe[RCF_SLAVE], &rfds);

    maxfd = max(STDIN_FILENO, this->rc_command_pipe[RCF_SLAVE].get());

    while (looping) {
	fd_set ready_rfds = rfds;
	int    rc;

	rc = select(maxfd + 1, &ready_rfds, NULL, NULL, NULL);
	if (rc < 0) {
	    switch (errno) {
	    case EINTR:
		break;
	    }
	}
	else {
	    if (FD_ISSET(STDIN_FILENO, &ready_rfds)) {
		struct itimerval itv;

		assert(current_context != this->rc_contexts.end());
		
		itv.it_value.tv_sec     = 0;
		itv.it_value.tv_usec    = KEY_TIMEOUT;
		itv.it_interval.tv_sec  = 0;
		itv.it_interval.tv_usec = 0;
		setitimer(ITIMER_REAL, &itv, NULL);

		rl_callback_read_char();
		if (RL_ISSTATE(RL_STATE_DONE) && !got_line) {
		    got_line = 1;
		    this->line_ready("");
		    rl_callback_handler_remove();
		}
		/* fprintf(stderr, " is done: %d\n", RL_ISSTATE(RL_STATE_DONE)); */
	    }
	    if (FD_ISSET(this->rc_command_pipe[RCF_SLAVE], &ready_rfds)) {
		char msg[1024 + 1];

		/* fprintf(stderr, "rl cmd\n"); */
		if ((rc = read(this->rc_command_pipe[RCF_SLAVE],
			       msg,
			       sizeof(msg) - 1)) < 0) {
		    fprintf(stderr, "read\n");
		}
		else {
		    int  context, prompt_start = 0;
		    char type[32];

		    fprintf(stderr, "msg: %s\n", msg);
		    msg[rc] = '\0';
		    if (sscanf(msg, "f:%d:%n", &context, &prompt_start) == 1 &&
			prompt_start != 0 &&
			(current_context = this->rc_contexts.find(context)) !=
			this->rc_contexts.end()) {
			current_context->second->load();
			rl_callback_handler_install(&msg[prompt_start],
						    line_ready_tramp);
		    }
		    else if (sscanf(msg,
				    "ap:%d:%31[^:]:%n",
				    &context,
				    type,
				    &prompt_start) == 2) {
			assert(this->rc_contexts[context] != NULL);

			this->rc_contexts[context]->
			add_possibility(string(type),
					string(&msg[prompt_start]));
		    }
		    else if (sscanf(msg,
				    "rp:%d:%31[^:]:%n",
				    &context,
				    type,
				    &prompt_start) == 2) {
			assert(this->rc_contexts[context] != NULL);

			this->rc_contexts[context]->
			rem_possibility(string(type),
					string(&msg[prompt_start]));
		    }
		    else {
			fprintf(stderr, "unhandled message: %s\n", msg);
		    }
		}
	    }
	}

	if (got_timeout) {
	    char msg[1024];

	    fprintf(stderr, "got timeout\n");
	    got_timeout = 0;
	    snprintf(msg, sizeof(msg), "t:%s", rl_line_buffer);
	    if (reliable_send(this->rc_command_pipe[RCF_SLAVE],
			      msg,
			      strlen(msg)) == -1) {
		perror("got_timeout: write failed");
		exit(1);
	    }
	}
	if (got_line) {
	    struct itimerval itv;

	    got_line                = 0;
	    itv.it_value.tv_sec     = 0;
	    itv.it_value.tv_usec    = 0;
	    itv.it_interval.tv_sec  = 0;
	    itv.it_interval.tv_usec = 0;
	    if (setitimer(ITIMER_REAL, &itv, NULL) < 0) {
		fprintf(stderr, "setitimer");
	    }
	    current_context->second->save();
	    current_context = this->rc_contexts.end();
	}
	if (got_winch) {
	    struct winsize ws;

	    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
		throw error(errno);
	    }
	    fprintf(stderr, "rl winched %d %d\n", ws.ws_row, ws.ws_col);
	    got_winch = 0;
	    rl_set_screen_size(ws.ws_row, ws.ws_col);
	}
    }

    fprintf(stderr, "writing history\n");
    std::map<int, readline_context *>::iterator citer;
    for (citer = this->rc_contexts.begin();
	 citer != this->rc_contexts.end();
	 ++citer) {
	char *home;
	
	citer->second->load();
	home = getenv("HOME");
	if (home) {
	    char hpath[2048];
	    
	    snprintf(hpath, sizeof(hpath),
		     "%s/.lnav/%s.history",
		     home, citer->second->get_name().c_str());
	    write_history(hpath);
	}
	citer->second->save();
    }

    exit(0);
}

void readline_curses::line_ready(const char *line)
{
    auto_mem<char> expanded;
    char msg[1024];
    int  rc;

    rc = history_expand(rl_line_buffer, expanded.out());
    switch (rc) {
#if 0
      // TODO: fix clash between history and pcre metacharacters
    case - 1:
	/* XXX */
	snprintf(msg, sizeof(msg),
		 "e:unable to expand history -- %s",
		 expanded.in());
	break;
#endif

    case -1:
      snprintf(msg, sizeof(msg), "d:%s", line);
      break;
    case 0:
    case 1:
    case 2: /* XXX */
	snprintf(msg, sizeof(msg), "d:%s", expanded.in());
	break;
    }
    if (reliable_send(this->rc_command_pipe[RCF_SLAVE],
		      msg,
		      strlen(msg)) == -1) {
	perror("line_ready: write failed");
	exit(1);
    }
}

void readline_curses::check_fd_set(fd_set &ready_rfds)
{
    int rc;

    if (FD_ISSET(this->rc_pty[RCF_MASTER], &ready_rfds)) {
	char buffer[128];

	rc = read(this->rc_pty[RCF_MASTER], buffer, sizeof(buffer));
	if (rc > 0) {
	    {
		int lpc;
		
		fprintf(stderr, "from child %d|", rc);
		for (lpc = 0; lpc < rc; lpc++) {
		    fprintf(stderr, " %d", buffer[lpc]);
		}
		fprintf(stderr, "\n");
	    }
	    
	    this->map_output(buffer, rc);
	}
    }
    if (FD_ISSET(this->rc_command_pipe[RCF_MASTER], &ready_rfds)) {
	char msg[1024 + 1];

	rc = read(this->rc_command_pipe[RCF_MASTER], msg, sizeof(msg) - 1);
	if (rc >= 2) {
	    string old_value = this->rc_value;

	    msg[rc] = '\0';
	    fprintf(stderr, "child command: %s\n", msg);
	    this->rc_value = string(&msg[2]);
	    switch (msg[0]) {
	    case 't':
		if (this->rc_value != old_value) {
		    this->rc_timeout.invoke(this);
		}
		break;

	    case 'd':
		fprintf(stderr, "done!\n");
		this->rc_active_context = -1;
		this->vc_past_lines.clear();
		this->rc_perform.invoke(this);
		break;
	    }
	}
    }
}

void readline_curses::handle_key(int ch)
{
    const char *bch;
    int        len;

    bch = this->map_input(ch, len);
    if (write(this->rc_pty[RCF_MASTER], bch, len) == -1) {
	perror("handle_key: write failed");
    }
    fprintf(stderr, "to child %d\n", bch[0]);
    if (ch == '\t' || ch == '\r') {
	this->vc_past_lines.clear();
    }
}

void readline_curses::focus(int context, const char *prompt)
{
    char buffer[1024];

    curs_set(1);

    this->rc_active_context = context;

    snprintf(buffer, sizeof(buffer), "f:%d:%s", context, prompt);
    if (reliable_send(this->rc_command_pipe[RCF_MASTER],
	      buffer,
	      strlen(buffer) + 1) == -1) {
	perror("focus: write failed");
    }
    wmove(this->vc_window, this->get_actual_y(), 0);
    wclrtoeol(this->vc_window);
}

void readline_curses::add_possibility(int context, string type, string value)
{
    char buffer[1024];

    snprintf(buffer, sizeof(buffer),
	     "ap:%d:%s:%s",
	     context, type.c_str(), value.c_str());
    fprintf(stderr, "msg: %s\n", buffer);
    if (reliable_send(this->rc_command_pipe[RCF_MASTER],
		      buffer,
		      strlen(buffer) + 1) == -1) {
	perror("add_possibility: write failed");
    }
}

void readline_curses::rem_possibility(int context, string type, string value)
{
    char buffer[1024];

    snprintf(buffer, sizeof(buffer),
	     "rp:%d:%s:%s",
	     context, type.c_str(), value.c_str());
    if (reliable_send(this->rc_command_pipe[RCF_MASTER],
		      buffer,
		      strlen(buffer) + 1) == -1) {
	perror("rem_possiblity: write failed");
    }
}

void readline_curses::do_update(void)
{
    if (this->rc_active_context == -1) {
	mvwprintw(this->vc_window, this->get_actual_y(), 0,
		  "%s",
		  this->rc_value.c_str());
	wclrtoeol(this->vc_window);
	this->set_x(0);
    }
    vt52_curses::do_update();
}
