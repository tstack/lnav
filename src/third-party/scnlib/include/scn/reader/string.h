// Copyright 2017 Elias Kosunen
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// This file is a part of scnlib:
//     https://github.com/eliaskosunen/scnlib

#ifndef SCN_READER_STRING_H
#define SCN_READER_STRING_H

#include "../util/small_vector.h"
#include "common.h"

namespace scn {
    SCN_BEGIN_NAMESPACE
    namespace detail {
        class set_parser_type {
        public:
            constexpr set_parser_type() = default;

            template <typename ParseCtx>
            error parse_set(ParseCtx& pctx, bool& parsed)
            {
                using char_type = typename ParseCtx::char_type;
                SCN_EXPECT(pctx.next_char() == ascii_widen<char_type>('['));

                pctx.advance_char();
                if (!pctx || pctx.check_arg_end()) {
                    return {error::invalid_format_string,
                            "Unexpected end of format string argument"};
                }

                get_option(flag::enabled) = true;
                parsed = true;

                if (pctx.next_char() == ascii_widen<char_type>('^')) {
                    // inverted
                    get_option(flag::inverted) = true;
                    pctx.advance_char();
                    if (!pctx || pctx.check_arg_end()) {
                        return {error::invalid_format_string,
                                "Unexpected end of format string argument"};
                    }
                }

                if (pctx.next_char() == ascii_widen<char_type>(']')) {
                    // end of range
                    get_option(flag::accept_all) = true;
                    pctx.advance_char();
                    return {};
                }

                while (true) {
                    if (!pctx || pctx.check_arg_end()) {
                        return {error::invalid_format_string,
                                "Unexpected end of format string argument"};
                    }

                    const auto ch = pctx.next_char();
                    if (ch == ascii_widen<char_type>(']')) {
                        break;
                    }

                    auto err = parse_next_char(pctx, true);
                    if (!err) {
                        return err;
                    }

                    err = pctx.advance_cp();
                    if (!err) {
                        pctx.advance_char();
                    }
                }
                auto err = pctx.advance_cp();
                if (!err) {
                    pctx.advance_char();
                }

                return {};
            }

