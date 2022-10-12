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

#include "time_util.hh"

#include "config.h"

namespace lnav {

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

}

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

    if (t->tm_mon < 2) {
        year--;
    }

    /* Find number of days since 1st March 1900 (in the Gregorian calendar). */

    days = year * 365 + year / 4 - year / 100 + (year / 100 + 3) / 4;
    days += dayoffset[t->tm_mon] + t->tm_mday - 1;
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

#define isleap(y) ((((y) % 4) == 0 && ((y) % 100) != 0) || ((y) % 400) == 0)

static const int year_lengths[2] = {365, 366};

const unsigned short int mon_yday[2][13] = {
    /* Normal years.  */
    {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365},
    /* Leap years.  */
    {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366}};

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
    long days, rem;
    lnav::time64_t lcltime;
    int y;
    int yleap;
    const unsigned short int* ip;

    /* base decision about std/dst time on current time */
    lcltime = tim;

    days = ((long) lcltime) / SECSPERDAY;
    rem = ((long) lcltime) % SECSPERDAY;
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
    y = EPOCH_YEAR;
    if (days >= 0) {
        for (;;) {
            yleap = isleap(y);
            if (days < year_lengths[yleap])
                break;
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

struct timeval
exttm::to_timeval() const
{
    struct timeval retval;

    retval.tv_sec = tm2sec(&this->et_tm);
    retval.tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::nanoseconds(this->et_nsec))
                         .count();

    return retval;
}
