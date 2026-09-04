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

#include "lnav.console.hh"

#include "config.h"
#include "doctest/doctest.h"

using namespace lnav::console::detail;

TEST_CASE("lnav::console::detail::to_byte_range")
{
    // "“ab" -- the quote is three bytes, so code point 1 is byte 3.
    const auto str = std::string{"“ab"};

    CHECK(str.size() == 5);

    SUBCASE("a byte range is passed through untouched")
    {
        auto lr = line_range{1, 3, line_range::unit::bytes};
        auto byte_lr = to_byte_range(str, lr);

        CHECK(byte_lr.lr_start == 1);
        CHECK(byte_lr.lr_end == 3);
    }

    SUBCASE("a code point range is converted")
    {
        auto lr = line_range{1, 2, line_range::unit::codepoint};
        auto byte_lr = to_byte_range(str, lr);

        // Code point 1 is the 'a' that follows the three-byte quote.
        CHECK(byte_lr.lr_start == 3);
        CHECK(byte_lr.lr_end == 4);
    }

    SUBCASE("an open-ended range keeps its end")
    {
        auto lr = line_range{1, -1, line_range::unit::codepoint};
        auto byte_lr = to_byte_range(str, lr);

        CHECK(byte_lr.lr_start == 3);
        CHECK(byte_lr.lr_end == -1);
    }

    SUBCASE("the two units differ, which is the bug this guards")
    {
        auto as_bytes
            = to_byte_range(str, line_range{1, 2, line_range::unit::bytes});
        auto as_points
            = to_byte_range(str, line_range{1, 2, line_range::unit::codepoint});

        CHECK(as_bytes.lr_start != as_points.lr_start);
    }

    SUBCASE("an ascii-only line is the same under either unit")
    {
        const auto ascii = std::string{"hello"};
        auto as_bytes
            = to_byte_range(ascii, line_range{1, 3, line_range::unit::bytes});
        auto as_points = to_byte_range(
            ascii, line_range{1, 3, line_range::unit::codepoint});

        CHECK(as_bytes.lr_start == as_points.lr_start);
        CHECK(as_bytes.lr_end == as_points.lr_end);
    }
}

TEST_CASE("lnav::console::detail::to_byte_range past the end")
{
    // A range can reach past the line it was built for; the walk must stop at
    // the end of the string rather than reading past it.
    const auto str = std::string{"“ab"};

    auto lr = line_range{99, 200, line_range::unit::codepoint};
    auto byte_lr = to_byte_range(str, lr);

    CHECK(byte_lr.lr_start == (int) str.size());
    CHECK(byte_lr.lr_end == (int) str.size());

    CHECK(to_byte_range(std::string{}, lr).lr_start == 0);
}
