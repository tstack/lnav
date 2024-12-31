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

#if !SCN_DISABLE_REGEX

#include <vector>

namespace scn {
SCN_BEGIN_NAMESPACE

/**
 * \defgroup regex Regular expressions
 *
 * In header `<scn/regex.h>`
 *
 * scnlib doesn't do the regex processing itself, but delegates that task
 * to an external regex engine. This behavior is controlled by the
 * CMake option `SCN_REGEX_BACKEND`, which defaults to `std` (use
 * `std::regex`). Other possible options are `Boost` and `re2`.
 *
 * The exact feature set, syntax, and semantics of the regular expressions
 * may differ between the different backends.
 * See the documentation for each to learn more about the supported syntax.
 * In general:
 *  - `std` is available without external dependencies, but doesn't support
 *     named captures, has limited support for flags, and is slow.
 *  - `Boost` has the largest feature set, but is slow.
 *  - `re2` is fast, but doesn't support all regex features.
 *
 * <table>
 * <caption id="regex-cmp-table">
 * Regex backend feature comparison
 * </caption>
 *
 * <tr>
 * <th>Feature</th>
 * <th>`std`</th>
 * <th>`Boost`</th>
 * <th>`re2`</th>
 * </tr>
 *
 * <tr>
 * <td>Named captures</td>
 * <td>No</td>
 * <td>Yes</td>
 * <td>Yes</td>
 * </tr>
 *
 * <tr>
 * <td>Wide strings (`wchar_t`) as input</td>
 * <td>Yes</td>
 * <td>Yes</td>
 * <td>No</td>
 * </tr>
 *
 * <tr>
 * <td>Unicode character classes (i.e. `\pL`)</td>
 * <td>No</td>
 * <td>Yes-ish <sup>[1]</sup></td>
 * <td>Yes</td>
 * </tr>
 *
 * <tr>
 * <td>Character classes (like this: `[[:alpha:]]`) match non-ASCII</td>
 * <td>No</td>
 * <td>Depends <sup>[2]</sup></td>
 * <td>No</td>
 * </tr>
 * </table>
 *
 * <sup>[1][2]</sup>: The behavior of Boost.Regex varies, whether it's
 * using the ICU or not. If it is, character classes like `\pL` and
 * `[[:alpha:]]` can match any non-ASCII characters. Otherwise, only ASCII
 * characters are matched.
 *
 * To do regex matching, the scanned type must either be a string
 * (`std::basic_string` or `std::basic_string_view`), or
 * `scn::basic_regex_matches`.
 * Due to limitations of the underlying regex engines,
 * the source must be contiguous.
 *
 * <table>
 * <caption id="regex-flags-table">
 * Possible flags for regex scanning
 * </caption>
 *
 * <tr>
 * <th>Flag</th>
 * <th>Description</th>
 * <th>Support</th>
 * </tr>
 *
 * <tr>
 * <td>`/m`</td>
 * <td>
 * `multiline`:
 * `^` matches the beginning of a line, and `$` the end of a line.
 * </td>
 * <td>
 * Supported by `Boost` and `re2`.
 * For `std`, uses `std::regex_constants::multiline`,
 * which was introduced in C++17, but isn't implemented by MSVC.
 * </td>
 * </tr>
 *
 * <tr>
 * <td>`/s`</td>
 * <td>
 * `singleline`:
 * `.` matches a newline.
 * </td>
 * <td>
 * Supported by `Boost` and `re2`, not by `std`.
 * </td>
 * </tr>
 *
 * <tr>
 * <td>`/i`</td>
 * <td>
 * `icase`:
 * Matches are case-insensitive.
 * </td>
 * <td>
 * Supported by everyone: `std`, `Boost`, and `re2`.
 * </td>
 * </tr>
 *
 * <tr>
 * <td>`/n`</td>
 * <td>
 * `nosubs`:
 * Subexpressions aren't matched and stored separately.
 * </td>
 * <td>
 * Supported by everyone: `std`, `Boost`, and `re2`.
 * </td>
 * </tr>
 * </table>
 */

/**
 * A single (sub)expression match.
 *
 * \ingroup regex
 */
template <typename CharT>
class basic_regex_match {
public:
    using char_type = CharT;

    basic_regex_match(std::basic_string_view<CharT> str) : m_str(str) {}

#if SCN_REGEX_SUPPORTS_NAMED_CAPTURES
    basic_regex_match(std::basic_string_view<CharT> str,
                      std::basic_string<CharT> name)
        : m_str(str), m_name(name)
    {
    }
#endif

    /// Matched string
    std::basic_string_view<CharT> get() const
    {
        return m_str;
    }

    auto operator*() const
    {
        return m_str;
    }
    auto operator->() const
    {
        return &m_str;
    }

#if SCN_REGEX_SUPPORTS_NAMED_CAPTURES
    /// The name of this capture, if any.
    std::optional<std::basic_string_view<CharT>> name() const
    {
        return m_name;
    }
#endif

private:
    std::basic_string_view<CharT> m_str;

#if SCN_REGEX_SUPPORTS_NAMED_CAPTURES
    std::optional<std::basic_string<CharT>> m_name;
#endif
};

/**
 * Can be used to get all subexpression captures of a regex match.
 * Interface similar to
 * `std::vector<std::optional<basic_regex_match<CharT>>>`.
 *
 * \code{.cpp}
 * auto result =
 *     scn::scan<scn::regex_matches>("abc123", "{:/[(a-z]+)([0-9]+)/}");
 * // result->value() has three elements:
 * //  [0]: "abc123" (entire match)
 * //  [1]: "abc" (first subexpression match)
 * //  [2]: "123" (second subexpression match)
 * \endcode
 *
 * \ingroup regex
 */
template <typename CharT>
class basic_regex_matches
    : private std::vector<std::optional<basic_regex_match<CharT>>> {
    using base = std::vector<std::optional<basic_regex_match<CharT>>>;

public:
    using match_type = basic_regex_match<CharT>;
    using typename base::const_iterator;
    using typename base::const_reverse_iterator;
    using typename base::iterator;
    using typename base::pointer;
    using typename base::reference;
    using typename base::reverse_iterator;
    using typename base::size_type;
    using typename base::value_type;

    using base::base;

    using base::emplace;
    using base::emplace_back;
    using base::insert;
    using base::push_back;

    using base::reserve;
    using base::resize;

    using base::at;
    using base::operator[];

    using base::begin;
    using base::end;
    using base::rbegin;
    using base::rend;

    using base::data;
    using base::size;

    using base::swap;
};

SCN_END_NAMESPACE
}  // namespace scn

#endif
