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

#include "fmt/format.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.hh"
#include "relative_time.hh"

using namespace std;

static struct {
    const char *reltime{nullptr};
    const char *expected{nullptr};
    const char *expected_negate{nullptr};
} TEST_DATA[] = {
    // { "10 minutes after the next hour", "next 0:10" },
    { "0s", "0s", "0s" },
    { "next day", "next day 0:00", "last day 0:00" },
    { "next month", "next month day 0 0:00", "last month day 0 0:00" },
    { "next year", "next year month 0 day 0 0:00",
      "last year month 0 day 0 0:00" },
    { "previous hour", "last 0:00", "next 0:00" },
    { "next 10 minutes after the hour", "next 0:10", "last 0:10" },
    { "1h50m", "1h50m", "-1h-50m" },
    { "next hour", "next 0:00", "last 0:00" },
    { "a minute ago", "0:-1", "0:-1" },
    { "1m ago", "0:-1", "0:-1" },
    { "a min ago", "0:-1", "0:-1" },
    { "a m ago", "0:-1", "0:-1" },
    { "+1 minute ago", "0:-1", "0:-1" },
    { "-1 minute ago", "0:-1", "0:-1" },
    { "-1 minute", "-1m", "1m" },
    { "10 minutes after the hour", "0:10", "0:10" },
    { "1:40", "1:40", "1:40" },
    { "01:30", "1:30", "1:30" },
    { "1pm", "13:00", "13:00" },
    { "12pm", "12:00", "12:00" },
    { "00:27:18.567", "0:27:18.567", "0:27:18.567" },

    {}
};

static struct {
    const char *reltime;
    const char *expected_error;
} BAD_TEST_DATA[] = {
    { "10am am", "Time has already been set" },
    { "yesterday today", "Current time reference has already been used" },
    { "10am 10am", "Time has already been set" },
    { "ago", "Expecting a time unit" },
    { "minute", "Expecting a number before time unit" },
    { "1 2", "No time unit given for the previous number" },
    { "blah", "Unrecognized input" },
    { "before", "'before' requires a point in time (e.g. before 10am)" },
    { "after", "'after' requires a point in time (e.g. after 10am)" },
    { "before after", "Before/after ranges are not supported yet" },

    { nullptr, nullptr }
};

