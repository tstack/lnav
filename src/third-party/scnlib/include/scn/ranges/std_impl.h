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

#ifndef SCN_RANGES_STD_IMPL_H
#define SCN_RANGES_STD_IMPL_H

#include "../detail/config.h"

#if SCN_HAS_CONCEPTS && SCN_HAS_RANGES

SCN_GCC_PUSH
SCN_GCC_IGNORE("-Wnoexcept")
#include <iterator>
#include <ranges>
SCN_GCC_POP

#include "util.h"

#include "../util/string_view.h"

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace std_ranges = ::std::ranges;

    namespace polyfill_2a {
        template <typename T>
        using iter_value_t = ::std::iter_value_t<T>;
        template <typename T>
        using iter_reference_t = ::std::iter_reference_t<T>;
        template <typename T>
        using iter_difference_t = ::std::iter_difference_t<T>;

        template <typename I>
        concept bidirectional_iterator = std::bidirectional_iterator<I>;
        template <typename I>
        concept random_access_iterator = std::random_access_iterator<I>;
    }  // namespace polyfill_2a

    SCN_END_NAMESPACE
}  // namespace scn

namespace std::ranges {
    template <typename CharT>
    inline constexpr bool enable_view<::scn::basic_string_view<CharT>> = true;
    template <typename T>
    inline constexpr bool enable_view<::scn::span<T>> = true;
}  // namespace std

#define SCN_CHECK_CONCEPT(C) C
#endif

#endif  // SCN_RANGES_STD_IMPL_H
