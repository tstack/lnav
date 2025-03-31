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

#include <chrono>
#include <cmath>

#include "listview_curses.hh"

#include <sys/time.h>
#include <time.h>

#include "base/func_util.hh"
#include "base/keycodes.hh"
#include "base/lnav_log.hh"
#include "config.h"

using namespace std::chrono_literals;

list_gutter_source listview_curses::DEFAULT_GUTTER_SOURCE;

listview_curses::listview_curses() : lv_scroll(noop_func{}) {}

std::optional<view_curses*>
listview_curses::contains(int x, int y)
{
    if (!this->vc_visible) {
        return std::nullopt;
    }

    auto child = view_curses::contains(x, y);
    if (child) {
        return child;
    }

    auto [height, width] = this->get_dimensions();

    if (this->vc_x <= x && x < this->vc_x + (int) width && this->vc_y <= y
        && y < this->vc_y + height)
    {
        return this;
    }
    return std::nullopt;
}

void
listview_curses::update_top_from_selection()
{
    if (!this->lv_selectable) {
        return;
    }

    auto [height, width] = this->get_dimensions();
    const auto inner_height = this->get_inner_height();

    if (this->lv_selection >= inner_height) {
        if (inner_height == 0_vl) {
            this->lv_selection = -1_vl;
        } else {
            this->lv_selection = inner_height - 1_vl;
        }
    }

    if (this->lv_selection < 0_vl) {
        this->set_top(0_vl);
        return;
    }

    if (this->lv_sync_selection_and_top || height <= this->lv_tail_space) {
        this->set_top(this->lv_selection);
        return;
    }

    auto layout = this->layout_for_row(this->lv_selection);
    auto min_top_for_sel
        = vis_line_t(this->lv_selection - layout.lr_above_line_heights.size());
    if (this->lv_top > this->lv_selection) {
        if (this->lv_head_space > 0_vl) {
            this->set_top(this->lv_selection
                          - vis_line_t(layout.lr_above_line_heights.size())
                              / 2_vl);
        } else {
            this->lv_top = this->lv_selection;
        }
    } else if (this->lv_top < min_top_for_sel) {
        this->set_top(min_top_for_sel);
    } else if (this->lv_top == this->lv_selection && this->lv_head_space > 0_vl)
    {
        if (!layout.lr_above_line_heights.empty()) {
            auto avail_height = height - layout.lr_desired_row_height;
            if (layout.lr_above_line_heights.front() < avail_height) {
                this->lv_top -= this->lv_head_space;
            }
        }
    }
}

void
listview_curses::get_dimensions(vis_line_t& height_out,
                                unsigned long& width_out) const
{
    unsigned int height;

    if (this->lv_window == nullptr) {
        height_out = std::max(this->lv_height, 1_vl);
        if (this->lv_source) {
            width_out = this->lv_source->listview_width(*this);
        } else {
            width_out = 80;
        }
    } else {
        unsigned int width_tmp;

        ncplane_dim_yx(this->lv_window, &height, &width_tmp);
        width_out = width_tmp;
        if (this->lv_height < 0) {
            height_out
                = vis_line_t(height) + this->lv_height - vis_line_t(this->vc_y);
            if (height_out < 0_vl) {
                height_out = 0_vl;
            }
        } else {
            height_out = this->lv_height;
        }
    }
    if (this->vc_x < (int) width_out) {
        width_out -= this->vc_x;
    } else {
        width_out = 0;
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
                this->set_selection_without_context(-1_vl);
            } else if (this->lv_selection >= this->get_inner_height()) {
                this->set_selection_without_context(this->get_inner_height()
                                                    - 1_vl);
            } else {
                auto curr_sel = this->get_selection();

                if (curr_sel == -1_vl) {
                    this->set_selection_without_context(0_vl);
                }
            }

            this->update_top_from_selection();
        }
    }
    this->vc_needs_update = true;
    this->invoke_scroll();
}

