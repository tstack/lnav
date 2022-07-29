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

#ifndef SCN_SCAN_SCAN_H
#define SCN_SCAN_SCAN_H

#include "../util/optional.h"
#include "common.h"
#include "vscan.h"

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace dummy {
    }

    /**
     * \tparam OriginalRange The type of the range passed to the scanning
     * function \param result Return value of `vscan` \return Result object
     *
     * \code{.cpp}
     * template <typename Range, typename... Args>
     * auto scan(Range&& r, string_view f, Args&... a) {
     *     auto range = scn::wrap(std::forward<Range>(r));
     *     auto args = scn::make_args_for(range, f, a...);
     *     auto ret = scn::vscan(std::move(range), f, {args});
     *     return scn::make_scan_result<Range>(std::move(ret));
     * }
     * \endcode
     */
    template <typename OriginalRange,
              typename Error = wrapped_error,
              typename WrappedRange>
    auto make_scan_result(vscan_result<WrappedRange> result)
        -> detail::scan_result_for_range<OriginalRange>
    {
        return detail::wrap_result(Error{result.err},
                                   detail::range_tag<OriginalRange>{},
                                   SCN_MOVE(result.range));
    }

    namespace detail {
        template <typename Range, typename Format, typename... Args>
        auto scan_boilerplate(Range&& r, const Format& f, Args&... a)
            -> detail::scan_result_for_range<Range>
        {
            static_assert(sizeof...(Args) > 0,
                          "Have to scan at least a single argument");
            static_assert(SCN_CHECK_CONCEPT(ranges::range<Range>),
                          "Input needs to be a Range");

            auto range = wrap(SCN_FWD(r));
            auto format = detail::to_format(f);
            auto args = make_args_for(range, format, a...);
            auto ret = vscan(SCN_MOVE(range), format, {args});
            return make_scan_result<Range>(SCN_MOVE(ret));
        }

        template <typename Range, typename... Args>
        auto scan_boilerplate_default(Range&& r, Args&... a)
            -> detail::scan_result_for_range<Range>
        {
            static_assert(sizeof...(Args) > 0,
                          "Have to scan at least a single argument");
            static_assert(SCN_CHECK_CONCEPT(ranges::range<Range>),
                          "Input needs to be a Range");

            auto range = wrap(SCN_FWD(r));
            auto format = static_cast<int>(sizeof...(Args));
            auto args = make_args_for(range, format, a...);
            auto ret = vscan_default(SCN_MOVE(range), format, {args});
            return make_scan_result<Range>(SCN_MOVE(ret));
        }

        template <typename Locale,
                  typename Range,
                  typename Format,
                  typename... Args>
        auto scan_boilerplate_localized(const Locale& loc,
                                        Range&& r,
                                        const Format& f,
                                        Args&... a)
            -> detail::scan_result_for_range<Range>
        {
            static_assert(sizeof...(Args) > 0,
                          "Have to scan at least a single argument");
            static_assert(SCN_CHECK_CONCEPT(ranges::range<Range>),
                          "Input needs to be a Range");

            auto range = wrap(SCN_FWD(r));
            auto format = detail::to_format(f);
            SCN_CLANG_PUSH_IGNORE_UNDEFINED_TEMPLATE
            auto locale =
                make_locale_ref<typename decltype(range)::char_type>(loc);
            SCN_CLANG_POP_IGNORE_UNDEFINED_TEMPLATE

            auto args = make_args_for(range, format, a...);
            auto ret = vscan_localized(SCN_MOVE(range), SCN_MOVE(locale),
                                       format, {args});
            return make_scan_result<Range>(SCN_MOVE(ret));
        }

    }  // namespace detail

    // scan

    // For some reason, Doxygen dislikes SCN_NODISCARD

    /**
     * The most fundamental part of the scanning API.
     * Reads from the range in \c r according to the format string \c f.
     *
     * \code{.cpp}
     * int i;
     * scn::scan("123", "{}", i);
     * // i == 123
     * \endcode
     */
#if SCN_DOXYGEN
    template <typename Range, typename Format, typename... Args>
    auto scan(Range&& r, const Format& f, Args&... a)
        -> detail::scan_result_for_range<Range>;
#else
    template <typename Range, typename Format, typename... Args>
    SCN_NODISCARD auto scan(Range&& r, const Format& f, Args&... a)
        -> detail::scan_result_for_range<Range>
    {
        return detail::scan_boilerplate(SCN_FWD(r), f, a...);
    }
