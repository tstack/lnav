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
        RTT_THE,
        RTT_AGO,
        RTT_LATER,
        RTT_BEFORE,
        RTT_AFTER,
        RTT_NOW,
        RTT_HERE,
        RTT_NEXT,
        RTT_PREVIOUS,

        RTT__MAX
    };

    enum rt_field_type {
        RTF_MICROSECONDS,
        RTF_SECONDS,
        RTF_MINUTES,
        RTF_HOURS,
        RTF_DAYS,
        RTF_MONTHS,
        RTF_YEARS,

        RTF__MAX
    };

    relative_time() {
        this->clear();
    };

    void clear() {
        memset(this->rt_field, 0, sizeof(this->rt_field));
        this->rt_next = false;
        this->rt_previous = false;
        this->rt_absolute_field_end = 0;
    };

    void negate() {
        if (this->is_absolute()) {
            if (this->rt_next) {
                this->rt_next = false;
                this->rt_previous = true;
            } else if (this->rt_previous) {
                this->rt_next = true;
                this->rt_previous = false;
            }
            return;
        }
        for (int lpc = 0; lpc < RTF__MAX; lpc++) {
            if (this->rt_field[lpc].value != 0) {
                this->rt_field[lpc].value = -this->rt_field[lpc].value;
            }
        }
    };

    bool is_negative() const {
        if (this->rt_previous) {
            return true;
        }
        for (auto rtf : this->rt_field) {
            if (rtf.value < 0) {
                return true;
            }
        }
        return false;
    };

    bool is_absolute() const {
        return this->rt_absolute_field_end > 0;
    };

    bool is_absolute(rt_field_type rft) {
        return rft < this->rt_absolute_field_end;
    };

    bool is_relative() const {
        return !this->is_absolute() || this->rt_next || this->rt_previous;
    }

    bool empty() const {
        for (auto rtf : this->rt_field) {
            if (rtf.is_set) {
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

    struct exttm add_now() {
        struct exttm tm;
        time_t now;

        time(&now);
        tm.et_tm = *gmtime(&now);
        this->add(tm);

        return tm;
    };

    struct exttm add(const struct timeval &tv) {
        struct exttm tm;

        tm.et_tm = *gmtime(&tv.tv_sec);
        tm.et_nsec = tv.tv_usec * 1000;
        this->add(tm);

        return tm;
    }

    void add(struct exttm &tm) {
        if (this->rt_field[RTF_MICROSECONDS].is_set && this->is_absolute(RTF_MICROSECONDS)) {
            tm.et_nsec = this->rt_field[RTF_MICROSECONDS].value * 1000;
        }
        else {
            tm.et_nsec += this->rt_field[RTF_MICROSECONDS].value * 1000;
        }
        if (this->rt_field[RTF_SECONDS].is_set && this->is_absolute(RTF_SECONDS)) {
            if (this->rt_next &&
                this->rt_field[RTF_SECONDS].value <= tm.et_tm.tm_sec) {
                tm.et_tm.tm_min += 1;
            }
            if (this->rt_previous &&
                this->rt_field[RTF_SECONDS].value >= tm.et_tm.tm_sec) {
                tm.et_tm.tm_min -= 1;
            }
            tm.et_tm.tm_sec = this->rt_field[RTF_SECONDS].value;
        }
        else {
            tm.et_tm.tm_sec += this->rt_field[RTF_SECONDS].value;
        }
        if (this->rt_field[RTF_MINUTES].is_set && this->is_absolute(RTF_MINUTES)) {
            if (this->rt_next &&
                this->rt_field[RTF_MINUTES].value <= tm.et_tm.tm_min) {
                tm.et_tm.tm_hour += 1;
            }
            if (this->rt_previous && (this->rt_field[RTF_MINUTES].value == 0 ||
                (this->rt_field[RTF_MINUTES].value >= tm.et_tm.tm_min))) {
                tm.et_tm.tm_hour -= 1;
            }
            tm.et_tm.tm_min = this->rt_field[RTF_MINUTES].value;
        }
        else {
            tm.et_tm.tm_min += this->rt_field[RTF_MINUTES].value;
        }
        if (this->rt_field[RTF_HOURS].is_set && this->is_absolute(RTF_HOURS)) {
            if (this->rt_next &&
                this->rt_field[RTF_HOURS].value <= tm.et_tm.tm_hour) {
                tm.et_tm.tm_mday += 1;
            }
            if (this->rt_previous &&
                this->rt_field[RTF_HOURS].value >= tm.et_tm.tm_hour) {
                tm.et_tm.tm_mday -= 1;
            }
            tm.et_tm.tm_hour = this->rt_field[RTF_HOURS].value;
        }
        else {
            tm.et_tm.tm_hour += this->rt_field[RTF_HOURS].value;
        }
        if (this->rt_field[RTF_DAYS].is_set && this->is_absolute(RTF_DAYS)) {
            if (this->rt_next &&
                this->rt_field[RTF_DAYS].value <= tm.et_tm.tm_mday) {
                tm.et_tm.tm_mon += 1;
            }
            if (this->rt_previous &&
                this->rt_field[RTF_DAYS].value >= tm.et_tm.tm_mday) {
                tm.et_tm.tm_mon -= 1;
            }
            tm.et_tm.tm_mday = this->rt_field[RTF_DAYS].value;
        }
        else {
            tm.et_tm.tm_mday += this->rt_field[RTF_DAYS].value;
        }
        if (this->rt_field[RTF_MONTHS].is_set && this->is_absolute(RTF_MONTHS)) {
            if (this->rt_next &&
                this->rt_field[RTF_MONTHS].value <= tm.et_tm.tm_mon) {
                tm.et_tm.tm_year += 1;
            }
            if (this->rt_previous &&
                this->rt_field[RTF_MONTHS].value >= tm.et_tm.tm_mon) {
                tm.et_tm.tm_year -= 1;
            }
            tm.et_tm.tm_mon = this->rt_field[RTF_MONTHS].value;
        }
        else {
            tm.et_tm.tm_mon += this->rt_field[RTF_MONTHS].value;
        }
        if (this->rt_field[RTF_YEARS].is_set && this->is_absolute(RTF_YEARS)) {
            tm.et_tm.tm_year = this->rt_field[RTF_YEARS].value;
        }
        else {
            tm.et_tm.tm_year += this->rt_field[RTF_YEARS].value;
        }
    };

    int64_t to_microseconds() {
        int64_t retval;

        retval = this->rt_field[RTF_YEARS].value * 12;
        retval = (retval + this->rt_field[RTF_MONTHS].value) * 30;
        retval = (retval + this->rt_field[RTF_DAYS].value) * 24;
        retval = (retval + this->rt_field[RTF_HOURS].value) * 60;
        retval = (retval + this->rt_field[RTF_MINUTES].value) * 60;
        retval = (retval + this->rt_field[RTF_SECONDS].value) * 1000 * 1000;

        return retval;
    };

    void to_timeval(struct timeval &tv_out) {
        int64_t us = this->to_microseconds();

        tv_out.tv_sec = us / (1000 * 1000);
        tv_out.tv_usec = us % (1000 * 1000);
    };

    std::string to_string();

    void rollover();

    static const char FIELD_CHARS[RTF__MAX];

    struct _rt_field {
        _rt_field(int64_t value) : value(value), is_set(true) {
        };

        _rt_field() : value(0), is_set(false) {
        };

        int64_t value;
        bool is_set;
    } rt_field[RTF__MAX];

    bool rt_next;
    bool rt_previous;
    int rt_absolute_field_end;
};

size_t str2reltime(int64_t millis, std::string &value_out);

inline
size_t str2reltime(const struct timeval &tv, std::string &value_out) {
    return str2reltime(tv.tv_sec * 1000 + tv.tv_usec / 1000, value_out);
};

#endif //LNAV_RELATIVE_TIME_HH
