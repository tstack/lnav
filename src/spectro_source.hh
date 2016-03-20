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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file spectroview_curses.hh
 */

#ifndef __spectro_source_hh
#define __spectro_source_hh

#include <math.h>
#include <time.h>

#include <map>
#include <vector>

#include "textview_curses.hh"

struct spectrogram_bounds {
    spectrogram_bounds()
        : sb_begin_time(0),
          sb_end_time(0),
          sb_min_value_out(0.0),
          sb_max_value_out(0.0),
          sb_count(0) {

    };

    time_t sb_begin_time;
    time_t sb_end_time;
    double sb_min_value_out;
    double sb_max_value_out;
    int64_t sb_count;
};

struct spectrogram_thresholds {
    int st_green_threshold;
    int st_yellow_threshold;
};

struct spectrogram_request {
    spectrogram_request(spectrogram_bounds &sb)
        : sr_bounds(sb), sr_width(0), sr_column_size(0) {
    };

    spectrogram_bounds &sr_bounds;
    unsigned long sr_width;
    time_t sr_begin_time;
    time_t sr_end_time;
    double sr_column_size;
};

struct spectrogram_row {
    spectrogram_row() : sr_values(NULL), sr_width(0) {

    };

    ~spectrogram_row() {
        delete this->sr_values;
    }

    int *sr_values;
    unsigned long sr_width;
    double sr_column_size;

    void add_value(spectrogram_request &sr, double value) {
        long index = lrint((value - sr.sr_bounds.sb_min_value_out) / sr.sr_column_size);

        this->sr_values[index] += 1;
    };
};

class spectrogram_value_source {
public:
    virtual ~spectrogram_value_source() { };

    virtual void spectro_bounds(spectrogram_bounds &sb_out) = 0;

    virtual void spectro_row(spectrogram_request &sr,
                             spectrogram_row &row_out) = 0;
};

