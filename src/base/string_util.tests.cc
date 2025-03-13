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

#include <iostream>

#include "base/string_util.hh"

#include "base/fts_fuzzy_match.hh"
#include "base/strnatcmp.h"
#include "config.h"
#include "doctest/doctest.h"

TEST_CASE("fuzzy_match")
{
    {
        // escape sequences should be ignored
        const char* str = "com.example.foo";
        const char* pattern1 = "c\\.e\\.f";
        const char* pattern2 = "c.e.f";

        int score1, score2;
        CHECK(fts::fuzzy_match(pattern1, str, score1));
        CHECK(fts::fuzzy_match(pattern2, str, score2));

        CHECK(score1 == score2);
    }
}

TEST_CASE("endswith")
{
    std::string hw("hello");

    CHECK(endswith(hw, "f") == false);
    CHECK(endswith(hw, "lo") == true);
}

TEST_CASE("truncate_to")
{
    const std::string orig = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::string str;

    truncate_to(str, 10);
    CHECK(str == "");
    str = "abc";
    truncate_to(str, 10);
    CHECK(str == "abc");
    str = orig;
    truncate_to(str, 10);
    CHECK(str == "01234\u22efwxyz");
    str = orig;
    truncate_to(str, 1);
    CHECK(str == "\u22ef");
    str = orig;
    truncate_to(str, 2);
    CHECK(str == "\u22ef");
    str = orig;
    truncate_to(str, 3);
    CHECK(str == "0\u22efz");
    str = orig;
    truncate_to(str, 4);
    CHECK(str == "01\u22efz");
    str = orig;
    truncate_to(str, 5);
    CHECK(str == "01\u22efyz");
}

TEST_CASE("strnatcmp")
{
    {
        constexpr const char* n1 = "010";
        constexpr const char* n2 = "020";

        CHECK(strnatcmp(strlen(n1), n1, strlen(n2), n2) < 0);
    }
    {
        constexpr const char* n1 = "2";
        constexpr const char* n2 = "10";

        CHECK(strnatcmp(strlen(n1), n1, strlen(n2), n2) < 0);
    }
    {
        constexpr const char* n1 = "servers";
        constexpr const char* n2 = "servers.alpha";

        CHECK(strnatcasecmp(strlen(n1), n1, strlen(n2), n2) < 0);
    }
    {
        static constexpr const char* TOKENS = "[](){}";
        const std::string n1 = "[servers]";
        const std::string n2 = "[servers.alpha]";

        auto lhs = string_fragment::from_str(n1).trim(TOKENS);
        auto rhs = string_fragment::from_str(n2).trim(TOKENS);
        CHECK(strnatcasecmp(lhs.length(), lhs.data(), rhs.length(), rhs.data())
              < 0);
    }

    {
        const std::string a = "10.112.81.15";
        const std::string b = "192.168.202.254";

        int ipcmp = 0;
        auto rc = ipv4cmp(a.length(), a.c_str(), b.length(), b.c_str(), &ipcmp);
        CHECK(rc == 1);
        CHECK(ipcmp == -1);
    }
}

TEST_CASE("last_word_str")
{
    {
        std::string s = "foobar baz";

        auto rc = last_word_str(&s[0], s.length(), 6);
        CHECK(s.length() == rc);
    }
    {
        std::string s = "com.example.foo";

        auto rc = last_word_str(&s[0], s.length(), 6);
        s.resize(rc);
        CHECK(s == "foo");
    }
}
