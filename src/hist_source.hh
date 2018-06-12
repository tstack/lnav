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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file hist_source.hh
 */

#ifndef __hist_source_hh
#define __hist_source_hh

#include <math.h>

#include <map>
#include <limits>
#include <string>
#include <vector>

#include "mapbox/variant.hpp"

#include "lnav_log.hh"
#include "strong_int.hh"
#include "textview_curses.hh"

typedef float bucket_count_t;

/** Type for indexes into a group of buckets. */
STRONG_INT_TYPE(int, bucket_group);

/** Type used to differentiate values added to the same row in the histogram */
STRONG_INT_TYPE(int, bucket_type);

struct stacked_bar_chart_base {
    struct show_none {};
    struct show_all {};
    struct show_one {
        int so_index;

        show_one(int so_index) : so_index(so_index) {

        }
    };

    typedef mapbox::util::variant<show_none, show_all, show_one> show_state;

    enum class direction {
        forward,
        backward,
    };
};

template<typename T>
class stacked_bar_chart : public stacked_bar_chart_base {

public:
    stacked_bar_chart()
            : sbc_do_stacking(true), sbc_left(0), sbc_right(0), sbc_show_state(show_all()) {

    };

    virtual ~stacked_bar_chart() { };

    stacked_bar_chart &with_stacking_enabled(bool enabled) {
        this->sbc_do_stacking = enabled;
        return *this;
    };

    stacked_bar_chart &with_attrs_for_ident(const T &ident, int attrs) {
        struct chart_ident &ci = this->find_ident(ident);
        ci.ci_attrs = attrs;
        return *this;
    };

    stacked_bar_chart &with_margins(unsigned long left, unsigned long right) {
        this->sbc_left = left;
        this->sbc_right = right;
        return *this;
    };

    show_state show_next_ident(direction dir) {
        bool single_ident = this->sbc_idents.size() == 1;

        if (this->sbc_idents.empty()) {
            return this->sbc_show_state;
        }

        this->sbc_show_state = this->sbc_show_state.match(
            [&] (show_none) -> show_state {
                switch (dir) {
                    case direction::forward:
                        if (single_ident) {
                            return show_all();
                        }
                        return show_one(0);
                    case direction::backward:
                        return show_all();
                }
            },
            [&] (show_one &one) -> show_state {
                switch (dir) {
                    case direction::forward:
                        if (one.so_index + 1 == this->sbc_idents.size()) {
                            return show_all();
                        }
                        return show_one(one.so_index + 1);
                    case direction::backward:
                        if (one.so_index == 0) {
                            return show_none();
                        }
                        return show_one(one.so_index - 1);
                }
            },
            [&] (show_all) -> show_state {
                switch (dir) {
                    case direction::forward:
                        return show_none();
                    case direction::backward:
                        if (single_ident) {
                            return show_none();
                        }
                        return show_one(this->sbc_idents.size() - 1);
                }
            }
        );

        return this->sbc_show_state;
    };

    void get_ident_to_show(T &ident_out) const {
        this->sbc_show_state.match(
            [] (const show_none) {},
            [] (const show_all) {},
            [&] (const show_one &one) {
                ident_out = this->sbc_idents[one.so_index].ci_ident;
            }
        );
    };

    void chart_attrs_for_value(const listview_curses &lc,
                               int &left,
                               const T &ident,
                               double value,
                               string_attrs_t &value_out) const
    {
        auto ident_iter = this->sbc_ident_lookup.find(ident);

        require(ident_iter != this->sbc_ident_lookup.end());

        int ident_index = ident_iter->second;
        unsigned long width, avail_width;
        bucket_stats_t overall_stats;
        struct line_range lr;
        vis_line_t height;

        int ident_to_show = this->sbc_show_state.match(
            [] (const show_none) {
                return -1;
            },
            [ident_index] (const show_all) {
                return ident_index;
            },
            [] (const show_one &one) {
                return one.so_index;
            }
        );

        if (ident_to_show != ident_index) {
            return;
        }

        lc.get_dimensions(height, width);

        for (size_t lpc = 0; lpc < this->sbc_idents.size(); lpc++) {
            if (this->sbc_show_state.template is<show_all>() || lpc == (size_t) ident_to_show) {
                overall_stats.merge(this->sbc_idents[lpc].ci_stats, this->sbc_do_stacking);
            }
        }

        if (this->sbc_show_state.template is<show_all>()) {
            avail_width = width - this->sbc_idents.size();
        }
        else {
            avail_width = width - 1;
        }
        avail_width -= this->sbc_left + this->sbc_right;

        lr.lr_start = left;

        const struct chart_ident &ci = this->sbc_idents[ident_index];
        int amount;

        if (value == 0.0) {
            amount = 0;
        }
        else if ((overall_stats.bs_max_value - 0.01) <= value &&
                 value <= (overall_stats.bs_max_value + 0.01)) {
            amount = avail_width;
        }
        else {
            double percent = (value - overall_stats.bs_min_value) /
                             overall_stats.width();
            amount = (int) rint(percent * avail_width);
            amount = std::max(1, amount);
        }
        lr.lr_end = left = lr.lr_start + amount;

        if (ci.ci_attrs != 0) {
            value_out.push_back(string_attr(lr,
                                            &view_curses::VC_STYLE,
                                            ci.ci_attrs | A_REVERSE));
        }
    };

