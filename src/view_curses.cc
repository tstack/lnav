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
 *
 * @file view_curses.cc
 */

#include "config.h"

#include <string>
#include <algorithm>

#include "view_curses.hh"

using namespace std;

void view_curses::mvwattrline(WINDOW *window,
			      int y,
			      int x,
			      attr_line_t &al,
			      struct line_range &lr,
			      view_colors::role_t base_role)
{
    int    text_attrs, attrs, line_width;
    string_attrs_t &sa = al.get_attrs();
    string &line = al.get_string();
    string_attrs_t::iterator iter;
    std::map<size_t, size_t, std::greater<size_t> > tab_list;
    int tab_count = 0;
    char *buffer, *expanded_line;
    size_t exp_index = 0;
    string full_line;

    assert(lr.lr_end != -1);

    line_width = lr.length();
    buffer = (char *)alloca(line_width + 1);
    tab_count = count(line.begin(), line.end(), '\t');
    expanded_line = (char *)alloca(line.size() + tab_count * 8 + 1);

    for (size_t lpc = 0; lpc < line.size(); lpc++) {
        switch (line[lpc]) {
        case '\t':
                do {
                        expanded_line[exp_index] = ' ';
                        exp_index += 1;
                } while (exp_index % 8);
                tab_list[lpc] = exp_index;
                break;
        case '\r':
                // exp_index = -1;
                break;
        default:
                expanded_line[exp_index] = line[lpc];
                exp_index += 1;
                break;
        }
    }

    expanded_line[exp_index] = '\0';
    full_line = string(expanded_line);

    text_attrs = view_colors::singleton().attrs_for_role(base_role);
    attrs = text_attrs;
    wmove(window, y, x);
    wattron(window, attrs);
    if (lr.lr_start < (int)full_line.size()) {
	waddnstr(window, &full_line.c_str()[lr.lr_start], line_width);
    }
    if (lr.lr_end > (int)full_line.size())
	whline(window, ' ', lr.lr_end - full_line.size());
    wattroff(window, attrs);

    for (iter = sa.begin(); iter != sa.end(); ++iter) {
	struct line_range attr_range = iter->first;
	std::map<size_t, size_t>::iterator tab_iter;
	
	assert(attr_range.lr_start >= 0);
	assert(attr_range.lr_end >= -1);

        tab_iter = tab_list.lower_bound(attr_range.lr_start);
        if (tab_iter != tab_list.end())
        	attr_range.lr_start += (tab_iter->second - tab_iter->first) - 1;

	if (attr_range.lr_end != -1) {
	    tab_iter = tab_list.lower_bound(attr_range.lr_end);
            if (tab_iter != tab_list.end())
        	    attr_range.lr_end += (tab_iter->second - tab_iter->first) - 1;
	}
	
	attr_range.lr_start = max(0, attr_range.lr_start - lr.lr_start);
	if (attr_range.lr_end == -1) {
	    attr_range.lr_end = line_width;
	}
	else {
	    attr_range.lr_end = min((int)line_width,
				    attr_range.lr_end - lr.lr_start);
	}

	if (attr_range.lr_end > 0) {
	    int awidth = attr_range.length();
	    attrs_map_t &am = iter->second;
	    attrs_map_t::iterator am_iter;

	    attrs = 0;
	    for (am_iter = am.begin(); am_iter != am.end(); ++am_iter) {
		if (am_iter->first == "style") {
		    attrs |= am_iter->second.sa_int;
		}
	    }
	    
	    /* This silliness is brought to you by a buggy old curses lib. */
	    mvwinnstr(window, y, x + attr_range.lr_start, buffer, awidth);
	    wattron(window, attrs);
	    mvwaddnstr(window, y, x + attr_range.lr_start, buffer, awidth);
	    wattroff(window, attrs);
	}
	
	attrs = text_attrs; /* Reset attrs to regular text. */
    }
}

view_colors &view_colors::singleton(void)
{
    static view_colors s_vc;

    return s_vc;
}

