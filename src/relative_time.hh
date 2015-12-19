/**
 * Copyright (c) 2015, Timothy Stack
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

#ifndef LNAV_RELATIVE_TIME_HH
#define LNAV_RELATIVE_TIME_HH

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include <string>

#include "ptimec.hh"

class relative_time {
public:
    enum token_t {
        RTT_INVALID = -1,

        RTT_WHITE,
        RTT_AM,
        RTT_PM,
        RTT_A,
        RTT_AN,
        RTT_AT,
        RTT_TIME,
        RTT_NUMBER,
        RTT_MICROS,
        RTT_MILLIS,
        RTT_SECONDS,
        RTT_MINUTES,
        RTT_HOURS,
        RTT_DAYS,
        RTT_WEEKS,
        RTT_MONTHS,
        RTT_YEARS,
        RTT_TODAY,
        RTT_YESTERDAY,
        RTT_TOMORROW,
        RTT_NOON,
        RTT_AND,
        RTT_AGO,
        RTT_LATER,
        RTT_BEFORE,
        RTT_NOW,
        RTT_HERE,

        RTT__MAX
    };

    relative_time() {
        this->clear();
    };

    void clear() {
        memset(this->rt_field, 0, sizeof(this->rt_field));
        memset(this->rt_is_absolute, 0, sizeof(this->rt_is_absolute));
    };

    void negate() {
        for (int lpc = 0; lpc < RTF__MAX; lpc++) {
            if (!this->rt_is_absolute[lpc] && this->rt_field[lpc] != 0) {
                this->rt_field[lpc] = -this->rt_field[lpc];
            }
        }
    };

    bool is_negative() const {
        for (int lpc = 0; lpc < RTF__MAX; lpc++) {
            if (this->rt_field[lpc] < 0) {
                return true;
            }
        }
        return false;
    };

    bool is_absolute() const {
        for (int lpc = 0; lpc < RTF__MAX; lpc++) {
            if (this->rt_is_absolute[lpc]) {
                return true;
            }
        }
        return false;
    };

    bool empty() const {
        for (int lpc = 0; lpc < RTF__MAX; lpc++) {
            if (this->rt_field[lpc]) {
                return false;
            }
        }
        return true;
    };

    struct parse_error {
        int pe_column;
        std::string pe_msg;
    };

    bool parse(const char *str, size_t len, struct parse_error &pe_out);

    bool parse(const std::string &str, struct parse_error &pe_out) {
        return this->parse(str.c_str(), str.length(), pe_out);
    }

    void add(struct exttm &tm) {
        if (this->rt_is_absolute[RTF_MICROSECONDS]) {
            tm.et_nsec = this->rt_field[RTF_MICROSECONDS] * 1000;
        }
        else {
            tm.et_nsec += this->rt_field[RTF_MICROSECONDS] * 1000;
        }
        if (this->rt_is_absolute[RTF_SECONDS]) {
            tm.et_tm.tm_sec = this->rt_field[RTF_SECONDS];
        }
        else {
            tm.et_tm.tm_sec += this->rt_field[RTF_SECONDS];
        }
        if (this->rt_is_absolute[RTF_MINUTES]) {
            tm.et_tm.tm_min = this->rt_field[RTF_MINUTES];
        }
        else {
            tm.et_tm.tm_min += this->rt_field[RTF_MINUTES];
        }
        if (this->rt_is_absolute[RTF_HOURS]) {
            tm.et_tm.tm_hour = this->rt_field[RTF_HOURS];
        }
        else {
            tm.et_tm.tm_hour += this->rt_field[RTF_HOURS];
        }
        if (this->rt_is_absolute[RTF_DAYS]) {
            tm.et_tm.tm_mday = this->rt_field[RTF_DAYS];
        }
        else {
            tm.et_tm.tm_mday += this->rt_field[RTF_DAYS];
        }
        if (this->rt_is_absolute[RTF_MONTHS]) {
            tm.et_tm.tm_mon = this->rt_field[RTF_MONTHS];
        }
        else {
            tm.et_tm.tm_mon += this->rt_field[RTF_MONTHS];
        }
        if (this->rt_is_absolute[RTF_YEARS]) {
            tm.et_tm.tm_year = this->rt_field[RTF_YEARS];
        }
        else {
            tm.et_tm.tm_year += this->rt_field[RTF_YEARS];
        }
    };

    int64_t to_microseconds() {
        int64_t retval;

        retval = this->rt_field[RTF_YEARS] * 12;
        retval = (retval + this->rt_field[RTF_MONTHS]) * 30;
        retval = (retval + this->rt_field[RTF_DAYS]) * 24;
        retval = (retval + this->rt_field[RTF_HOURS]) * 60;
        retval = (retval + this->rt_field[RTF_MINUTES]) * 60;
        retval = (retval + this->rt_field[RTF_SECONDS]) * 1000 * 1000;

        return retval;
    };

    void to_timeval(struct timeval &tv_out) {
        int64_t us = this->to_microseconds();

        tv_out.tv_sec = us / (1000 * 1000);
        tv_out.tv_usec = us % (1000 * 1000);
    };

    std::string to_string() {
        char dst[128];

        snprintf(dst, sizeof(dst),
                 "%" PRId64 "%c%" PRId64 "%c%" PRId64 "%c%" PRId64 "%c%" PRId64 "%c%" PRId64 "%c%" PRId64 "%c",
                 this->rt_field[RTF_YEARS],
                 this->rt_is_absolute[RTF_YEARS] ? 'Y' : 'y',
                 this->rt_field[RTF_MONTHS],
                 this->rt_is_absolute[RTF_MONTHS] ? 'M' : 'm',
                 this->rt_field[RTF_DAYS],
                 this->rt_is_absolute[RTF_DAYS] ? 'D' : 'd',
                 this->rt_field[RTF_HOURS],
                 this->rt_is_absolute[RTF_HOURS] ? 'H' : 'h',
                 this->rt_field[RTF_MINUTES],
                 this->rt_is_absolute[RTF_MINUTES] ? 'M' : 'm',
                 this->rt_field[RTF_SECONDS],
                 this->rt_is_absolute[RTF_SECONDS] ? 'S' : 's',
                 this->rt_field[RTF_MICROSECONDS],
                 this->rt_is_absolute[RTF_MICROSECONDS] ? 'U' : 'u');
        return dst;
    };

    void rollover();

    enum {
        RTF_MICROSECONDS,
        RTF_SECONDS,
        RTF_MINUTES,
        RTF_HOURS,
        RTF_DAYS,
        RTF_MONTHS,
        RTF_YEARS,

        RTF__MAX
    };

    int64_t rt_field[RTF__MAX];
    bool rt_is_absolute[RTF__MAX];
};

#endif //LNAV_RELATIVE_TIME_HH
