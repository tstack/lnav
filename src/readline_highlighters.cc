/**
 * Copyright (c) 2014, Timothy Stack
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
 *
 * @file readline_highlighters.cc
 */

#include "readline_highlighters.hh"

#include "base/attr_line.builder.hh"
#include "base/snippet_highlighters.hh"
#include "base/string_util.hh"
#include "config.h"
#include "pcrepp/pcrepp.hh"
#include "shlex.hh"
#include "sql_help.hh"
#include "sql_util.hh"
#include "view_curses.hh"

static void readline_sqlite_highlighter_int(attr_line_t& al,
                                            int x,
                                            line_range sub);

static bool
is_bracket(const std::string& str, int index, bool is_lit)
{
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
                        lpc, VC_STYLE.value(text_attrs{A_BOLD | A_REVERSE}));
                    alb.overlay_attr_for_char(lpc,
                                              VC_ROLE.value(role_t::VCR_OK));
                    break;
                }
                depth -= 1;
            }
        }
    }

    if (line[x] == left && is_bracket(line, x, is_lit)) {
        for (size_t lpc = x + 1; lpc < sub.lr_end; lpc++) {
            if (line[lpc] == left && is_bracket(line, lpc, is_lit)) {
                depth += 1;
            } else if (line[lpc] == right && is_bracket(line, lpc, is_lit)) {
                if (depth == 0) {
                    alb.overlay_attr_for_char(
                        lpc, VC_STYLE.value(text_attrs{A_BOLD | A_REVERSE}));
                    alb.overlay_attr_for_char(lpc,
                                              VC_ROLE.value(role_t::VCR_OK));
                    break;
                }
                depth -= 1;
            }
        }
    }

    nonstd::optional<int> first_left;

    depth = 0;

    for (size_t lpc = sub.lr_start; lpc < sub.lr_end; lpc++) {
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
                    lr, VC_STYLE.value(text_attrs{A_BOLD | A_REVERSE}));
                alb.overlay_attr(lr, VC_ROLE.value(role_t::VCR_ERROR));
            }
        }
    }

    if (depth > 0) {
        auto lr
            = line_range(is_lit ? first_left.value() - 1 : first_left.value(),
                         first_left.value() + 1);
        alb.overlay_attr(lr, VC_STYLE.value(text_attrs{A_BOLD | A_REVERSE}));
        alb.overlay_attr(lr, VC_ROLE.value(role_t::VCR_ERROR));
    }
}

void
readline_regex_highlighter(attr_line_t& al, int x)
{
    lnav::snippets::regex_highlighter(
        al, x, line_range{1, (int) al.get_string().size()});
}

void
readline_command_highlighter_int(attr_line_t& al, int x, line_range sub)
{
    static const pcrepp RE_PREFIXES(
        R"(^:(filter-in|filter-out|delete-filter|enable-filter|disable-filter|highlight|clear-highlight|create-search-table\s+[^\s]+\s+))");
    static const pcrepp SH_PREFIXES(
        "^:(eval|open|append-to|write-to|write-csv-to|write-json-to)");
    static const pcrepp SQL_PREFIXES("^:(filter-expr|mark-expr)");
    static const pcrepp IDENT_PREFIXES("^:(tag|untag|delete-tags)");
    static const pcrepp COLOR_PREFIXES("^:(config)");
    static const pcrepp COLOR_RE("(#(?:[a-fA-F0-9]{6}|[a-fA-F0-9]{3}))");

    attr_line_builder alb(al);
    const auto& line = al.get_string();
    pcre_context_static<30> pc;
    pcre_input pi(&line[sub.lr_start], 0, sub.length());
    size_t ws_index;

    ws_index = line.find(' ', sub.lr_start);
    auto command = line.substr(sub.lr_start, ws_index);
    if (ws_index != std::string::npos) {
        alb.overlay_attr(line_range(sub.lr_start + 1, ws_index),
                         VC_ROLE.value(role_t::VCR_KEYWORD));

        if (RE_PREFIXES.match(pc, pi)) {
            lnav::snippets::regex_highlighter(
                al, x, line_range{(int) ws_index, sub.lr_end});
        }
        pi.reset(&line[sub.lr_start], 0, sub.length());
        if (SH_PREFIXES.match(pc, pi)) {
            readline_shlex_highlighter_int(
                al, x, line_range{(int) ws_index, sub.lr_end});
        }
        pi.reset(&line[sub.lr_start], 0, sub.length());
        if (SQL_PREFIXES.match(pc, pi)) {
            readline_sqlite_highlighter_int(
                al, x, line_range{(int) ws_index, sub.lr_end});
        }
    }
    pi.reset(&line[sub.lr_start], 0, sub.length());
    if (COLOR_PREFIXES.match(pc, pi)) {
        pi.reset(&line[sub.lr_start], 0, sub.length());
        if (COLOR_RE.match(pc, pi)) {
            auto* cap = pc[0];
            auto hash_color = pi.get_substr(cap);

            styling::color_unit::from_str(hash_color)
                .then([&](const auto& rgb_fg) {
                    auto color = view_colors::singleton().match_color(rgb_fg);
                    alb.template overlay_attr(
                        line_range{sub.lr_start + cap->c_begin,
                                   sub.lr_start + cap->c_begin + 1},
                        VC_STYLE.value(text_attrs{
                            A_BOLD,
                            color,
                        }));
                });
        }
    }
    pi.reset(&line[sub.lr_start], 0, sub.length());
    if (IDENT_PREFIXES.match(pc, pi) && ws_index != std::string::npos) {
        size_t start = ws_index, last;

        do {
            for (; start < sub.length() && isspace(line[start]); start++)
                ;
            for (last = start; last < sub.length() && !isspace(line[last]);
                 last++)
                ;
            struct line_range lr {
                (int) start, (int) last
            };

            if (lr.length() > 0 && !lr.contains(x) && !lr.contains(x - 1)) {
                std::string value(lr.substr(line), lr.sublen(line));

                if ((command == ":tag" || command == ":untag"
                     || command == ":delete-tags")
                    && !startswith(value, "#"))
                {
                    value = "#" + value;
                }
                alb.overlay_attr(lr, VC_ROLE.value(role_t::VCR_IDENTIFIER));
            }

            start = last;
        } while (start < sub.length());
    }
}

