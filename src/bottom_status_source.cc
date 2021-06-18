/**
 * Copyright (c) 2019, Timothy Stack
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
 */

#include "config.h"

#include "bottom_status_source.hh"


bottom_status_source::bottom_status_source()
{
    this->bss_fields[BSF_LINE_NUMBER].set_min_width(10);
    this->bss_fields[BSF_LINE_NUMBER].set_share(1000);
    this->bss_fields[BSF_PERCENT].set_width(6);
    this->bss_fields[BSF_PERCENT].set_left_pad(1);
    this->bss_fields[BSF_HITS].set_min_width(10);
    this->bss_fields[BSF_HITS].set_share(5);
    this->bss_fields[BSF_SEARCH_TERM].set_min_width(10);
    this->bss_fields[BSF_SEARCH_TERM].set_share(1);
    this->bss_fields[BSF_LOADING].set_width(13);
    this->bss_fields[BSF_LOADING].set_cylon(true);
    this->bss_fields[BSF_LOADING].right_justify(true);
    this->bss_fields[BSF_HELP].set_width(14);
    this->bss_fields[BSF_HELP].set_value("?:View Help");
    this->bss_fields[BSF_HELP].right_justify(true);
    this->bss_prompt.set_left_pad(1);
    this->bss_prompt.set_min_width(35);
    this->bss_prompt.set_share(1);
    this->bss_error.set_left_pad(1);
    this->bss_error.set_min_width(35);
    this->bss_error.set_share(1);
    this->bss_line_error.set_left_pad(1);
    this->bss_line_error.set_min_width(35);
    this->bss_line_error.set_share(1);
}

void bottom_status_source::update_line_number(listview_curses *lc)
{
    status_field &sf = this->bss_fields[BSF_LINE_NUMBER];

    if (lc->get_inner_height() == 0) {
        sf.set_value(" L0");
    }
    else {
        sf.set_value(" L%'d", (int) lc->get_top());
    }

    if (lc->get_inner_height() > 0) {
        std::vector<attr_line_t> rows(1);

        lc->get_data_source()->
            listview_value_for_rows(*lc, lc->get_top(), rows);
        auto& sa = rows[0].get_attrs();
        auto iter = find_string_attr(sa, &SA_ERROR);
        if (iter != sa.end()) {
            this->bss_line_error.set_value(iter->sa_str_value);
        } else {
            this->bss_line_error.clear();
        }
    } else {
        this->bss_line_error.clear();
    }
}

void bottom_status_source::update_search_term(textview_curses &tc)
{
    auto &sf = this->bss_fields[BSF_SEARCH_TERM];
    auto search_term = tc.get_current_search();

    if (search_term.empty()) {
        sf.clear();
    } else {
        sf.set_value("\"%s\"", search_term.c_str());
    }

    this->bss_paused = tc.is_paused();
    this->update_loading(0, 0);
}

void bottom_status_source::update_percent(listview_curses *lc)
{
    status_field &sf  = this->bss_fields[BSF_PERCENT];
    vis_line_t    top = lc->get_top();
    vis_line_t    bottom, height;
    unsigned long width;
    double        percent;

    lc->get_dimensions(height, width);

    if (lc->get_inner_height() > 0) {
        bottom = std::min(top + height - vis_line_t(1),
                          vis_line_t(lc->get_inner_height() - 1));
        percent  = (double)(bottom + 1);
        percent /= (double)lc->get_inner_height();
        percent *= 100.0;
    }
    else {
        percent = 0.0;
    }
    sf.set_value("%3d%% ", (int)percent);
}

void bottom_status_source::update_marks(listview_curses *lc)
{
    auto *tc = static_cast<textview_curses *>(lc);
    vis_bookmarks   &bm = tc->get_bookmarks();
    status_field    &sf = this->bss_fields[BSF_HITS];

    if (bm.find(&textview_curses::BM_SEARCH) != bm.end()) {
        bookmark_vector<vis_line_t> &bv = bm[&textview_curses::BM_SEARCH];

        if (!bv.empty() || !tc->get_current_search().empty()) {
            bookmark_vector<vis_line_t>::iterator lb;

            lb = std::lower_bound(bv.begin(), bv.end(), tc->get_top());
            if (lb != bv.end() && *lb == tc->get_top()) {
                sf.set_value(
                    "  Hit %'d of %'d for ",
                    std::distance(bv.begin(), lb) + 1, tc->get_match_count());
            } else {
                sf.set_value("  %'d hits for ", tc->get_match_count());
            }
        } else {
            sf.clear();
        }
    }
    else {
        sf.clear();
    }
}

void bottom_status_source::update_hits(textview_curses *tc)
{
    status_field &      sf = this->bss_fields[BSF_HITS];
    view_colors::role_t new_role;

    if (tc->is_searching()) {
        this->bss_hit_spinner += 1;
        if (this->bss_hit_spinner % 2) {
            new_role = view_colors::VCR_ACTIVE_STATUS;
        }
        else{
            new_role = view_colors::VCR_ACTIVE_STATUS2;
        }
        sf.set_cylon(true);
    }
    else {
        new_role = view_colors::VCR_STATUS;
        sf.set_cylon(false);
    }
    // this->bss_error.clear();
    sf.set_role(new_role);
    this->update_marks(tc);
}

void bottom_status_source::update_loading(file_off_t off, file_size_t total)
{
    auto &sf = this->bss_fields[BSF_LOADING];

    require(off >= 0);

    if (total == 0 || (size_t)off == total) {
        sf.set_cylon(false);
        sf.set_role(view_colors::VCR_STATUS);
        if (this->bss_paused) {
            sf.set_value("\xE2\x80\x96 Paused");
        } else {
            sf.clear();
        }
    }
    else {
        int pct = (int)(((double)off / (double)total) * 100.0);

        if (this->bss_load_percent != pct) {
            this->bss_load_percent = pct;

            sf.set_cylon(true);
            sf.set_role(view_colors::VCR_ACTIVE_STATUS2);
            sf.set_value(" Loading %2d%% ", pct);
        }
    }
}

size_t bottom_status_source::statusview_fields()
{
    size_t retval;

    if (this->bss_prompt.empty() &&
        this->bss_error.empty() &&
        this->bss_line_error.empty()) {
        retval = BSF__MAX;
    }
    else{
        retval = 1;
    }

    return retval;
}

status_field &bottom_status_source::statusview_value_for_field(int field)
{
    if (!this->bss_error.empty()) {
        return this->bss_error;
    }
    else if (!this->bss_prompt.empty()) {
        return this->bss_prompt;
    }
    else if (!this->bss_line_error.empty()) {
        return this->bss_line_error;
    }
    else {
        return this->get_field((field_t)field);
    }
}
