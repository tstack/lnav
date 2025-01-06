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
 *
 * @file time_util.cc
 */

#include <chrono>
#include <map>

#include "time_util.hh"

#include <date/ptz.h>

#include "config.h"
#include "lnav_log.hh"

namespace lnav {

#define isleap(y) ((((y) % 4) == 0 && ((y) % 100) != 0) || ((y) % 400) == 0)

ssize_t
strftime_rfc3339(
    char* buffer, size_t buffer_size, lnav::time64_t tim, int millis, char sep)
{
    struct tm gmtm;
    int year, month, index = 0;

    secs2tm(tim, &gmtm);
    year = gmtm.tm_year + 1900;
    month = gmtm.tm_mon + 1;
    buffer[index++] = '0' + ((year / 1000) % 10);
    buffer[index++] = '0' + ((year / 100) % 10);
    buffer[index++] = '0' + ((year / 10) % 10);
    buffer[index++] = '0' + ((year / 1) % 10);
    buffer[index++] = '-';
    buffer[index++] = '0' + ((month / 10) % 10);
    buffer[index++] = '0' + ((month / 1) % 10);
    buffer[index++] = '-';
    buffer[index++] = '0' + ((gmtm.tm_mday / 10) % 10);
    buffer[index++] = '0' + ((gmtm.tm_mday / 1) % 10);
    buffer[index++] = sep;
    buffer[index++] = '0' + ((gmtm.tm_hour / 10) % 10);
    buffer[index++] = '0' + ((gmtm.tm_hour / 1) % 10);
    buffer[index++] = ':';
    buffer[index++] = '0' + ((gmtm.tm_min / 10) % 10);
    buffer[index++] = '0' + ((gmtm.tm_min / 1) % 10);
    buffer[index++] = ':';
    buffer[index++] = '0' + ((gmtm.tm_sec / 10) % 10);
    buffer[index++] = '0' + ((gmtm.tm_sec / 1) % 10);
    buffer[index++] = '.';
    buffer[index++] = '0' + ((millis / 100) % 10);
    buffer[index++] = '0' + ((millis / 10) % 10);
    buffer[index++] = '0' + ((millis / 1) % 10);
    buffer[index] = '\0';

    return index;
}

std::string
to_rfc3339_string(time64_t tim, int millis, char sep)
{
    char buf[64];

    strftime_rfc3339(buf, sizeof(buf), tim, millis, sep);

    return buf;
}

static std::optional<Posix::time_zone>
get_posix_zone(const char* name)
{
    if (name == nullptr) {
        return std::nullopt;
    }

    try {
        return date::zoned_traits<Posix::time_zone>::locate_zone(name);
    } catch (const std::runtime_error& e) {
        log_error("invalid TZ value: %s -- %s", name, e.what());
        return std::nullopt;
    }
}

static std::optional<const date::time_zone*>
get_date_zone(const char* name)
{
    if (name == nullptr) {
        try {
            return date::current_zone();
        } catch (const std::runtime_error& e) {
            return std::nullopt;
        }
    }

    try {
        return date::locate_zone(name);
    } catch (const std::runtime_error& e) {
        log_error("invalid TZ value: %s -- %s", name, e.what());
        try {
            return date::current_zone();
        } catch (const std::runtime_error& e) {
            return std::nullopt;
        }
    }
}

date::sys_seconds
to_sys_time(date::local_seconds secs)
{
    static const auto* TZ = getenv("TZ");
    static const auto TZ_POSIX_ZONE = get_posix_zone(TZ);
    if (TZ_POSIX_ZONE) {
        return TZ_POSIX_ZONE.value().to_sys(secs);
    }

    static const auto TZ_DATE_ZONE = get_date_zone(TZ);

    if (TZ_DATE_ZONE) {
        auto inf = TZ_DATE_ZONE.value()->get_info(secs);

        return TZ_DATE_ZONE.value()->to_sys(secs);
    }

    static const auto TZ_POSIX_UTC = get_posix_zone("UTC0");

    return TZ_POSIX_UTC.value().to_sys(secs);
}

date::local_seconds
to_local_time(date::sys_seconds secs)
{
    static const auto* TZ = getenv("TZ");
    static const auto TZ_POSIX_ZONE = get_posix_zone(TZ);

    if (TZ_POSIX_ZONE) {
        return TZ_POSIX_ZONE.value().to_local(secs);
    }

    static const auto TZ_DATE_ZONE = get_date_zone(TZ);

    if (TZ_DATE_ZONE) {
        return TZ_DATE_ZONE.value()->to_local(secs);
    }

    static const auto TZ_POSIX_UTC = get_posix_zone("UTC0");

    return TZ_POSIX_UTC.value().to_local(secs);
}

date::sys_info
sys_time_to_info(date::sys_seconds secs)
{
    static const auto* TZ = getenv("TZ");
    static const auto TZ_POSIX_ZONE = get_posix_zone(TZ);

    if (TZ_POSIX_ZONE) {
        return TZ_POSIX_ZONE.value().get_info(secs);
    }

    static const auto TZ_DATE_ZONE = get_date_zone(TZ);
    if (TZ_DATE_ZONE) {
        return TZ_DATE_ZONE.value()->get_info(secs);
    }

    static const auto TZ_POSIX_UTC = get_posix_zone("UTC0");

    return TZ_POSIX_UTC.value().get_info(secs);
}

date::local_info
local_time_to_info(date::local_seconds secs)
{
    static const auto* TZ = getenv("TZ");
    static const auto TZ_POSIX_ZONE = get_posix_zone(TZ);

    if (TZ_POSIX_ZONE) {
        return TZ_POSIX_ZONE.value().get_info(secs);
    }

    static const auto TZ_DATE_ZONE = get_date_zone(TZ);
    if (TZ_DATE_ZONE) {
        return TZ_DATE_ZONE.value()->get_info(secs);
    }

    static const auto TZ_POSIX_UTC = get_posix_zone("UTC0");

    return TZ_POSIX_UTC.value().get_info(secs);
}

}  // namespace lnav

