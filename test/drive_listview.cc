
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "listview_curses.hh"

using namespace std;

static listview_curses lv;

class my_source : public list_data_source {

public:

    my_source() : ms_rows(2) { };

    size_t listview_rows(const listview_curses &lv) {
	return this->ms_rows;
    };

    void listview_value_for_row(const listview_curses &lv,
				vis_line_t row,
				attr_line_t &value_out) {
	if (row == 0) {
	    value_out = "Hello";
	}
	else if (row == 1) {
	    value_out = "World!";
	}
	else if (row < this->ms_rows) {
	    char buffer[32];

	    snprintf(buffer, sizeof(buffer), "%d", (int)row);
	    value_out = string(buffer);
	}
	else {
	    assert(0);
	}
    };

    bool attrline_next_token(const view_curses &vc,
			     int line,
			     struct line_range &lr,
			     int &attrs_out) {
	return false;
    };

    int ms_rows;
    
};

int main(int argc, char *argv[])
{
    int c, retval = EXIT_SUCCESS;
    bool wait_for_input = false;
    my_source ms;
    WINDOW *win;

    win = initscr();
    lv.set_data_source(&ms);
    lv.set_window(win);
    noecho();

    while ((c = getopt(argc, argv, "y:t:l:r:h:w")) != -1) {
	switch (c) {
	case 'y':
	    lv.set_y(atoi(optarg));
	    break;
	case 'h':
	    lv.set_height(vis_line_t(atoi(optarg)));
	    break;
	case 't':
	    lv.set_top(vis_line_t(atoi(optarg)));
	    break;
	case 'l':
	    lv.set_left(atoi(optarg));
	    break;
	case 'w':
	    wait_for_input = true;
	    break;
	case 'r':
	    ms.ms_rows = atoi(optarg);
	    break;
	}
    }

    lv.do_update();
    refresh();
    if (wait_for_input)
	getch();
    endwin();

    return retval;
}
