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

#ifndef SCN_SCAN_LIST_H
#define SCN_SCAN_LIST_H

#include "common.h"

namespace scn {
    SCN_BEGIN_NAMESPACE

    /**
     * Adapts a `span` into a type that can be read into using \ref
     * scan_list. This way, potentially unnecessary dynamic memory
     * allocations can be avoided. To use as a parameter to \ref scan_list,
     * use \ref make_span_list_wrapper.
     *
     * \code{.cpp}
     * std::vector<int> buffer(8, 0);
     * scn::span<int> s = scn::make_span(buffer);
     *
     * auto wrapper = scn::span_list_wrapper<int>(s);
     * scn::scan_list("123 456", wrapper);
     * // s[0] == buffer[0] == 123
     * // s[1] == buffer[1] == 456
     * \endcode
     *
     * \see scan_list
     * \see make_span_list_wrapper
     */
    template <typename T>
    struct span_list_wrapper {
        using value_type = T;

        span_list_wrapper(span<T> s) : m_span(s) {}

        void push_back(T val)
        {
            SCN_EXPECT(n < max_size());
            m_span[n] = SCN_MOVE(val);
            ++n;
        }

        SCN_NODISCARD constexpr std::size_t size() const noexcept
        {
            return n;
        }
        SCN_NODISCARD constexpr std::size_t max_size() const noexcept
        {
            return m_span.size();
        }

        span<T> m_span;
        std::size_t n{0};
    };

    namespace detail {
        template <typename T>
        using span_list_wrapper_for =
            span_list_wrapper<typename decltype(make_span(
                SCN_DECLVAL(T&)))::value_type>;
    }

    /**
     * Adapts a contiguous buffer into a type containing a `span` that can
     * be read into using \ref scn::scan_list.
     *
     * Example adapted from \ref span_list_wrapper:
     * \code{.cpp}
     * std::vector<int> buffer(8, 0);
     * scn::scan_list("123 456", scn::make_span_list_wrapper(buffer));
     * // s[0] == buffer[0] == 123
     * // s[1] == buffer[1] == 456
     * \endcode
     *
     * \see scan_list
     * \see span_list_wrapper
     */
    template <typename T>
    auto make_span_list_wrapper(T& s)
        -> temporary<detail::span_list_wrapper_for<T>>
    {
        auto _s = make_span(s);
        return temp(span_list_wrapper<typename decltype(_s)::value_type>(_s));
    }

    /**
     * Used to customize `scan_list_ex()`.
     *
     * \tparam CharT Can be a code unit type (`char` or `wchar_t`, depending on
     * the source range), or `code_point`.
     *
     * `list_separator`, `list_until` and `list_separator_and_until` can be used
     * to create a value of this type, taking advantage of template argument
     * deduction (no need to hand-specify `CharT`).
     */
    template <typename CharT>
    struct scan_list_options {
        /**
         * If set, up to one separator character can be accepted between values,
         * which may be surrounded by whitespace.
         */
        optional<CharT> separator{};
        /**
         * If set, reading the list is stopped if this character is found
         * between values.
         *
         * In that case, it is advanced over, and no error is returned.
         */
        optional<CharT> until{};

        scan_list_options() = default;
        scan_list_options(optional<CharT> s, optional<CharT> u)
            : separator(SCN_MOVE(s)), until(SCN_MOVE(u))
        {
        }
    };

    /**
     * Create a `scan_list_options` for `scan_list_ex`, by using `ch` as the
     * separator character.
     */
    template <typename CharT>
    scan_list_options<CharT> list_separator(CharT ch)
    {
        return {optional<CharT>{ch}, nullopt};
    }
    /**
     * Create a `scan_list_options` for `scan_list_ex`, by using `ch` as the
     * until-character.
     */
    template <typename CharT>
    scan_list_options<CharT> list_until(CharT ch)
    {
        return {nullopt, optional<CharT>{ch}};
    }
    /**
     * Create a `scan_list_options` for `scan_list_ex`, by using `sep` as the
     * separator, and `until` as the until-character.
     */
    template <typename CharT>
    scan_list_options<CharT> list_separator_and_until(CharT sep, CharT until)
    {
        return {optional<CharT>{sep}, optional<CharT>{until}};
    }