static time_t BAD_DATE = -1;

time_t
tm2sec(const struct tm* t)
{
    int year;
    time_t days, secs;
    const int dayoffset[12]
        = {306, 337, 0, 31, 61, 92, 122, 153, 184, 214, 245, 275};

    year = t->tm_year;

    if (year < 70) {
        return BAD_DATE;
    }
    if ((sizeof(time_t) <= 4) && (year >= 138)) {
        year = 137;
    }

    /* shift new year to 1st March in order to make leap year calc easy */

    if (t->tm_yday >= 1) {
        if (t->tm_yday <= 59) {
            year--;
        }
    } else if (t->tm_mon < 2) {
        year--;
    }

    /* Find number of days since 1st March 1900 (in the Gregorian calendar). */
    days = year * 365 + year / 4 - year / 100 + (year / 100 + 3) / 4;
    if (t->tm_yday >= 1) {
        int yday_diff = 0;
        if (t->tm_yday > 59) {
            yday_diff = t->tm_yday - 59;
            if (isleap(year)) {
                yday_diff -= 1;
            }
        } else {
            yday_diff = 306 + t->tm_yday;
        }
        days += yday_diff;
    } else {
        days += dayoffset[t->tm_mon] + t->tm_mday - 1;
    }
    days -= 25508; /* 1 jan 1970 is 25508 days since 1 mar 1900 */

    secs = ((days * 24 + t->tm_hour) * 60 + t->tm_min) * 60 + t->tm_sec;

    if (secs < 0) {
        return BAD_DATE;
    } /* must have overflowed */
    else
    {
#ifdef HAVE_STRUCT_TM_TM_ZONE
        if (t->tm_zone) {
            secs -= t->tm_gmtoff;
        }
#endif
        return secs;
    } /* must be a valid time */
}

static const int SECSPERMIN = 60;
static const int SECSPERHOUR = 60 * SECSPERMIN;
static const int SECSPERDAY = 24 * SECSPERHOUR;
static const int YEAR_BASE = 1900;
static const int EPOCH_WDAY = 4;
static const int DAYSPERWEEK = 7;
static const int EPOCH_YEAR = 1970;

