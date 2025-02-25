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
#include <array>
#include <string>
#include <vector>

#include "sql.formatter.hh"

#include "base/attr_line.hh"
#include "base/intern_string.hh"
#include "base/lnav_log.hh"
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
    } else if (str.back() != '.') {
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

static bool
always_close_scope(std::vector<std::string>& scope_stack)
{
    return true;
}

static bool
never_close_scope(std::vector<std::string>& scope_stack)
{
    return false;
}

static bool
in_case_close_scope(std::vector<std::string>& scope_stack)
{
    if (scope_stack.back() == "CASE"_frag) {
        return false;
    }
    return true;
}

static bool
end_close_scope(std::vector<std::string>& scope_stack)
{
    if (scope_stack.empty()) {
        return false;
    }

    scope_stack.pop_back();
    return (scope_stack.back() == "CASE"_frag);
}

struct keyword_attrs {
    string_fragment ka_keyword;
    bool ka_clear_left{false};
    bool ka_clear_right{false};
    bool (*ka_close_scope_p)(std::vector<std::string>& scope_stack)
        = always_close_scope;
};

static constexpr std::array<keyword_attrs, 15> ATTRS_FOR_KW = {{
    {"CASE"_frag, true, false, never_close_scope},
    {"CREATE"_frag, true, false},
    {"ELSE"_frag, true, false, in_case_close_scope},
    {"END"_frag, true, false, end_close_scope},
    {"EXCEPT"_frag, true, false},
    {"FROM"_frag, true, true},
    {"HAVING"_frag, true, true},
    {"INTERSECT"_frag, true, false},
    {"SELECT"_frag, true, true},
    {"SET"_frag, true, true},
    {"UNION"_frag, true, false},
    {"VALUES"_frag, true, true},
    {"WHEN"_frag, true, false, in_case_close_scope},
    {"WHERE"_frag, true, true},
    {"WITH"_frag, true, true},
}};

constexpr auto ATTRS_FOR_KW_DEFAULT
    = keyword_attrs{""_frag, false, false, never_close_scope};

static const keyword_attrs&
get_keyword_attrs(const string_fragment& sf)
{
    auto iter = std::find_if(
        ATTRS_FOR_KW.begin(), ATTRS_FOR_KW.end(), [&sf](const auto& x) {
            return sf.iequal(x.ka_keyword);
        });
    if (iter == ATTRS_FOR_KW.end()) {
        return ATTRS_FOR_KW_DEFAULT;
    }

    return *iter;
}

static constexpr auto INDENT_SIZE = size_t{4};

static void
check_for_multi_word_clear(std::string& str,
                           std::vector<std::string>& scope_stack)
{
    struct clear_rules {
        const char* word;
        bool do_right;
        const char* padding{""};
    };

    static constexpr auto clear_words = std::array<clear_rules, 7>{
        {
            {" GROUP BY", true},
            {"INSERT INTO", true},
            {" ON CONFLICT", false},
            {" ORDER BY", true},
            {" LEFT JOIN", false},
            {" PARTITION BY", false},
            {"REPLACE INTO", true},
        },
    };

    for (const auto& [words, do_right, padding] : clear_words) {
        if (endswith(str.c_str(), words)) {
            auto words_len = strlen(words);
            if (str[str.length() - words_len] == ' ') {
                str[str.length() - words_len] = '\n';
            }
            if (scope_stack.size() > 1) {
                if (do_right) {
                    scope_stack.pop_back();
                }
                str.insert(str.length() - words_len + 1,
                           (scope_stack.size() - 1) * INDENT_SIZE,
                           ' ');
                str.insert(str.length() - words_len + 1, padding);
            }
            if (do_right) {
                clear_right(str);
                scope_stack.emplace_back(words);
            }
            break;
        }
    }
}

format_result
format(const attr_line_t& al, int cursor_offset)
{
    string_attrs_t funcs;
    std::string retval;
    std::optional<int> cursor_retval;
    std::vector<bool> paren_indents;
    std::vector<std::string> scope_stack;

    scope_stack.emplace_back();
    for (const auto& attr : al.al_attrs) {
        if (!cursor_retval && cursor_offset < attr.sa_range.lr_start) {
            cursor_retval = retval.size();
        }
        if (find_string_attr(funcs, attr.sa_range.lr_start) != funcs.end()) {
            continue;
        }

        auto sf = al.to_string_fragment(attr);
        auto indent = (scope_stack.size() - 1) * INDENT_SIZE;
        if (attr.sa_type == &SQL_KEYWORD_ATTR) {
            const auto& ka = get_keyword_attrs(sf);
            const auto sf_upper = sf.to_string_with_case_style(
                string_fragment::case_style::upper);
            if (ka.ka_clear_left) {
                if (!paren_indents.empty()) {
                    paren_indents.back() = true;
                }
                if (ka.ka_close_scope_p(scope_stack)) {
                    if (scope_stack.size() > 1) {
                        scope_stack.pop_back();
                    }
                    indent = (scope_stack.size() - 1) * INDENT_SIZE;
                }
                clear_left(retval);
                if (ka.ka_keyword != "END"_frag) {  // XXX dumb special case
                    scope_stack.emplace_back(sf_upper);
                }
            }
            add_space(retval, indent);
            retval.append(sf_upper);
            if (ka.ka_clear_right) {
                clear_right(retval);
            } else {
                check_for_multi_word_clear(retval, scope_stack);
            }
        } else if (attr.sa_type == &SQL_COMMA_ATTR) {
            retval.append(sf.data(), sf.length());
            if (paren_indents.empty() || paren_indents.back()) {
                clear_right(retval);
            }
        } else if (attr.sa_type == &SQL_COMMENT_ATTR) {
            add_space(retval, indent);
            retval.append(sf.data(), sf.length());
            clear_right(retval);
        } else if (attr.sa_type == &SQL_PAREN_ATTR && sf.front() == '(') {
            paren_indents.push_back(false);
            while (!retval.empty() && isspace(retval.back())
                   && !endswith(retval, ",\n") && !endswith(retval, "VALUES\n"))
            {
                retval.pop_back();
            }
            if (endswith(retval, "OVER")) {
                paren_indents.back() = true;
            }
            add_space(retval, indent);
            retval.append(sf.data(), sf.length());
            if (scope_stack.back() == "CREATE") {
                clear_right(retval);
                paren_indents.back() = true;
            } else {
                scope_stack.emplace_back();
            }
        } else if (attr.sa_type == &SQL_PAREN_ATTR && sf.front() == ')') {
            if (scope_stack.size() > 1) {
                scope_stack.pop_back();
            }
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
        } else if (attr.sa_type == &SQL_GARBAGE_ATTR && sf.front() == '.') {
            retval.push_back('.');
        } else if (attr.sa_type == &SQL_GARBAGE_ATTR && sf.front() == ';') {
            retval.push_back(';');
            clear_right(retval);
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

        ensure(!scope_stack.empty());
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