    namespace detail {
        template <typename WrappedRange, typename CharT>
        expected<CharT> check_separator(WrappedRange& r, size_t& n, CharT)
        {
            auto ret = read_code_unit(r);
            if (!ret) {
                return ret.error();
            }
            n = 1;
            return ret.value();
        }
        template <typename WrappedRange>
        expected<code_point> check_separator(WrappedRange& r,
                                             size_t& n,
                                             code_point)
        {
            unsigned char buf[4] = {0};
            auto ret = read_code_point(r, make_span(buf, 4));
            if (!ret) {
                return ret.error();
            }
            n = ret.value().chars.size();
            return ret.value().cp;
        }

        template <typename Context, typename Container, typename Separator>
        auto scan_list_impl(Context& ctx,
                            bool localized,
                            Container& c,
                            scan_list_options<Separator> options) -> error
        {
            using char_type = typename Context::char_type;
            using value_type = typename Container::value_type;
            value_type value;

            auto args = make_args_for(ctx.range(), 1, value);

            bool scanning = true;
            while (scanning) {
                if (c.size() == c.max_size()) {
                    break;
                }

                // read value
                auto pctx = make_parse_context(1, ctx.locale(), localized);
                auto err = visit(ctx, pctx, basic_args<char_type>{args});
                if (!err) {
                    if (err == error::end_of_range) {
                        break;
                    }
                    return err;
                }
                c.push_back(SCN_MOVE(value));

                auto next = static_cast<Separator>(0);
                size_t n{0};

                auto read_next = [&]() -> error {
                    auto ret = check_separator(ctx.range(), n,
                                               static_cast<Separator>(0));
                    if (!ret) {
                        if (ret.error() == error::end_of_range) {
                            scanning = false;
                            return {};
                        }
                        return ret.error();
                    }
                    next = ret.value();

                    err =
                        putback_n(ctx.range(), static_cast<std::ptrdiff_t>(n));
                    if (!err) {
                        return err;
                    }

                    return {};
                };

                bool sep_found = false;
                while (true) {
                    // read until
                    if (options.until) {
                        err = read_next();
                        if (!err) {
                            return err;
                        }
                        if (!scanning) {
                            break;
                        }

                        if (next == options.until.get()) {
                            scanning = false;
                            break;
                        }
                    }

                    // read sep
                    if (options.separator && !sep_found) {
                        err = read_next();
                        if (!err) {
                            return err;
                        }
                        if (!scanning) {
                            break;
                        }

                        if (next == options.separator.get()) {
                            // skip to next char
                            ctx.range().advance(static_cast<std::ptrdiff_t>(n));
                            continue;
                        }
                    }

                    err = read_next();
                    if (!err) {
                        return err;
                    }
                    if (!scanning) {
                        break;
                    }

                    if (ctx.locale().get_static().is_space(next)) {
                        // skip ws
                        ctx.range().advance(static_cast<std::ptrdiff_t>(n));
                    }
                    else {
                        break;
                    }
                }
            }

            return {};
        }
    }  // namespace detail