            error sanitize(bool localized)
            {
                // specifiers -> chars, if not localized
                if (get_option(flag::use_specifiers)) {
                    if ((get_option(specifier::letters) ||
                         get_option(specifier::alpha)) &&
                        get_option(specifier::inverted_letters)) {
                        get_option(flag::accept_all) = true;
                    }
                    if (get_option(specifier::alnum_underscore) &&
                        get_option(specifier::inverted_alnum_underscore)) {
                        get_option(flag::accept_all) = true;
                    }
                    if ((get_option(specifier::whitespace) ||
                         get_option(specifier::space)) &&
                        get_option(specifier::inverted_whitespace)) {
                        get_option(flag::accept_all) = true;
                    }
                    if ((get_option(specifier::numbers) ||
                         get_option(specifier::digit)) &&
                        get_option(specifier::inverted_numbers)) {
                        get_option(flag::accept_all) = true;
                    }
                }

                if (get_option(flag::use_specifiers) &&
                    !get_option(flag::accept_all)) {
                    if (localized) {
                        if (get_option(specifier::letters)) {
                            get_option(specifier::letters) = false;
                            get_option(specifier::alpha) = true;
                        }
                        if (get_option(specifier::alnum_underscore)) {
                            get_option(specifier::alnum_underscore) = false;
                            get_option(specifier::alnum) = true;
                            get_option('_') = true;
                        }
                        if (get_option(specifier::whitespace)) {
                            get_option(specifier::whitespace) = false;
                            get_option(specifier::space) = true;
                        }
                        if (get_option(specifier::numbers)) {
                            get_option(specifier::numbers) = false;
                            get_option(specifier::digit) = true;
                        }
                    }
                    else {
                        auto do_range = [&](char a, char b) {
                            for (; a < b; ++a) {
                                get_option(a) = true;
                            }
                            get_option(b) = true;
                        };
                        auto do_lower = [&]() {
                            // a-z
                            do_range(0x61, 0x7a);
                        };
                        auto do_upper = [&]() {
                            // A-Z
                            do_range(0x41, 0x5a);
                        };
                        auto do_digit = [&]() {
                            // 0-9
                            do_range(0x30, 0x39);
                        };

                        if (get_option(specifier::alnum)) {
                            do_lower();
                            do_upper();
                            do_digit();
                            get_option(specifier::alnum) = false;
                        }
                        if (get_option(specifier::alpha)) {
                            do_lower();
                            do_upper();
                            get_option(specifier::alpha) = false;
                        }
                        if (get_option(specifier::blank)) {
                            get_option(' ') = true;
                            get_option('\t') = true;
                            get_option(specifier::blank) = false;
                        }
                        if (get_option(specifier::cntrl)) {
                            do_range(0, 0x1f);
                            get_option(0x7f) = true;
                            get_option(specifier::cntrl) = false;
                        }
                        if (get_option(specifier::digit)) {
                            do_digit();
                            get_option(specifier::digit) = false;
                        }
                        if (get_option(specifier::graph)) {
                            do_range(0x21, 0x7e);
                            get_option(specifier::graph) = false;
                        }
                        if (get_option(specifier::lower)) {
                            do_lower();
                            get_option(specifier::lower) = false;
                        }
                        if (get_option(specifier::print)) {
                            do_range(0x20, 0x7e);
                            get_option(specifier::print) = false;
                        }
                        if (get_option(specifier::punct)) {
                            do_range(0x21, 0x2f);
                            do_range(0x3a, 0x40);
                            do_range(0x5b, 0x60);
                            do_range(0x7b, 0x7e);
                            get_option(specifier::punct) = false;
                        }
                        if (get_option(specifier::space)) {
                            do_range(0x9, 0xd);
                            get_option(' ') = true;
                            get_option(specifier::space) = false;
                        }
                        if (get_option(specifier::upper)) {
                            do_upper();
                            get_option(specifier::upper) = false;
                        }
                        if (get_option(specifier::xdigit)) {
                            do_digit();
                            do_range(0x41, 0x46);
                            do_range(0x61, 0x66);
                            get_option(specifier::xdigit) = false;
                        }
                        if (get_option(specifier::letters)) {
                            do_upper();
                            do_lower();
                            get_option(specifier::letters) = false;
                        }
                        if (get_option(specifier::inverted_letters)) {
                            do_range(0x0, 0x2f);
                            do_range(0x3a, 0x40);
                            do_range(0x5b, 0x60);
                            do_range(0x7b, 0x7f);
                            get_option(specifier::inverted_letters) = false;
                        }
                        if (get_option(specifier::alnum_underscore)) {
                            do_digit();
                            do_upper();
                            do_lower();
                            get_option('_') = true;
                            get_option(specifier::alnum_underscore) = false;
                        }
                        if (get_option(specifier::inverted_alnum_underscore)) {
                            bool underscore = get_option('_');
                            do_range(0x0, 0x2f);
                            do_range(0x3a, 0x40);
                            do_range(0x5b, 0x60);
                            do_range(0x7b, 0x7f);
                            get_option('_') = underscore;  // reset back
                            get_option(specifier::inverted_alnum_underscore) =
                                false;
                        }
                        if (get_option(specifier::whitespace)) {
                            do_range(0x9, 0xd);
                            get_option(' ') = true;
                            get_option(specifier::whitespace) = false;
                        }
                        if (get_option(specifier::inverted_whitespace)) {
                            do_range(0, 0x8);
                            do_range(0xe, 0x1f);
                            do_range(0x21, 0x7f);
                            get_option(specifier::inverted_whitespace) = false;
                        }
                        if (get_option(specifier::numbers)) {
                            do_digit();
                            get_option(specifier::numbers) = false;
                        }
                        if (get_option(specifier::inverted_numbers)) {
                            do_range(0, 0x2f);
                            do_range(0x3a, 0x7f);
                            get_option(specifier::inverted_numbers) = false;
                        }

                        {
                            bool first = get_option(0);
                            char i = 1;
                            for (; i < 0x7f; ++i) {
                                if (first != get_option(i)) {
                                    break;
                                }
                            }
                            if (i == 0x7f && first == get_option(0x7f)) {
                                get_option(flag::accept_all) = true;
                                if (!first) {
                                    get_option(flag::inverted) = true;
                                }
                            }
                        }

                        get_option(flag::use_specifiers) = false;
                        get_option(flag::use_chars) = true;
                    }
                }

                return {};
            }

            // true = char accepted
            template <typename CharT, typename Locale>
            bool check_character(CharT ch, bool localized, const Locale& loc)
            {
                SCN_EXPECT(get_option(flag::enabled));

                const bool not_inverted = !get_option(flag::inverted);
                if (get_option(flag::accept_all)) {
                    return not_inverted;
                }

                if (get_option(flag::use_specifiers)) {
                    SCN_EXPECT(localized);  // ensured by sanitize()
                    SCN_UNUSED(localized);
                    SCN_CLANG_PUSH_IGNORE_UNDEFINED_TEMPLATE
                    if (get_option(specifier::alnum) &&
                        loc.get_localized().is_alnum(ch)) {
                        return not_inverted;
                    }
                    if (get_option(specifier::alpha) &&
                        loc.get_localized().is_alpha(ch)) {
                        return not_inverted;
                    }
                    if (get_option(specifier::blank) &&
                        loc.get_localized().is_blank(ch)) {
                        return not_inverted;
                    }
                    if (get_option(specifier::cntrl) &&
                        loc.get_localized().is_cntrl(ch)) {
                        return not_inverted;
                    }
                    if (get_option(specifier::digit) &&
                        loc.get_localized().is_digit(ch)) {
                        return not_inverted;
                    }
                    if (get_option(specifier::graph) &&
                        loc.get_localized().is_graph(ch)) {
                        return not_inverted;
                    }
                    if (get_option(specifier::lower) &&
                        loc.get_localized().is_lower(ch)) {
                        return not_inverted;
                    }
                    if (get_option(specifier::print) &&
                        loc.get_localized().is_print(ch)) {
                        return not_inverted;
                    }
                    if (get_option(specifier::punct) &&
                        loc.get_localized().is_punct(ch)) {
                        return not_inverted;
                    }
                    if (get_option(specifier::space) &&
                        loc.get_localized().is_space(ch)) {
                        return not_inverted;
                    }
                    if (get_option(specifier::upper) &&
                        loc.get_localized().is_upper(ch)) {
                        return not_inverted;
                    }
                    if (get_option(specifier::xdigit) &&
                        loc.get_localized().is_xdigit(ch)) {
                        return not_inverted;
                    }
                    SCN_CLANG_POP_IGNORE_UNDEFINED_TEMPLATE
                }
                if (get_option(flag::use_chars) && (ch >= 0 && ch <= 0x7f)) {
                    if (get_option(static_cast<char>(ch))) {
                        return not_inverted;
                    }
                }
                if (get_option(flag::use_ranges)) {
                    const auto c = static_cast<uint32_t>(ch);
                    for (const auto& e : set_extra_ranges) {
                        if (c >= e.begin && c <= e.end) {
                            return not_inverted;
                        }
                    }
                }
                return !not_inverted;
            }