    void clear() {
        this->sbc_idents.clear();
        this->sbc_ident_lookup.clear();
        this->sbc_show_state = show_all();
    };

    void add_value(const T &ident, double amount = 1.0) {
        struct chart_ident &ci = this->find_ident(ident);
        ci.ci_stats.update(amount);
    };

    struct bucket_stats_t {
        bucket_stats_t() :
            bs_min_value(std::numeric_limits<double>::max()),
            bs_max_value(0)
        {
        };

        void merge(const bucket_stats_t &rhs, bool do_stacking) {
            this->bs_min_value = std::min(this->bs_min_value, rhs.bs_min_value);
            if (do_stacking) {
                this->bs_max_value += rhs.bs_max_value;
            }
            else {
                this->bs_max_value = std::max(this->bs_max_value, rhs.bs_max_value);
            }
        };

        double width() const {
            return fabs(this->bs_max_value - this->bs_min_value);
        };

        void update(double value) {
            this->bs_max_value = std::max(this->bs_max_value, value);
            this->bs_min_value = std::min(this->bs_min_value, value);
        };

        double bs_min_value;
        double bs_max_value;
    };

    const bucket_stats_t &get_stats_for(const T &ident) {
        const chart_ident &ci = this->find_ident(ident);

        return ci.ci_stats;
    };

protected:

    struct chart_ident {
        chart_ident(const T &ident) : ci_ident(ident) { };

        T ci_ident;
        int ci_attrs{0};
        bucket_stats_t ci_stats;
    };

    struct chart_ident &find_ident(const T &ident) {
        typename std::map<T, unsigned int>::iterator iter;

        iter = this->sbc_ident_lookup.find(ident);
        if (iter == this->sbc_ident_lookup.end()) {
            this->sbc_ident_lookup[ident] = this->sbc_idents.size();
            this->sbc_idents.push_back(ident);
            return this->sbc_idents.back();
        }
        return this->sbc_idents[iter->second];
    };

    bool sbc_do_stacking;
    unsigned long sbc_left, sbc_right;
    std::vector<struct chart_ident> sbc_idents;
    std::map<T, unsigned int> sbc_ident_lookup;
    show_state sbc_show_state;
};

