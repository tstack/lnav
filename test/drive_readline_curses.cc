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

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

#ifdef HAVE_SYS_TTYDEFAULTS_H
#include <sys/ttydefaults.h>
#endif

#include <algorithm>

#include "lnav_util.hh"
#include "readline_curses.hh"
#include "../src/lnav_util.hh"

using namespace std;

static readline_context::command_map_t COMMANDS;

static struct {
    bool dd_active;
    readline_curses *dd_rl_view;
    volatile sig_atomic_t dd_looping;
} drive_data;

static void rl_callback(void *dummy, readline_curses *rc)
{
    string line = rc->get_value();

    if (line == "quit")
	drive_data.dd_looping = false;
    fprintf(stderr, "callback\n");
    drive_data.dd_active = false;
}

static void rl_timeout(void *dummy, readline_curses *rc)
{
    fprintf(stderr, "timeout\n");
}

int main(int argc, char *argv[])
{
    int c, fd, retval = EXIT_SUCCESS;

    fd = open("/tmp/lnav.err", O_WRONLY|O_CREAT|O_APPEND, 0666);
    dup2(fd, STDERR_FILENO);
    close(fd);
    fprintf(stderr, "startup\n");
    
    while ((c = getopt(argc, argv, "h")) != -1) {
	switch (c) {
	case 'h':
	    break;
	default:
	    break;
	}
    }
    
    readline_context context("test", &COMMANDS);
    readline_curses rlc;

    rlc.add_context(1, context);
    rlc.start();

    drive_data.dd_rl_view = &rlc;

    screen_curses sc;
    
    keypad(stdscr, TRUE);
    nonl();
    cbreak();
    noecho();
    nodelay(sc.get_window(), 1);
    
    rlc.set_window(sc.get_window());
    rlc.set_y(-1);
    rlc.set_perform_action(readline_curses::action(rl_callback));
    rlc.set_timeout_action(readline_curses::action(rl_timeout));

    drive_data.dd_looping = true;
    while (drive_data.dd_looping) {
        vector<struct pollfd> pollfds;
	int rc;

        pollfds.push_back((struct pollfd) {
                STDIN_FILENO,
                POLLIN,
                0
        });
        rlc.update_poll_set(pollfds);

	rlc.do_update();
	refresh();
	rc = poll(&pollfds[0], pollfds.size(), -1);
	if (rc > 0) {
	    if (pollfd_ready(pollfds, STDIN_FILENO)) {
		int ch;
		
		while ((ch = getch()) != ERR) {
		    switch (ch) {
		    case CEOF:
		    case KEY_RESIZE:
			break;
			
		    default:
			if (drive_data.dd_active) {
			    rlc.handle_key(ch);
			}
			else if (ch == ':') {
			    rlc.focus(1, ":");
			    drive_data.dd_active = true;
			}
			break;
		    }
		}
	    }
	    rlc.check_poll_set(pollfds);
	}
    }
    
    return retval;
}