bool
listview_curses::handle_key(const ncinput& ch)
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
    switch (ch.eff_text[0]) {
        case 'l':
        case NCKEY_RIGHT:
            if (ncinput_shift_p(&ch)) {
                this->shift_left(10);
            } else {
                this->shift_left(width / 2);
            }
            break;

        case 'h':
        case NCKEY_LEFT:
            if (ncinput_shift_p(&ch)) {
                this->shift_left(-10);
            } else {
                this->shift_left(-(width / 2));
            }
            break;
        case 'L':
            this->shift_left(10);
            break;
        case 'H':
            this->shift_left(-10);
            break;

        case '\r':
        case 'j':
        case KEY_CTRL('N'):
        case NCKEY_DOWN:
        case NCKEY_ENTER:
            this->shift_selection(shift_amount_t::down_line);
            break;

        case 'k':
        case KEY_CTRL('P'):
        case NCKEY_UP:
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
                    auto bot = this->get_bottom();
                    this->lv_overlay_focused = !this->lv_overlay_focused;
                    auto overlay_height = vis_line_t(this->get_overlay_height(
                        overlay_content.size(), height));

                    if (this->lv_selection + overlay_height >= bot) {
                        this->set_top(this->lv_selection, true);
                    }
                    this->lv_source->listview_selection_changed(*this);
                    this->set_needs_update();
                }
            } else {
                retval = false;
            }
            break;

        case 'b':
        case NCKEY_BACKSPACE:
        case NCKEY_PGUP:
            if (this->lv_overlay_focused) {
                this->shift_selection(shift_amount_t::up_page);
                break;
            }

            if (this->lv_top == 0_vl && this->lv_selectable
                && this->lv_selection != 0_vl)
            {
                this->set_selection(0_vl);
            } else {
                auto shift_amount
                    = -(this->rows_available(this->lv_top, RD_UP) - 1_vl);
                this->shift_top(shift_amount);
            }
            break;

        case ' ':
        case NCKEY_PGDOWN: {
            if (this->lv_overlay_source != nullptr) {
                std::vector<attr_line_t> overlay_content;
                this->lv_overlay_source->list_value_for_overlay(
                    *this, this->get_selection(), overlay_content);
                if (!overlay_content.empty()) {
                    this->shift_selection(shift_amount_t::down_page);
                    break;
                }
            }

            auto inner_height = this->get_inner_height();
            if (this->lv_top + height * 2 > inner_height) {
                // NB: getting the last row can read from the file, which can
                // be expensive.  Use sparingly.
                auto top_for_last = this->get_top_for_last_row();

                if (this->lv_top + height > inner_height) {
                    this->set_selection(inner_height - 1_vl);
                } else {
                    this->set_top(top_for_last);
                }
            } else {
                auto rows_avail = this->rows_available(this->lv_top, RD_DOWN);
                if (rows_avail == 0_vl) {
                    rows_avail = 2_vl;
                } else if (rows_avail > 2_vl) {
                    rows_avail -= 1_vl;
                }

                this->shift_top(rows_avail);
            }
            break;
        }

        case 'g':
        case NCKEY_HOME:
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
        case NCKEY_END: {
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

            auto last_line = this->get_inner_height() - 1_vl;
            auto tail_bottom = this->get_top_for_last_row();

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
        if (this->lv_focused_overlay_selection >= (ssize_t) total) {
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

bool
listview_curses::do_update()
{
    static auto& vc = view_colors::singleton();
    bool retval = false;

    if (this->lv_window == nullptr || this->lv_height == 0 || !this->vc_visible)
    {
        return view_curses::do_update();
    }

    std::vector<attr_line_t> row_overlay_content;
    vis_line_t height;
    unsigned long width;

    this->get_dimensions(height, width);
    if (height <= 0) {
        return retval;
    }

    this->update_top_from_selection();
    while (this->vc_needs_update) {
        vis_line_t row;
        attr_line_t overlay_line;
        line_range lr;
        unsigned long wrap_width;
        int y = this->vc_y, bottom;
        auto role_attrs = vc.attrs_for_role(this->vc_default_role);

        retval = true;
        if (this->vc_width > 0) {
            width = std::min((unsigned long) this->vc_width, width);
        }

        wrap_width = width;
        if (this->lv_show_scrollbar && wrap_width > 0) {
            wrap_width -= 1;
        }

        size_t row_count = this->get_inner_height();
        row = this->lv_top;
        bottom = y + height;
        std::vector<attr_line_t> rows(
            std::min((size_t) height, row_count - (int) this->lv_top));
        this->lv_source->listview_value_for_rows(*this, row, rows);
        this->lv_display_lines.clear();
        this->lv_display_lines_row = row;
        auto x = this->vc_x;
        uint64_t border_channels = 0;
        if (this->lv_border_left_role) {
            this->lv_display_lines.emplace_back(empty_space{});
            border_channels = vc.to_channels(
                vc.attrs_for_role(this->lv_border_left_role.value()));

            auto al = attr_line_t("  ");
            if (!this->vc_title.empty()) {
                al.append(this->vc_title,
                          VC_STYLE.value(text_attrs::with_bold()));
            }
            al.al_attrs.emplace_back(line_range{0, 1},
                                     VC_GRAPHIC.value(NCACS_ULCORNER));
            auto hline_lr = line_range{1, (int) width - 1};
            if (!this->vc_title.empty()) {
                al.al_attrs.emplace_back(line_range{1, 2},
                                         VC_GRAPHIC.value(NCACS_RTEE));
                al.al_attrs.emplace_back(
                    line_range{
                        2 + (int) this->vc_title.length(),
                        2 + (int) this->vc_title.length() + 1,
                    },
                    VC_GRAPHIC.value(NCACS_LTEE));
                hline_lr.lr_start += 1 + this->vc_title.length() + 1;
            }
            al.al_attrs.emplace_back(hline_lr, VC_GRAPHIC.value(NCACS_HLINE));
            al.al_attrs.emplace_back(line_range{(int) width - 1, (int) width},
                                     VC_GRAPHIC.value(NCACS_URCORNER));
            mvwattrline(this->lv_window,
                        y,
                        x,
                        al,
                        line_range{0, (int) width},
                        this->lv_border_left_role.value());

            y += 1;
            for (auto border_y = y; border_y < bottom; border_y++) {
                ncplane_putstr_yx(
                    this->lv_window, border_y, this->vc_x, NCACS_VLINE);
                ncplane_set_cell_yx(this->lv_window,
                                    border_y,
                                    x,
                                    NCSTYLE_ALTCHARSET,
                                    border_channels);
            }
            x += 1;
            width -= 1;
        }
        while (y < bottom) {
            lr.lr_start = this->lv_left;
            lr.lr_end = this->lv_left + wrap_width;
            if (this->lv_overlay_source != nullptr
                && this->lv_overlay_source->list_static_overlay(
                    *this, y - this->vc_y, bottom - this->vc_y, overlay_line))
            {
                this->lv_display_lines.push_back(static_overlay_content{});
                mvwattrline(this->lv_window, y, x, overlay_line, lr);
                overlay_line.clear();
                ++y;
            } else if (row < (int) row_count) {
                auto& al = rows[row - this->lv_top];

                for (const auto& attr : al.get_attrs()) {
                    require_ge(attr.sa_range.lr_start, 0);
                }

                mvwattrline_result write_res;
                do {
                    this->lv_display_lines.push_back(main_content{row});
                    if (this->lv_word_wrap) {
                        // XXX mvwhline(this->lv_window, y, this->vc_x, ' ',
                        // width);
                    }
                    write_res = mvwattrline(
                        this->lv_window, y, x, al, lr, this->vc_default_role);
                    lr.lr_start = write_res.mr_chars_out;
                    lr.lr_end = write_res.mr_chars_out + width - 1;
                    ++y;
                } while (this->lv_word_wrap && y < bottom
                         && write_res.mr_bytes_remaining > 0);

                if (this->lv_overlay_source != nullptr) {
                    row_overlay_content.clear();

                    lr.lr_start = this->lv_left;
                    lr.lr_end = this->lv_left + wrap_width;

                    auto ov_menu = this->lv_overlay_source->list_overlay_menu(
                        *this, row);
                    auto ov_menu_row = 0_vl;
                    for (auto& ov_menu_line : ov_menu) {
                        if (y >= bottom) {
                            break;
                        }

                        this->lv_display_lines.push_back(overlay_menu{
                            ov_menu_row,
                        });
                        mvwattrline(this->lv_window,
                                    y,
                                    x,
                                    ov_menu_line,
                                    line_range{0, (int) wrap_width},
                                    role_t::VCR_ALT_ROW);
                        ov_menu_row += 1_vl;
                        ++y;
                    }

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
                            auto ov_hdr_attrs = text_attrs::with_underline();
                            auto ov_hdr = hdr.value();
                            ov_hdr.pad_to(width).with_attr_for_all(
                                VC_STYLE.value(ov_hdr_attrs));
                            this->lv_display_lines.emplace_back(
                                static_overlay_content{});
                            mvwattrline(this->lv_window,
                                        y,
                                        x,
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

                        this->lv_display_lines.emplace_back(overlay_content{
                            row,
                            overlay_row,
                            overlay_height,
                            vis_line_t{(int) row_overlay_content.size()},
                        });
                        mvwattrline(this->lv_window,
                                    y,
                                    x,
                                    row_overlay_content[overlay_row],
                                    lr,
                                    role_t::VCR_ALT_ROW);
                        ov_height_remaining -= 1_vl;
                        ++overlay_row;
                        ++y;
                    }

                    if (overlay_height != (ssize_t) row_overlay_content.size())
                    {
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

                        for (auto gutter_y = overlay_y;
                             gutter_y < (overlay_y + overlay_height);
                             gutter_y++)
                        {
                            auto role = this->vc_default_role;
                            auto bar_role = role_t::VCR_SCROLLBAR;
                            text_attrs attrs;
                            auto ch = gutter_y == overlay_y
                                ? NCACS_URCORNER
                                : (gutter_y == (overlay_y + overlay_height - 1)
                                       ? NCACS_LRCORNER
                                       : NCACS_VLINE);

                            if (gutter_y >= scroll_y && gutter_y <= lines) {
                                role = bar_role;
                            }
                            attrs = vc.attrs_for_role(role);
                            ncplane_putstr_yx(
                                this->lv_window, gutter_y, x + width - 2, ch);
                            ncplane_set_cell_yx(
                                this->lv_window,
                                gutter_y,
                                x + width - 2,
                                attrs.ta_attrs | NCSTYLE_ALTCHARSET,
                                view_colors::to_channels(attrs));
                        }
                    }
                }

                ++row;
            } else {
                nccell clear_cell;
                nccell_init(&clear_cell);
                nccell_prime(this->lv_window,
                             &clear_cell,
                             " ",
                             0,
                             view_colors::to_channels(role_attrs));
                ncplane_cursor_move_yx(this->lv_window, y, x);
                ncplane_hline(this->lv_window, &clear_cell, width);
                nccell_release(this->lv_window, &clear_cell);

                this->lv_display_lines.push_back(empty_space{});
                ++y;
            }
        }

        if (this->lv_selectable && !this->lv_sync_selection_and_top
            && this->lv_selection >= 0 && row < this->lv_selection)
        {
            this->shift_top(this->lv_selection - row + this->lv_tail_space);
            continue;
        }

        if (this->lv_show_scrollbar) {
            auto scroll_offset = (this->lv_border_left_role ? 1 : 0);
            auto avail_height = height - scroll_offset;
            double progress = 1.0;
            double coverage = 1.0;
            double adjusted_height = (double) row_count / (double) avail_height;

            if (row_count > 0) {
                progress = (double) this->lv_top / (double) row_count;
                coverage = (double) avail_height / (double) row_count;
            }

            this->lv_scroll_top
                = scroll_offset + (int) (progress * (double) avail_height);
            this->lv_scroll_bottom = this->lv_scroll_top
                + std::min((int) avail_height,
                           (int) (coverage * (double) avail_height));

            for (auto gutter_y = this->vc_y + scroll_offset;
                 gutter_y < (this->vc_y + height);
                 gutter_y++)
            {
                int range_start = 0, range_end;
                auto role = this->vc_default_role;
                auto bar_role = role_t::VCR_SCROLLBAR;
                text_attrs attrs;
                const char* ch;

                if (row_count > 0) {
                    range_start
                        = (double) (gutter_y - this->vc_y) * adjusted_height;
                }
                range_end = range_start + adjusted_height;

                this->lv_gutter_source->listview_gutter_value_for_range(
                    *this, range_start, range_end, ch, role, bar_role);
                if (gutter_y >= this->vc_y + this->lv_scroll_top
                    && gutter_y <= this->vc_y + this->lv_scroll_bottom)
                {
                    role = bar_role;
                }
                attrs = vc.attrs_for_role(role);
                ncplane_putstr_yx(this->lv_window, gutter_y, x + width - 1, ch);
                ncplane_set_cell_yx(this->lv_window,
                                    gutter_y,
                                    x + width - 1,
                                    attrs.ta_attrs | NCSTYLE_ALTCHARSET,
                                    view_colors::to_channels(attrs));
            }
        }

        if (this->lv_show_bottom_border) {
            auto bottom_y = this->vc_y + height - 1;
            for (size_t lpc = 0; lpc < width - 1; lpc++) {
                ncplane_on_styles_yx(
                    this->lv_window, bottom_y, x + lpc, NCSTYLE_UNDERLINE);
            }
        }

        this->vc_needs_update = false;
    }

    return view_curses::do_update() || retval;
}

void
listview_curses::set_show_details_in_overlay(bool val)
{
    if (this->lv_overlay_source == nullptr) {
        return;
    }

    this->lv_overlay_source->set_show_details_in_overlay(val);
    if (!val) {
        return;
    }

    this->update_top_from_selection();
}

void
listview_curses::shift_selection(shift_amount_t sa)
{
    vis_line_t height;
    unsigned long width;

    this->get_dimensions(height, width);
    if (this->lv_overlay_focused) {
        const auto focused = this->get_selection();
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
                        < (ssize_t) overlay_content.size())
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
                    new_selection = top_for_last + 1_vl;
                }
            }
        }

        this->set_selection_without_context(new_selection);
        this->update_top_from_selection();
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
    static constexpr auto GUTTER_REPEAT_DELAY
        = std::chrono::duration_cast<std::chrono::microseconds>(100ms).count();

    if (view_curses::handle_mouse(me)) {
        return true;
    }

    if (!this->vc_enabled) {
        return false;
    }

    vis_line_t inner_height, height;
    unsigned long width;

    auto diff = me.me_time - this->lv_mouse_time;
    this->get_dimensions(height, width);
    inner_height = this->get_inner_height();

    switch (me.me_button) {
        case mouse_button_t::BUTTON_SCROLL_UP:
        case mouse_button_t::BUTTON_SCROLL_DOWN: {
            if (me.me_y < (ssize_t) this->lv_display_lines.size()) {
                auto display_content = this->lv_display_lines[me.me_y];

                if (display_content.is<overlay_content>()
                    && this->lv_overlay_focused)
                {
                    auto oc = display_content.get<overlay_content>();

                    if (oc.oc_height < oc.oc_inner_height) {
                        if (me.me_button == mouse_button_t::BUTTON_SCROLL_UP
                            && this->lv_focused_overlay_top > 0_vl)
                        {
                            this->lv_mouse_time = me.me_time;
                            this->lv_focused_overlay_top -= 1_vl;
                            if (this->lv_focused_overlay_selection
                                >= this->lv_focused_overlay_top
                                    + (oc.oc_height - 1_vl))
                            {
                                this->lv_focused_overlay_selection
                                    = this->lv_focused_overlay_top
                                    + oc.oc_height - 2_vl;
                            }
                            this->set_needs_update();
                        }

                        if (me.me_button == mouse_button_t::BUTTON_SCROLL_DOWN
                            && this->lv_focused_overlay_top
                                < (oc.oc_inner_height - oc.oc_height))
                        {
                            this->lv_mouse_time = me.me_time;
                            this->lv_focused_overlay_top += 1_vl;
                            if (this->lv_focused_overlay_selection
                                <= this->lv_focused_overlay_top)
                            {
                                this->lv_focused_overlay_selection
                                    = this->lv_focused_overlay_top + 1_vl;
                            }
                            this->set_needs_update();
                        }
                    }
                    return true;
                }
            }
            this->shift_top(vis_line_t(scroll_polarity(me.me_button) * 2_vl),
                            true);
            return true;
        }
        default:
            break;
    }
    if (me.me_button != mouse_button_t::BUTTON_LEFT || inner_height == 0
        || (me.me_press_x < (int) (width - 2)))
    {
        return false;
    }

    if (me.is_double_click_in(mouse_button_t::BUTTON_LEFT,
                              line_range{(int) width - 2, (int) width}))
    {
        auto pct = (double) inner_height / (double) height;
        auto new_top = (int) floor(((double) me.me_y * pct) + 0.5);
        this->set_top(vis_line_t(new_top), true);
        this->lv_mouse_mode = lv_mode_t::NONE;
        return true;
    }

    switch (this->lv_mouse_mode) {
        case lv_mode_t::NONE: {
            if (me.me_x < (int) (width - 2)) {
                return false;
            }
            break;
        }
        case lv_mode_t::DRAG:
            break;
        case lv_mode_t::UP:
        case lv_mode_t::DOWN:
            if (me.me_x < (int) (width - 2)) {
                return true;
            }
            break;
    }
    if (me.me_state != mouse_button_state_t::BUTTON_STATE_RELEASED
        && this->lv_mouse_mode != lv_mode_t::DRAG && diff.tv_sec == 0
        && diff.tv_usec < GUTTER_REPEAT_DELAY)
    {
        return true;
    }
    this->lv_mouse_time = me.me_time;

    if (me.me_state == mouse_button_state_t::BUTTON_STATE_RELEASED) {
        this->lv_mouse_y = -1;
        this->lv_mouse_mode = lv_mode_t::NONE;
        return true;
    }

    int shift_amount = 0;

    if (this->lv_mouse_mode == lv_mode_t::NONE) {
        if (this->lv_scroll_top <= me.me_y && me.me_y <= this->lv_scroll_bottom)
        {
            this->lv_mouse_mode = lv_mode_t::DRAG;
            this->lv_mouse_y = me.me_y - this->lv_scroll_top;
        } else if (me.me_y < this->lv_scroll_top) {
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
            if (me.me_y < this->lv_scroll_top) {
                shift_amount = -1 * height;
            }
            break;

        case lv_mode_t::DOWN:
            if (me.me_y > this->lv_scroll_bottom) {
                shift_amount = height;
            }
            break;

        case lv_mode_t::DRAG: {
            auto pct = (double) inner_height / (double) height;
            auto new_top = me.me_y - this->lv_mouse_y;
            new_top = (int) floor(((double) new_top * pct) + 0.5);
            this->set_top(vis_line_t(new_top), true);
            break;
        }
    }

    if (shift_amount != 0) {
        this->shift_top(vis_line_t(shift_amount), true);
    }

    return true;
}