            enum class specifier : size_t {
                alnum = 0x80,
                alpha,
                blank,
                cntrl,
                digit,
                graph,
                lower,
                print,
                punct,
                space,
                upper,
                xdigit,
                letters = 0x90,             // \l
                inverted_letters,           // \L
                alnum_underscore,           // \w
                inverted_alnum_underscore,  // \W
                whitespace,                 // \s
                inverted_whitespace,        // \S
                numbers,                    // \d
                inverted_numbers,           // \D
                last = 0x9f
            };
            enum class flag : size_t {
                enabled = 0xa0,  // using [set]
                accept_all,      // empty [set]
                inverted,        // ^ flag
                // 0x00 - 0x7f
                use_chars,
                // 0x80 - 0x8f
                use_specifiers,
                // set_extra_ranges
                use_ranges,
                last = 0xaf
            };

            bool& get_option(char ch)
            {
                SCN_GCC_PUSH
                SCN_GCC_IGNORE("-Wtype-limits")
                SCN_EXPECT(ch >= 0 && ch <= 0x7f);
                SCN_GCC_POP
                return set_options[static_cast<size_t>(ch)];
            }
            SCN_NODISCARD bool get_option(char ch) const
            {
                SCN_GCC_PUSH
                SCN_GCC_IGNORE("-Wtype-limits")
                SCN_EXPECT(ch >= 0 && ch <= 0x7f);
                SCN_GCC_POP
                return set_options[static_cast<size_t>(ch)];
            }

            bool& get_option(specifier s)
            {
                return set_options[static_cast<size_t>(s)];
            }
            SCN_NODISCARD bool get_option(specifier s) const
            {
                return set_options[static_cast<size_t>(s)];
            }

            bool& get_option(flag f)
            {
                return set_options[static_cast<size_t>(f)];
            }
            SCN_NODISCARD bool get_option(flag f) const
            {
                return set_options[static_cast<size_t>(f)];
            }

            SCN_NODISCARD bool enabled() const
            {
                return get_option(flag::enabled);
            }

        private:
            void accept_char(char ch)
            {
                get_option(ch) = true;
                get_option(flag::use_chars) = true;
            }
            void accept_char(code_point cp)
            {
                if (cp >= 0 && cp <= 0x7f) {
                    return accept_char(static_cast<char>(cp));
                }
                set_extra_ranges.push_back(set_range::single(cp));
                get_option(flag::use_ranges) = true;
            }
            void accept_char(wchar_t ch)
            {
                SCN_GCC_COMPAT_PUSH
                SCN_GCC_COMPAT_IGNORE("-Wtype-limits")
                if (ch >= 0 && ch <= 0x7f) {
                    return accept_char(static_cast<char>(ch));
                }
                SCN_GCC_COMPAT_POP
                set_extra_ranges.push_back(set_range::single(ch));
                get_option(flag::use_ranges) = true;
            }

            void accept_char_range(char first, char last)
            {
                SCN_EXPECT(first >= 0);
                SCN_EXPECT(last >= 0);
                SCN_EXPECT(first <= last);
                get_option(flag::use_chars) = true;
                for (; first != last; ++first) {
                    get_option(first) = true;
                }
                SCN_ENSURE(first == last);
                get_option(last) = true;
            }
            void accept_char_range(code_point first, code_point last)
            {
                SCN_EXPECT(first <= last);
                if (first >= 0 && last <= 0x7f) {
                    return accept_char_range(static_cast<char>(first),
                                             static_cast<char>(last));
                }
                set_extra_ranges.push_back(set_range::range(first, last));
                get_option(flag::use_ranges) = true;
            }
            void accept_char_range(wchar_t first, wchar_t last)
            {
                SCN_EXPECT(first <= last);
                SCN_GCC_COMPAT_PUSH
                SCN_GCC_COMPAT_IGNORE("-Wtype-limits")
                if (first >= 0 && last <= 0x7f) {
                    return accept_char_range(static_cast<char>(first),
                                             static_cast<char>(last));
                }
                SCN_GCC_COMPAT_POP
                set_extra_ranges.push_back(set_range::range(first, last));
                get_option(flag::use_ranges) = true;
            }

