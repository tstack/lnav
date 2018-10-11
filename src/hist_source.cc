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

#include "lnav_util.hh"
#include "hist_source.hh"

using namespace std;

const char *hist_source2::LINE_FORMAT = " %8d normal  %8d errors  %8d warnings  %8d marks";

int hist_source2::row_for_time(struct timeval tv_bucket)
{
    std::map<int64_t, struct bucket_block>::iterator iter;
    int retval = 0;
    time_t time_bucket = rounddown(tv_bucket.tv_sec, this->hs_time_slice);

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
}

void hist_source2::text_value_for_line(textview_curses &tc, int row,
                                       std::string &value_out,
                                       text_sub_source::line_flags_t flags)
{
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
}

void hist_source2::text_attrs_for_line(textview_curses &tc, int row,
                                       string_attrs_t &value_out)
{
    bucket_t &bucket = this->find_bucket(row);
    int left = 0;

    for (int lpc = 0; lpc < HT__MAX; lpc++) {
        this->hs_chart.chart_attrs_for_value(
            tc, left, (const hist_type_t) lpc,
            bucket.b_values[lpc].hv_value,
            value_out);
    }
}
