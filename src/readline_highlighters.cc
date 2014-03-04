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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file readline_highlighters.cc
 */

#include "config.h"

#include "pcrepp.hh"
#include "sql_util.hh"
#include "readline_highlighters.hh"

using namespace std;

static bool check_re_prev(const string &line, int x)
{
    bool retval = false;

    if ((x > 0 && line[x - 1] != ')' && line[x - 1] != ']') &&
        (x < 2 || line[x - 2] != '\\')) {
        retval = true;
    }

    return retval;
}

static void find_matching_bracket(attr_line_t &al, int x, char left, char right)
{
    static int matching_bracket_attrs = (
        A_BOLD|A_REVERSE|view_colors::ansi_color_pair(COLOR_GREEN, COLOR_BLACK));
    static int missing_bracket_attrs = (
        A_BOLD|A_REVERSE|view_colors::ansi_color_pair(COLOR_RED, COLOR_BLACK));

    const string &line = al.get_string();

    if (line[x] == right && line[x - 1] != '\\') {
        for (int lpc = x; lpc > 0; lpc--) {
            if (line[lpc] == left && line[lpc - 1] != '\\') {
                al.get_attrs().push_back(string_attr(
                    line_range(lpc, lpc + 1),
                    &view_curses::VC_STYLE,
                    matching_bracket_attrs));
                break;
            }
        }
    }

    if (line[x] == left && line[x - 1] != '\\') {
        for (int lpc = x; lpc < line.length(); lpc++) {
            if (line[lpc] == right && line[lpc - 1] != '\\') {
                al.get_attrs().push_back(string_attr(
                    line_range(lpc, lpc + 1),
                    &view_curses::VC_STYLE,
                    matching_bracket_attrs));
                break;
            }
        }
    }

    int depth = 0, last_left = -1;

    for (int lpc = 1; lpc < line.length(); lpc++) {
        if (line[lpc] == left && line[lpc - 1] != '\\') {
            depth += 1;
            last_left = lpc;
        }
        else if (line[lpc] == right && line[lpc - 1] != '\\') {
            if (depth > 0) {
                depth -= 1;
            }
            else {
                al.get_attrs().push_back(string_attr(
                    line_range(lpc, lpc + 1),
                    &view_curses::VC_STYLE,
                    missing_bracket_attrs));
            }
        }
    }

    if (depth > 0) {
        al.get_attrs().push_back(string_attr(
            line_range(last_left, last_left + 1),
            &view_curses::VC_STYLE,
            missing_bracket_attrs));
    }
}

void readline_regex_highlighter(attr_line_t &al, int x)
{
    static int special_char = (
        A_BOLD|view_colors::ansi_color_pair(COLOR_CYAN, COLOR_BLACK));
    static int class_attrs = (
        A_BOLD|view_colors::ansi_color_pair(COLOR_MAGENTA, COLOR_BLACK));
    static int repeated_char_attrs = (
        view_colors::ansi_color_pair(COLOR_YELLOW, COLOR_BLACK));
    static int bracket_attrs = (
        view_colors::ansi_color_pair(COLOR_GREEN, COLOR_BLACK));
    static int error_attrs = (
        A_BOLD|A_REVERSE|view_colors::ansi_color_pair(COLOR_RED, COLOR_BLACK));

    static const char *brackets[] = {
        "[]",
        "{}",
        "()",

        NULL
    };

    string &line = al.get_string();

    for (int lpc = 1; lpc < line.length(); lpc++) {
        if (line[lpc - 1] != '\\') {
            switch (line[lpc]) {
            case '^':
            case '$':
            case '*':
            case '+':
            case '|':
            case '.':
                al.get_attrs().push_back(string_attr(
                    line_range(lpc, lpc + 1),
                    &view_curses::VC_STYLE,
                    special_char));

                if ((line[lpc] == '*' || line[lpc] == '+') &&
                    check_re_prev(line, lpc)) {
                    al.get_attrs().push_back(string_attr(
                        line_range(lpc - 1, lpc),
                        &view_curses::VC_STYLE,
                        repeated_char_attrs));
                }
                break;
            case '?': {
                struct line_range lr(lpc, lpc + 1);

                if (line[lpc - 1] == '(') {
                    switch (line[lpc + 1]) {
                    case ':':
                    case '!':
                    case '>':
                    case '<':
                    case '#':
                        lr.lr_end += 1;
                        break;
                    }
                    al.get_attrs().push_back(string_attr(
                        lr,
                        &view_curses::VC_STYLE,
                        bracket_attrs));
                }
                else {
                    al.get_attrs().push_back(string_attr(
                        lr,
                        &view_curses::VC_STYLE,
                        special_char));

                    if (check_re_prev(line, lpc)) {
                        al.get_attrs().push_back(string_attr(
                            line_range(lpc - 1, lpc),
                            &view_curses::VC_STYLE,
                            repeated_char_attrs));
                    }
                }
                break;
            }

            case '(':
            case ')':
            case '{':
            case '}':
            case '[':
            case ']':
                al.get_attrs().push_back(string_attr(
                    line_range(lpc, lpc + 1),
                    &view_curses::VC_STYLE,
                    bracket_attrs));
                break;

            }
        }
        if (line[lpc - 1] == '\\') {
            switch (line[lpc]) {
            case 'A':
            case 'b':
            case 'w':
            case 'W':
            case 's':
            case 'S':
            case 'd':
            case 'D':
            case 'Z':
            case 'z':
                al.get_attrs().push_back(string_attr(
                    line_range(lpc - 1, lpc + 1),
                    &view_curses::VC_STYLE,
                    class_attrs));
                break;
            case ' ':
                al.get_attrs().push_back(string_attr(
                    line_range(lpc - 1, lpc + 1),
                    &view_curses::VC_STYLE,
                    error_attrs));
                break;
            default:
                if (isdigit(line[lpc])) {
                    al.get_attrs().push_back(string_attr(
                        line_range(lpc - 1, lpc + 1),
                        &view_curses::VC_STYLE,
                        special_char));
                }
                break;
            }
        }
    }

    for (int lpc = 0; brackets[lpc]; lpc++) {
        find_matching_bracket(al, x, brackets[lpc][0], brackets[lpc][1]);
    }
}

