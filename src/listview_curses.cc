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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file listview_curses.cc
 */

#include <cmath>

#include "listview_curses.hh"

#include <sys/time.h>
#include <time.h>

#include "base/func_util.hh"
#include "base/lnav_log.hh"
#include "config.h"

list_gutter_source listview_curses::DEFAULT_GUTTER_SOURCE;

listview_curses::listview_curses() : lv_scroll(noop_func{}) {}

void
listview_curses::update_top_from_selection()
{
    if (!this->lv_selectable) {
        return;
    }

    vis_line_t height;
    unsigned long width;

    this->get_dimensions(height, width);

    if (this->lv_selection < 0_vl) {
        this->set_top(0_vl);
    } else if (this->lv_sync_selection_and_top) {
        this->set_top(this->lv_selection);
    } else if (this->lv_selection == this->get_inner_height() - 1_vl) {
        this->set_top(this->get_top_for_last_row());
    } else if (height <= this->lv_tail_space) {
        this->set_top(this->lv_selection);
    } else if (this->lv_selection
               >= (this->lv_top + height - this->lv_tail_space - 1_vl))
    {
        auto diff = this->lv_selection
            - (this->lv_top + height - this->lv_tail_space - 1_vl);

        if (height < 10 || diff < (height / 8_vl)) {
            // for small differences between the bottom and the
            // selection, just move a little bit.
            this->set_top(
                this->lv_selection - height + 1_vl + this->lv_tail_space, true);
        } else {
            // for large differences, put the focus in the middle
            this->set_top(this->lv_selection - height / 2_vl, true);
        }
    } else if (this->lv_selection <= this->lv_top) {
        auto diff = this->lv_top - this->lv_selection;

        if (this->lv_selection > 0 && (height < 10 || diff < (height / 8_vl))) {
            this->set_top(this->lv_selection - 1_vl);
        } else if (this->lv_selection < height) {
            this->set_top(0_vl);
        } else {
            this->set_top(this->lv_selection - height / 2_vl, true);
        }
    }
}

void
listview_curses::reload_data()
{
    if (this->lv_source == nullptr) {
        this->lv_top = 0_vl;
        this->lv_selection = -1_vl;
        this->lv_focused_overlay_top = 0_vl;
        this->lv_focused_overlay_selection = 0_vl;
        this->lv_left = 0;
    } else {
        if (this->lv_top >= this->get_inner_height()) {
            this->lv_top
                = std::max(0_vl, vis_line_t(this->get_inner_height() - 1));
            this->lv_focused_overlay_top = 0_vl;
            this->lv_focused_overlay_selection = 0_vl;
        }
        if (this->lv_selectable) {
            if (this->get_inner_height() == 0) {
                this->set_selection(-1_vl);
            } else if (this->lv_selection >= this->get_inner_height()) {
                this->set_selection(this->get_inner_height() - 1_vl);
            } else {
                auto curr_sel = this->get_selection();

                if (curr_sel == -1_vl) {
                    curr_sel = 0_vl;
                }
                this->lv_selection = -1_vl;
                this->set_selection(curr_sel);
            }

            this->update_top_from_selection();
        }
    }
    this->vc_needs_update = true;
    this->invoke_scroll();
}

