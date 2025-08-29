/**
 * Copyright (c) 2025, Timothy Stack
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

#ifndef lnav_ptime_spec_hh
#define lnav_ptime_spec_hh

#include <sys/types.h>

#include "base/time_util.hh"

inline bool
ptime_YmdTHM(struct exttm* dst, const char* str, off_t& off_inout, ssize_t len)
{
    static constexpr auto WIDTH = 16;

    if (off_inout + WIDTH > len) {
        return false;
    }

    auto sep_count = size_t{0};

    auto Y_hundreds
        = (str[off_inout + 0] - '0') * 10 + (str[off_inout + 1] - '0') * 1;
    auto Y_ones
        = (str[off_inout + 2] - '0') * 10 + (str[off_inout + 3] - '0') * 1;

    sep_count += str[off_inout + 4] == '-';

    auto m = (str[off_inout + 5] - '0') * 10 + (str[off_inout + 6] - '0') * 1;

    sep_count += str[off_inout + 7] == '-';

    auto d = (str[off_inout + 8] - '0') * 10 + (str[off_inout + 9] - '0') * 1;

    sep_count += str[off_inout + 10] == 'T';

    auto H = (str[off_inout + 11] - '0') * 10 + (str[off_inout + 12] - '0') * 1;

    sep_count += str[off_inout + 13] == ':';

    auto M = (str[off_inout + 14] - '0') * 10 + (str[off_inout + 15] - '0') * 1;

    auto Y = (Y_hundreds * 100 + Y_ones) - 1900;
    if (sep_count != 4 || Y < 0 || Y > 1100 || m < 1 || m > 12 || d < 0
        || d > 31 || H < 0 || H > 23 || M < 0 || M > 59)
    {
        return false;
    }

    dst->et_tm.tm_year = Y;
    dst->et_tm.tm_mon = m - 1;
    dst->et_tm.tm_mday = d;
    dst->et_tm.tm_hour = H;
    dst->et_tm.tm_min = M;

    dst->et_flags |= ETF_YEAR_SET | ETF_MONTH_SET | ETF_DAY_SET | ETF_HOUR_SET
        | ETF_MINUTE_SET;

    off_inout += WIDTH;

    return true;
}

#endif
