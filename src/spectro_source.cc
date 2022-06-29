/**
 * Copyright (c) 2020, Timothy Stack
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
 * @file spectro_source.cc
 */

#include "spectro_source.hh"

#include "base/ansi_scrubber.hh"
#include "base/math_util.hh"
#include "config.h"

bool
spectrogram_source::list_input_handle_key(listview_curses& lv, int ch)
{
    switch (ch) {
        case 'm': {
            if (this->ss_cursor_top < 0
                || (size_t) this->ss_cursor_top >= this->text_line_count()
                || this->ss_cursor_column == -1
                || this->ss_value_source == nullptr)
            {
                alerter::singleton().chime();
                return true;
            }

            unsigned long width;
            vis_line_t height;

            lv.get_dimensions(height, width);

            spectrogram_bounds& sb = this->ss_cached_bounds;
            auto begin_time_opt = this->time_for_row(this->ss_cursor_top);
            if (!begin_time_opt) {
                return true;
            }
            auto begin_time = begin_time_opt.value();
            struct timeval end_time = begin_time;

            end_time.tv_sec += this->ss_granularity;
            double range_min, range_max, column_size;

            column_size = (sb.sb_max_value_out - sb.sb_min_value_out)
                / (double) (width - 1);
            range_min
                = sb.sb_min_value_out + this->ss_cursor_column * column_size;
            range_max = range_min + column_size + column_size * 0.01;
            this->ss_value_source->spectro_mark((textview_curses&) lv,
                                                begin_time.tv_sec,
                                                end_time.tv_sec,
                                                range_min,
                                                range_max);
            this->invalidate();
            lv.reload_data();
            return true;
        }
        case KEY_LEFT:
        case KEY_RIGHT: {
            unsigned long width;
            vis_line_t height;
            string_attrs_t sa;

            this->ss_cursor_top = lv.get_top();
            lv.get_dimensions(height, width);

            this->text_attrs_for_line(
                (textview_curses&) lv, this->ss_cursor_top, sa);

            if (sa.empty()) {
                this->ss_cursor_column = -1;
                return true;
            }

            string_attrs_t::iterator current;

            struct line_range lr(this->ss_cursor_column,
                                 this->ss_cursor_column + 1);

            current = find_string_attr(sa, lr);

            if (current != sa.end()) {
                if (ch == KEY_LEFT) {
                    if (current == sa.begin()) {
                        current = sa.end();
                    } else {
                        --current;
                    }
                } else {
                    ++current;
                }
            }

            if (current == sa.end()) {
                if (ch == KEY_LEFT) {
                    current = sa.end();
                    --current;
                } else {
                    current = sa.begin();
                }
            }
            this->ss_cursor_column = current->sa_range.lr_start;

            lv.reload_data();

            return true;
        }
        default:
            return false;
    }
}

bool
spectrogram_source::list_value_for_overlay(const listview_curses& lv,
                                           int y,
                                           int bottom,
                                           vis_line_t row,
                                           attr_line_t& value_out)
{
    if (y != 0) {
        return false;
    }

    std::string& line = value_out.get_string();
    char buf[128];
    vis_line_t height;
    unsigned long width;

    lv.get_dimensions(height, width);

    this->cache_bounds();

    if (this->ss_cached_line_count == 0) {
        value_out.with_ansi_string(ANSI_ROLE("error: no log data"),
                                   role_t::VCR_ERROR);
        return true;
    }

    spectrogram_bounds& sb = this->ss_cached_bounds;
    spectrogram_thresholds& st = this->ss_cached_thresholds;

    snprintf(buf, sizeof(buf), "Min: %'.10lg", sb.sb_min_value_out);
    line = buf;

    snprintf(buf,
             sizeof(buf),
             ANSI_ROLE("  ") " 1-%'d " ANSI_ROLE("  ") " %'d-%'d " ANSI_ROLE(
                 "  ") " %'d+",
             role_t::VCR_LOW_THRESHOLD,
             st.st_green_threshold - 1,
             role_t::VCR_MED_THRESHOLD,
             st.st_green_threshold,
             st.st_yellow_threshold - 1,
             role_t::VCR_HIGH_THRESHOLD,
             st.st_yellow_threshold);
    line.append(width / 2 - strlen(buf) / 3 - line.length(), ' ');
    line.append(buf);
    scrub_ansi_string(line, value_out.get_attrs());

    snprintf(buf, sizeof(buf), "Max: %'.10lg", sb.sb_max_value_out);
    line.append(width - strlen(buf) - line.length() - 2, ' ');
    line.append(buf);

    value_out.with_attr(string_attr(line_range(0, -1),
                                    VC_STYLE.value(A_UNDERLINE)));

    return true;
}

size_t
spectrogram_source::text_line_count()
{
    if (this->ss_value_source == nullptr) {
        return 0;
    }

    this->cache_bounds();

    return this->ss_cached_line_count;
}

size_t
spectrogram_source::text_line_width(textview_curses& tc)
{
    if (tc.get_window() == nullptr) {
        return 80;
    }

    unsigned long width;
    vis_line_t height;

    tc.get_dimensions(height, width);
    return width;
}

nonstd::optional<struct timeval>
spectrogram_source::time_for_row(vis_line_t row)
{
    struct timeval retval {
        0, 0
    };

    this->cache_bounds();
    retval.tv_sec
        = rounddown(this->ss_cached_bounds.sb_begin_time, this->ss_granularity)
        + row * this->ss_granularity;

    return retval;
}

