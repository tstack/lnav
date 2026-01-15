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
 *
 * @file logfmt.parser.test.cc
 */

#include <iostream>

#include "config.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "logfmt.parser.hh"
#include "scn/scan.h"

TEST_CASE("basic")
{
    static const char* line
        = "abc=def ghi=\"1 2 3 4\" time=333 empty1= tf=true empty2=";

    auto p = logfmt::parser{string_fragment{line}};

    auto pair1 = p.step();

    CHECK(pair1.is<logfmt::parser::kvpair>());
    CHECK(pair1.get<logfmt::parser::kvpair>().first == "abc");
    CHECK(pair1.get<logfmt::parser::kvpair>()
              .second.get<logfmt::parser::unquoted_value>()
              .uv_value
          == "def");

    auto pair2 = p.step();

    CHECK(pair2.is<logfmt::parser::kvpair>());
    CHECK(pair2.get<logfmt::parser::kvpair>().first == "ghi");
    CHECK(pair2.get<logfmt::parser::kvpair>()
              .second.get<logfmt::parser::quoted_value>()
              .qv_value
          == "\"1 2 3 4\"");

    auto pair3 = p.step();

    CHECK(pair3.is<logfmt::parser::kvpair>());
    CHECK(pair3.get<logfmt::parser::kvpair>().first == "time");
    CHECK(pair3.get<logfmt::parser::kvpair>()
              .second.get<logfmt::parser::int_value>()
              .iv_value
          == 333);

    auto pair4 = p.step();

    CHECK(pair4.is<logfmt::parser::kvpair>());
    CHECK(pair4.get<logfmt::parser::kvpair>().first == "empty1");
    CHECK(pair4.get<logfmt::parser::kvpair>()
              .second.get<logfmt::parser::unquoted_value>()
              .uv_value
          == "");

    auto pair5 = p.step();

    CHECK(pair5.is<logfmt::parser::kvpair>());
    CHECK(pair5.get<logfmt::parser::kvpair>().first == "tf");
    CHECK(pair5.get<logfmt::parser::kvpair>()
              .second.get<logfmt::parser::bool_value>()
              .bv_value);

    auto pair6 = p.step();

    CHECK(pair6.is<logfmt::parser::kvpair>());
    CHECK(pair6.get<logfmt::parser::kvpair>().first == "empty2");
    CHECK(pair6.get<logfmt::parser::kvpair>()
              .second.get<logfmt::parser::unquoted_value>()
              .uv_value
          == "");

    auto eoi = p.step();
    CHECK(eoi.is<logfmt::parser::end_of_input>());
}

TEST_CASE("floats")
{
    static const char* line
        = "f1=1.0 f2=-2.0 f3=1.2e3 f4=1.2e-2 f5=2e1 f6=2e+1";

    auto p = logfmt::parser{string_fragment{line}};

    auto pair1 = p.step();

    CHECK(pair1.is<logfmt::parser::kvpair>());
    CHECK(pair1.get<logfmt::parser::kvpair>().first == "f1");
    CHECK(pair1.get<logfmt::parser::kvpair>()
              .second.get<logfmt::parser::float_value>()
              .fv_value
          == 1.0);

    auto pair2 = p.step();

    CHECK(pair2.is<logfmt::parser::kvpair>());
    CHECK(pair2.get<logfmt::parser::kvpair>().first == "f2");
    CHECK(pair2.get<logfmt::parser::kvpair>()
              .second.get<logfmt::parser::float_value>()
              .fv_value
          == -2.0);

    auto pair3 = p.step();

    CHECK(pair3.is<logfmt::parser::kvpair>());
    CHECK(pair3.get<logfmt::parser::kvpair>().first == "f3");
    CHECK(pair3.get<logfmt::parser::kvpair>()
              .second.get<logfmt::parser::float_value>()
              .fv_value
          == 1200);

    auto pair4 = p.step();

    CHECK(pair4.is<logfmt::parser::kvpair>());
    CHECK(pair4.get<logfmt::parser::kvpair>().first == "f4");
    CHECK(pair4.get<logfmt::parser::kvpair>()
              .second.get<logfmt::parser::float_value>()
              .fv_value
          == 0.012);

    auto pair5 = p.step();

    CHECK(pair5.is<logfmt::parser::kvpair>());
    CHECK(pair5.get<logfmt::parser::kvpair>().first == "f5");
    CHECK(pair5.get<logfmt::parser::kvpair>()
              .second.get<logfmt::parser::float_value>()
              .fv_value
          == 20);

    auto pair6 = p.step();

    CHECK(pair6.is<logfmt::parser::kvpair>());
    CHECK(pair6.get<logfmt::parser::kvpair>().first == "f6");
    CHECK(pair6.get<logfmt::parser::kvpair>()
              .second.get<logfmt::parser::float_value>()
              .fv_value
          == 20);
}

TEST_CASE("message with keys")
{
    static const auto line
        = R"(this is foo=bar duration=10 a value="with spaces" message)"_frag;

    auto p = logfmt::parser{line};

    auto step1 = p.step();

    CHECK(step1.is<string_fragment>());
    CHECK(step1.get<string_fragment>() == "this is "_frag);

    auto step2 = p.step();

    CHECK(step2.is<logfmt::parser::kvpair>());
    auto step2pair = step2.get<logfmt::parser::kvpair>();
    CHECK(step2pair.first == "foo");
    CHECK(step2pair.second.get<logfmt::parser::unquoted_value>().uv_value
          == "bar");

    auto step3 = p.step();

    CHECK(step3.is<logfmt::parser::kvpair>());
    auto step3pair = step3.get<logfmt::parser::kvpair>();
    CHECK(step3pair.first == "duration");
    CHECK(step3pair.second.get<logfmt::parser::int_value>().iv_value == 10);

    auto step4 = p.step();

    CHECK(step4.is<string_fragment>());
    CHECK(step4.get<string_fragment>() == "a "_frag);

    auto step5 = p.step();

    CHECK(step5.is<logfmt::parser::kvpair>());
    auto step5pair = step5.get<logfmt::parser::kvpair>();
    CHECK(step5pair.first == "value");
    CHECK(step5pair.second.get<logfmt::parser::quoted_value>().qv_value
          == "\"with spaces\""_frag);

    auto step6 = p.step();
    CHECK(step6.is<string_fragment>());
    CHECK(step6.get<string_fragment>() == "message"_frag);
}

