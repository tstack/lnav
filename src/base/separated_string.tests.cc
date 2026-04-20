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
#include <vector>

#include "separated_string.hh"

#include "doctest/doctest.h"

namespace {

struct parsed_cell {
    std::string value;
    separated_string::cell_kind kind;
};

std::vector<parsed_cell>
tokenize(const std::string& input)
{
    separated_string ss(input.data(), input.length());
    std::vector<parsed_cell> out;
    for (auto iter = ss.begin(); iter != ss.end(); ++iter) {
        out.push_back(parsed_cell{(*iter).to_string(), iter.kind()});
    }
    return out;
}

}  // namespace

TEST_CASE("empty input")
{
    auto cells = tokenize("");
    CHECK(cells.empty());
}

TEST_CASE("plain CSV splits")
{
    auto cells = tokenize("a,b,c");
    REQUIRE(cells.size() == 3);
    CHECK(cells[0].value == "a");
    CHECK(cells[1].value == "b");
    CHECK(cells[2].value == "c");
}

TEST_CASE("classifies integers, floats, empty, other")
{
    auto cells = tokenize("42,3.14,,foo,1e3,+5,-2.5");
    REQUIRE(cells.size() == 7);
    CHECK(cells[0].kind == separated_string::cell_kind::integer);
    CHECK(cells[1].kind == separated_string::cell_kind::floating);
    CHECK(cells[2].kind == separated_string::cell_kind::empty);
    CHECK(cells[3].kind == separated_string::cell_kind::other);
    // Scientific notation counts as floating.
    CHECK(cells[4].kind == separated_string::cell_kind::floating);
    CHECK(cells[5].kind == separated_string::cell_kind::integer);
    CHECK(cells[6].kind == separated_string::cell_kind::floating);
}

TEST_CASE("number_with_suffix: common unit-suffixed shapes")
{
    auto cells = tokenize("20.0KB,12ms,42%,1.5 MB,-3.2 dB,2048 bytes");
    REQUIRE(cells.size() == 6);
    for (const auto& c : cells) {
        CHECK(c.kind == separated_string::cell_kind::number_with_suffix);
    }
}

TEST_CASE("number_with_suffix distinguishes hex-like strings from units")
{
    // `0x1F` is not a number_with_suffix: after the `x` we see a digit
    // which demotes to OTHER.
    auto cells = tokenize("0x1F,42abc123");
    REQUIRE(cells.size() == 2);
    CHECK(cells[0].kind == separated_string::cell_kind::other);
    // `42abc123` also has a digit after the alpha run → OTHER.
    CHECK(cells[1].kind == separated_string::cell_kind::other);
}

TEST_CASE("number_with_suffix rejects trailing content after the unit")
{
    auto cells = tokenize("12 KB extra,42ms something");
    REQUIRE(cells.size() == 2);
    // Space after the unit followed by more non-ws → OTHER.
    CHECK(cells[0].kind == separated_string::cell_kind::other);
    CHECK(cells[1].kind == separated_string::cell_kind::other);
}

TEST_CASE("scientific notation stays floating, not number_with_suffix")
{
    // `e` has exponent priority over the suffix entry.
    auto cells = tokenize("1e5,1.5e-3");
    REQUIRE(cells.size() == 2);
    CHECK(cells[0].kind == separated_string::cell_kind::floating);
    CHECK(cells[1].kind == separated_string::cell_kind::floating);
}

TEST_CASE("non-numeric alpha prefix stays other")
{
    auto cells = tokenize("KB,ms,foo");
    REQUIRE(cells.size() == 3);
    CHECK(cells[0].kind == separated_string::cell_kind::other);
    CHECK(cells[1].kind == separated_string::cell_kind::other);
    CHECK(cells[2].kind == separated_string::cell_kind::other);
}

TEST_CASE("trims surrounding whitespace from unquoted cells")
{
    auto cells = tokenize("  hello  , 12 , 3.5 ,  ");
    REQUIRE(cells.size() == 4);
    CHECK(cells[0].value == "hello");
    CHECK(cells[0].kind == separated_string::cell_kind::other);
    CHECK(cells[1].value == "12");
    CHECK(cells[1].kind == separated_string::cell_kind::integer);
    CHECK(cells[2].value == "3.5");
    CHECK(cells[2].kind == separated_string::cell_kind::floating);
    CHECK(cells[3].value.empty());
    CHECK(cells[3].kind == separated_string::cell_kind::empty);
}

