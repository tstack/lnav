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

#include "config.h"

#include <sys/time.h>

#include "intern_string.hh"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.hh"
#include "relative_time.hh"

using namespace std;

static struct {
    const char *reltime;
    const char *expected;
} TEST_DATA[] = {
    // { "10 minutes after the next hour", "next 0:10" },
    { "next day", "next day 0:00" },
    { "next month", "next month day 0 0:00" },
    { "next year", "next year month 0 day 0 0:00" },
    { "last hour", "last 0:00" },
    { "next 10 minutes after the hour", "next 0:10" },
    { "1h50m", "1h50m" },
    { "next hour", "next 0:00" },
    { "a minute ago", "-1m" },
    { "1m ago", "-1m" },
    { "a min ago", "-1m" },
    { "a m ago", "-1m" },
    { "+1 minute ago", "-1m" },
    { "-1 minute ago", "-1m" },
    { "-1 minute", "-1m" },
    { "10 minutes after the hour", "0:10" },
    { "1:40", "1:40" },
    { "01:30", "1:30" },
    { "1pm", "13:00" },
    { "12pm", "12:00" },
    { "00:27:18.567", "0:27:18.567" },

    { NULL, NULL }
};

static struct {
    const char *reltime;
    const char *expected_error;
} BAD_TEST_DATA[] = {
        { "ago", "" },
        { "minute", "" },
        { "1 2", "" },

        { NULL, NULL }
};

TEST_CASE("reltime")
{
    time_t base_time = 1317913200;
    struct exttm base_tm;
    base_tm.et_tm = *gmtime(&base_time);
    struct relative_time::parse_error pe;
    struct timeval tv;
    struct exttm tm, tm2;
    time_t new_time;

    relative_time rt;

    for (int lpc = 0; TEST_DATA[lpc].reltime; lpc++) {
        bool rc;

        rt.clear();
        rc = rt.parse(TEST_DATA[lpc].reltime, pe);
        CHECK_MESSAGE(rc, TEST_DATA[lpc].reltime);
        CHECK(std::string(TEST_DATA[lpc].expected) == rt.to_string());
    }

    for (int lpc = 0; BAD_TEST_DATA[lpc].reltime; lpc++) {
        bool rc;
        rt.clear();
        rc = rt.parse(BAD_TEST_DATA[lpc].reltime, pe);
        CHECK(!rc);
    }

    rt.clear();
    rt.parse("", pe);
    CHECK(rt.empty());

    rt.clear();
    rt.parse("a minute ago", pe);
    CHECK(rt.rt_field[relative_time::RTF_MINUTES].value == -1);

    rt.clear();
    rt.parse("5 milliseconds", pe);
    CHECK(rt.rt_field[relative_time::RTF_MICROSECONDS].value == 5 * 1000);

    rt.clear();
    rt.parse("5000 ms ago", pe);
    CHECK(rt.rt_field[relative_time::RTF_SECONDS].value == -5);

    rt.clear();
    rt.parse("5 hours 20 minutes ago", pe);

    CHECK(rt.rt_field[relative_time::RTF_HOURS].value == -5);
    CHECK(rt.rt_field[relative_time::RTF_MINUTES].value == -20);

    rt.clear();
    rt.parse("5 hours and 20 minutes ago", pe);

    CHECK(rt.rt_field[relative_time::RTF_HOURS].value == -5);
    CHECK(rt.rt_field[relative_time::RTF_MINUTES].value == -20);

    rt.clear();
    rt.parse("1:23", pe);

    CHECK(rt.rt_field[relative_time::RTF_HOURS].value == 1);
    CHECK(rt.rt_field[relative_time::RTF_MINUTES].value == 23);
    CHECK(rt.rt_is_absolute);

    rt.clear();
    rt.parse("1:23:45", pe);

    CHECK(rt.rt_field[relative_time::RTF_HOURS].value == 1);
    CHECK(rt.rt_field[relative_time::RTF_MINUTES].value == 23);
    CHECK(rt.rt_field[relative_time::RTF_SECONDS].value == 45);
    CHECK(rt.rt_is_absolute);

    tm = base_tm;
    rt.add(tm);

    new_time = timegm(&tm.et_tm);
    tm.et_tm = *gmtime(&new_time);
    CHECK(tm.et_tm.tm_hour == 1);
    CHECK(tm.et_tm.tm_min == 23);

    rt.clear();
    rt.parse("5 minutes ago", pe);

    tm = base_tm;
    rt.add(tm);

    new_time = timegm(&tm.et_tm);

    CHECK(new_time == (base_time - (5 * 60)));

    rt.clear();
    rt.parse("today at 4pm", pe);
    memset(&tm, 0, sizeof(tm));
    memset(&tm2, 0, sizeof(tm2));
    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &tm2.et_tm);
    tm2.et_tm.tm_hour = 16;
    tm2.et_tm.tm_min = 0;
    tm2.et_tm.tm_sec = 0;
    rt.add(tm);
    tm.et_tm.tm_yday = 0;
    tm2.et_tm.tm_yday = 0;
    tm.et_tm.tm_wday = 0;
    tm2.et_tm.tm_wday = 0;
#ifdef HAVE_STRUCT_TM_TM_ZONE
    tm2.et_tm.tm_gmtoff = 0;
    tm2.et_tm.tm_zone = NULL;
#endif
    CHECK(tm.et_tm.tm_year == tm2.et_tm.tm_year);
    CHECK(tm.et_tm.tm_mon == tm2.et_tm.tm_mon);
    CHECK(tm.et_tm.tm_mday == tm2.et_tm.tm_mday);
    CHECK(tm.et_tm.tm_hour == tm2.et_tm.tm_hour);
    CHECK(tm.et_tm.tm_min == tm2.et_tm.tm_min);
    CHECK(tm.et_tm.tm_sec == tm2.et_tm.tm_sec);

    rt.clear();
    rt.parse("yesterday at 4pm", pe);
    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &tm2.et_tm);
    tm2.et_tm.tm_mday -= 1;
    tm2.et_tm.tm_hour = 16;
    tm2.et_tm.tm_min = 0;
    tm2.et_tm.tm_sec = 0;
    rt.add(tm);
    tm.et_tm.tm_yday = 0;
    tm2.et_tm.tm_yday = 0;
    tm.et_tm.tm_wday = 0;
    tm2.et_tm.tm_wday = 0;
#ifdef HAVE_STRUCT_TM_TM_ZONE
    tm2.et_tm.tm_gmtoff = 0;
    tm2.et_tm.tm_zone = NULL;
#endif
    CHECK(tm.et_tm.tm_year == tm2.et_tm.tm_year);
    CHECK(tm.et_tm.tm_mon == tm2.et_tm.tm_mon);
    CHECK(tm.et_tm.tm_mday == tm2.et_tm.tm_mday);
    CHECK(tm.et_tm.tm_hour == tm2.et_tm.tm_hour);
    CHECK(tm.et_tm.tm_min == tm2.et_tm.tm_min);
    CHECK(tm.et_tm.tm_sec == tm2.et_tm.tm_sec);
}
