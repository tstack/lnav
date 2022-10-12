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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "byte_array.hh"
#include "data_scanner.hh"
#include "doctest/doctest.h"
#include "lnav_config.hh"
#include "lnav_util.hh"
#include "relative_time.hh"
#include "unique_path.hh"

using namespace std;

#if 0
TEST_CASE("overwritten-logfile") {
    string fname = "reload_test.0";

    ofstream(fname) << "test 1\n";

    logfile_open_options loo;
    logfile lf(fname, loo);
    auto build_result = lf.rebuild_index();
    CHECK(build_result == logfile::RR_NEW_LINES);
    CHECK(lf.size() == 1);

    sleep(1);
    ofstream(fname) << "test 2\n";
    auto rebuild_result = lf.rebuild_index();
    CHECK(rebuild_result == logfile::RR_NO_NEW_LINES);
    CHECK(lf.is_closed());
}
#endif

TEST_CASE("byte_array")
{
    using my_array_t = byte_array<8>;

    my_array_t ba1;

    memcpy(ba1.out(), "abcd1234", my_array_t::BYTE_COUNT);
    CHECK(ba1.to_string() == "6162636431323334");
    auto ba2 = ba1;
    CHECK(ba1 == ba2);
    CHECK_FALSE(ba1 != ba2);
    CHECK_FALSE(ba1 < ba2);

    my_array_t ba3;

    memcpy(ba3.out(), "abcd1235", my_array_t::BYTE_COUNT);
    CHECK(ba1 < ba3);
    CHECK_FALSE(ba3 < ba1);

    ba1.clear();
    CHECK(ba1.to_string() == "0000000000000000");
    CHECK(ba2.to_string() == "6162636431323334");

    auto outbuf = auto_buffer::alloc(my_array_t::STRING_SIZE);
    ba2.to_string(std::back_inserter(outbuf));
    CHECK(std::string(outbuf.in(), outbuf.size()) == "6162636431323334");
}

TEST_CASE("ptime_fmt")
{
    const char* date_str = "2018-05-16 18:16:42";
    struct exttm tm;
    off_t off = 0;

    bool rc
        = ptime_fmt("%Y-%d-%m\t%H:%M:%S", &tm, date_str, off, strlen(date_str));
    CHECK(!rc);
    CHECK(off == 8);
}

TEST_CASE("rgb_color from string")
{
    string name = "SkyBlue1";
    auto color = rgb_color::from_str(name).unwrap();
    CHECK(color.rc_r == 135);
    CHECK(color.rc_g == 215);
    CHECK(color.rc_b == 255);
}

TEST_CASE("ptime_roundtrip")
{
    const char* fmts[] = {
        "%Y-%m-%d %l:%M:%S %p",
        "%Y-%m-%d %I:%M:%S %p",
    };
    time_t now = time(nullptr);

    for (const auto* fmt : fmts) {
        for (time_t sec = now; sec < (now + (24 * 60 * 60)); sec++) {
            char ftime_result[128];
            char strftime_result[128];
            struct exttm etm;

            memset(&etm, 0, sizeof(etm));
            gmtime_r(&sec, &etm.et_tm);
            etm.et_flags = ETF_YEAR_SET | ETF_MONTH_SET | ETF_DAY_SET;
            size_t ftime_size
                = ftime_fmt(ftime_result, sizeof(ftime_result), fmt, etm);
            size_t strftime_size = strftime(
                strftime_result, sizeof(strftime_result), fmt, &etm.et_tm);

            CHECK(string(ftime_result, ftime_size)
                  == string(strftime_result, strftime_size));

            struct exttm etm2;
            off_t off = 0;

            memset(&etm2, 0, sizeof(etm2));
            bool rc = ptime_fmt(fmt, &etm2, ftime_result, off, ftime_size);
            CHECK(rc);
            CHECK(sec == tm2sec(&etm2.et_tm));
        }
    }
}

class my_path_source : public unique_path_source {
public:
    explicit my_path_source(ghc::filesystem::path p) : mps_path(std::move(p)) {}

    ghc::filesystem::path get_path() const override { return this->mps_path; }

    ghc::filesystem::path mps_path;
};

TEST_CASE("unique_path")
{
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

TEST_CASE("attr_line to json")
{
    attr_line_t al;

    al.append("Hello, ").append(lnav::roles::symbol("World")).append("!");

    auto json = lnav::to_json(al);
    auto al2 = lnav::from_json<attr_line_t>(json).unwrap();
    auto json2 = lnav::to_json(al2);

    CHECK(json == json2);
}

TEST_CASE("user_message to json")
{
    auto um = lnav::console::user_message::error("testing")
                  .with_reason("because")
                  .with_snippet(lnav::console::snippet::from(
                                    intern_string::lookup("hello.c"), "printf(")
                                    .with_line(1))
                  .with_help("close it");

    auto json = lnav::to_json(um);
    auto um2 = lnav::from_json<lnav::console::user_message>(json).unwrap();
    auto json2 = lnav::to_json(um2);

    CHECK(json == json2);
}

TEST_CASE("data_scanner CSI")
{
    static const char INPUT[] = "\x1b[32mHello\x1b[0m";

    data_scanner ds(string_fragment::from_const(INPUT));

    auto tok_res = ds.tokenize2();
    CHECK(tok_res->tr_token == DT_CSI);
    CHECK(tok_res->to_string() == "\x1b[32m");
    tok_res = ds.tokenize2();
    CHECK(tok_res->tr_token == DT_SYMBOL);
    CHECK(tok_res->to_string() == "Hello");
    tok_res = ds.tokenize2();
    CHECK(tok_res->tr_token == DT_CSI);
    CHECK(tok_res->to_string() == "\x1b[0m");
}