view_colors::view_colors()
    : vc_next_highlight(0)
{
    int lpc;

    /* Setup the mappings from roles to actual colors. */
    this->vc_role_colors[VCR_TEXT]   = COLOR_PAIR(VC_WHITE) | A_DIM;
    this->vc_role_colors[VCR_SEARCH] =
	this->vc_role_colors[VCR_TEXT] | A_REVERSE;
    this->vc_role_colors[VCR_OK] = COLOR_PAIR(VC_GREEN) | A_BOLD;
    this->vc_role_colors[VCR_ERROR]   = COLOR_PAIR(VC_RED) | A_BOLD;
    this->vc_role_colors[VCR_WARNING] = COLOR_PAIR(VC_YELLOW) | A_BOLD;
    this->vc_role_colors[VCR_ALT_ROW] = COLOR_PAIR(VC_WHITE) | A_BOLD;

    this->vc_role_colors[VCR_STATUS] =
	COLOR_PAIR(VC_BLACK_ON_WHITE);
    this->vc_role_colors[VCR_WARN_STATUS] =
	COLOR_PAIR(VC_YELLOW_ON_WHITE) | A_BOLD;
    this->vc_role_colors[VCR_ALERT_STATUS] =
	COLOR_PAIR(VC_RED_ON_WHITE);
    this->vc_role_colors[VCR_ACTIVE_STATUS] =
	COLOR_PAIR(VC_GREEN_ON_WHITE);
    this->vc_role_colors[VCR_ACTIVE_STATUS2] =
	COLOR_PAIR(VC_GREEN_ON_WHITE) | A_BOLD;

    this->vc_role_colors[VCR_DIFF_DELETE] = COLOR_PAIR(VC_RED);
    this->vc_role_colors[VCR_DIFF_ADD] = COLOR_PAIR(VC_GREEN);
    this->vc_role_colors[VCR_DIFF_SECTION] = COLOR_PAIR(VC_MAGENTA);

    for (lpc = 0; lpc < VCR__MAX; lpc++) {
	this->vc_role_reverse_colors[lpc] =
	    this->vc_role_colors[lpc] | A_REVERSE;
    }

    /*
     * Prime the highlight vector.  The first HL_COLOR_COUNT color pairs are
     * assumed to be the highlight colors.
     */
    for (lpc = 1; lpc <= HL_COLOR_COUNT; lpc++) {
	this->vc_role_colors[VCR__MAX + (lpc - 1) * 2]     = COLOR_PAIR(lpc);
	this->vc_role_colors[VCR__MAX + (lpc - 1) * 2 + 1] =
	    COLOR_PAIR(lpc) | A_BOLD;

	this->vc_role_reverse_colors[VCR__MAX + (lpc - 1) * 2] =
	    COLOR_PAIR(lpc + HL_COLOR_COUNT) | A_REVERSE;
	this->vc_role_reverse_colors[VCR__MAX + (lpc - 1) * 2 + 1] =
	    COLOR_PAIR(lpc) | A_BOLD | A_REVERSE;
    }
}

void view_colors::init(void)
{
    if (has_colors()) {
	start_color();

	/* use_default_colors(); */
	init_pair(VC_BLUE, COLOR_BLUE, COLOR_BLACK);
	init_pair(VC_CYAN, COLOR_CYAN, COLOR_BLACK);
	init_pair(VC_GREEN, COLOR_GREEN, COLOR_BLACK);
	init_pair(VC_MAGENTA, COLOR_MAGENTA, COLOR_BLACK);
	
	init_pair(VC_BLUE_ON_WHITE, COLOR_BLUE, COLOR_WHITE);
	init_pair(VC_CYAN_ON_BLACK, COLOR_CYAN, COLOR_BLACK);
	init_pair(VC_GREEN_ON_WHITE, COLOR_GREEN, COLOR_WHITE);
	init_pair(VC_MAGENTA_ON_WHITE, COLOR_MAGENTA, COLOR_WHITE);

	init_pair(VC_RED, COLOR_RED, COLOR_BLACK);
	init_pair(VC_YELLOW, COLOR_YELLOW, COLOR_BLACK);
	init_pair(VC_WHITE, COLOR_WHITE, COLOR_BLACK);

	init_pair(VC_BLACK_ON_WHITE, COLOR_BLACK, COLOR_WHITE);
	init_pair(VC_RED_ON_WHITE, COLOR_RED, COLOR_WHITE);
	init_pair(VC_YELLOW_ON_WHITE, COLOR_YELLOW, COLOR_WHITE);
	
	init_pair(VC_WHITE_ON_GREEN, COLOR_WHITE, COLOR_GREEN);
    }
}

view_colors::role_t view_colors::next_highlight(void)
{
    role_t retval = (role_t)(VCR__MAX + this->vc_next_highlight);

    this->vc_next_highlight = (this->vc_next_highlight + 1) %
			      (HL_COLOR_COUNT * 2);

    return retval;
}
