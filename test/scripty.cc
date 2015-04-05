/**
 * Copyright (c) 2007-2012, Timothy Stack
 *
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * * Neither the name of Timothy Stack nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <signal.h>

#if defined HAVE_NCURSESW_CURSES_H
#  include <ncursesw/curses.h>
#elif defined HAVE_NCURSESW_H
#  include <ncursesw.h>
#elif defined HAVE_NCURSES_CURSES_H
#  include <ncurses/curses.h>
#elif defined HAVE_NCURSES_H
#  include <ncurses.h>
#elif defined HAVE_CURSES_H
#  include <curses.h>
#else
#  error "SysV or X/Open-compatible Curses header file required"
#endif

#ifdef HAVE_PTY_H
#include <pty.h>
#endif

#ifdef HAVE_UTIL_H
#include <util.h>
#endif

#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif

#include <queue>
#include <algorithm>

#include "auto_fd.hh"

using namespace std;

/**
 * An RAII class for opening a PTY and forking a child process.
 */
class child_term {

public:
    class error : public std::exception {
    public:
	error(int err) : e_err(err) { };

	int e_err;
    };
    
    child_term() {
	struct winsize ws;
	auto_fd slave;

	memset(&ws, 0, sizeof(ws));

	if (isatty(STDIN_FILENO) &&
	    tcgetattr(STDIN_FILENO, &this->ct_termios) == -1) {
	    throw error(errno);
	}

	if (isatty(STDOUT_FILENO) &&
	    ioctl(STDOUT_FILENO, TIOCGWINSZ, &this->ct_winsize) == -1) {
	    throw error(errno);
	}

	ws.ws_col = 80;
	ws.ws_row = 24;
	
	if (openpty(this->ct_master.out(),
		    slave.out(),
		    NULL,
		    NULL,
		    &ws) < 0) {
	    throw error(errno);
	}

	if ((this->ct_child = fork()) == -1)
	    throw error(errno);

	if (this->ct_child == 0) {
	    this->ct_master.reset();
	    
	    dup2(slave, STDIN_FILENO);
	    dup2(slave, STDOUT_FILENO);

	    setenv("TERM", "xterm-color", 1);
	}
	else {
	    slave.reset();
	}
    };
    
    virtual ~child_term() {
	(void)this->wait_for_child();

	if (isatty(STDIN_FILENO) &&
	    tcsetattr(STDIN_FILENO, TCSANOW, &this->ct_termios) == -1) {
	    perror("tcsetattr");
	}
	if (isatty(STDOUT_FILENO) &&
	    ioctl(STDOUT_FILENO, TIOCSWINSZ, &this->ct_winsize) == -1) {
	    perror("ioctl");
	}
    };

    int wait_for_child(void) {
	int retval = -1;

	if (this->ct_child > 0) {
	    kill(this->ct_child, SIGTERM);
	    this->ct_child = -1;
	    
	    while (wait(&retval) < 0 && (errno == EINTR));
	}

	return retval;
    };

    bool is_child() { return this->ct_child == 0; };
    pid_t get_child_pid() { return this->ct_child; };

    int get_fd() { return this->ct_master; };
    
protected:
    pid_t ct_child;
    auto_fd ct_master;
    struct termios ct_termios;
    struct winsize ct_winsize;
    
};

/**
 * @param fd The file descriptor to switch to raw mode.
 * @return Zero on success, -1 on error.
 */
static int tty_raw(int fd)
{
    struct termios attr[1];

    assert(fd >= 0);
    
    if (tcgetattr(fd, attr) == -1)
	return -1;
    
    attr->c_lflag &= ~(ECHO | ICANON | IEXTEN);
    attr->c_iflag &= ~(ICRNL | INPCK | ISTRIP | IXON);
    attr->c_cflag &= ~(CSIZE | PARENB);
    attr->c_cflag |= (CS8);
    attr->c_oflag &= ~(OPOST);
    attr->c_cc[VMIN] = 1;
    attr->c_cc[VTIME] = 0;
    
    return tcsetattr(fd, TCSANOW, attr);
}

static void dump_memory(FILE *dst, const char *src, int len)
{
    int lpc;

    for (lpc = 0; lpc < len; lpc++) {
	fprintf(dst, "%02x", src[lpc]);
    }
}

static char *hex2bits(const char *src)
{
    int len, pos = sizeof(int);
    char *retval;

    len = strlen(src) / 2;
    retval = new char[sizeof(uint32_t) + len];
    *((uint32_t *)retval) = len;
    while ((size_t)pos < (sizeof(uint32_t) + len)) {
	int val;
	
	sscanf(src, "%2x", &val);
	src += 2;
	retval[pos] = (char)val;
	pos += 1;
    }

    return retval;
}