            template <typename ParseCtx>
            error parse_range(ParseCtx& pctx, code_point begin)
            {
                using char_type = typename ParseCtx::char_type;
                SCN_EXPECT(pctx.next_char() == ascii_widen<char_type>('-'));
                if (pctx.can_peek_char() &&
                    pctx.peek_char() == ascii_widen<char_type>(']')) {
                    // Just a '-'
                    accept_char(begin);
                    accept_char(ascii_widen<char_type>('-'));
                    return {};
                }
                pctx.advance_char();
                if (!pctx || pctx.check_arg_end()) {
                    return {error::invalid_format_string,
                            "Unexpected end of format string argument"};
                }
                return parse_next_char(pctx, false, begin);
            }
            template <typename ParseCtx>
            error parse_literal(ParseCtx& pctx,
                                bool allow_range,
                                code_point begin = make_code_point(0))
            {
                using char_type = typename ParseCtx::char_type;
                if (allow_range) {
                    auto e = pctx.peek_cp();
                    if (!e && e.error().code() != error::end_of_range) {
                        return e.error();
                    }
                    if (e && e.value() == ascii_widen<char_type>('-')) {
                        const auto cp = pctx.next_cp();
                        if (!cp) {
                            return cp.error();
                        }
                        auto err = pctx.advance_cp();
                        if (!err) {
                            return err;
                        }
                        return parse_range(pctx, cp.value());
                    }
                }
                const auto cp = pctx.next_cp();
                if (!cp) {
                    return cp.error();
                }
                if (cp.value() >= 0 && cp.value() <= 0x7f) {
                    if (!allow_range) {
                        if (static_cast<
                                typename std::make_unsigned<char_type>::type>(
                                cp.value()) <
                            static_cast<
                                typename std::make_unsigned<char_type>::type>(
                                begin)) {
                            return {error::invalid_format_string,
                                    "Last char in [set] range is less than the "
                                    "first"};
                        }
                        accept_char_range(begin, cp.value());
                    }
                    else {
                        accept_char(cp.value());
                    }
                }
                else {
                    if (!allow_range) {
                        if (static_cast<
                                typename std::make_unsigned<char_type>::type>(
                                cp.value()) <
                            static_cast<
                                typename std::make_unsigned<char_type>::type>(
                                begin)) {
                            return {error::invalid_format_string,
                                    "Last char in [set] range is less than the "
                                    "first"};
                        }
                        set_extra_ranges.push_back(
                            set_range::range(begin, cp.value()));
                    }
                    else {
                        set_extra_ranges.push_back(
                            set_range::single(cp.value()));
                    }
                    get_option(flag::use_ranges) = true;
                }
                return {};
            }
            template <typename ParseCtx>
            error parse_colon_specifier(ParseCtx& pctx)
            {
                using char_type = typename ParseCtx::char_type;
                SCN_EXPECT(pctx.next_char() == ascii_widen<char_type>(':'));
                pctx.advance_char();
                if (!pctx || pctx.check_arg_end()) {
                    return {error::invalid_format_string,
                            "Unexpected end of format string argument"};
                }
                if (pctx.next_char() == ascii_widen<char_type>(']')) {
                    return {
                        error::invalid_format_string,
                        "Unexpected end of [set] in format string after ':'"};
                }

                std::basic_string<char_type> buf;
                while (true) {
                    if (!pctx || pctx.check_arg_end()) {
                        return {error::invalid_format_string,
                                "Unexpected end of format string argument"};
                    }
                    auto ch = pctx.next_char();
                    if (ch == ascii_widen<char_type>(':')) {
                        break;
                    }
                    if (ch == ascii_widen<char_type>(']')) {
                        return {error::invalid_format_string,
                                "Unexpected end of [set] :specifier:, did you "
                                "forget a terminating colon?"};
                    }
                    buf.push_back(ch);
                    pctx.advance_char();
                }

                auto ch = pctx.next_char();
                if (buf == all_str(ch)) {
                    get_option(flag::accept_all) = true;
                    return {};
                }
                if (buf == alnum_str(ch)) {
                    get_option(specifier::alnum) = true;
                    get_option(flag::use_specifiers) = true;
                    return {};
                }
                if (buf == alpha_str(ch)) {
                    get_option(specifier::alpha) = true;
                    get_option(flag::use_specifiers) = true;
                    return {};
                }
                if (buf == blank_str(ch)) {
                    get_option(specifier::blank) = true;
                    get_option(flag::use_specifiers) = true;
                    return {};
                }
                if (buf == cntrl_str(ch)) {
                    get_option(specifier::cntrl) = true;
                    get_option(flag::use_specifiers) = true;
                    return {};
                }
                if (buf == digit_str(ch)) {
                    get_option(specifier::digit) = true;
                    get_option(flag::use_specifiers) = true;
                    return {};
                }
                if (buf == graph_str(ch)) {
                    get_option(specifier::graph) = true;
                    get_option(flag::use_specifiers) = true;
                    return {};
                }
                if (buf == lower_str(ch)) {
                    get_option(specifier::lower) = true;
                    get_option(flag::use_specifiers) = true;
                    return {};
                }
                if (buf == print_str(ch)) {
                    get_option(specifier::print) = true;
                    get_option(flag::use_specifiers) = true;
                    return {};
                }
                if (buf == punct_str(ch)) {
                    get_option(specifier::punct) = true;
                    get_option(flag::use_specifiers) = true;
                    return {};
                }
                if (buf == space_str(ch)) {
                    get_option(specifier::space) = true;
                    get_option(flag::use_specifiers) = true;
                    return {};
                }
                if (buf == upper_str(ch)) {
                    get_option(specifier::upper) = true;
                    get_option(flag::use_specifiers) = true;
                    return {};
                }
                if (buf == xdigit_str(ch)) {
                    get_option(specifier::xdigit) = true;
                    get_option(flag::use_specifiers) = true;
                    return {};
                }

                return {error::invalid_format_string,
                        "Invalid :specifier: in [set]"};
            }
            template <typename ParseCtx>
            error parse_backslash_hex(ParseCtx& pctx,
                                      bool allow_range,
                                      code_point begin = make_code_point(0))
            {
                using char_type = typename ParseCtx::char_type;
                SCN_EXPECT(pctx.next_char() == ascii_widen<char_type>('x') ||
                           pctx.next_char() == ascii_widen<char_type>('u') ||
                           pctx.next_char() == ascii_widen<char_type>('U'));

                const char_type flag_char = pctx.next_char();
                const int chars = [flag_char]() {
                    auto ch = static_cast<char>(flag_char);
                    if (ch == 'x') {
                        return 2;
                    }
                    if (ch == 'u') {
                        return 4;
                    }
                    if (ch == 'U') {
                        return 8;
                    }
                    SCN_ENSURE(false);
                    SCN_UNREACHABLE;
                }();

                char_type str[8] = {0};
                for (int i = 0; i < chars; ++i) {
                    pctx.advance_char();
                    if (!pctx || pctx.check_arg_end()) {
                        return {error::invalid_format_string,
                                "Unexpected end of format string argument "
                                "after '\\x', '\\u', or '\\U'"};
                    }
                    if (pctx.next_char() == ascii_widen<char_type>(']')) {
                        return {error::invalid_format_string,
                                "Unexpected end of [set] in format string "
                                "after '\\x', '\\u', or '\\U'"};
                    }
                    str[i] = pctx.next_char();
                }

                auto scanner = simple_integer_scanner<uint64_t>{};
                uint64_t i;
                SCN_CLANG_PUSH_IGNORE_UNDEFINED_TEMPLATE
                auto res = scanner.scan(
                    scn::make_span(str, static_cast<size_t>(chars)).as_const(),
                    i, 16);
                SCN_CLANG_POP_IGNORE_UNDEFINED_TEMPLATE
                if (!res) {
                    return {error::invalid_format_string,
                            "Failed to parse '\\x', '\\u', or '\\U' flag in "
                            "format string"};
                }
                const uint64_t min = 0;
                const uint64_t max = [chars]() {
                    if (chars == 2) {
                        // \x
                        return uint64_t{0x7f};
                    }
                    if (chars == 4) {
                        return uint64_t{0xffff};
                    }
                    if (chars == 8) {
                        return uint64_t{0xffffffff};
                    }
                    SCN_ENSURE(false);
                    SCN_UNREACHABLE;
                }();
                if (i < min || i > max) {
                    return {error::invalid_format_string,
                            "'\\x', '\\u', or '\\U' option in format string "
                            "out of range"};
                }

                if (allow_range && pctx.can_peek_char() &&
                    pctx.peek_char() == ascii_widen<char_type>('-')) {
                    pctx.advance_char();
                    return parse_range(pctx, make_code_point(i));
                }
                if (!allow_range) {
                    accept_char_range(begin, make_code_point(i));
                }
                else {
                    accept_char(make_code_point(i));
                }
                return {};
            }
            template <typename ParseCtx>
            error parse_backslash_specifier(
                ParseCtx& pctx,
                bool allow_range,
                code_point begin = make_code_point(0))
            {
                using char_type = typename ParseCtx::char_type;
                SCN_EXPECT(pctx.next_char() == ascii_widen<char_type>('\\'));
                pctx.advance_char();

                if (!pctx || pctx.check_arg_end()) {
                    return {error::invalid_format_string,
                            "Unexpected end of format string argument"};
                }
                if (pctx.next_char() == ascii_widen<char_type>(']') &&
                    pctx.can_peek_char() &&
                    pctx.peek_char() == ascii_widen<char_type>('}')) {
                    return {error::invalid_format_string,
                            "Unexpected end of [set] in format string"};
                }

                if (pctx.next_char() == ascii_widen<char_type>('\\')) {
                    // Literal "\\"
                    accept_char(pctx.next_char());
                    return {};
                }

                // specifiers
                if (pctx.next_char() == ascii_widen<char_type>('l')) {
                    // \l
                    get_option(specifier::letters) = true;
                    get_option(flag::use_specifiers) = true;
                    return {};
                }
                if (pctx.next_char() == ascii_widen<char_type>('L')) {
                    // \L
                    get_option(specifier::inverted_letters) = true;
                    get_option(flag::use_specifiers) = true;
                    return {};
                }

                if (pctx.next_char() == ascii_widen<char_type>('w')) {
                    // \w
                    get_option(specifier::alnum_underscore) = true;
                    get_option(flag::use_specifiers) = true;
                    return {};
                }
                if (pctx.next_char() == ascii_widen<char_type>('W')) {
                    // \W
                    get_option(specifier::inverted_alnum_underscore) = true;
                    get_option(flag::use_specifiers) = true;
                    return {};
                }

                if (pctx.next_char() == ascii_widen<char_type>('s')) {
                    // \s
                    get_option(specifier::whitespace) = true;
                    get_option(flag::use_specifiers) = true;
                    return {};
                }
                if (pctx.next_char() == ascii_widen<char_type>('S')) {
                    // \S
                    get_option(specifier::inverted_whitespace) = true;
                    get_option(flag::use_specifiers) = true;
                    return {};
                }

                if (pctx.next_char() == ascii_widen<char_type>('d')) {
                    // \d
                    get_option(specifier::numbers) = true;
                    get_option(flag::use_specifiers) = true;
                    return {};
                }
                if (pctx.next_char() == ascii_widen<char_type>('D')) {
                    // \D
                    get_option(specifier::inverted_numbers) = true;
                    get_option(flag::use_specifiers) = true;
                    return {};
                }

                if (pctx.next_char() == ascii_widen<char_type>('x') ||
                    pctx.next_char() == ascii_widen<char_type>('u') ||
                    pctx.next_char() == ascii_widen<char_type>('U')) {
                    // \x__, \u____, or \U________
                    return parse_backslash_hex(pctx, allow_range, begin);
                }

                // Literal, e.g. \: -> :
                return parse_literal(pctx, true);
            }
            template <typename ParseCtx>
            error parse_next_char(ParseCtx& pctx,
                                  bool allow_range,
                                  code_point begin = make_code_point(0))
            {
                using char_type = typename ParseCtx::char_type;
                const auto ch = pctx.next_char();
                if (ch == ascii_widen<char_type>('\\')) {
                    return parse_backslash_specifier(pctx, allow_range, begin);
                }
                if (allow_range && ch == ascii_widen<char_type>(':')) {
                    return parse_colon_specifier(pctx);
                }
                return parse_literal(pctx, allow_range, begin);
            }

