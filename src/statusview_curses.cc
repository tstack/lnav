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
 * @file statusview_curses.cc
 */

#include <algorithm>
#include <vector>

#include "statusview_curses.hh"

#include "base/ansi_scrubber.hh"
#include "base/itertools.hh"
#include "config.h"

void
status_field::no_op_action(status_field&)
{
}

void
status_field::set_value(std::string value)
{
    auto& sa = this->sf_value.get_attrs();

    sa.clear();

    scrub_ansi_string(value, &sa);
    this->sf_value.with_string(value);
}

void
status_field::do_cylon()
{
    auto& sa = this->sf_value.get_attrs();

    remove_string_attr(sa, &VC_STYLE);

    auto cycle_pos = (this->sf_cylon_pos % (4 + this->sf_width * 2)) - 2;
    auto start = cycle_pos < this->sf_width
        ? cycle_pos
        : (this->sf_width - (cycle_pos - this->sf_width) - 1);
    auto stop = std::min(start + 3, this->sf_width);
    struct line_range lr(std::max<long>(start, 0L), stop);
    const auto& vc = view_colors::singleton();

    auto attrs = vc.attrs_for_role(role_t::VCR_ACTIVE_STATUS);
    attrs |= text_attrs::style::reverse;
    sa.emplace_back(lr, VC_STYLE.value(attrs));

    this->sf_cylon_pos += 1;
}

void
status_field::set_stitch_value(role_t left, role_t right)
{
    auto& sa = this->sf_value.get_attrs();
    struct line_range lr(0, 1);

    this->sf_value.get_string() = "::";
    sa.clear();
    sa.emplace_back(lr, VC_ROLE.value(left));
    lr.lr_start = 1;
    lr.lr_end = 2;
    sa.emplace_back(lr, VC_ROLE.value(right));
}

bool
statusview_curses::do_update()
{
    int left = 0;
    auto& vc = view_colors::singleton();
    unsigned int width, height;

    this->sc_displayed_fields.clear();
    if (!this->vc_visible || this->sc_window == nullptr) {
        return false;
    }

    ncplane_dim_yx(this->sc_window, &height, &width);
    this->window_change();

    int top = this->vc_y < 0 ? height + this->vc_y : this->vc_y;
    int right = width;
    const auto attrs = vc.attrs_for_role(
        this->sc_enabled ? this->vc_default_role : role_t::VCR_INACTIVE_STATUS);

    nccell clear_cell;
    nccell_init(&clear_cell);
    nccell_prime(
        this->sc_window, &clear_cell, " ", 0, view_colors::to_channels(attrs));
    ncplane_cursor_move_yx(this->sc_window, top, 0);
    ncplane_hline(this->sc_window, &clear_cell, width);
    nccell_release(this->sc_window, &clear_cell);

    if (this->sc_source != nullptr) {
        auto field_count = this->sc_source->statusview_fields();
        for (size_t field = 0; field < field_count; field++) {
            auto& sf = this->sc_source->statusview_value_for_field(field);
            struct line_range lr(0, sf.get_width());
            int x;

            if (sf.is_cylon()) {
                sf.do_cylon();
            }
            auto val = sf.get_value();
            if (!this->sc_enabled) {
                for (auto& sa : val.get_attrs()) {
                    if (sa.sa_type == &VC_STYLE) {
                        auto sa_attrs = sa.sa_value.get<text_attrs>();
                        sa_attrs.clear_style(text_attrs::style::reverse);
                        sa_attrs.ta_fg_color
                            = styling::color_unit::make_empty();
                        sa_attrs.ta_bg_color
                            = styling::color_unit::make_empty();
                        sa.sa_value = sa_attrs;
                    } else if (sa.sa_type == &VC_ROLE) {
                        if (sa.sa_value.get<role_t>()
                            == role_t::VCR_ALERT_STATUS)
                        {
                            sa.sa_value.get<role_t>()
                                = role_t::VCR_INACTIVE_ALERT_STATUS;
                        } else {
                            sa.sa_value = role_t::VCR_NONE;
                        }
                    }
                }
            }
            if (sf.get_left_pad() > 0) {
                val.insert(0, sf.get_left_pad(), ' ');
            }

            if (sf.is_right_justified()) {
                val.right_justify(sf.get_width());

                right -= sf.get_width();
                x = right;
            } else {
                x = left;
                left += sf.get_width();
            }

            if (val.length() > sf.get_width()) {
                static constexpr auto ELLIPSIS = "\xE2\x8B\xAF"_frag;

                if (sf.get_width() > 11) {
                    size_t half_width = sf.get_width() / 2 - 1;

                    val.erase(half_width, val.length() - (half_width * 2));
                    val.insert(half_width, ELLIPSIS);
                } else {
                    val = val.subline(0, sf.get_width() - 1);
                    val.append(ELLIPSIS);
                }
            }

            auto default_role = sf.get_role();
            if (!this->sc_enabled) {
                if (default_role == role_t::VCR_ALERT_STATUS) {
                    default_role = role_t::VCR_INACTIVE_ALERT_STATUS;
                } else if (default_role != role_t::VCR_STATUS_INFO) {
                    default_role = role_t::VCR_INACTIVE_STATUS;
                }
            }

            auto write_res
                = mvwattrline(this->sc_window, top, x, val, lr, default_role);
            this->sc_displayed_fields.emplace_back(
                line_range{x, static_cast<int>(x + write_res.mr_chars_out)},
                field);
        }
    }

    return true;
}

void
statusview_curses::window_change()
{
    if (this->sc_source == nullptr) {
        return;
    }

    int field_count = this->sc_source->statusview_fields();
    int total_shares = 0;
    double remaining = 0;
    std::vector<status_field*> resizable;

    auto width = ncplane_dim_x(this->sc_window);
    remaining = width - 2;

    for (int field = 0; field < field_count; field++) {
        auto& sf = this->sc_source->statusview_value_for_field(field);

        remaining -= sf.get_share() ? sf.get_min_width() : sf.get_width();
        total_shares += sf.get_share();
        if (sf.get_share()) {
            resizable.emplace_back(&sf);
        }
    }

    if (remaining < 2) {
        remaining = 0;
    }

    std::stable_sort(begin(resizable), end(resizable), [](auto l, auto r) {
        return r->get_share() < l->get_share();
    });
    for (auto* sf : resizable) {
        double divisor = total_shares / sf->get_share();
        int available = remaining / divisor;
        int actual_width;

        if ((sf->get_left_pad() + sf->get_value().length())
            < (sf->get_min_width() + available))
        {
            actual_width = std::max(
                (int) sf->get_min_width(),
                (int) (sf->get_left_pad() + sf->get_value().length()));
        } else {
            actual_width = sf->get_min_width() + available;
        }
        remaining -= (actual_width - sf->get_min_width());
        total_shares -= sf->get_share();

        sf->set_width(actual_width);
    }
}

bool
statusview_curses::handle_mouse(mouse_event& me)
{
    auto find_res = this->sc_displayed_fields
        | lnav::itertools::find_if([&me](const auto& elem) {
                        return me.is_click_in(mouse_button_t::BUTTON_LEFT,
                                              elem.df_range.lr_start,
                                              elem.df_range.lr_end);
                    });

    if (find_res) {
        auto& sf = this->sc_source->statusview_value_for_field(
            find_res.value()->df_field_index);

        sf.on_click(sf);
    }

    return true;
}