nonstd::optional<vis_line_t>
spectrogram_source::row_for_time(struct timeval time_bucket)
{
    if (this->ss_value_source == nullptr) {
        return nonstd::nullopt;
    }

    time_t diff;
    int retval;

    this->cache_bounds();
    if (time_bucket.tv_sec < this->ss_cached_bounds.sb_begin_time) {
        return 0_vl;
    }

    diff = time_bucket.tv_sec - this->ss_cached_bounds.sb_begin_time;
    retval = diff / this->ss_granularity;

    return vis_line_t(retval);
}

void
spectrogram_source::text_value_for_line(textview_curses& tc,
                                        int row,
                                        std::string& value_out,
                                        text_sub_source::line_flags_t flags)
{
    spectrogram_row& s_row = this->load_row(tc, row);

    char tm_buffer[128];
    struct tm tm;

    auto row_time_opt = this->time_for_row(vis_line_t(row));
    if (!row_time_opt) {
        value_out.clear();
        return;
    }
    auto row_time = row_time_opt.value();

    gmtime_r(&row_time.tv_sec, &tm);
    strftime(tm_buffer, sizeof(tm_buffer), " %a %b %d %H:%M:%S", &tm);

    value_out = tm_buffer;
    value_out.resize(s_row.sr_width, ' ');

    for (size_t lpc = 0; lpc <= s_row.sr_width; lpc++) {
        if (s_row.sr_values[lpc].rb_marks) {
            value_out[lpc] = 'x';
        }
    }

    if (this->ss_cursor_top == row && this->ss_cursor_column != -1) {
        if (value_out[this->ss_cursor_column] == 'x') {
            value_out[this->ss_cursor_column] = '*';
        } else {
            value_out[this->ss_cursor_column] = '+';
        }
    }
}

void
spectrogram_source::text_attrs_for_line(textview_curses& tc,
                                        int row,
                                        string_attrs_t& value_out)
{
    if (this->ss_value_source == nullptr) {
        return;
    }

    const auto& st = this->ss_cached_thresholds;
    const auto& s_row = this->load_row(tc, row);

    for (int lpc = 0; lpc <= (int) s_row.sr_width; lpc++) {
        int col_value = s_row.sr_values[lpc].rb_counter;

        if (col_value == 0) {
            continue;
        }

        role_t role;

        if (col_value < st.st_green_threshold) {
            role = role_t::VCR_LOW_THRESHOLD;
        } else if (col_value < st.st_yellow_threshold) {
            role = role_t::VCR_MED_THRESHOLD;
        } else {
            role = role_t::VCR_HIGH_THRESHOLD;
        }
        value_out.emplace_back(line_range(lpc, lpc + 1), VC_ROLE.value(role));
    }
}

void
spectrogram_source::cache_bounds()
{
    if (this->ss_value_source == nullptr) {
        this->ss_cached_bounds.sb_count = 0;
        this->ss_cached_bounds.sb_begin_time = 0;
        return;
    }

    spectrogram_bounds sb;

    this->ss_value_source->spectro_bounds(sb);

    if (sb.sb_count == this->ss_cached_bounds.sb_count) {
        return;
    }

    this->ss_cached_bounds = sb;

    if (sb.sb_count == 0) {
        this->ss_cached_line_count = 0;
        return;
    }

    time_t grain_begin_time = rounddown(sb.sb_begin_time, this->ss_granularity);
    time_t grain_end_time = roundup_size(sb.sb_end_time, this->ss_granularity);

    time_t diff = std::max((time_t) 1, grain_end_time - grain_begin_time);
    this->ss_cached_line_count
        = (diff + this->ss_granularity - 1) / this->ss_granularity;

    int64_t samples_per_row = sb.sb_count / this->ss_cached_line_count;
    spectrogram_thresholds& st = this->ss_cached_thresholds;

    st.st_yellow_threshold = samples_per_row / 2;
    st.st_green_threshold = st.st_yellow_threshold / 2;

    if (st.st_green_threshold <= 1) {
        st.st_green_threshold = 2;
    }
    if (st.st_yellow_threshold <= st.st_green_threshold) {
        st.st_yellow_threshold = st.st_green_threshold + 1;
    }
}

spectrogram_row&
spectrogram_source::load_row(textview_curses& tc, int row)
{
    this->cache_bounds();

    unsigned long width;
    vis_line_t height;

    tc.get_dimensions(height, width);
    width -= 2;

    spectrogram_bounds& sb = this->ss_cached_bounds;
    spectrogram_request sr(sb);
    time_t row_time;

    sr.sr_width = width;
    row_time = rounddown(sb.sb_begin_time, this->ss_granularity)
        + row * this->ss_granularity;
    sr.sr_begin_time = row_time;
    sr.sr_end_time = row_time + this->ss_granularity;

    sr.sr_column_size
        = (sb.sb_max_value_out - sb.sb_min_value_out) / (double) (width - 1);

    spectrogram_row& s_row = this->ss_row_cache[row_time];

    if (s_row.sr_values == nullptr || s_row.sr_width != width
        || s_row.sr_column_size != sr.sr_column_size)
    {
        s_row.sr_width = width;
        s_row.sr_column_size = sr.sr_column_size;
        delete[] s_row.sr_values;
        s_row.sr_values = new spectrogram_row::row_bucket[width + 1];
        this->ss_value_source->spectro_row(sr, s_row);
    }

    return s_row;
}
