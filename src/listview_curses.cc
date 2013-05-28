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
 * @file listview_curses.cc
 */

#include "config.h"

#include "listview_curses.hh"

using namespace std;

listview_curses::listview_curses()
    : lv_source(NULL),
      lv_window(NULL),
      lv_y(0),
      lv_top(0),
      lv_left(0),
      lv_height(0),
      lv_needs_update(true),
      lv_show_scrollbar(true)
{ }

listview_curses::~listview_curses()
{ }

void listview_curses::reload_data(void)
{
    if (this->lv_source == NULL) {
        this->lv_top  = vis_line_t(0);
        this->lv_left = 0;
    }
    else if (this->lv_top >= this->get_inner_height()) {
        this->lv_top = max(vis_line_t(0),
                           vis_line_t(this->get_inner_height() - 1));
    }
    this->lv_needs_update = true;
}

bool listview_curses::handle_key(int ch)
{
    vis_line_t height(0);

    unsigned long width;
    bool          retval = true;

    this->get_dimensions(height, width);
    switch (ch) {
    case 'l':
    case KEY_RIGHT:
        this->shift_left(width / 2);
        break;

    case 'h':
    case KEY_LEFT:
        this->shift_left(-(width / 2));
        break;

    case '\r':
    case 'j':
    case KEY_DOWN:
        this->shift_top(vis_line_t(1));
        break;

    case 'k':
    case KEY_UP:
        this->shift_top(vis_line_t(-1));
        break;

    case 'b':
    case KEY_BACKSPACE:
    case KEY_PPAGE:
        this->shift_top(-height);
        break;

    case ' ':
    case KEY_NPAGE:
        this->shift_top(height);
        break;

    case KEY_HOME:
        this->set_top(vis_line_t(0));
        break;

    case KEY_END:
    case 'B':
        this->set_top(max(vis_line_t(0),
                          max(this->lv_top,
                              vis_line_t(this->get_inner_height() - height +
                                         1))));
        break;

    default:
        retval = false;
        break;
    }

    return retval;
}

void listview_curses::do_update(void)
{
    if (this->lv_window != NULL && this->lv_needs_update) {
        vis_line_t        y(this->lv_y), height, bottom, lines, row;
        attr_line_t       overlay_line;
        vis_line_t        overlay_height(0);
        struct line_range lr;
        unsigned long     width;
        size_t            row_count;

        if (this->lv_overlay_source != NULL) {
            overlay_height = vis_line_t(
                this->lv_overlay_source->list_overlay_count(*this));
        }

        this->get_dimensions(height, width);
        lr.lr_start = this->lv_left;
        lr.lr_end   = this->lv_left + width;

        row_count = this->get_inner_height();
        if (this->lv_top >= (int)row_count) {
            this->lv_top = max(vis_line_t(0), vis_line_t(row_count) - height);
        }

        row   = this->lv_top;
        lines = min(height - overlay_height,
                    vis_line_t(row_count) - this->lv_top);
        bottom = y + height;
        for (; y < bottom; ++y) {
            if (this->lv_overlay_source != NULL &&
                this->lv_overlay_source->list_value_for_overlay(
                    *this,
                    y - vis_line_t(this->lv_y),
                    overlay_line)) {
                this->mvwattrline(this->lv_window, y, 0, overlay_line, lr);
                overlay_line.clear();
            }
            else if (lines > 0) {
                attr_line_t al;

                this->lv_source->listview_value_for_row(*this, row, al);
                this->mvwattrline(this->lv_window, y, 0, al, lr);
                --lines;
                ++row;
            }
            else {
                wmove(this->lv_window, y, 0);
                wclrtoeol(this->lv_window);
            }
        }

        if (this->lv_show_scrollbar) {
            double progress = 1.0;
            double coverage = 1.0;

            if (this->get_inner_height() > 0) {
                progress = (double)this->lv_top /
                           (double)this->get_inner_height();
                coverage = (double)height / (double)this->get_inner_height();
            }

            y = vis_line_t(this->lv_y) +
                vis_line_t((int)(progress * (double)height));
            lines = y + min(height, vis_line_t(
                                (int)(coverage * (double)height)));
            for (; y <= lines; ++y) {
                char buffer;

                mvwinnstr(this->lv_window, y, width - 1, &buffer, 1);
                wattron(this->lv_window, A_REVERSE);
                mvwaddnstr(this->lv_window, y, width - 1, &buffer, 1);
                wattroff(this->lv_window, A_REVERSE);
            }
            wmove(this->lv_window, this->lv_y + height - 1, 0);
        }

        this->lv_needs_update = false;
    }
}
