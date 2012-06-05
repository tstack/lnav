
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "view_curses.hh"

class test_colors : public view_curses {

public:
	test_colors() 
		: tc_window(NULL) {

	}

	void do_update(void) {
		int lpc;

		for (lpc = 0; lpc < 16; lpc++) {
			char label[64];
			attr_line_t al;
			line_range lr;

			snprintf(label, sizeof(label), "This is line: %d", lpc);
			al = label;
			lr.lr_start = 0;
			lr.lr_end = 40;
			this->mvwattrline(this->tc_window,
			                 lpc,
			                 0,
			                 al,
			                 lr,
			                 view_colors::singleton().next_highlight());
		}
	};

	WINDOW *tc_window;
};

int main(int argc, char *argv[])
{
	int c, retval = EXIT_SUCCESS;
	bool wait_for_input = false;
	WINDOW *win;
	test_colors tc;

	win = initscr();
	noecho();

	while ((c = getopt(argc, argv, "w")) != -1) {
		switch (c) {
			case 'w':
			wait_for_input = true;
			break;
		}
	}

	view_colors::singleton().init();
	tc.tc_window = win;
	tc.do_update();
	refresh();
	if (wait_for_input) {
		getch();
	}
	endwin();

	return retval;
}