bool
listview_curses::handle_key(int ch)
{
    for (auto& lv_input_delegate : this->lv_input_delegates) {
        if (lv_input_delegate->list_input_handle_key(*this, ch)) {
            return true;
        }
    }

    auto height = 0_vl;

    unsigned long width;
    bool retval = true;

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
            this->shift_selection(shift_amount_t::down_line);
            break;

        case 'k':
        case KEY_UP:
            this->shift_selection(shift_amount_t::up_line);
            break;

        case 'q':
        case KEY_ESCAPE:
            if (this->lv_overlay_focused) {
                this->lv_overlay_focused = false;
                this->lv_source->listview_selection_changed(*this);
                this->set_needs_update();
            } else {
                retval = false;
            }
            break;

        case KEY_CTRL(']'):
            if (this->lv_overlay_source != nullptr && !this->lv_overlay_focused)
            {
                std::vector<attr_line_t> overlay_content;
                this->lv_overlay_source->list_value_for_overlay(
                    *this, this->get_selection(), overlay_content);
                if (!overlay_content.empty()) {
                    this->lv_overlay_focused = !this->lv_overlay_focused;
                    this->lv_source->listview_selection_changed(*this);
                    this->set_needs_update();
                }
            } else {
                retval = false;
            }
            break;

        case 'b':
        case KEY_BACKSPACE:
        case KEY_PPAGE:
            if (this->lv_overlay_focused) {
                this->shift_selection(shift_amount_t::up_page);
                break;
            }

            if (this->lv_top == 0_vl && this->lv_selectable
                && this->lv_selection != 0_vl)
            {
                this->set_selection(0_vl);
            } else {
                this->shift_top(
                    -(this->rows_available(this->lv_top, RD_UP) - 1_vl));
            }
            break;

        case ' ':
        case KEY_NPAGE: {
            if (this->lv_overlay_source != nullptr) {
                std::vector<attr_line_t> overlay_content;
                this->lv_overlay_source->list_value_for_overlay(
                    *this, this->get_selection(), overlay_content);
                if (!overlay_content.empty()) {
                    this->shift_selection(shift_amount_t::down_page);
                    break;
                }
            }

            auto rows_avail
                = this->rows_available(this->lv_top, RD_DOWN) - 1_vl;
            auto top_for_last = this->get_top_for_last_row();

            if ((this->lv_top < top_for_last)
                && (this->lv_top + rows_avail > top_for_last))
            {
                this->set_top(top_for_last);
                if (this->lv_selection <= top_for_last) {
                    this->set_selection(top_for_last + 1_vl);
                }
            } else {
                this->shift_top(rows_avail);

                auto inner_height = this->get_inner_height();
                if (this->lv_selectable && this->lv_top >= top_for_last
                    && inner_height > 0_vl)
                {
                    this->set_selection(inner_height - 1_vl);
                }
            }
            break;
        }

        case 'g':
        case KEY_HOME:
            if (this->lv_overlay_focused) {
                this->lv_focused_overlay_top = 0_vl;
                this->lv_focused_overlay_selection = 0_vl;
                this->lv_source->listview_selection_changed(*this);
                this->set_needs_update();
                break;
            }

            if (this->is_selectable()) {
                this->set_selection(0_vl);
            } else {
                this->set_top(0_vl);
            }
            break;

        case 'G':
        case KEY_END: {
            if (this->lv_overlay_focused) {
                std::vector<attr_line_t> overlay_content;
                this->lv_overlay_source->list_value_for_overlay(
                    *this, this->get_selection(), overlay_content);
                auto overlay_height
                    = this->get_overlay_height(overlay_content.size(), height);
                auto ov_top_for_last = vis_line_t{
                    static_cast<int>(overlay_content.size() - overlay_height)};

                this->lv_focused_overlay_top = ov_top_for_last;
                this->lv_focused_overlay_selection
                    = vis_line_t(overlay_content.size() - 1);
                this->lv_source->listview_selection_changed(*this);
                this->set_needs_update();
                break;
            }

            vis_line_t last_line(this->get_inner_height() - 1);
            vis_line_t tail_bottom(this->get_top_for_last_row());

            if (this->is_selectable()) {
                this->set_selection(last_line);
            } else if (this->get_top() == last_line) {
                this->set_top(tail_bottom);
            } else if (tail_bottom <= this->get_top()) {
                this->set_top(last_line);
            } else {
                this->set_top(tail_bottom);
            }
            break;
        }

        case ']': {
            double tenth = ((double) this->get_inner_height()) / 10.0;

            this->shift_top(vis_line_t((int) tenth));
            break;
        }

        case '[':
        case 'B': {
            double tenth = ((double) this->get_inner_height()) / 10.0;

            this->shift_top(vis_line_t((int) -tenth));
            break;
        }

        default:
            retval = false;
            break;
    }

    return retval;
}

