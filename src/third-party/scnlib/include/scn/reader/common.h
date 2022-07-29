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

#ifndef SCN_READER_COMMON_H
#define SCN_READER_COMMON_H

#include "../detail/error.h"
#include "../detail/locale.h"
#include "../detail/range.h"
#include "../unicode/unicode.h"
#include "../util/algorithm.h"

namespace scn {
    SCN_BEGIN_NAMESPACE

    // read_code_unit

    namespace detail {
        template <typename WrappedRange>
        expected<typename WrappedRange::char_type>
        read_code_unit_impl(WrappedRange& r, bool advance, std::true_type)
        {
            SCN_CLANG_PUSH
            // clang 10 behaves weirdly
            SCN_CLANG_IGNORE("-Wzero-as-null-pointer-constant")
            SCN_EXPECT(r.begin() < r.end());
            SCN_CLANG_POP
            auto ch = *r.begin();
            if (advance) {
                r.advance();
            }
            return {ch};
        }
        template <typename WrappedRange>
        expected<typename WrappedRange::char_type>
        read_code_unit_impl(WrappedRange& r, bool advance, std::false_type)
        {
            SCN_EXPECT(r.begin() != r.end());
            auto ch = *r.begin();
            if (advance && ch) {
                r.advance();
            }
            return ch;
        }
    }  // namespace detail

    /**
     * Reads a single character (= code unit) from the range.
     * Dereferences the begin iterator, wrapping it in an `expected` if
     * necessary.
     *
     * Encoding-agnostic, doesn't care about code points, and may leave behind
     * partial ones.
     *
     * \param r Range to read from
     * \param advance If `true`, and the read was successful, the range is
     * advanced by a single character, as if by calling `r.advance()`.
     *
     * \return The next character in the range, obtained as if by dereferencing
     * the begin iterator `*r.begin()`.
     * If `r.begin() == r.end()`, returns EOF.
     * If `r` is direct, returns `*r.begin()` wrapped in an `expected`.
     * If `r` is not direct, returns `*r.begin()` as-is, with any errors that
     * may have been caused by the read.
     */
    template <typename WrappedRange>
    expected<typename WrappedRange::char_type> read_code_unit(
        WrappedRange& r,
        bool advance = true)
    {
        if (r.begin() == r.end()) {
            return error(error::end_of_range, "EOF");
        }
        return detail::read_code_unit_impl(
            r, advance,
            std::integral_constant<bool, WrappedRange::is_direct>{});
    }

    // putback_n

    /// @{

    /**
     * Puts back `n` characters (= code units) into `r` as if by repeatedly
     * calling `r.advance(-1)`.
     *
     * Encoding-agnostic, may leave behind partial code points.
     *
     * \param r Range to roll back
     * \param n Characters to put back, must be less than or equal to the number
     * of characters already read from `r`.
     *
     * \return If `r` is contiguous, will always return `error::good`.
     * Otherwise, may return `error::unrecoverable_source_error`, if the putback
     * fails.
     */
    template <
        typename WrappedRange,
        typename std::enable_if<WrappedRange::is_contiguous>::type* = nullptr>
    error putback_n(WrappedRange& r, ranges::range_difference_t<WrappedRange> n)
    {
        SCN_EXPECT(n <= ranges::distance(r.begin_underlying(), r.begin()));
        r.advance(-n);
        return {};
    }
    template <
        typename WrappedRange,
        typename std::enable_if<!WrappedRange::is_contiguous>::type* = nullptr>
    error putback_n(WrappedRange& r, ranges::range_difference_t<WrappedRange> n)
    {
        for (ranges::range_difference_t<WrappedRange> i = 0; i < n; ++i) {
            r.advance(-1);
            if (r.begin() == r.end()) {
                return {error::unrecoverable_source_error, "Putback failed"};
            }
        }
        return {};
    }

    /// @}

    // read_code_point

    /**
     * Type returned by `read_code_point`
     * \tparam CharT Character type of the range
     */
    template <typename CharT>
    struct read_code_point_result {
        /// Code units, may point to `writebuf` given to `read_code_point`
        span<const CharT> chars;
        /// Parsed code point
        code_point cp;
    };

    namespace detail {
        // contiguous && direct
        template <typename CharT, typename WrappedRange>
        expected<read_code_point_result<CharT>> read_code_point_impl(
            WrappedRange& r,
            span<CharT> writebuf,
            std::true_type)
        {
            if (r.begin() == r.end()) {
                return error(error::end_of_range, "EOF");
            }

            auto sbuf = r.get_buffer_and_advance(4 / sizeof(CharT));
            if (sbuf.size() == 0) {
                auto ret = read_code_unit(r, true);
                if (!ret) {
                    return ret.error();
                }
                sbuf = writebuf.first(1);
                writebuf[0] = ret.value();
            }
            int len = ::scn::get_sequence_length(sbuf[0]);
            if (SCN_UNLIKELY(len == 0)) {
                return error(error::invalid_encoding, "Invalid code point");
            }
            if (sbuf.ssize() > len) {
                auto e = putback_n(r, sbuf.ssize() - len);
                if (!e) {
                    return e;
                }
                sbuf = sbuf.first(static_cast<size_t>(len));
            }
            if (len == 1) {
                // Single-char code point
                return read_code_point_result<CharT>{sbuf.first(1),
                                                     make_code_point(sbuf[0])};
            }
            while (sbuf.ssize() < len) {
                auto ret = read_code_unit(r, true);
                if (!ret) {
                    auto e = putback_n(r, sbuf.ssize());
                    if (!e) {
                        return e;
                    }
                    if (ret.error().code() == error::end_of_range) {
                        return error(error::invalid_encoding,
                                     "Invalid code point");
                    }
                    return ret.error();
                }
                sbuf = make_span(writebuf.begin(), sbuf.size() + 1);
                writebuf[sbuf.size() - 1] = ret.value();
            }

            code_point cp{};
            auto ret = parse_code_point(sbuf.begin(), sbuf.end(), cp);
            if (!ret) {
                return ret.error();
            }
            return read_code_point_result<CharT>{sbuf, cp};
        }

