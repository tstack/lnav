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

#include "attr_line.builder.hh"
#include "pcrepp/pcre2pp.hh"

namespace lnav::snippets {

static bool
is_bracket(const std::string& str, int index, bool is_lit)
{
    if (index == 0) {
        return true;
    }

    if (is_lit && str[index - 1] == '\\') {
        return true;
    }
    if (!is_lit && str[index - 1] != '\\') {
        return true;
    }
    return false;
}

static void
find_matching_bracket(
    attr_line_t& al, int x, line_range sub, char left, char right)
{
    bool is_lit = (left == 'Q');
    attr_line_builder alb(al);
    const auto& line = al.get_string();
    int depth = 0;

    if (x < sub.lr_start || x > sub.lr_end) {
        return;
    }

    if (line[x] == right && is_bracket(line, x, is_lit)) {
        for (int lpc = x - 1; lpc >= sub.lr_start; lpc--) {
            if (line[lpc] == right && is_bracket(line, lpc, is_lit)) {
                depth += 1;
            } else if (line[lpc] == left && is_bracket(line, lpc, is_lit)) {
                if (depth == 0) {
                    alb.overlay_attr_for_char(
                        lpc,
                        VC_STYLE.value(text_attrs::with_styles(
                            text_attrs::style::bold,
                            text_attrs::style::reverse)));
                    alb.overlay_attr_for_char(lpc,
                                              VC_ROLE.value(role_t::VCR_OK));
                    break;
                }
                depth -= 1;
            }
        }
    }

    if (line[x] == left && is_bracket(line, x, is_lit)) {
        for (int lpc = x + 1; lpc < sub.lr_end; lpc++) {
            if (line[lpc] == left && is_bracket(line, lpc, is_lit)) {
                depth += 1;
            } else if (line[lpc] == right && is_bracket(line, lpc, is_lit)) {
                if (depth == 0) {
                    alb.overlay_attr_for_char(
                        lpc,
                        VC_STYLE.value(text_attrs::with_styles(
                            text_attrs::style::bold,
                            text_attrs::style::reverse)));
                    alb.overlay_attr_for_char(lpc,
                                              VC_ROLE.value(role_t::VCR_OK));
                    break;
                }
                depth -= 1;
            }
        }
    }

    std::optional<int> first_left;

    depth = 0;

    for (auto lpc = sub.lr_start; lpc < sub.lr_end; lpc++) {
        if (line[lpc] == left && is_bracket(line, lpc, is_lit)) {
            depth += 1;
            if (!first_left) {
                first_left = lpc;
            }
        } else if (line[lpc] == right && is_bracket(line, lpc, is_lit)) {
            if (depth > 0) {
                depth -= 1;
            } else {
                auto lr = line_range(is_lit ? lpc - 1 : lpc, lpc + 1);
                alb.overlay_attr(
                    lr,
                    VC_STYLE.value(text_attrs::with_styles(
                        text_attrs::style::bold, text_attrs::style::reverse)));
                alb.overlay_attr(lr, VC_ROLE.value(role_t::VCR_ERROR));
            }
        }
    }

    if (depth > 0) {
        auto lr
            = line_range(is_lit ? first_left.value() - 1 : first_left.value(),
                         first_left.value() + 1);
        alb.overlay_attr(
            lr,
            VC_STYLE.value(text_attrs::with_styles(
                text_attrs::style::bold, text_attrs::style::reverse)));
        alb.overlay_attr(lr, VC_ROLE.value(role_t::VCR_ERROR));
    }
}

static bool
check_re_prev(const std::string& line, int x)
{
    bool retval = false;

    if ((x > 0 && line[x - 1] != ')' && line[x - 1] != ']' && line[x - 1] != '*'
         && line[x - 1] != '?' && line[x - 1] != '+')
        && (x < 2 || line[x - 2] != '\\'))
    {
        retval = true;
    }

    return retval;
}

static char
safe_read(const std::string& str, std::string::size_type index)
{
    if (index < str.length()) {
        return str.at(index);
    }

    return 0;
}

void
regex_highlighter(attr_line_t& al, std::optional<int> x, line_range sub)
{
    static const char* brackets[] = {
        "[]",
        "{}",
        "()",
        "QE",

        nullptr,
    };

    const auto& line = al.get_string();
    attr_line_builder alb(al);
    bool backslash_is_quoted = false;

    for (auto lpc = sub.lr_start; lpc < sub.lr_end; lpc++) {
        if (lpc == 0 || line[lpc - 1] != '\\') {
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
                        && check_re_prev(line, lpc))
                    {
                        alb.overlay_attr_for_char(
                            lpc - 1, VC_ROLE.value(role_t::VCR_RE_REPEAT));
                    }
                    break;
                case '?': {
                    line_range lr(lpc, lpc + 1);

                    if (lpc == sub.lr_start || (lpc - sub.lr_start) == 0) {
                        alb.overlay_attr_for_char(
                            lpc,
                            VC_STYLE.value(text_attrs::with_styles(
                                text_attrs::style::bold,
                                text_attrs::style::reverse)));
                        alb.overlay_attr_for_char(
                            lpc, VC_ROLE.value(role_t::VCR_ERROR));
                    } else if (line[lpc - 1] == '(') {
                        switch (line[lpc + 1]) {
                            case ':':
                            case '!':
                            case '#':
                                lr.lr_end += 1;
                                break;
                        }
                        alb.overlay_attr(lr, VC_ROLE.value(role_t::VCR_OK));
                        if (line[lpc + 1] == '<') {
                            alb.overlay_attr(
                                line_range(lpc + 1, lpc + 2),
                                VC_ROLE.value(role_t::VCR_RE_SPECIAL));
                        }
                    } else {
                        alb.overlay_attr(lr,
                                         VC_ROLE.value(role_t::VCR_RE_SPECIAL));

                        if (check_re_prev(line, lpc)) {
                            alb.overlay_attr_for_char(
                                lpc - 1, VC_ROLE.value(role_t::VCR_RE_REPEAT));
                        }
                    }
                    break;
                }
                case '>': {
                    static const auto CAP_RE
                        = lnav::pcre2pp::code::from_const(R"(\(\?\<\w+$)");

                    auto capture_start
                        = string_fragment::from_str_range(
                              line, sub.lr_start, lpc)
                              .find_left_boundary(lpc - sub.lr_start - 1,
                                                  string_fragment::tag1{'('});

                    auto cap_find_res
                        = CAP_RE.find_in(capture_start).ignore_error();

                    if (cap_find_res) {
                        alb.overlay_attr(
                            line_range(capture_start.sf_begin
                                           + cap_find_res->f_all.sf_begin + 3,
                                       capture_start.sf_begin
                                           + cap_find_res->f_all.sf_end),
                            VC_ROLE.value(role_t::VCR_IDENTIFIER));
                        alb.overlay_attr(line_range(lpc, lpc + 1),
                                         VC_ROLE.value(role_t::VCR_RE_SPECIAL));
                    }
                    break;
                }

                case '(':
                case ')':
                case '{':
                case '}':
                case '[':
                case ']':
                    alb.overlay_attr_for_char(lpc,
                                              VC_ROLE.value(role_t::VCR_OK));
                    break;
            }
        }
        if (lpc > 0 && line[lpc - 1] == '\\') {
            if (backslash_is_quoted) {
                backslash_is_quoted = false;
                continue;
            }
            switch (line[lpc]) {
                case '\\':
                    backslash_is_quoted = true;
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
                case ' ':
                    alb.overlay_attr(line_range(lpc - 1, lpc + 1),
                                     VC_STYLE.value(text_attrs::with_styles(
                                         text_attrs::style::bold,
                                         text_attrs::style::reverse)));
                    alb.overlay_attr(line_range(lpc - 1, lpc + 1),
                                     VC_ROLE.value(role_t::VCR_ERROR));
                    break;
                case '0':
                case 'x':
                    if (safe_read(line, lpc + 1) == '{') {
                        alb.overlay_attr(line_range(lpc - 1, lpc + 1),
                                         VC_ROLE.value(role_t::VCR_RE_SPECIAL));
                    } else if (isdigit(safe_read(line, lpc + 1))
                               && isdigit(safe_read(line, lpc + 2)))
                    {
                        alb.overlay_attr(line_range(lpc - 1, lpc + 3),
                                         VC_ROLE.value(role_t::VCR_RE_SPECIAL));
                    } else {
                        alb.overlay_attr(line_range(lpc - 1, lpc + 1),
                                         VC_STYLE.value(text_attrs::with_styles(
                                             text_attrs::style::bold,
                                             text_attrs::style::reverse)));
                        alb.overlay_attr(line_range(lpc - 1, lpc + 1),
                                         VC_ROLE.value(role_t::VCR_ERROR));
                    }
                    break;
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
            al, x.value_or(0), sub, brackets[lpc][0], brackets[lpc][1]);
    }
}

}  // namespace lnav::snippets