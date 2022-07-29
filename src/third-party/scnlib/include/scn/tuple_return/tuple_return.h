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

#ifndef SCN_TUPLE_RETURN_TUPLE_RETURN_H
#define SCN_TUPLE_RETURN_TUPLE_RETURN_H

#include "../scan/vscan.h"
#include "util.h"

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace dummy {
    }

/**
 * Alternative interface for scanning, returning values as a tuple, instead
 * of taking them by reference.
 *
 * It's highly recommended to use this interface only with C++17 or later,
 * as structured bindings make it way more ergonomic.
 *
 * Compared to the regular scan interface, the performance of this interface
 * is the same (generated code is virtually identical with optimizations
 * enabled), but the compile time is slower.
 *
 * Values scanned by this function still need to be default-constructible.
 * To scan a non-default-constructible value, use \c scn::optional
 *
 * \param r Input range
 * \param f Format string to use
 *
 * \return Tuple, where the first element is the scan result, and the
 * remaining elements are the scanned values.
 */
#if SCN_DOXYGEN
    template <typename... Args, typename Range, typename Format>
    auto scan_tuple(Range&& r, Format f)
        -> std::tuple<detail::scan_result_for_range<Range>, Args...>;
#else
    template <typename... Args, typename Range, typename Format>
    SCN_NODISCARD auto scan_tuple(Range&& r, Format f)
        -> std::tuple<detail::scan_result_for_range<Range>, Args...>
    {
        using result = detail::scan_result_for_range<Range>;
        using range_type = typename result::wrapped_range_type;

        using context_type = basic_context<range_type>;
        using parse_context_type =
            basic_parse_context<typename context_type::locale_type>;
        using char_type = typename range_type::char_type;

        auto range = wrap(SCN_FWD(r));
        auto scanfn = [&range, &f](Args&... a) {
            auto args = make_args<context_type, parse_context_type>(a...);
            return vscan(SCN_MOVE(range), basic_string_view<char_type>(f),
                         {args});
        };

        std::tuple<Args...> values{Args{}...};
        auto ret = detail::apply(scanfn, values);
        return std::tuple_cat(
            std::tuple<result>{detail::wrap_result(wrapped_error{ret.err},
                                                   detail::range_tag<Range>{},
                                                   SCN_MOVE(ret.range))},
            SCN_MOVE(values));
    }
#endif

    /**
     * Equivalent to `scan_tuple`, except uses `vscan_default` under the hood.
     */
#if SCN_DOXYGEN
    template <typename... Args, typename Range>
    auto scan_tuple_default(Range&& r)
        -> std::tuple<detail::scan_result_for_range<Range>, Args...>;
#else
    template <typename... Args, typename Range>
    SCN_NODISCARD auto scan_tuple_default(Range&& r)
        -> std::tuple<detail::scan_result_for_range<Range>, Args...>
    {
        using result = detail::scan_result_for_range<Range>;
        using range_type = typename result::wrapped_range_type;

        using context_type = basic_context<range_type>;
        using parse_context_type =
            basic_empty_parse_context<typename context_type::locale_type>;

        auto range = wrap(SCN_FWD(r));
        auto scanfn = [&range](Args&... a) {
            auto args = make_args<context_type, parse_context_type>(a...);
            return vscan_default(SCN_MOVE(range),
                                 static_cast<int>(sizeof...(Args)), {args});
        };

        std::tuple<Args...> values{Args{}...};
        auto ret = detail::apply(scanfn, values);
        return std::tuple_cat(
            std::tuple<result>{detail::wrap_result(wrapped_error{ret.err},
                                                   detail::range_tag<Range>{},
                                                   SCN_MOVE(ret.range))},
            SCN_MOVE(values));
    }
#endif

    SCN_END_NAMESPACE
}  // namespace scn

#endif