        template <typename CharT, typename WrappedRange>
        expected<read_code_point_result<CharT>> read_code_point_impl(
            WrappedRange& r,
            span<CharT> writebuf,
            std::false_type)
        {
            auto first = read_code_unit(r, false);
            if (!first) {
                return first.error();
            }

            auto len =
                static_cast<size_t>(::scn::get_sequence_length(first.value()));
            if (SCN_UNLIKELY(len == 0)) {
                return error(error::invalid_encoding, "Invalid code point");
            }
            r.advance();

            writebuf[0] = first.value();
            if (len == 1) {
                // Single-char code point
                return read_code_point_result<CharT>{
                    make_span(writebuf.data(), 1),
                    make_code_point(first.value())};
            }

            size_t index = 1;

            auto parse = [&]() -> expected<read_code_point_result<CharT>> {
                code_point cp{};
                auto ret = parse_code_point(writebuf.data(),
                                            writebuf.data() + len, cp);
                if (!ret) {
                    auto pb = putback_n(r, static_cast<std::ptrdiff_t>(len));
                    if (!pb) {
                        return pb;
                    }
                    return ret.error();
                }
                auto s = make_span(writebuf.data(), len);
                return read_code_point_result<CharT>{s, cp};
            };
            auto advance = [&]() -> error {
                auto ret = read_code_unit(r, false);
                if (!ret) {
                    auto pb = putback_n(r, static_cast<std::ptrdiff_t>(index));
                    if (!pb) {
                        return pb;
                    }
                    return ret.error();
                }
                writebuf[index] = ret.value();
                ++index;
                r.advance();
                return {};
            };

            while (index < 4) {
                auto e = advance();
                if (!e) {
                    return e;
                }
                if (index == len) {
                    return parse();
                }
            }
            SCN_ENSURE(false);
            SCN_UNREACHABLE;
        }
    }  // namespace detail

    /**
     * Read a single Unicode code point from `r` as if by repeatedly calling
     * `read_code_unit()`.
     *
     * Advances the range past the read code point. On error, rolls back the
     * range into the state it was before calling this function, as if by
     * calling `putback_n()`.
     *
     * \param r Range to read from
     * \param writebuf Buffer to use for reading into, if necessary. `BufValueT`
     * can be any trivial type. Must be at least 4 bytes long. May be written
     * over.
     *
     * \return An instance of `read_code_point_result`, wrapped in an
     * `expected`. `chars` contains the code units read from `r`, which may
     * point to `writebuf`. `cp` contains the code point parsed.
     * If `r.begin() == r.end()`, returns EOF.
     * If `read_code_unit()` or `putback_n()` fails, returns any errors returned
     * by it.
     * If the code point was not encoded correctly, returns
     * `error::invalid_encoding`.
     */
    template <typename WrappedRange, typename BufValueT>
    expected<read_code_point_result<typename WrappedRange::char_type>>
    read_code_point(WrappedRange& r, span<BufValueT> writebuf)
    {
        SCN_EXPECT(writebuf.size() * sizeof(BufValueT) >= 4);
        using char_type = typename WrappedRange::char_type;
        SCN_GCC_PUSH
        SCN_GCC_IGNORE("-Wcast-align")  // taken care of by the caller
        return detail::read_code_point_impl<char_type>(
            r,
            make_span(reinterpret_cast<char_type*>(writebuf.data()),
                      writebuf.size() * sizeof(BufValueT) / sizeof(char_type)),
            std::integral_constant<bool,
                                   WrappedRange::provides_buffer_access>{});
        SCN_GCC_POP
    }

    // read_zero_copy

    /// @{

    /**
     * Reads up to `n` characters (= code units) from `r`, as if by repeatedly
     * incrementing `r.begin()`, and returns a `span` pointing into `r`.
     *
     * Let `count` be `min(r.size(), n)`.
     * Reads, and advances `r` by `count` characters.
     * `r.begin()` is in no point dereferenced.
     * If `r.size()` is not defined, the range is not contiguous, and an empty
     * span is returned.
     *
     * \return A `span` pointing to `r`, starting from `r.begin()` and with a
     * size of `count`.
     * If `r.begin() == r.end()`, returns EOF.
     * If the range does not satisfy `contiguous_range`, returns an empty
     * `span`.
     */
    template <typename WrappedRange,
              typename std::enable_if<
                  WrappedRange::provides_buffer_access>::type* = nullptr>
    expected<span<const typename detail::extract_char_type<
        typename WrappedRange::iterator>::type>>
    read_zero_copy(WrappedRange& r, ranges::range_difference_t<WrappedRange> n)
    {
        if (r.begin() == r.end()) {
            return error(error::end_of_range, "EOF");
        }
        return r.get_buffer_and_advance(static_cast<size_t>(n));
    }
    template <typename WrappedRange,
              typename std::enable_if<
                  !WrappedRange::provides_buffer_access>::type* = nullptr>
    expected<span<const typename detail::extract_char_type<
        typename WrappedRange::iterator>::type>>
    read_zero_copy(WrappedRange& r, ranges::range_difference_t<WrappedRange>)
    {
        if (r.begin() == r.end()) {
            return error(error::end_of_range, "EOF");
        }
        return span<const typename detail::extract_char_type<
            typename WrappedRange::iterator>::type>{};
    }
    /// @}

    // read_all_zero_copy

