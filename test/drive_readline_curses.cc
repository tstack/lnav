
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

#include <algorithm>

#include "readline_curses.hh"

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
    int lpc, c, fd, maxfd, retval = EXIT_SUCCESS;

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
    
    readline_context context(&COMMANDS);
    readline_curses rlc;
    bool done = false;
    fd_set rfds;

    rlc.add_context(1, context);
    rlc.start();

    drive_data.dd_rl_view = &rlc;
    
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);

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
    maxfd = max(STDIN_FILENO, rlc.update_fd_set(rfds));

    drive_data.dd_looping = true;
    while (drive_data.dd_looping) {
	fd_set ready_rfds = rfds;
	int rc;

	rlc.do_update();
	refresh();
	rc = select(maxfd + 1, &ready_rfds, NULL, NULL, NULL);
	if (rc > 0) {
	    if (FD_ISSET(STDIN_FILENO, &ready_rfds)) {
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
	    rlc.check_fd_set(ready_rfds);
	}
    }
    
    return retval;
}
