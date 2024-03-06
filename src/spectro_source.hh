/**
 * Copyright (c) 2016, Timothy Stack
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
 * @file spectroview_curses.hh
 */

#ifndef spectro_source_hh
#define spectro_source_hh

#include <unordered_map>
#include <vector>

#include <math.h>
#include <time.h>

#include "statusview_curses.hh"
#include "textview_curses.hh"

struct exec_context;

struct spectrogram_bounds {
    time_t sb_begin_time{0};
    time_t sb_end_time{0};
    double sb_min_value_out{0.0};
    double sb_max_value_out{0.0};
    int64_t sb_count{0};
};

struct spectrogram_thresholds {
    int st_green_threshold{0};
    int st_yellow_threshold{0};
};

struct spectrogram_request {
    explicit spectrogram_request(spectrogram_bounds& sb) : sr_bounds(sb) {}

    spectrogram_bounds& sr_bounds;
    unsigned long sr_width{0};
    time_t sr_begin_time{0};
    time_t sr_end_time{0};
    double sr_column_size{0};
};

struct spectrogram_row {
    spectrogram_row() = default;
    spectrogram_row(const spectrogram_row&) = delete;

    struct row_bucket {
        int rb_counter{0};
        int rb_marks{0};
    };

    std::vector<row_bucket> sr_values;
    unsigned long sr_width{0};
    double sr_column_size{0.0};
    std::function<std::unique_ptr<text_sub_source>(
        const spectrogram_request&, double range_min, double range_max)>
        sr_details_source_provider;

    void add_value(spectrogram_request& sr, double value, bool marked)
    {
        long index = std::floor((value - sr.sr_bounds.sb_min_value_out)
                                / sr.sr_column_size);

        this->sr_values[index].rb_counter += 1;
        if (marked) {
            this->sr_values[index].rb_marks += 1;
        }
    }

    nonstd::optional<size_t> nearest_column(size_t current) const;
};

class spectrogram_value_source {
public:
    virtual ~spectrogram_value_source() = default;

    virtual void spectro_bounds(spectrogram_bounds& sb_out) = 0;

    virtual void spectro_row(spectrogram_request& sr, spectrogram_row& row_out)
        = 0;

    virtual void spectro_mark(textview_curses& tc,
                              time_t begin_time,
                              time_t end_time,
                              double range_min,
                              double range_max)
        = 0;
};

class spectrogram_source
    : public text_sub_source
    , public text_time_translator
    , public list_overlay_source
    , public list_input_delegate {
public:
    ~spectrogram_source() override = default;

    void invalidate()
    {
        this->ss_cached_bounds.sb_count = 0;
        this->ss_row_cache.clear();
        this->ss_cursor_column = nonstd::nullopt;
    }

    bool list_input_handle_key(listview_curses& lv, int ch) override;

    bool list_static_overlay(const listview_curses& lv,
                             int y,
                             int bottom,
                             attr_line_t& value_out) override;

    void list_value_for_overlay(const listview_curses& lv,
                                vis_line_t row,
                                std::vector<attr_line_t>& value_out) override;

    size_t text_line_count() override;

    size_t text_line_width(textview_curses& tc) override;

    size_t text_size_for_line(textview_curses& tc,
                              int row,
                              line_flags_t flags) override
    {
        return 0;
    }

    bool text_is_row_selectable(textview_curses& tc, vis_line_t row) override;

    void text_selection_changed(textview_curses& tc) override;

    nonstd::optional<row_info> time_for_row(vis_line_t row) override;

    nonstd::optional<vis_line_t> row_for_time(
        struct timeval time_bucket) override;

    void text_value_for_line(textview_curses& tc,
                             int row,
                             std::string& value_out,
                             line_flags_t flags) override;

    void text_attrs_for_line(textview_curses& tc,
                             int row,
                             string_attrs_t& value_out) override;

    void cache_bounds();

    nonstd::optional<row_info> time_for_row_int(vis_line_t row);

    const spectrogram_row& load_row(const listview_curses& lv, int row);

    void reset_details_source();

    textview_curses* ss_details_view;
    text_sub_source* ss_no_details_source;
    exec_context* ss_exec_context;
    std::unique_ptr<text_sub_source> ss_details_source;
    int ss_granularity{60};
    spectrogram_value_source* ss_value_source{nullptr};
    spectrogram_bounds ss_cached_bounds;
    spectrogram_thresholds ss_cached_thresholds;
    size_t ss_cached_line_count{0};
    std::unordered_map<time_t, spectrogram_row> ss_row_cache;
    nonstd::optional<size_t> ss_cursor_column;
};

class spectro_status_source : public status_data_source {
public:
    enum field_t {
        F_TITLE,
        F_HELP,

        F_MAX
    };

    spectro_status_source();

    size_t statusview_fields() override;

    status_field& statusview_value_for_field(int field) override;

private:
    status_field sss_fields[F_MAX];
};

#endif