    /// @{
    /**
     * Reads every character from `r`, as if by repeatedly incrementing
     * `r.begin()`, and returns a `span` pointing into `r`.
     *
     * If there's no error, `r` is advanced to the end.
     * `r.begin()` is in no point dereferenced.
     * If `r.size()` is not defined, the range is not contiguous, and an empty
     * span is returned.
     *
     * \return A `span` pointing to `r`, starting at `r.begin()` and ending at
     * `r.end()`.
     * If `r.begin() == r.end()`, returns EOF.
     * If the range does not satisfy `contiguous_range`, returns an empty
     * `span`.
     */
    template <
        typename WrappedRange,
        typename std::enable_if<WrappedRange::is_contiguous>::type* = nullptr>
    expected<span<const typename detail::extract_char_type<
        typename WrappedRange::iterator>::type>>
    read_all_zero_copy(WrappedRange& r)
    {
        if (r.begin() == r.end()) {
            return error(error::end_of_range, "EOF");
        }
        auto s = make_span(r.data(), static_cast<size_t>(r.size()));
        r.advance(r.size());
        return s;
    }
    template <
        typename WrappedRange,
        typename std::enable_if<!WrappedRange::is_contiguous>::type* = nullptr>
    expected<span<const typename detail::extract_char_type<
        typename WrappedRange::iterator>::type>>
    read_all_zero_copy(WrappedRange& r)
    {
        if (r.begin() == r.end()) {
            return error(error::end_of_range, "EOF");
        }
        return span<const typename detail::extract_char_type<
            typename WrappedRange::iterator>::type>{};
    }
    /// @}

    // read_into

    namespace detail {
        template <typename WrappedRange, typename OutputIterator>
        error read_into_impl(WrappedRange& r,
                             OutputIterator& it,
                             ranges::range_difference_t<WrappedRange> n)
        {
            for (; n != 0; --n) {
                auto ret = read_code_unit(r, false);
                if (!ret) {
                    return ret.error();
                }
                *it = ret.value();
                r.advance();
            }
            return {};
        }
    }  // namespace detail

    /// @{

    /**
     * Reads up to `n` characters (= code units) from `r`, as if by repeatedly
     * calling `read_code_unit()`, and writing the characters into `it`.
     *
     * If reading fails at any point, the error is returned.
     * `r` is advanced by as many characters that were successfully read.
     *
     * \param r Range to read
     * \param it Iterator to write into, e.g. `std::back_insert_iterator`. Must
     * satisfy `output_iterator`, and be incrementable by `n` times.
     * \param n Characters to read from `r`
     *
     * \return `error::good` if `n` characters were read.
     * If `r.begin() == r.end()` at any point before `n` characters has been
     * read, returns EOF.
     * Any error returned by `read_code_unit()` if one
     * occurred.
     */
    template <typename WrappedRange,
              typename OutputIterator,
              typename std::enable_if<
                  WrappedRange::provides_buffer_access>::type* = nullptr>
    error read_into(WrappedRange& r,
                    OutputIterator& it,
                    ranges::range_difference_t<WrappedRange> n)
    {
        while (n != 0) {
            if (r.begin() == r.end()) {
                return {error::end_of_range, "EOF"};
            }
            auto s = read_zero_copy(r, n);
            if (!s) {
                return s.error();
            }
            if (s.value().size() == 0) {
                break;
            }
            it = std::copy(s.value().begin(), s.value().end(), it);
            n -= s.value().ssize();
        }
        if (n != 0) {
            return detail::read_into_impl(r, it, n);
        }
        return {};
    }
    template <typename WrappedRange,
              typename OutputIterator,
              typename std::enable_if<
                  !WrappedRange::provides_buffer_access>::type* = nullptr>
    error read_into(WrappedRange& r,
                    OutputIterator& it,
                    ranges::range_difference_t<WrappedRange> n)
    {
        if (r.begin() == r.end()) {
            return {error::end_of_range, "EOF"};
        }
        return detail::read_into_impl(r, it, n);
    }
    /// @}

    namespace detail {
        template <typename WrappedRange, typename Predicate>
        expected<span<const typename WrappedRange::char_type>>
        read_until_pred_contiguous(WrappedRange& r,
                                   Predicate&& pred,
                                   bool pred_result_to_stop,
                                   bool keep_final)
        {
            using span_type = span<const typename WrappedRange::char_type>;

            if (r.begin() == r.end()) {
                return error(error::end_of_range, "EOF");
            }

            if (!pred.is_multibyte()) {
                for (auto it = r.begin(); it != r.end(); ++it) {
                    if (pred(make_span(&*it, 1)) == pred_result_to_stop) {
                        auto begin = r.data();
                        auto end = keep_final ? it + 1 : it;
                        r.advance_to(end);
                        return span_type{
                            begin, to_address_safe(end, r.begin(), r.end())};
                    }
                }
            }
            else {
                for (auto it = r.begin(); it != r.end();) {
                    auto len = ::scn::get_sequence_length(*it);
                    if (len == 0 || ranges::distance(it, r.end()) < len) {
                        return error{error::invalid_encoding,
                                     "Invalid code point"};
                    }
                    auto span =
                        make_span(to_address_safe(it, r.begin(), r.end()),
                                  static_cast<size_t>(len));
                    code_point cp{};
                    auto i = parse_code_point(span.begin(), span.end(), cp);
                    if (!i) {
                        return i.error();
                    }
                    if (i.value() != span.end()) {
                        return error{error::invalid_encoding,
                                     "Invalid code point"};
                    }
                    if (pred(span) == pred_result_to_stop) {
                        auto begin = r.data();
                        auto end = keep_final ? it + len : it;
                        r.advance_to(end);
                        return span_type{
                            begin, to_address_safe(end, r.begin(), r.end())};
                    }
                    it += len;
                }
            }
            auto begin = r.data();
            auto end = r.data() + r.size();
            r.advance_to(r.end());
            return span_type{begin, end};
        }
    }  // namespace detail

    // read_until_space_zero_copy

    namespace detail {
        template <typename WrappedRange, typename Predicate>
        expected<span<const typename WrappedRange::char_type>>
        read_until_space_zero_copy_impl(WrappedRange& r,
                                        Predicate&& is_space,
                                        bool keep_final_space,
                                        std::true_type)
        {
            return detail::read_until_pred_contiguous(r, SCN_FWD(is_space),
                                                      true, keep_final_space);
        }
        template <typename WrappedRange, typename Predicate>
        expected<span<const typename WrappedRange::char_type>>
        read_until_space_zero_copy_impl(WrappedRange& r,
                                        Predicate&&,
                                        bool,
                                        std::false_type)
        {
            if (r.begin() == r.end()) {
                return error(error::end_of_range, "EOF");
            }
            return span<const typename WrappedRange::char_type>{};
        }
    }  // namespace detail

