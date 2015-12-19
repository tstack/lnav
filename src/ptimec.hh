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
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <arpa/inet.h>

struct tm *secs2tm(time_t *tim_p, struct tm *res);

enum exttm_bits_t {
    ETB_YEAR_SET,
    ETB_MONTH_SET,
    ETB_DAY_SET,
};

enum exttm_flags_t {
    ETF_YEAR_SET = (1L << ETB_YEAR_SET),
    ETF_MONTH_SET = (1L << ETB_MONTH_SET),
    ETF_DAY_SET = (1L << ETB_DAY_SET),
};

struct exttm {
    struct tm et_tm;
    int32_t et_nsec;
    int et_flags;
    long et_gmtoff;
};

#define PTIME_CONSUME(amount, block) \
    if ((off_inout + amount) > len) { \
        return false; \
    } \
    \
    block \
    \
    off_inout += amount;

#define ABR_TO_INT(a, b, c) \
    ((a << 24) | (b << 16) | (c << 8))

inline
bool ptime_upto(char ch, const char *str, off_t &off_inout, ssize_t len)
{
    for (; off_inout < len; off_inout++) {
        if (str[off_inout] == ch) {
            return true;
        }
    }

    return false;
}

inline
bool ptime_upto_end(const char *str, off_t &off_inout, ssize_t len)
{
    off_inout = len;

    return true;
}

bool ptime_b_slow(struct exttm *dst, const char *str, off_t &off_inout, ssize_t len);

inline bool ptime_b(struct exttm *dst, const char *str, off_t &off_inout, ssize_t len)
{
    if (off_inout + 3 < len) {
        uint32_t *iptr = (uint32_t *)(&str[off_inout]);
        int val;

        switch (htonl(*iptr) & 0xdfdfdf00) {
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
            dst->et_tm.tm_mon = val;
            dst->et_flags |= ETF_MONTH_SET;
            return true;
        }
    }

    return ptime_b_slow(dst, str, off_inout, len);
}

inline bool ptime_S(struct exttm *dst, const char *str, off_t &off_inout, ssize_t len)
{
    PTIME_CONSUME(2, {
        if (str[off_inout + 1] > '9') {
            return false;
        }
        dst->et_tm.tm_sec = (str[off_inout] - '0') * 10 + (str[off_inout + 1] - '0');
    });

    return (dst->et_tm.tm_sec >= 0 && dst->et_tm.tm_sec <= 59);
}

inline bool ptime_s(struct exttm *dst, const char *str, off_t &off_inout, ssize_t len)
{
    time_t epoch = 0;

    while (off_inout < len && isdigit(str[off_inout])) {
        epoch *= 10;
        epoch += str[off_inout] - '0';
        off_inout += 1;
    }

    secs2tm(&epoch, &dst->et_tm);
    dst->et_flags = ETF_DAY_SET|ETF_MONTH_SET|ETF_YEAR_SET;

    return (epoch > 0);
}

inline bool ptime_L(struct exttm *dst, const char *str, off_t &off_inout, ssize_t len)
{
    int ms = 0;

    PTIME_CONSUME(3, {
        char c0 = str[off_inout];
        char c1 = str[off_inout + 1];
        char c2 = str[off_inout + 2];
        if (!isdigit(c0) || !isdigit(c1) || !isdigit(c2)) {
            return false;
        }
        ms = (
                (str[off_inout    ] - '0') * 100 +
                (str[off_inout + 1] - '0') * 10 +
                (str[off_inout + 2] - '0'));
    });

    if ((ms >= 0 && ms <= 999)) {
        dst->et_nsec = ms * 1000000;
        return true;
    }
    return false;
}

inline bool ptime_M(struct exttm *dst, const char *str, off_t &off_inout, ssize_t len)
{
    PTIME_CONSUME(2, {
        if (str[off_inout + 1] > '9') {
            return false;
        }
        dst->et_tm.tm_min = (str[off_inout] - '0') * 10 + (str[off_inout + 1] - '0');
    });

    return (dst->et_tm.tm_min >= 0 && dst->et_tm.tm_min <= 59);
}

inline bool ptime_H(struct exttm *dst, const char *str, off_t &off_inout, ssize_t len)
{
    PTIME_CONSUME(2, {
        if (str[off_inout + 1] > '9') {
            return false;
        }
        dst->et_tm.tm_hour = (str[off_inout] - '0') * 10 + (str[off_inout + 1] - '0');
    });

    return (dst->et_tm.tm_hour >= 0 && dst->et_tm.tm_hour <= 23);
}

