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
#include <sys/time.h>

#include <cmath>

#include "base/lnav_log.hh"
#include "listview_curses.hh"

using namespace std;

list_gutter_source listview_curses::DEFAULT_GUTTER_SOURCE;

listview_curses::listview_curses()
= default;

listview_curses::~listview_curses()
= default;

void listview_curses::reload_data()
{
    if (this->lv_source == nullptr) {
        this->lv_top  = 0_vl;
        this->lv_left = 0;
    }
    else {
        if (this->lv_top >= this->get_inner_height()) {
            this->lv_top = max(0_vl, vis_line_t(this->get_inner_height() - 1));
        }
        if (this->get_inner_height() == 0) {
            this->lv_selection = 0_vl;
        } else if (this->lv_selection >= this->get_inner_height()) {
            this->lv_selection = this->get_inner_height() - 1_vl;
        }
    }
    this->vc_needs_update = true;
}

bool listview_curses::handle_key(int ch)
{
    for (auto &lv_input_delegate : this->lv_input_delegates) {
        if (lv_input_delegate->list_input_handle_key(*this, ch)) {
            return true;
        }
    }

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
    case 'L':
    case KEY_SRIGHT:
        this->shift_left(10);
        break;
    case 'H':
    case KEY_SLEFT:
        this->shift_left(-10);
        break;

    case '\r':
    case 'j':
    case KEY_DOWN:
        if (this->is_selectable()) {
            this->shift_selection(1);
        } else {
            this->shift_top(1_vl);
        }
        break;

    case 'k':
    case KEY_UP:
        if (this->is_selectable()) {
            this->shift_selection(-1);
        } else {
            this->shift_top(-1_vl);
        }
        break;

    case 'b':
    case KEY_BACKSPACE:
    case KEY_PPAGE:
        this->shift_top(-(this->rows_available(this->lv_top, RD_UP) - 1_vl));
        break;

    case ' ':
    case KEY_NPAGE:
        this->shift_top(this->rows_available(this->lv_top, RD_DOWN) - 1_vl);
        break;

    case 'g':
    case KEY_HOME:
        this->set_top(0_vl);
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

    case ']':
        {
            double tenth = ((double)this->get_inner_height()) / 10.0;

            this->shift_top(vis_line_t((int)tenth));
        }
        break;

    case '[':
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

void listview_curses::do_update()
{
    if (this->lv_window == nullptr || this->lv_height == 0) {
        view_curses::do_update();
        return;
    }

    if (this->vc_needs_update) {
        view_colors &vc = view_colors::singleton();
        vis_line_t        height, row;
        attr_line_t       overlay_line;
        struct line_range lr;
        unsigned long     width, wrap_width;
        size_t            row_count;
        int y = this->lv_y, bottom;
        attr_t role_attrs = vc.attrs_for_role(this->vc_default_role);

        this->get_dimensions(height, width);

        if (this->vc_width > 0) {
            width = std::min((unsigned long) this->vc_width, width);
        }

        wrap_width = width - (this->lv_word_wrap ? 1 : this->lv_show_scrollbar ? 1 : 0);

        row_count = this->get_inner_height();
        row   = this->lv_top;
        bottom = y + height;
        vector<attr_line_t> rows(min((size_t) height, row_count - (int) this->lv_top));
        this->lv_source->listview_value_for_rows(*this, row, rows);
        while (y < bottom) {
            lr.lr_start = this->lv_left;
            lr.lr_end   = this->lv_left + wrap_width;
            if (this->lv_overlay_source != NULL &&
                this->lv_overlay_source->list_value_for_overlay(
                    *this,
                    y - this->lv_y, bottom - this->lv_y,
                    row,
                    overlay_line)) {
                mvwattrline(this->lv_window, y, this->lv_x, overlay_line, lr);
                overlay_line.clear();
                ++y;
            }
            else if (row < (int)row_count) {
                attr_line_t &al = rows[row - this->lv_top];

                do {
                    mvwattrline(this->lv_window, y, this->lv_x, al, lr,
                                this->vc_default_role);
                    if (this->lv_word_wrap) {
                        mvwhline(this->lv_window, y, this->lv_x + wrap_width, ' ', width - wrap_width);
                    }
                    lr.lr_start += wrap_width;
                    lr.lr_end += wrap_width;
                    ++y;
                } while (this->lv_word_wrap && y < bottom && lr.lr_start < (int)al.length());
                ++row;
            }
            else {
                wattron(this->lv_window, role_attrs);
                mvwhline(this->lv_window, y, this->lv_x, ' ', width);
                wattroff(this->lv_window, role_attrs);
                ++y;
            }
        }

        if (this->lv_show_scrollbar) {
            double progress = 1.0;
            double coverage = 1.0;
            double adjusted_height = (double)row_count / (double)height;
            vis_line_t lines;

            if (row_count > 0) {
                progress = (double)this->lv_top / (double)row_count;
                coverage = (double)height / (double)row_count;
            }

            y = this->lv_y + (int)(progress * (double)height);
            lines = vis_line_t(y + min((int) height, (int)(coverage * (double)height)));

            for (unsigned int gutter_y = this->lv_y;
                 gutter_y < (this->lv_y + height);
                 gutter_y++) {
                int range_start = 0, range_end;
                view_colors::role_t role = this->vc_default_role;
                view_colors::role_t bar_role = view_colors::VCR_STATUS;
                int attrs;
                chtype ch = ACS_VLINE;

                if (row_count > 0) {
                    range_start = (double)(gutter_y - this->lv_y) * adjusted_height;
                }
                range_end = range_start + adjusted_height;

                this->lv_gutter_source->listview_gutter_value_for_range(
                    *this, range_start, range_end, ch, role, bar_role);
                if (gutter_y >= (unsigned int)y && gutter_y <= (unsigned int)lines) {
                    role = bar_role;
                }
                attrs = vc.attrs_for_role(role);
                wattron(this->lv_window, attrs);
                mvwaddch(this->lv_window, gutter_y, this->lv_x + width - 1, ch);
                wattroff(this->lv_window, attrs);
            }
            wmove(this->lv_window, this->lv_y + height - 1, this->lv_x);
        }

        if (this->lv_show_bottom_border) {
            cchar_t row_ch[width];
            int y = this->lv_y + height - 1;

            mvwin_wchnstr(this->lv_window, y, this->lv_x, row_ch, width - 1);
            for (int lpc = 0; lpc < width - 1; lpc++) {
                row_ch[lpc].attr |= A_UNDERLINE;
            }
            mvwadd_wchnstr(this->lv_window, y, this->lv_x, row_ch, width - 1);
        }

        this->vc_needs_update = false;
    }

    view_curses::do_update();

#if 0
    else if (this->lv_overlay_needs_update && this->lv_overlay_source != NULL) {
        vis_line_t y(this->lv_y), height, bottom;
        attr_line_t overlay_line;
        unsigned long width, wrap_width;
        struct line_range lr;

        this->lv_overlay_source->list_overlay_count(*this);
        this->get_dimensions(height, width);
        wrap_width = width - (this->lv_word_wrap ? 1 : this->lv_show_scrollbar ? 1 : 0);

        lr.lr_start = this->lv_left;
        lr.lr_end   = this->lv_left + wrap_width;

        bottom = y + height;
        while (y < bottom) {
            if (this->lv_overlay_source->list_value_for_overlay(
                    *this,
                    y - vis_line_t(this->lv_y),
                overlay_line)) {
                this->mvwattrline(this->lv_window, y, this->lv_x, overlay_line, lr);
                overlay_line.clear();
            }
            ++y;
        }
    }
#endif
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
            this->lv_scroll_accel += 2;
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
        if ((scroll_top - 1) <= me.me_y && me.me_y <= (scroll_bottom + 1)) {
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
        require(0);
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