vis_line_t
listview_curses::get_overlay_top(vis_line_t row, size_t count, size_t total)
{
    if (row == this->get_selection()) {
        if (this->lv_focused_overlay_selection >= total) {
            this->lv_focused_overlay_selection = vis_line_t(total) - 1_vl;
        }
        if (this->lv_focused_overlay_selection < 0_vl) {
            this->lv_focused_overlay_selection = 0_vl;
        }
        auto max_top = vis_line_t(total - count);
        if (this->lv_focused_overlay_selection <= this->lv_focused_overlay_top)
        {
            this->lv_focused_overlay_top = this->lv_focused_overlay_selection;
            if (this->lv_focused_overlay_top > 0_vl) {
                this->lv_focused_overlay_top -= 1_vl;
            }
        }
        if (this->lv_focused_overlay_selection
            > (this->lv_focused_overlay_top + vis_line_t(count) - 2_vl))
        {
            this->lv_focused_overlay_top
                = this->lv_focused_overlay_selection - vis_line_t(count) + 2_vl;
        }
        if (this->lv_focused_overlay_top > max_top) {
            this->lv_focused_overlay_top = max_top;
        }

        return this->lv_focused_overlay_top;
    }

    return 0_vl;
}

void
listview_curses::do_update()
{
    if (this->lv_window == nullptr || this->lv_height == 0 || !this->vc_visible)
    {
        view_curses::do_update();
        return;
    }

    std::vector<attr_line_t> row_overlay_content;
    vis_line_t height;
    unsigned long width;

    this->update_top_from_selection();
    this->get_dimensions(height, width);
    while (this->vc_needs_update) {
        auto& vc = view_colors::singleton();
        vis_line_t row;
        attr_line_t overlay_line;
        struct line_range lr;
        unsigned long wrap_width;
        int y = this->lv_y, bottom;
        auto role_attrs = vc.attrs_for_role(this->vc_default_role);

        if (height <= 0) {
            return;
        }

        if (this->vc_width > 0) {
            width = std::min((unsigned long) this->vc_width, width);
        }

        wrap_width = width;
        if (this->lv_show_scrollbar) {
            wrap_width -= 1;
        }

        size_t row_count = this->get_inner_height();
        size_t blank_rows = 0;
        row = this->lv_top;
        bottom = y + height;
        std::vector<attr_line_t> rows(
            std::min((size_t) height, row_count - (int) this->lv_top));
        this->lv_source->listview_value_for_rows(*this, row, rows);
        while (y < bottom) {
            lr.lr_start = this->lv_left;
            lr.lr_end = this->lv_left + wrap_width;
            if (this->lv_overlay_source != nullptr
                && this->lv_overlay_source->list_static_overlay(
                    *this, y - this->lv_y, bottom - this->lv_y, overlay_line))
            {
                mvwattrline(this->lv_window, y, this->lv_x, overlay_line, lr);
                overlay_line.clear();
                ++y;
            } else if (row < (int) row_count) {
                auto& al = rows[row - this->lv_top];

                for (const auto& attr : al.get_attrs()) {
                    require_ge(attr.sa_range.lr_start, 0);
                }

                size_t remaining = 0;
                do {
                    remaining = mvwattrline(this->lv_window,
                                            y,
                                            this->lv_x,
                                            al,
                                            lr,
                                            this->vc_default_role);
                    if (this->lv_word_wrap) {
                        mvwhline(this->lv_window,
                                 y,
                                 this->lv_x + wrap_width,
                                 ' ',
                                 width - wrap_width);
                    }
                    lr.lr_start += wrap_width;
                    lr.lr_end += wrap_width;
                    ++y;
                } while (this->lv_word_wrap && y < bottom && remaining > 0);

                if (this->lv_overlay_source != nullptr) {
                    row_overlay_content.clear();

                    lr.lr_start = this->lv_left;
                    lr.lr_end = this->lv_left + wrap_width;
                    this->lv_overlay_source->list_value_for_overlay(
                        *this, row, row_overlay_content);
                    auto overlay_height = this->get_overlay_height(
                        row_overlay_content.size(), height);
                    auto ov_height_remaining = overlay_height;
                    auto overlay_top = this->get_overlay_top(
                        row, overlay_height, row_overlay_content.size());
                    auto overlay_row = overlay_top;
                    if (row_overlay_content.size() > 1) {
                        auto hdr
                            = this->lv_overlay_source->list_header_for_overlay(
                                *this, row);
                        if (hdr) {
                            auto ov_hdr_attrs = text_attrs{};
                            ov_hdr_attrs.ta_attrs |= A_UNDERLINE;
                            auto ov_hdr = hdr.value().with_attr_for_all(
                                VC_STYLE.value(ov_hdr_attrs));
                            mvwattrline(this->lv_window,
                                        y,
                                        this->lv_x,
                                        ov_hdr,
                                        lr,
                                        role_t::VCR_STATUS_INFO);
                            ++y;
                        }
                    }
                    auto overlay_y = y;
                    while (ov_height_remaining > 0 && y < bottom) {
                        if (this->lv_overlay_focused
                            && row == this->get_selection()
                            && overlay_row
                                == this->lv_focused_overlay_selection)
                        {
                            row_overlay_content[overlay_row].with_attr_for_all(
                                VC_ROLE.value(role_t::VCR_CURSOR_LINE));
                        }
                        mvwattrline(this->lv_window,
                                    y,
                                    this->lv_x,
                                    row_overlay_content[overlay_row],
                                    lr,
                                    role_t::VCR_ALT_ROW);
                        ov_height_remaining -= 1;
                        ++overlay_row;
                        ++y;
                    }

                    if (overlay_height != row_overlay_content.size()) {
                        double progress = 1.0;
                        double coverage = 1.0;
                        vis_line_t lines;

                        if (!row_overlay_content.empty()) {
                            progress = (double) overlay_top
                                / (double) row_overlay_content.size();
                            coverage = (double) overlay_height
                                / (double) row_overlay_content.size();
                        }

                        auto scroll_y = overlay_y
                            + (int) (progress * (double) overlay_height);
                        lines = vis_line_t(
                            scroll_y
                            + std::min(
                                (int) overlay_height,
                                (int) (coverage * (double) overlay_height)));

                        for (unsigned int gutter_y = overlay_y;
                             gutter_y < (overlay_y + overlay_height);
                             gutter_y++)
                        {
                            auto role = this->vc_default_role;
                            auto bar_role = role_t::VCR_SCROLLBAR;
                            text_attrs attrs;
                            chtype ch = gutter_y == overlay_y
                                ? ACS_URCORNER
                                : (gutter_y == (overlay_y + overlay_height - 1)
                                       ? ACS_LRCORNER
                                       : ACS_VLINE);

                            if (gutter_y >= (unsigned int) scroll_y
                                && gutter_y <= (unsigned int) lines)
                            {
                                role = bar_role;
                            }
                            attrs = vc.attrs_for_role(role);
                            wattr_set(this->lv_window,
                                      attrs.ta_attrs,
                                      vc.ensure_color_pair(attrs.ta_fg_color,
                                                           attrs.ta_bg_color),
                                      nullptr);
                            mvwaddch(this->lv_window,
                                     gutter_y,
                                     this->lv_x + width - 2,
                                     ch);
                        }
                    }
                }

                ++row;
            } else {
                wattr_set(this->lv_window,
                          role_attrs.ta_attrs,
                          vc.ensure_color_pair(role_attrs.ta_fg_color,
                                               role_attrs.ta_bg_color),
                          nullptr);
                mvwhline(this->lv_window, y, this->lv_x, ' ', width);
                ++y;
                blank_rows += 1;
            }
        }

        if (this->lv_selectable && !this->lv_sync_selection_and_top
            && this->lv_selection >= 0 && (row > this->lv_tail_space)
            && (blank_rows < this->lv_tail_space)
            && ((row - this->lv_tail_space) < this->lv_selection))
        {
            this->shift_top(this->lv_selection - row + this->lv_tail_space);
            continue;
        }

        if (this->lv_show_scrollbar) {
            double progress = 1.0;
            double coverage = 1.0;
            double adjusted_height = (double) row_count / (double) height;
            vis_line_t lines;

            if (row_count > 0) {
                progress = (double) this->lv_top / (double) row_count;
                coverage = (double) height / (double) row_count;
            }

            y = this->lv_y + (int) (progress * (double) height);
            lines = vis_line_t(
                y + std::min((int) height, (int) (coverage * (double) height)));

            for (unsigned int gutter_y = this->lv_y;
                 gutter_y < (this->lv_y + height);
                 gutter_y++)
            {
                int range_start = 0, range_end;
                role_t role = this->vc_default_role;
                role_t bar_role = role_t::VCR_SCROLLBAR;
                text_attrs attrs;
                chtype ch = ACS_VLINE;

                if (row_count > 0) {
                    range_start
                        = (double) (gutter_y - this->lv_y) * adjusted_height;
                }
                range_end = range_start + adjusted_height;

                this->lv_gutter_source->listview_gutter_value_for_range(
                    *this, range_start, range_end, ch, role, bar_role);
                if (gutter_y >= (unsigned int) y
                    && gutter_y <= (unsigned int) lines)
                {
                    role = bar_role;
                }
                attrs = vc.attrs_for_role(role);
                wattr_set(
                    this->lv_window,
                    attrs.ta_attrs,
                    vc.ensure_color_pair(attrs.ta_fg_color, attrs.ta_bg_color),
                    nullptr);
                mvwaddch(this->lv_window, gutter_y, this->lv_x + width - 1, ch);
            }
            wmove(this->lv_window, this->lv_y + height - 1, this->lv_x);
        }

        if (this->lv_show_bottom_border) {
            cchar_t row_ch[width];
            int y = this->lv_y + height - 1;

            mvwin_wchnstr(this->lv_window, y, this->lv_x, row_ch, width - 1);
            for (unsigned long lpc = 0; lpc < width - 1; lpc++) {
                row_ch[lpc].attr |= A_UNDERLINE;
            }
            mvwadd_wchnstr(this->lv_window, y, this->lv_x, row_ch, width - 1);
        }

        this->vc_needs_update = false;
    }

    view_curses::do_update();
}

