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

#pragma once

#include <scn/scan.h>

namespace scn {
SCN_BEGIN_NAMESPACE

/**
 * \defgroup xchar Wide character APIs
 *
 * \brief Scanning interfaces taking wide strings (`wchar_t`).
 *
 * The header `<scn/xchar.h>` needs to be included for these APIs.
 */

// vscan

/**
 * \ingroup xchar
 *
 * \see vscan()
 */
template <typename Range>
auto vscan(Range&& range, std::wstring_view format, wscan_args args)
    -> vscan_result<Range>
{
    return detail::vscan_generic(SCN_FWD(range), format, args);
}

/**
 * \ingroup xchar
 *
 * \see vscan()
 */
template <typename Range,
          typename Locale,
          std::void_t<decltype(Locale::classic())>* = nullptr>
auto vscan(const Locale& loc,
           Range&& range,
           std::wstring_view format,
           wscan_args args) -> vscan_result<Range>
{
    return detail::vscan_localized_generic(loc, SCN_FWD(range), format, args);
}

/**
 * \ingroup xchar
 *
 * \see vscan_value()
 */
template <typename Range>
auto vscan_value(Range&& range, basic_scan_arg<wscan_context> arg)
    -> vscan_result<Range>
{
    return detail::vscan_value_generic(SCN_FWD(range), arg);
}

// scan

/**
 * \ingroup xchar
 *
 * \see scan()
 */
template <typename... Args,
          typename Source,
          std::enable_if_t<detail::is_wide_range<Source>>* = nullptr>
SCN_NODISCARD auto scan(Source&& source,
                        wscan_format_string<Source, Args...> format)
    -> scan_result_type<Source, Args...>
{
    auto result = make_scan_result<Source, Args...>();
    fill_scan_result(result,
                     vscan(SCN_FWD(source), format,
                           make_scan_args<wscan_context>(result->values())));
    return result;
}

/**
 * \ingroup xchar
 *
 * \see scan()
 */
template <typename... Args,
          typename Source,
          std::enable_if_t<detail::is_wide_range<Source>>* = nullptr>
SCN_NODISCARD auto scan(Source&& source,
                        wscan_format_string<Source, Args...> format,
                        std::tuple<Args...>&& initial_args)
    -> scan_result_type<Source, Args...>
{
    auto result = make_scan_result<Source>(SCN_MOVE(initial_args));
    fill_scan_result(result,
                     vscan(SCN_FWD(source), format,
                           make_scan_args<wscan_context>(result->values())));
    return result;
}

/**
 * \ingroup xchar
 *
 * \see scan()
 */
template <typename... Args,
          typename Locale,
          typename Source,
          std::enable_if_t<detail::is_wide_range<Source>>* = nullptr,
          std::void_t<decltype(Locale::classic())>* = nullptr>
SCN_NODISCARD auto scan(const Locale& loc,
                        Source&& source,
                        wscan_format_string<Source, Args...> format)
    -> scan_result_type<Source, Args...>
{
    auto result = make_scan_result<Source, Args...>();
    fill_scan_result(result,
                     vscan(loc, SCN_FWD(source), format,
                           make_scan_args<wscan_context>(result->values())));
    return result;
}

/**
 * \ingroup xchar
 *
 * \see scan()
 */
template <typename... Args,
          typename Locale,
          typename Source,
          std::enable_if_t<detail::is_wide_range<Source>>* = nullptr,
          std::void_t<decltype(Locale::classic())>* = nullptr>
SCN_NODISCARD auto scan(const Locale& loc,
                        Source&& source,
                        wscan_format_string<Source, Args...> format,
                        std::tuple<Args...>&& initial_args)
    -> scan_result_type<Source, Args...>
{
    auto result = make_scan_result<Source>(SCN_MOVE(initial_args));
    fill_scan_result(result,
                     vscan(loc, SCN_FWD(source), format,
                           make_scan_args<wscan_context>(result->values())));
    return result;
}

/**
 * \ingroup xchar
 *
 * \see scan_value()
 */
template <typename T,
          typename Source,
          std::enable_if_t<detail::is_wide_range<Source>>* = nullptr>
SCN_NODISCARD auto scan_value(Source&& source) -> scan_result_type<Source, T>
{
    auto result = make_scan_result<Source, T>();
    fill_scan_result(
        result, vscan_value(SCN_FWD(source),
                            detail::make_arg<wscan_context>(result->value())));
    return result;
}

/**
 * \ingroup xchar
 *
 * \see scan_value()
 */
template <typename T,
          typename Source,
          std::enable_if_t<detail::is_wide_range<Source>>* = nullptr>
SCN_NODISCARD auto scan_value(Source&& source, T initial_value)
    -> scan_result_type<Source, T>
{
    auto result =
        make_scan_result<Source>(std::tuple<T>{SCN_MOVE(initial_value)});
    fill_scan_result(
        result, vscan_value(SCN_FWD(source),
                            detail::make_arg<wscan_context>(result->value())));
    return result;
}

namespace detail {
SCN_DECLARE_EXTERN_SCANNER_SCAN_FOR_CTX(wscan_context)
#undef SCN_DECLARE_EXTERN_SCANNER_SCAN_FOR_CTX
}  // namespace detail

SCN_END_NAMESPACE
}  // namespace scn
