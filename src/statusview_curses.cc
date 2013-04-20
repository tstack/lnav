/**
 * @file statusview_curses.cc
 */

#include "config.h"

#include "statusview_curses.hh"

using namespace std;

void statusview_curses::do_update(void)
{
    int           top, attrs, field, field_count, left = 1, right;
    view_colors &vc = view_colors::singleton();
    unsigned long width, height;

    getmaxyx(this->sc_window, height, width);
    top   = this->sc_top < 0 ? height + this->sc_top : this->sc_top;
    right = width - 2;
    attrs = vc.attrs_for_role(view_colors::VCR_STATUS);

    wattron(this->sc_window, attrs);
    wmove(this->sc_window, top, 0);
    wclrtoeol(this->sc_window);
    whline(this->sc_window, ' ', width);
    wattroff(this->sc_window, attrs);

    field_count = this->sc_source->statusview_fields();
    for (field = 0; field < field_count; field++) {
	status_field &sf = this->sc_source->statusview_value_for_field(field);
	struct line_range lr = { 0, sf.get_width() };
	attr_line_t val;
	int x;

	val = sf.get_value();
	if (sf.is_right_justified()) {
	    right -= 1 + sf.get_width();
	    x = right;
	}
	else {
	    x = left;
	    left += sf.get_width() + 1;
	}
	this->mvwattrline(this->sc_window,
			  top, x,
			  val,
			  lr,
			  sf.get_role());
    }
    wmove(this->sc_window, top + 1, 0);
}
