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
 * @file ptimec.hh
 */

#ifndef __pctimec_hh
#define __pctimec_hh

// XXX
#include <stdio.h>

#include <ctype.h>
#include <time.h>
#include <sys/types.h>

#define PTIME_CONSUME(amount, block) \
    if (off_inout + amount > len) { \
        return false; \
    } \
    \
    block \
    \
    off_inout += amount;

#define ABR_TO_INT(a, b, c) \
    ((a) | (b << 8) | (c << 16))

inline
bool ptime_upto(char ch, const char *str, off_t &off_inout, size_t len)
{
    for (; off_inout < len; off_inout++) {
        if (str[off_inout] == ch) {
            return true;
        }
    }

    return false;
}

bool ptime_b_slow(struct tm *dst, const char *str, off_t &off_inout, size_t len);

inline bool ptime_b(struct tm *dst, const char *str, off_t &off_inout, size_t len)
{
    if (off_inout + 3 < len) {
        int *iptr = (int *)(&str[off_inout]);
        int val;

        switch (*iptr & 0xdfdfdf) {
        case ABR_TO_INT('J', 'A', 'N'):
            val = 0;
            break;
        case ABR_TO_INT('F', 'E', 'B'):
            val = 1;
            break;
        case ABR_TO_INT('M', 'A', 'R'):
            val = 2;
            break;
        case ABR_TO_INT('A', 'P', 'R'):
            val = 3;
            break;
        case ABR_TO_INT('M', 'A', 'Y'):
            val = 4;
            break;
        case ABR_TO_INT('J', 'U', 'N'):
            val = 5;
            break;
        case ABR_TO_INT('J', 'U', 'L'):
            val = 6;
            break;
        case ABR_TO_INT('A', 'U', 'G'):
            val = 7;
            break;
        case ABR_TO_INT('S', 'E', 'P'):
            val = 8;
            break;
        case ABR_TO_INT('O', 'C', 'T'):
            val = 9;
            break;
        case ABR_TO_INT('N', 'O', 'V'):
            val = 10;
            break;
        case ABR_TO_INT('D', 'E', 'C'):
            val = 11;
            break;
        default:
            val = -1;
            break;
        }
        if (val >= 0) {
            off_inout += 3;
            dst->tm_mon = val;
            return true;
        }
    }

    return ptime_b_slow(dst, str, off_inout, len);
}

inline bool ptime_S(struct tm *dst, const char *str, off_t &off_inout, size_t len)
{
    PTIME_CONSUME(2, {
        if (str[off_inout + 1] > '9') {
            return false;
        }
        dst->tm_sec = (str[off_inout] - '0') * 10 + (str[off_inout + 1] - '0');
    });

    return (dst->tm_sec >= 0 && dst->tm_sec <= 59);
}

inline bool ptime_M(struct tm *dst, const char *str, off_t &off_inout, size_t len)
{
    PTIME_CONSUME(2, {
        if (str[off_inout + 1] > '9') {
            return false;
        }
        dst->tm_min = (str[off_inout] - '0') * 10 + (str[off_inout + 1] - '0');
    });

    return (dst->tm_min >= 0 && dst->tm_min <= 59);
}

inline bool ptime_H(struct tm *dst, const char *str, off_t &off_inout, size_t len)
{
    PTIME_CONSUME(2, {
        if (str[off_inout + 1] > '9') {
            return false;
        }
        dst->tm_hour = (str[off_inout] - '0') * 10 + (str[off_inout + 1] - '0');
    });

    return (dst->tm_hour >= 0 && dst->tm_hour <= 23);
}

inline bool ptime_d(struct tm *dst, const char *str, off_t &off_inout, size_t len)
{
    PTIME_CONSUME(2, {
        if (str[off_inout] == ' ') {
            dst->tm_mday = 0;
        }
        else {
            dst->tm_mday = (str[off_inout] - '0') * 10;
        }
        if (str[off_inout + 1] > '9') {
            return false;
        }
        dst->tm_mday += (str[off_inout + 1] - '0');
    });

    return (dst->tm_mday >= 1 && dst->tm_mday <= 31);
}

inline bool ptime_m(struct tm *dst, const char *str, off_t &off_inout, size_t len)
{
    PTIME_CONSUME(2, {
        if (str[off_inout + 1] > '9') {
            return false;
        }
        dst->tm_mon = ((str[off_inout] - '0') * 10 + (str[off_inout + 1] - '0')) - 1;
    });

    return (0 <= dst->tm_mon && dst->tm_mon <= 11);
}

inline bool ptime_Y(struct tm *dst, const char *str, off_t &off_inout, size_t len)
{
    PTIME_CONSUME(4, {
        dst->tm_year = (
            (str[off_inout + 0] - '0') * 1000 +
            (str[off_inout + 1] - '0') *  100 +
            (str[off_inout + 2] - '0') *   10 +
            (str[off_inout + 3] - '0') *    1) - 1900;
    });

    return true;
}

inline bool ptime_y(struct tm *dst, const char *str, off_t &off_inout, size_t len)
{
    PTIME_CONSUME(2, {
        dst->tm_year = (
            (str[off_inout + 0] - '0') * 10 +
            (str[off_inout + 1] - '0') *  1);

        if (dst->tm_year >= 0 && dst->tm_year < 100) {
            if (dst->tm_year < 69) {
                dst->tm_year += 100;
            }
            return true;
        }
    });

    return true;
}

inline bool ptime_z(struct tm *dst, const char *str, off_t &off_inout, size_t len)
{
#ifdef HAVE_STRUCT_TM_TM_ZONE
    PTIME_CONSUME(5, {
        long sign;
        long hours;
        long mins;

        if (str[off_inout] == '+') {
            sign = 1;
        }
        else if (str[off_inout] == '-') {
            sign = -1;
        }
        else {
            return false;
        }

        hours = (
            (str[off_inout + 0] - '0') * 10 +
            (str[off_inout + 1] - '0') *  1) * 60 * 60;
        mins = (
            (str[off_inout + 2] - '0') *   10 +
            (str[off_inout + 3] - '0') *    1) * 60;
        dst->tm_gmtoff = hours + mins;
    });
#endif

    return true;
}

inline bool ptime_char(char val, const char *str, off_t &off_inout, size_t len)
{
    PTIME_CONSUME(1, {
        if (str[off_inout] != val) {
            return false;
        }
    });

    return true;
}

typedef bool (*ptime_func)(struct tm *dst, const char *str, off_t &off, size_t len);

struct ptime_fmt {
    const char *pf_fmt;
    ptime_func pf_func;
};

extern struct ptime_fmt PTIMEC_FORMATS[];

extern const char *PTIMEC_FORMAT_STR[];

#endif
