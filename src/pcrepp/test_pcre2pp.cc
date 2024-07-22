/**
 * Copyright (c) 2022, Timothy Stack
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
#include "doctest/doctest.h"
#include "pcre2pp.hh"

TEST_CASE("bad pattern")
{
    auto compile_res
        = lnav::pcre2pp::code::from(string_fragment::from_const("[abc"));

    CHECK(compile_res.isErr());
    auto ce = compile_res.unwrapErr();
    CHECK(ce.ce_offset == 4);
}

TEST_CASE("named captures")
{
    auto compile_res = lnav::pcre2pp::code::from(
        string_fragment::from_const("(?<abc>a)(b)(?<def>c)"));

    CHECK(compile_res.isOk());

    const std::vector<std::pair<size_t, string_fragment>> expected_caps = {
        {1, string_fragment::from_const("abc")},
        {3, string_fragment::from_const("def")},
    };

    int caps_index = 0;
    auto co = compile_res.unwrap();
    for (const auto cap : co.get_named_captures()) {
        const auto& expected_cap = expected_caps[caps_index];

        CHECK(expected_cap.first == cap.get_index());
        CHECK(expected_cap.second == cap.get_name());
        caps_index += 1;
    }
}

TEST_CASE("match")
{
    static const char INPUT[] = "key1=1234;key2=5678;";

    auto co
        = lnav::pcre2pp::code::from_const(R"((?<key>\w+)=(?<value>[^;]+);)");

    co.capture_from(string_fragment::from_const(INPUT))
        .for_each([](lnav::pcre2pp::match_data& md) {
            printf("got '%s' %s = %s\n",
                   md[0]->to_string().c_str(),
                   md[1]->to_string().c_str(),
                   md[2]->to_string().c_str());
        });
}

TEST_CASE("partial")
{
    auto co = lnav::pcre2pp::code::from_const(R"([a-z]+=.*)");

    {
        static const char INPUT[] = "key1=1234";

        auto matched = co.match_partial(string_fragment::from_const(INPUT));
        CHECK(matched == 3);
    }

    {
        static const char INPUT[] = "key";

        auto matched = co.match_partial(string_fragment::from_const(INPUT));
        CHECK(matched == 3);
    }
}

TEST_CASE("capture_name")
{
    auto co = lnav::pcre2pp::code::from_const("(?<abc>def)(ghi)");

    CHECK(co.get_capture_count() == 2);
    CHECK(string_fragment::from_c_str(co.get_name_for_capture(1)) == "abc");
    CHECK(co.get_name_for_capture(2) == nullptr);
}

TEST_CASE("get_capture_count")
{
    auto co = lnav::pcre2pp::code::from_const("(DEFINE)");

    CHECK(co.get_capture_count() == 1);
}

TEST_CASE("get_captures")
{
    auto co = lnav::pcre2pp::code::from_const(R"((?<abc>\w+)-(def)-)");

    CHECK(co.get_capture_count() == 2);
    const auto& caps = co.get_captures();
    CHECK(caps.size() == 2);
    CHECK(caps[0].to_string() == R"((?<abc>\w+))");
    CHECK(caps[1].to_string() == R"((def))");
}

TEST_CASE("replace")
{
    static const char INPUT[] = "test 1 2 3";

    auto co = lnav::pcre2pp::code::from_const(R"(\w*)");
    auto in = string_fragment::from_const(INPUT);

    auto res = co.replace(in, R"({\0})");
    CHECK(res == "{test}{} {1}{} {2}{} {3}{}");
}

TEST_CASE("replace-empty")
{
    static const char INPUT[] = "";

    auto co = lnav::pcre2pp::code::from_const(R"(\w*)");
    auto in = string_fragment::from_const(INPUT);

    auto res = co.replace(in, R"({\0})");
    CHECK(res == "{}");
}

TEST_CASE("for_each-all")
{
    static const char INPUT[] = "Hello, World!\n";

    auto co = lnav::pcre2pp::code::from_const(R"(.*)");
    auto in = string_fragment::from_const(INPUT);

    co.capture_from(in).for_each([](lnav::pcre2pp::match_data& md) {
        printf("range %d:%d\n", md[0]->sf_begin, md[0]->sf_end);
    });
}

TEST_CASE("capture_count")
{
    auto co = lnav::pcre2pp::code::from_const(R"(^(\w+)=([^;]+);)");

    CHECK(co.get_capture_count() == 2);
}

TEST_CASE("no-caps")
{
    const static std::string empty_cap_regexes[] = {
        "foo (?:bar)",
        "foo [(]",
        "foo \\Q(bar)\\E",
        "(?i)",
    };

    for (auto re : empty_cap_regexes) {
        auto co = lnav::pcre2pp::code::from(re).unwrap();

        CHECK(co.get_captures().empty());
    }
}

TEST_CASE("ipmatcher")
{
    auto co = lnav::pcre2pp::code::from_const(
        R"((?(DEFINE)(?<byte>2[0-4]\d|25[0-5]|1\d\d|[1-9]?\d))\b(?&byte)(\.(?&byte)){3}\b)");
    auto inp = string_fragment::from_const("192.168.1.1");

    auto find_res = co.find_in(inp).ignore_error();
    CHECK(find_res.has_value());
    CHECK(find_res->f_all.sf_begin == 0);
}

TEST_CASE("get_captures-nested")
{
    auto re = lnav::pcre2pp::code::from_const("foo (bar (?:baz)?)");

    CHECK(re.get_captures().size() == 1);
    CHECK(re.get_captures()[0].sf_begin == 4);
    CHECK(re.get_captures()[0].sf_end == 18);
    CHECK(re.get_captures()[0].length() == 14);
}

TEST_CASE("get_captures-basic")
{
    auto re = lnav::pcre2pp::code::from_const("(a)(b)(c)");

    assert(re.get_captures().size() == 3);
    assert(re.get_captures()[0].sf_begin == 0);
    assert(re.get_captures()[0].sf_end == 3);
    assert(re.get_captures()[1].sf_begin == 3);
    assert(re.get_captures()[1].sf_end == 6);
    assert(re.get_captures()[2].sf_begin == 6);
    assert(re.get_captures()[2].sf_end == 9);
}

TEST_CASE("get_captures-escape")
{
    auto re = lnav::pcre2pp::code::from_const("\\(a\\)(b)");

    assert(re.get_captures().size() == 1);
    assert(re.get_captures()[0].sf_begin == 5);
    assert(re.get_captures()[0].sf_end == 8);
}

TEST_CASE("get_captures-named")
{
    auto re = lnav::pcre2pp::code::from_const("(?<named>b)");

    assert(re.get_captures().size() == 1);
    assert(re.get_captures()[0].sf_begin == 0);
    assert(re.get_captures()[0].sf_end == 11);
}

TEST_CASE("get_captures-namedP")
{
    auto re = lnav::pcre2pp::code::from_const("(?P<named>b)");

    assert(re.get_captures().size() == 1);
    assert(re.get_captures()[0].sf_begin == 0);
    assert(re.get_captures()[0].sf_end == 12);
}

TEST_CASE("get_captures-namedq")
{
    auto re = lnav::pcre2pp::code::from_const("(?'named'b)");

    CHECK(re.get_captures().size() == 1);
    CHECK(re.get_captures()[0].sf_begin == 0);
    CHECK(re.get_captures()[0].sf_end == 11);
}

TEST_CASE("anchored")
{
    auto re = lnav::pcre2pp::code::from_const(
        "abc", PCRE2_ANCHORED | PCRE2_ENDANCHORED);

    const auto sub1 = string_fragment::from_const("abc");
    const auto sub2 = string_fragment::from_const("abcd");
    const auto sub3 = string_fragment::from_const("0abc");

    CHECK(re.find_in(sub1).ignore_error().has_value());
    CHECK_FALSE(re.find_in(sub2).ignore_error().has_value());
    CHECK_FALSE(re.find_in(sub3).ignore_error().has_value());
}