    /**
     * Reads values repeatedly from `r` and writes them into `c`.
     *
     * The values read are of type `Container::value_type`, and they are
     * written into `c` using `c.push_back`.
     * The values are separated by whitespace.
     *
     * The range is read, until:
     *  - `c.max_size()` is reached, or
     *  - range `EOF` is reached
     *
     * In these cases, an error will not be returned, and the beginning
     * of the returned range will point to the first character after the
     * scanned list.
     *
     * If an invalid value is scanned, `error::invalid_scanned_value` is
     * returned, but the values already in `vec` will remain there. The range is
     * put back to the state it was before reading the invalid value.
     *
     * To scan into `span`, use \ref span_list_wrapper.
     * \ref make_span_list_wrapper
     *
     * \code{.cpp}
     * std::vector<int> vec{};
     * auto result = scn::scan_list("123 456", vec);
     * // vec == [123, 456]
     * // result.empty() == true
     *
     * vec.clear();
     * result = scn::scan_list("123 456 abc", vec);
     * // vec == [123, 456]
     * // result.error() == invalid_scanned_value
     * // result.range() == " abc"
     * \endcode
     *
     * \param r Range to read from
     * \param c Container to write values to, using `c.push_back()`.
     * `Container::value_type` will be used to determine the type of the values
     * to read.
     */
#if SCN_DOXYGEN
    template <typename Range, typename Container>
    auto scan_list(Range&& r, Container& c)
        -> detail::scan_result_for_range<Range>;
#else
    template <typename Range, typename Container>
    SCN_NODISCARD auto scan_list(Range&& r, Container& c)
        -> detail::scan_result_for_range<Range>
    {
        auto range = wrap(SCN_FWD(r));
        auto ctx = make_context(SCN_MOVE(range));
        using char_type = typename decltype(ctx)::char_type;

        auto err = detail::scan_list_impl(ctx, false, c,
                                          scan_list_options<char_type>{});

        return detail::wrap_result(wrapped_error{err},
                                   detail::range_tag<Range>{},
                                   SCN_MOVE(ctx.range()));
    }
#endif

    /**
     * Otherwise equivalent to `scan_list()`, except can react to additional
     * characters, based on `options`.
     *
     * See `scan_list_options` for more information.
     *
     * \param r Range to scan from
     * \param c Container to write read values into
     * \param options Options to use
     *
     * \code{.cpp}
     * std::vector<int> vec{};
     * auto result = scn::scan_list_ex("123, 456", vec,
     *                                 scn::list_separator(','));
     * // vec == [123, 456]
     * // result.empty() == true
     * \endcode
     *
     * \see scan_list
     * \see scan_list_options
     */
#if SCN_DOXYGEN
    template <typename Range, typename Container, typename CharT>
    auto scan_list_ex(Range&& r, Container& c, scan_list_options<CharT> options)
        -> detail::scan_result_for_range<Range>;
#else
    template <typename Range, typename Container, typename CharT>
    SCN_NODISCARD auto scan_list_ex(Range&& r,
                                    Container& c,
                                    scan_list_options<CharT> options)
        -> detail::scan_result_for_range<Range>
    {
        auto range = wrap(SCN_FWD(r));
        auto ctx = make_context(SCN_MOVE(range));

        auto err = detail::scan_list_impl(ctx, false, c, options);

        return detail::wrap_result(wrapped_error{err},
                                   detail::range_tag<Range>{},
                                   SCN_MOVE(ctx.range()));
    }
#endif

    /**
     * Otherwise equivalent to `scan_list_ex()`, except uses `loc` to scan the
     * values.
     *
     * \param loc Locale to use for scanning. Must be a `std::locale`.
     * \param r Range to scan from
     * \param c Container to write read values into
     * \param options Options to use
     *
     * \see scan_list_ex()
     * \see scan_localized()
     */
#if SCN_DOXYGEN
    template <typename Locale,
              typename Range,
              typename Container,
              typename CharT>
    auto scan_list_localized(const Locale& loc,
                             Range&& r,
                             Container& c,
                             scan_list_options<CharT> options)
        -> detail::scan_result_for_range<Range>;
#else
    template <typename Locale,
              typename Range,
              typename Container,
              typename CharT>
    SCN_NODISCARD auto scan_list_localized(const Locale& loc,
                                           Range&& r,
                                           Container& c,
                                           scan_list_options<CharT> options)
        -> detail::scan_result_for_range<Range>
    {
        auto range = wrap(SCN_FWD(r));
        using char_type = typename decltype(range)::char_type;
        auto locale = make_locale_ref<char_type>(loc);
        auto ctx = make_context(SCN_MOVE(range), SCN_MOVE(locale));

        auto err = detail::scan_list_impl(ctx, true, c, options);

        return detail::wrap_result(wrapped_error{err},
                                   detail::range_tag<Range>{},
                                   SCN_MOVE(ctx.range()));
    }
#endif

    SCN_END_NAMESPACE
}  // namespace scn

#endif