TEST_CASE("unwraps surrounding double-quotes")
{
    auto cells = tokenize(R"("Time","cpu_pct","mem_mb")");
    REQUIRE(cells.size() == 3);
    CHECK(cells[0].value == "Time");
    CHECK(cells[1].value == "cpu_pct");
    CHECK(cells[2].value == "mem_mb");
}

TEST_CASE("separator inside a quoted region does not split the field")
{
    auto cells = tokenize(R"("a,b,c",next)");
    REQUIRE(cells.size() == 2);
    CHECK(cells[0].value == "a,b,c");
    CHECK(cells[1].value == "next");
}

TEST_CASE("CSV-escaped doubled quotes stay inside the field")
{
    // Single quoted field whose contents are: sum(x{k=~"prod"})
    const std::string input = R"csv("sum(x{k=~""prod""})")csv";
    auto cells = tokenize(input);
    REQUIRE(cells.size() == 1);
    // Value preserves the `""`-escaped literals verbatim.
    CHECK(cells[0].value == R"csv(sum(x{k=~""prod""}))csv");
    CHECK(cells[0].kind == separated_string::cell_kind::other);
}

TEST_CASE("numeric value inside quotes is still classified as such")
{
    auto cells = tokenize(R"("123","45.6")");
    REQUIRE(cells.size() == 2);
    CHECK(cells[0].kind == separated_string::cell_kind::integer);
    CHECK(cells[1].kind == separated_string::cell_kind::floating);
}

TEST_CASE("with_separator switches the delimiter")
{
    auto input = "a;b;c"_frag;
    separated_string ss(input);
    ss.with_separator(';');
    std::vector<std::string> cells;
    for (auto iter = ss.begin(); iter != ss.end(); ++iter) {
        cells.push_back((*iter).to_string());
    }
    REQUIRE(cells.size() == 3);
    CHECK(cells[0] == "a");
    CHECK(cells[1] == "b");
    CHECK(cells[2] == "c");
}

TEST_CASE("constructs from string_fragment")
{
    auto sf = string_fragment::from_const("alpha,beta,gamma");
    separated_string ss{sf};
    std::vector<std::string> cells;
    for (auto iter = ss.begin(); iter != ss.end(); ++iter) {
        cells.push_back((*iter).to_string());
    }
    REQUIRE(cells.size() == 3);
    CHECK(cells[0] == "alpha");
    CHECK(cells[1] == "beta");
    CHECK(cells[2] == "gamma");
}

TEST_CASE("tab separator works")
{
    auto input = "a\tb\tc"_frag;
    separated_string ss(input);
    ss.with_separator('\t');
    std::vector<std::string> cells;
    for (auto iter = ss.begin(); iter != ss.end(); ++iter) {
        cells.push_back((*iter).to_string());
    }
    REQUIRE(cells.size() == 3);
    CHECK(cells[0] == "a");
    CHECK(cells[1] == "b");
    CHECK(cells[2] == "c");
}

TEST_CASE("trailing separator emits an empty trailing cell")
{
    // Matches Python csv / awk: N separators produce N+1 fields.
    auto cells = tokenize("a,b,");
    REQUIRE(cells.size() == 3);
    CHECK(cells[0].value == "a");
    CHECK(cells[1].value == "b");
    CHECK(cells[2].value.empty());
    CHECK(cells[2].kind == separated_string::cell_kind::empty);
}

TEST_CASE("lone separator yields two empty cells")
{
    auto cells = tokenize(",");
    REQUIRE(cells.size() == 2);
    CHECK(cells[0].value.empty());
    CHECK(cells[1].value.empty());
    CHECK(cells[0].kind == separated_string::cell_kind::empty);
    CHECK(cells[1].kind == separated_string::cell_kind::empty);
}

TEST_CASE("consecutive separators yield empty cells in between")
{
    auto cells = tokenize(",,");
    REQUIRE(cells.size() == 3);
    for (const auto& c : cells) {
        CHECK(c.value.empty());
        CHECK(c.kind == separated_string::cell_kind::empty);
    }
}

TEST_CASE("leading separator yields an empty leading cell")
{
    auto cells = tokenize(",a,b");
    REQUIRE(cells.size() == 3);
    CHECK(cells[0].value.empty());
    CHECK(cells[1].value == "a");
    CHECK(cells[2].value == "b");
}

