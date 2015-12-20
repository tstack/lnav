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

#include <assert.h>

#include "relative_time.hh"

static struct {
    const char *reltime;
    const char *expected;
} TEST_DATA[] = {
        { "a minute ago", "0y0m0d0h-1m0s0u" },
        { "1m ago", "0y0m0d0h-1m0s0u" },
        { "a min ago", "0y0m0d0h-1m0s0u" },
        { "a m ago", "0y0m0d0h-1m0s0u" },
        { "+1 minute ago", "0y0m0d0h-1m0s0u" },
        { "-1 minute ago", "0y0m0d0h-1m0s0u" },
        { "-1 minute", "0y0m0d0h-1m0s0u" },
        { "1:40", "0y0m0d1H40M0S0U" },
        { "01:40", "0y0m0d1H40M0S0U" },
        { "1h40m", "0y0m0d1h40m0s0u" },
        { "1pm", "0y0m0d13H0M0S0U" },

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

int main(int argc, char *argv[])
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
        printf("%s %s %s\n", TEST_DATA[lpc].reltime, TEST_DATA[lpc].expected, rt.to_string().c_str());
        assert(rc);
        assert(std::string(TEST_DATA[lpc].expected) == rt.to_string());
    }

    for (int lpc = 0; BAD_TEST_DATA[lpc].reltime; lpc++) {
        bool rc;
        rt.clear();
        rc = rt.parse(BAD_TEST_DATA[lpc].reltime, pe);
        printf("%s -- %s\n", BAD_TEST_DATA[lpc].reltime, pe.pe_msg.c_str());
        assert(!rc);
    }

    rt.parse("a minute ago", pe);
    assert(rt.rt_field[relative_time::RTF_MINUTES] == -1);

    rt.parse("5 milliseconds", pe);

    assert(rt.rt_field[relative_time::RTF_MICROSECONDS] == 5 * 1000);

    rt.clear();
    rt.parse("5000 ms ago", pe);
    assert(rt.rt_field[relative_time::RTF_SECONDS] == -5);

    rt.clear();
    rt.parse("5 hours 20 minutes ago", pe);

    assert(rt.rt_field[relative_time::RTF_HOURS] == -5);
    assert(rt.rt_field[relative_time::RTF_MINUTES] == -20);

    rt.clear();
    rt.parse("5 hours and 20 minutes ago", pe);

    assert(rt.rt_field[relative_time::RTF_HOURS] == -5);
    assert(rt.rt_field[relative_time::RTF_MINUTES] == -20);

    rt.clear();
    rt.parse("1:23", pe);

    assert(rt.rt_field[relative_time::RTF_HOURS] == 1);
    assert(rt.rt_is_absolute[relative_time::RTF_HOURS]);
    assert(rt.rt_field[relative_time::RTF_MINUTES] == 23);
    assert(rt.rt_is_absolute[relative_time::RTF_MINUTES]);

    rt.clear();
    rt.parse("1:23:45", pe);

    assert(rt.rt_field[relative_time::RTF_HOURS] == 1);
    assert(rt.rt_is_absolute[relative_time::RTF_HOURS]);
    assert(rt.rt_field[relative_time::RTF_MINUTES] == 23);
    assert(rt.rt_is_absolute[relative_time::RTF_MINUTES]);
    assert(rt.rt_field[relative_time::RTF_SECONDS] == 45);
    assert(rt.rt_is_absolute[relative_time::RTF_SECONDS]);

    tm = base_tm;
    rt.add(tm);

    new_time = timegm(&tm.et_tm);
    tm.et_tm = *gmtime(&new_time);
    assert(tm.et_tm.tm_hour == 1);
    assert(tm.et_tm.tm_min == 23);

    rt.clear();
    rt.parse("5 minutes ago", pe);

    tm = base_tm;
    rt.add(tm);

    new_time = timegm(&tm.et_tm);

    assert(new_time == (base_time - (5 * 60)));

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
    assert(tm.et_tm.tm_year == tm2.et_tm.tm_year);
    assert(tm.et_tm.tm_mon == tm2.et_tm.tm_mon);
    assert(tm.et_tm.tm_mday == tm2.et_tm.tm_mday);
    assert(tm.et_tm.tm_hour == tm2.et_tm.tm_hour);
    assert(tm.et_tm.tm_min == tm2.et_tm.tm_min);
    assert(tm.et_tm.tm_sec == tm2.et_tm.tm_sec);

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
    assert(tm.et_tm.tm_year == tm2.et_tm.tm_year);
    assert(tm.et_tm.tm_mon == tm2.et_tm.tm_mon);
    assert(tm.et_tm.tm_mday == tm2.et_tm.tm_mday);
    assert(tm.et_tm.tm_hour == tm2.et_tm.tm_hour);
    assert(tm.et_tm.tm_min == tm2.et_tm.tm_min);
    assert(tm.et_tm.tm_sec == tm2.et_tm.tm_sec);
}