class spectrogram_source
    : public text_sub_source,
      public text_time_translator,
      public list_overlay_source {
public:

    spectrogram_source()
        : ss_granularity(60),
          ss_value_source(NULL) {

    };

    void invalidate() {
        this->ss_cached_bounds.sb_count = 0;
        this->ss_row_cache.clear();
    };

    size_t list_overlay_count(const listview_curses &lv) {
        return 1;
    };

    bool list_value_for_overlay(const listview_curses &lv,
                                vis_line_t y,
                                attr_line_t &value_out) {
        if (y != 0) {
            return false;
        }

        std::string &line = value_out.get_string();
        char buf[128];
        vis_line_t height;
        unsigned long width;

        lv.get_dimensions(height, width);

        this->cache_bounds();

        if (this->ss_cached_line_count == 0) {
            value_out.with_ansi_string(
                ANSI_ROLE("error: no log data"),
                view_colors::VCR_ERROR);
            return true;
        }

        spectrogram_bounds &sb = this->ss_cached_bounds;
        spectrogram_thresholds &st = this->ss_cached_thresholds;

        snprintf(buf, sizeof(buf), "Min: %'.10lg", sb.sb_min_value_out);
        line = buf;

        snprintf(buf, sizeof(buf),
                 ANSI_ROLE("  ") " 1-%'d "
                     ANSI_ROLE("  ") " %'d-%'d "
                     ANSI_ROLE("  ") " %'d+",
                 view_colors::VCR_LOW_THRESHOLD,
                 st.st_green_threshold - 1,
                 view_colors::VCR_MED_THRESHOLD,
                 st.st_green_threshold,
                 st.st_yellow_threshold - 1,
                 view_colors::VCR_HIGH_THRESHOLD,
                 st.st_yellow_threshold);
        line.append(width / 2 - strlen(buf) / 3 - line.length(), ' ');
        line.append(buf);
        scrub_ansi_string(line, value_out.get_attrs());

        snprintf(buf, sizeof(buf), "Max: %'.10lg", sb.sb_max_value_out);
        line.append(width - strlen(buf) - line.length() - 2, ' ');
        line.append(buf);

        value_out.with_attr(string_attr(
            line_range(0, -1),
            &view_curses::VC_STYLE,
            A_UNDERLINE));

        return true;
    };

    size_t text_line_count() {
        if (this->ss_value_source == NULL) {
            return 0;
        }

        this->cache_bounds();

        return this->ss_cached_line_count;
    };

    size_t text_line_width(textview_curses &tc) {
        unsigned long width;
        vis_line_t height;

        tc.get_dimensions(height, width);
        return width;
    };

    size_t text_size_for_line(textview_curses &tc, int row, bool raw) {
        return 0;
    };

    time_t time_for_row(int row) {
        time_t retval;

        this->cache_bounds();
        retval = rounddown(this->ss_cached_bounds.sb_begin_time, this->ss_granularity) +
                 row * this->ss_granularity;

        return retval;
    }

    int row_for_time(time_t time_bucket) {
        if (this->ss_value_source == NULL) {
            return 0;
        }

        time_t diff;
        int retval;

        this->cache_bounds();
        if (time_bucket < this->ss_cached_bounds.sb_begin_time) {
            return 0;
        }

        diff = time_bucket - this->ss_cached_bounds.sb_begin_time;
        retval = diff / this->ss_granularity;

        return retval;
    }

    void text_value_for_line(textview_curses &tc,
                             int row,
                             std::string &value_out,
                             bool no_scrub) {
        time_t row_time;
        char tm_buffer[128];
        struct tm tm;

        row_time = this->time_for_row(row);

        gmtime_r(&row_time, &tm);
        strftime(tm_buffer, sizeof(tm_buffer), " %a %b %d %H:%M:%S", &tm);

        value_out = tm_buffer;
    };

    void text_attrs_for_line(textview_curses &tc,
                             int row,
                             string_attrs_t &value_out) {
        if (this->ss_value_source == NULL) {
            return;
        }

        this->cache_bounds();

        view_colors &vc = view_colors::singleton();
        unsigned long width;
        vis_line_t height;

        tc.get_dimensions(height, width);
        width -= 2;

        spectrogram_bounds &sb = this->ss_cached_bounds;
        spectrogram_thresholds &st = this->ss_cached_thresholds;
        spectrogram_request sr(sb);
        time_t row_time;

        sr.sr_width = width;
        row_time = rounddown(sb.sb_begin_time, this->ss_granularity) +
            row * this->ss_granularity;
        sr.sr_begin_time = row_time;
        sr.sr_end_time = row_time + this->ss_granularity;

        sr.sr_column_size = (sb.sb_max_value_out - sb.sb_min_value_out) /
            (double) (width - 1);

        spectrogram_row &s_row = this->ss_row_cache[row_time];

        if (s_row.sr_values == NULL ||
            s_row.sr_width != width ||
            s_row.sr_column_size != sr.sr_column_size) {
            s_row.sr_width = width;
            s_row.sr_column_size = sr.sr_column_size;
            delete s_row.sr_values;
            s_row.sr_values = new int[width + 1];
            memset(s_row.sr_values, 0, sizeof(int) * (width + 1));
            this->ss_value_source->spectro_row(sr, s_row);
        }

        for (int lpc = 0; lpc <= width; lpc++) {
            int col_value = s_row.sr_values[lpc];

            if (col_value == 0) {
                continue;
            }

            int color;

            if (col_value < st.st_green_threshold) {
                color = COLOR_GREEN;
            }
            else if (col_value < st.st_yellow_threshold) {
                color = COLOR_YELLOW;
            }
            else {
                color = COLOR_RED;
            }
            value_out.push_back(string_attr(
                line_range(lpc, lpc + 1),
                &view_curses::VC_STYLE,
                vc.ansi_color_pair(COLOR_BLACK, color)
            ));
        }
    };

    void cache_bounds() {
        if (this->ss_value_source == NULL) {
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

        time_t diff = std::max((time_t) 1, sb.sb_end_time - sb.sb_begin_time + 1);
        this->ss_cached_line_count =
            (diff + this->ss_granularity - 1) / this->ss_granularity;

        int64_t samples_per_row = sb.sb_count / this->ss_cached_line_count;
        spectrogram_thresholds &st = this->ss_cached_thresholds;

        st.st_yellow_threshold = samples_per_row / 2;
        st.st_green_threshold = st.st_yellow_threshold / 2;

        if (st.st_green_threshold <= 1) {
            st.st_green_threshold = 2;
        }
        if (st.st_yellow_threshold <= st.st_green_threshold) {
            st.st_yellow_threshold = st.st_green_threshold + 1;
        }
    };

    int ss_granularity;
    spectrogram_value_source *ss_value_source;
    spectrogram_bounds ss_cached_bounds;
    spectrogram_thresholds ss_cached_thresholds;
    size_t ss_cached_line_count;
    std::map<time_t, spectrogram_row> ss_row_cache;
};

#endif