    /**
     * Reads code points from `r`, until a space, as determined by `is_space`,
     * is found, and returns a `span` pointing to `r`.
     *
     * If no error occurs `r` is advanced past the returned span.
     * On error, `r` is not advanced.
     *
     * \param r Range to read from
     *
     * \param is_space Predicate taking a span of code units encompassing a code
     * point, and returning a `bool`, where `true` means that the character is a
     * space. Additionally, it must have a member function
     * `is_space.is_multibyte()`, returning a `bool`, where `true` means that a
     * space character can encompass multiple code units.
     *
     * \param keep_final_space If `true`, the space code point found is included
     * in the returned span, and it is advanced past in `r`. If `false`, it is
     * not included, and `r.begin()` will point to the space.
     *
     * \return Span of code units, pointing to `r`, starting at `r.begin()`, and
     * ending at the space character, the precise location determined by the
     * `keep_final_space` parameter.
     * If `r.begin() == r.end()`, returns EOF.
     * `r` reaching its end before a space character is found is not considered
     * an error.
     * If `r` contains invalid encoding, returns `error::invalid_encoding`.
     * If the range is not contiguous, returns an empty `span`.
     */
    template <typename WrappedRange, typename Predicate>
    expected<span<const typename WrappedRange::char_type>>
    read_until_space_zero_copy(WrappedRange& r,
                               Predicate&& is_space,
                               bool keep_final_space)
    {
        return detail::read_until_space_zero_copy_impl(
            r, SCN_FWD(is_space), keep_final_space,
            std::integral_constant<bool, WrappedRange::is_contiguous>{});
    }

    // read_until_space

    namespace detail {
        template <typename WrappedRange,
                  typename Predicate,
                  typename OutputIt,
                  typename OutputItCmp>
        error read_until_pred_buffer(WrappedRange& r,
                                     Predicate&& pred,
                                     bool pred_result_to_stop,
                                     OutputIt& out,
                                     OutputItCmp out_cmp,
                                     bool keep_final,
                                     bool& done,
                                     std::true_type)
        {
            if (!pred.is_multibyte()) {
                while (r.begin() != r.end() && !done) {
                    auto s = r.get_buffer_and_advance();
                    for (auto it = s.begin(); it != s.end() && out_cmp(out);
                         ++it) {
                        if (pred(make_span(&*it, 1)) == pred_result_to_stop) {
                            if (keep_final) {
                                *out = *it;
                                ++out;
                            }
                            auto e =
                                putback_n(r, ranges::distance(it, s.end()));
                            if (!e) {
                                return e;
                            }
                            done = true;
                            break;
                        }
                        *out = *it;
                        ++out;
                    }
                    if (!done && out_cmp(out)) {
                        auto ret = read_code_unit(r, false);
                        if (!ret) {
                            if (ret.error() == error::end_of_range) {
                                return {};
                            }
                            return ret.error();
                        }
                        if (pred(make_span(&ret.value(), 1)) ==
                            pred_result_to_stop) {
                            if (keep_final) {
                                r.advance();
                                *out = ret.value();
                                ++out;
                            }
                            done = true;
                            break;
                        }
                        r.advance();
                        *out = ret.value();
                        ++out;
                    }
                }
            }
            else {
                while (r.begin() != r.end() && !done) {
                    auto s = r.get_buffer_and_advance();
                    for (auto it = s.begin(); it != s.end() && out_cmp(out);) {
                        auto len = ::scn::get_sequence_length(*it);
                        if (len == 0) {
                            return error{error::invalid_encoding,
                                         "Invalid code point"};
                        }
                        if (ranges::distance(it, s.end()) < len) {
                            auto e = putback_n(r, len);
                            if (!e) {
                                return e;
                            }
                            break;
                        }
                        auto cpspan = make_span(it, static_cast<size_t>(len));
                        code_point cp{};
                        auto i =
                            parse_code_point(cpspan.begin(), cpspan.end(), cp);
                        if (!i) {
                            return i.error();
                        }
                        if (i.value() != cpspan.end()) {
                            return error{error::invalid_encoding,
                                         "Invalid code point"};
                        }
                        if (pred(cpspan) == pred_result_to_stop) {
                            if (keep_final) {
                                out = std::copy(cpspan.begin(), cpspan.end(),
                                                out);
                            }
                            done = true;
                            break;
                        }
                        out = std::copy(cpspan.begin(), cpspan.end(), out);
                    }

                    if (!done && out_cmp(out)) {
                        alignas(typename WrappedRange::char_type) unsigned char
                            buf[4] = {0};
                        auto cpret = read_code_point(r, make_span(buf, 4));
                        if (!cpret) {
                            if (cpret.error() == error::end_of_range) {
                                return {};
                            }
                            return cpret.error();
                        }
                        if (pred(cpret.value().chars) == pred_result_to_stop) {
                            if (keep_final) {
                                out = std::copy(cpret.value().chars.begin(),
                                                cpret.value().chars.end(), out);
                            }
                            else {
                                return putback_n(r,
                                                 cpret.value().chars.ssize());
                            }
                            done = true;
                            break;
                        }
                        out = std::copy(cpret.value().chars.begin(),
                                        cpret.value().chars.end(), out);
                    }
                }
            }
            return {};
        }
        template <typename WrappedRange,
                  typename Predicate,
                  typename OutputIt,
                  typename OutputItCmp>
        error read_until_pred_buffer(WrappedRange&,
                                     Predicate&&,
                                     bool,
                                     OutputIt&,
                                     OutputItCmp,
                                     bool,
                                     bool& done,
                                     std::false_type)
        {
            done = false;
            return {};
        }