TEST_CASE("unterminated quote captures rest of input")
{
    auto cells = tokenize(R"("start,here,no-close)");
    REQUIRE(cells.size() == 1);
    CHECK(cells[0].value == "start,here,no-close");
}

TEST_CASE("newlines inside a quoted cell are preserved verbatim")
{
    auto cells = tokenize("\"line1\nline2\",next");
    REQUIRE(cells.size() == 2);
    CHECK(cells[0].value == "line1\nline2");
    CHECK(cells[0].kind == separated_string::cell_kind::other);
    CHECK(cells[1].value == "next");
}

TEST_CASE("CRLF inside a quoted cell stays inside the field")
{
    auto cells = tokenize("\"a\r\nb\",c");
    REQUIRE(cells.size() == 2);
    CHECK(cells[0].value == "a\r\nb");
    CHECK(cells[1].value == "c");
}

TEST_CASE("unterminated quote with embedded doubled quotes")
{
    // No closing quote — tokenizer should still consume the `""` as an
    // escape and capture the rest of the input (including the unescaped
    // separator) as a single cell.
    auto cells = tokenize(R"("a""b,rest)");
    REQUIRE(cells.size() == 1);
    CHECK(cells[0].value == R"(a""b,rest)");
    CHECK(cells[0].kind == separated_string::cell_kind::other);
}

TEST_CASE("trailing separator after a quoted cell emits empty trailing cell")
{
    auto cells = tokenize(R"("a,b",c,)");
    REQUIRE(cells.size() == 3);
    CHECK(cells[0].value == "a,b");
    CHECK(cells[1].value == "c");
    CHECK(cells[2].value.empty());
    CHECK(cells[2].kind == separated_string::cell_kind::empty);
}

TEST_CASE("leading whitespace before a quoted cell still enters quote mode")
{
    auto cells = tokenize(R"(  "hello",world)");
    REQUIRE(cells.size() == 2);
    CHECK(cells[0].value == "hello");
    CHECK(cells[1].value == "world");
}

TEST_CASE("quoted cell containing only whitespace is classified as empty")
{
    auto cells = tokenize(R"("   ",next)");
    REQUIRE(cells.size() == 2);
    CHECK(cells[0].value == "   ");
    CHECK(cells[0].kind == separated_string::cell_kind::empty);
    CHECK(cells[1].value == "next");
}

TEST_CASE("high-bit UTF-8 bytes in unquoted cells classify as other")
{
    auto cells = tokenize("caf\xc3\xa9,42");
    REQUIRE(cells.size() == 2);
    CHECK(cells[0].value == "caf\xc3\xa9");
    CHECK(cells[0].kind == separated_string::cell_kind::other);
    CHECK(cells[1].kind == separated_string::cell_kind::integer);
}

TEST_CASE("double-quote as the separator treats quotes literally")
{
    separated_string ss{string_fragment::from_const(R"(a"b"c)")};
    ss.with_separator('"');
    std::vector<parsed_cell> cells;
    for (auto iter = ss.begin(); iter != ss.end(); ++iter) {
        cells.push_back(parsed_cell{(*iter).to_string(), iter.kind()});
    }
    REQUIRE(cells.size() == 3);
    CHECK(cells[0].value == "a");
    CHECK(cells[1].value == "b");
    CHECK(cells[2].value == "c");
}

TEST_CASE("unescape_quoted collapses CSV double-quote escapes")
{
    // The primary use case: a PromQL-ish column name whose interior
    // contains CSV-escaped double quotes.
    CHECK(separated_string::unescape_quoted(
              string_fragment::from_const(R"csv(sum(x{k=~""prod""}))csv"))
          == R"csv(sum(x{k=~"prod"}))csv");
    // No escapes → returned verbatim.
    CHECK(separated_string::unescape_quoted(
              string_fragment::from_const("plain_name"))
          == "plain_name");
    // An isolated single `"` is preserved (not an escape pair).
    CHECK(separated_string::unescape_quoted(
              string_fragment::from_const(R"csv(has"quote)csv"))
          == R"csv(has"quote)csv");
    // Empty input → empty output.
    CHECK(separated_string::unescape_quoted(string_fragment::from_const(""))
              .empty());
}

TEST_CASE("iterator index tracks cell position")
{
    auto input = "a,b,c,d"_frag;
    separated_string ss(input);
    size_t n = 0;
    for (auto iter = ss.begin(); iter != ss.end(); ++iter) {
        CHECK(iter.index() == n);
        n += 1;
    }
    CHECK(n == 4);
}