static const int year_lengths[2] = {365, 366};

const unsigned short int mon_yday[2][13] = {
    /* Normal years.  */
    {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365},
    /* Leap years.  */
    {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366},
};

void
secs2wday(const struct timeval& tv, struct tm* res)
{
    long days, rem;
    time_t lcltime;

    /* base decision about std/dst time on current time */
    lcltime = tv.tv_sec;

    days = ((long) lcltime) / SECSPERDAY;
    rem = ((long) lcltime) % SECSPERDAY;
    while (rem < 0) {
        rem += SECSPERDAY;
        --days;
    }

    /* compute day of week */
    if ((res->tm_wday = ((EPOCH_WDAY + days) % DAYSPERWEEK)) < 0) {
        res->tm_wday += DAYSPERWEEK;
    }
}

struct tm*
secs2tm(lnav::time64_t tim, struct tm* res)
{
    int yleap;
    const unsigned short int* ip;

    /* base decision about std/dst time on current time */
    lnav::time64_t lcltime = tim;

    long days = ((long) lcltime) / SECSPERDAY;
    long rem = ((long) lcltime) % SECSPERDAY;
    while (rem < 0) {
        rem += SECSPERDAY;
        --days;
    }

    /* compute hour, min, and sec */
    res->tm_hour = (int) (rem / SECSPERHOUR);
    rem %= SECSPERHOUR;
    res->tm_min = (int) (rem / SECSPERMIN);
    res->tm_sec = (int) (rem % SECSPERMIN);

    /* compute day of week */
    if ((res->tm_wday = ((EPOCH_WDAY + days) % DAYSPERWEEK)) < 0)
        res->tm_wday += DAYSPERWEEK;

    /* compute year & day of year */
    int y = EPOCH_YEAR;
    if (days >= 0) {
        for (;;) {
            yleap = isleap(y);
            if (days < year_lengths[yleap]) {
                break;
            }
            y++;
            days -= year_lengths[yleap];
        }
    } else {
        do {
            --y;
            yleap = isleap(y);
            days += year_lengths[yleap];
        } while (days < 0);
    }

    res->tm_year = y - YEAR_BASE;
    res->tm_yday = days;
    ip = mon_yday[isleap(y)];
    for (y = 11; days < (long int) ip[y]; --y)
        continue;
    days -= ip[y];
    res->tm_mon = y;
    res->tm_mday = days + 1;

    res->tm_isdst = 0;

    return (res);
}

exttm
exttm::from_tv(const timeval& tv)
{
    auto retval = exttm{};

    gmtime_r(&tv.tv_sec, &retval.et_tm);
    retval.et_nsec = tv.tv_usec * 1000;

    return retval;
}

struct timeval
exttm::to_timeval() const
{
    struct timeval retval;

    retval.tv_sec = tm2sec(&this->et_tm);
    retval.tv_sec -= this->et_gmtoff;
    retval.tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::nanoseconds(this->et_nsec))
                         .count();

    return retval;
}

time_range&
time_range::operator|=(const time_range& rhs)
{
    if (rhs.tr_begin < this->tr_begin) {
        this->tr_begin = rhs.tr_begin;
    }
    if (this->tr_end < rhs.tr_end) {
        this->tr_end = rhs.tr_end;
    }

    return *this;
}

void
time_range::extend_to(const timeval& tv)
{
    if (tv < this->tr_begin) {
        // logs aren't always in time-order
        this->tr_begin = tv;
    } else if (this->tr_end < tv) {
        this->tr_end = tv;
    }
}

std::chrono::milliseconds
time_range::duration() const
{
    auto diff = this->tr_end - this->tr_begin;

    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::seconds(diff.tv_sec))
        + std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::microseconds(diff.tv_usec));
}

bool
time_range::contains_inclusive(const timeval& tv) const
{
    return (this->tr_begin <= tv) && (tv <= this->tr_end);
}
