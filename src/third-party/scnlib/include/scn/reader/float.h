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

#ifndef SCN_READER_FLOAT_H
#define SCN_READER_FLOAT_H

#include "../util/small_vector.h"
#include "common.h"

namespace scn {
    SCN_BEGIN_NAMESPACE
    namespace detail {
        template <typename T>
        struct float_scanner_access;

        template <typename T>
        struct float_scanner : common_parser {
            static_assert(std::is_floating_point<T>::value,
                          "float_scanner requires a floating point type");

            friend struct float_scanner_access<T>;

            template <typename ParseCtx>
            error parse(ParseCtx& pctx)
            {
                using char_type = typename ParseCtx::char_type;

                array<char_type, 10> options{
                    {// hex
                     ascii_widen<char_type>('a'), ascii_widen<char_type>('A'),
                     // scientific
                     ascii_widen<char_type>('e'), ascii_widen<char_type>('E'),
                     // fixed
                     ascii_widen<char_type>('f'), ascii_widen<char_type>('F'),
                     // general
                     ascii_widen<char_type>('g'), ascii_widen<char_type>('G'),
                     // localized digits
                     ascii_widen<char_type>('n'),
                     // thsep
                     ascii_widen<char_type>('\'')}};
                bool flags[10] = {false};

                auto e = parse_common(
                    pctx, span<const char_type>{options.begin(), options.end()},
                    span<bool>{flags, 10}, null_type_cb<ParseCtx>);
                if (!e) {
                    return e;
                }

                if (flags[0] && flags[1]) {
                    return {error::invalid_format_string,
                            "Can't have both 'a' and 'A' flags with floats"};
                }
                if (flags[2] && flags[3]) {
                    return {error::invalid_format_string,
                            "Can't have both 'e' and 'E' flags with floats"};
                }
                if (flags[4] && flags[5]) {
                    return {error::invalid_format_string,
                            "Can't have both 'f' and 'F' flags with floats"};
                }
                if (flags[6] && flags[7]) {
                    return {error::invalid_format_string,
                            "Can't have both 'g' and 'G' flags with floats"};
                }

                bool set_hex = flags[0] || flags[1];
                bool set_scientific = flags[2] || flags[3];
                bool set_fixed = flags[4] || flags[5];
                bool set_general = flags[6] || flags[7];
                if (set_general && set_fixed) {
                    return {error::invalid_format_string,
                            "General float already implies fixed"};
                }
                if (set_general && set_scientific) {
                    return {error::invalid_format_string,
                            "General float already implies scientific"};
                }

                format_options = 0;
                if (set_hex) {
                    format_options |= allow_hex;
                }
                if (set_scientific) {
                    format_options |= allow_scientific;
                }
                if (set_fixed) {
                    format_options |= allow_fixed;
                }
                if (set_general) {
                    format_options |= allow_fixed | allow_scientific;
                }
                if (format_options == 0) {
                    format_options |=
                        allow_fixed | allow_scientific | allow_hex;
                }

                // 'n'
                if (flags[8]) {
                    common_options |= localized;
                    format_options |= localized_digits;
                }

                // thsep
                if (flags[9]) {
                    format_options |= allow_thsep;
                }

                return {};
            }

            template <typename Context>
            error scan(T& val, Context& ctx)
            {
                using char_type = typename Context::char_type;

                auto do_parse_float = [&](span<const char_type> s) -> error {
                    T tmp = 0;
                    expected<std::ptrdiff_t> ret{0};
                    if (SCN_UNLIKELY((format_options & localized_digits) != 0 ||
                                     ((common_options & localized) != 0 &&
                                      (format_options & allow_hex) != 0))) {
                        // 'n' OR ('L' AND 'a')
                        // because none of our parsers support BOTH hexfloats
                        // and custom (localized) decimal points,
                        // so we have to fall back on iostreams
                        SCN_CLANG_PUSH_IGNORE_UNDEFINED_TEMPLATE
                        std::basic_string<char_type> str(s.data(), s.size());
                        ret =
                            ctx.locale().get_localized().read_num(tmp, str, 0);
                        SCN_CLANG_POP_IGNORE_UNDEFINED_TEMPLATE
                    }
                    else {
                        ret = _read_float(
                            tmp, s,
                            ctx.locale()
                                .get((common_options & localized) != 0)
                                .decimal_point());
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

                auto is_space_pred = make_is_space_predicate(
                    ctx.locale(), (common_options & localized) != 0,
                    field_width);

                if (Context::range_type::is_contiguous) {
                    auto s = read_until_space_zero_copy(ctx.range(),
                                                        is_space_pred, false);
                    if (!s) {
                        return s.error();
                    }
                    return do_parse_float(s.value());
                }

                small_vector<char_type, 32> buf;
                auto outputit = std::back_inserter(buf);
                auto e = read_until_space(ctx.range(), outputit, is_space_pred,
                                          false);
                if (!e && buf.empty()) {
                    return e;
                }

                return do_parse_float(make_span(buf));
            }

            enum format_options_type {
                allow_hex = 1,
                allow_scientific = 2,
                allow_fixed = 4,
                localized_digits = 8,
                allow_thsep = 16
            };
            uint8_t format_options{allow_hex | allow_scientific | allow_fixed};

        private:
            template <typename CharT>
            expected<std::ptrdiff_t> _read_float(T& val,
                                                 span<const CharT> s,
                                                 CharT locale_decimal_point)
            {
                size_t chars{};
                std::basic_string<CharT> str(s.data(), s.size());
                SCN_CLANG_PUSH_IGNORE_UNDEFINED_TEMPLATE
                auto ret =
                    _read_float_impl(str.data(), chars, locale_decimal_point);
                SCN_CLANG_POP_IGNORE_UNDEFINED_TEMPLATE
                if (!ret) {
                    return ret.error();
                }
                val = ret.value();
                return static_cast<std::ptrdiff_t>(chars);
            }

            template <typename CharT>
            expected<T> _read_float_impl(const CharT* str,
                                         size_t& chars,
                                         CharT locale_decimal_point);
        };

        // instantiate
        template struct float_scanner<float>;
        template struct float_scanner<double>;
        template struct float_scanner<long double>;

        template <typename T>
        struct float_scanner_access : public float_scanner<T> {
            using float_scanner<T>::_read_float;
            using float_scanner<T>::_read_float_impl;
        };
    }  // namespace detail
    SCN_END_NAMESPACE
}  // namespace scn

#if defined(SCN_HEADER_ONLY) && SCN_HEADER_ONLY && \
    !defined(SCN_READER_FLOAT_CPP)
#include "reader_float.cpp"
#endif

#endif