inline bool ptime_i(struct exttm *dst, const char *str, off_t &off_inout, ssize_t len)
{
    uint64_t epoch_ms = 0;
    time_t epoch;

    while (off_inout < len && isdigit(str[off_inout])) {
        epoch_ms *= 10;
        epoch_ms += str[off_inout] - '0';
        off_inout += 1;
    }

    dst->et_nsec = (epoch_ms % 1000ULL) * 1000000;
    epoch = (epoch_ms / 1000ULL);
    secs2tm(&epoch, &dst->et_tm);
    dst->et_flags = ETF_DAY_SET|ETF_MONTH_SET|ETF_YEAR_SET;

    return (epoch_ms > 0);
}

inline bool ptime_I(struct exttm *dst, const char *str, off_t &off_inout, ssize_t len)
{
    PTIME_CONSUME(2, {
        if (str[off_inout + 1] > '9') {
            return false;
        }
        dst->et_tm.tm_hour = (str[off_inout] - '0') * 10 + (str[off_inout + 1] - '0');
    });

    return (dst->et_tm.tm_hour >= 1 && dst->et_tm.tm_hour <= 12);
}

inline bool ptime_d(struct exttm *dst, const char *str, off_t &off_inout, ssize_t len)
{
    PTIME_CONSUME(2, {
        if (str[off_inout] == ' ') {
            dst->et_tm.tm_mday = 0;
        }
        else {
            dst->et_tm.tm_mday = (str[off_inout] - '0') * 10;
        }
        if (str[off_inout + 1] > '9') {
            return false;
        }
        dst->et_tm.tm_mday += (str[off_inout + 1] - '0');
    });

    if (dst->et_tm.tm_mday >= 1 && dst->et_tm.tm_mday <= 31) {
        dst->et_flags |= ETF_DAY_SET;
        return true;
    }
    return false;
}

inline bool ptime_e(struct exttm *dst, const char *str, off_t &off_inout, ssize_t len)
{
    dst->et_tm.tm_mday = 0;
    PTIME_CONSUME(1, {
        if (str[off_inout] < '1' || str[off_inout] > '9') {
            return false;
        }
        dst->et_tm.tm_mday = str[off_inout] - '0';
    });
    if (off_inout + 1 < len) {
        if (str[off_inout] >= '0' && str[off_inout] <= '9') {
            dst->et_tm.tm_mday *= 10;
            dst->et_tm.tm_mday += str[off_inout] - '0';
            off_inout += 1;
        }
    }

    if (dst->et_tm.tm_mday >= 1 && dst->et_tm.tm_mday <= 31) {
        dst->et_flags |= ETF_DAY_SET;
        return true;
    }
    return false;
}

inline bool ptime_m(struct exttm *dst, const char *str, off_t &off_inout, ssize_t len)
{
    dst->et_tm.tm_mon = 0;
    PTIME_CONSUME(1, {
        if (str[off_inout] < '0' || str[off_inout] > '9') {
            return false;
        }
        dst->et_tm.tm_mon = str[off_inout] - '0';
    });
    if (off_inout + 1 < len) {
        if (str[off_inout] >= '0' && str[off_inout] <= '9') {
            dst->et_tm.tm_mon *= 10;
            dst->et_tm.tm_mon += str[off_inout] - '0';
            off_inout += 1;
        }
    }

    dst->et_tm.tm_mon -= 1;

    if (dst->et_tm.tm_mon >= 0 && dst->et_tm.tm_mon <= 11) {
        dst->et_flags |= ETF_MONTH_SET;
        return true;
    }
    return false;
}

inline bool ptime_k(struct exttm *dst, const char *str, off_t &off_inout, ssize_t len)
{
    dst->et_tm.tm_hour = 0;
    PTIME_CONSUME(1, {
        if (str[off_inout] < '0' || str[off_inout] > '9') {
            return false;
        }
        dst->et_tm.tm_hour = str[off_inout] - '0';
    });
    if (off_inout + 1 < len) {
        if (str[off_inout] >= '0' && str[off_inout] <= '9') {
            dst->et_tm.tm_hour *= 10;
            dst->et_tm.tm_hour += str[off_inout] - '0';
            off_inout += 1;
        }
    }

    return (dst->et_tm.tm_hour >= 0 && dst->et_tm.tm_hour <= 23);
}