static string sql_keyword_re(void)
{
    string retval = "(?:";

    for (int lpc = 0; sql_keywords[lpc]; lpc++) {
        if (lpc > 0) {
            retval.append("|");
        }
        retval.append("\\b");
        retval.append(sql_keywords[lpc]);
        retval.append("\\b");
    }
    retval += ")";

    return retval;
}

void readline_sqlite_highlighter(attr_line_t &al, int x)
{
    static int keyword_attrs = (
        A_BOLD|view_colors::ansi_color_pair(COLOR_CYAN, COLOR_BLACK));
    static int symbol_attrs = (
        view_colors::ansi_color_pair(COLOR_MAGENTA, COLOR_BLACK));
    static int string_attrs = (
        view_colors::ansi_color_pair(COLOR_GREEN, COLOR_BLACK));
    static int error_attrs = (
        A_BOLD|A_REVERSE|view_colors::ansi_color_pair(COLOR_RED, COLOR_BLACK));

    static string keyword_re_str = sql_keyword_re();
    static pcrepp keyword_pcre(keyword_re_str.c_str(), PCRE_CASELESS);
    static pcrepp string_literal_pcre("'[^']*('(?:'[^']*')*|$)");
    static pcrepp ident_pcre("(\\b[a-z_]\\w*)|\"([^\"]+)\"|\\[([^\\]]+)]", PCRE_CASELESS);

    static const char *brackets[] = {
        "[]",
        "()",

        NULL
    };

    view_colors &vc = view_colors::singleton();
    pcre_context_static<30> pc;
    pcre_input pi(al.get_string());
    string &line = al.get_string();

    while (ident_pcre.match(pc, pi)) {
        pcre_context::capture_t *cap = pc.first_valid();
        int attrs = vc.attrs_for_ident(pi.get_substr_start(cap), cap->length());
        struct line_range lr(cap->c_begin, cap->c_end);

        if (line[cap->c_end] == '(') {

        }
        else if (!lr.contains(x) && !lr.contains(x - 1)) {
            al.get_attrs().push_back(string_attr(
                lr, &view_curses::VC_STYLE, attrs));
        }
    }

    pi.reset(line);

    while (keyword_pcre.match(pc, pi)) {
        pcre_context::capture_t *cap = pc.all();

        al.get_attrs().push_back(string_attr(
            line_range(cap->c_begin, cap->c_end),
            &view_curses::VC_STYLE,
            keyword_attrs));
    }

    for (int lpc = 0; lpc < line.length(); lpc++) {
        switch (line[lpc]) {
        case '*':
        case '<':
        case '>':
        case '=':
        case '!':
        case '-':
        case '+':
            al.get_attrs().push_back(string_attr(
                line_range(lpc, lpc + 1),
                &view_curses::VC_STYLE,
                symbol_attrs));
            break;
        }
    }

    pi.reset(line);

    while (string_literal_pcre.match(pc, pi)) {
        pcre_context::capture_t *cap = pc.all();
        struct line_range lr(cap->c_begin, cap->c_end);
        string_attrs_t &sa = al.get_attrs();
        string_attrs_t::const_iterator iter;

        while ((iter = find_string_attr(sa, lr)) != sa.end()) {
            sa.erase(iter);
        }

        if (line[cap->c_end - 1] != '\'') {
            sa.push_back(string_attr(
                line_range(cap->c_begin, cap->c_begin + 1),
                &view_curses::VC_STYLE,
                error_attrs));
            lr.lr_start += 1;
        }
        sa.push_back(string_attr(
            lr,
            &view_curses::VC_STYLE,
            string_attrs));
    }

    for (int lpc = 0; brackets[lpc]; lpc++) {
        find_matching_bracket(al, x, brackets[lpc][0], brackets[lpc][1]);
    }
}
