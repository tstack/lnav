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
//
// The contents of this file are based on utfcpp:
//     https://github.com/nemtrif/utfcpp
//     Copyright (c) 2006 Nemanja Trifunovic
//     Distributed under the Boost Software License, version 1.0

#ifndef SCN_UNICODE_UNICODE_H
#define SCN_UNICODE_UNICODE_H

#include "utf16.h"
#include "utf8.h"

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace detail {
        inline constexpr bool is_wide_multichar()
        {
            return sizeof(wchar_t) == 2;
        }

        inline constexpr bool is_multichar_type(char)
        {
            return true;
        }
        inline constexpr bool is_multichar_type(wchar_t)
        {
            return is_wide_multichar();
        }

        using utf8_tag = std::integral_constant<size_t, 1>;
        using utf16_tag = std::integral_constant<size_t, 2>;
        using utf32_tag = std::integral_constant<size_t, 4>;

#define SCN_MAKE_UTF_TAG(CharT) \
    std::integral_constant<size_t, sizeof(CharT)> {}

        template <typename I, typename S>
        SCN_CONSTEXPR14 expected<I> parse_code_point(I begin,
                                                     S end,
                                                     code_point& cp,
                                                     utf8_tag)
        {
            return utf8::parse_code_point(begin, end, cp);
        }
        template <typename I, typename S>
        SCN_CONSTEXPR14 expected<I> parse_code_point(I begin,
                                                     S end,
                                                     code_point& cp,
                                                     utf16_tag)
        {
            return utf16::parse_code_point(begin, end, cp);
        }
        template <typename I, typename S>
        SCN_CONSTEXPR14 expected<I> parse_code_point(I begin,
                                                     S end,
                                                     code_point& cp,
                                                     utf32_tag)
        {
            SCN_EXPECT(begin != end);
            cp = make_code_point(*begin);
            return {++begin};
        }
    }  // namespace detail

    /**
     * Parses a Unicode code point from the range at `[begin, end)`, and writes
     * it into `cp`.
     *
     * The encoding is determined by the size of the value type of the range.
     * Let `n = sizeof(typename std::iterator_traits<I>::value_type)`.
     * If `n == 1` -> UTF-8. If `n == 2` -> UTF-16. If `n == 4` -> UTF-32.
     *
     * `begin != end` must be `true`.
     *
     * On error, `cp` is not written into.
     *
     * \return On success, returns an iterator one-past the last code unit used
     * to parse `cp`. If the code point is encoded incorrectly, returns
     * `error::invalid_encoding`.
     */
    template <typename I, typename S>
    SCN_CONSTEXPR14 expected<I> parse_code_point(I begin, S end, code_point& cp)
    {
        return detail::parse_code_point(
            begin, end, cp,
            SCN_MAKE_UTF_TAG(typename std::iterator_traits<I>::value_type));
    }

    namespace detail {
        template <typename I, typename S>
        SCN_CONSTEXPR14 expected<I> encode_code_point(I begin,
                                                      S end,
                                                      code_point cp,
                                                      utf8_tag)
        {
            return utf8::encode_code_point(begin, end, cp);
        }
        template <typename I, typename S>
        SCN_CONSTEXPR14 expected<I> encode_code_point(I begin,
                                                      S end,
                                                      code_point cp,
                                                      utf16_tag)
        {
            return utf16::encode_code_point(begin, end, cp);
        }
        template <typename I, typename S>
        SCN_CONSTEXPR14 expected<I> encode_code_point(I begin,
                                                      S end,
                                                      code_point cp,
                                                      utf32_tag)
        {
            SCN_EXPECT(begin + 1 >= end);
            *begin++ = static_cast<uint32_t>(cp);
            return {begin};
        }
    }  // namespace detail

    /**
     * Writes the code point `cp` into `begin`, using the encoding determined by
     * the type of `begin`.
     *
     * For more information on how the encoding is determined, see \ref
     * parse_code_point().
     *
     * `end` must be reachable from `begin`, and must have enough room to encode
     * the code point (4 code units for UTF-8, 2 for UTF-16, and 1 for UTF-32).
     *
     * \param begin Beginning of the range to write the result to
     * \param end End of the range to write the result to
     * \param cp Code point to encode
     * \return On success, one-past the last code unit written.
     * If `cp` was not a valid code point, returns `error::invalid_encoding`.
     */
    template <typename I, typename S>
    SCN_CONSTEXPR14 expected<I> encode_code_point(I begin, S end, code_point cp)
    {
        return detail::encode_code_point(
            begin, end, cp,
            SCN_MAKE_UTF_TAG(typename std::iterator_traits<I>::value_type));
    }

    namespace detail {
        template <typename T>
        SCN_CONSTEXPR14 int get_sequence_length(T a, utf8_tag)
        {
            return utf8::get_sequence_length(a);
        }
        template <typename T>
        SCN_CONSTEXPR14 int get_sequence_length(T a, utf16_tag)
        {
            return utf16::get_sequence_length(a);
        }
        template <typename T>
        SCN_CONSTEXPR14 int get_sequence_length(T, utf32_tag)
        {
            return 1;
        }
    }  // namespace detail

    /**
     * Returns the length of the code point starting from code unit `a` in code
     * units.
     *
     * For information on how the encoding is determined, see \ref
     * parse_code_point().
     *
     * \param a The first code unit in a code point.
     *
     * \return Length of the code point starting from `a`, in code units
     * If the code point is encoded incorrectly, or this code unit is not the
     * first code unit in a code point, returns 0.
     */
    template <typename T>
    SCN_CONSTEXPR14 int get_sequence_length(T a)
    {
        return detail::get_sequence_length(a, SCN_MAKE_UTF_TAG(T));
    }

    namespace detail {
        template <typename I, typename S>
        SCN_CONSTEXPR14 expected<std::ptrdiff_t> code_point_distance(I begin,
                                                                     S end,
                                                                     utf8_tag)
        {
            return utf8::code_point_distance(begin, end);
        }
        template <typename I, typename S>
        SCN_CONSTEXPR14 expected<std::ptrdiff_t> code_point_distance(I begin,
                                                                     S end,
                                                                     utf16_tag)
        {
            return utf16::code_point_distance(begin, end);
        }
        template <typename I, typename S>
        SCN_CONSTEXPR14 expected<std::ptrdiff_t> code_point_distance(I begin,
                                                                     S end,
                                                                     utf32_tag)
        {
            return {end - begin};
        }
    }  // namespace detail

    /**
     * Get the distance between two code points, in code points.
     *
     * `end >= begin` must be `true`.
     * `begin` and `end` must both point to the first code units in a code
     * point.
     *
     * \return The distance between `begin` and `end`, in code points. If the
     * string was encoded incorrectly, returns `error::invalid_encoding`.
     */
    template <typename I, typename S>
    SCN_CONSTEXPR14 expected<std::ptrdiff_t> code_point_distance(I begin, S end)
    {
        return detail::code_point_distance(
            begin, end,
            SCN_MAKE_UTF_TAG(typename std::iterator_traits<I>::value_type));
    }

#undef SCN_MAKE_UTF_TAG

    SCN_END_NAMESPACE
}  // namespace scn

#endif