inline bool ptime_l(struct exttm *dst, const char *str, off_t &off_inout, ssize_t len)
{
    dst->et_tm.tm_hour = 0;
    PTIME_CONSUME(1, {
        if (str[off_inout] < '1' || str[off_inout] > '9') {
            return false;
        }
        dst->et_tm.tm_hour = str[off_inout] - '0';
    });
    if (off_inout + 1 < len) {
        if (str[off_inout] >= '0' && str[off_inout] <= '9') {
            dst->et_tm.tm_hour *= 10;
            dst->et_tm.tm_hour += str[off_inout] - '0';
            off_inout += 1;
        }
    }

    return (dst->et_tm.tm_hour >= 1 && dst->et_tm.tm_hour <= 12);
}

inline bool ptime_p(struct exttm *dst, const char *str, off_t &off_inout, ssize_t len)
{
    PTIME_CONSUME(2, {
        char lead = str[off_inout];

        if ((str[off_inout + 1] & 0xdf) != 'M') {
            return false;
        }
        else if ((lead & 0xdf) == 'A') {
        }
        else if ((lead & 0xdf) == 'P') {
            dst->et_tm.tm_hour += 12;
        }
        else {
            return false;
        }
    });

    return true;
}

inline bool ptime_Y(struct exttm *dst, const char *str, off_t &off_inout, ssize_t len)
{
    PTIME_CONSUME(4, {
        dst->et_tm.tm_year = (
            (str[off_inout + 0] - '0') * 1000 +
            (str[off_inout + 1] - '0') *  100 +
            (str[off_inout + 2] - '0') *   10 +
            (str[off_inout + 3] - '0') *    1) - 1900;

        dst->et_flags |= ETF_YEAR_SET;
    });

    return true;
}

inline bool ptime_y(struct exttm *dst, const char *str, off_t &off_inout, ssize_t len)
{
    PTIME_CONSUME(2, {
        dst->et_tm.tm_year = (
            (str[off_inout + 0] - '0') * 10 +
            (str[off_inout + 1] - '0') *  1);
    });

    if (dst->et_tm.tm_year >= 0 && dst->et_tm.tm_year < 100) {
        if (dst->et_tm.tm_year < 69) {
            dst->et_tm.tm_year += 100;
        }

        dst->et_flags |= ETF_YEAR_SET;
        return true;
    }
    return false;
}

inline bool ptime_z(struct exttm *dst, const char *str, off_t &off_inout, ssize_t len)
{
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
        dst->et_gmtoff = sign * (hours + mins);
#ifdef HAVE_STRUCT_TM_TM_ZONE
        dst->et_tm.tm_gmtoff = sign * (hours + mins);
#endif
    });

    return true;
}

inline bool ptime_f(struct exttm *dst, const char *str, off_t &off_inout, ssize_t len)
{
    PTIME_CONSUME(6, {
        for (int lpc = 0; lpc < 6; lpc++) {
            if (str[off_inout + lpc] < '0' || str[off_inout + lpc] > '9') {
                return false;
            }
        }
        dst->et_nsec = (
                (str[off_inout + 0] - '0') * 100000 +
                (str[off_inout + 1] - '0') *  10000 +
                (str[off_inout + 2] - '0') *   1000 +
                (str[off_inout + 3] - '0') *    100 +
                (str[off_inout + 4] - '0') *     10 +
                (str[off_inout + 5] - '0') *      1) * 1000;
    });

    return true;
}

inline bool ptime_F(struct exttm *dst, const char *str, off_t &off_inout, ssize_t len)
{
    PTIME_CONSUME(3, {
        dst->et_nsec = (
                        (str[off_inout + 0] - '0') * 100 +
                        (str[off_inout + 1] - '0') *  10 +
                        (str[off_inout + 2] - '0') *   1) * 1000 * 1000;
    });

    return true;
}

inline bool ptime_char(char val, const char *str, off_t &off_inout, ssize_t len)
{
    PTIME_CONSUME(1, {
        if (str[off_inout] != val) {
            return false;
        }
    });

    return true;
}

typedef bool (*ptime_func)(struct exttm *dst, const char *str, off_t &off, ssize_t len);

bool ptime_fmt(const char *fmt, struct exttm *dst, const char *str, off_t &off, ssize_t len);

struct ptime_fmt {
    const char *pf_fmt;
    ptime_func pf_func;
};

extern struct ptime_fmt PTIMEC_FORMATS[];

extern const char *PTIMEC_FORMAT_STR[];

#endif
