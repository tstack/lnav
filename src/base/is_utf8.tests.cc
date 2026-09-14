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

#include <string>

#include "base/is_utf8.hh"

#include "config.h"
#include "doctest/doctest.h"

// The scan works in 16-byte chunks, so most of the interesting cases are about
// where a byte falls relative to a chunk boundary.
static constexpr size_t CHUNK = 16;

static utf8_scan_result
scan(const std::string& s, std::optional<unsigned char> term = std::nullopt)
{
    return is_utf8(string_fragment::from_bytes(s.data(), s.size()), term);
}

TEST_CASE("is_utf8: empty and short ascii")
{
    {
        auto res = scan("");
        CHECK(res.is_valid());
        CHECK(res.usr_valid_frag.length() == 0);
        CHECK(!res.usr_remaining.has_value());
        CHECK(res.usr_column_width_guess == 0);
    }

    for (auto len : {size_t{1}, CHUNK - 1, CHUNK, CHUNK + 1, CHUNK * 4 + 5}) {
        const auto s = std::string(len, 'a');
        auto res = scan(s);

        CAPTURE(len);
        CHECK(res.is_valid());
        CHECK((size_t) res.usr_valid_frag.length() == len);
        CHECK(res.usr_column_width_guess == len);
        CHECK(!res.usr_has_ansi);
    }
}

TEST_CASE("is_utf8: printable range boundaries")
{
    // The fast path tests for "printable ASCII" as a range.  Every byte value
    // has to agree with the byte-at-a-time validator about whether it belongs
    // to that range, so run each one through on its own and in a chunk.
    for (int c = 0x20; c <= 0x7E; c++) {
        const auto s = std::string(CHUNK * 2, (char) c);
        auto res = scan(s);

        CAPTURE(c);
        CHECK(res.is_valid());
        CHECK((size_t) res.usr_valid_frag.length() == CHUNK * 2);
    }

    // 0x7F is not printable but is still valid UTF-8, so it must fall out of
    // the fast path and still be accepted.
    {
        auto res = scan(std::string(CHUNK * 2, (char) 0x7F));
        CHECK(res.is_valid());
        CHECK((size_t) res.usr_valid_frag.length() == CHUNK * 2);
    }
}

TEST_CASE("is_utf8: terminator at every position")
{
    for (size_t pos = 0; pos < CHUNK * 2 + 1; pos++) {
        auto s = std::string(CHUNK * 3, 'a');
        s[pos] = '\n';

        auto res = scan(s, '\n');

        CAPTURE(pos);
        CHECK(res.is_valid());
        CHECK((size_t) res.usr_valid_frag.length() == pos);
        REQUIRE(res.usr_remaining.has_value());
        CHECK((size_t) res.usr_remaining->length() == s.size() - pos - 1);
        CHECK(res.remaining_ptr() == s.data() + pos + 1);
    }
}

TEST_CASE("is_utf8: multi-byte sequences across chunk boundaries")
{
    // 2-, 3- and 4-byte sequences, walked across a chunk boundary so that the
    // lead byte lands at every offset around it.
    const std::string seqs[] = {
        "\xC2\xA9",          // (c)
        "\xE4\xB8\xAD",      // CJK
        "\xF0\x9F\x98\x80",  // emoji
    };

    for (const auto& seq : seqs) {
        for (size_t pos = CHUNK - 4; pos < CHUNK + 4; pos++) {
            auto s = std::string(CHUNK * 3, 'a');
            s.replace(pos, seq.size(), seq);

            auto res = scan(s);

            CAPTURE(seq.size());
            CAPTURE(pos);
            CHECK(res.is_valid());
            CHECK((size_t) res.usr_valid_frag.length() == s.size());
            // Every sequence counts as one column, so the guess is short by
            // the extra bytes it occupies.
            CHECK(res.usr_column_width_guess == s.size() - (seq.size() - 1));
        }
    }
}

