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
 */

#include "config.h"

#include <math.h>
#include <limits.h>

#include <numeric>

#include "lnav_util.hh"
#include "hist_source.hh"

using namespace std;

hist_source::hist_source()
    : hs_bucket_size(1),
      hs_group_size(100),
      hs_label_source(NULL),
      hs_token_bucket(NULL)
{ }

void hist_source::text_value_for_line(textview_curses &tc,
                                      int row,
                                      std::string &value_out,
                                      bool no_scrub)
{
    int grow = row / (this->buckets_per_group() + 1);
    int brow = row % (this->buckets_per_group() + 1);

    if (brow == 0) {
        unsigned long width;
        vis_line_t    height;

        tc.get_dimensions(height, width);
        value_out.insert((unsigned int)0, width, '-');

        if (this->hs_label_source != NULL) {
            this->hs_label_source->hist_label_for_group(grow, value_out);
        }
        this->hs_token_bucket = NULL;
    }
    else {
        std::map<bucket_group_t, bucket_array_t>::iterator group_iter;
        bucket_t::iterator iter;
        int bucket_index;

        bucket_index          = brow - 1;
        group_iter = this->hs_groups.begin();
        advance(group_iter, grow);
        this->hs_token_bucket = &(group_iter->second[bucket_index]);
        if (this->hs_label_source != NULL) {
            this->hs_label_source->
            hist_label_for_bucket((group_iter->first * this->hs_group_size) +
                                  (bucket_index * this->hs_bucket_size),
                                  *this->hs_token_bucket,
                                  value_out);
        }
    }
}

void hist_source::text_attrs_for_line(textview_curses &tc,
                                      int row,
                                      string_attrs_t &value_out)
{
    int            grow         = row / (this->buckets_per_group() + 1);
    int            brow         = row % (this->buckets_per_group() + 1);
    int            bucket_index = brow - 1;

    if (this->hs_token_bucket != NULL) {
        std::map<bucket_group_t, bucket_array_t>::iterator group_iter;
        view_colors &      vc = view_colors::singleton();
        unsigned long      width, avail_width;
        bucket_t::iterator iter;
        vis_line_t         height;
        struct line_range  lr;

        group_iter = this->hs_groups.begin();
        advance(group_iter, grow);

        tc.get_dimensions(height, width);
        avail_width = width - this->hs_token_bucket->size();

        bucket_stats_t overall_stats;

        for (std::map<bucket_type_t, bucket_stats_t>::iterator stats_iter =
             this->hs_bucket_stats.begin();
             stats_iter != this->hs_bucket_stats.end();
             ++stats_iter) {
            if (!this->is_bucket_graphed(stats_iter->first))
                continue;
            overall_stats.merge(stats_iter->second);
        }

        lr.lr_start = 0;
        for (iter = this->hs_token_bucket->begin();
             iter != this->hs_token_bucket->end();
             iter++) {
            double percent = (double)(iter->second - overall_stats.bs_min_count) /
        overall_stats.width();
            int amount, attrs;

            if (!this->is_bucket_graphed(iter->first))
                continue;
            
            attrs = vc.
                    reverse_attrs_for_role(this->get_role_for_type(iter->first));
            amount = (int)rint(percent * avail_width);
            if (iter->second == 0.0) {
                amount = 0;
            }
            else {
                amount = max(1, amount);
            }

            lr.lr_end = lr.lr_start + amount;
            value_out.push_back(string_attr(lr, &view_curses::VC_STYLE, attrs));

            lr.lr_start = lr.lr_end;
        }

        this->hs_label_source->hist_attrs_for_bucket(
            (group_iter->first * this->hs_group_size) +
            (bucket_index * this->hs_bucket_size),
            *this->hs_token_bucket,
            value_out);
    }
}

void hist_source::add_value(unsigned int value,
                            bucket_type_t bt,
                            bucket_count_t amount)
{
    bucket_group_t bg;

    bg = bucket_group_t(value / this->hs_group_size);

    bucket_array_t &ba = this->hs_groups[bg];

    if (ba.empty()) {
        ba.resize(this->buckets_per_group());
    }

    bucket_count_t &bc = ba[(value % this->hs_group_size) /
                            this->hs_bucket_size][bt];

    bc += amount;

    bucket_stats_t &stats = this->hs_bucket_stats[bt];

    stats.bs_max_count = max(stats.bs_max_count, bc);
    stats.bs_min_count = min(stats.bs_min_count, bc);
}

const char *hist_source2::LINE_FORMAT = " %8d normal  %8d errors  %8d warnings  %8d marks";