void
listview_curses::shift_selection(shift_amount_t sa)
{
    vis_line_t height;
    unsigned long width;

    this->get_dimensions(height, width);
    if (this->lv_overlay_focused) {
        vis_line_t focused = this->get_selection();
        std::vector<attr_line_t> overlay_content;

        this->lv_overlay_source->list_value_for_overlay(
            *this, focused, overlay_content);
        if (overlay_content.empty()) {
            this->lv_overlay_focused = false;
            this->lv_focused_overlay_top = 0_vl;
            this->lv_focused_overlay_selection = 0_vl;
            this->lv_source->listview_selection_changed(*this);
        } else {
            auto overlay_height
                = this->get_overlay_height(overlay_content.size(), height);
            auto ov_top_for_last = vis_line_t{
                static_cast<int>(overlay_content.size() - overlay_height)};
            switch (sa) {
                case shift_amount_t::up_line:
                    if (this->lv_focused_overlay_selection > 0_vl) {
                        this->lv_focused_overlay_selection -= 1_vl;
                    }
                    break;
                case shift_amount_t::up_page: {
                    if (this->lv_focused_overlay_selection > overlay_height) {
                        this->lv_focused_overlay_selection
                            -= vis_line_t{static_cast<int>(overlay_height - 1)};
                    } else {
                        this->lv_focused_overlay_selection = 0_vl;
                    }
                    break;
                }
                case shift_amount_t::down_line:
                    if (this->lv_focused_overlay_selection + 1
                        < overlay_content.size())
                    {
                        this->lv_focused_overlay_selection += 1_vl;
                    }
                    break;
                case shift_amount_t::down_page: {
                    if (this->lv_focused_overlay_selection + overlay_height - 1
                        >= ov_top_for_last)
                    {
                        this->lv_focused_overlay_selection
                            = vis_line_t(overlay_content.size() - 1);
                    } else {
                        this->lv_focused_overlay_selection
                            += vis_line_t{static_cast<int>(overlay_height - 1)};
                    }
                    break;
                }
                default:
                    break;
            }
            this->lv_source->listview_selection_changed(*this);
            this->set_needs_update();
            return;
        }
    }

    auto offset = 0_vl;
    switch (sa) {
        case shift_amount_t::up_line:
            offset = -1_vl;
            break;
        case shift_amount_t::up_page:
            offset = -(height - 1_vl);
            break;
        case shift_amount_t::down_line:
            offset = 1_vl;
            break;
        case shift_amount_t::down_page:
            offset = height - 1_vl;
            break;
    }
    if (this->is_selectable()) {
        if (this->lv_selection == -1_vl) {
            this->lv_selection = this->lv_top;
        }
        auto new_selection = this->lv_selection + vis_line_t(offset);

        if (new_selection < 0_vl) {
            new_selection = 0_vl;
        } else if (new_selection >= this->get_inner_height()) {
            auto rows_avail
                = this->rows_available(this->lv_top, RD_DOWN) - 1_vl;
            auto top_for_last = this->get_top_for_last_row();

            if ((this->lv_top < top_for_last)
                && (this->lv_top + rows_avail > top_for_last))
            {
                this->set_top(top_for_last);
                if (this->lv_selection <= top_for_last) {
                    this->set_selection(top_for_last + 1_vl);
                }
            } else {
                this->shift_top(rows_avail);

                auto inner_height = this->get_inner_height();
                if (this->lv_selectable && this->lv_top >= top_for_last
                    && inner_height > 0_vl)
                {
                    this->set_selection(inner_height - 1_vl);
                }
            }
        }
        this->set_selection(new_selection);
    } else {
        this->shift_top(vis_line_t{offset});
    }
}

