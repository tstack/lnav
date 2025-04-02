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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <locale.h>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "../src/lnav_util.hh"
#include "base/date_time_scanner.hh"
#include "config.h"
#include "doctest/doctest.h"
#include "lnav_config.hh"
#include "ptimec.hh"

static const char* GOOD_TIMES[] = {
    "2023-001T00:59:36.208491Z",
    "2023-200T00:59:36.208491Z",
    "2023-08-11T00:59:36.208491Z",
    "09/Aug/2023:21:41:44 +0000",
    "2022-08-27T17:22:01.694554+03:00",
    "2022-08-27T17:22:01.694554+0300",
    "2022-08-27T17:22:01.694554+00:00",
    "2022-08-27T17:22:01.694554+0000",
    "2022-08-27T17:22:01.694554Z",
    "2022-08-27 17:22:01.694554 UTC",
    "2022-08-27 17:22:01.694554 GMT",
    "2017 May 08 Mon 18:57:57.578",
    "May 01 00:00:01",
    "May 10 12:00:01",
    "2014-02-11 16:12:34",
    "2014-02-11 16:12:34.123",
    "05/18/2018 12:00:53 PM",
    "05/18/2018 12:00:53 AM",
};

static const char* BAD_TIMES[] = {
    "1-2-3 1:2:3",

    "2013-22-01 12:01:22",
    "2013-00-01 12:01:22",

    "@4000000043",
};

