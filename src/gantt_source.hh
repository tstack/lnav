/**
 * Copyright (c) 2023, Timothy Stack
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

#ifndef lnav_gantt_source_hh
#define lnav_gantt_source_hh

#include "gantt_status_source.hh"
#include "logfile_sub_source.hh"
#include "plain_text_source.hh"
#include "textview_curses.hh"

class gantt_source
    : public text_sub_source
    , public text_time_translator {
public:
    explicit gantt_source(textview_curses& log_view,
                          logfile_sub_source& lss,
                          plain_text_source& preview_source,
                          gantt_status_source& preview_status_source);

    size_t text_line_count() override;

    size_t text_line_width(textview_curses& curses) override;

    void text_value_for_line(textview_curses& tc,
                             int line,
                             std::string& value_out,
                             line_flags_t flags) override;

    void text_attrs_for_line(textview_curses& tc,
                             int line,
                             string_attrs_t& value_out) override;

    size_t text_size_for_line(textview_curses& tc,
                              int line,
                              line_flags_t raw) override;

    void text_selection_changed(textview_curses& tc) override;

    void text_filters_changed() override;
    int get_filtered_count() const override;
    int get_filtered_count_for(size_t filter_index) const override;

    void text_crumbs_for_line(int line,
                              std::vector<breadcrumb::crumb>& crumbs) override;

    nonstd::optional<vis_line_t> row_for_time(
        struct timeval time_bucket) override;
    nonstd::optional<struct timeval> time_for_row(vis_line_t row) override;

    void rebuild_indexes();

    std::pair<timeval, timeval> get_time_bounds_for(int line);

    textview_curses& gs_log_view;
    logfile_sub_source& gs_lss;
    plain_text_source& gs_preview_source;
    gantt_status_source& gs_preview_status_source;
    ArenaAlloc::Alloc<char> gs_allocator{64 * 1024};

    struct opid_description_defs {
        std::map<intern_string_t,
                 std::map<intern_string_t, log_format::opid_descriptors>>
            odd_format_to_desc;
    };

    using gantt_opid_map
        = robin_hood::unordered_map<string_fragment,
                                    opid_description_defs,
                                    frag_hasher,
                                    std::equal_to<string_fragment>>;

    using gantt_subid_map
        = robin_hood::unordered_map<string_fragment,
                                    bool,
                                    frag_hasher,
                                    std::equal_to<string_fragment>>;

    gantt_opid_map gs_opid_map;
    gantt_subid_map gs_subid_map;

    struct opid_row {
        string_fragment or_name;
        opid_time_range or_value;
        std::map<intern_string_t,
                 std::map<intern_string_t, std::map<size_t, std::string>>>
            or_descriptions;
        std::string or_description;
    };

    attr_line_t gs_rendered_line;
    size_t gs_opid_width{0};
    size_t gs_total_width{0};
    std::vector<opid_row> gs_time_order;
    struct timeval gs_lower_bound {};
    struct timeval gs_upper_bound {};
    size_t gs_filtered_count{0};
    std::array<size_t, logfile_filter_state::MAX_FILTERS> gs_filter_hits{};
    exec_context* gs_exec_context;
};

class gantt_header_overlay : public list_overlay_source {
public:
    explicit gantt_header_overlay(std::shared_ptr<gantt_source> src);

    bool list_static_overlay(const listview_curses& lv,
                             int y,
                             int bottom,
                             attr_line_t& value_out) override;

    nonstd::optional<attr_line_t> list_header_for_overlay(
        const listview_curses& lv, vis_line_t line) override;

    void list_value_for_overlay(const listview_curses& lv,
                                vis_line_t line,
                                std::vector<attr_line_t>& value_out) override;

private:
    std::shared_ptr<gantt_source> gho_src;
};

#endif