static int
scroll_polarity(mouse_button_t button)
{
    return button == mouse_button_t::BUTTON_SCROLL_UP ? -1 : 1;
}

bool
listview_curses::handle_mouse(mouse_event& me)
{
    vis_line_t inner_height, height;
    struct timeval diff;
    unsigned long width;

    timersub(&me.me_time, &this->lv_mouse_time, &diff);
    this->get_dimensions(height, width);
    inner_height = this->get_inner_height();

    switch (me.me_button) {
        case mouse_button_t::BUTTON_SCROLL_UP:
        case mouse_button_t::BUTTON_SCROLL_DOWN:
            if (diff.tv_sec > 0 || diff.tv_usec > 80000) {
                this->lv_scroll_accel = 1;
                this->lv_scroll_velo = 0;
            } else {
                this->lv_scroll_accel += 2;
            }
            this->lv_scroll_velo += this->lv_scroll_accel;

            this->shift_top(vis_line_t(scroll_polarity(me.me_button)
                                       * this->lv_scroll_velo),
                            true);
            break;
        default:
            break;
    }
    this->lv_mouse_time = me.me_time;

    if (me.me_button != mouse_button_t::BUTTON_LEFT || inner_height == 0
        || (this->lv_mouse_mode != lv_mode_t::DRAG
            && me.me_x < (int) (width - 2)))
    {
        return false;
    }

    if (me.me_state == mouse_button_state_t::BUTTON_STATE_RELEASED) {
        this->lv_mouse_y = -1;
        this->lv_mouse_mode = lv_mode_t::NONE;
        return true;
    }

    int scroll_top, scroll_bottom, shift_amount = 0, new_top = 0;
    double top_pct, bot_pct, pct;

    top_pct = (double) this->get_top() / (double) inner_height;
    bot_pct = (double) this->get_bottom() / (double) inner_height;
    scroll_top = (this->get_y() + (int) (top_pct * (double) height));
    scroll_bottom = (this->get_y() + (int) (bot_pct * (double) height));

    if (this->lv_mouse_mode == lv_mode_t::NONE) {
        if ((scroll_top - 1) <= me.me_y && me.me_y <= (scroll_bottom + 1)) {
            this->lv_mouse_mode = lv_mode_t::DRAG;
            this->lv_mouse_y = me.me_y - scroll_top;
        } else if (me.me_y < scroll_top) {
            this->lv_mouse_mode = lv_mode_t::UP;
        } else {
            this->lv_mouse_mode = lv_mode_t::DOWN;
        }
    }

    switch (this->lv_mouse_mode) {
        case lv_mode_t::NONE:
            require(0);
            break;

        case lv_mode_t::UP:
            if (me.me_y < scroll_top) {
                shift_amount = -1 * height;
            }
            break;

        case lv_mode_t::DOWN:
            if (me.me_y > scroll_bottom) {
                shift_amount = height;
            }
            break;

        case lv_mode_t::DRAG:
            pct = (double) inner_height / (double) height;
            new_top = me.me_y - this->get_y() - this->lv_mouse_y;
            new_top = (int) floor(((double) new_top * pct) + 0.5);
            this->set_top(vis_line_t(new_top));
            break;
    }

    if (shift_amount != 0) {
        this->shift_top(vis_line_t(shift_amount));
    }

    return true;
}