enum class selection_location_t {
    upper,
    middle,
    lower,
};

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
        this->lv_top = top;
        if (this->lv_selectable) {
            if (this->lv_selection < 0_vl) {
                this->set_selection_without_context(top);
            } else {
                auto layout = this->layout_for_row(this->lv_top);
                auto last_row = this->lv_top
                    + vis_line_t(layout.lr_below_line_heights.size());

                if (this->lv_top <= this->lv_selection
                    && this->lv_selection <= last_row)
                {
                    // selection is already in view, nothing to do
                } else if (layout.lr_below_line_heights.size() < 2) {
                    this->set_selection_without_context(this->lv_top);
                } else {
                    auto sel_location = selection_location_t::middle;

                    if (this->lv_top - 5_vl <= this->lv_selection
                        && this->lv_selection < this->lv_top)
                    {
                        sel_location = selection_location_t::upper;
                    } else if (last_row < this->lv_selection
                               && this->lv_selection <= last_row + 5_vl)
                    {
                        sel_location = selection_location_t::lower;
                    }

                    switch (sel_location) {
                        case selection_location_t::upper: {
                            this->set_selection_without_context(this->lv_top
                                                                + 1_vl);
                            break;
                        }
                        case selection_location_t::middle: {
                            auto middle_of_below = vis_line_t(
                                layout.lr_below_line_heights.size() / 2);
                            this->set_selection_without_context(
                                this->lv_top + middle_of_below);
                            break;
                        }
                        case selection_location_t::lower: {
                            this->set_selection_without_context(last_row);
                            break;
                        }
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
listview_curses::height_for_row(vis_line_t row,
                                vis_line_t height,
                                unsigned long width) const
{
    auto retval = 1_vl;

    if (this->lv_word_wrap) {
        // source size plus some padding for decorations
        auto len = this->lv_source->listview_size_for_row(*this, row) + 5;

        while (len > width) {
            len -= width;
            retval += 1_vl;
        }
    }
    if (this->lv_overlay_source != nullptr) {
        std::vector<attr_line_t> overlay_content;

        this->lv_overlay_source->list_value_for_overlay(
            *this, row, overlay_content);
        retval += vis_line_t(
            this->get_overlay_height(overlay_content.size(), height));
    }

    return retval;
}

listview_curses::layout_result_t
listview_curses::layout_for_row(vis_line_t row) const
{
    auto [height, width] = this->get_dimensions();
    const auto inner_height = this->get_inner_height();
    layout_result_t retval;

    retval.lr_desired_row = row;
    retval.lr_desired_row_height = this->height_for_row(row, height, width);
    {
        auto above_height_avail
            = height - retval.lr_desired_row_height - this->lv_tail_space;
        auto curr_above_row = row - 1_vl;
        while (curr_above_row >= 0_vl && above_height_avail > 0_vl) {
            auto curr_above_height
                = this->height_for_row(curr_above_row, height, width);

            above_height_avail -= curr_above_height;
            if (above_height_avail < 0_vl) {
                break;
            }
            curr_above_row -= 1_vl;
            retval.lr_above_line_heights.emplace_back(curr_above_height);
        }
    }
    {
        auto below_height_avail
            = height - retval.lr_desired_row_height - this->lv_tail_space;
        auto curr_below_row = row + 1_vl;
        while (curr_below_row < inner_height && below_height_avail > 0_vl) {
            auto curr_below_height
                = this->height_for_row(curr_below_row, height, width);

            below_height_avail -= curr_below_height;
            if (below_height_avail < 0_vl) {
                break;
            }
            curr_below_row += 1_vl;
            retval.lr_below_line_heights.emplace_back(curr_below_height);
        }
    }

    return retval;
}

vis_line_t
listview_curses::rows_available(vis_line_t line, row_direction_t dir) const
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
listview_curses::set_selection_without_context(vis_line_t sel)
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

void
listview_curses::set_selection(vis_line_t sel)
{
    if (!this->lv_selectable) {
        if (sel >= 0_vl) {
            this->set_top(sel);
        }
        return;
    }

    if (sel < 0_vl) {
        return;
    }

    this->set_selection_without_context(sel);
    this->update_top_from_selection();
}

vis_line_t
listview_curses::get_top_for_last_row()
{
    auto inner_height = this->get_inner_height();
    auto retval = 0_vl;

    if (inner_height > 0) {
        auto last_line = inner_height - 1_vl;
        const auto layout = this->layout_for_row(last_line);

        retval = last_line - vis_line_t(layout.lr_above_line_heights.size());
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

void
listview_curses::set_left(int left)
{
    if (this->lv_left == left || left < 0) {
        return;
    }

    if (left > this->lv_left) {
        unsigned long width;
        vis_line_t height;

        this->get_dimensions(height, width);
        if (this->lv_show_scrollbar) {
            width -= 1;
        }
        if ((this->get_inner_width() - this->lv_left) <= width) {
            alerter::singleton().chime(
                "the maximum width of the view has been reached");
            return;
        }
    }

    this->lv_left = left;
    this->invoke_scroll();
    this->set_needs_update();
}

vis_line_t
listview_curses::get_overlay_height(size_t total, vis_line_t view_height) const
{
    return std::min(vis_line_t{(int) total}, (2_vl * (view_height / 3_vl)));
}

void
listview_curses::set_overlay_selection(std::optional<vis_line_t> sel)
{
    if (this->lv_overlay_source == nullptr) {
        return;
    }
    if (sel) {
        if (this->lv_overlay_focused
            && sel.value() == this->lv_focused_overlay_selection)
        {
            return;
        }

        std::vector<attr_line_t> overlay_content;
        this->lv_overlay_source->list_value_for_overlay(
            *this, this->get_selection(), overlay_content);
        if (!overlay_content.empty()) {
            this->lv_overlay_focused = true;
            if (sel.value() < 0) {
                this->lv_focused_overlay_selection = 0_vl;
            } else if (sel.value() >= (ssize_t) overlay_content.size()) {
                this->lv_focused_overlay_selection
                    = vis_line_t(overlay_content.size()) - 1_vl;
            } else {
                this->lv_focused_overlay_selection = sel.value();
            }

            const auto [height, width] = this->get_dimensions();
            auto bot = this->get_bottom();
            auto overlay_height = vis_line_t(
                this->get_overlay_height(overlay_content.size(), height));

            if (this->lv_selection + overlay_height >= bot) {
                this->set_top(this->lv_selection, true);
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

int
listview_curses::get_y_for_selection() const
{
    return this->get_y() + (int) (this->get_selection() - this->get_top());
}
