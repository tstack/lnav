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

#include "snippet_highlighters.hh"

#include <cstring>
#include <optional>
#include <vector>

#include <lnav_log.hh>

#include "attr_line.builder.hh"
#include "pcrepp/pcre2pp.hh"

namespace lnav::snippets {

namespace {

struct regex_scan {
    /**
     * True for a character that immediately follows an unescaped '\'
     * outside of a \Q...\E span.  The letter of the closing \E is marked.
     */
    std::vector<bool> rs_escaped;
    /** True for a character in the body of a \Q...\E span. */
    std::vector<bool> rs_quoted;
    /**
     * True for an unescaped character inside a character class, not
     * including the class's own delimiters or a leading '^'.
     */
    std::vector<bool> rs_in_class;
    /**
     * True for a character that has syntactic meaning as a bracket: not
     * escaped, not in a \Q...\E span, and not inside a character class
     * (other than the class's own delimiters).  For \Q and \E, the letter
     * is marked.  A ']' only counts when it closes a class, and braces
     * only count when they delimit a quantifier or the argument of an
     * escape like \x{...}; otherwise PCRE2 treats them as literals.
     */
    std::vector<bool> rs_structural;
};

/**
 * If the "[:" at the given index starts a POSIX class, return the index of
 * the ':' in the closing ":]".  Like PCRE2, the search gives up at a ']' or
 * another "[:", in which case the "[:" is literal.
 */
std::optional<int>
posix_class_end(const std::string& line, line_range sub, int index)
{
    for (auto lpc = index + 2; lpc + 1 < sub.lr_end; lpc++) {
        auto ch = line[lpc];
        auto next = line[lpc + 1];

        if (ch == '\\' && (next == ']' || next == '\\')) {
            lpc += 1;
        } else if ((ch == '[' && next == ':') || ch == ']') {
            return std::nullopt;
        } else if (ch == ':' && next == ']') {
            return lpc;
        }
    }

    return std::nullopt;
}

/**
 * If the '{' at the given index starts a quantifier, as accepted by PCRE2
 * 10.43+ ({n}, {n,}, {n,m}, {,m}, with optional horizontal space), return
 * the index of the closing '}'.
 */
std::optional<int>
quantifier_end(const std::string& line, line_range sub, int index)
{
    auto has_digit = false;
    auto has_comma = false;

    for (auto lpc = index + 1; lpc < sub.lr_end; lpc++) {
        auto ch = line[lpc];

        if (isdigit(ch)) {
            has_digit = true;
        } else if (ch == ' ' || ch == '\t') {
        } else if (ch == ',' && !has_comma) {
            has_comma = true;
        } else if (ch == '}' && has_digit) {
            return lpc;
        } else {
            return std::nullopt;
        }
    }

    return std::nullopt;
}

regex_scan
scan_regex(const std::string& line, line_range sub)
{
    regex_scan retval;
    retval.rs_escaped.resize(line.size() + 1);
    retval.rs_quoted.resize(line.size() + 1);
    retval.rs_in_class.resize(line.size() + 1);
    retval.rs_structural.resize(line.size() + 1);

    auto in_quote = false;
    auto in_class = false;
    auto class_body_start = 0;
    for (auto lpc = sub.lr_start; lpc < sub.lr_end; lpc++) {
        auto ch = line[lpc];
        auto next = lpc + 1 < sub.lr_end ? line[lpc + 1] : '\0';

        if (in_quote) {
            if (ch == '\\' && next == 'E') {
                retval.rs_escaped[lpc + 1] = true;
                retval.rs_structural[lpc + 1] = true;
                in_quote = false;
                lpc += 1;
            } else {
                retval.rs_quoted[lpc] = true;
            }
            continue;
        }
        if (ch == '\\') {
            retval.rs_escaped[lpc + 1] = true;
            if (next == 'Q') {
                in_quote = true;
                retval.rs_structural[lpc + 1] = true;
            } else if (next != '\0' && strchr("xopPgkN", next) != nullptr
                       && lpc + 2 < sub.lr_end && line[lpc + 2] == '{')
            {
                retval.rs_structural[lpc + 2] = true;
                auto close = line.find('}', lpc + 3);
                if (close != std::string::npos && (int) close < sub.lr_end) {
                    retval.rs_structural[close] = true;
                    lpc = close;
                } else {
                    lpc += 2;
                }
                continue;
            } else if (next == 'E') {
                retval.rs_structural[lpc + 1] = true;
            }
            lpc += 1;
            continue;
        }
        if (in_class) {
            if (ch == '[' && next == ':') {
                auto close = posix_class_end(line, sub, lpc);
                if (close) {
                    for (; lpc <= close.value() + 1; lpc++) {
                        retval.rs_in_class[lpc] = true;
                    }
                    lpc -= 1;
                    continue;
                }
            } else if (ch == ']' && lpc > class_body_start) {
                retval.rs_structural[lpc] = true;
                in_class = false;
                continue;
            }
            retval.rs_in_class[lpc] = true;
            continue;
        }
        switch (ch) {
            case '[':
                in_class = true;
                retval.rs_structural[lpc] = true;
                if (next == '^') {
                    lpc += 1;
                }
                class_body_start = lpc + 1;
                break;
            case '(':
            case ')':
                retval.rs_structural[lpc] = true;
                break;
            case '{': {
                auto close = quantifier_end(line, sub, lpc);
                if (close) {
                    retval.rs_structural[lpc] = true;
                    retval.rs_structural[close.value()] = true;
                    lpc = close.value();
                }
                break;
            }
        }
    }

    return retval;
}

}  // namespace

static void
overlay_emphasized(attr_line_builder& alb, line_range lr, role_t role)
{
    alb.overlay_attr(lr,
                     VC_STYLE.value(text_attrs::with_styles(
                         text_attrs::style::bold, text_attrs::style::reverse)));
    alb.overlay_attr(lr, VC_ROLE.value(role));
}

static void
find_matching_bracket(attr_line_t& al,
                      const regex_scan& scan,
                      std::optional<int> x,
                      line_range sub,
                      char left,
                      char right)
{
    // PCRE2 ignores a stray \E and lets a \Q run to the end of the pattern,
    // so those are only paired up for the cursor, never reported.
    bool report_unmatched = (left != 'Q');
    attr_line_builder alb(al);
    const auto& line = al.get_string();
    const auto& structural = scan.rs_structural;

    auto is_left
        = [&](int index) { return line[index] == left && structural[index]; };
    auto is_right
        = [&](int index) { return line[index] == right && structural[index]; };
    auto mark_unmatched = [&](int index) {
        if (report_unmatched) {
            overlay_emphasized(
                alb, line_range(index, index + 1), role_t::VCR_ERROR);
        }
    };

    std::optional<int> partner;
    std::vector<int> open_lefts;

    for (auto lpc = sub.lr_start; lpc < sub.lr_end; lpc++) {
        if (is_left(lpc)) {
            open_lefts.push_back(lpc);
        } else if (is_right(lpc)) {
            if (open_lefts.empty()) {
                mark_unmatched(lpc);
            } else {
                if (x == lpc) {
                    partner = open_lefts.back();
                } else if (x == open_lefts.back()) {
                    partner = lpc;
                }
                open_lefts.pop_back();
            }
        }
    }

    for (const auto index : open_lefts) {
        mark_unmatched(index);
    }

    if (partner) {
        overlay_emphasized(alb,
                           line_range(partner.value(), partner.value() + 1),
                           role_t::VCR_OK);
    }
}

static bool
check_re_prev(const std::string& line,
              const regex_scan& scan,
              line_range sub,
              int x)
{
    if (x <= sub.lr_start || scan.rs_escaped[x - 1]) {
        return false;
    }

    switch (line[x - 1]) {
        case ')':
        case ']':
        case '}':
            return !scan.rs_structural[x - 1];
        case '*':
        case '?':
        case '+':
            return false;
        default:
            return true;
    }
}

static int
is_odigit(int ch)
{
    return '0' <= ch && ch <= '7';
}

/**
 * Count the digits, up to two, that PCRE2 consumes after an escape like
 * \x or \0.
 */
static int
count_digits(const std::string& line,
             line_range sub,
             int index,
             int (*is_digit)(int))
{
    auto retval = 0;

    while (retval < 2 && index + retval < sub.lr_end
           && is_digit((unsigned char) line[index + retval]))
    {
        retval += 1;
    }

    return retval;
}

void
regex_highlighter(attr_line_t& al, std::optional<int> x, line_range sub)
{
    static constexpr const char* brackets[] = {
        "[]",
        "{}",
        "()",
        "QE",

        nullptr,
    };

    const auto& line = al.get_string();
    attr_line_builder alb(al);
    const auto scan = scan_regex(line, sub);
    const auto& escaped = scan.rs_escaped;
    bool in_cap_name = false;

    for (auto lpc = sub.lr_start; lpc < sub.lr_end; lpc++) {
        if (scan.rs_quoted[lpc]) {
            continue;
        }
        if (!escaped[lpc] && !scan.rs_in_class[lpc]) {
            switch (line[lpc]) {
                case '^':
                case '$':
                case '*':
                case '+':
                case '|':
                case '.':
                    alb.overlay_attr_for_char(
                        lpc, VC_ROLE.value(role_t::VCR_RE_SPECIAL));

                    if ((line[lpc] == '*' || line[lpc] == '+')
                        && check_re_prev(line, scan, sub, lpc))
                    {
                        alb.overlay_attr_for_char(
                            lpc - 1, VC_ROLE.value(role_t::VCR_RE_REPEAT));
                    }
                    break;
                case '?': {
                    line_range lr(lpc, lpc + 1);
                    auto next = lpc + 1 < sub.lr_end ? line[lpc + 1] : '\0';

                    if (lpc == sub.lr_start) {
                        overlay_emphasized(
                            alb, line_range(lpc, lpc + 1), role_t::VCR_ERROR);
                    } else if (line[lpc - 1] == '(' && !escaped[lpc - 1]) {
                        switch (next) {
                            case ':':
                            case '!':
                            case '#':
                                lr.lr_end += 1;
                                break;
                        }
                        alb.overlay_attr(lr, VC_ROLE.value(role_t::VCR_OK));
                        if (next == '<') {
                            alb.overlay_attr(
                                line_range(lpc + 1, lpc + 2),
                                VC_ROLE.value(role_t::VCR_RE_SPECIAL));
                            in_cap_name = true;
                        }
                    } else {
                        alb.overlay_attr(lr,
                                         VC_ROLE.value(role_t::VCR_RE_SPECIAL));

                        if (check_re_prev(line, scan, sub, lpc)) {
                            alb.overlay_attr_for_char(
                                lpc - 1, VC_ROLE.value(role_t::VCR_RE_REPEAT));
                        }
                    }
                    break;
                }
                case '>': {
                    if (in_cap_name) {
                        static const auto CAP_RE
                            = lnav::pcre2pp::code::from_const(R"(\?\<\w+$)");

                        auto capture_start
                            = string_fragment::from_str_range(
                                  line, sub.lr_start, lpc)
                                  .find_left_boundary(
                                      lpc - sub.lr_start - 1,
                                      string_fragment::tag1{'('});

                        auto cap_find_res
                            = CAP_RE.find_in(capture_start).ignore_error();

                        if (cap_find_res) {
                            auto id_lr = line_range{
                                cap_find_res->f_all.sf_begin + 2,
                                cap_find_res->f_all.sf_end,
                            };
                            if (!x || !id_lr.contains(x.value())) {
                                alb.overlay_attr(
                                    id_lr,
                                    VC_ROLE.value(role_t::VCR_IDENTIFIER));
                            }
                            alb.overlay_attr(
                                line_range(lpc, lpc + 1),
                                VC_ROLE.value(role_t::VCR_RE_SPECIAL));
                        }
                        in_cap_name = false;
                    }
                    break;
                }

                case '(':
                case ')':
                case '{':
                case '}':
                case '[':
                case ']':
                    if (scan.rs_structural[lpc]) {
                        alb.overlay_attr_for_char(
                            lpc, VC_ROLE.value(role_t::VCR_OK));
                    }
                    break;
            }
        }
        if (escaped[lpc]) {
            switch (line[lpc]) {
                case '\\':
                    alb.overlay_attr(line_range(lpc - 1, lpc + 1),
                                     VC_ROLE.value(role_t::VCR_RE_SPECIAL));
                    break;
                case 'd':
                case 'D':
                case 'h':
                case 'H':
                case 'N':
                case 'R':
                case 's':
                case 'S':
                case 'v':
                case 'V':
                case 'w':
                case 'W':
                case 'X':

                case 'A':
                case 'b':
                case 'B':
                case 'G':
                case 'Z':
                case 'z':
                    alb.overlay_attr(line_range(lpc - 1, lpc + 1),
                                     VC_ROLE.value(role_t::VCR_SYMBOL));
                    break;
                case 'x': {
                    auto digits = lpc + 1 < sub.lr_end && line[lpc + 1] == '{'
                        ? 0
                        : count_digits(line, sub, lpc + 1, isxdigit);
                    alb.overlay_attr(line_range(lpc - 1, lpc + 1 + digits),
                                     VC_ROLE.value(role_t::VCR_RE_SPECIAL));
                    break;
                }
                case '0': {
                    auto digits = count_digits(line, sub, lpc + 1, is_odigit);
                    alb.overlay_attr(line_range(lpc - 1, lpc + 1 + digits),
                                     VC_ROLE.value(role_t::VCR_RE_SPECIAL));
                    break;
                }
                case 'Q':
                case 'E':
                    alb.overlay_attr(line_range(lpc - 1, lpc + 1),
                                     VC_ROLE.value(role_t::VCR_OK));
                    break;
                default:
                    if (isdigit(line[lpc])) {
                        alb.overlay_attr(line_range(lpc - 1, lpc + 1),
                                         VC_ROLE.value(role_t::VCR_RE_SPECIAL));
                    }
                    break;
            }
        }
    }

    for (int lpc = 0; brackets[lpc]; lpc++) {
        find_matching_bracket(
            al, scan, x, sub, brackets[lpc][0], brackets[lpc][1]);
    }
}

}  // namespace lnav::snippets