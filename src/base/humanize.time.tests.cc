/**
 * Copyright (c) 2021, Timothy Stack
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
#include <iostream>

#include "config.h"
#include "doctest/doctest.h"
#include "humanize.time.hh"

TEST_CASE("time ago")
{
    using namespace std::chrono_literals;

    time_t t1 = 1610000000;
    auto t1_chrono = std::chrono::seconds(t1);

    auto p1 = humanize::time::point::from_tv({t1, 0}).with_recent_point(
        {(time_t) t1 + 5, 0});

    CHECK(p1.as_time_ago() == "just now");
    CHECK(p1.as_precise_time_ago() == " 5 seconds ago");

    auto p2 = humanize::time::point::from_tv({t1, 0}).with_recent_point(
        {(time_t) t1 + 65, 0});

    CHECK(p2.as_time_ago() == "one minute ago");
    CHECK(p2.as_precise_time_ago() == " 1 minute and  5 seconds ago");

    auto p3 = humanize::time::point::from_tv({t1, 0}).with_recent_point(
        {(time_t) t1 + (3 * 60 + 5), 0});

    CHECK(p3.as_time_ago() == "3 minutes ago");
    CHECK(p3.as_precise_time_ago() == " 3 minutes and  5 seconds ago");

    auto p4 = humanize::time::point::from_tv({t1, 0}).with_recent_point(
        {(time_t) (t1_chrono + 65min).count(), 0});

    CHECK(p4.as_time_ago() == "one hour ago");
    CHECK(p4.as_precise_time_ago() == "one hour ago");

    auto p5 = humanize::time::point::from_tv({t1, 0}).with_recent_point(
        {(time_t) (t1_chrono + 3h).count(), 0});

    CHECK(p5.as_time_ago() == "3 hours ago");
    CHECK(p5.as_precise_time_ago() == "3 hours ago");

    auto p6 = humanize::time::point::from_tv({t1, 0}).with_recent_point(
        {(time_t) (t1_chrono + 25h).count(), 0});

    CHECK(p6.as_time_ago() == "one day ago");
    CHECK(p6.as_precise_time_ago() == "one day ago");

    auto p7 = humanize::time::point::from_tv({t1, 0}).with_recent_point(
        {(time_t) (t1_chrono + 50h).count(), 0});

    CHECK(p7.as_time_ago() == "2 days ago");
    CHECK(p7.as_precise_time_ago() == "2 days ago");

    auto p8 = humanize::time::point::from_tv({t1, 0}).with_recent_point(
        {(time_t) (t1_chrono + 370 * 24h).count(), 0});

    CHECK(p8.as_time_ago() == "over a year ago");
    CHECK(p8.as_precise_time_ago() == "over a year ago");

    auto p9 = humanize::time::point::from_tv({t1, 0}).with_recent_point(
        {(time_t) (t1_chrono + 800 * 24h).count(), 0});

    CHECK(p9.as_time_ago() == "over 2 years ago");
    CHECK(p9.as_precise_time_ago() == "over 2 years ago");

    CHECK(humanize::time::point::from_tv({1610000000, 0})
              .with_recent_point({(time_t) 1612000000, 0})
              .as_time_ago()
          == "23 days ago");
}

TEST_CASE("duration to_string")
{
    std::string val;

    val = humanize::time::duration::from_tv({25 * 60 * 60, 123000}).to_string();
    CHECK(val == "1d1h0m0s");
    val = humanize::time::duration::from_tv({10, 123000}).to_string();
    CHECK(val == "10s123");
    val = humanize::time::duration::from_tv({10, 0}).to_string();
    CHECK(val == "10s000");
    val = humanize::time::duration::from_tv({0, 100000}).to_string();
    CHECK(val == "100");
    val = humanize::time::duration::from_tv({0, 0}).to_string();
    CHECK(val == "");
    val = humanize::time::duration::from_tv({0, -10000}).to_string();
    CHECK(val == "-010");
    val = humanize::time::duration::from_tv({-10, 0}).to_string();
    CHECK(val == "-10s000");
}
