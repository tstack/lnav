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

#ifndef SCN_SCAN_IGNORE_H
#define SCN_SCAN_IGNORE_H

#include "common.h"

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace detail {
        template <typename CharT>
        struct ignore_iterator {
            using value_type = CharT;
            using pointer = value_type*;
            using reference = value_type&;
            using difference_type = std::ptrdiff_t;
            using iterator_category = std::output_iterator_tag;

            constexpr ignore_iterator() = default;

            SCN_CONSTEXPR14 ignore_iterator& operator=(CharT) noexcept
            {
                return *this;
            }
            constexpr const ignore_iterator& operator=(CharT) const noexcept
            {
                return *this;
            }

            SCN_CONSTEXPR14 ignore_iterator& operator*() noexcept
            {
                return *this;
            }
            constexpr const ignore_iterator& operator*() const noexcept
            {
                return *this;
            }

            SCN_CONSTEXPR14 ignore_iterator& operator++() noexcept
            {
                return *this;
            }
            constexpr const ignore_iterator& operator++() const noexcept
            {
                return *this;
            }
        };

        template <typename CharT>
        struct ignore_iterator_n {
            using value_type = CharT;
            using pointer = value_type*;
            using reference = value_type&;
            using difference_type = std::ptrdiff_t;
            using iterator_category = std::output_iterator_tag;

            ignore_iterator_n() = default;
            ignore_iterator_n(difference_type n) : i(n) {}

            constexpr const ignore_iterator_n& operator=(CharT) const noexcept
            {
                return *this;
            }

            constexpr const ignore_iterator_n& operator*() const noexcept
            {
                return *this;
            }

            SCN_CONSTEXPR14 ignore_iterator_n& operator++() noexcept
            {
                ++i;
                return *this;
            }

            constexpr bool operator==(const ignore_iterator_n& o) const noexcept
            {
                return i == o.i;
            }
            constexpr bool operator!=(const ignore_iterator_n& o) const noexcept
            {
                return !(*this == o);
            }

            difference_type i{0};
        };

        template <typename WrappedRange,
                  typename Until,
                  typename CharT = typename WrappedRange::char_type>
        error ignore_until_impl(WrappedRange& r, Until until)
        {
            ignore_iterator<CharT> it{};
            return read_until_space(r, it, until_pred<CharT>{until}, false);
        }

        template <typename WrappedRange,
                  typename Until,
                  typename CharT = typename WrappedRange::char_type>
        error ignore_until_n_impl(WrappedRange& r,
                                  ranges::range_difference_t<WrappedRange> n,
                                  Until until)
        {
            ignore_iterator_n<CharT> begin{}, end{n};
            return read_until_space_ranged(r, begin, end,
                                           until_pred<CharT>{until}, false);
        }
    }  // namespace detail

    /**
     * Advances the beginning of \c r until \c until is found.
     */
#if SCN_DOXYGEN
    template <typename Range, typename Until>
    auto ignore_until(Range&& r, Until until)
        -> detail::scan_result_for_range<Range>;
#else
    template <typename Range, typename Until>
    SCN_NODISCARD auto ignore_until(Range&& r, Until until)
        -> detail::scan_result_for_range<Range>
    {
        auto wrapped = wrap(SCN_FWD(r));
        auto err = detail::ignore_until_impl(wrapped, until);
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
     * Advances the beginning of \c r until \c until is found, or the
     * beginning has been advanced \c n times.
     *
     * `Until` can be the `r` character type (`char` or `wchar_t`), or
     * `code_point`.
     */
#if SCN_DOXYGEN
    template <typename Range, typename Until>
    auto ignore_until_n(Range&& r,
                        ranges::range_difference_t<Range> n,
                        Until until) -> detail::scan_result_for_range<Range>;
#else
    template <typename Range, typename Until>
    SCN_NODISCARD auto ignore_until_n(Range&& r,
                                      ranges::range_difference_t<Range> n,
                                      Until until)
        -> detail::scan_result_for_range<Range>
    {
        auto wrapped = wrap(SCN_FWD(r));
        auto err = detail::ignore_until_n_impl(wrapped, n, until);
        if (!err) {
            auto e = wrapped.reset_to_rollback_point();
            if (!e) {
                err = e;
            }
        }
        return detail::wrap_result(
            wrapped_error{err}, detail::range_tag<Range>{}, SCN_MOVE(wrapped));
    }
#endif

    SCN_END_NAMESPACE
}  // namespace scn

#endif