TEST_CASE("oops")
{
    static const auto line
        = "1320279567.211031\tCtgxRAqDLvrRUQdqe\t192.168.2.76\t52033\t184.72.234.3\t80\t1\tGET\tpixel.redditmedia.com\t/pixel/of_destiny.png?v=32tb6zakMbpImUZWtz+pksVc/8wYRc822cfKz091HT0oAKWHwZGxGpDcvvwUpyjwU8nJsyGc4cw=&r=296143927\thttp://www.reddit.com/\t1.1\tMozilla/5.0 (Macintosh; Intel Mac OS X 10.6; rv:7.0.1) Gecko/20100101 Firefox/7.0.1\t0\t105\t200\tOK\t-\t-\t(empty)\t-\t-\t-\t-\t-\t-\tF5EJmr1cvlMkJFqSSk\t-\timage/png\n1320279567.296908\tCwFs1P2UcUdlSxD2La\t192.168.2.76\t52026\t132.235.215.119\t80\t2\tGET\twww.reddit.com\t/static/bg-button-positive-unpressed.png\thttp://www.reddit.com/static/reddit.RZTLMiZ4gTk.css\t1.1\tMozilla/5.0 (Macintosh; Intel Mac OS X 10.6; rv:7.0.1) Gecko/20100101 Firefox/7.0.1\t0\t0\t304\tNot Modified\t-\t-\t(empty)\t-\t-\t-\t-\t-\t-\t-\t-\t-\n1320279567.451885\tCtgxRAqDLvrRUQdqe\t192.168.2.76\t52033\t184.72.234.3\t80\t2\tGET\tpixel.redditmedia.com\t/fetch-trackers?callback=jQuery16107779853632052074_1320279566998&ids[]=t5_6&ids[]=t3_lsfmb&ids[]=t3_lsejk&_=1320279567192\thttp://www.reddit.com/\t1.1\tMozilla/5.0 (Macintosh; Intel Mac OS X 10.6; rv:7.0.1) G"_frag;

    auto p = logfmt::parser{line};

    while (true) {
        auto step1 = p.step();

        if (step1.is<logfmt::parser::end_of_input>()) {
            break;
        }
    }
}

TEST_CASE("bad floats")
{
    static const char* line = "bf1=- bf2=-1.2e bf3=1.2.3 bf4=1e2e4";

    auto p = logfmt::parser{string_fragment{line}};

    auto pair1 = p.step();

    CHECK(pair1.is<logfmt::parser::kvpair>());
    CHECK(pair1.get<logfmt::parser::kvpair>().first == "bf1");
    CHECK(pair1.get<logfmt::parser::kvpair>()
              .second.get<logfmt::parser::unquoted_value>()
              .uv_value
          == "-");

    auto pair2 = p.step();

    CHECK(pair2.is<logfmt::parser::kvpair>());
    CHECK(pair2.get<logfmt::parser::kvpair>().first == "bf2");
    CHECK(pair2.get<logfmt::parser::kvpair>()
              .second.get<logfmt::parser::unquoted_value>()
              .uv_value
          == "-1.2e");

    auto pair3 = p.step();

    CHECK(pair3.is<logfmt::parser::kvpair>());
    CHECK(pair3.get<logfmt::parser::kvpair>().first == "bf3");
    CHECK(pair3.get<logfmt::parser::kvpair>()
              .second.get<logfmt::parser::unquoted_value>()
              .uv_value
          == "1.2.3");

    auto pair4 = p.step();

    CHECK(pair4.is<logfmt::parser::kvpair>());
    CHECK(pair4.get<logfmt::parser::kvpair>().first == "bf4");
    CHECK(pair4.get<logfmt::parser::kvpair>()
              .second.get<logfmt::parser::unquoted_value>()
              .uv_value
          == "1e2e4");
}

TEST_CASE("non-terminated string")
{
    static const char* line = "abc=\"12 2";

    auto p = logfmt::parser{string_fragment{line}};
    auto pair1 = p.step();

    CHECK(pair1.is<logfmt::parser::error>());
    CHECK(pair1.get<logfmt::parser::error>().e_offset == 9);
    CHECK(pair1.get<logfmt::parser::error>().e_msg == "non-terminated string");
}

TEST_CASE("missing equals")
{
    static const char* line = "abc";

    auto p = logfmt::parser{string_fragment{line}};
    auto pair1 = p.step();

    CHECK(pair1.is<string_fragment>());
    CHECK(pair1.get<string_fragment>() == "abc"_frag);
}

TEST_CASE("missing key")
{
    static const char* line = "=def";

    auto p = logfmt::parser{string_fragment{line}};
    auto pair1 = p.step();

    CHECK(pair1.is<logfmt::parser::error>());
    CHECK(pair1.get<logfmt::parser::error>().e_offset == 0);
    CHECK(pair1.get<logfmt::parser::error>().e_msg
          == "expecting key followed by '='");
}

TEST_CASE("empty")
{
    static const char* line = "";

    auto p = logfmt::parser{string_fragment{line}};
    auto pair1 = p.step();

    CHECK(pair1.is<logfmt::parser::end_of_input>());
}
