/**
 * Copyright (c) 2020, Timothy Stack
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

#ifndef lnav_time_util_hh
#define lnav_time_util_hh

#include <chrono>

#include <inttypes.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>

#include "config.h"
#include "date/date.h"
#include "date/tz.h"

namespace lnav {

using time64_t = uint64_t;

ssize_t strftime_rfc3339(
    char* buffer, size_t buffer_size, time64_t tim, int millis, char sep = ' ');

std::string to_rfc3339_string(time64_t tim, int millis, char sep = ' ');

inline std::string
to_rfc3339_string(struct timeval tv, char sep = ' ')
{
    return to_rfc3339_string(tv.tv_sec, tv.tv_usec / 1000, sep);
}

date::sys_info sys_time_to_info(date::sys_seconds secs);

date::local_info local_time_to_info(date::local_seconds secs);

date::sys_seconds to_sys_time(date::local_seconds secs);

date::local_seconds to_local_time(date::sys_seconds secs);

}  // namespace lnav

struct tm* secs2tm(lnav::time64_t tim, struct tm* res);
/**
 * Convert the time stored in a 'tm' struct into epoch time.
 *
 * @param t The 'tm' structure to convert to epoch time.
 * @return The given time in seconds since the epoch.
 */
time_t tm2sec(const struct tm* t);
void secs2wday(const struct timeval& tv, struct tm* res);

inline time_t
convert_log_time_to_local(time_t value)
{
    struct tm tm;

    localtime_r(&value, &tm);
#ifdef HAVE_STRUCT_TM_TM_ZONE
    tm.tm_zone = NULL;
#endif
    tm.tm_isdst = 0;
    return tm2sec(&tm);
}

constexpr lnav::time64_t MAX_TIME_T = 4000000000LL;

enum exttm_bits_t {
    ETB_YEAR_SET,
    ETB_MONTH_SET,
    ETB_DAY_SET,
    ETB_YDAY_SET,
    ETB_HOUR_SET,
    ETB_MINUTE_SET,
    ETB_SECOND_SET,
    ETB_MACHINE_ORIENTED,
    ETB_EPOCH_TIME,
    ETB_SUB_NOT_IN_FORMAT,
    ETB_MILLIS_SET,
    ETB_MICROS_SET,
    ETB_NANOS_SET,
    ETB_ZONE_SET,
    ETB_Z_FOR_UTC,
    ETB_Z_COLON,
    ETB_Z_IS_UTC,
    ETB_Z_IS_GMT,
};

enum exttm_flags_t {
    ETF_YEAR_SET = (1UL << ETB_YEAR_SET),
    ETF_MONTH_SET = (1UL << ETB_MONTH_SET),
    ETF_DAY_SET = (1UL << ETB_DAY_SET),
    ETF_YDAY_SET = (1UL << ETB_YDAY_SET),
    ETF_HOUR_SET = (1UL << ETB_HOUR_SET),
    ETF_MINUTE_SET = (1UL << ETB_MINUTE_SET),
    ETF_SECOND_SET = (1UL << ETB_SECOND_SET),
    ETF_MACHINE_ORIENTED = (1UL << ETB_MACHINE_ORIENTED),
    ETF_EPOCH_TIME = (1UL << ETB_EPOCH_TIME),
    ETF_SUB_NOT_IN_FORMAT = (1UL << ETB_SUB_NOT_IN_FORMAT),
    ETF_MILLIS_SET = (1UL << ETB_MILLIS_SET),
    ETF_MICROS_SET = (1UL << ETB_MICROS_SET),
    ETF_NANOS_SET = (1UL << ETB_NANOS_SET),
    ETF_ZONE_SET = (1UL << ETB_ZONE_SET),
    ETF_Z_FOR_UTC = (1UL << ETB_Z_FOR_UTC),
    ETF_Z_COLON = (1UL << ETB_Z_COLON),
    ETF_Z_IS_UTC = (1UL << ETB_Z_IS_UTC),
    ETF_Z_IS_GMT = (1UL << ETB_Z_IS_GMT),
};

struct exttm {
    static exttm from_tv(const timeval& tv);

    struct tm et_tm {};
    int32_t et_nsec{0};
    unsigned int et_flags{0};
    long et_gmtoff{0};

    exttm() { memset(&this->et_tm, 0, sizeof(this->et_tm)); }

    bool operator==(const exttm& other) const
    {
        return memcmp(this, &other, sizeof(exttm)) == 0;
    }

    struct timeval to_timeval() const;
};

inline bool
operator<(const struct timeval& left, time_t right)
{
    return left.tv_sec < right;
}

inline bool
operator<(time_t left, const struct timeval& right)
{
    return left < right.tv_sec;
}

inline bool
operator<(const struct timeval& left, const struct timeval& right)
{
    return left.tv_sec < right.tv_sec
        || ((left.tv_sec == right.tv_sec) && (left.tv_usec < right.tv_usec));
}

inline bool
operator<=(const struct timeval& left, const struct timeval& right)
{
    return left.tv_sec < right.tv_sec
        || ((left.tv_sec == right.tv_sec) && (left.tv_usec <= right.tv_usec));
}

inline bool
operator!=(const struct timeval& left, const struct timeval& right)
{
    return left.tv_sec != right.tv_sec || left.tv_usec != right.tv_usec;
}

inline bool
operator==(const struct timeval& left, const struct timeval& right)
{
    return left.tv_sec == right.tv_sec && left.tv_usec == right.tv_usec;
}

inline struct timeval
operator-(const struct timeval& lhs, const struct timeval& rhs)
{
    struct timeval diff;

    timersub(&lhs, &rhs, &diff);
    return diff;
}

inline struct timeval
operator+(const struct timeval& lhs, const struct timeval& rhs)
{
    struct timeval retval;

    timeradd(&lhs, &rhs, &retval);
    return retval;
}

typedef int64_t mstime_t;

inline mstime_t
to_mstime(const timeval& tv)
{
    return tv.tv_sec * 1000ULL + tv.tv_usec / 1000ULL;
}

inline mstime_t
getmstime()
{
    struct timeval tv;

    gettimeofday(&tv, nullptr);

    return to_mstime(tv);
}

inline struct timeval
current_timeval()
{
    struct timeval retval;

    gettimeofday(&retval, nullptr);

    return retval;
}

inline struct timespec
current_timespec()
{
    struct timespec retval;

    clock_gettime(CLOCK_REALTIME, &retval);

    return retval;
}

inline time_t
day_num(time_t ti)
{
    return ti / (24 * 60 * 60);
}

inline time_t
hour_num(time_t ti)
{
    return ti / (60 * 60);
}

struct time_range {
    struct timeval tr_begin;
    struct timeval tr_end;

    bool valid() const { return this->tr_end.tv_sec == 0; }

    void invalidate()
    {
        this->tr_begin.tv_sec = INT_MAX;
        this->tr_begin.tv_usec = 0;
        this->tr_end.tv_sec = 0;
        this->tr_end.tv_usec = 0;
    }

    bool operator<(const time_range& rhs) const
    {
        return this->tr_begin < rhs.tr_begin;
    }

    time_range& operator|=(const time_range& rhs);

    bool contains_inclusive(const timeval& tv) const;

    void extend_to(const timeval& tv);
    std::chrono::milliseconds duration() const;
};

#endif
