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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef LNAV_FIELD_OVERLAY_SOURCE_H
#define LNAV_FIELD_OVERLAY_SOURCE_H

#include <utility>
#include <vector>

#include "listview_curses.hh"
#include "log_data_helper.hh"
#include "logfile_sub_source.hh"
#include "textfile_sub_source.hh"

class field_overlay_source : public list_overlay_source {
public:
    explicit field_overlay_source(logfile_sub_source& lss,
                                  textfile_sub_source& tss)
        : fos_lss(lss), fos_tss(tss), fos_log_helper(lss)
    {
    }

    void add_key_line_attrs(int key_size, bool last_line = false);

    void reset() override
    {
        this->fos_lines.clear();
        this->fos_meta_lines.clear();
        this->fos_meta_lines_row = -1_vl;
    }

    bool list_value_for_overlay(const listview_curses& lv,
                                int y,
                                int bottom,
                                vis_line_t row,
                                attr_line_t& value_out) override;

    void build_field_lines(const listview_curses& lv, vis_line_t row);
    void build_meta_line(const listview_curses& lv,
                         std::vector<attr_line_t>& dst,
                         vis_line_t row);

    struct context {
        context(std::string prefix, bool show, bool show_discovered)
            : c_prefix(std::move(prefix)), c_show(show),
              c_show_discovered(show_discovered)
        {
        }

        std::string c_prefix;
        bool c_show{false};
        bool c_show_discovered{true};
    };

    bool fos_show_status{true};
    std::stack<context> fos_contexts;
    logfile_sub_source& fos_lss;
    textfile_sub_source& fos_tss;
    log_data_helper fos_log_helper;
    int fos_known_key_size{0};
    int fos_unknown_key_size{0};
    std::vector<attr_line_t> fos_lines;
    vis_line_t fos_meta_lines_row{0_vl};
    std::vector<attr_line_t> fos_meta_lines;
};

#endif  // LNAV_FIELD_OVERLAY_SOURCE_H
