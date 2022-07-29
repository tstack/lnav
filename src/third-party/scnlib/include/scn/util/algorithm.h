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

#ifndef SCN_UTIL_ALGORITHM_H
#define SCN_UTIL_ALGORITHM_H

#include "../detail/fwd.h"

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace detail {
        /**
         * Implementation of `std::exchange` for C++11
         */
        template <typename T, typename U = T>
        SCN_CONSTEXPR14 T exchange(T& obj, U&& new_value)
        {
            T old_value = SCN_MOVE(obj);
            obj = SCN_FWD(new_value);
            return old_value;
        }

        /**
         * Implementation of `std::max` without including `<algorithm>`
         */
        template <typename T>
        constexpr T max(T a, T b) noexcept
        {
            return (a < b) ? b : a;
        }

        /**
         * Implementation of `std::min_element` without including `<algorithm>`
         */
        template <typename It>
        SCN_CONSTEXPR14 It min_element(It first, It last)
        {
            if (first == last) {
                return last;
            }

            It smallest = first;
            ++first;
            for (; first != last; ++first) {
                if (*first < *smallest) {
                    smallest = first;
                }
            }
            return smallest;
        }

        /**
         * Implementation of `std::min` without including `<algorithm>`
         */
        template <typename T>
        constexpr T min(T a, T b) noexcept
        {
            return (b < a) ? b : a;
        }
    }  // namespace detail

    SCN_END_NAMESPACE
}  // namespace scn

#endif
