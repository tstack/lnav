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
 * @file statusview_curses.cc
 */

#include "config.h"

#include "statusview_curses.hh"

using namespace std;

void statusview_curses::do_update(void)
{
    int           top, attrs, field, field_count, left = 1, right;
    view_colors & vc = view_colors::singleton();
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
        status_field &sf     = this->sc_source->statusview_value_for_field(
            field);
        struct line_range lr = { 0, sf.get_width() };
        attr_line_t       val;
        int x;

        val = sf.get_value();

        if (sf.is_right_justified()) {
            right -= 1 + sf.get_width();
            x      = right;
        }
        else {
            x     = left;
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
