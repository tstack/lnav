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

#if defined(SCN_HEADER_ONLY) && SCN_HEADER_ONLY
#define SCN_READER_FLOAT_CPP
#endif

#include <scn/detail/args.h>
#include <scn/reader/float.h>

#include <cerrno>
#include <clocale>

#if SCN_HAS_FLOAT_CHARCONV
#include <charconv>
#endif

SCN_GCC_PUSH
SCN_GCC_IGNORE("-Wold-style-cast")
SCN_GCC_IGNORE("-Wnoexcept")
SCN_GCC_IGNORE("-Wshift-count-overflow")
SCN_GCC_IGNORE("-Wsign-conversion")

SCN_CLANG_PUSH
SCN_CLANG_IGNORE("-Wold-style-cast")

#if SCN_CLANG >= SCN_COMPILER(13, 0, 0)
SCN_CLANG_IGNORE("-Wreserved-identifier")
#endif

#if SCN_CLANG >= SCN_COMPILER(10, 0, 0)
SCN_CLANG_IGNORE("-Wextra-semi-stmt")
#endif

#include <fast_float/fast_float.h>

SCN_CLANG_POP
SCN_GCC_POP

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace read_float {
        static bool is_hexfloat(const char* str, std::size_t len) noexcept
        {
            if (len < 3) {
                return false;
            }
            return str[0] == '0' && (str[1] == 'x' || str[1] == 'X');
        }
        static bool is_hexfloat(const wchar_t* str, std::size_t len) noexcept
        {
            if (len < 3) {
                return false;
            }
            return str[0] == L'0' && (str[1] == L'x' || str[1] == L'X');
        }

        namespace cstd {
#if SCN_GCC >= SCN_COMPILER(7, 0, 0)
            SCN_GCC_PUSH
            SCN_GCC_IGNORE("-Wnoexcept-type")
#endif
            template <typename T, typename CharT, typename F>
            expected<T> impl(F&& f_strtod,
                             T huge_value,
                             const CharT* str,
                             size_t& chars,
                             uint8_t options)
            {
                // Get current C locale
                const auto loc = std::setlocale(LC_NUMERIC, nullptr);
                // For whatever reason, this cannot be stored in the heap if
                // setlocale hasn't been called before, or msan errors with
                // 'use-of-unitialized-value' when resetting the locale back.
                // POSIX specifies that the content of loc may not be static, so
                // we need to save it ourselves
                char locbuf[64] = {0};
                std::strcpy(locbuf, loc);

                std::setlocale(LC_NUMERIC, "C");

                CharT* end{};
                errno = 0;
                T f = f_strtod(str, &end);
                chars = static_cast<size_t>(end - str);
                auto err = errno;
                // Reset locale
                std::setlocale(LC_NUMERIC, locbuf);
                errno = 0;

                SCN_GCC_COMPAT_PUSH
                SCN_GCC_COMPAT_IGNORE("-Wfloat-equal")
                // No conversion
                if (f == detail::zero_value<T>::value && chars == 0) {
                    return error(error::invalid_scanned_value, "strtod");
                }
                // Range error
                if (err == ERANGE) {
                    // Underflow
                    if (f == detail::zero_value<T>::value) {
                        return error(
                            error::value_out_of_range,
                            "Floating-point value out of range: underflow");
                    }
                    // Overflow
                    if (f == huge_value || f == -huge_value) {
                        return error(
                            error::value_out_of_range,
                            "Floating-point value out of range: overflow");
                    }
                    // Subnormals cause ERANGE but a value is still returned
                }

                if (is_hexfloat(str, detail::strlen(str)) &&
                    (options & detail::float_scanner<T>::allow_hex) == 0) {
                    return error(error::invalid_scanned_value,
                                 "Hexfloats not allowed by the format string");
                }

                SCN_GCC_COMPAT_POP
                return f;
            }
#if SCN_GCC >= SCN_COMPILER(7, 0, 0)
            SCN_GCC_POP
#endif

            template <typename CharT, typename T>
            struct read;

            template <>
            struct read<char, float> {
                static expected<float> get(const char* str,
                                           size_t& chars,
                                           uint8_t options)
                {
                    return impl<float>(strtof, HUGE_VALF, str, chars, options);
                }
            };

            template <>
            struct read<char, double> {
                static expected<double> get(const char* str,
                                            size_t& chars,
                                            uint8_t options)
                {
                    return impl<double>(strtod, HUGE_VAL, str, chars, options);
                }
            };

            template <>
            struct read<char, long double> {
                static expected<long double> get(const char* str,
                                                 size_t& chars,
                                                 uint8_t options)
                {
                    return impl<long double>(strtold, HUGE_VALL, str, chars,
                                             options);
                }
            };

            template <>
            struct read<wchar_t, float> {
                static expected<float> get(const wchar_t* str,
                                           size_t& chars,
                                           uint8_t options)
                {
                    return impl<float>(wcstof, HUGE_VALF, str, chars, options);
                }
            };
            template <>
            struct read<wchar_t, double> {
                static expected<double> get(const wchar_t* str,
                                            size_t& chars,
                                            uint8_t options)
                {
                    return impl<double>(wcstod, HUGE_VAL, str, chars, options);
                }
            };
            template <>
            struct read<wchar_t, long double> {
                static expected<long double> get(const wchar_t* str,
                                                 size_t& chars,
                                                 uint8_t options)
                {
                    return impl<long double>(wcstold, HUGE_VALL, str, chars,
                                             options);
                }
            };
        }  // namespace cstd

        namespace from_chars {
#if SCN_HAS_FLOAT_CHARCONV
            template <typename T>
            struct read {
                static expected<T> get(const char* str,
                                       size_t& chars,
                                       uint8_t options)
                {
                    const auto len = std::strlen(str);
                    std::chars_format flags{};
                    if (((options & detail::float_scanner<T>::allow_hex) !=
                         0) &&
                        is_hexfloat(str, len)) {
                        str += 2;
                        flags = std::chars_format::hex;
                    }
                    else {
                        if ((options & detail::float_scanner<T>::allow_fixed) !=
                            0) {
                            flags |= std::chars_format::fixed;
                        }
                        if ((options &
                             detail::float_scanner<T>::allow_scientific) != 0) {
                            flags |= std::chars_format::scientific;
                        }
                    }
                    if (flags == static_cast<std::chars_format>(0)) {
                        return error{error::invalid_scanned_value,
                                     "Expected a hexfloat"};
                    }

                    T value{};
                    const auto result =
                        std::from_chars(str, str + len, value, flags);
                    if (result.ec == std::errc::invalid_argument) {
                        return error(error::invalid_scanned_value,
                                     "from_chars");
                    }
                    if (result.ec == std::errc::result_out_of_range) {
                        // Out of range, may be subnormal -> fall back to strtod
                        // On gcc std::from_chars doesn't parse subnormals
                        return cstd::read<char, T>::get(str, chars, options);
                    }
                    chars = static_cast<size_t>(result.ptr - str);
                    return value;
                }
            };
#else
            template <typename T>
            struct read {
                static expected<T> get(const char* str,
                                       size_t& chars,
                                       uint8_t options)
                {
                    // Fall straight back to strtod
                    return cstd::read<char, T>::get(str, chars, options);
                }
            };
#endif
        }  // namespace from_chars

        namespace fast_float {
            template <typename T>
            expected<T> impl(const char* str,
                             size_t& chars,
                             uint8_t options,
                             char locale_decimal_point)
            {
                const auto len = std::strlen(str);
                if (((options & detail::float_scanner<T>::allow_hex) != 0) &&
                    is_hexfloat(str, len)) {
                    // fast_float doesn't support hexfloats
                    return from_chars::read<T>::get(str, chars, options);
                }

                T value{};
                ::fast_float::parse_options flags{};
                if ((options & detail::float_scanner<T>::allow_fixed) != 0) {
                    flags.format = ::fast_float::fixed;
                }
                if ((options & detail::float_scanner<T>::allow_scientific) !=
                    0) {
                    flags.format = static_cast<::fast_float::chars_format>(
                        flags.format | ::fast_float::scientific);
                }
                if ((options & detail::float_scanner<T>::localized) != 0) {
                    flags.decimal_point = locale_decimal_point;
                }

                const auto result = ::fast_float::from_chars_advanced(
                    str, str + len, value, flags);
                if (result.ec == std::errc::invalid_argument) {
                    return error(error::invalid_scanned_value, "fast_float");
                }
                if (result.ec == std::errc::result_out_of_range) {
                    return error(error::value_out_of_range, "fast_float");
                }
                if (std::isinf(value)) {
                    // fast_float represents very large or small values as inf
                    // But, it also parses "inf", which from_chars does not
                    if (!(len >= 3 && (str[0] == 'i' || str[0] == 'I'))) {
                        // Input was not actually infinity ->
                        // invalid result, fall back to from_chars
                        return from_chars::read<T>::get(str, chars, options);
                    }
                }
                chars = static_cast<size_t>(result.ptr - str);
                return value;
            }

            template <typename T>
            struct read;

            template <>
            struct read<float> {
                static expected<float> get(const char* str,
                                           size_t& chars,
                                           uint8_t options,
                                           char locale_decimal_point)
                {
                    return impl<float>(str, chars, options,
                                       locale_decimal_point);
                }
            };
            template <>
            struct read<double> {
                static expected<double> get(const char* str,
                                            size_t& chars,
                                            uint8_t options,
                                            char locale_decimal_points)
                {
                    return impl<double>(str, chars, options,
                                        locale_decimal_points);
                }
            };
            template <>
            struct read<long double> {
                static expected<long double> get(const char* str,
                                                 size_t& chars,
                                                 uint8_t options,
                                                 char)
                {
                    // Fallback to strtod
                    // fast_float doesn't support long double
                    return cstd::read<char, long double>::get(str, chars,
                                                              options);
                }
            };
        }  // namespace fast_float

        template <typename CharT, typename T>
        struct read;

        template <typename T>
        struct read<char, T> {
            static expected<T> get(const char* str,
                                   size_t& chars,
                                   uint8_t options,
                                   char locale_decimal_points)
            {
                // char -> default to fast_float,
                // fallback to strtod if necessary
                return read_float::fast_float::read<T>::get(
                    str, chars, options, locale_decimal_points);
            }
        };
        template <typename T>
        struct read<wchar_t, T> {
            static expected<T> get(const wchar_t* str,
                                   size_t& chars,
                                   uint8_t options,
                                   wchar_t)
            {
                // wchar_t -> straight to strtod
                return read_float::cstd::read<wchar_t, T>::get(str, chars,
                                                               options);
            }
        };
    }  // namespace read_float

    namespace detail {
        template <typename T>
        template <typename CharT>
        expected<T> float_scanner<T>::_read_float_impl(
            const CharT* str,
            size_t& chars,
            CharT locale_decimal_point)
        {
            // Parsing algorithm to use:
            // If CharT == wchar_t -> strtod
            // If CharT == char:
            //   1. fast_float
            //      fallback if a hex float, or incorrectly parsed an inf
            //      (very large or small value)
            //   2. std::from_chars
            //      fallback if not available (C++17) or float is subnormal
            //   3. std::strtod
            return read_float::read<CharT, T>::get(str, chars, format_options,
                                                   locale_decimal_point);
        }

#if SCN_INCLUDE_SOURCE_DEFINITIONS

        template expected<float>
        float_scanner<float>::_read_float_impl(const char*, size_t&, char);
        template expected<double>
        float_scanner<double>::_read_float_impl(const char*, size_t&, char);
        template expected<long double>
        float_scanner<long double>::_read_float_impl(const char*,
                                                     size_t&,
                                                     char);
        template expected<float> float_scanner<float>::_read_float_impl(
            const wchar_t*,
            size_t&,
            wchar_t);
        template expected<double> float_scanner<double>::_read_float_impl(
            const wchar_t*,
            size_t&,
            wchar_t);
        template expected<long double>
        float_scanner<long double>::_read_float_impl(const wchar_t*,
                                                     size_t&,
                                                     wchar_t);
#endif
    }  // namespace detail

    SCN_END_NAMESPACE
}  // namespace scn