        template <typename WrappedRange,
                  typename Predicate,
                  typename OutputIt,
                  typename OutputItCmp>
        error read_until_pred_non_contiguous(WrappedRange& r,
                                             Predicate&& pred,
                                             bool pred_result_to_stop,
                                             OutputIt& out,
                                             OutputItCmp out_cmp,
                                             bool keep_final)
        {
            if (r.begin() == r.end()) {
                return {error::end_of_range, "EOF"};
            }

            {
                bool done = false;
                auto e = read_until_pred_buffer(
                    r, pred, pred_result_to_stop, out, out_cmp, keep_final,
                    done,
                    std::integral_constant<
                        bool, WrappedRange::provides_buffer_access>{});
                if (!e) {
                    return e;
                }
                if (done) {
                    return {};
                }
            }

            if (!pred.is_multibyte()) {
                while (r.begin() != r.end() && out_cmp(out)) {
                    auto cu = read_code_unit(r, false);
                    if (!cu) {
                        return cu.error();
                    }
                    if (pred(make_span(&cu.value(), 1)) ==
                        pred_result_to_stop) {
                        if (keep_final) {
                            r.advance();
                            *out = cu.value();
                            ++out;
                        }
                        return {};
                    }
                    r.advance();
                    *out = cu.value();
                    ++out;
                }
            }
            else {
                unsigned char buf[4] = {0};
                while (r.begin() != r.end() && out_cmp(out)) {
                    auto cp = read_code_point(r, make_span(buf, 4));
                    if (!cp) {
                        return cp.error();
                    }
                    if (pred(cp.value().chars) == pred_result_to_stop) {
                        if (keep_final) {
                            out = std::copy(cp.value().chars.begin(),
                                            cp.value().chars.end(), out);
                            return {};
                        }
                        else {
                            return putback_n(r, cp.value().chars.ssize());
                        }
                    }
                    out = std::copy(cp.value().chars.begin(),
                                    cp.value().chars.end(), out);
                }
            }
            return {};
        }
    }  // namespace detail

    /// @{

    /**
     * Reads code points from `r`, until a space, as determined by `is_space`,
     * is found, and writes them into `out`, a single code unit at a time.
     *
     * If no error occurs, `r` is advanced past the last character written into
     * `out`.
     *
     * On error, `r` is advanced an indeterminate amount, as if by calling
     * `r.advance(n)`, where `n` is a non-negative integer.
     * It is, however, not advanced past any space characters.
     *
     * \param r Range to read from
     *
     * \param out Iterator to write read characters into. Must satisfy
     * `output_iterator`.
     *
     * \param is_space Predicate taking a span of code units encompassing a code
     * point, and returning a `bool`, where `true` means that the character is a
     * space. Additionally, it must have a member function
     * `is_space.is_multibyte()`, returning a `bool`, where `true` means that a
     * space character can encompass multiple code units.
     *
     * \param keep_final_space If `true`, the space code point found is written
     * into `out`, and it is advanced past in `r`. If `false`, it is not
     * included, and `r.begin()` will point to the space.
     *
     * \return `error::good` on success.
     * If `r.begin() == r.end()`, returns EOF.
     * `r` reaching its end before a space character is found is not considered
     * an error.
     * If `r` contains invalid encoding, returns `error::invalid_encoding`.
     */
    template <
        typename WrappedRange,
        typename OutputIterator,
        typename Predicate,
        typename std::enable_if<WrappedRange::is_contiguous>::type* = nullptr>
    error read_until_space(WrappedRange& r,
                           OutputIterator& out,
                           Predicate&& is_space,
                           bool keep_final_space)
    {
        auto s =
            read_until_space_zero_copy(r, SCN_FWD(is_space), keep_final_space);
        if (!s) {
            return s.error();
        }
        out = std::copy(s.value().begin(), s.value().end(), out);
        return {};
    }
    template <
        typename WrappedRange,
        typename OutputIterator,
        typename Predicate,
        typename std::enable_if<!WrappedRange::is_contiguous>::type* = nullptr>
    error read_until_space(WrappedRange& r,
                           OutputIterator& out,
                           Predicate&& is_space,
                           bool keep_final_space)
    {
        return detail::read_until_pred_non_contiguous(
            r, SCN_FWD(is_space), true, out,
            [](const OutputIterator&) { return true; }, keep_final_space);
    }

    /// @}

    // read_until_space_ranged

    /// @{

    /**
     * Otherwise equivalent to `read_until_space`, except will also stop reading
     * if `out == end`.
     *
     * \see read_until_space
     */
    template <typename WrappedRange,
              typename OutputIterator,
              typename Sentinel,
              typename Predicate>
    error read_until_space_ranged(WrappedRange& r,
                                  OutputIterator& out,
                                  Sentinel end,
                                  Predicate&& is_space,
                                  bool keep_final_space)
    {
        return detail::read_until_pred_non_contiguous(
            r, SCN_FWD(is_space), true, out,
            [&end](const OutputIterator& it) { return it != end; },
            keep_final_space);
    }

    /// @}

    namespace detail {
        /**
         * Predicate to pass to read_until_space etc.
         */
        template <typename CharT>
        struct is_space_predicate {
            using char_type = CharT;
            using locale_type = basic_locale_ref<char_type>;

            /**
             * \param l Locale to use, fetched from `ctx.locale()`
             * \param localized If `true`, use `l.get_custom()`, otherwise use
             * `l.get_static()`.
             * \param width If `width != 0`, limit the number of code
             * units to be read
             */
            SCN_CONSTEXPR14 is_space_predicate(const locale_type& l,
                                               bool localized,
                                               size_t width)
                : m_locale{nullptr},
                  m_width{width},
                  m_fn{get_fn(localized, width != 0)}
            {
                if (localized) {
                    l.prepare_localized();
                    m_locale = l.get_localized_unsafe();
                }
            }

            /**
             * Returns `true` if `ch` is a code point according to the supplied
             * locale, using either the static or custom locale, depending on
             * the `localized` parameter given to the constructor.
             *
             * Returns also `true` if the maximum width, as determined by the
             * `width` parameter given to the constructor, was reached.
             */
            bool operator()(span<const char_type> ch)
            {
                SCN_EXPECT(m_fn);
                SCN_EXPECT(ch.size() >= 1);
                return m_fn(m_locale, ch, m_i, m_width);
            }

            /**
             * Returns `true`, if `*this` uses the custom locale for classifying
             * space characters
             */
            constexpr bool is_localized() const
            {
                return m_locale != nullptr;
            }
            /**
             * Returns `true` if a space character can encompass multiple code
             * units
             */
            constexpr bool is_multibyte() const
            {
                return is_localized() && is_multichar_type(CharT{});
            }

