/**
 * Copyright (c) 2015, Timothy Stack
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

#ifndef LNAV_FIELD_OVERLAY_SOURCE_H
#define LNAV_FIELD_OVERLAY_SOURCE_H

#include <vector>

#include "listview_curses.hh"
#include "log_data_helper.hh"
#include "logfile_sub_source.hh"

class field_overlay_source : public list_overlay_source {
public:
    field_overlay_source(logfile_sub_source &lss)
            : fos_lss(lss), fos_log_helper(lss) {

    };

    void add_key_line_attrs(int key_size, bool last_line = false) {
        string_attrs_t &sa = this->fos_lines.back().get_attrs();
        struct line_range lr(1, 2);
        sa.push_back(string_attr(lr, &view_curses::VC_GRAPHIC, last_line ? ACS_LLCORNER : ACS_LTEE));

        lr.lr_start = 3 + key_size + 3;
        lr.lr_end   = -1;
        sa.emplace_back(lr, &view_curses::VC_STYLE, A_BOLD);
    };

    bool list_value_for_overlay(const listview_curses &lv,
                                int y, int bottom,
                                vis_line_t row,
                                attr_line_t &value_out) override {
        if (y == 0) {
            this->build_field_lines(lv);
            this->build_summary_lines(lv);
            return false;
        }

        if (1 <= y && y <= (int)this->fos_lines.size()) {
            value_out = this->fos_lines[y - 1];
            return true;
        }

        if (!this->fos_summary_lines.empty() && y == (bottom - 1)) {
            value_out = this->fos_summary_lines[0];
            return true;
        }

        if (!this->fos_meta_lines.empty()) {
            value_out = this->fos_meta_lines.front();
            this->fos_meta_lines.erase(this->fos_meta_lines.begin());

            return true;
        }

        if (row < lv.get_inner_height()) {
            this->build_meta_line(lv, this->fos_meta_lines, row);
        }

        return false;
    };

    void build_field_lines(const listview_curses &lv);
    void build_summary_lines(const listview_curses &lv);
    void build_meta_line(const listview_curses &lv,
                         std::vector<attr_line_t> &dst,
                         vis_line_t row);

    bool fos_active{false};
    bool fos_active_prev{false};
    logfile_sub_source &fos_lss;
    log_data_helper fos_log_helper;
    int fos_known_key_size{0};
    int fos_unknown_key_size{0};
    std::vector<attr_line_t> fos_lines;
    std::vector<attr_line_t> fos_summary_lines;
    std::vector<attr_line_t> fos_meta_lines;
};

#endif //LNAV_FIELD_OVERLAY_SOURCE_H