#endif

    // default format

    /**
     * Equivalent to \ref scan, but with a
     * format string with the appropriate amount of space-separated `"{}"`s for
     * the number of arguments. Because this function doesn't have to parse the
     * format string, performance is improved.
     *
     * Adapted from the example for \ref scan
     * \code{.cpp}
     * int i;
     * scn::scan_default("123", i);
     * // i == 123
     * \endcode
     *
     * \see scan
     */
#if SCN_DOXYGEN
    template <typename Range, typename... Args>
    auto scan_default(Range&& r, Args&... a)
        -> detail::scan_result_for_range<Range>;
#else
    template <typename Range, typename... Args>
    SCN_NODISCARD auto scan_default(Range&& r, Args&... a)
        -> detail::scan_result_for_range<Range>
    {
        return detail::scan_boilerplate_default(std::forward<Range>(r), a...);
    }
#endif

    // scan localized

    /**
     * Read from the range in \c r using the locale in \c loc.
     * \c loc must be a \c std::locale. The parameter is a template to avoid
     * inclusion of `<locale>`.
     *
     * Use of this function is discouraged, due to the overhead involved
     * with locales. Note, that the other functions are completely
     * locale-agnostic, and aren't affected by changes to the global C
     * locale.
     *
     * \code{.cpp}
     * double d;
     * scn::scan_localized(std::locale{"fi_FI"}, "3,14", "{}", d);
     * // d == 3.14
     * \endcode
     *
     * \see scan
     */
#if SCN_DOXYGEN
    template <typename Locale,
              typename Range,
              typename Format,
              typename... Args>
    auto scan_localized(const Locale& loc,
                        Range&& r,
                        const Format& f,
                        Args&... a) -> detail::scan_result_for_range<Range>;
#else
    template <typename Locale,
              typename Range,
              typename Format,
              typename... Args>
    SCN_NODISCARD auto scan_localized(const Locale& loc,
                                      Range&& r,
                                      const Format& f,
                                      Args&... a)
        -> detail::scan_result_for_range<Range>
    {
        return detail::scan_boilerplate_localized(loc, std::forward<Range>(r),
                                                  f, a...);
    }
#endif

    // value

    /**
     * Scans a single value with the default options, returning it instead of
     * using an output parameter.
     *
     * The parsed value is in `ret.value()`, if `ret == true`.
     * The return type of this function is otherwise similar to other scanning
     * functions.
     *
     * \code{.cpp}
     * auto ret = scn::scan_value<int>("42");
     * if (ret) {
     *   // ret.value() == 42
     * }
     * \endcode
     */
#if SCN_DOXYGEN
    template <typename T, typename Range>
    auto scan_value(Range&& r)
        -> detail::generic_scan_result_for_range<expected<T>, Range>;
#else
    template <typename T, typename Range>
    SCN_NODISCARD auto scan_value(Range&& r)
        -> detail::generic_scan_result_for_range<expected<T>, Range>
    {
        T value;
        auto range = wrap(SCN_FWD(r));
        auto args = make_args_for(range, 1, value);
        auto ret = vscan_default(SCN_MOVE(range), 1, {args});
        if (ret.err) {
            return detail::wrap_result(expected<T>{value},
                                       detail::range_tag<Range>{},
                                       SCN_MOVE(ret.range));
        }
        return detail::wrap_result(expected<T>{ret.err},
                                   detail::range_tag<Range>{},
                                   SCN_MOVE(ret.range));
    }
#endif

    // input

    /**
     * Otherwise equivalent to \ref scan, expect reads from `stdin`.
     * Character type is determined by the format string.
     * Syncs with `<cstdio>`.
     */
    template <typename Format,
              typename... Args,
              typename CharT = ranges::range_value_t<Format>>
#if SCN_DOXYGEN
    auto input(const Format& f, Args&... a)
        -> detail::scan_result_for_range<basic_file<CharT>&>;
#else
    SCN_NODISCARD auto input(const Format& f, Args&... a)
        -> detail::scan_result_for_range<basic_file<CharT>&>
    {
        auto& range = stdin_range<CharT>();
        auto ret = detail::scan_boilerplate(range, f, a...);
        range.sync();
        ret.range().reset_begin_iterator();
        return ret;
    }
#endif

    // prompt

    namespace detail {
        inline void put_stdout(const char* str)
        {
            std::fputs(str, stdout);
        }
        inline void put_stdout(const wchar_t* str)
        {
            std::fputws(str, stdout);
        }
    }  // namespace detail

    /**
     * Equivalent to \ref input, except writes what's in `p` to `stdout`.
     *
     * \code{.cpp}
     * int i{};
     * scn::prompt("What's your favorite number? ", "{}", i);
     * // Equivalent to:
     * //   std::fputs("What's your favorite number? ", stdout);
     * //   scn::input("{}", i);
     * \endcode
     */