class hist_source2
    : public text_sub_source,
      public text_time_translator {
public:

    typedef enum {
        HT_NORMAL,
        HT_WARNING,
        HT_ERROR,
        HT_MARK,

        HT__MAX
    } hist_type_t;

    hist_source2() : hs_time_slice(10 * 60) {
        this->clear();
    };

    void init() {
        view_colors &vc = view_colors::singleton();

        this->hs_chart
                .with_attrs_for_ident(HT_NORMAL,
                                      vc.attrs_for_role(view_colors::VCR_TEXT))
                .with_attrs_for_ident(HT_WARNING,
                                      vc.attrs_for_role(view_colors::VCR_WARNING))
                .with_attrs_for_ident(HT_ERROR,
                                      vc.attrs_for_role(view_colors::VCR_ERROR))
                .with_attrs_for_ident(HT_MARK,
                                      vc.attrs_for_role(view_colors::VCR_KEYWORD));
    };

    void set_time_slice(int64_t slice) {
        this->hs_time_slice = slice;
    };

    int64_t get_time_slice() const {
        return this->hs_time_slice;
    };

    size_t text_line_count() {
        return this->hs_line_count;
    };

    size_t text_line_width(textview_curses &curses) {
        return strlen(LINE_FORMAT) + 8 * 4;
    };

    void clear() {
        this->hs_line_count = 0;
        this->hs_last_bucket = -1;
        this->hs_last_row = -1;
        this->hs_blocks.clear();
        this->hs_chart.clear();
        this->init();
    };

    void add_value(time_t row, hist_type_t htype, double value = 1.0) {
        require(row >= this->hs_last_row);

        row = rounddown(row, this->hs_time_slice);
        if (row != this->hs_last_row) {
            this->end_of_row();

            this->hs_last_bucket += 1;
            this->hs_last_row = row;
        }

        bucket_t &bucket = this->find_bucket(this->hs_last_bucket);
        bucket.b_time = row;
        bucket.b_values[htype].hv_value += value;
    };

    void end_of_row() {
        if (this->hs_last_bucket >= 0) {
            bucket_t &last_bucket = this->find_bucket(this->hs_last_bucket);

            for (int lpc = 0; lpc < HT__MAX; lpc++) {
                this->hs_chart.add_value(
                        (const hist_type_t) lpc,
                        last_bucket.b_values[lpc].hv_value);
            }
        }
    };

    void text_value_for_line(textview_curses &tc,
                             int row,
                             std::string &value_out,
                             line_flags_t flags) {
        bucket_t &bucket = this->find_bucket(row);
        struct tm bucket_tm;
        char tm_buffer[128];
        char line[256];

        if (gmtime_r(&bucket.b_time, &bucket_tm) != NULL) {
            strftime(tm_buffer, sizeof(tm_buffer),
                     " %a %b %d %H:%M:%S  ",
                     &bucket_tm);
        }
        else {
            log_error("no time?");
            tm_buffer[0] = '\0';
        }
        snprintf(line, sizeof(line),
                 LINE_FORMAT,
                 (int) rint(bucket.b_values[HT_NORMAL].hv_value),
                 (int) rint(bucket.b_values[HT_ERROR].hv_value),
                 (int) rint(bucket.b_values[HT_WARNING].hv_value),
                 (int) rint(bucket.b_values[HT_MARK].hv_value));

        value_out.clear();
        value_out.append(tm_buffer);
        value_out.append(line);
    };

    void text_attrs_for_line(textview_curses &tc,
                             int row,
                             string_attrs_t &value_out) {
        bucket_t &bucket = this->find_bucket(row);
        int left = 0;

        for (int lpc = 0; lpc < HT__MAX; lpc++) {
            this->hs_chart.chart_attrs_for_value(
                    tc, left, (const hist_type_t) lpc,
                    bucket.b_values[lpc].hv_value,
                    value_out);
        }
    };

    size_t text_size_for_line(textview_curses &tc, int row, line_flags_t flags) {
        return 0;
    };

    time_t time_for_row(int row) {
        require(row >= 0);
        require(row < this->hs_line_count);

        bucket_t &bucket = this->find_bucket(row);

        return bucket.b_time;
    };

    int row_for_time(time_t time_bucket) {
        std::map<int64_t, struct bucket_block>::iterator iter;
        int retval = 0;

        time_bucket = rounddown(time_bucket, this->hs_time_slice);

        for (iter = this->hs_blocks.begin();
             iter != this->hs_blocks.end();
             ++iter) {
            struct bucket_block &bb = iter->second;

            if (time_bucket < bb.bb_buckets[0].b_time) {
                break;
            }
            if (time_bucket > bb.bb_buckets[bb.bb_used].b_time) {
                retval += bb.bb_used + 1;
                continue;
            }

            for (unsigned int lpc = 0; lpc <= bb.bb_used; lpc++, retval++) {
                if (time_bucket <= bb.bb_buckets[lpc].b_time) {
                    return retval;
                }
            }
        }
        return retval;
    };

private:
    static const char *LINE_FORMAT;

    struct hist_value {
        double hv_value;
    };

    struct bucket_t {
        time_t b_time;
        hist_value b_values[HT__MAX];
    };

    static const unsigned int BLOCK_SIZE = 100;

    struct bucket_block {
        bucket_block() : bb_used(0) {
            memset(this->bb_buckets, 0, sizeof(this->bb_buckets));
        };

        unsigned int bb_used;
        bucket_t bb_buckets[BLOCK_SIZE];
    };

    bucket_t &find_bucket(int64_t index) {
        struct bucket_block &bb = this->hs_blocks[index / this->BLOCK_SIZE];
        unsigned int intra_block_index = index % BLOCK_SIZE;
        bb.bb_used = std::max(intra_block_index, bb.bb_used);
        this->hs_line_count = std::max(this->hs_line_count, index + 1);
        return bb.bb_buckets[intra_block_index];
    };

    int64_t hs_time_slice;
    int64_t hs_line_count;
    int64_t hs_last_bucket;
    time_t hs_last_row;
    std::map<int64_t, struct bucket_block> hs_blocks;
    stacked_bar_chart<hist_type_t> hs_chart;
};

#endif