typedef enum {
    ET_NONE,
    ET_READ,
} expect_type_t;

struct expect_read {
	uint32_t er_length;
	char er_data[];
};

struct expect {
    expect_type_t e_type;
    union {
    	struct expect_read *read;
    } e_arg;
};

struct expect_read_state {
	uint32_t ers_pos;
};

class expect_handler {
public:
	expect_handler() {
		memset(&this->eh_state, 0, sizeof(this->eh_state));
	};

	int process_input(const char *buffer, size_t blen) {
		if (this->eh_queue.empty())
			return 0;

		uint32_t &exp_pos = this->eh_state.es_read.ers_pos;
		struct expect &next = this->eh_queue.front();
		int cmp_len = min((next.e_arg.read->er_length - exp_pos), (uint32_t)blen);
		char *exp_start = &next.e_arg.read->er_data[this->eh_state.es_read.ers_pos];
		int retval = 0;

		assert(buffer != NULL || blen == 0);

		if (memcmp(exp_start, buffer, cmp_len) == 0) {
			exp_pos += cmp_len;
			if (exp_pos == next.e_arg.read->er_length) {
				retval = 1;
				if (!this->eh_queue.empty()) {
					exp_pos = 0;
					this->eh_queue.pop();
				}
			}
		}
		else {
			printf("Detected output differences at offset %d, "
			       "expecting:\n  ", exp_pos);
			dump_memory(stdout, exp_start, cmp_len);
			printf("\nGot:\n  ");
			dump_memory(stdout, buffer, cmp_len);
			retval = -1;
		}

		fprintf(stderr, "pi ret %d\n", retval);

		return retval;
	};

	queue<struct expect> eh_queue;
private:
	union {
		struct expect_read_state es_read;
	} eh_state;
};

typedef enum {
    CT_SLEEP,
    CT_WRITE,
} command_type_t;

struct command {
    command_type_t c_type;
    union {
	char *b;
    } c_arg;
};

static struct {
    const char *sd_program_name;
    sig_atomic_t sd_looping;

    pid_t sd_child_pid;
    
    const char *sd_to_child_name;
    FILE *sd_to_child;
    
    const char *sd_from_child_name;
    FILE *sd_from_child;

    queue<struct command> sd_replay;

    bool sd_user_step;
} scripty_data;

static void sigchld(int sig)
{
}

static void sigpass(int sig)
{
    kill(scripty_data.sd_child_pid, sig);
}

static void usage(void)
{
    const char *usage_msg =
	"usage: %s [-h] [-t to_child] [-f from_child] -- <cmd>\n"
	"\n"
        "Recorder for TTY I/O from a child process."
        "\n"
        "Options:\n"
        "  -h         Print this message, then exit.\n"
        "  -t <file>  The file where any input sent to the child process\n"
        "             should be stored.\n"
        "  -f <file>  The file where any output from the child process\n"
        "             should be stored.\n"
        "  -r <file>  The file containing the input to be sent to the child\n"
        "             process.\n"
        "  -e <file>  The file containing the expected output from the child\n"
        "             process.\n"
        "\n"
        "Examples:\n"
        "  To record a session for playback later:\n"
        "    $ scripty -t input.0 -f output.0 -- myCursesApp\n"
        "\n"
        "  To replay the recorded session:\n"
        "    $ scripty -r input.0 -- myCursesApp\n";
    
    fprintf(stderr, usage_msg, scripty_data.sd_program_name);
}

