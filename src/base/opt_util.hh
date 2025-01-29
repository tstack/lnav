/**
 * Copyright (c) 2019, Timothy Stack
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * * Neither the name of Timothy Stack nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef lnav_opt_util_hh
#define lnav_opt_util_hh

#include <optional>

#include <stdlib.h>

namespace detail {

template<class T>
typename std::enable_if<std::is_void_v<T>, T>::type
void_or_nullopt()
{
    return;
}

template<class T>
typename std::enable_if<not std::is_void_v<T>, T>::type
void_or_nullopt()
{
    return std::nullopt;
}

template<class T>
struct is_optional : std::false_type {};

template<class T>
struct is_optional<std::optional<T>> : std::true_type {};
}  // namespace detail

template<class T,
         class F,
         std::enable_if_t<detail::is_optional<std::decay_t<T>>::value, int> = 0>
auto
operator|(T&& t, F f) -> decltype(detail::void_or_nullopt<decltype(f(
                                      std::forward<T>(t).operator*()))>())
{
    using return_type = decltype(f(std::forward<T>(t).operator*()));
    if (t) {
        return f(std::forward<T>(t).operator*());
    }

    return detail::void_or_nullopt<return_type>();
}

template<class T>
constexpr std::optional<std::decay_t<T>>
make_optional_from_nullable(T&& v)
{
    if (v != nullptr) {
        return std::optional<std::decay_t<T>>(std::forward<T>(v));
    }
    return std::nullopt;
}

template<template<typename, typename...> class C, typename T>
std::optional<T>
cget(const C<T>& container, size_t index)
{
    if (index < container.size()) {
        return container[index];
    }

    return std::nullopt;
}

inline std::optional<const char*>
getenv_opt(const char* name)
{
    return make_optional_from_nullable(getenv(name));
}

#endif
