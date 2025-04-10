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
 */

#ifndef db_sub_source_hh
#define db_sub_source_hh

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "ArenaAlloc/arenaalloc.h"
#include "base/cell_container.hh"
#include "base/lnav.resolver.hh"
#include "hist_source.hh"
#include "textview_curses.hh"

class db_label_source
    : public text_sub_source
    , public text_time_translator
    , public list_input_delegate
    , public text_delegate
    , public text_detail_provider {
public:
    bool has_log_time_column() const { return !this->dls_time_column.empty(); }

    bool empty() const override { return this->dls_headers.empty(); }

    size_t text_line_count() override { return this->dls_row_cursors.size(); }

    size_t text_size_for_line(textview_curses& tc,
                              int line,
                              line_flags_t flags) override
    {
        return this->text_line_width(tc);
    }

    size_t text_line_width(textview_curses& curses) override
    {
        size_t retval = 0;

        for (const auto& dls_header : this->dls_headers) {
            retval += dls_header.hm_column_size + 1;
        }
        return retval;
    }

    line_info text_value_for_line(textview_curses& tc,
                                  int row,
                                  std::string& label_out,
                                  line_flags_t flags) override;

    void text_attrs_for_line(textview_curses& tc,
                             int row,
                             string_attrs_t& sa) override;

    void push_header(const std::string& colstr, int type);

    void set_col_as_graphable(int lpc);

    using column_value_t
        = mapbox::util::variant<string_fragment, int64_t, double, null_value_t>;

    static_assert(!column_value_t::needs_destruct);
    void push_column(const column_value_t& sv);

    void clear();

    std::optional<size_t> column_name_to_index(const std::string& name) const;

    std::optional<vis_line_t> row_for_time(timeval time_bucket) override;

    std::optional<row_info> time_for_row(vis_line_t row) override;

    bool text_handle_mouse(textview_curses& tc,
                           const listview_curses::display_line_content_t&,
                           mouse_event& me) override;

    bool list_input_handle_key(listview_curses& lv, const ncinput& ch) override;

    std::string get_row_as_string(vis_line_t row);

    std::optional<json_string> text_row_details(
        const textview_curses& tc) override;

    std::string get_cell_as_string(vis_line_t row, size_t col);
    std::optional<int64_t> get_cell_as_int64(vis_line_t row, size_t col);
    std::optional<double> get_cell_as_double(vis_line_t row, size_t col);

    void update_time_column(const string_fragment& sf);

    void reset_user_state();

    struct header_meta {
        explicit header_meta(std::string name) : hm_name(std::move(name)) {}

        bool operator==(const std::string& name) const
        {
            return this->hm_name == name;
        }

        bool is_graphable() const
        {
            return this->hm_graphable && this->hm_graphable.value();
        }

        std::string hm_name;
        int hm_column_type{SQLITE3_TEXT};
        unsigned int hm_sub_type{0};
        bool hm_hidden{false};
        std::optional<bool> hm_graphable;
        size_t hm_column_size{0};
        text_align_t hm_align{text_align_t::start};
        text_attrs hm_title_attrs{text_attrs::with_underline()};
        stacked_bar_chart<std::string> hm_chart;
    };

    struct row_style {
        std::map<int, text_attrs> rs_column_config;
    };

    uint32_t dls_generation{0};
    std::optional<std::chrono::steady_clock::time_point> dls_query_start;
    std::optional<std::chrono::steady_clock::time_point> dls_query_end;
    size_t dls_max_column_width{120};
    std::vector<header_meta> dls_headers;
    lnav::cell_container dls_cell_container;
    std::vector<lnav::cell_container::cursor> dls_row_cursors;
    size_t dls_push_column{0};
    std::vector<timeval> dls_time_column;
    std::vector<size_t> dls_cell_width;
    size_t dls_time_column_index{SIZE_MAX};
    std::optional<size_t> dls_time_column_invalidated_at;
    std::optional<size_t> dls_level_column;
    std::vector<row_style> dls_row_styles;
    bool dls_row_styles_have_errors{false};
    size_t dls_row_style_column{SIZE_MAX};
    ArenaAlloc::Alloc<char> dls_cell_allocator{1024};
    string_attrs_t dls_ansi_attrs;

    static const unsigned char NULL_STR[];
};

class db_overlay_source : public list_overlay_source {
public:
    bool list_static_overlay(const listview_curses& lv,
                             int y,
                             int bottom,
                             attr_line_t& value_out) override;

    void list_value_for_overlay(const listview_curses& lv,
                                vis_line_t line,
                                std::vector<attr_line_t>& value_out) override;

    std::optional<attr_line_t> list_header_for_overlay(
        const listview_curses& lv, vis_line_t line) override;

    void set_show_details_in_overlay(bool val) override
    {
        this->dos_active = val;
    }

    bool get_show_details_in_overlay() const override
    {
        return this->dos_active;
    }

    bool dos_active{false};
    db_label_source* dos_labels{nullptr};
};

#endif