void
listview_curses::set_top(vis_line_t top, bool suppress_flash)
{
    auto inner_height = this->get_inner_height();

    if (inner_height > 0 && top >= inner_height) {
        top = vis_line_t(inner_height - 1);
    }
    if (top < 0 || (top > 0 && top >= inner_height)) {
        if (suppress_flash == false) {
            alerter::singleton().chime("invalid top");
        }
    } else if (this->lv_top != top) {
        auto old_top = this->lv_top;
        this->lv_top = top;
        this->lv_focused_overlay_top = 0_vl;
        this->lv_focused_overlay_selection = 0_vl;
        if (this->lv_selectable) {
            if (this->lv_selection < 0_vl) {
                this->set_selection(top);
            } else if (this->lv_selection < top) {
                auto sel_diff = this->lv_selection - old_top;
                this->set_selection(top + sel_diff);
            } else {
                auto sel_diff = this->lv_selection - old_top;
                auto bot = this->get_bottom();
                unsigned long width;
                vis_line_t height;

                this->get_dimensions(height, width);

                if (bot != -1_vl && (bot - top) >= (height - 1)) {
                    if (this->lv_selection > (bot - this->lv_tail_space)) {
                        this->set_selection(top + sel_diff);
                    }
                }
            }
        }
        this->invoke_scroll();
        this->set_needs_update();
    }
}

