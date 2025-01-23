/**
 * Copyright (c) 2025, Timothy Stack
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
 * @file hist_source.hh
 */

#ifndef hist_source_T_hh
#define hist_source_T_hh

#include "hist_source.hh"

template<typename T>
void
stacked_bar_chart<T>::chart_attrs_for_value(
    const listview_curses& lc,
    int& left,
    unsigned long width,
    const T& ident,
    double value,
    string_attrs_t& value_out,
    std::optional<text_attrs> user_attrs) const
{
    auto ident_iter = this->sbc_ident_lookup.find(ident);

    require(ident_iter != this->sbc_ident_lookup.end());

    size_t ident_index = ident_iter->second;
    unsigned long avail_width;
    bucket_stats_t overall_stats;
    line_range lr;

    // lr.lr_unit = line_range::unit::codepoint;

    size_t ident_to_show = this->sbc_show_state.match(
        [](const show_none) { return -1; },
        [ident_index](const show_all) { return ident_index; },
        [](const show_one& one) { return one.so_index; });

    if (ident_to_show != ident_index) {
        return;
    }

    for (size_t lpc = 0; lpc < this->sbc_idents.size(); lpc++) {
        if (this->sbc_show_state.template is<show_all>()
            || lpc == (size_t) ident_to_show)
        {
            overall_stats.merge(this->sbc_idents[lpc].ci_stats);
        }
    }
    if (this->sbc_max_row_value > overall_stats.bs_max_value) {
        overall_stats.bs_max_value = this->sbc_max_row_value;
    }
    if (this->sbc_row_sum > overall_stats.bs_max_value) {
        overall_stats.bs_max_value = this->sbc_row_sum;
    }

    if (this->sbc_show_state.template is<show_all>()) {
        if (this->sbc_idents.size() == 1) {
            avail_width = width;
        } else if (width < this->sbc_max_row_items) {
            avail_width = 0;
        } else {
            avail_width = width;
        }
    } else {
        avail_width = width - 1;
    }
    if (avail_width > (this->sbc_left + this->sbc_right)) {
        avail_width -= this->sbc_left + this->sbc_right;
    }

    lr.lr_start = left;

    const auto& ci = this->sbc_idents[ident_index];
    int amount;

    if (value == 0.0 || avail_width < 0) {
        amount = 0;
    } else if ((overall_stats.bs_max_value - 0.01) <= value
               && value <= (overall_stats.bs_max_value + 0.01))
    {
        amount = avail_width;
    } else {
        double percent
            = (value - overall_stats.bs_min_value) / overall_stats.width();
        amount = (int) rint(percent * avail_width);
        amount = std::max(1, amount);
    }
    require_ge(amount, 0);
    lr.lr_end = left = lr.lr_start + amount;

    if (!ci.ci_attrs.empty() && !lr.empty()) {
        auto rev_attrs = ci.ci_attrs | text_attrs::style::reverse;
        if (user_attrs) {
            if (!user_attrs->ta_fg_color.empty()) {
                rev_attrs.ta_fg_color = user_attrs->ta_fg_color;
            }
            if (!user_attrs->ta_bg_color.empty()) {
                rev_attrs.ta_bg_color = user_attrs->ta_bg_color;
            }
        }
        value_out.emplace_back(lr, VC_STYLE.value(rev_attrs));
    }
}

#endif