TEST_CASE("is_utf8: valid prefix stops at the first bad byte")
{
    // The regression this file was added for.  A bad byte early on, followed
    // by enough clean ASCII to fill several chunks: the fast path must not
    // carry the valid prefix past the error.
    auto s = std::string(CHUNK * 6, 'a');
    s[5] = (char) 0xFF;

    auto res = scan(s);

    REQUIRE(!res.is_valid());
    CHECK(res.usr_valid_frag.length() == 5);
}

TEST_CASE("is_utf8: bad byte followed by a terminator")
{
    auto s = std::string(CHUNK * 6, 'a');
    s[5] = (char) 0xFF;
    s[CHUNK * 4] = '\n';

    auto res = scan(s, '\n');

    REQUIRE(!res.is_valid());
    CHECK(res.usr_valid_frag.length() == 5);
    // The scan still has to find the newline for the caller, even though the
    // line it just read was not valid.
    REQUIRE(res.usr_remaining.has_value());
    CHECK(res.remaining_ptr() == s.data() + CHUNK * 4 + 1);
}

TEST_CASE("is_utf8: nul handling depends on the terminator")
{
    auto s = std::string(CHUNK * 3, 'a');
    s[7] = '\0';

    {
        // With no terminator a NUL is an error, which is what the binary-file
        // check in line_buffer relies on.
        auto res = scan(s);
        REQUIRE(!res.is_valid());
        CHECK(std::string(res.usr_message) == "Null bytes are not allowed");
        CHECK(res.usr_valid_frag.length() == 7);
    }

    {
        // ...and when it *is* the terminator, it ends the line instead.
        auto res = scan(s, '\0');
        CHECK(res.is_valid());
        CHECK(res.usr_valid_frag.length() == 7);
        REQUIRE(res.usr_remaining.has_value());
    }
}

TEST_CASE("is_utf8: malformed sequences")
{
    struct {
        const char* name;
        std::string input;
        int valid_len;
    } cases[] = {
        {"bad lead byte", std::string("abc\x80xyz"), 3},
        {"truncated 2-byte at end", std::string("abc\xC2"), 3},
        {"truncated 3-byte at end", std::string("abc\xE4\xB8"), 3},
        {"truncated 4-byte at end", std::string("abc\xF0\x9F\x98"), 3},
        {"bad continuation", std::string("abc\xC2\x20xyz"), 3},
        {"overlong", std::string("abc\xC0\xAFxyz"), 3},
        {"surrogate", std::string("abc\xED\xA0\x80xyz"), 3},
        {"out of range", std::string("abc\xF5\x80\x80\x80xyz"), 3},
    };

    for (const auto& tc : cases) {
        auto res = scan(tc.input);

        CAPTURE(tc.name);
        REQUIRE(!res.is_valid());
        CHECK(res.usr_message != nullptr);
        CHECK(res.usr_valid_frag.length() == tc.valid_len);
    }
}

TEST_CASE("is_utf8: ansi and tabs")
{
    {
        auto res = scan(std::string(CHUNK * 2, 'a') + "\x1b[1m" + "bold");
        CHECK(res.is_valid());
        CHECK(res.usr_has_ansi);
    }
    {
        auto res = scan(std::string(CHUNK * 2, 'a') + "\b");
        CHECK(res.is_valid());
        CHECK(res.usr_has_ansi);
    }
    {
        auto res = scan(std::string("a\tb"));
        CHECK(res.is_valid());
        // one column each for 'a' and 'b', eight for the tab
        CHECK(res.usr_column_width_guess == 10);
    }
}

TEST_CASE("is_utf8: fragment that does not start at zero")
{
    const std::string s = "skipme\xFF" "tail";
    auto whole = string_fragment::from_bytes(s.data(), s.size());
    auto tail = whole.substr(6);

    REQUIRE(tail.sf_begin == 6);

    auto res = is_utf8(tail);

    REQUIRE(!res.is_valid());
    // The valid prefix of *this* fragment is empty; the bad byte is its first.
    CHECK(res.usr_valid_frag.length() == 0);
}