vis_line_t
listview_curses::get_bottom() const
{
    auto retval = this->lv_top;
    auto avail = this->rows_available(retval, RD_DOWN);

    if (avail > 0) {
        retval += vis_line_t(avail - 1);
    }

    return retval;
}

vis_line_t
listview_curses::rows_available(vis_line_t line,
                                listview_curses::row_direction_t dir) const
{
    unsigned long width;
    vis_line_t height;
    vis_line_t retval(0);

    this->get_dimensions(height, width);
    if (this->lv_word_wrap) {
        size_t row_count = this->lv_source->listview_rows(*this);

        width -= 1;
        while ((height > 0) && (line >= 0) && ((size_t) line < row_count)) {
            size_t len = this->lv_source->listview_size_for_row(*this, line);

            do {
                len -= std::min((size_t) width, len);
                --height;
            } while (len > 0);
            line += vis_line_t(dir);
            if (height >= 0) {
                ++retval;
            }
        }
    } else {
        switch (dir) {
            case RD_UP:
                retval = std::min(height, line + 1_vl);
                break;
            case RD_DOWN:
                retval = std::min(
                    height,
                    vis_line_t(this->lv_source->listview_rows(*this) - line));
                break;
        }
    }

    return retval;
}

void
listview_curses::set_selection(vis_line_t sel)
{
    if (this->lv_selectable) {
        if (this->lv_selection == sel) {
            return;
        }
        if (sel == -1_vl) {
            this->lv_selection = sel;
            this->lv_overlay_focused = false;
            this->lv_focused_overlay_top = 0_vl;
            this->lv_focused_overlay_selection = 0_vl;
            this->lv_source->listview_selection_changed(*this);
            this->set_needs_update();
            this->invoke_scroll();
            return;
        }

        auto inner_height = this->get_inner_height();
        if (sel >= inner_height) {
            sel = inner_height - 1_vl;
        }
        if (sel >= 0_vl) {
            auto found = false;
            vis_line_t step;

            if (sel == 0_vl) {
                step = 1_vl;
            } else if (sel == inner_height - 1_vl) {
                step = -1_vl;
            } else if (sel < this->lv_selection) {
                step = -1_vl;
            } else {
                step = 1_vl;
            }
            while (sel < inner_height) {
                if (this->lv_source->listview_is_row_selectable(*this, sel)) {
                    found = true;
                    break;
                }
                sel += step;
            }
            if (found) {
                this->lv_selection = sel;
                if (this->lv_sync_selection_and_top) {
                    this->lv_top = sel;
                }
                this->lv_overlay_focused = false;
                this->lv_focused_overlay_top = 0_vl;
                this->lv_focused_overlay_selection = 0_vl;
                this->lv_source->listview_selection_changed(*this);
                this->set_needs_update();
                this->invoke_scroll();
            }
        }
    } else if (sel >= 0_vl) {
        this->set_top(sel);
    }
}