void
readline_command_highlighter(attr_line_t& al, int x)
{
    readline_command_highlighter_int(
        al, x, line_range{0, (int) al.get_string().length()});
}

static void
readline_sqlite_highlighter_int(attr_line_t& al, int x, line_range sub)
{
    static const char* brackets[] = {
        "[]",
        "()",

        nullptr,
    };

    attr_line_builder alb(al);
    const auto& line = al.get_string();

    auto anno_sql = al.subline(sub.lr_start, sub.length());
    anno_sql.get_attrs().clear();
    annotate_sql_statement(anno_sql);

    for (const auto& attr : anno_sql.al_attrs) {
        line_range lr{
            sub.lr_start + attr.sa_range.lr_start,
            sub.lr_start + attr.sa_range.lr_end,
        };
        if (attr.sa_type == &SQL_COMMAND_ATTR
            || attr.sa_type == &SQL_KEYWORD_ATTR)
        {
            alb.overlay_attr(lr, VC_ROLE.value(role_t::VCR_KEYWORD));
        } else if (attr.sa_type == &SQL_IDENTIFIER_ATTR) {
            if (!attr.sa_range.contains(x) && attr.sa_range.lr_end != x) {
                alb.overlay_attr(lr, VC_ROLE.value(role_t::VCR_IDENTIFIER));
            }
        } else if (attr.sa_type == &SQL_FUNCTION_ATTR) {
            alb.overlay_attr(
                line_range{lr.lr_start, (int) line.find('(', lr.lr_start)},
                VC_ROLE.value(role_t::VCR_SYMBOL));
        } else if (attr.sa_type == &SQL_NUMBER_ATTR) {
            alb.overlay_attr(lr, VC_ROLE.value(role_t::VCR_NUMBER));
        } else if (attr.sa_type == &SQL_STRING_ATTR) {
            if (lr.length() > 1 && al.al_string[lr.lr_end - 1] == '\'') {
                alb.overlay_attr(lr, VC_ROLE.value(role_t::VCR_STRING));
            } else {
                alb.overlay_attr_for_char(
                    lr.lr_start, VC_STYLE.value(text_attrs{A_REVERSE}));
                alb.overlay_attr_for_char(lr.lr_start,
                                          VC_ROLE.value(role_t::VCR_ERROR));
            }
        } else if (attr.sa_type == &SQL_OPERATOR_ATTR) {
            alb.overlay_attr(lr, VC_ROLE.value(role_t::VCR_SYMBOL));
        } else if (attr.sa_type == &SQL_COMMENT_ATTR) {
            alb.overlay_attr(lr, VC_ROLE.value(role_t::VCR_COMMENT));
        }
    }

    for (int lpc = 0; brackets[lpc]; lpc++) {
        find_matching_bracket(al, x, sub, brackets[lpc][0], brackets[lpc][1]);
    }
}

void
readline_sqlite_highlighter(attr_line_t& al, int x)
{
    readline_sqlite_highlighter_int(
        al, x, line_range{0, (int) al.get_string().length()});
}