        private:
            using static_locale_type = typename locale_type::static_type;
            using custom_locale_type = typename locale_type::custom_type;
            const custom_locale_type* m_locale;
            size_t m_width{0}, m_i{0};

            constexpr static bool call(const custom_locale_type*,
                                       span<const char_type> ch,
                                       size_t&,
                                       size_t)
            {
                return static_locale_type::is_space(ch);
            }
            static bool localized_call(const custom_locale_type* locale,
                                       span<const char_type> ch,
                                       size_t&,
                                       size_t)
            {
                SCN_EXPECT(locale != nullptr);
                return locale->is_space(ch);
            }
            SCN_CONSTEXPR14 static bool call_counting(const custom_locale_type*,
                                                      span<const char_type> ch,
                                                      size_t& i,
                                                      size_t max)
            {
                SCN_EXPECT(i <= max);
                if (i == max || i + ch.size() > max) {
                    return true;
                }
                i += ch.size();
                return static_locale_type::is_space(ch);
            }
            static bool localized_call_counting(
                const custom_locale_type* locale,
                span<const char_type> ch,
                size_t& i,
                size_t max)
            {
                SCN_EXPECT(locale != nullptr);
                SCN_EXPECT(i <= max);
                if (i == max || i + ch.size() > max) {
                    return true;
                }
                i += ch.size();
                return locale->is_space(ch);
            }

            using fn_type = bool (*)(const custom_locale_type*,
                                     span<const char_type>,
                                     size_t&,
                                     size_t);
            fn_type m_fn{nullptr};

            static SCN_CONSTEXPR14 fn_type get_fn(bool localized, bool counting)
            {
                if (localized) {
                    return counting ? localized_call_counting : localized_call;
                }
                return counting ? call_counting : call;
            }
        };

        template <typename CharT>
        is_space_predicate<CharT> make_is_space_predicate(
            const basic_locale_ref<CharT>& locale,
            bool localized,
            size_t width = 0)
        {
            return {locale, localized, width};
        }

        template <typename CharT>
        struct basic_skipws_iterator {
            using value_type = void;
            using reference = void;
            using pointer = void;
            using size_type = size_t;
            using difference_type = std::ptrdiff_t;
            using iterator_category = std::output_iterator_tag;

            constexpr basic_skipws_iterator() = default;

            basic_skipws_iterator& operator=(CharT)
            {
                return *this;
            }
            basic_skipws_iterator& operator*()
            {
                return *this;
            }
            basic_skipws_iterator& operator++()
            {
                return *this;
            }
        };
    }  // namespace detail

    // skip_range_whitespace

    /// @{

    /**
     * Reads code points from `ctx.range()`, as if by repeatedly calling
     * `read_code_point()`, until a non-space character is found, or EOF is
     * reached. That non-space character is then put back into the range.
     *
     * Whether a character is a space, is determined by `ctx.locale()` and the
     * `localized` parameter.
     *
     * \param ctx Context to get the range and locale from.
     *
     * \param localized If `true`, `ctx.locale().get_custom()` is used.
     * Otherwise, `ctx.locale().get_static()` is used.
     * In practice, means whether locale-specific whitespace characters are
     * accepted, or just those given by `std::isspace` with the `"C"` locale.
     *
     * \return `error::good` on success.
     * If `ctx.range().begin() == ctx.range().end()`, returns EOF.
     * If `ctx.range()` contains invalid encoding, returns
     * `error::invalid_encoding`.
     */
    template <typename Context,
              typename std::enable_if<
                  !Context::range_type::is_contiguous>::type* = nullptr>
    error skip_range_whitespace(Context& ctx, bool localized) noexcept
    {
        auto is_space_pred =
            detail::make_is_space_predicate(ctx.locale(), localized);
        auto it = detail::basic_skipws_iterator<typename Context::char_type>{};
        return detail::read_until_pred_non_contiguous(
            ctx.range(), is_space_pred, false, it,
            [](decltype(it)) { return true; }, false);
    }
    template <typename Context,
              typename std::enable_if<
                  Context::range_type::is_contiguous>::type* = nullptr>
    error skip_range_whitespace(Context& ctx, bool localized) noexcept
    {
        auto is_space_pred =
            detail::make_is_space_predicate(ctx.locale(), localized);
        return detail::read_until_pred_contiguous(ctx.range(), is_space_pred,
                                                  false, false)
            .error();
    }

    /// @}

    namespace detail {
        template <typename T>
        struct simple_integer_scanner {
            template <typename CharT>
            static expected<typename span<const CharT>::iterator> scan(
                span<const CharT> buf,
                T& val,
                int base = 10,
                uint16_t flags = 0);

            template <typename CharT>
            static expected<typename span<const CharT>::iterator> scan_lower(
                span<const CharT> buf,
                T& val,
                int base = 10,
                uint16_t flags = 0);
        };
    }  // namespace detail

    /**
     * A very simple parser base class, which only accepts empty format string
     * specifiers, e.g. `{}`, `{:}` or `{1:}`.
     */
    struct empty_parser : parser_base {
        template <typename ParseCtx>
        error parse(ParseCtx& pctx)
        {
            pctx.arg_begin();
            if (SCN_UNLIKELY(!pctx)) {
                return {error::invalid_format_string,
                        "Unexpected format string end"};
            }
            if (!pctx.check_arg_end()) {
                return {error::invalid_format_string, "Expected argument end"};
            }
            pctx.arg_end();
            return {};
        }
    };

    /**
     * Provides a framework for building a format string parser.
     * Does not provide a `parse()` member function, so not a parser on to its
     * own.
     */
    struct common_parser : parser_base {
        static constexpr bool support_align_and_fill()
        {
            return true;
        }

