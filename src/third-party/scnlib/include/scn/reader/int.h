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

#ifndef SCN_READER_INT_H
#define SCN_READER_INT_H

#include "../util/math.h"
#include "common.h"

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace detail {
        template <typename T>
        struct integer_scanner : common_parser {
            static_assert(std::is_integral<T>::value,
                          "integer_scanner requires an integral type");

            friend struct simple_integer_scanner<T>;

            bool skip_preceding_whitespace()
            {
                // if format_options == single_code_unit,
                // then we're scanning a char -> don't skip
                return format_options != single_code_unit;
            }

            template <typename ParseCtx>
            error parse(ParseCtx& pctx)
            {
                using char_type = typename ParseCtx::char_type;

                format_options = 0;

                int custom_base = 0;
                auto each = [&](ParseCtx& p, bool& parsed) -> error {
                    parsed = false;
                    auto ch = pctx.next_char();

                    if (ch == detail::ascii_widen<char_type>('B')) {
                        // Custom base
                        p.advance_char();
                        if (SCN_UNLIKELY(!p)) {
                            return {error::invalid_format_string,
                                    "Unexpected format string end"};
                        }
                        if (SCN_UNLIKELY(p.check_arg_end())) {
                            return {error::invalid_format_string,
                                    "Unexpected argument end"};
                        }
                        ch = p.next_char();

                        const auto zero = detail::ascii_widen<char_type>('0'),
                                   nine = detail::ascii_widen<char_type>('9');
                        integer_type_for_char<char_type> tmp = 0;
                        if (ch < zero || ch > nine) {
                            return {error::invalid_format_string,
                                    "Invalid character after 'B', "
                                    "expected digit"};
                        }
                        tmp = static_cast<integer_type_for_char<char_type>>(
                            p.next_char() - zero);
                        if (tmp < 1) {
                            return {error::invalid_format_string,
                                    "Invalid base, must be between 2 and 36"};
                        }

                        p.advance_char();
                        if (!p) {
                            return {error::invalid_format_string,
                                    "Unexpected end of format string"};
                        }
                        if (p.check_arg_end()) {
                            custom_base = static_cast<uint8_t>(tmp);
                            parsed = true;
                            return {};
                        }
                        ch = p.next_char();

                        if (ch < zero || ch > nine) {
                            return {error::invalid_format_string,
                                    "Invalid character after 'B', "
                                    "expected digit"};
                        }
                        tmp *= 10;
                        tmp += static_cast<integer_type_for_char<char_type>>(
                            ch - zero);
                        if (tmp < 2 || tmp > 36) {
                            return {error::invalid_format_string,
                                    "Invalid base, must be between 2 and 36"};
                        }
                        custom_base = static_cast<uint8_t>(tmp);
                        parsed = true;
                        pctx.advance_char();
                        return {};
                    }

                    return {};
                };

                array<char_type, 9> options{{// decimal
                                             ascii_widen<char_type>('d'),
                                             // binary
                                             ascii_widen<char_type>('b'),
                                             // octal
                                             ascii_widen<char_type>('o'),
                                             // hex
                                             ascii_widen<char_type>('x'),
                                             // detect base
                                             ascii_widen<char_type>('i'),
                                             // unsigned decimal
                                             ascii_widen<char_type>('u'),
                                             // code unit
                                             ascii_widen<char_type>('c'),
                                             // localized digits
                                             ascii_widen<char_type>('n'),
                                             // thsep
                                             ascii_widen<char_type>('\'')}};
                bool flags[9] = {false};

                auto e = parse_common(
                    pctx, span<const char_type>{options.begin(), options.end()},
                    span<bool>{flags, 9}, each);
                if (!e) {
                    return e;
                }

                int base_flags_set = int(flags[0]) + int(flags[1]) +
                                     int(flags[2]) + int(flags[3]) +
                                     int(flags[4]) + int(flags[5]) +
                                     int(custom_base != 0);
                if (SCN_UNLIKELY(base_flags_set > 1)) {
                    return {error::invalid_format_string,
                            "Up to one base flags ('d', 'i', 'u', 'b', 'o', "
                            "'x', 'B') allowed"};
                }
                else if (base_flags_set == 0) {
                    // Default:
                    //   'c' for CharT
                    //   'd' otherwise
                    if (std::is_same<T, typename ParseCtx::char_type>::value) {
                        format_options = single_code_unit;
                    }
                    else {
                        base = 10;
                    }
                }
                else if (custom_base != 0) {
                    // B__
                    base = static_cast<uint8_t>(custom_base);
                }
                else if (flags[0]) {
                    // 'd' flag
                    base = 10;
                }
                else if (flags[1]) {
                    // 'b' flag
                    base = 2;
                    format_options |= allow_base_prefix;
                }
                else if (flags[2]) {
                    // 'o' flag
                    base = 8;
                    format_options |= allow_base_prefix;
                }
                else if (flags[3]) {
                    // 'x' flag
                    base = 16;
                    format_options |= allow_base_prefix;
                }
                else if (flags[4]) {
                    // 'i' flag
                    base = 0;
                }
                else if (flags[5]) {
                    // 'u' flag
                    base = 10;
                    format_options |= only_unsigned;
                }

                // n set, implies L
                if (flags[7]) {
                    common_options |= localized;
                    format_options |= localized_digits;
                }
                if ((format_options & localized_digits) != 0 &&
                    (base != 0 && base != 10 && base != 8 && base != 16)) {
                    return {error::invalid_format_string,
                            "Localized integers can only be scanned in "
                            "bases 8, 10 and 16"};
                }

                // thsep flag
                if (flags[8]) {
                    format_options |= allow_thsep;
                }

                // 'c' flag -> no other options allowed
                if (flags[6]) {
                    if (!(format_options == 0 ||
                          format_options == single_code_unit) ||
                        base_flags_set != 0) {
                        return {error::invalid_format_string,
                                "'c' flag cannot be used in conjunction with "
                                "any other flags"};
                    }
                    format_options = single_code_unit;
                }

                return {};
            }

            template <typename Context>
            error scan(T& val, Context& ctx)
            {
                using char_type = typename Context::char_type;
                auto do_parse_int = [&](span<const char_type> s) -> error {
                    T tmp = 0;
                    expected<std::ptrdiff_t> ret{0};
                    if (SCN_UNLIKELY((format_options & localized_digits) !=
                                     0)) {
                        SCN_CLANG_PUSH_IGNORE_UNDEFINED_TEMPLATE
                        int b{base};
                        auto r = parse_base_prefix<char_type>(s, b);
                        if (!r) {
                            return r.error();
                        }
                        if (b == -1) {
                            // -1 means we read a '0'
                            tmp = 0;
                            return {};
                        }
                        if (b != 10 && base != b && base != 0) {
                            return {error::invalid_scanned_value,
                                    "Invalid base prefix"};
                        }
                        if (base == 0) {
                            base = static_cast<uint8_t>(b);
                        }
                        if (base != 8 && base != 10 && base != 16) {
                            return {error::invalid_scanned_value,
                                    "Localized values have to be in base "
                                    "8, 10 or 16"};
                        }

                        auto it = r.value();
                        std::basic_string<char_type> str(to_address(it),
                                                         s.size());
                        ret = ctx.locale().get_localized().read_num(
                            tmp, str, static_cast<int>(base));

                        if (tmp < T{0} &&
                            (format_options & only_unsigned) != 0) {
                            return {error::invalid_scanned_value,
                                    "Parsed negative value when type was 'u'"};
                        }
                        SCN_CLANG_POP_IGNORE_UNDEFINED_TEMPLATE
                    }
                    else {
                        SCN_CLANG_PUSH_IGNORE_UNDEFINED_TEMPLATE
                        ret = _parse_int(tmp, s);
                        SCN_CLANG_POP_IGNORE_UNDEFINED_TEMPLATE
                    }

                    if (!ret) {
                        return ret.error();
                    }
                    if (ret.value() != s.ssize()) {
                        auto pb =
                            putback_n(ctx.range(), s.ssize() - ret.value());
                        if (!pb) {
                            return pb;
                        }
                    }
                    val = tmp;
                    return {};
                };

                if (format_options == single_code_unit) {
                    SCN_MSVC_PUSH
                    SCN_MSVC_IGNORE(4127)  // conditional expression is constant
                    if (sizeof(T) < sizeof(char_type)) {
                        // sizeof(char_type) > 1 -> wide range
                        // Code unit might not fit
                        return error{error::invalid_operation,
                                     "Cannot read this type as a code unit "
                                     "from a wide range"};
                    }
                    SCN_MSVC_POP
                    auto ch = read_code_unit(ctx.range());
                    if (!ch) {
                        return ch.error();
                    }
                    val = static_cast<T>(ch.value());
                    return {};
                }

                SCN_MSVC_PUSH
                SCN_MSVC_IGNORE(4127)  // conditional expression is constant
                if ((std::is_same<T, char>::value ||
                     std::is_same<T, wchar_t>::value) &&
                    !std::is_same<T, char_type>::value) {
                    // T is a character type, but not char_type:
                    // Trying to read a char from a wide range, or wchar_t from
                    // a narrow one
                    // Reading a code unit is allowed, however
                    return error{error::invalid_operation,
                                 "Cannot read a char from a wide range, or a "
                                 "wchar_t from a narrow one"};
                }
                SCN_MSVC_POP

                std::basic_string<char_type> buf{};
                span<const char_type> bufspan{};
                auto e = _read_source(
                    ctx, buf, bufspan,
                    std::integral_constant<
                        bool, Context::range_type::is_contiguous>{});
                if (!e) {
                    return e;
                }

                return do_parse_int(bufspan);
            }

            enum format_options_type : uint8_t {
                // "n" option -> localized digits and digit grouping
                localized_digits = 1,
                // "'" option -> accept thsep
                // if "L" use locale, default=','
                allow_thsep = 2,
                // "u" option -> don't allow sign
                only_unsigned = 4,
                // Allow base prefix (e.g. 0B and 0x)
                allow_base_prefix = 8,
                // "c" option -> scan a code unit
                single_code_unit = 16,
            };
            uint8_t format_options{default_format_options()};

            // 0 = detect base
            // Otherwise [2,36]
            uint8_t base{0};

        private:
            static SCN_CONSTEXPR14 uint8_t default_format_options()
            {
                SCN_MSVC_PUSH
                SCN_MSVC_IGNORE(4127)  // conditional expression is constant
                if (std::is_same<T, char>::value ||
                    std::is_same<T, wchar_t>::value) {
                    return single_code_unit;
                }
                return 0;
                SCN_MSVC_POP
            }

            template <typename Context, typename Buf, typename CharT>
            error _read_source(Context& ctx,
                               Buf& buf,
                               span<const CharT>& s,
                               std::false_type)
            {
                auto do_read = [&](Buf& b) -> error {
                    auto outputit = std::back_inserter(b);
                    auto is_space_pred = make_is_space_predicate(
                        ctx.locale(), (common_options & localized) != 0,
                        field_width);
                    auto e = read_until_space(ctx.range(), outputit,
                                              is_space_pred, false);
                    if (!e && b.empty()) {
                        return e;
                    }

                    return {};
                };

                if (SCN_LIKELY((format_options & allow_thsep) == 0)) {
                    auto e = do_read(buf);
                    if (!e) {
                        return e;
                    }
                    s = make_span(buf.data(), buf.size());
                    return {};
                }

                Buf tmp;
                auto e = do_read(tmp);
                if (!e) {
                    return e;
                }
                auto thsep = ctx.locale()
                                 .get((common_options & localized) != 0)
                                 .thousands_separator();

                auto it = tmp.begin();
                for (; it != tmp.end(); ++it) {
                    if (*it == thsep) {
                        for (auto it2 = it; ++it2 != tmp.end();) {
                            *it++ = SCN_MOVE(*it2);
                        }
                        break;
                    }
                }

                auto n =
                    static_cast<std::size_t>(std::distance(tmp.begin(), it));
                if (n == 0) {
                    return {error::invalid_scanned_value,
                            "Only a thousands separator found"};
                }

                buf = SCN_MOVE(tmp);
                s = make_span(buf.data(), n);
                return {};
            }

            template <typename Context, typename Buf, typename CharT>
            error _read_source(Context& ctx,
                               Buf& buf,
                               span<const CharT>& s,
                               std::true_type)
            {
                if (SCN_UNLIKELY((format_options & allow_thsep) != 0)) {
                    return _read_source(ctx, buf, s, std::false_type{});
                }
                auto ret = read_zero_copy(
                    ctx.range(), field_width != 0
                                     ? static_cast<std::ptrdiff_t>(field_width)
                                     : ctx.range().size());
                if (!ret) {
                    return ret.error();
                }
                s = ret.value();
                return {};
            }

            template <typename CharT>
            expected<typename span<const CharT>::iterator> parse_base_prefix(
                span<const CharT> s,
                int& b) const;

            template <typename CharT>
            expected<std::ptrdiff_t> _parse_int(T& val, span<const CharT> s);

            template <typename CharT>
            expected<typename span<const CharT>::iterator> _parse_int_impl(
                T& val,
                bool minus_sign,
                span<const CharT> buf) const;
        };

        // instantiate
        template struct integer_scanner<signed char>;
        template struct integer_scanner<short>;
        template struct integer_scanner<int>;
        template struct integer_scanner<long>;
        template struct integer_scanner<long long>;
        template struct integer_scanner<unsigned char>;
        template struct integer_scanner<unsigned short>;
        template struct integer_scanner<unsigned int>;
        template struct integer_scanner<unsigned long>;
        template struct integer_scanner<unsigned long long>;
        template struct integer_scanner<char>;
        template struct integer_scanner<wchar_t>;

        template <typename T>
        template <typename CharT>
        expected<typename span<const CharT>::iterator>
        simple_integer_scanner<T>::scan(span<const CharT> buf,
                                        T& val,
                                        int base,
                                        uint16_t flags)
        {
            SCN_EXPECT(buf.size() != 0);

            integer_scanner<T> s{};
            s.base = static_cast<uint8_t>(base);
            s.format_options = flags & 0xffu;
            s.common_options = static_cast<uint8_t>(flags >> 8u);
            SCN_CLANG_PUSH_IGNORE_UNDEFINED_TEMPLATE
            auto n = s._parse_int(val, buf);
            SCN_CLANG_POP_IGNORE_UNDEFINED_TEMPLATE
            if (!n) {
                return n.error();
            }
            return buf.begin() + n.value();
        }
        template <typename T>
        template <typename CharT>
        expected<typename span<const CharT>::iterator>
        simple_integer_scanner<T>::scan_lower(span<const CharT> buf,
                                              T& val,
                                              int base,
                                              uint16_t flags)
        {
            SCN_EXPECT(buf.size() != 0);
            SCN_EXPECT(base > 0);

            integer_scanner<T> s{};
            s.base = static_cast<uint8_t>(base);
            s.format_options = flags & 0xffu;
            s.common_options = static_cast<uint8_t>(flags >> 8u);

            bool minus_sign = false;
            if (buf[0] == ascii_widen<CharT>('-')) {
                buf = buf.subspan(1);
                minus_sign = true;
            }

            SCN_CLANG_PUSH_IGNORE_UNDEFINED_TEMPLATE
            return s._parse_int_impl(val, minus_sign, buf);
            SCN_CLANG_POP_IGNORE_UNDEFINED_TEMPLATE
        }
    }  // namespace detail
    SCN_END_NAMESPACE
}  // namespace scn

#if defined(SCN_HEADER_ONLY) && SCN_HEADER_ONLY && !defined(SCN_READER_INT_CPP)
#include "reader_int.cpp"
#endif

#endif