vis_line_t
listview_curses::get_top_for_last_row()
{
    auto inner_height = this->get_inner_height();
    auto retval = 0_vl;

    if (inner_height > 0) {
        auto last_line = inner_height - 1_vl;
        unsigned long width;
        vis_line_t height;

        this->get_dimensions(height, width);
        retval = last_line - this->rows_available(last_line, RD_UP) + 1_vl;
        if (inner_height >= (height - this->lv_tail_space)
            && (retval + this->lv_tail_space) < inner_height)
        {
            retval += this->lv_tail_space;
        }
    }

    return retval;
}

vis_line_t
listview_curses::shift_top(vis_line_t offset, bool suppress_flash)
{
    if (offset < 0 && this->lv_top == 0) {
        if (suppress_flash == false) {
            alerter::singleton().chime("the top of the view has been reached");
        }
    } else {
        this->set_top(std::max(0_vl, this->lv_top + offset), suppress_flash);
    }

    return this->lv_top;
}

size_t
listview_curses::get_overlay_height(size_t total, vis_line_t view_height)
{
    return std::min(total, static_cast<size_t>(2 * (view_height / 3)));
}

void
listview_curses::set_overlay_selection(nonstd::optional<vis_line_t> sel)
{
    if (sel) {
        if (sel.value() == this->lv_focused_overlay_selection) {
            return;
        }

        std::vector<attr_line_t> overlay_content;
        this->lv_overlay_source->list_value_for_overlay(
            *this, this->get_selection(), overlay_content);
        if (!overlay_content.empty()) {
            if (sel.value() >= 0 && sel.value() < overlay_content.size()) {
                this->lv_overlay_focused = true;
                this->lv_focused_overlay_selection = sel.value();
            } else {
                this->lv_overlay_focused = true;
                this->lv_focused_overlay_selection = 0_vl;
            }
        }
    } else {
        this->lv_overlay_focused = false;
        this->lv_focused_overlay_top = 0_vl;
        this->lv_focused_overlay_selection = 0_vl;
    }
    this->lv_source->listview_selection_changed(*this);
    this->set_needs_update();
}
