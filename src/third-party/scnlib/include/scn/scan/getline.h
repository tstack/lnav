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

#ifndef SCN_SCAN_GETLINE_H
#define SCN_SCAN_GETLINE_H

#include "common.h"

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace detail {
        template <typename WrappedRange,
                  typename String,
                  typename Until,
                  typename CharT = typename WrappedRange::char_type>
        error getline_impl(WrappedRange& r, String& str, Until until)
        {
            auto pred = until_pred<CharT>{until};
            auto s = read_until_space_zero_copy(r, pred, true);
            if (!s) {
                return s.error();
            }
            if (s.value().size() != 0) {
                auto size = s.value().size();
                if (pred(s.value().last(1))) {
                    --size;
                }
                str.clear();
                str.resize(size);
                std::copy(s.value().begin(), s.value().begin() + size,
                          str.begin());
                return {};
            }

            String tmp;
            auto out = std::back_inserter(tmp);
            auto e = read_until_space(r, out, pred, true);
            if (!e) {
                return e;
            }
            if (pred(span<const CharT>(&*(tmp.end() - 1), 1))) {
                tmp.pop_back();
            }
            str = SCN_MOVE(tmp);
            return {};
        }
        template <typename WrappedRange,
                  typename Until,
                  typename CharT = typename WrappedRange::char_type>
        error getline_impl(WrappedRange& r,
                           basic_string_view<CharT>& str,
                           Until until)
        {
            static_assert(
                WrappedRange::is_contiguous,
                "Cannot getline a string_view from a non-contiguous range");
            auto pred = until_pred<CharT>{until};
            auto s = read_until_space_zero_copy(r, pred, true);
            if (!s) {
                return s.error();
            }
            SCN_ASSERT(s.value().size(), "");
            auto size = s.value().size();
            if (pred(s.value().last(1))) {
                --size;
            }
            str = basic_string_view<CharT>{s.value().data(), size};
            return {};
        }
#if SCN_HAS_STRING_VIEW
        template <typename WrappedRange,
                  typename Until,
                  typename CharT = typename WrappedRange::char_type>
        auto getline_impl(WrappedRange& r,
                          std::basic_string_view<CharT>& str,
                          Until until) -> error
        {
            auto sv = ::scn::basic_string_view<CharT>{};
            auto ret = getline_impl(r, sv, until);
            str = ::std::basic_string_view<CharT>{sv.data(), sv.size()};
            return ret;
        }
#endif
    }  // namespace detail

    /**
     * Read the range in \c r into \c str until \c until is found.
     * \c until will be skipped in parsing: it will not be pushed into \c
     * str, and the returned range will go past it.
     *
     * If `str` is convertible to a `basic_string_view`:
     *  - And if `r` is a `contiguous_range`:
     *    - `str` is set to point inside `r` with the appropriate length
     *  - if not, returns an error
     *
     * Otherwise, clears `str` by calling `str.clear()`, and then reads the
     * range into `str` as if by repeatedly calling \c str.push_back.
     * `str.reserve()` is also required to be present.
     *
     * `Until` can either be the same as `r` character type (`char` or
     * `wchar_t`), or `code_point`.
     *
     * \code{.cpp}
     * auto source = "hello\nworld"
     * std::string line;
     * auto result = scn::getline(source, line, '\n');
     * // line == "hello"
     * // result.range() == "world"
     *
     * // Using the other overload
     * result = scn::getline(result.range(), line);
     * // line == "world"
     * // result.empty() == true
     * \endcode
     */
#if SCN_DOXYGEN
    template <typename Range, typename String, typename Until>
    auto getline(Range&& r, String& str, Until until)
        -> detail::scan_result_for_range<Range>;
#else
    template <typename Range, typename String, typename Until>
    SCN_NODISCARD auto getline(Range&& r, String& str, Until until)
        -> detail::scan_result_for_range<Range>
    {
        auto wrapped = wrap(SCN_FWD(r));
        auto err = getline_impl(wrapped, str, until);
        if (!err) {
            auto e = wrapped.reset_to_rollback_point();
            if (!e) {
                err = e;
            }
        }
        else {
            wrapped.set_rollback_point();
        }
        return detail::wrap_result(
            wrapped_error{err}, detail::range_tag<Range>{}, SCN_MOVE(wrapped));
    }
#endif

    /**
     * Equivalent to \ref getline with the last parameter set to
     * <tt>'\\n'</tt> with the appropriate character type.
     *
     * In other words, reads `r` into `str` until <tt>'\\n'</tt> is found.
     *
     * The character type is determined by `r`.
     */
#if SCN_DOXYGEN
    template <typename Range,
              typename String,
              typename CharT = typename detail::extract_char_type<
                  ranges::iterator_t<range_wrapper_for_t<Range>>>::type>
    auto getline(Range&& r, String& str)
        -> detail::scan_result_for_range<Range>;
#else
    template <typename Range,
              typename String,
              typename CharT = typename detail::extract_char_type<
                  ranges::iterator_t<range_wrapper_for_t<Range>>>::type>
    SCN_NODISCARD auto getline(Range&& r, String& str)
        -> detail::scan_result_for_range<Range>
    {
        return getline(SCN_FWD(r), str, detail::ascii_widen<CharT>('\n'));
    }
#endif

    SCN_END_NAMESPACE
}  // namespace scn

#endif
