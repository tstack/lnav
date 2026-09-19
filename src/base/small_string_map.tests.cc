/**
 * Copyright (c) 2025, Timothy Stack
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

#include <iostream>

#include "fmt/format.h"
#include "small_string_map.hh"

#include "doctest/doctest.h"

TEST_CASE("empty lookup")
{
    auto ssm = lnav::small_string_map{};

    auto res = ssm.lookup("test"_frag);
    CHECK_FALSE(res.has_value());
}

TEST_CASE("basic lookup")
{
    auto ssm = lnav::small_string_map{};

    ssm.insert("info"_frag, 123);
    ssm.insert("304"_frag, 123);
    ssm.insert("404"_frag, 123);
    auto res = ssm.lookup("info"_frag);
    CHECK(res.has_value());
    CHECK(123 == res.value());

    res = ssm.lookup("test2"_frag);
    CHECK_FALSE(res.has_value());
}

TEST_CASE("empty keys")
{
    auto ssm = lnav::small_string_map{};

    CHECK_FALSE(ssm.lookup(""_frag).has_value());
    ssm.insert(""_frag, 1);
    CHECK_FALSE(ssm.lookup(""_frag).has_value());
}

TEST_CASE("key too long")
{
    auto ssm = lnav::small_string_map{};

    ssm.insert("123456789"_frag, 1);
    CHECK_FALSE(ssm.lookup("123456789"_frag).has_value());
    CHECK_FALSE(ssm.lookup("12345678"_frag).has_value());
}

TEST_CASE("every key length")
{
    auto ssm = lnav::small_string_map{};
    const auto full = std::string("abcdefgh");

    for (size_t len = 1; len <= full.size(); len++) {
        ssm.insert(string_fragment::from_str_range(full, 0, len), len);
    }
    for (size_t len = 1; len <= full.size(); len++) {
        auto res = ssm.lookup(string_fragment::from_str_range(full, 0, len));
        CHECK(res.has_value());
        CHECK(res.value() == len);
    }

    for (size_t len = 1; len <= full.size(); len++) {
        auto other = full.substr(0, len);
        other[len - 1] = 'z';
        CHECK_FALSE(ssm.lookup(string_fragment::from_str(other)).has_value());
    }
}

TEST_CASE("prefixes")
{
    auto ssm = lnav::small_string_map{};

    ssm.insert("warn"_frag, 1);
    ssm.insert("warning"_frag, 2);
    ssm.insert("critical"_frag, 3);
    ssm.insert("critica"_frag, 4);

    CHECK(ssm.lookup("warn"_frag).value() == 1);
    CHECK(ssm.lookup("warning"_frag).value() == 2);
    CHECK(ssm.lookup("critical"_frag).value() == 3);
    CHECK(ssm.lookup("critica"_frag).value() == 4);
    CHECK_FALSE(ssm.lookup("war"_frag).has_value());
}

TEST_CASE("eviction keeps recently used keys")
{
    auto ssm = lnav::small_string_map{};

    ssm.insert("hot"_frag, 100);
    for (uint32_t lpc = 0; lpc < 16; lpc++) {
        auto key = fmt::format(FMT_STRING("k{}"), lpc);

        CHECK(ssm.lookup("hot"_frag).value() == 100);
        ssm.insert(string_fragment::from_str(key), lpc);
        CHECK(ssm.lookup(string_fragment::from_str(key)).value() == lpc);
    }
    CHECK(ssm.lookup("hot"_frag).value() == 100);
}
