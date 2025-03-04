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

#include <optional>
#include <stack>
#include <vector>

#include "base/lrucache.hpp"
#include "listview_curses.hh"
#include "log_data_helper.hh"
#include "logfile_sub_source.hh"
#include "text_overlay_menu.hh"

class field_overlay_source : public text_overlay_menu {
public:
    explicit field_overlay_source(logfile_sub_source& lss,
                                  text_sub_source& tss)
        : fos_lss(lss), fos_tss(tss), fos_log_helper(lss)
    {
    }

    void add_key_line_attrs(int key_size, bool last_line = false);

    void reset() override
    {
        this->fos_lines.clear();
        this->fos_meta_lines.clear();
    }

    bool list_static_overlay(const listview_curses& lv,
                             int y,
                             int bottom,
                             attr_line_t& value_out) override;

    std::optional<attr_line_t> list_header_for_overlay(
        const listview_curses& lv, vis_line_t vl) override;

    void list_value_for_overlay(const listview_curses& lv,
                                vis_line_t row,
                                std::vector<attr_line_t>& value_out) override;

    void build_field_lines(const listview_curses& lv, vis_line_t row);
    void build_meta_line(const listview_curses& lv,
                         std::vector<attr_line_t>& dst,
                         vis_line_t row);

    void set_show_details_in_overlay(bool val) override
    {
        this->fos_contexts.top().c_show = val;
    }

    bool get_show_details_in_overlay() const override
    {
        return this->fos_contexts.top().c_show;
    }

    struct context {
        context(std::string prefix,
                bool show,
                bool show_discovered,
                bool show_applicable_annotations)
            : c_prefix(std::move(prefix)), c_show(show),
              c_show_discovered(show_discovered),
              c_show_applicable_annotations(show_applicable_annotations)
        {
        }

        std::string c_prefix;
        bool c_show{false};
        bool c_show_discovered{true};
        bool c_show_applicable_annotations{true};
    };

    std::stack<context> fos_contexts;
    logfile_sub_source& fos_lss;
    text_sub_source& fos_tss;
    uint32_t fos_index_generation{0};
    cache::lru_cache<vis_line_t, std::optional<attr_line_t>> fos_anno_cache{
        256};
    log_data_helper fos_log_helper;
    int fos_known_key_size{0};
    int fos_unknown_key_size{0};
    std::vector<attr_line_t> fos_lines;
    std::vector<attr_line_t> fos_meta_lines;

    struct row_info {
        std::optional<logline_value_meta> ri_meta;
        std::string ri_value;
    };

    std::map<size_t, row_info> fos_row_to_field_meta;
};

#endif  // LNAV_FIELD_OVERLAY_SOURCE_H
