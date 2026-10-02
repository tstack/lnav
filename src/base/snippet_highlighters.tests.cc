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

#include <optional>
#include <vector>

#include "snippet_highlighters.hh"

#include "config.h"
#include "doctest/doctest.h"

namespace {

attr_line_t
highlight(const char* re, std::optional<int> x = std::nullopt, int start = 0)
{
    attr_line_t retval(re);

    lnav::snippets::regex_highlighter(
        retval, x, line_range{start, (int) retval.length()});
    return retval;
}

std::vector<line_range>
ranges_with_role(const attr_line_t& al, role_t role)
{
    std::vector<line_range> retval;

    for (const auto& sa : al.get_attrs()) {
        if (sa.sa_type == &VC_ROLE && sa.sa_value.get<role_t>() == role) {
            retval.emplace_back(sa.sa_range);
        }
    }
    return retval;
}

std::vector<line_range>
error_ranges(const attr_line_t& al)
{
    return ranges_with_role(al, role_t::VCR_ERROR);
}

std::vector<line_range>
styled_ranges(const attr_line_t& al)
{
    std::vector<line_range> retval;

    for (const auto& sa : al.get_attrs()) {
        if (sa.sa_type == &VC_STYLE) {
            retval.emplace_back(sa.sa_range);
        }
    }
    return retval;
}

bool
has_role_at(const attr_line_t& al, role_t role, int index)
{
    for (const auto& lr : ranges_with_role(al, role)) {
        if (lr.contains(index)) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("regex_highlighter unmatched brackets")
{
    CHECK(error_ranges(highlight("(a")) == std::vector{line_range{0, 1}});
    CHECK(error_ranges(highlight("a)")) == std::vector{line_range{1, 2}});
    CHECK(error_ranges(highlight("[abc")) == std::vector{line_range{0, 1}});
    CHECK(error_ranges(highlight("(a)")).empty());
    CHECK(error_ranges(highlight("(a)(b")) == std::vector{line_range{3, 4}});
    CHECK(error_ranges(highlight("((a)")) == std::vector{line_range{0, 1}});
    CHECK(error_ranges(highlight("(a(b"))
          == std::vector{line_range{0, 1}, line_range{2, 3}});
}

TEST_CASE("regex_highlighter unpaired quote markers")
{
    CHECK(error_ranges(highlight("\\Qabc")).empty());
    CHECK(error_ranges(highlight("\\Qa\\E\\Qb")).empty());
    CHECK(error_ranges(highlight("a\\E")).empty());
    CHECK(error_ranges(highlight("\\Qa\\E\\E")).empty());

    // The rest of the pattern stays literal after an unterminated \Q.
    CHECK(error_ranges(highlight("\\Q(a")).empty());
    CHECK(!has_role_at(highlight("\\Q.*"), role_t::VCR_RE_SPECIAL, 2));

    CHECK(styled_ranges(highlight("\\Qa\\E", 1))
          == std::vector{line_range{4, 5}});
    CHECK(styled_ranges(highlight("\\Qa\\E", 4))
          == std::vector{line_range{1, 2}});
    CHECK(styled_ranges(highlight("\\Qabc", 1)).empty());
}

TEST_CASE("regex_highlighter brackets in a character class")
{
    CHECK(error_ranges(highlight("[(]")).empty());
    CHECK(error_ranges(highlight("[)]")).empty());
    CHECK(error_ranges(highlight("[{]")).empty());
    CHECK(error_ranges(highlight("[]a]")).empty());
    CHECK(error_ranges(highlight("[^]a]")).empty());
    CHECK(error_ranges(highlight("[a\\]]")).empty());
    CHECK(error_ranges(highlight("[[:alpha:]]")).empty());
    CHECK(error_ranges(highlight("[[:alpha:](]")).empty());
    CHECK(error_ranges(highlight("[[:]x:]")).empty());
    CHECK(error_ranges(highlight("[[:[:alpha:]]")).empty());

    auto ipv6 = highlight("[[:]?[0-9a-f:]+");
    CHECK(error_ranges(ipv6).empty());
    CHECK(has_role_at(ipv6, role_t::VCR_OK, 0));
    CHECK(has_role_at(ipv6, role_t::VCR_OK, 3));
    CHECK(has_role_at(ipv6, role_t::VCR_RE_SPECIAL, 4));
    CHECK(has_role_at(ipv6, role_t::VCR_OK, 5));
    CHECK(has_role_at(ipv6, role_t::VCR_OK, 13));
    CHECK(has_role_at(ipv6, role_t::VCR_RE_SPECIAL, 14));
    CHECK(error_ranges(highlight("([)])")).empty());
    CHECK(error_ranges(highlight("[(]+)")) == std::vector{line_range{4, 5}});
}

TEST_CASE("regex_highlighter brackets in a quoted span")
{
    CHECK(error_ranges(highlight("\\Q(\\E")).empty());
    CHECK(error_ranges(highlight("\\Q[\\E")).empty());
    CHECK(error_ranges(highlight("\\Q\\Q\\E")).empty());
    CHECK(error_ranges(highlight("(\\Q)\\E)")).empty());
}

TEST_CASE("regex_highlighter escaped backslash")
{
    CHECK(error_ranges(highlight("(\\\\)")).empty());
    CHECK(error_ranges(highlight("\\\\(")) == std::vector{line_range{2, 3}});
    CHECK(error_ranges(highlight("\\\\\\(")).empty());

    auto star = highlight("a\\\\*");
    CHECK(has_role_at(star, role_t::VCR_RE_SPECIAL, 3));

    auto digit = highlight("\\\\d");
    CHECK(!has_role_at(digit, role_t::VCR_SYMBOL, 2));

    auto esc_digit = highlight("\\\\\\d");
    CHECK(has_role_at(esc_digit, role_t::VCR_SYMBOL, 3));
}

TEST_CASE("regex_highlighter cursor bracket matching")
{
    CHECK(styled_ranges(highlight("(abc)")).empty());
    CHECK(styled_ranges(highlight("[abc]")).empty());

    CHECK(styled_ranges(highlight("(abc)", 0))
          == std::vector{line_range{4, 5}});
    CHECK(styled_ranges(highlight("(abc)", 4))
          == std::vector{line_range{0, 1}});
    CHECK(styled_ranges(highlight("(abc)", 2)).empty());
    CHECK(styled_ranges(highlight("(a(b)c)", 0))
          == std::vector{line_range{6, 7}});
    CHECK(styled_ranges(highlight("(a[)]b)", 0))
          == std::vector{line_range{6, 7}});
    CHECK(styled_ranges(highlight("[(]", 1)).empty());
}

TEST_CASE("regex_highlighter quoted span is literal")
{
    auto dot_star = highlight("\\Q.*\\E");
    CHECK(!has_role_at(dot_star, role_t::VCR_RE_SPECIAL, 2));
    CHECK(!has_role_at(dot_star, role_t::VCR_RE_SPECIAL, 3));
    CHECK(!has_role_at(dot_star, role_t::VCR_RE_REPEAT, 2));

    auto esc_d = highlight("\\Qa\\d\\E");
    CHECK(!has_role_at(esc_d, role_t::VCR_SYMBOL, 4));

    auto esc_bs = highlight("\\Qa\\\\E");
    CHECK(!has_role_at(esc_bs, role_t::VCR_RE_SPECIAL, 3));
    CHECK(has_role_at(esc_bs, role_t::VCR_OK, 4));
    CHECK(has_role_at(esc_bs, role_t::VCR_OK, 5));
}

TEST_CASE("regex_highlighter character class is literal")
{
    auto dot_star = highlight("[.*]");
    CHECK(!has_role_at(dot_star, role_t::VCR_RE_SPECIAL, 1));
    CHECK(!has_role_at(dot_star, role_t::VCR_RE_SPECIAL, 2));
    CHECK(!has_role_at(dot_star, role_t::VCR_RE_REPEAT, 1));

    auto parens = highlight("[()]");
    CHECK(!has_role_at(parens, role_t::VCR_OK, 1));
    CHECK(!has_role_at(parens, role_t::VCR_OK, 2));
    CHECK(has_role_at(parens, role_t::VCR_OK, 0));
    CHECK(has_role_at(parens, role_t::VCR_OK, 3));

    auto esc_d = highlight("[\\d]");
    CHECK(has_role_at(esc_d, role_t::VCR_SYMBOL, 2));

    CHECK(has_role_at(highlight("[^a]"), role_t::VCR_RE_SPECIAL, 1));
    CHECK(!has_role_at(highlight("[a^]"), role_t::VCR_RE_SPECIAL, 2));
    CHECK(!has_role_at(highlight("[^^]"), role_t::VCR_RE_SPECIAL, 2));
}

TEST_CASE("regex_highlighter repeat")
{
    CHECK(!has_role_at(
        highlight("/*a", std::nullopt, 1), role_t::VCR_RE_REPEAT, 0));

    CHECK(has_role_at(highlight("a*"), role_t::VCR_RE_REPEAT, 0));
    CHECK(has_role_at(highlight("\\\\a*"), role_t::VCR_RE_REPEAT, 2));
    CHECK(!has_role_at(highlight("\\d*"), role_t::VCR_RE_REPEAT, 1));
}

TEST_CASE("regex_highlighter escaped paren before question mark")
{
    auto al = highlight("\\(?");
    CHECK(has_role_at(al, role_t::VCR_RE_SPECIAL, 2));
    CHECK(!has_role_at(al, role_t::VCR_OK, 2));

    CHECK(has_role_at(highlight("(?:a)"), role_t::VCR_OK, 2));
}

TEST_CASE("regex_highlighter with a prefix")
{
    CHECK(error_ranges(highlight(":filter-in (a", std::nullopt, 10))
          == std::vector{line_range{11, 12}});
    CHECK(error_ranges(highlight(")(a)", std::nullopt, 1)).empty());
    CHECK(error_ranges(highlight("[(a)]", std::nullopt, 1)).empty());
    CHECK(error_ranges(highlight("/?a", std::nullopt, 1))
          == std::vector{line_range{1, 2}});

    CHECK(styled_ranges(highlight("/(a)", 1, 1))
          == std::vector{line_range{3, 4}});
    CHECK(styled_ranges(highlight("/(a)", 0, 1)).empty());
}

TEST_CASE("regex_highlighter literal braces and brackets")
{
    CHECK(error_ranges(highlight("{\"level\"")).empty());
    CHECK(error_ranges(highlight("a}")).empty());
    CHECK(error_ranges(highlight("a]")).empty());
    CHECK(error_ranges(highlight("a{x}")).empty());
    CHECK(error_ranges(highlight("a{,}")).empty());
    CHECK(!has_role_at(highlight("{\"level\""), role_t::VCR_OK, 0));
    CHECK(!has_role_at(highlight("a]"), role_t::VCR_OK, 1));
    CHECK(!has_role_at(highlight("a{x}"), role_t::VCR_OK, 1));

    for (const auto* re : {"a{2}", "a{2,}", "a{2,3}", "a{,3}", "a{ 2 , 4 }"}) {
        INFO(re);
        auto al = highlight(re);
        CHECK(error_ranges(al).empty());
        CHECK(has_role_at(al, role_t::VCR_OK, 1));
    }

    CHECK(styled_ranges(highlight("a{2,3}", 1))
          == std::vector{line_range{5, 6}});
}

TEST_CASE("regex_highlighter escape braces")
{
    CHECK(error_ranges(highlight("\\x{41}")).empty());
    CHECK(error_ranges(highlight("\\p{Lu}")).empty());
    CHECK(error_ranges(highlight("\\x{41")) == std::vector{line_range{2, 3}});
    CHECK(styled_ranges(highlight("\\p{Lu}", 2))
          == std::vector{line_range{5, 6}});
}

TEST_CASE("regex_highlighter repeat after a literal bracket")
{
    CHECK(has_role_at(highlight("a]*"), role_t::VCR_RE_REPEAT, 1));
    CHECK(has_role_at(highlight("a}+"), role_t::VCR_RE_REPEAT, 1));
    CHECK(!has_role_at(highlight("a{2}+"), role_t::VCR_RE_REPEAT, 3));
    CHECK(!has_role_at(highlight("[a]*"), role_t::VCR_RE_REPEAT, 2));
}

TEST_CASE("regex_highlighter hex and octal escapes")
{
    for (const auto* re : {"\\x", "\\xA", "\\x41", "\\x{41}", "\\0", "\\07",
                           "\\012", "\\08"})
    {
        INFO(re);
        CHECK(error_ranges(highlight(re)).empty());
    }

    CHECK(ranges_with_role(highlight("\\x"), role_t::VCR_RE_SPECIAL)
          == std::vector{line_range{0, 2}});
    CHECK(ranges_with_role(highlight("\\xAg"), role_t::VCR_RE_SPECIAL)
          == std::vector{line_range{0, 3}});
    CHECK(ranges_with_role(highlight("\\x414"), role_t::VCR_RE_SPECIAL)
          == std::vector{line_range{0, 4}});
    CHECK(ranges_with_role(highlight("\\0"), role_t::VCR_RE_SPECIAL)
          == std::vector{line_range{0, 2}});
    CHECK(ranges_with_role(highlight("\\0123"), role_t::VCR_RE_SPECIAL)
          == std::vector{line_range{0, 4}});
    CHECK(ranges_with_role(highlight("\\08"), role_t::VCR_RE_SPECIAL)
          == std::vector{line_range{0, 2}});

    auto al = attr_line_t("\\x4 1");
    lnav::snippets::regex_highlighter(al, std::nullopt, line_range{0, 3});
    CHECK(ranges_with_role(al, role_t::VCR_RE_SPECIAL)
          == std::vector{line_range{0, 3}});
}

TEST_CASE("regex_highlighter escaped space")
{
    CHECK(error_ranges(highlight("a\\ b")).empty());
    CHECK(styled_ranges(highlight("a\\ b")).empty());
    CHECK(error_ranges(highlight("[\\ ]")).empty());
}