TEST_CASE("reltime")
{
    time_t base_time = 1317913200;
    struct exttm base_tm;
    base_tm.et_tm = *gmtime(&base_time);
    struct timeval tv;
    struct exttm tm, tm2;
    time_t new_time;

    {
        auto rt_res = relative_time::from_str("before 2014");

            CHECK(rt_res.isOk());
        auto rt = rt_res.unwrap();

        time_t t_in = 1438948860;
        memset(&tm, 0, sizeof(tm));
        tm.et_tm = *gmtime(&t_in);
        auto win_opt = rt.window_start(tm);
            CHECK(!win_opt.has_value());
    }

    {
        auto rt_res = relative_time::from_str("after 2014");

            CHECK(rt_res.isOk());
        auto rt = rt_res.unwrap();

        time_t t_in = 1438948860;
        memset(&tm, 0, sizeof(tm));
        tm.et_tm = *gmtime(&t_in);
        auto win_opt = rt.window_start(tm);
            CHECK(win_opt.has_value());
    }

    {
        auto rt_res = relative_time::from_str("after fri");

            CHECK(rt_res.isOk());
        auto rt = rt_res.unwrap();

        time_t t_in = 1438948860;
        memset(&tm, 0, sizeof(tm));
        tm.et_tm = *gmtime(&t_in);
        auto win_opt = rt.window_start(tm);
            CHECK(!win_opt.has_value());
    }

    {
        auto rt_res = relative_time::from_str("before fri");

            CHECK(rt_res.isOk());
        auto rt = rt_res.unwrap();

        time_t t_in = 1438948860;
        memset(&tm, 0, sizeof(tm));
        tm.et_tm = *gmtime(&t_in);
        auto win_opt = rt.window_start(tm);
        CHECK(!win_opt.has_value());
    }

    {
        auto rt_res = relative_time::from_str("before 12pm");

            CHECK(rt_res.isOk());
        auto rt = rt_res.unwrap();

        time_t t_in = 1438948860;
        memset(&tm, 0, sizeof(tm));
        tm.et_tm = *gmtime(&t_in);
        auto win_opt = rt.window_start(tm);
            CHECK(!win_opt.has_value());
    }

    {
        auto rt_res = relative_time::from_str("sun after 1pm");

        CHECK(rt_res.isOk());
        auto rt = rt_res.unwrap();

        time_t t_in = 1615727900;
        memset(&tm, 0, sizeof(tm));
        tm.et_tm = *gmtime(&t_in);
        auto win_opt = rt.window_start(tm);
        auto win_tm = *win_opt;
            CHECK(win_tm.et_tm.tm_year == 121);
            CHECK(win_tm.et_tm.tm_mon == 2);
            CHECK(win_tm.et_tm.tm_mday == 14);
            CHECK(win_tm.et_tm.tm_hour == 13);
            CHECK(win_tm.et_tm.tm_min == 0);
            CHECK(win_tm.et_tm.tm_sec == 0);
    }

    {
        auto rt_res = relative_time::from_str("0:05");

            CHECK(rt_res.isOk());
        auto rt = rt_res.unwrap();

        time_t t_in = 5 * 60 + 15;
        memset(&tm, 0, sizeof(tm));
        tm.et_tm = *gmtime(&t_in);
        auto win_opt = rt.window_start(tm);
        auto win_tm = *win_opt;
            CHECK(win_tm.et_tm.tm_sec == 0);
            CHECK(win_tm.et_tm.tm_min == 5);
            CHECK(win_tm.et_tm.tm_hour == 0);

        t_in = 4 * 60 + 15;
        memset(&tm, 0, sizeof(tm));
        tm.et_tm = *gmtime(&t_in);
        win_opt = rt.window_start(tm);
            CHECK(!win_opt.has_value());
    }

    {
        auto rt_res = relative_time::from_str("mon");

            CHECK(rt_res.isOk());
        auto rt = rt_res.unwrap();

        time_t t_in = 1615841352;
        memset(&tm, 0, sizeof(tm));
        tm.et_tm = *gmtime(&t_in);
        auto win_opt = rt.window_start(tm);
        auto win_tm = *win_opt;
            CHECK(win_tm.et_tm.tm_year == 121);
            CHECK(win_tm.et_tm.tm_mon == 2);
            CHECK(win_tm.et_tm.tm_mday == 15);
            CHECK(win_tm.et_tm.tm_hour == 0);
            CHECK(win_tm.et_tm.tm_min == 0);
            CHECK(win_tm.et_tm.tm_sec == 0);
    }

    {
        auto rt_res = relative_time::from_str("tue");

        CHECK(rt_res.isOk());
        auto rt = rt_res.unwrap();
        CHECK(rt.rt_included_days ==
              std::set<relative_time::token_t>{relative_time::RTT_TUESDAY});
    }

    {
        auto rt_res = relative_time::from_str("1m");

        CHECK(rt_res.isOk());
        auto rt = rt_res.unwrap();

        time_t t_in = 30;
        memset(&tm, 0, sizeof(tm));
        tm.et_tm = *gmtime(&t_in);
        auto win_opt = rt.window_start(tm);
        auto win_tm = *win_opt;
        CHECK(win_tm.et_tm.tm_sec == 0);
        CHECK(win_tm.et_tm.tm_min == 0);
        CHECK(win_tm.et_tm.tm_hour == 0);

        t_in = 90;
        memset(&tm, 0, sizeof(tm));
        tm.et_tm = *gmtime(&t_in);
        win_opt = rt.window_start(tm);
        win_tm = *win_opt;
            CHECK(win_tm.et_tm.tm_sec == 0);
            CHECK(win_tm.et_tm.tm_min == 1);
            CHECK(win_tm.et_tm.tm_hour == 0);
    }

    relative_time rt;
    for (int lpc = 0; TEST_DATA[lpc].reltime; lpc++) {
        auto res = relative_time::from_str(TEST_DATA[lpc].reltime);
        CHECK_MESSAGE(res.isOk(), TEST_DATA[lpc].reltime);
        rt = res.unwrap();
        CHECK(std::string(TEST_DATA[lpc].expected) == rt.to_string());
        rt.negate();
        CHECK(std::string(TEST_DATA[lpc].expected_negate) == rt.to_string());
    }

    for (int lpc = 0; BAD_TEST_DATA[lpc].reltime; lpc++) {
        auto res = relative_time::from_str(BAD_TEST_DATA[lpc].reltime);
        CHECK(res.isErr());
        CHECK(res.unwrapErr().pe_msg == string(BAD_TEST_DATA[lpc].expected_error));
    }

    rt = relative_time::from_str("").unwrap();
    CHECK(rt.empty());

    rt = relative_time::from_str("a minute ago").unwrap();
    CHECK(rt.rt_field[relative_time::RTF_MINUTES].value == -1);
    CHECK(rt.is_negative() == true);

    rt = relative_time::from_str("5 milliseconds").unwrap();
    CHECK(rt.rt_field[relative_time::RTF_MICROSECONDS].value == 5 * 1000);

    rt = relative_time::from_str("5000 ms ago").unwrap();
    CHECK(rt.rt_field[relative_time::RTF_SECONDS].value == -5);

    rt = relative_time::from_str("5 hours 20 minutes ago").unwrap();

    CHECK(rt.rt_field[relative_time::RTF_HOURS].value == -5);
    CHECK(rt.rt_field[relative_time::RTF_MINUTES].value == -20);

    rt = relative_time::from_str("5 hours and 20 minutes ago").unwrap();

    CHECK(rt.rt_field[relative_time::RTF_HOURS].value == -5);
    CHECK(rt.rt_field[relative_time::RTF_MINUTES].value == -20);

    rt = relative_time::from_str("1:23").unwrap();

    CHECK(rt.rt_field[relative_time::RTF_HOURS].value == 1);
    CHECK(rt.rt_field[relative_time::RTF_MINUTES].value == 23);
    CHECK(rt.is_absolute());

    rt = relative_time::from_str("1:23:45").unwrap();

    CHECK(rt.rt_field[relative_time::RTF_HOURS].value == 1);
    CHECK(rt.rt_field[relative_time::RTF_MINUTES].value == 23);
    CHECK(rt.rt_field[relative_time::RTF_SECONDS].value == 45);
    CHECK(rt.is_absolute());

    tm = base_tm;
    tm = rt.adjust(tm);

    new_time = timegm(&tm.et_tm);
    tm.et_tm = *gmtime(&new_time);
    CHECK(tm.et_tm.tm_hour == 1);
    CHECK(tm.et_tm.tm_min == 23);

    rt = relative_time::from_str("5 minutes ago").unwrap();

    tm = base_tm;
    tm = rt.adjust(tm);

    new_time = timegm(&tm.et_tm);

    CHECK(new_time == (base_time - (5 * 60)));

    rt = relative_time::from_str("today at 4pm").unwrap();
    memset(&tm, 0, sizeof(tm));
    memset(&tm2, 0, sizeof(tm2));
    gettimeofday(&tv, nullptr);
    localtime_r(&tv.tv_sec, &tm.et_tm);
    localtime_r(&tv.tv_sec, &tm2.et_tm);
    tm2.et_tm.tm_hour = 16;
    tm2.et_tm.tm_min = 0;
    tm2.et_tm.tm_sec = 0;
    tm = rt.adjust(tm);
    tm.et_tm.tm_yday = 0;
    tm2.et_tm.tm_yday = 0;
    tm.et_tm.tm_wday = 0;
    tm2.et_tm.tm_wday = 0;
#ifdef HAVE_STRUCT_TM_TM_ZONE
    tm2.et_tm.tm_gmtoff = 0;
    tm2.et_tm.tm_zone = nullptr;
#endif
    CHECK(tm.et_tm.tm_year == tm2.et_tm.tm_year);
    CHECK(tm.et_tm.tm_mon == tm2.et_tm.tm_mon);
    CHECK(tm.et_tm.tm_mday == tm2.et_tm.tm_mday);
    CHECK(tm.et_tm.tm_hour == tm2.et_tm.tm_hour);
    CHECK(tm.et_tm.tm_min == tm2.et_tm.tm_min);
    CHECK(tm.et_tm.tm_sec == tm2.et_tm.tm_sec);

    rt = relative_time::from_str("yesterday at 4pm").unwrap();
    gettimeofday(&tv, nullptr);
    localtime_r(&tv.tv_sec, &tm.et_tm);
    localtime_r(&tv.tv_sec, &tm2.et_tm);
    tm2.et_tm.tm_mday -= 1;
    tm2.et_tm.tm_hour = 16;
    tm2.et_tm.tm_min = 0;
    tm2.et_tm.tm_sec = 0;
    tm = rt.adjust(tm);
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

    rt = relative_time::from_str("2 days ago").unwrap();
    gettimeofday(&tv, nullptr);
    localtime_r(&tv.tv_sec, &tm.et_tm);
    localtime_r(&tv.tv_sec, &tm2.et_tm);
    tm2.et_tm.tm_mday -= 2;
    tm2.et_tm.tm_hour = 0;
    tm2.et_tm.tm_min = 0;
    tm2.et_tm.tm_sec = 0;
    tm = rt.adjust(tm);
    tm.et_tm.tm_yday = 0;
    tm2.et_tm.tm_yday = 0;
    tm.et_tm.tm_wday = 0;
    tm2.et_tm.tm_wday = 0;
#ifdef HAVE_STRUCT_TM_TM_ZONE
    tm2.et_tm.tm_gmtoff = 0;
    tm2.et_tm.tm_zone = nullptr;
#endif
        CHECK(tm.et_tm.tm_year == tm2.et_tm.tm_year);
        CHECK(tm.et_tm.tm_mon == tm2.et_tm.tm_mon);
        CHECK(tm.et_tm.tm_mday == tm2.et_tm.tm_mday);
        CHECK(tm.et_tm.tm_hour == tm2.et_tm.tm_hour);
        CHECK(tm.et_tm.tm_min == tm2.et_tm.tm_min);
        CHECK(tm.et_tm.tm_sec == tm2.et_tm.tm_sec);
}
