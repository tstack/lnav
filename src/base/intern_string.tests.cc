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

#include <cctype>
#include <iostream>

#include "intern_string.hh"

#include "config.h"
#include "doctest/doctest.h"

TEST_CASE("string_fragment::startswith")
{
    std::string empty;
    auto sf = string_fragment{empty};

    CHECK_FALSE(sf.startswith("abc"));
}

TEST_CASE("string_fragment::lt")
{
    auto sf1 = string_fragment::from_const("abc");
    auto sf2 = string_fragment::from_const("abcdef");

    CHECK(sf1 < sf2);
    CHECK_FALSE(sf2 < sf1);
}

TEST_CASE("split_lines")
{
    std::string in1 = "Hello, World!";
    std::string in2 = "Hello, World!\nGoodbye, World!";

    {
        auto sf = string_fragment(in1);
        auto split = sf.split_lines();

        CHECK(1 == split.size());
        CHECK(in1 == split[0].to_string());
    }

    {
        auto sf = string_fragment::from_str_range(in1, 7, -1);
        auto split = sf.split_lines();

        CHECK(1 == split.size());
        CHECK("World!" == split[0].to_string());
    }

    {
        auto sf = string_fragment(in2);
        auto split = sf.split_lines();

        CHECK(2 == split.size());
        CHECK("Hello, World!\n" == split[0].to_string());
        CHECK("Goodbye, World!" == split[1].to_string());
    }
}

TEST_CASE("consume")
{
    auto is_eq = string_fragment::tag1{'='};
    auto is_dq = string_fragment::tag1{'"'};
    auto is_colon = string_fragment::tag1{':'};

    const char* pair = "foo  =  bar";
    auto sf = string_fragment(pair);

    auto split_sf = sf.split_while(isalnum);

    CHECK(split_sf.has_value());
    CHECK(split_sf->first.to_string() == "foo");
    CHECK(split_sf->second.to_string() == "  =  bar");

    auto value_frag = split_sf->second.skip(isspace).consume(is_eq);

    CHECK(value_frag.has_value());
    CHECK(value_frag->to_string() == "  bar");

    auto stripped_value_frag = value_frag->consume(isspace);

    CHECK(stripped_value_frag.has_value());
    CHECK(stripped_value_frag->to_string() == "bar");

    auto no_value = sf.consume(is_colon);
    CHECK(!no_value.has_value());

    const char* qs = R"("foo \" bar")";
    auto qs_sf = string_fragment{qs};

    auto qs_body = qs_sf.consume(is_dq);
    string_fragment::quoted_string_body qsb;
    auto split_body = qs_body->split_while(qsb);

    CHECK(split_body.has_value());
    CHECK(split_body->first.to_string() == "foo \\\" bar");
    CHECK(split_body->second.to_string() == "\"");

    auto empty = split_body->second.consume(is_dq);

    CHECK(empty.has_value());
    CHECK(empty->empty());
}

TEST_CASE("find_left_boundary")
{
    std::string in1 = "Hello,\nWorld!\n";

    {
        auto sf = string_fragment{in1};

        auto world_sf = sf.find_left_boundary(
            in1.length() - 3, [](auto ch) { return ch == '\n'; });
        CHECK(world_sf.to_string() == "World!\n");
        auto world_sf2 = sf.find_left_boundary(
            in1.length() - 3, [](auto ch) { return ch == '\n'; }, 2);
        CHECK(world_sf2.to_string() == "Hello,\nWorld!\n");
        auto world_sf3 = sf.find_left_boundary(
            in1.length() - 3, [](auto ch) { return ch == '\n'; }, 3);
        CHECK(world_sf3.to_string() == "Hello,\nWorld!\n");
        auto full_sf
            = sf.find_left_boundary(3, [](auto ch) { return ch == '\n'; });
        CHECK(full_sf.to_string() == in1);
    }
}

TEST_CASE("find_right_boundary")
{
    {
        const auto sf = string_fragment::from_const("Hello,\nWorld!\n");

        auto world_sf = sf.find_right_boundary(sf.length() - 3,
                                               string_fragment::tag1{'\n'});
        CHECK(world_sf.to_string() == "Hello,\nWorld!");
        auto hello_sf
            = sf.find_right_boundary(3, [](auto ch) { return ch == '\n'; });
        CHECK(hello_sf.to_string() == "Hello,");
        auto hello_sf2
            = sf.find_right_boundary(3, string_fragment::tag1{'\n'}, 2);
        CHECK(hello_sf2.to_string() == "Hello,\nWorld!");
    }
}

TEST_CASE("find_boundaries_around")
{
    {
        const auto sf = string_fragment::from_const(
            R"(Hello,
World!
Goodbye,
World!)");

        auto all_sf1
            = sf.find_boundaries_around(3, string_fragment::tag1{'\n'});
        CHECK(all_sf1 == "Hello,");
        auto all_sf2
            = sf.find_boundaries_around(3, string_fragment::tag1{'\n'}, 2);
        CHECK(all_sf2 == "Hello,\nWorld!");
    }
}

TEST_CASE("string_fragment::column_width")
{
    {
        const auto sf = string_fragment::from_const("Key(s)\n");

        CHECK(7 == sf.column_width());
    }
    {
        const auto sf = string_fragment::from_const("\u26a0");

        CHECK(1 == sf.column_width());
    }
}
