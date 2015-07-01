/**
 * Copyright (c) 2014, Timothy Stack
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
 * @file ptimec_rt.cc
 */

#include "config.h"
#include <string.h>
#include <langinfo.h>
#include <set>
#include <algorithm>

#include "ptimec.hh"

bool ptime_b_slow(struct exttm *dst, const char *str, off_t &off_inout, ssize_t len)
{
    size_t zone_len = len - off_inout;
    char zone[zone_len + 1];
    const char *end_of_date;

    memcpy(zone, &str[off_inout], zone_len);
    zone[zone_len] = '\0';
    if ((end_of_date = strptime(zone, "%b", &dst->et_tm)) != NULL) {
        off_inout += end_of_date - zone;
        return true;
    }

    return false;
}

#define FMT_CASE(ch, c) \
    case ch: \
        if (!ptime_ ## c(dst, str, off, len)) return false; \
        lpc += 1; \
        break

bool ptime_fmt(const char *fmt, struct exttm *dst, const char *str, off_t &off, ssize_t len)
{
    for (ssize_t lpc = 0; fmt[lpc]; lpc++) {
        if (fmt[lpc] == '%') {
            switch (fmt[lpc + 1]) {
                case 'a':
                case 'Z':
                    if (fmt[lpc + 2]) {
                        if (!ptime_upto(fmt[lpc + 2], str, off, len)) return false;
                        lpc += 1;
                    }
                    else {
                        if (!ptime_upto_end(str, off, len)) return false;
                        lpc += 1;
                    }
                    break;
                FMT_CASE('b', b);
                FMT_CASE('S', S);
                FMT_CASE('s', s);
                FMT_CASE('L', L);
                FMT_CASE('M', M);
                FMT_CASE('H', H);
                FMT_CASE('i', i);
                FMT_CASE('I', I);
                FMT_CASE('d', d);
                FMT_CASE('e', e);
                FMT_CASE('k', k);
                FMT_CASE('l', l);
                FMT_CASE('m', m);
                FMT_CASE('p', p);
                FMT_CASE('Y', Y);
                FMT_CASE('y', y);
                FMT_CASE('z', z);
            }
        }
        else {
            if (!ptime_char(fmt[lpc], str, off, len)) return false;
        }
    }

    return true;
}