int main(int argc, char *argv[])
{
    int c, fd, retval = EXIT_SUCCESS;
    expect_handler ex_handler;
    bool passout = true;
    FILE *file;

    scripty_data.sd_program_name = argv[0];
    scripty_data.sd_looping = true;
    
    while ((c = getopt(argc, argv, "ht:f:r:e:ns")) != -1) {
	switch (c) {
	case 'h':
	    usage();
	    exit(retval);
	    break;
	case 's':
	    scripty_data.sd_user_step = true;
	    break;
	case 't':
	    scripty_data.sd_to_child_name = optarg;
	    break;
	case 'f':
	    scripty_data.sd_from_child_name = optarg;
	    break;
	case 'e':
	    if ((file = fopen(optarg, "r")) == NULL) {
		fprintf(stderr, "error: cannot open %s\n", optarg);
		retval = EXIT_FAILURE;
	    }
	    else {
		char line[32 * 1024];

		while (fgets(line, sizeof(line), file)) {
		    char *sp;

		    if (line[0] == '#' || (sp = strchr(line, ' ')) == NULL) {
		    }
		    else {
			struct expect exp;
			
			*sp = '\0';
			sp += 1;
			if (strcmp(line, "read") == 0) {
			    exp.e_type = ET_READ;
			    exp.e_arg.read = (struct expect_read *)hex2bits(sp);
			}
			else {
			    fprintf(stderr,
				    "error: unknown command -- %s\n",
				    line);
			    retval = EXIT_FAILURE;
			}
			ex_handler.eh_queue.push(exp);
		    }
		}
		fclose(file);
		file = NULL;
	    }
	    break;
	case 'r':
	    if ((file = fopen(optarg, "r")) == NULL) {
		fprintf(stderr, "error: cannot open %s\n", optarg);
		retval = EXIT_FAILURE;
	    }
	    else {
		char line[32 * 1024];

		while (fgets(line, sizeof(line), file)) {
		    char *sp;

		    if (line[0] == '#' || (sp = strchr(line, ' ')) == NULL) {
		    }
		    else {
			struct command cmd;
			
			*sp = '\0';
			sp += 1;
			if (strcmp(line, "sleep") == 0) {
			    cmd.c_type = CT_SLEEP;
			}
			else if (strcmp(line, "write") == 0) {
			    cmd.c_type = CT_WRITE;
			    cmd.c_arg.b = hex2bits(sp);
			    scripty_data.sd_replay.push(cmd);
			}
			else {
			    fprintf(stderr,
				    "error: unknown command -- %s\n",
				    line);
			    retval = EXIT_FAILURE;
			}
		    }
		}
		fclose(file);
		file = NULL;
	    }
	    break;
	case 'n':
	    passout = false;
	    break;
	default:
	    retval = EXIT_FAILURE;
	    break;
	}
    }

    argc -= optind;
    argv += optind;

    if ((scripty_data.sd_to_child_name != NULL) &&
	(scripty_data.sd_to_child =
	 fopen(scripty_data.sd_to_child_name, "w")) == NULL) {
	fprintf(stderr,
		"error: unable to open %s -- %s\n",
		scripty_data.sd_to_child_name,
		strerror(errno));
	retval = EXIT_FAILURE;
    }

    if (scripty_data.sd_from_child_name != NULL) {
	if (strcmp(scripty_data.sd_from_child_name, "-") == 0) {
	    scripty_data.sd_from_child = stdout;
	}
	else if ((scripty_data.sd_from_child =
		  fopen(scripty_data.sd_from_child_name, "w")) == NULL) {
	    fprintf(stderr,
		    "error: unable from open %s -- %s\n",
		    scripty_data.sd_from_child_name,
		    strerror(errno));
	    retval = EXIT_FAILURE;
	}
    }

    fd = open("/tmp/scripty.err", O_WRONLY|O_CREAT|O_APPEND, 0666);
    dup2(fd, STDERR_FILENO);
    close(fd);
    fprintf(stderr, "startup\n");

    if (scripty_data.sd_to_child != NULL)
	fcntl(fileno(scripty_data.sd_to_child), F_SETFD, 1);
    if (scripty_data.sd_from_child != NULL)
	fcntl(fileno(scripty_data.sd_from_child), F_SETFD, 1);
    
    if (retval != EXIT_FAILURE) {
	child_term ct;

	if (ct.is_child()) {
	    execvp(argv[0], argv);
	    perror("execvp");
	    exit(-1);
	}
	else {
	    int maxfd, out_len = 0;
	    bool got_expected = true;
	    bool got_user_step;
	    struct timeval last, now;
	    char out_buffer[8192];
	    fd_set read_fds;

	    scripty_data.sd_child_pid = ct.get_child_pid();
	    signal(SIGINT, sigpass);
	    signal(SIGTERM, sigpass);
	    
	    signal(SIGCHLD, sigchld);
	    
	    gettimeofday(&now, NULL);
	    last = now;
	    
	    FD_ZERO(&read_fds);
	    FD_SET(STDIN_FILENO, &read_fds);
	    FD_SET(ct.get_fd(), &read_fds);

	    fprintf(stderr, "goin in the loop\n");

	    tty_raw(STDIN_FILENO);
	    
	    if (!ex_handler.eh_queue.empty()) {
		got_expected = false;
	    }
	    
	    maxfd = max(STDIN_FILENO, ct.get_fd());
	    while (scripty_data.sd_looping) {
		fd_set ready_rfds = read_fds;
		struct timeval diff, to;
		int rc;

		to.tv_sec = 0;
		to.tv_usec = 10000;
		rc = select(maxfd + 1, &ready_rfds, NULL, NULL, &to);
		if (rc == 0) {
		    if (!got_expected) {
		    switch (ex_handler.process_input(NULL, 0)) {
	    	    case -1:
	    	        scripty_data.sd_looping = false;
		    	retval = EXIT_FAILURE;
		    	break;
		    case 0:
		        break;
		    case 1:
		        got_expected = true;
		        break;
		    }
		}
		    if (!scripty_data.sd_replay.empty() && got_expected &&
		        (!scripty_data.sd_user_step || got_user_step)) {
			struct command cmd = scripty_data.sd_replay.front();
			int len;

			fprintf(stderr, " us %d got %d\n", scripty_data.sd_user_step, got_user_step);
			scripty_data.sd_replay.pop();
			fprintf(stderr, "replay %zd\n", scripty_data.sd_replay.size());
			switch (cmd.c_type) {
			case CT_SLEEP:
			    break;
			case CT_WRITE:
			    len = *((int *)cmd.c_arg.b);
			    log_perror(write(ct.get_fd(),
				  cmd.c_arg.b + sizeof(int),
				  len));
			    delete [] cmd.c_arg.b;
			    break;
			}
			got_user_step = false;
			got_expected = false;
		    }
		}
		else if (rc < 0) {
		    switch (errno) {
		    case EINTR:
			break;
		    default:
			fprintf(stderr, "select %s\n", strerror(errno));
			scripty_data.sd_looping = false;
			break;
		    }
		}
		else {
		    char buffer[1024];

		    fprintf(stderr, "fds ready %d\n", rc);
		    gettimeofday(&now, NULL);
		    timersub(&now, &last, &diff);
		    if (FD_ISSET(STDIN_FILENO, &ready_rfds)) {
			rc = read(STDIN_FILENO, buffer, sizeof(buffer));
			if (rc < 0) {
			    scripty_data.sd_looping = false;
			}
			else if (rc == 0) {
			  FD_CLR(STDIN_FILENO, &read_fds);
			}
			else if (!scripty_data.sd_replay.empty()) {
				if (scripty_data.sd_user_step) {
					got_user_step = true;
				}
			}
			else {
			    log_perror(write(ct.get_fd(), buffer, rc));
			    if (scripty_data.sd_to_child != NULL) {
				fprintf(scripty_data.sd_to_child,
					"sleep %ld.%06ld\n"
					"write ",
					(long)diff.tv_sec, (long)diff.tv_usec);
				dump_memory(scripty_data.sd_to_child,
					    buffer,
					    rc);
				fprintf(scripty_data.sd_to_child, "\n");
			    }
			    if (scripty_data.sd_from_child != NULL) {
				fprintf(stderr, "do write %d\n", out_len);
				fprintf(scripty_data.sd_from_child, "read ");
				dump_memory(scripty_data.sd_from_child,
					    out_buffer,
					    out_len);
				fprintf(scripty_data.sd_from_child, "\n");
				fprintf(scripty_data.sd_from_child,
					"# write ");
				dump_memory(scripty_data.sd_from_child,
					    buffer,
					    rc);
				fprintf(scripty_data.sd_from_child, "\n");
				out_len = 0;
			    }
			}
		    }
		    if (FD_ISSET(ct.get_fd(), &ready_rfds)) {
			rc = read(ct.get_fd(), buffer, sizeof(buffer));
			fprintf(stderr, "read rc %d\n", rc);
			if (rc <= 0) {
			    scripty_data.sd_looping = false;
			    if (scripty_data.sd_from_child) {
				fprintf(scripty_data.sd_from_child, "read ");
				dump_memory(scripty_data.sd_from_child,
					    out_buffer,
					    out_len);
				fprintf(scripty_data.sd_from_child, "\n");
				out_len = 0;
			    }
			}
			else {
			    if (passout)
				log_perror(write(STDOUT_FILENO, buffer, rc));
			    if (scripty_data.sd_from_child != NULL) {
				fprintf(stderr, "got out %d\n", rc);
				memcpy(&out_buffer[out_len],
				       buffer,
				       rc);
				out_len += rc;
			    }
			    switch (ex_handler.process_input(buffer, rc)) {
		    	    case -1:
		    	        scripty_data.sd_looping = false;
			    	retval = EXIT_FAILURE;
			    	break;
			    case 0:
			        break;
			    case 1:
			        got_expected = true;
			        break;
			    }
			}
		    }
		}
		last = now;
	    }
	}

	if (!ex_handler.eh_queue.empty()) {
		fprintf(stderr, "More input expected from child\n");
	    retval = EXIT_FAILURE;
	}

	retval = ct.wait_for_child() || retval;
    }

    if (scripty_data.sd_to_child != NULL) {
	fclose(scripty_data.sd_to_child);
	scripty_data.sd_to_child = NULL;
    }

    if (scripty_data.sd_from_child != NULL) {
	fclose(scripty_data.sd_from_child);
	scripty_data.sd_from_child = NULL;
    }

    return retval;
}
