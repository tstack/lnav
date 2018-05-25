/**
 * Copyright (c) 2017, Timothy Stack
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

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.hh"

#include "lnav_config.hh"
#include "view_curses.hh"
#include "relative_time.hh"
#include "unique_path.hh"

using namespace std;

TEST_CASE("str2reltime") {
    string val;

    str2reltime(25 * 60 * 60 * 1000 + 123, val);
    CHECK(val == "1d1h0m0s");
    val.clear();
    str2reltime(10 * 1000 + 123, val);
    CHECK(val == "10s123");
    val.clear();
    str2reltime(10 * 1000, val);
    CHECK(val == "10s000");
    val.clear();
    str2reltime(100, val);
    CHECK(val == "100");
    val.clear();
    str2reltime(0, val);
    CHECK(val == "");
}

TEST_CASE("ptime_fmt") {
    const char *date_str = "2018-05-16 18:16:42";
    struct exttm tm;
    off_t off = 0;

    bool rc = ptime_fmt("%Y-%d-%m\t%H:%M:%S", &tm, date_str, off, strlen(date_str));
    CHECK(!rc);
    CHECK(off == 8);
}

TEST_CASE("rgb_color from string") {
    string name = "SkyBlue1";
    rgb_color color;
    string errmsg;
    bool rc;

    rc = rgb_color::from_str(name, color, errmsg);
    CHECK(rc);
    CHECK(color.rc_r == 135);
    CHECK(color.rc_g == 215);
    CHECK(color.rc_b == 255);
}

TEST_CASE("ptime_roundtrip") {
    const char *fmt = "%Y-%d-%m\t%H:%M:%S";
    time_t now = time(nullptr);

    for (time_t sec = now; sec < (now + (24 * 60 * 60)); sec++) {
        char ftime_result[128];
        char strftime_result[128];
        struct exttm etm;

        memset(&etm, 0, sizeof(etm));
        gmtime_r(&sec, &etm.et_tm);
        etm.et_flags = ETF_YEAR_SET | ETF_MONTH_SET | ETF_DAY_SET;
        size_t ftime_size = ftime_fmt(ftime_result, sizeof(ftime_result), fmt, etm);
        size_t strftime_size = strftime(strftime_result, sizeof(strftime_result), fmt, &etm.et_tm);

        CHECK(string(ftime_result, ftime_size) ==
              string(strftime_result, strftime_size));

        struct exttm etm2;
        off_t off = 0;

        memset(&etm2, 0, sizeof(etm2));
        bool rc = ptime_fmt(fmt, &etm2, ftime_result, off, ftime_size);
        CHECK(rc);
        CHECK(sec == tm2sec(&etm2.et_tm));
    }
}

class my_path_source : public unique_path_source {
public:
    my_path_source(const filesystem::path &p) : mps_path(p) {

    }

    filesystem::path get_path() const override {
        return this->mps_path;
    }

    filesystem::path mps_path;
};

TEST_CASE("unique_path") {
    unique_path_generator upg;

    auto bar = make_shared<my_path_source>("/foo/bar");
    auto bar_dupe = make_shared<my_path_source>("/foo/bar");
    auto baz = make_shared<my_path_source>("/foo/baz");
    auto baz2 = make_shared<my_path_source>("/foo2/bar");
    auto log1 = make_shared<my_path_source>(
        "/home/bob/downloads/machine1/var/log/syslog.log");
    auto log2 = make_shared<my_path_source>(
        "/home/bob/downloads/machine2/var/log/syslog.log");

    upg.add_source(bar);
    upg.add_source(bar_dupe);
    upg.add_source(baz);
    upg.add_source(baz2);
    upg.add_source(log1);
    upg.add_source(log2);

    upg.generate();

    CHECK(bar->get_unique_path() == "[foo]/bar");
    CHECK(bar_dupe->get_unique_path() == "[foo]/bar");
    CHECK(baz->get_unique_path() == "baz");
    CHECK(baz2->get_unique_path() == "[foo2]/bar");
    CHECK(log1->get_unique_path() == "[machine1]/syslog.log");
    CHECK(log2->get_unique_path() == "[machine2]/syslog.log");
}