TEST_CASE("date_time_scanner")
{
    setenv("TZ", "UTC", 1);

    lnav_config.lc_log_date_time.c_zoned_to_local = false;
    for (const auto* good_time : GOOD_TIMES) {
        date_time_scanner dts;
        timeval tv;
        exttm tm;
        const char* rc;

        rc = dts.scan(good_time, strlen(good_time), nullptr, &tm, tv);
        CHECK(dts.dts_zoned_to_local == false);
        printf("ret %s %p\n", good_time, rc);
        assert(rc != nullptr);

        char ts[64];

        dts.ftime(ts, sizeof(ts), nullptr, tm);
        printf("fmt %s\n", PTIMEC_FORMATS[dts.dts_fmt_lock].pf_fmt);
        printf("orig %s\n", good_time);
        printf("loop %s\n", ts);
        CHECK(std::string(ts) == std::string(good_time));
    }

    {
        const auto sf
            = string_fragment::from_const("2014-02-11 16:12:34.123.456");
        timeval tv;
        exttm tm;
        date_time_scanner dts;
        const auto* rc = dts.scan(sf.data(), sf.length(), nullptr, &tm, tv);
        CHECK((tm.et_flags & ETF_MILLIS_SET));
        CHECK(std::string(rc) == sf.substr(23).to_string());

        char ts[64];
        dts.ftime(ts, sizeof(ts), nullptr, tm);

        CHECK(std::string(ts) == std::string("2014-02-11 16:12:34.123"));
    }

    {
        const auto sf
            = string_fragment::from_const("2014-02-11 16:12:34.12345Z");
        timeval tv;
        exttm tm;
        date_time_scanner dts;
        const auto* rc = dts.scan(sf.data(), sf.length(), nullptr, &tm, tv);
        printf("fmt %s\n", PTIMEC_FORMAT_STR[dts.dts_fmt_lock]);
        CHECK(rc != nullptr);
        CHECK((tm.et_flags & ETF_MICROS_SET));
        CHECK(*rc == '\0');

        char ts[64];
        dts.ftime(ts, sizeof(ts), nullptr, tm);

        CHECK(std::string(ts) == std::string("2014-02-11 16:12:34.123450Z"));
    }

    {
        const auto sf
            = string_fragment::from_const("Tue Jul 25 12:01:01 AM UTC 2023");
        timeval tv;
        exttm tm;
        date_time_scanner dts;
        const auto* rc = dts.scan(sf.data(), sf.length(), nullptr, &tm, tv);
        printf("fmt %s\n", PTIMEC_FORMAT_STR[dts.dts_fmt_lock]);
        CHECK(rc != nullptr);
        CHECK((tm.et_flags & ETF_ZONE_SET));
        CHECK((tm.et_flags & ETF_Z_IS_UTC));
        CHECK(*rc == '\0');

        char ts[64];
        dts.ftime(ts, sizeof(ts), nullptr, tm);

        CHECK(std::string(ts)
              == std::string("Tue Jul 25 12:01:01 AM UTC 2023"));
    }

    {
        static const char* OLD_TIME = "05/18/1960 12:00:53 AM";
        date_time_scanner dts;
        timeval tv;
        exttm tm;

        auto rc = dts.scan(OLD_TIME, strlen(OLD_TIME), nullptr, &tm, tv);
        assert(rc != nullptr);
        char ts[64];
        dts.ftime(ts, sizeof(ts), nullptr, tm);
        assert(strcmp(ts, "05/18/1980 12:00:53 AM") == 0);
    }

    {
        date_time_scanner dts;
        timeval tv;

        dts.convert_to_timeval("@40000000433225833b6e1a8c", -1, nullptr, tv);
        assert(tv.tv_sec == 1127359865);
        assert(tv.tv_usec == 997071);

        memset(&tv, 0, sizeof(tv));
        dts.convert_to_timeval("@4000000043322583", -1, nullptr, tv);
        assert(tv.tv_sec == 1127359865);
        assert(tv.tv_usec == 0);
    }

    for (const auto* bad_time : BAD_TIMES) {
        date_time_scanner dts;
        timeval tv;
        exttm tm;

        printf("Checking bad time: %s\n", bad_time);
        assert(dts.scan(bad_time, strlen(bad_time), nullptr, &tm, tv)
               == nullptr);
    }

    {
        const char* en_date = "Jan  1 12:00:00";
        const char* es_date = " 1/Ene/2014:12:00:00 +0000";
        timeval en_tv, es_tv;
        exttm en_tm, es_tm;
        date_time_scanner dts;

        if (setlocale(LC_TIME, "es_ES.UTF-8") != nullptr) {
            CHECK(dts.scan(en_date, strlen(en_date), nullptr, &en_tm, en_tv)
                  != nullptr);
            dts.clear();
            CHECK(dts.scan(es_date, strlen(es_date), nullptr, &es_tm, es_tv)
                  != nullptr);
        }
    }

    {
        const char* en_date = "Jan  1 12:00:00";
        const char* fr_date = "août 19 11:08:37";
        timeval en_tv, fr_tv;
        exttm en_tm, fr_tm;
        date_time_scanner dts;

        if (setlocale(LC_TIME, "fr_FR.UTF-8") != nullptr) {
            CHECK(dts.scan(en_date, strlen(en_date), nullptr, &en_tm, en_tv)
                  != nullptr);
            dts.clear();
            CHECK(dts.scan(fr_date, strlen(fr_date), nullptr, &fr_tm, fr_tv)
                  != nullptr);
        }
    }

    {
        const char* ts = "22:46:03.471";
        const char* fmt[] = {
            "%H:%M:%S.%L",
            nullptr,
        };
        char buf[64];
        date_time_scanner dts;
        exttm tm;
        timeval tv;

        const auto* ts_end = dts.scan(ts, strlen(ts), fmt, &tm, tv);
        CHECK(ts_end - ts == 12);
        auto rc = dts.ftime(buf, sizeof(buf), fmt, tm);
        CHECK(rc == 12);
        CHECK(strcmp(ts, buf) == 0);
    }

    {
        const char* epoch_str = "ts 1428721664 ]";
        exttm tm;
        off_t off = 0;

        memset(&tm, 0, sizeof(tm));
        bool rc = ptime_fmt("ts %s ]", &tm, epoch_str, off, strlen(epoch_str));
        CHECK(rc);
        CHECK(tm2sec(&tm.et_tm) == 1428721664);
    }

    {
        const char* epoch_str = "ts 60150c93 ]";
        exttm tm;
        off_t off = 0;

        memset(&tm, 0, sizeof(tm));
        bool rc = ptime_fmt("ts %q ]", &tm, epoch_str, off, strlen(epoch_str));
        assert(rc);
        assert(tm2sec(&tm.et_tm) == 1611992211);

        char buf[32];
        ftime_fmt(buf, sizeof(buf), "ts %q ]", tm);
        assert(strcmp(buf, epoch_str) == 0);
    }

    {
        auto ts = "Jan  1 12:00:00";
        const char* fmt[] = {
            "%b %e %H:%M:%S",
            nullptr,
        };
        char buf[64];
        date_time_scanner dts;
        exttm tm;
        timeval tv;

        const auto* ts_end = dts.scan(ts, strlen(ts), fmt, &tm, tv);
        assert(ts_end - ts == 15);
        auto rc = dts.ftime(buf, sizeof(buf), fmt, tm);
        assert(rc == 15);
        assert(strcmp(ts, buf) == 0);
    }

    {
        const auto* ts = "1743570493000000014";
        const char* fmt[] = {
            "%9",
            nullptr,
        };
        char buf[64];
        date_time_scanner dts;
        exttm tm;
        timeval tv;

        const auto* ts_end = dts.scan(ts, strlen(ts), fmt, &tm, tv);
        assert(ts_end - ts == 19);
        assert(tv.tv_sec == 1743570493);
        assert(tm.et_nsec == 14);
        auto rc = ftime_fmt(buf, sizeof(buf), fmt[0], tm);
        assert(rc == 19);
        assert(strcmp(ts, buf) == 0);
    }
}