            SCN_NODISCARD static constexpr const char* all_str(char)
            {
                return "all";
            }
            SCN_NODISCARD static constexpr const wchar_t* all_str(wchar_t)
            {
                return L"all";
            }
            SCN_NODISCARD static constexpr const char* alnum_str(char)
            {
                return "alnum";
            }
            SCN_NODISCARD static constexpr const wchar_t* alnum_str(wchar_t)
            {
                return L"alnum";
            }
            SCN_NODISCARD static constexpr const char* alpha_str(char)
            {
                return "alpha";
            }
            SCN_NODISCARD static constexpr const wchar_t* alpha_str(wchar_t)
            {
                return L"alpha";
            }
            SCN_NODISCARD static constexpr const char* blank_str(char)
            {
                return "blank";
            }
            SCN_NODISCARD static constexpr const wchar_t* blank_str(wchar_t)
            {
                return L"blank";
            }
            SCN_NODISCARD static constexpr const char* cntrl_str(char)
            {
                return "cntrl";
            }
            SCN_NODISCARD static constexpr const wchar_t* cntrl_str(wchar_t)
            {
                return L"cntrl";
            }
            SCN_NODISCARD static constexpr const char* digit_str(char)
            {
                return "digit";
            }
            SCN_NODISCARD static constexpr const wchar_t* digit_str(wchar_t)
            {
                return L"digit";
            }
            SCN_NODISCARD static constexpr const char* graph_str(char)
            {
                return "graph";
            }
            SCN_NODISCARD static constexpr const wchar_t* graph_str(wchar_t)
            {
                return L"graph";
            }
            SCN_NODISCARD static constexpr const char* lower_str(char)
            {
                return "lower";
            }
            SCN_NODISCARD static constexpr const wchar_t* lower_str(wchar_t)
            {
                return L"lower";
            }
            SCN_NODISCARD static constexpr const char* print_str(char)
            {
                return "print";
            }
            SCN_NODISCARD static constexpr const wchar_t* print_str(wchar_t)
            {
                return L"print";
            }
            SCN_NODISCARD static constexpr const char* punct_str(char)
            {
                return "punct";
            }
            SCN_NODISCARD static constexpr const wchar_t* punct_str(wchar_t)
            {
                return L"punct";
            }
            SCN_NODISCARD static constexpr const char* space_str(char)
            {
                return "space";
            }
            SCN_NODISCARD static constexpr const wchar_t* space_str(wchar_t)
            {
                return L"space";
            }
            SCN_NODISCARD static constexpr const char* upper_str(char)
            {
                return "upper";
            }
            SCN_NODISCARD static constexpr const wchar_t* upper_str(wchar_t)
            {
                return L"upper";
            }
            SCN_NODISCARD static constexpr const char* xdigit_str(char)
            {
                return "xdigit";
            }
            SCN_NODISCARD static constexpr const wchar_t* xdigit_str(wchar_t)
            {
                return L"xdigit";
            }

