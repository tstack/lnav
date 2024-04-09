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

#include "hist_source.hh"

#include "base/math_util.hh"
#include "config.h"
#include "fmt/chrono.h"

nonstd::optional<vis_line_t>
hist_source2::row_for_time(struct timeval tv_bucket)
{
    std::map<int64_t, struct bucket_block>::iterator iter;
    int retval = 0;
    time_t time_bucket = rounddown(tv_bucket.tv_sec, this->hs_time_slice);

    for (iter = this->hs_blocks.begin(); iter != this->hs_blocks.end(); ++iter)
    {
        struct bucket_block& bb = iter->second;

        if (time_bucket < bb.bb_buckets[0].b_time) {
            break;
        }
        if (time_bucket > bb.bb_buckets[bb.bb_used].b_time) {
            retval += bb.bb_used + 1;
            continue;
        }

        for (unsigned int lpc = 0; lpc <= bb.bb_used; lpc++, retval++) {
            if (time_bucket <= bb.bb_buckets[lpc].b_time) {
                return vis_line_t(retval);
            }
        }
    }
    return vis_line_t(retval);
}

void
hist_source2::text_value_for_line(textview_curses& tc,
                                  int row,
                                  std::string& value_out,
                                  text_sub_source::line_flags_t flags)
{
    bucket_t& bucket = this->find_bucket(row);
    struct tm bucket_tm;

    value_out.clear();
    if (gmtime_r(&bucket.b_time, &bucket_tm) != nullptr) {
        fmt::format_to(std::back_inserter(value_out),
                       FMT_STRING(" {:%a %b %d %H:%M:%S}  "),
                       bucket_tm);
    } else {
        log_error("no time?");
    }
    fmt::format_to(
        std::back_inserter(value_out),
        FMT_STRING(" {:8L} normal  {:8L} errors  {:8L} warnings  {:8L} marks"),
        rint(bucket.b_values[HT_NORMAL].hv_value),
        rint(bucket.b_values[HT_ERROR].hv_value),
        rint(bucket.b_values[HT_WARNING].hv_value),
        rint(bucket.b_values[HT_MARK].hv_value));
}

void
hist_source2::text_attrs_for_line(textview_curses& tc,
                                  int row,
                                  string_attrs_t& value_out)
{
    auto& bucket = this->find_bucket(row);
    auto dim = tc.get_dimensions();
    auto width = dim.second;
    int left = 0;

    for (int lpc = 0; lpc < HT__MAX; lpc++) {
        this->hs_chart.chart_attrs_for_value(tc,
                                             left,
                                             width,
                                             (const hist_type_t) lpc,
                                             bucket.b_values[lpc].hv_value,
                                             value_out);
    }
    auto alt_row_index = row % 4;
    if (alt_row_index == 2 || alt_row_index == 3) {
        value_out.emplace_back(line_range{0, -1},
                               VC_ROLE.value(role_t::VCR_ALT_ROW));
    }
}

void
hist_source2::add_value(time_t row,
                        hist_source2::hist_type_t htype,
                        double value)
{
    require_ge(row, this->hs_last_row);

    row = rounddown(row, this->hs_time_slice);
    if (row != this->hs_last_row) {
        this->end_of_row();

        this->hs_last_bucket += 1;
        this->hs_last_row = row;
    }

    auto& bucket = this->find_bucket(this->hs_last_bucket);
    bucket.b_time = row;
    bucket.b_values[htype].hv_value += value;
}

void
hist_source2::init()
{
    view_colors& vc = view_colors::singleton();

    this->hs_chart.with_show_state(stacked_bar_chart_base::show_all{})
        .with_attrs_for_ident(HT_NORMAL, vc.attrs_for_role(role_t::VCR_TEXT))
        .with_attrs_for_ident(HT_WARNING,
                              vc.attrs_for_role(role_t::VCR_WARNING))
        .with_attrs_for_ident(HT_ERROR, vc.attrs_for_role(role_t::VCR_ERROR))
        .with_attrs_for_ident(HT_MARK, vc.attrs_for_role(role_t::VCR_COMMENT));
}

void
hist_source2::clear()
{
    this->hs_line_count = 0;
    this->hs_last_bucket = -1;
    this->hs_last_row = -1;
    this->hs_blocks.clear();
    this->hs_chart.clear();
    this->init();
}

void
hist_source2::end_of_row()
{
    if (this->hs_last_bucket >= 0) {
        bucket_t& last_bucket = this->find_bucket(this->hs_last_bucket);

        for (int lpc = 0; lpc < HT__MAX; lpc++) {
            this->hs_chart.add_value((const hist_type_t) lpc,
                                     last_bucket.b_values[lpc].hv_value);
        }
    }
}

nonstd::optional<text_time_translator::row_info>
hist_source2::time_for_row(vis_line_t row)
{
    if (row < 0 || row > this->hs_line_count) {
        return nonstd::nullopt;
    }

    bucket_t& bucket = this->find_bucket(row);

    return row_info{timeval{bucket.b_time, 0}, row};
}

hist_source2::bucket_t&
hist_source2::find_bucket(int64_t index)
{
    struct bucket_block& bb = this->hs_blocks[index / BLOCK_SIZE];
    unsigned int intra_block_index = index % BLOCK_SIZE;
    bb.bb_used = std::max(intra_block_index, bb.bb_used);
    this->hs_line_count = std::max(this->hs_line_count, index + 1);
    return bb.bb_buckets[intra_block_index];
}
