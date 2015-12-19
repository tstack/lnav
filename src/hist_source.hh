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

#include "lnav_log.hh"
#include "strong_int.hh"
#include "textview_curses.hh"

typedef float bucket_count_t;

/** Type for indexes into a group of buckets. */
STRONG_INT_TYPE(int, bucket_group);

/** Type used to differentiate values added to the same row in the histogram */
STRONG_INT_TYPE(int, bucket_type);

/**
 * A text source that displays data as a histogram using horizontal bars.  Data
 * is added to the histogram using the add_value() method.  Once all of the
 * values have been added, the analyze() method needs to be called to analyze
 * the data so that it can be displayed.
 *
 * For example, if the values (7, 3, 4, 2) were added, they would be displayed
 * like so:
 *
 *   ******
 *   ***
 *   ****
 *   **
 */
class hist_source
    : public text_sub_source {
public:
    typedef std::map<bucket_type_t, bucket_count_t> bucket_t;

    /**
     * Source for labels on each bucket and group.
     */
    class label_source {
public:
        virtual ~label_source() { };

        virtual size_t hist_label_width() {
            return INT_MAX;
        };

        virtual void hist_label_for_group(int group,
                                          std::string &label_out) { };

        virtual void hist_label_for_bucket(int bucket_start_value,
                                           const bucket_t &bucket,
                                           std::string &label_out) { };

        virtual void hist_attrs_for_bucket(int bucket_start_value,
                                           const bucket_t &bucket,
                                           string_attrs_t &sa) { };
    };

    hist_source();
    virtual ~hist_source() { };

    void set_bucket_size(unsigned int bs)
    {
        require(bs > 0);

        this->hs_bucket_size = bs;
    };
    unsigned int get_bucket_size(void) const { return this->hs_bucket_size; };

    void set_group_size(unsigned int gs)
    {
        require(gs > 0);
        this->hs_group_size = gs;
    };
    unsigned int get_group_size(void) const { return this->hs_group_size; };

    void set_label_source(label_source *hls)
    {
        this->hs_label_source = hls;
    }

    label_source *get_label_source(void)
    {
        return this->hs_label_source;
    };

    int buckets_per_group(void) const
    {
        return this->hs_group_size / this->hs_bucket_size;
    };

    void clear(void) {
        this->hs_groups.clear();
        this->hs_bucket_stats.clear();
    };

    size_t text_line_count()
    {
        return (this->buckets_per_group() + 1) * this->hs_groups.size();
    };

    size_t text_line_width() {
        return this->hs_label_source == NULL ? 0 :
               this->hs_label_source->hist_label_width();
    };

    void set_role_for_type(bucket_type_t bt, view_colors::role_t role)
    {
        this->hs_type2role[bt] = role;
    };
    const view_colors::role_t &get_role_for_type(bucket_type_t bt)
    {
        return this->hs_type2role[bt];
    };

    void text_value_for_line(textview_curses &tc,
                             int row,
                             std::string &value_out,
                             bool no_scrub);
    void text_attrs_for_line(textview_curses &tc,
                             int row,
                             string_attrs_t &value_out);

    size_t text_size_for_line(textview_curses &tc, int row, bool raw) {
        return 0;
    };

    int value_for_row(vis_line_t row)
    {
        int grow   = row / (this->buckets_per_group() + 1);
        int brow   = row % (this->buckets_per_group() + 1);
        int retval = 0;

        if (!this->hs_groups.empty()) {
            std::map<bucket_group_t, bucket_array_t>::const_iterator iter;

            iter = this->hs_groups.begin();
            std::advance(iter, grow);

            bucket_group_t bg = iter->first;

            if (brow > 0) {
                brow -= 1;
            }
            retval =
                (bg * this->hs_group_size) + (brow * this->hs_bucket_size);
        }

        return retval;
    };

    vis_line_t row_for_value(int value)
    {
        vis_line_t retval;

        if (!this->hs_groups.empty()) {
            bucket_group_t bg(value / this->hs_group_size);

            std::map<bucket_group_t, bucket_array_t>::iterator lb;

            lb = this->hs_groups.lower_bound(bg);
            retval = vis_line_t(distance(this->hs_groups.begin(), lb) *
                                (this->buckets_per_group() + 1));
            retval += vis_line_t(1 +
                                 (value % this->hs_group_size) /
                                 this->hs_bucket_size);
        }

        return retval;
    };

    /**
     * Add a value to the histogram.
     *
     * @param value The row in the histogram.
     * @param bt The type of data.
     * @param amount The amount to add to this row in the histogram.
     */
    void add_value(unsigned int value,
                   bucket_type_t bt,
                   bucket_count_t amount = 1.0);

    void add_empty_value(unsigned int value)
    {
        bucket_group_t bg;

        bg = bucket_group_t(value / this->hs_group_size);

        bucket_array_t &ba = this->hs_groups[bg];

        if (ba.empty()) {
            ba.resize(this->buckets_per_group());
        }
    };

    std::vector<bucket_type_t> &get_displayed_buckets() {
        return this->hs_displayed_buckets;
    };

    bool is_bucket_graphed(bucket_type_t type) const {
        return (this->hs_displayed_buckets.empty() ||
                std::find(this->hs_displayed_buckets.begin(),
                          this->hs_displayed_buckets.end(),
                          type) != this->hs_displayed_buckets.end());
    };

protected:
    typedef std::vector<bucket_t> bucket_array_t;

    struct bucket_stats_t {
        bucket_stats_t() :
            bs_min_count(std::numeric_limits<bucket_count_t>::max()),
            bs_max_count(0)
        {
        };

        void merge(const bucket_stats_t &rhs) {
            this->bs_min_count = std::min(this->bs_min_count, rhs.bs_min_count);
            this->bs_max_count += rhs.bs_max_count;
        };

        bucket_count_t width() const {
            return fabs(this->bs_max_count - this->bs_min_count);
        };

        bucket_count_t bs_min_count;
        bucket_count_t bs_max_count;
    };

    std::map<bucket_type_t, view_colors::role_t> hs_type2role;

    std::map<bucket_group_t, bucket_array_t> hs_groups;
    std::map<bucket_type_t, bucket_stats_t> hs_bucket_stats;

    unsigned int   hs_bucket_size; /* hours */
    unsigned int   hs_group_size;  /* days */
    label_source *hs_label_source;

    bucket_t *hs_token_bucket;
    std::vector<bucket_type_t> hs_displayed_buckets;
};