            // 0x00 - 0x7f, individual chars, true = accept
            // 0x80 - 0x9f, specifiers, true = accept (if use_specifiers = true)
            // 0xa0 - 0xaf, flags
            array<bool, 0xb0> set_options{{false}};

            struct set_range {
                constexpr set_range(uint32_t b, uint32_t e) : begin(b), end(e)
                {
                }

                uint32_t begin{};
                uint32_t end{};  // inclusive

                static set_range single(code_point cp)
                {
                    return {static_cast<uint32_t>(cp),
                            static_cast<uint32_t>(cp)};
                }
                static set_range single(wchar_t ch)
                {
                    return {static_cast<uint32_t>(ch),
                            static_cast<uint32_t>(ch)};
                }

                static set_range range(code_point begin, code_point end)
                {
                    SCN_EXPECT(begin <= end);
                    return {static_cast<uint32_t>(begin),
                            static_cast<uint32_t>(end)};
                }
                static set_range range(wchar_t begin, wchar_t end)
                {
                    SCN_EXPECT(begin <= end);
                    return {static_cast<uint32_t>(begin),
                            static_cast<uint32_t>(end)};
                }
            };
            // Used if set_options[use_ranges] = true
            small_vector<set_range, 1> set_extra_ranges{};
        };

        struct string_scanner : common_parser {
            static constexpr bool skip_preceding_whitespace()
            {
                return false;
            }

