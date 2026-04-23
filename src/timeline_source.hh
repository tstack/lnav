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

#ifndef lnav_timeline_source_hh
#define lnav_timeline_source_hh

#include <array>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/attr_line.hh"
#include "base/map_util.hh"
#include "base/progress.hh"
#include "logfile_sub_source.hh"
#include "plain_text_source.hh"
#include "robin_hood/robin_hood.h"
#include "statusview_curses.hh"
#include "text_overlay_menu.hh"
#include "textview_curses.hh"
#include "timeline_status_source.hh"

class timeline_preview_overlay : public list_overlay_source {
public:
    std::vector<attr_line_t> list_overlay_menu(const listview_curses& lv,
                                               vis_line_t line) override;
};

class timeline_header_overlay;

class timeline_source
    : public text_sub_source
    , public list_input_delegate
    , public text_time_translator
    , public text_delegate {
public:
    friend timeline_header_overlay;

    explicit timeline_source(textview_curses& log_view,
                             logfile_sub_source& lss,
                             textview_curses& preview_view,
                             plain_text_source& preview_source,
                             statusview_curses& preview_status_view,
                             timeline_status_source& preview_status_source);

    bool empty() const override { return false; }

    bool list_input_handle_key(listview_curses& lv, const ncinput& ch) override;

    bool text_handle_mouse(textview_curses& tc,
                           const listview_curses::display_line_content_t&,
                           mouse_event& me) override;

    size_t text_line_count() override;

    size_t text_line_width(textview_curses& curses) override;

    line_info text_value_for_line(textview_curses& tc,
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
    void clear_preview() override;
    void add_commands_for_session(
        const std::function<void(const std::string&)>& receiver) override;
    int get_filtered_count() const override;
    int get_filtered_count_for(size_t filter_index) const override;

    void text_crumbs_for_line(int line,
                              std::vector<breadcrumb::crumb>& crumbs) override;

    std::optional<vis_line_t> row_for_time(timeval time_bucket) override;
    std::optional<vis_line_t> row_for(const row_info& ri) override;
    std::optional<row_info> time_for_row(vis_line_t row) override;

    bool rebuild_indexes();

    Result<std::string, lnav::console::user_message> text_reload_data(
        exec_context& ec) override;

    std::pair<std::chrono::microseconds, std::chrono::microseconds>
    get_time_bounds_for(int line);

    textview_curses& ts_log_view;
    logfile_sub_source& ts_lss;
    textview_curses& ts_preview_view;
    plain_text_source& ts_preview_source;
    statusview_curses& ts_preview_status_view;
    timeline_status_source& ts_preview_status_source;
    ArenaAlloc::Alloc<char> ts_allocator{64 * 1024};
    bool ts_rebuild_in_progress{false};

    struct opid_description_def_key {
        intern_string_t oddk_format_name;
        size_t oddk_desc_index;

        bool operator<(const opid_description_def_key& rhs) const
        {
            if (this->oddk_format_name < rhs.oddk_format_name) {
                return true;
            }
            if (this->oddk_format_name == rhs.oddk_format_name) {
                return this->oddk_desc_index < rhs.oddk_desc_index;
            }

            return false;
        }
    };

    struct opid_description_defs {
        lnav::map::small<opid_description_def_key, log_format::opid_descriptors>
            odd_defs;
    };

    using timeline_subid_map
        = robin_hood::unordered_map<string_fragment,
                                    bool,
                                    frag_hasher,
                                    std::equal_to<string_fragment>>;

    timeline_subid_map ts_subid_map;

    enum class row_type {
        logfile,
        thread,
        opid,
        tag,
        partition,
    };

    struct opid_row {
        row_type or_type;
        string_fragment or_name;
        opid_time_range or_value;
        string_fragment or_description;
        opid_description_defs or_description_defs;
        std::optional<std::chrono::microseconds> or_description_begin;
        opid_description_def_key or_description_def_key;
        lnav::map::small<size_t, std::string> or_description_value;
        size_t or_max_subid_width{0};
        logfile* or_logfile{nullptr};
        bool or_is_context{false};

        bool operator<(const opid_row& rhs) const
        {
            if (this->or_value.otr_range < rhs.or_value.otr_range) {
                return true;
            }
            if (this->or_value.otr_range.tr_begin
                    == rhs.or_value.otr_range.tr_begin
                && this->or_name < rhs.or_name)
            {
                return true;
            }

            return false;
        }
    };

    using timeline_opid_row_map
        = robin_hood::unordered_map<string_fragment,
                                    opid_row,
                                    frag_hasher,
                                    std::equal_to<string_fragment>>;
    using timeline_desc_map
        = robin_hood::unordered_set<string_fragment,
                                    frag_hasher,
                                    std::equal_to<string_fragment>>;

    static std::optional<row_type> row_type_from_string(const std::string& str);
    static const char* row_type_to_string(row_type rt);

    void set_row_type_visibility(row_type rt, bool visible);
    bool is_row_type_visible(row_type rt) const;

    std::set<row_type> ts_hidden_row_types;
    std::set<row_type> ts_preview_hidden_row_types;

    static constexpr size_t MAX_METRICS = 4;

    enum class metric_kind {
        shorthand,
        sql,
    };

    struct metric_def {
        std::string md_label;
        std::string md_query;
        metric_kind md_kind{metric_kind::shorthand};
    };

    struct metric_state {
        metric_def ms_def;
        std::vector<std::pair<std::chrono::microseconds, double>> ms_samples;
        double ms_min{std::numeric_limits<double>::quiet_NaN()};
        double ms_max{std::numeric_limits<double>::quiet_NaN()};
        // Unit hint pulled from the source column's logline_value_meta
        // when the samples come from a metric log file.  Empty for
        // SQL-backed metrics, which don't carry format metadata.
        // Samples are pre-divided by ms_unit_divisor at collection
        // time so the sparkline scale and the rendered status value
        // share the same unit.
        std::string ms_unit_suffix;
        double ms_unit_divisor{1.0};
        // Shorthand lookups: set true when at least one loaded file's
        // format declares the requested column.  Empty ms_samples with
        // ms_matched == false is a probable typo; empty ms_samples with
        // ms_matched == true is the "file still indexing" case.  SQL
        // metrics always set this true once prepared.
        bool ms_matched{false};
        // Populated when SQL collection fails at runtime (query that
        // validated at command time can fail later if the underlying
        // tables change — e.g. a search-table column disappears after
        // a format reload).  Rendered as an inline error banner in
        // place of the sparkline.
        std::string ms_error;
    };

    std::vector<metric_state> ts_metrics;

    Result<void, lnav::console::user_message> add_metric(const metric_def& md);
    bool remove_metric(const std::string& label);
    void clear_metrics();
    void collect_metric_samples();
    void collect_metric_samples_for(metric_state& ms);
    void collect_metric_samples_from_files(metric_state& ms);
    void collect_metric_samples_via_sql(metric_state& ms);
    void update_metric_status();

    timeline_preview_overlay ts_preview_overlay;
    attr_line_t ts_rendered_line;
    size_t ts_opid_width{0};
    size_t ts_total_width{0};
    timeline_opid_row_map ts_active_opids;
    timeline_desc_map ts_descriptions;
    std::vector<const opid_row*> ts_time_order;
    std::chrono::microseconds ts_lower_bound{};
    std::chrono::microseconds ts_upper_bound{};
    size_t ts_filtered_count{0};
    std::array<size_t, logfile_filter_state::MAX_FILTERS> ts_filter_hits{};
    exec_context* ts_exec_context{nullptr};
    bool ts_preview_focused{false};
    std::vector<row_info> ts_preview_rows;

    struct progress_t {
        size_t p_curr{0};
        size_t p_total{0};
    };

    std::function<lnav::progress_result_t(std::optional<progress_t>)>
        ts_index_progress;

    struct pending_bookmark {
        row_type pb_row_type;
        std::string pb_row_name;
        const bookmark_type_t* pb_mark_type;
    };

    std::vector<pending_bookmark> ts_pending_bookmarks;

    void apply_pending_bookmarks();
};

class timeline_header_overlay : public text_overlay_menu {
public:
    explicit timeline_header_overlay(
        const std::shared_ptr<timeline_source>& src);

    bool list_static_overlay(const listview_curses& lv,
                             media_t media,
                             int y,
                             int bottom,
                             attr_line_t& value_out) override;

    std::optional<attr_line_t> list_header_for_overlay(
        const listview_curses& lv, media_t media, vis_line_t line) override;

    void list_value_for_overlay(const listview_curses& lv,
                                vis_line_t line,
                                std::vector<attr_line_t>& value_out) override;

    void set_show_details_in_overlay(bool val) override
    {
        this->gho_show_details = val;
    }

    bool get_show_details_in_overlay() const override
    {
        return this->gho_show_details;
    }

private:
    bool gho_show_details{false};
    std::shared_ptr<timeline_source> gho_src;
    std::vector<attr_line_t> gho_static_lines;
};

#endif
