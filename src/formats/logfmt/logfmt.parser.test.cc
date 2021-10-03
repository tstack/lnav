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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file logfmt.parser.test.cc
 */

#include "config.h"

#include <iostream>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "logfmt.parser.hh"

TEST_CASE("basic")
{
    static const char *line = "abc=def ghi=\"1 2 3 4\" time=333 empty1= tf=true empty2=";

    auto p = logfmt::parser{string_fragment{line}};

    auto pair1 = p.step();

    CHECK(pair1.is<logfmt::parser::kvpair>());
    CHECK(pair1.get<logfmt::parser::kvpair>().first == "abc");
    CHECK(pair1.get<logfmt::parser::kvpair>().second
              .get<logfmt::parser::unquoted_value>().uv_value == "def");

    auto pair2 = p.step();

    CHECK(pair2.is<logfmt::parser::kvpair>());
    CHECK(pair2.get<logfmt::parser::kvpair>().first == "ghi");
    CHECK(pair2.get<logfmt::parser::kvpair>().second
              .get<logfmt::parser::quoted_value>().qv_value == "\"1 2 3 4\"");

    auto pair3 = p.step();

    CHECK(pair3.is<logfmt::parser::kvpair>());
    CHECK(pair3.get<logfmt::parser::kvpair>().first == "time");
    CHECK(pair3.get<logfmt::parser::kvpair>().second
              .get<logfmt::parser::int_value>().iv_value == 333);

    auto pair4 = p.step();

    CHECK(pair4.is<logfmt::parser::kvpair>());
    CHECK(pair4.get<logfmt::parser::kvpair>().first == "empty1");
    CHECK(pair4.get<logfmt::parser::kvpair>().second
              .get<logfmt::parser::unquoted_value>().uv_value == "");

    auto pair5 = p.step();

    CHECK(pair5.is<logfmt::parser::kvpair>());
    CHECK(pair5.get<logfmt::parser::kvpair>().first == "tf");
    CHECK(pair5.get<logfmt::parser::kvpair>().second
              .get<logfmt::parser::bool_value>().bv_value);

    auto pair6 = p.step();

    CHECK(pair6.is<logfmt::parser::kvpair>());
    CHECK(pair6.get<logfmt::parser::kvpair>().first == "empty2");
    CHECK(pair6.get<logfmt::parser::kvpair>().second
              .get<logfmt::parser::unquoted_value>().uv_value == "");

    auto eoi = p.step();
    CHECK(eoi.is<logfmt::parser::end_of_input>());
}

TEST_CASE("floats")
{
    static const char *line = "f1=1.0 f2=-2.0 f3=1.2e3 f4=1.2e-2 f5=2e1 f6=2e+1";

    auto p = logfmt::parser{string_fragment{line}};

    auto pair1 = p.step();

    CHECK(pair1.is<logfmt::parser::kvpair>());
    CHECK(pair1.get<logfmt::parser::kvpair>().first == "f1");
    CHECK(pair1.get<logfmt::parser::kvpair>().second
              .get<logfmt::parser::float_value>().fv_value == 1.0);

    auto pair2 = p.step();

    CHECK(pair2.is<logfmt::parser::kvpair>());
    CHECK(pair2.get<logfmt::parser::kvpair>().first == "f2");
    CHECK(pair2.get<logfmt::parser::kvpair>().second
              .get<logfmt::parser::float_value>().fv_value == -2.0);

    auto pair3 = p.step();

    CHECK(pair3.is<logfmt::parser::kvpair>());
    CHECK(pair3.get<logfmt::parser::kvpair>().first == "f3");
    CHECK(pair3.get<logfmt::parser::kvpair>().second
              .get<logfmt::parser::float_value>().fv_value == 1200);

    auto pair4 = p.step();

    CHECK(pair4.is<logfmt::parser::kvpair>());
    CHECK(pair4.get<logfmt::parser::kvpair>().first == "f4");
    CHECK(pair4.get<logfmt::parser::kvpair>().second
              .get<logfmt::parser::float_value>().fv_value == 0.012);

    auto pair5 = p.step();

    CHECK(pair5.is<logfmt::parser::kvpair>());
    CHECK(pair5.get<logfmt::parser::kvpair>().first == "f5");
    CHECK(pair5.get<logfmt::parser::kvpair>().second
              .get<logfmt::parser::float_value>().fv_value == 20);

    auto pair6 = p.step();

    CHECK(pair6.is<logfmt::parser::kvpair>());
    CHECK(pair6.get<logfmt::parser::kvpair>().first == "f6");
    CHECK(pair6.get<logfmt::parser::kvpair>().second
              .get<logfmt::parser::float_value>().fv_value == 20);
}

TEST_CASE("bad floats")
{
    static const char *line = "bf1=- bf2=-1.2e bf3=1.2.3 bf4=1e2e4";

    auto p = logfmt::parser{string_fragment{line}};

    auto pair1 = p.step();

    CHECK(pair1.is<logfmt::parser::kvpair>());
    CHECK(pair1.get<logfmt::parser::kvpair>().first == "bf1");
    CHECK(pair1.get<logfmt::parser::kvpair>().second
              .get<logfmt::parser::unquoted_value>().uv_value == "-");

    auto pair2 = p.step();

    CHECK(pair2.is<logfmt::parser::kvpair>());
    CHECK(pair2.get<logfmt::parser::kvpair>().first == "bf2");
    CHECK(pair2.get<logfmt::parser::kvpair>().second
              .get<logfmt::parser::unquoted_value>().uv_value == "-1.2e");

    auto pair3 = p.step();

    CHECK(pair3.is<logfmt::parser::kvpair>());
    CHECK(pair3.get<logfmt::parser::kvpair>().first == "bf3");
    CHECK(pair3.get<logfmt::parser::kvpair>().second
              .get<logfmt::parser::unquoted_value>().uv_value == "1.2.3");

    auto pair4 = p.step();

    CHECK(pair4.is<logfmt::parser::kvpair>());
    CHECK(pair4.get<logfmt::parser::kvpair>().first == "bf4");
    CHECK(pair4.get<logfmt::parser::kvpair>().second
              .get<logfmt::parser::unquoted_value>().uv_value == "1e2e4");
}

TEST_CASE("non-terminated string")
{
    static const char *line = "abc=\"12 2";

    auto p = logfmt::parser{string_fragment{line}};
    auto pair1 = p.step();

    CHECK(pair1.is<logfmt::parser::error>());
    CHECK(pair1.get<logfmt::parser::error>().e_offset == 9);
    CHECK(pair1.get<logfmt::parser::error>().e_msg == "non-terminated string");
}

TEST_CASE("missing equals")
{
    static const char *line = "abc";

    auto p = logfmt::parser{string_fragment{line}};
    auto pair1 = p.step();

    CHECK(pair1.is<logfmt::parser::error>());
    CHECK(pair1.get<logfmt::parser::error>().e_offset == 3);
    CHECK(pair1.get<logfmt::parser::error>().e_msg == "expecting '='");
}

TEST_CASE("missing key")
{
    static const char *line = "=def";

    auto p = logfmt::parser{string_fragment{line}};
    auto pair1 = p.step();

    CHECK(pair1.is<logfmt::parser::error>());
    CHECK(pair1.get<logfmt::parser::error>().e_offset == 0);
    CHECK(pair1.get<logfmt::parser::error>().e_msg == "expecting key followed by '='");
}

TEST_CASE("empty")
{
    static const char *line = "";

    auto p = logfmt::parser{string_fragment{line}};
    auto pair1 = p.step();

    CHECK(pair1.is<logfmt::parser::end_of_input>());
}