            template <typename ParseCtx>
            error parse(ParseCtx& pctx)
            {
                using char_type = typename ParseCtx::char_type;

                auto s_flag = detail::ascii_widen<char_type>('s');
                bool s_set{};

                auto each = [&](ParseCtx& p, bool& parsed) -> error {
                    if (p.next_char() == ascii_widen<char_type>('[')) {
                        if (set_parser.get_option(
                                set_parser_type::flag::enabled)) {
                            return {error::invalid_format_string,
                                    "[set] already specified for this argument "
                                    "in format string"};
                        }
                        return set_parser.parse_set(p, parsed);
                    }
                    return {};
                };
                auto e = parse_common(pctx, span<const char_type>{&s_flag, 1},
                                      span<bool>{&s_set, 1}, each);
                if (!e) {
                    return e;
                }
                if (set_parser.enabled()) {
                    bool loc = (common_options & localized) != 0;
                    return set_parser.sanitize(loc);
                }
                return {};
            }

            template <typename Context, typename Allocator>
            error scan(
                std::basic_string<typename Context::char_type,
                                  std::char_traits<typename Context::char_type>,
                                  Allocator>& val,
                Context& ctx)
            {
                if (set_parser.enabled()) {
                    bool loc = (common_options & localized) != 0;
                    bool mb = (loc || set_parser.get_option(
                                          set_parser_type::flag::use_ranges)) &&
                              is_multichar_type(typename Context::char_type{});
                    return do_scan(ctx, val,
                                   pred<Context>{ctx, set_parser, loc, mb});
                }

                auto e = skip_range_whitespace(ctx, false);
                if (!e) {
                    return e;
                }

                auto is_space_pred = make_is_space_predicate(
                    ctx.locale(), (common_options & localized) != 0,
                    field_width);
                return do_scan(ctx, val, is_space_pred);
            }

            set_parser_type set_parser;

        protected:
            template <typename Context, typename Allocator, typename Pred>
            error do_scan(
                Context& ctx,
                std::basic_string<typename Context::char_type,
                                  std::char_traits<typename Context::char_type>,
                                  Allocator>& val,
                Pred&& predicate)
            {
                using string_type = std::basic_string<
                    typename Context::char_type,
                    std::char_traits<typename Context::char_type>, Allocator>;

                if (Context::range_type::is_contiguous) {
                    auto s = read_until_space_zero_copy(
                        ctx.range(), SCN_FWD(predicate), false);
                    if (!s) {
                        return s.error();
                    }
                    if (s.value().size() == 0) {
                        return {error::invalid_scanned_value,
                                "Empty string parsed"};
                    }
                    val.assign(s.value().data(), s.value().size());
                    return {};
                }

                string_type tmp(val.get_allocator());
                auto outputit = std::back_inserter(tmp);
                auto ret = read_until_space(ctx.range(), outputit,
                                            SCN_FWD(predicate), false);
                if (SCN_UNLIKELY(!ret)) {
                    return ret;
                }
                if (SCN_UNLIKELY(tmp.empty())) {
                    return {error::invalid_scanned_value,
                            "Empty string parsed"};
                }
                val = SCN_MOVE(tmp);

                return {};
            }

            template <typename Context>
            struct pred {
                Context& ctx;
                set_parser_type& set_parser;
                bool localized;
                bool multibyte;

