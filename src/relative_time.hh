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

#include <sys/time.h>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include <set>
#include <array>
#include <chrono>
#include <string>

#include "ptimec.hh"
#include "base/result.h"

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

        RTT_SUNDAY,
        RTT_MONDAY,
        RTT_TUESDAY,
        RTT_WEDNESDAY,
        RTT_THURSDAY,
        RTT_FRIDAY,
        RTT_SATURDAY,

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

    struct parse_error {
        int pe_column;
        std::string pe_msg;
    };

    static Result<relative_time, parse_error>
    from_str(const char *str, size_t len);

    static Result<relative_time, parse_error>
    from_str(const std::string &str) {
        return from_str(str.c_str(), str.length());
    }

    static relative_time from_timeval(const struct timeval& tv);

    static relative_time from_usecs(std::chrono::microseconds usecs);

    relative_time() {
        this->clear();
    };

    void clear() {
        this->rt_field.fill({});
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
        return !this->rt_included_days.empty() || this->rt_absolute_field_end > 0;
    };

    bool is_absolute(rt_field_type rft) const {
        return rft < this->rt_absolute_field_end;
    };

    bool is_relative() const {
        return !this->is_absolute() || this->rt_next || this->rt_previous;
    }

    bool empty() const {
        if (!this->rt_included_days.empty()) {
            return false;
        }
        for (auto rtf : this->rt_field) {
            if (rtf.is_set) {
                return false;
            }
        }
        return true;
    };

    struct exttm adjust_now() const {
        struct exttm tm;
        time_t now;

        time(&now);
        memset(&tm, 0, sizeof(tm));
        tm.et_tm = *gmtime(&now);
        return this->adjust(tm);
    };

    struct exttm adjust(const struct timeval &tv) const {
        struct exttm tm;

        memset(&tm, 0, sizeof(tm));
        tm.et_tm = *gmtime(&tv.tv_sec);
        tm.et_nsec = tv.tv_usec * 1000;
        return this->adjust(tm);
    }

    struct exttm adjust(const struct exttm &tm) const;

    nonstd::optional<exttm> window_start(const struct exttm &tm) const;

    int64_t to_microseconds() const;

    void to_timeval(struct timeval &tv_out) const {
        int64_t us = this->to_microseconds();

        tv_out.tv_sec = us / (1000 * 1000);
        tv_out.tv_usec = us % (1000 * 1000);
    };

    struct timeval to_timeval() const {
        int64_t us = this->to_microseconds();
        struct timeval retval;

        retval.tv_sec = us / (1000 * 1000);
        retval.tv_usec = us % (1000 * 1000);
        return retval;
    };

    std::string to_string() const;

    void rollover();

    static const char FIELD_CHARS[RTF__MAX];

    struct _rt_field {
        _rt_field(int64_t value) : value(value), is_set(true) {
        };

        _rt_field() : value(0), is_set(false) {
        };

        void clear() {
            this->value = 0;
            this->is_set = false;
        }

        int64_t value;
        bool is_set;
    };

    std::array<_rt_field, RTF__MAX> rt_field;
    std::set<token_t> rt_included_days;
    std::chrono::microseconds rt_duration{0};

    bool rt_next;
    bool rt_previous;
    int rt_absolute_field_end;
};

#endif //LNAV_RELATIVE_TIME_HH