void
readline_shlex_highlighter_int(attr_line_t& al, int x, line_range sub)
{
    attr_line_builder alb(al);
    const auto& str = al.get_string();
    pcre_context::capture_t cap;
    shlex_token_t token;
    nonstd::optional<int> quote_start;
    shlex lexer(string_fragment{al.al_string.data(), sub.lr_start, sub.lr_end});

    while (lexer.tokenize(cap, token)) {
        switch (token) {
            case shlex_token_t::ST_ERROR:
                alb.overlay_attr(line_range(sub.lr_start + cap.c_begin,
                                            sub.lr_start + cap.c_end),
                                 VC_STYLE.value(text_attrs{A_REVERSE}));
                alb.overlay_attr(line_range(sub.lr_start + cap.c_begin,
                                            sub.lr_start + cap.c_end),
                                 VC_ROLE.value(role_t::VCR_ERROR));
                break;
            case shlex_token_t::ST_TILDE:
            case shlex_token_t::ST_ESCAPE:
                alb.overlay_attr(line_range(sub.lr_start + cap.c_begin,
                                            sub.lr_start + cap.c_end),
                                 VC_ROLE.value(role_t::VCR_SYMBOL));
                break;
            case shlex_token_t::ST_DOUBLE_QUOTE_START:
            case shlex_token_t::ST_SINGLE_QUOTE_START:
                quote_start = sub.lr_start + cap.c_begin;
                break;
            case shlex_token_t::ST_DOUBLE_QUOTE_END:
            case shlex_token_t::ST_SINGLE_QUOTE_END:
                alb.overlay_attr(
                    line_range(quote_start.value(), sub.lr_start + cap.c_end),
                    VC_ROLE.value(role_t::VCR_STRING));
                quote_start = nonstd::nullopt;
                break;
            case shlex_token_t::ST_VARIABLE_REF:
            case shlex_token_t::ST_QUOTED_VARIABLE_REF: {
                int extra = token == shlex_token_t::ST_VARIABLE_REF ? 0 : 1;
                auto ident = str.substr(sub.lr_start + cap.c_begin + 1 + extra,
                                        cap.length() - 1 - extra * 2);
                alb.overlay_attr(
                    line_range(sub.lr_start + cap.c_begin,
                               sub.lr_start + cap.c_begin + 1 + extra),
                    VC_ROLE.value(role_t::VCR_SYMBOL));
                alb.overlay_attr(
                    line_range(sub.lr_start + cap.c_begin + 1 + extra,
                               sub.lr_start + cap.c_end - extra),
                    VC_ROLE.value(x == sub.lr_start + cap.c_end
                                          || cap.contains(x)
                                      ? role_t::VCR_SYMBOL
                                      : role_t::VCR_IDENTIFIER));
                if (extra) {
                    alb.overlay_attr_for_char(
                        sub.lr_start + cap.c_end - 1,
                        VC_ROLE.value(role_t::VCR_SYMBOL));
                }
                break;
            }
            case shlex_token_t::ST_WHITESPACE:
                break;
        }
    }

    if (quote_start) {
        alb.overlay_attr_for_char(quote_start.value(),
                                  VC_ROLE.value(role_t::VCR_ERROR));
    }
}

void
readline_shlex_highlighter(attr_line_t& al, int x)
{
    readline_shlex_highlighter_int(
        al, x, line_range{0, (int) al.al_string.length()});
}

static void
readline_lnav_highlighter_int(attr_line_t& al, int x, line_range sub)
{
    switch (al.al_string[sub.lr_start]) {
        case ':':
            readline_command_highlighter_int(al, x, sub);
            break;
        case ';':
            readline_sqlite_highlighter_int(al,
                                            x,
                                            line_range{
                                                sub.lr_start + 1,
                                                sub.lr_end,
                                            });
            break;
        case '|':
            break;
        case '/':
            lnav::snippets::regex_highlighter(al,
                                              x,
                                              line_range{
                                                  sub.lr_start + 1,
                                                  sub.lr_end,
                                              });
            break;
    }
}

void
readline_lnav_highlighter(attr_line_t& al, int x)
{
    static const pcrepp COMMENT_RE{R"(^\s*#)"};

    attr_line_builder alb(al);
    size_t start = 0, lf_pos;
    nonstd::optional<size_t> section_start;

    while ((lf_pos = al.get_string().find('\n', start)) != std::string::npos) {
        line_range line{(int) start, (int) lf_pos};

        if (line.empty()) {
            start = lf_pos + 1;
            continue;
        }

        pcre_input pi(&al.al_string[line.lr_start], 0, line.length());
        pcre_context_static<30> pc;

        if (COMMENT_RE.match(pc, pi)) {
            if (section_start) {
                readline_lnav_highlighter_int(al,
                                              x,
                                              line_range{
                                                  (int) section_start.value(),
                                                  line.lr_start,
                                              });
                section_start = nonstd::nullopt;
            }
            const auto* cap = pc.all();
            alb.overlay_attr(
                line_range{line.lr_start + cap->c_begin, (int) lf_pos},
                VC_ROLE.value(role_t::VCR_COMMENT));
        } else {
            switch (al.al_string[line.lr_start]) {
                case ':':
                case ';':
                case '|':
                case '/':
                    if (section_start) {
                        readline_lnav_highlighter_int(
                            al,
                            x,
                            line_range{
                                (int) section_start.value(),
                                line.lr_start,
                            });
                    }

                    section_start = line.lr_start;
                    break;
            }
        }

        start = lf_pos;
    }

    if (start == 0) {
        section_start = 0;
    }

    if (section_start) {
        readline_lnav_highlighter_int(al,
                                      x,
                                      line_range{
                                          (int) section_start.value(),
                                          (int) al.al_string.length(),
                                      });
    }
}