                bool operator()(span<const char> ch) const
                {
                    SCN_EXPECT(ch.size() >= 1);
                    code_point cp{};
                    auto it = parse_code_point(ch.begin(), ch.end(), cp);
                    if (!it) {
                        // todo: is this really a good idea
                        return !set_parser.check_character(ch[0], localized,
                                                           ctx.locale());
                    }
                    return !set_parser.check_character(cp, localized,
                                                       ctx.locale());
                }
                bool operator()(span<const wchar_t> ch) const
                {
                    SCN_EXPECT(ch.size() == 1);
                    return !set_parser.check_character(ch[0], localized,
                                                       ctx.locale());
                }
                constexpr bool is_localized() const
                {
                    return localized;
                }
                constexpr bool is_multibyte() const
                {
                    return multibyte;
                }
            };
        };

        struct span_scanner : public string_scanner {
            template <typename Context>
            error scan(span<typename Context::char_type>& val, Context& ctx)
            {
                if (val.size() == 0) {
                    return {error::invalid_scanned_value,
                            "Cannot scan into an empty span"};
                }

                if (set_parser.enabled()) {
                    bool loc = (common_options & localized) != 0;
                    bool mb = (loc || set_parser.get_option(
                                          set_parser_type::flag::use_ranges)) &&
                              is_multichar_type(typename Context::char_type{});
                    return do_scan(ctx, val,
                                   string_scanner::pred<Context>{
                                       ctx, set_parser, loc, mb});
                }

                auto e = skip_range_whitespace(ctx, false);
                if (!e) {
                    return e;
                }

                auto is_space_pred = make_is_space_predicate(
                    ctx.locale(), (common_options & localized) != 0,
                    field_width != 0 ? min(field_width, val.size())
                                     : val.size());
                return do_scan(ctx, val, is_space_pred);
            }

        protected:
            template <typename Context, typename Pred>
            error do_scan(Context& ctx,
                          span<typename Context::char_type>& val,
                          Pred&& predicate)
            {
                if (Context::range_type::is_contiguous) {
                    auto s = read_until_space_zero_copy(
                        ctx.range(), SCN_FWD(predicate), false);
                    if (!s) {
                        return s.error();
                    }
                    if (s.value().size() == 0) {
                        return {error::invalid_scanned_value,
                                "Empty string parsed"};
                    }
                    std::copy(s.value().begin(), s.value().end(), val.begin());
                    val = val.first(s.value().size());
                    return {};
                }

                std::basic_string<typename Context::char_type> tmp;
                auto outputit = std::back_inserter(tmp);
                auto ret = read_until_space(ctx.range(), outputit,
                                            SCN_FWD(predicate), false);
                if (SCN_UNLIKELY(!ret)) {
                    return ret;
                }
                if (SCN_UNLIKELY(tmp.empty())) {
                    return {error::invalid_scanned_value,
                            "Empty string parsed"};
                }
                std::copy(tmp.begin(), tmp.end(), val.begin());
                val = val.first(tmp.size());

                return {};
            }
        };

        struct string_view_scanner : string_scanner {
        public:
            template <typename Context>
            error scan(basic_string_view<typename Context::char_type>& val,
                       Context& ctx)
            {
                if (!Context::range_type::is_contiguous) {
                    return {error::invalid_operation,
                            "Cannot read a string_view from a "
                            "non-contiguous_range"};
                }

                if (set_parser.enabled()) {
                    bool loc = (common_options & localized) != 0;
                    bool mb = (loc || set_parser.get_option(
                                          set_parser_type::flag::use_ranges)) &&
                              is_multichar_type(typename Context::char_type{});
                    return do_scan(ctx, val,
                                   string_scanner::pred<Context>{
                                       ctx, set_parser, loc, mb});
                }

                auto e = skip_range_whitespace(ctx, false);
                if (!e) {
                    return e;
                }

                auto is_space_pred = make_is_space_predicate(
                    ctx.locale(), (common_options & localized) != 0,
                    field_width);
                return do_scan(ctx, val, is_space_pred);
            }

        protected:
            template <typename Context, typename Pred>
            error do_scan(Context& ctx,
                          basic_string_view<typename Context::char_type>& val,
                          Pred&& predicate)
            {
                SCN_EXPECT(Context::range_type::is_contiguous);

                auto s = read_until_space_zero_copy(ctx.range(),
                                                    SCN_FWD(predicate), false);
                if (!s) {
                    return s.error();
                }
                if (s.value().size() == 0) {
                    return {error::invalid_scanned_value,
                            "Empty string parsed"};
                }
                val = basic_string_view<typename Context::char_type>(
                    s.value().data(), s.value().size());
                return {};
            }
        };

#if SCN_HAS_STRING_VIEW
        struct std_string_view_scanner : string_view_scanner {
            template <typename Context>
            error scan(std::basic_string_view<typename Context::char_type>& val,
                       Context& ctx)
            {
                using char_type = typename Context::char_type;
                auto sv =
                    ::scn::basic_string_view<char_type>(val.data(), val.size());
                auto e = string_view_scanner::scan(sv, ctx);
                if (e) {
                    val =
                        std::basic_string_view<char_type>(sv.data(), sv.size());
                }
                return e;
            }
        };
#endif
    }  // namespace detail
    SCN_END_NAMESPACE
}  // namespace scn

#endif