    protected:
        /**
         * Parse the beginning of the argument.
         * Returns `error::invalid_format_string` if `!pctx` (the format string
         * ended)
         */
        template <typename ParseCtx>
        error parse_common_begin(ParseCtx& pctx)
        {
            pctx.arg_begin();
            if (SCN_UNLIKELY(!pctx)) {
                return {error::invalid_format_string,
                        "Unexpected format string end"};
            }
            return {};
        }

        /**
         * Returns `error::invalid_format_string` if the format string or the
         * argument has ended.
         */
        template <typename ParseCtx>
        error check_end(ParseCtx& pctx)
        {
            if (!pctx || pctx.check_arg_end()) {
                return {error::invalid_format_string,
                        "Unexpected end of format string argument"};
            }
            return {};
        }

        /**
         * Parse alignment, fill, width, and localization flags, and populate
         * appropriate member variables.
         *
         * Returns `error::invalid_format_string` if an error occurred.
         */
        template <typename ParseCtx>
        error parse_common_flags(ParseCtx& pctx)
        {
            SCN_EXPECT(check_end(pctx));
            using char_type = typename ParseCtx::char_type;

            auto ch = pctx.next_char();
            auto next_char = [&]() -> error {
                pctx.advance_char();
                auto e = check_end(pctx);
                if (!e) {
                    return e;
                }
                ch = pctx.next_char();
                return {};
            };
            auto parse_number = [&](size_t& n) -> error {
                SCN_EXPECT(pctx.locale().get_static().is_digit(ch));

                auto it = pctx.begin();
                for (; it != pctx.end(); ++it) {
                    if (!pctx.locale().get_static().is_digit(*it)) {
                        break;
                    }
                }
                auto buf = make_span(pctx.begin(), it);

                auto s = detail::simple_integer_scanner<size_t>{};
                auto res = s.scan(buf, n, 10);
                if (!res) {
                    return res.error();
                }

                for (it = pctx.begin(); it != res.value();
                     pctx.advance_char(), it = pctx.begin()) {}
                return {};
            };

            auto get_align_char = [&](char_type c) -> common_options_type {
                if (c == detail::ascii_widen<char_type>('<')) {
                    return aligned_left;
                }
                if (c == detail::ascii_widen<char_type>('>')) {
                    return aligned_right;
                }
                if (c == detail::ascii_widen<char_type>('^')) {
                    return aligned_center;
                }
                return common_options_none;
            };
            auto parse_align = [&](common_options_type align, char_type fill) {
                if (align != common_options_none) {
                    common_options |= align;
                }
                fill_char = static_cast<char32_t>(fill);
            };

            // align and fill
            common_options_type align{};
            bool align_set = false;
            if (pctx.chars_left() > 1 &&
                ch != detail::ascii_widen<char_type>('[')) {
                const auto peek = pctx.peek_char();
                align = get_align_char(peek);
                if (align != common_options_none) {
                    // Arg is like "{:_x}", where _ is some fill character, and
                    // x is an alignment flag
                    // -> we have both alignment and fill
                    parse_align(align, ch);

                    auto e = next_char();
                    SCN_ENSURE(e);
                    if (!next_char()) {
                        return {};
                    }
                    align_set = true;
                }
            }
            if (!align_set) {
                align = get_align_char(ch);
                if (align != common_options_none) {
                    // Arg is like "{:x}", where x is an alignment flag
                    // -> we have alignment with default fill (space ' ')
                    parse_align(align, detail::ascii_widen<char_type>(' '));
                    if (!next_char()) {
                        return {};
                    }
                }
            }

            // digit -> width
            if (pctx.locale().get_static().is_digit(ch)) {
                common_options |= width_set;

                size_t w{};
                auto e = parse_number(w);
                if (!e) {
                    return e;
                }
                field_width = w;
                return {};
            }
            // L -> localized
            if (ch == detail::ascii_widen<char_type>('L')) {
                common_options |= localized;

                if (!next_char()) {
                    return {};
                }
            }

            return {};
        }

        /**
         * Parse argument end.
         *
         * Returns `error::invalid_format_string` if argument end was not found.
         */
        template <typename ParseCtx>
        error parse_common_end(ParseCtx& pctx)
        {
            if (!pctx || !pctx.check_arg_end()) {
                return {error::invalid_format_string, "Expected argument end"};
            }

            pctx.arg_end();
            return {};
        }

        /**
         * A null callback to pass to `parse_common`, doing nothing and
         * returning `error::good`.
         */
        template <typename ParseCtx>
        static error null_type_cb(ParseCtx&, bool&)
        {
            return {};
        }

    public:
        /**
         * Parse a format string argument, using `parse_common_begin`,
         * `parse_common_flags`, `parse_common_end`, and the supplied type
         * flags.
         *
         * `type_options.size() == type_flags.size()` must be `true`.
         * `pctx` must be valid, and must start at the format string argument
         * specifiers, e.g. in the case of `"{1:foo}"` -> `pctx == "foo}"`
         *
         * \param pctx Format string to parse
         * \param type_options A span of characters, where each character
         * corresponds to a valid type flag. For example, for characters, this
         * span would be \c ['c']
         * \param type_flags A span of bools, where the values will be set to
         * `true`, if a corresponding type flag from `type_options` was found.
         * Should be initialized to all-`false`, as a `false` value will not be
         * written.
         * \param type_cb A callback to call, if none of the `type_options`
         * matched. Must have the signature `(ParseCtx& pctx, bool& parsed) ->
         * error`., where `parsed` is set to `true`, if the flag at
         * `pctx.next_char()` was parsed and advanced past.
         */
        template <typename ParseCtx,
                  typename F,
                  typename CharT = typename ParseCtx::char_type>
        error parse_common(ParseCtx& pctx,
                           span<const CharT> type_options,
                           span<bool> type_flags,
                           F&& type_cb)
        {
            SCN_EXPECT(type_options.size() == type_flags.size());

            auto e = parse_common_begin(pctx);
            if (!e) {
                return e;
            }

            if (!pctx) {
                return {error::invalid_format_string,
                        "Unexpected end of format string"};
            }
            if (pctx.check_arg_end()) {
                return {};
            }

            e = parse_common_flags(pctx);
            if (!e) {
                return e;
            }

            if (!pctx) {
                return {error::invalid_format_string,
                        "Unexpected end of format string"};
            }
            if (pctx.check_arg_end()) {
                return {};
            }

            for (auto ch = pctx.next_char(); pctx && !pctx.check_arg_end();
                 ch = pctx.next_char()) {
                bool parsed = false;
                for (std::size_t i = 0; i < type_options.size() && !parsed;
                     ++i) {
                    if (ch == type_options[i]) {
                        if (SCN_UNLIKELY(type_flags[i])) {
                            return {error::invalid_format_string,
                                    "Repeat flag in format string"};
                        }
                        type_flags[i] = true;
                        parsed = true;
                    }
                }
                if (parsed) {
                    pctx.advance_char();
                    if (!pctx || pctx.check_arg_end()) {
                        break;
                    }
                    continue;
                }

                e = type_cb(pctx, parsed);
                if (!e) {
                    return e;
                }
                if (parsed) {
                    if (!pctx || pctx.check_arg_end()) {
                        break;
                    }
                    continue;
                }
                ch = pctx.next_char();

                if (!parsed) {
                    return {error::invalid_format_string,
                            "Invalid character in format string"};
                }
                if (!pctx || pctx.check_arg_end()) {
                    break;
                }
            }

            return parse_common_end(pctx);
        }