#if SCN_DOXYGEN
    template <typename CharT, typename Format, typename... Args>
    auto prompt(const CharT* p, const Format& f, Args&... a)
        -> detail::scan_result_for_range<basic_file<CharT>&>;
#else
    template <typename CharT, typename Format, typename... Args>
    SCN_NODISCARD auto prompt(const CharT* p, const Format& f, Args&... a)
        -> detail::scan_result_for_range<basic_file<CharT>&>
    {
        SCN_EXPECT(p != nullptr);
        detail::put_stdout(p);

        return input(f, a...);
    }
#endif

    // parse_integer

    /**
     * Parses an integer into \c val in base \c base from \c str.
     * Returns a pointer past the last character read, or an error.
     *
     * @param str source, can't be empty, cannot have:
     *   - preceding whitespace
     *   - preceding \c "0x" or \c "0" (base is determined by the \c base
     * parameter)
     *   - \c '+' sign (\c '-' is fine)
     * @param val parsed integer, must be value-constructed
     * @param base between [2,36]
     */
#if SCN_DOXYGEN
    template <typename T, typename CharT>
    expected<const CharT*> parse_integer(basic_string_view<CharT> str,
                                         T& val,
                                         int base = 10);
#else
    template <typename T, typename CharT>
    SCN_NODISCARD expected<const CharT*>
    parse_integer(basic_string_view<CharT> str, T& val, int base = 10)
    {
        SCN_EXPECT(!str.empty());
        auto s = detail::simple_integer_scanner<T>{};
        SCN_CLANG_PUSH_IGNORE_UNDEFINED_TEMPLATE
        auto ret =
            s.scan_lower(span<const CharT>(str.data(), str.size()), val, base);
        SCN_CLANG_POP_IGNORE_UNDEFINED_TEMPLATE
        if (!ret) {
            return ret.error();
        }
        return {ret.value()};
    }
#endif

    /**
     * Parses float into \c val from \c str.
     * Returns a pointer past the last character read, or an error.
     *
     * @param str source, can't be empty
     * @param val parsed float, must be value-constructed
     */
#if SCN_DOXYGEN
    template <typename T, typename CharT>
    expected<const CharT*> parse_float(basic_string_view<CharT> str, T& val);
#else
    template <typename T, typename CharT>
    SCN_NODISCARD expected<const CharT*> parse_float(
        basic_string_view<CharT> str,
        T& val)
    {
        SCN_EXPECT(!str.empty());
        auto s = detail::float_scanner_access<T>{};
        auto ret = s._read_float(val, make_span(str.data(), str.size()),
                                 detail::ascii_widen<CharT>('.'));
        if (!ret) {
            return ret.error();
        }
        return {str.data() + ret.value()};
    }
#endif

    /**
     * A convenience function for creating scanners for user-provided types.
     *
     * Wraps \ref vscan_usertype
     *
     * Example use:
     *
     * \code{.cpp}
     * // Type has two integers, and its textual representation is
     * // "[val1, val2]"
     * struct user_type {
     *     int val1;
     *     int val2;
     * };
     *
     * template <>
     * struct scn::scanner<user_type> : public scn::empty_parser {
     *     template <typename Context>
     *     error scan(user_type& val, Context& ctx)
     *     {
     *         return scan_usertype(ctx, "[{}, {}]", val.val1, val.val2);
     *     }
     * };
     * \endcode
     *
     * \param ctx Context given to the scanning function
     * \param f Format string to parse
     * \param a Member types (etc) to parse
     */
#if SCN_DOXYGEN
    template <typename WrappedRange, typename Format, typename... Args>
    error scan_usertype(basic_context<WrappedRange>& ctx,
                        const Format& f,
                        Args&... a);
#else
    template <typename WrappedRange, typename Format, typename... Args>
    SCN_NODISCARD error scan_usertype(basic_context<WrappedRange>& ctx,
                                      const Format& f,
                                      Args&... a)
    {
        static_assert(sizeof...(Args) > 0,
                      "Have to scan at least a single argument");

        using char_type = typename WrappedRange::char_type;
        auto args = make_args<basic_context<WrappedRange>,
                              basic_parse_context<char_type>>(a...);
        return vscan_usertype(ctx, basic_string_view<char_type>(f), {args});
    }
#endif

    SCN_END_NAMESPACE
}  // namespace scn

#endif  // SCN_DETAIL_SCAN_H
