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

#include <time.h>
#include <math.h>
#include <sys/time.h>

#include "listview_curses.hh"

using namespace std;

listview_curses::listview_curses()
    : lv_source(NULL),
      lv_overlay_source(NULL),
      lv_window(NULL),
      lv_y(0),
      lv_top(0),
      lv_left(0),
      lv_height(0),
      lv_needs_update(true),
      lv_show_scrollbar(true),
      lv_word_wrap(false),
      lv_mouse_y(-1),
      lv_mouse_mode(LV_MODE_NONE)
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
        this->shift_top(-(this->rows_available(this->lv_top, RD_UP) - vis_line_t(1)));
        break;

    case ' ':
    case KEY_NPAGE:
        this->shift_top(this->rows_available(this->lv_top, RD_DOWN) - vis_line_t(1));
        break;

    case 'g':
    case KEY_HOME:
        this->set_top(vis_line_t(0));
        break;

    case 'G':
    case KEY_END: {
            vis_line_t last_line(this->get_inner_height() - 1);
            vis_line_t tail_bottom(this->get_top_for_last_row());

            if (this->get_top() == last_line)
                this->set_top(tail_bottom);
            else if (tail_bottom <= this->get_top())
                this->set_top(last_line);
            else
                this->set_top(tail_bottom);
        }
        break;

    case 'A':
        {
            double tenth = ((double)this->get_inner_height()) / 10.0;

            this->shift_top(vis_line_t((int)tenth));
        }
        break;

    case 'B':
        {
            double tenth = ((double)this->get_inner_height()) / 10.0;

            this->shift_top(vis_line_t((int)-tenth));
        }
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
        vis_line_t        y(this->lv_y), height, bottom, row;
        attr_line_t       overlay_line;
        vis_line_t        overlay_height(0);
        struct line_range lr;
        unsigned long     width, wrap_width;
        size_t            row_count;

        if (this->lv_overlay_source != NULL) {
            overlay_height = vis_line_t(
                this->lv_overlay_source->list_overlay_count(*this));
        }

        this->get_dimensions(height, width);
        wrap_width = width - (this->lv_word_wrap ? 1 : 0);

        row_count = this->get_inner_height();
        row   = this->lv_top;
        bottom = y + height;
        while (y < bottom) {
            lr.lr_start = this->lv_left;
            lr.lr_end   = this->lv_left + wrap_width;
            if (this->lv_overlay_source != NULL &&
                this->lv_overlay_source->list_value_for_overlay(
                    *this,
                    y - vis_line_t(this->lv_y),
                    overlay_line)) {
                this->mvwattrline(this->lv_window, y, 0, overlay_line, lr);
                overlay_line.clear();
                ++y;
            }
            else if (row < (int)row_count) {
                attr_line_t al;

                this->lv_source->listview_value_for_row(*this, row, al);
                do {
                    this->mvwattrline(this->lv_window, y, 0, al, lr);
                    if (this->lv_word_wrap) {
                        wmove(this->lv_window, y, wrap_width);
                        wclrtoeol(this->lv_window);
                    }
                    lr.lr_start += wrap_width;
                    lr.lr_end += wrap_width;
                    ++y;
                } while (this->lv_word_wrap && y < bottom && lr.lr_start < (int)al.length());
                ++row;
            }
            else {
                wmove(this->lv_window, y, 0);
                wclrtoeol(this->lv_window);
                ++y;
            }
        }

        if (this->lv_show_scrollbar) {
            double progress = 1.0;
            double coverage = 1.0;
            vis_line_t lines;

            if (this->get_inner_height() > 0) {
                progress = (double)this->lv_top / (double)row_count;
                coverage = (double)height / (double)row_count;
            }

            y = vis_line_t(this->lv_y) +
                vis_line_t((int)(progress * (double)height));
            lines = y + min(height, vis_line_t(
                                (int)(coverage * (double)height)));
            for (; y <= lines; ++y) {
                mvwchgat(this->lv_window,
                         y, width - 1,
                         1,
                         A_REVERSE, view_colors::ansi_color_pair(
                            COLOR_WHITE, COLOR_BLACK),
                         NULL);
            }
            wmove(this->lv_window, this->lv_y + height - 1, 0);
        }

        this->lv_needs_update = false;
    }
}

static int scroll_polarity(mouse_button_t button)
{
    return button == BUTTON_SCROLL_UP ? -1 : 1;
}

bool listview_curses::handle_mouse(mouse_event &me)
{
    vis_line_t inner_height, height;
    struct timeval diff;
    unsigned long width;

    timersub(&me.me_time, &this->lv_mouse_time, &diff);
    this->get_dimensions(height, width);
    inner_height = this->get_inner_height();

    switch (me.me_button) {
    case BUTTON_SCROLL_UP:
    case BUTTON_SCROLL_DOWN:
        if (diff.tv_sec > 0 || diff.tv_usec > 80000) {
            this->lv_scroll_accel = 1;
            this->lv_scroll_velo = 0;
        }
        else {
            this->lv_scroll_accel += 3;
        }
        this->lv_scroll_velo += this->lv_scroll_accel;

        this->shift_top(vis_line_t(scroll_polarity(me.me_button) *
                                   this->lv_scroll_velo),
                        true);
        break;
    default:
        break;
    }
    this->lv_mouse_time = me.me_time;

    if (me.me_button != BUTTON_LEFT ||
        inner_height == 0 ||
        (this->lv_mouse_mode != LV_MODE_DRAG && me.me_x < (int)(width - 2))) {
        return false;
    }

    if (me.me_state == BUTTON_STATE_RELEASED) {
        this->lv_mouse_y = -1;
        this->lv_mouse_mode = LV_MODE_NONE;
        return true;
    }

    int scroll_top, scroll_bottom, shift_amount = 0, new_top = 0;
    double top_pct, bot_pct, pct;

    top_pct = (double)this->get_top() / (double)inner_height;
    bot_pct = (double)this->get_bottom() / (double)inner_height;
    scroll_top = (this->get_y() + (int)(top_pct * (double)height));
    scroll_bottom = (this->get_y() + (int)(bot_pct * (double)height));

    if (this->lv_mouse_mode == LV_MODE_NONE) {
        if (scroll_top <= me.me_y && me.me_y <= scroll_bottom) {
            this->lv_mouse_mode        = LV_MODE_DRAG;
            this->lv_mouse_y = me.me_y - scroll_top;
        }
        else if (me.me_y < scroll_top) {
            this->lv_mouse_mode = LV_MODE_UP;
        }
        else {
            this->lv_mouse_mode = LV_MODE_DOWN;
        }
    }

    switch (this->lv_mouse_mode) {
    case LV_MODE_NONE:
        assert(0);
        break;

    case LV_MODE_UP:
        if (me.me_y < scroll_top) {
            shift_amount = -1 * height;
        }
        break;

    case LV_MODE_DOWN:
        if (me.me_y > scroll_bottom) {
            shift_amount = height;
        }
        break;

    case LV_MODE_DRAG:
        pct     = (double)inner_height / (double)height;
        new_top = me.me_y - this->get_y() - this->lv_mouse_y;
        new_top = (int)floor(((double)new_top * pct) + 0.5);
        this->set_top(vis_line_t(new_top));
        break;
    }

    if (shift_amount != 0) {
        this->shift_top(vis_line_t(shift_amount));
    }

    return true;
}
