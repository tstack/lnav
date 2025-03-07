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
#include "hist_source_T.hh"
#include "textinput_curses.hh"

std::optional<vis_line_t>
hist_source2::row_for_time(timeval tv_bucket)
{
    int retval = 0;
    auto time_bucket = rounddown(to_us(tv_bucket), this->hs_time_slice);

    for (auto& bb : this->hs_blocks) {
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

line_info
hist_source2::text_value_for_line(textview_curses& tc,
                                  int row,
                                  std::string& value_out,
                                  line_flags_t flags)
{
    auto& bucket = this->find_bucket(row);
    struct tm bucket_tm;

    if (this->hs_needs_flush) {
        this->end_of_row();
    }

    value_out.clear();
    auto secs = to_time_t(bucket.b_time);
    if (gmtime_r(&secs, &bucket_tm) != nullptr) {
        fmt::format_to(std::back_inserter(value_out),
                       FMT_STRING(" {:%a %b %d %H:%M:%S}  "),
                       bucket_tm);
    } else {
        log_error("no time?");
    }
    fmt::format_to(
        std::back_inserter(value_out),
        FMT_STRING(" {:8L} normal  {:8L} errors  {:8L} warnings  {:8L} marks"),
        rint(bucket.value_for(hist_type_t::HT_NORMAL).hv_value),
        rint(bucket.value_for(hist_type_t::HT_ERROR).hv_value),
        rint(bucket.value_for(hist_type_t::HT_WARNING).hv_value),
        rint(bucket.value_for(hist_type_t::HT_MARK).hv_value));

    return {};
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

    if (width > 0 && tc.get_show_scrollbar()) {
        width -= 1;
    }
    for (int lpc = 0; lpc < lnav::enums::to_underlying(hist_type_t::HT__MAX);
         lpc++)
    {
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
hist_source2::add_value(std::chrono::microseconds ts,
                        hist_type_t htype,
                        double value)
{
    require_ge(ts.count(), this->hs_last_ts.count());

    ts = rounddown(ts, this->hs_time_slice);
    if (ts != this->hs_last_ts) {
        this->end_of_row();

        this->hs_current_row += 1;
        this->hs_last_ts = ts;
    }

    auto& bucket = this->find_bucket(this->hs_current_row);
    bucket.b_time = ts;
    bucket.value_for(htype).hv_value += value;

    this->hs_needs_flush = true;
}

void
hist_source2::init()
{
    auto& vc = view_colors::singleton();

    this->hs_chart.with_show_state(stacked_bar_chart_base::show_all{})
        .with_attrs_for_ident(hist_type_t::HT_NORMAL,
                              vc.attrs_for_role(role_t::VCR_TEXT))
        .with_attrs_for_ident(hist_type_t::HT_WARNING,
                              vc.attrs_for_role(role_t::VCR_WARNING))
        .with_attrs_for_ident(hist_type_t::HT_ERROR,
                              vc.attrs_for_role(role_t::VCR_ERROR))
        .with_attrs_for_ident(hist_type_t::HT_MARK,
                              vc.attrs_for_role(role_t::VCR_COMMENT));
}

void
hist_source2::clear()
{
    this->hs_line_count = 0;
    this->hs_current_row = -1;
    this->hs_last_ts = std::chrono::microseconds::zero();
    this->hs_blocks.clear();
    this->hs_chart.clear();
    if (this->tss_view != nullptr) {
        this->tss_view->get_bookmarks().clear();
    }
    this->init();
}

void
hist_source2::end_of_row()
{
    if (this->hs_current_row >= 0) {
        auto& last_bucket = this->find_bucket(this->hs_current_row);

        for (size_t lpc = 0;
             lpc < lnav::enums::to_underlying(hist_type_t::HT__MAX);
             lpc++)
        {
            auto& hv = last_bucket.b_values[lpc];
            this->hs_chart.add_value((const hist_type_t) lpc, hv.hv_value);

            if (hv.hv_value > 0.0) {
                const bookmark_type_t* bt = nullptr;
                switch ((hist_type_t) lpc) {
                    case hist_type_t::HT_WARNING: {
                        bt = &textview_curses::BM_WARNINGS;
                        break;
                    }
                    case hist_type_t::HT_ERROR: {
                        bt = &textview_curses::BM_ERRORS;
                        break;
                    }
                    case hist_type_t::HT_MARK: {
                        bt = &textview_curses::BM_META;
                        break;
                    }
                    default:
                        break;
                }
                if (bt != nullptr) {
                    auto& bm = this->tss_view->get_bookmarks();
                    bm[bt].insert_once(vis_line_t(this->hs_current_row));
                }
            }
        }
        this->hs_chart.next_row();
    }
}

std::optional<text_time_translator::row_info>
hist_source2::time_for_row(vis_line_t row)
{
    if (row < 0 || row > this->hs_line_count) {
        return std::nullopt;
    }

    const auto& bucket = this->find_bucket(row);

    return row_info{timeval{to_time_t(bucket.b_time), 0}, row};
}

hist_source2::bucket_t&
hist_source2::find_bucket(int64_t index)
{
    const auto block_index = index / BLOCK_SIZE;
    if (block_index >= this->hs_blocks.size()) {
        this->hs_blocks.resize(block_index + 1);
    }
    auto& bb = this->hs_blocks[block_index];
    const unsigned int intra_block_index = index % BLOCK_SIZE;
    bb.bb_used = std::max(intra_block_index, bb.bb_used);
    this->hs_line_count = std::max(this->hs_line_count, index + 1);
    return bb.bb_buckets[intra_block_index];
}