template<typename T>
class stacked_bar_chart {

public:
    stacked_bar_chart()
            : sbc_do_stacking(true), sbc_left(0), sbc_right(0), sbc_ident_to_show(-1) {

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

    int show_next_ident(int offset = 1) {
        this->sbc_ident_to_show += offset;
        if (this->sbc_ident_to_show < -1) {
            this->sbc_ident_to_show = this->sbc_idents.size() - 1;
        }
        else if (this->sbc_ident_to_show >= this->sbc_idents.size()) {
            this->sbc_ident_to_show = -1;
        }
        return this->sbc_ident_to_show;
    };

    void get_ident_to_show(T &ident_out) {
        if (this->sbc_ident_to_show != -1) {
            ident_out = this->sbc_idents[this->sbc_ident_to_show].ci_ident;
        }
    };

    void chart_attrs_for_value(const listview_curses &lc,
                               int &left,
                               const T &ident,
                               double value,
                               string_attrs_t &value_out) const
    {
        typename std::map<T, unsigned int>::const_iterator ident_iter = this->sbc_ident_lookup.find(ident);

        require(ident_iter != this->sbc_ident_lookup.end());

        int ident_index = ident_iter->second;
        unsigned long width, avail_width;
        bucket_stats_t overall_stats;
        struct line_range lr;
        vis_line_t height;

        if (this->sbc_ident_to_show != -1 &&
            this->sbc_ident_to_show != ident_index) {
            return;
        }

        lc.get_dimensions(height, width);

        for (int lpc = 0; lpc < this->sbc_idents.size(); lpc++) {
            if (this->sbc_ident_to_show == -1 || lpc == this->sbc_ident_to_show) {
                overall_stats.merge(this->sbc_idents[lpc].ci_stats, this->sbc_do_stacking);
            }
        }

        if (this->sbc_ident_to_show == -1) {
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

    void clear(void) {
        this->sbc_idents.clear();
        this->sbc_ident_lookup.clear();
        this->sbc_ident_to_show = -1;
    };

    void add_value(const T &ident, double amount = 1.0) {
        struct chart_ident &ci = this->find_ident(ident);
        ci.ci_stats.update(amount);
    };

protected:
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

    struct chart_ident {
        chart_ident(const T &ident) : ci_ident(ident) { };

        T ci_ident;
        int ci_attrs;
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
    int sbc_ident_to_show;
};

class hist_source2 : public text_sub_source {
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
        require(slice >= 60);
        require((slice % 60) == 0);

        this->hs_time_slice = slice;
    };

    int64_t get_time_slice() const {
        return this->hs_time_slice;
    };

    size_t text_line_count() {
        return this->hs_line_count;
    };

    size_t text_line_width() {
        return strlen(LINE_FORMAT) + 8 * 4;
    };

    void clear(void) {
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
                             bool no_scrub) {
        bucket_t &bucket = this->find_bucket(row);
        struct tm bucket_tm;
        char tm_buffer[128];
        char line[256];

        if (gmtime_r(&bucket.b_time, &bucket_tm) != NULL) {
            strftime(tm_buffer, sizeof(tm_buffer),
                     " %a %b %d %H:%M  ",
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

    size_t text_size_for_line(textview_curses &tc, int row, bool raw) {
        return 0;
    };

    time_t time_for_row(int64_t row) {
        require(row >= 0);
        require(row < this->hs_line_count);

        bucket_t &bucket = this->find_bucket(row);

        return bucket.b_time;
    };

    int64_t row_for_time(time_t time_bucket) {
        std::map<int64_t, struct bucket_block>::iterator iter;
        int64_t retval = 0;

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
