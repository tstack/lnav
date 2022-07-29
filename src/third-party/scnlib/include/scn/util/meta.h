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

#ifndef SCN_UTIL_META_H
#define SCN_UTIL_META_H

#include "../detail/fwd.h"

#include <type_traits>

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace detail {
        template <typename... Ts>
        struct make_void {
            using type = void;
        };
        template <typename... Ts>
        using void_t = typename make_void<Ts...>::type;

        template <typename... T>
        void valid_expr(T&&...);

        template <typename T>
        struct remove_cvref {
            using type = typename std::remove_cv<
                typename std::remove_reference<T>::type>::type;
        };
        template <typename T>
        using remove_cvref_t = typename remove_cvref<T>::type;

        // Stolen from range-v3
        template <typename T>
        struct static_const {
            static constexpr T value{};
        };
        template <typename T>
        constexpr T static_const<T>::value;

        template <std::size_t I>
        struct priority_tag : priority_tag<I - 1> {
        };
        template <>
        struct priority_tag<0> {
        };

        struct dummy_type {
        };

        template <typename T>
        struct dependent_false : std::false_type {
        };

        template <typename T>
        using integer_type_for_char = typename std::
            conditional<std::is_signed<T>::value, int, unsigned>::type;
    }  // namespace detail

    SCN_END_NAMESPACE
}  // namespace scn

#endif
