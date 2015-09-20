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
            : fos_active(false),
              fos_active_prev(false),
              fos_log_helper(lss),
              fos_known_key_size(0),
              fos_unknown_key_size(0) {

    };

    size_t list_overlay_count(const listview_curses &lv);

    void add_key_line_attrs(int key_size, bool last_line = false) {
        string_attrs_t &sa = this->fos_lines.back().get_attrs();
        struct line_range lr(1, 2);
        sa.push_back(string_attr(lr, &view_curses::VC_GRAPHIC, last_line ? ACS_LLCORNER : ACS_LTEE));

        lr.lr_start = 3 + key_size + 3;
        lr.lr_end   = -1;
        sa.push_back(string_attr(lr, &view_curses::VC_STYLE, A_BOLD));
    };

    bool list_value_for_overlay(const listview_curses &lv,
                                vis_line_t y,
                                attr_line_t &value_out)
    {
        if (!this->fos_active || this->fos_log_helper.ldh_parser.get() == NULL) {
            return false;
        }

        int  row       = (int)y - 1;

        if (row < 0 || row >= (int)this->fos_lines.size()) {
            return false;
        }

        value_out = this->fos_lines[row];

        return true;
    };

    bool          fos_active;
    bool          fos_active_prev;
    log_data_helper fos_log_helper;
    int fos_known_key_size;
    int fos_unknown_key_size;
    std::vector<attr_line_t> fos_lines;
};

#endif //LNAV_FIELD_OVERLAY_SOURCE_H
