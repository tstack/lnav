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

#include <algorithm>
#include <string>
#include <vector>

#include "sql.formatter.hh"

#include "base/attr_line.hh"
#include "base/intern_string.hh"
#include "sql_help.hh"

static void
clear_left(std::string& str)
{
    if (str.empty() || str.back() == '\n') {
        return;
    }

    str.push_back('\n');
}

static void
clear_right(std::string& str)
{
    str.push_back('\n');
}

static void
add_indent(std::string& str, size_t indent)
{
    if (str.back() == '\n') {
        str.append(indent, ' ');
    }
}

static void
add_space(std::string& str, size_t indent)
{
    if (str.empty()) {
        return;
    }

    if (str.back() == '\n') {
        str.append(indent, ' ');
    } else {
        str.push_back(' ');
    }
}

namespace lnav {
namespace prql {

format_result
format(const attr_line_t& al, int cursor_offset)
{
    std::string retval;
    std::optional<int> cursor_retval;

    for (const auto& attr : al.al_attrs) {
        auto sf = al.to_string_fragment(attr);

        if (attr.sa_type == &sql::PRQL_STAGE_ATTR) {
            auto start_len = sf.length();
            sf = sf.trim("| \t\n");
            auto trimmed_size = start_len - sf.length();
            if (sf.empty()) {
                continue;
            }
            retval.append(sf.data(), sf.length());

            if (attr.sa_range.contains(cursor_offset)) {
                auto diff = attr.sa_range.lr_end - cursor_offset - trimmed_size;
                if (diff > 0 && diff < retval.length()) {
                    cursor_retval = retval.length() - diff;
                } else {
                    cursor_retval = retval.length();
                }
            }
            retval.push_back('\n');
        }
    }

    return {retval, cursor_retval.value_or(retval.length())};
}

}  // namespace prql

namespace sql {

static const std::vector CLEAR_LR = {
    "FROM"_frag,
    "SELECT"_frag,
    "SET"_frag,
    "WHERE"_frag,
};

static void
check_for_multi_word_clear_left(std::string& str, size_t indent)
{
    static const auto clear_words = std::vector<const char*>{
        "ORDER BY",
    };

    for (const auto& words : clear_words) {
        if (endswith(str.c_str(), words)) {
            auto words_len = strlen(words);
            str[str.length() - words_len - 1] = '\n';
            str.insert(str.length() - words_len - 1, indent, ' ');
            break;
        }
    }
}

format_result
format(const attr_line_t& al, int cursor_offset)
{
    static constexpr auto INDENT_SIZE = size_t{4};

    auto indent = size_t{0};
    string_attrs_t funcs;
    std::string retval;
    std::optional<int> cursor_retval;
    std::vector<bool> paren_indents;

    for (const auto& attr : al.al_attrs) {
        if (!cursor_retval && cursor_offset < attr.sa_range.lr_start) {
            cursor_retval = retval.size();
        }
        if (find_string_attr(funcs, attr.sa_range.lr_start) != funcs.end()) {
            continue;
        }

        auto sf = al.to_string_fragment(attr);
        if (attr.sa_type == &SQL_KEYWORD_ATTR) {
            auto do_clear = std::count_if(
                CLEAR_LR.begin(), CLEAR_LR.end(), [&sf](const auto& x) {
                    return sf.iequal(x);
                });

            if (do_clear) {
                if (!paren_indents.empty()) {
                    paren_indents.back() = true;
                }
                if (indent > 0) {
                    indent -= INDENT_SIZE;
                }
                clear_left(retval);
            }
            add_space(retval, indent);
            retval.append(sf.to_string_with_case_style(
                string_fragment::case_style::upper));
            if (do_clear) {
                clear_right(retval);
                indent += INDENT_SIZE;
            } else {
                check_for_multi_word_clear_left(retval, indent);
            }
        } else if (attr.sa_type == &SQL_COMMA_ATTR) {
            retval.append(sf.data(), sf.length());
            clear_right(retval);
        } else if (attr.sa_type == &SQL_PAREN_ATTR && sf.front() == '(') {
            paren_indents.push_back(false);
            while (!retval.empty() && isspace(retval.back())) {
                retval.pop_back();
            }
            retval.push_back(' ');
            indent += INDENT_SIZE;
            retval.append(sf.data(), sf.length());
        } else if (attr.sa_type == &SQL_PAREN_ATTR && sf.front() == ')') {
            indent -= INDENT_SIZE;
            if (!paren_indents.empty()) {
                if (paren_indents.back()) {
                    retval.push_back('\n');
                }
                paren_indents.pop_back();
            }
            add_indent(retval, indent > 0 ? indent - INDENT_SIZE : 0);
            retval.append(sf.data(), sf.length());
        } else if (attr.sa_type == &SQL_FUNCTION_ATTR) {
            funcs.emplace_back(attr);
            add_space(retval, indent);
            retval.append(sf.data(), sf.length());
        } else {
            if (retval.empty() || retval.back() != '(') {
                add_space(retval, indent);
            }
            retval.append(sf.data(), sf.length());
        }

        if (attr.sa_range.contains(cursor_offset)) {
            auto diff = attr.sa_range.lr_end - cursor_offset;
            if (retval.back() == '\n') {
                diff += 1;
            }
            if (diff < retval.length()) {
                cursor_retval = retval.length() - diff;
            } else {
                cursor_retval = retval.length();
            }
        }
    }

    return {retval, cursor_retval.value_or(retval.length())};
}

}  // namespace sql

namespace db {

format_result
format(const attr_line_t& al, int cursor_offset)
{
    if (lnav::sql::is_prql(al.to_string_fragment())) {
        return prql::format(al, cursor_offset);
    }

    return sql::format(al, cursor_offset);
}

}  // namespace db

}  // namespace lnav