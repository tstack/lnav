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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file ptimec_rt.cc
 */

#include "ptimec.hh"

#include <string.h>

#include "base/short_alloc.h"
#include "config.h"

bool
ptime_b_slow(struct exttm* dst, const char* str, off_t& off_inout, ssize_t len)
{
    size_t zone_len = len - off_inout;
    stack_buf allocator;
    auto* zone = allocator.allocate(zone_len + 1);
    const char* end_of_date;

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
        if (!ptime_##c(dst, str, off, len)) \
            return false; \
        lpc += 1; \
        break

bool
ptime_fmt(const char* fmt,
          struct exttm* dst,
          const char* str,
          off_t& off,
          ssize_t len)
{
    for (ssize_t lpc = 0; fmt[lpc]; lpc++) {
        if (fmt[lpc] == '%') {
            switch (fmt[lpc + 1]) {
                case 'B': {
                    size_t b_len = len - off;
                    stack_buf allocator;
                    auto* full_month = allocator.allocate(b_len + 1);
                    const char* end_of_date;

                    memcpy(full_month, &str[off], b_len);
                    full_month[b_len] = '\0';
                    if ((end_of_date = strptime(full_month, "%B", &dst->et_tm))
                        != nullptr)
                    {
                        off += end_of_date - full_month;
                        lpc += 1;
                    } else {
                        return false;
                    }
                    break;
                }
                case 'a':
                case 'Z':
                    if (fmt[lpc + 2]) {
                        if (!ptime_upto(fmt[lpc + 2], str, off, len)) {
                            return false;
                        }
                        lpc += 1;
                    } else {
                        if (!ptime_upto_end(str, off, len)) {
                            return false;
                        }
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
                    FMT_CASE('6', 6);
                    FMT_CASE('9', 9);
                    FMT_CASE('I', I);
                    FMT_CASE('d', d);
                    FMT_CASE('e', e);
                    FMT_CASE('j', j);
                    FMT_CASE('f', f);
                    FMT_CASE('k', k);
                    FMT_CASE('l', l);
                    FMT_CASE('m', m);
                    FMT_CASE('N', N);
                    FMT_CASE('p', p);
                    FMT_CASE('q', q);
                    FMT_CASE('Y', Y);
                    FMT_CASE('y', y);
                    FMT_CASE('z', z);
                    FMT_CASE('@', at);
            }
        } else {
            if (!ptime_char(fmt[lpc], str, off, len)) {
                return false;
            }
        }
    }

    return true;
}

#define FTIME_FMT_CASE(ch, c) \
    case ch: \
        ftime_##c(dst, off_inout, len, tm); \
        lpc += 1; \
        break

size_t
ftime_fmt(char* dst, size_t len, const char* fmt, const struct exttm& tm)
{
    off_t off_inout = 0;

    for (ssize_t lpc = 0; fmt[lpc]; lpc++) {
        if (fmt[lpc] == '%') {
            switch (fmt[lpc + 1]) {
                case '%':
                    ftime_char(dst, off_inout, len, '%');
                    break;
                    FTIME_FMT_CASE('a', a);
                    FTIME_FMT_CASE('b', b);
                    FTIME_FMT_CASE('S', S);
                    FTIME_FMT_CASE('s', s);
                    FTIME_FMT_CASE('L', L);
                    FTIME_FMT_CASE('M', M);
                    FTIME_FMT_CASE('H', H);
                    FTIME_FMT_CASE('i', i);
                    FTIME_FMT_CASE('6', 6);
                    FTIME_FMT_CASE('9', 9);
                    FTIME_FMT_CASE('I', I);
                    FTIME_FMT_CASE('d', d);
                    FTIME_FMT_CASE('e', e);
                    FTIME_FMT_CASE('j', j);
                    FTIME_FMT_CASE('f', f);
                    FTIME_FMT_CASE('k', k);
                    FTIME_FMT_CASE('l', l);
                    FTIME_FMT_CASE('m', m);
                    FTIME_FMT_CASE('N', N);
                    FTIME_FMT_CASE('p', p);
                    FTIME_FMT_CASE('q', q);
                    FTIME_FMT_CASE('Y', Y);
                    FTIME_FMT_CASE('y', y);
                    FTIME_FMT_CASE('z', z);
            }
        } else {
            ftime_char(dst, off_inout, len, fmt[lpc]);
        }
    }

    dst[off_inout] = '\0';

    return (size_t) off_inout;
}