        void make_localized()
        {
            common_options |= localized;
        }

        /**
         * Invoke `parse_common()` with default options (no type flags)
         */
        template <typename ParseCtx>
        error parse_default(ParseCtx& pctx)
        {
            return parse_common(pctx, {}, {}, null_type_cb<ParseCtx>);
        }

        constexpr bool is_aligned_left() const noexcept
        {
            return (common_options & aligned_left) != 0 ||
                   (common_options & aligned_center) != 0;
        }
        constexpr bool is_aligned_right() const noexcept
        {
            return (common_options & aligned_right) != 0 ||
                   (common_options & aligned_center) != 0;
        }
        template <typename CharT>
        constexpr CharT get_fill_char() const noexcept
        {
            return static_cast<CharT>(fill_char);
        }

        size_t field_width{0};
        char32_t fill_char{0};
        enum common_options_type : uint8_t {
            common_options_none = 0,
            localized = 1,       // 'L',
            aligned_left = 2,    // '<'
            aligned_right = 4,   // '>'
            aligned_center = 8,  // '^'
            width_set = 16,      // width
            common_options_all = 31,
        };
        uint8_t common_options{0};
    };

    /**
     * Derives from `common_parser`, and implements `parse()` with
     * `parse_default()`
     */
    struct common_parser_default : common_parser {
        template <typename ParseCtx>
        error parse(ParseCtx& pctx)
        {
            return parse_default(pctx);
        }
    };

    namespace detail {
        template <typename Context,
                  typename std::enable_if<
                      !Context::range_type::is_contiguous>::type* = nullptr>
        error scan_alignment(Context& ctx,
                             typename Context::char_type fill) noexcept
        {
            while (true) {
                SCN_CLANG_PUSH_IGNORE_UNDEFINED_TEMPLATE

                auto ch = read_code_unit(ctx.range());
                if (SCN_UNLIKELY(!ch)) {
                    return ch.error();
                }
                if (ch.value() != fill) {
                    auto pb = putback_n(ctx.range(), 1);
                    if (SCN_UNLIKELY(!pb)) {
                        return pb;
                    }
                    break;
                }

                SCN_CLANG_POP_IGNORE_UNDEFINED_TEMPLATE
            }
            return {};
        }
        template <typename Context,
                  typename std::enable_if<
                      Context::range_type::is_contiguous>::type* = nullptr>
        error scan_alignment(Context& ctx,
                             typename Context::char_type fill) noexcept
        {
            SCN_CLANG_PUSH_IGNORE_UNDEFINED_TEMPLATE
            const auto end = ctx.range().end();
            for (auto it = ctx.range().begin(); it != end; ++it) {
                if (*it != fill) {
                    ctx.range().advance_to(it);
                    return {};
                }
            }
            ctx.range().advance_to(end);
            return {};

            SCN_CLANG_POP_IGNORE_UNDEFINED_TEMPLATE
        }

        template <typename Scanner, typename = void>
        struct scanner_supports_alignment : std::false_type {
        };
        template <typename Scanner>
        struct scanner_supports_alignment<
            Scanner,
            typename std::enable_if<Scanner::support_align_and_fill()>::type>
            : std::true_type {
        };

        template <typename Context, typename Scanner>
        error skip_alignment(Context& ctx,
                             Scanner& scanner,
                             bool left,
                             std::true_type)
        {
            if (left && !scanner.is_aligned_left()) {
                return {};
            }
            if (!left && !scanner.is_aligned_right()) {
                return {};
            }
            return scan_alignment(
                ctx,
                scanner.template get_fill_char<typename Context::char_type>());
        }
        template <typename Context, typename Scanner>
        error skip_alignment(Context&, Scanner&, bool, std::false_type)
        {
            return {};
        }

        /**
         * Scan argument in `val`, from `ctx`, using `Scanner` and `pctx`.
         *
         * Parses `pctx` for `Scanner`, skips whitespace and alignment if
         * necessary, and scans the argument into `val`.
         */
        template <typename Scanner,
                  typename T,
                  typename Context,
                  typename ParseCtx>
        error visitor_boilerplate(T& val, Context& ctx, ParseCtx& pctx)
        {
            Scanner scanner;

            auto err = pctx.parse(scanner);
            if (!err) {
                return err;
            }

            if (scanner.skip_preceding_whitespace()) {
                err = skip_range_whitespace(ctx, false);
                if (!err) {
                    return err;
                }
            }

            err = skip_alignment(ctx, scanner, false,
                                 scanner_supports_alignment<Scanner>{});
            if (!err) {
                return err;
            }

            err = scanner.scan(val, ctx);
            if (!err) {
                return err;
            }

            return skip_alignment(ctx, scanner, true,
                                  scanner_supports_alignment<Scanner>{});
        }
    }  // namespace detail

    SCN_END_NAMESPACE
}  // namespace scn

#endif
