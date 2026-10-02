/**
 * Copyright (c) 2026, Timothy Stack
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

#include <chrono>

#include <stdlib.h>

#include "time_util.hh"

#include "doctest/doctest.h"

TEST_CASE("tm2sec round-trips around leap years")
{
    // 1999-01-01 through 2001-12-31, plus 2023-12-31 through 2025-01-01.
    for (auto range : {std::pair<time_t, time_t>{915148800, 1009843200},
                       std::pair<time_t, time_t>{1703980800, 1735689600}})
    {
        for (auto t = range.first; t < range.second; t += 86400 + 3661) {
            tm gm;

            gmtime_r(&t, &gm);
            INFO(t);
            CHECK(tm2sec(&gm) == t);
        }
    }
}

TEST_CASE("strftime_rfc3339")
{
    using namespace std::chrono_literals;

    char buf[64];

    SUBCASE("typical")
    {
        auto len = lnav::strftime_rfc3339(
            buf, sizeof(buf), std::chrono::microseconds{951868800123456LL});
        CHECK(std::string(buf) == "2000-03-01 00:00:00.123456");
        CHECK(len == 26);
    }

    SUBCASE("negative")
    {
        lnav::strftime_rfc3339(
            buf, sizeof(buf), std::chrono::microseconds{-1500000}, 'T');
        CHECK(std::string(buf) == "1969-12-31T23:59:58.500000");
    }

    SUBCASE("wide years")
    {
        tm tm{};
        tm.tm_mday = 1;

        tm.tm_year = 10000 - 1900;
        lnav::strftime_rfc3339(buf, sizeof(buf), tm, 0us);
        CHECK(std::string(buf) == "10000-01-01 00:00:00.000000");

        tm.tm_year = -1 - 1900;
        lnav::strftime_rfc3339(buf, sizeof(buf), tm, 0us);
        CHECK(std::string(buf) == "-0001-01-01 00:00:00.000000");

        tm.tm_year = 5 - 1900;
        lnav::strftime_rfc3339(buf, sizeof(buf), tm, 0us);
        CHECK(std::string(buf) == "0005-01-01 00:00:00.000000");
    }

    SUBCASE("small buffers")
    {
        auto micros = std::chrono::microseconds{951868800123456LL};

        memset(buf, 'x', sizeof(buf));
        CHECK(lnav::strftime_rfc3339(buf, 0, micros) == 0);
        CHECK(buf[0] == 'x');

        CHECK(lnav::strftime_rfc3339(buf, 1, micros) == 0);
        CHECK(buf[0] == '\0');

        memset(buf, 'x', sizeof(buf));
        CHECK(lnav::strftime_rfc3339(buf, 11, micros) == 10);
        CHECK(std::string(buf) == "2000-03-01");
        CHECK(buf[11] == 'x');

        CHECK(lnav::strftime_rfc3339(buf, 27, micros) == 26);
        CHECK(std::string(buf) == "2000-03-01 00:00:00.123456");
    }
}

TEST_CASE("to_sys_time across DST transitions")
{
    // to_sys_time() looks up TZ on its first call, so set it before any.
    const auto* old_tz = getenv("TZ");
    auto saved_tz = old_tz ? std::string(old_tz) : std::string();
    setenv("TZ", "America/Los_Angeles", 1);
    tzset();

    using namespace date;
    using namespace std::chrono_literals;

    // Normal time in PST.
    CHECK(lnav::to_sys_time(local_days{2024_y / 1 / 15} + 12h)
          == sys_days{2024_y / 1 / 15} + 20h);

    // 02:30 on the spring-forward day does not exist.
    sys_seconds gap;
    CHECK_NOTHROW(gap = lnav::to_sys_time(local_days{2024_y / 3 / 10} + 2h
                                          + 30min));
    CHECK(gap == sys_days{2024_y / 3 / 10} + 10h);

    // 01:30 on the fall-back day happens twice; take the earlier (PDT).
    sys_seconds overlap;
    CHECK_NOTHROW(overlap = lnav::to_sys_time(local_days{2024_y / 11 / 3}
                                              + 1h + 30min));
    CHECK(overlap == sys_days{2024_y / 11 / 3} + 8h + 30min);

    if (old_tz) {
        setenv("TZ", saved_tz.c_str(), 1);
    } else {
        unsetenv("TZ");
    }
    tzset();
}
