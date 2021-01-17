/**
 * Copyright (c) 2021, Timothy Stack
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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file injector.bind.hh
 */

#ifndef lnav_injector_bind_hh
#define lnav_injector_bind_hh

#include "injector.hh"

namespace injector {

template<typename T, typename...Annotations>
struct bind : singleton_storage<T, Annotations...> {
    static bool to_singleton() noexcept {
        static T storage;

        singleton_storage<T, Annotations...>::ss_data = &storage;
        return true;
    }

    template<typename...Args>
    static bool to_instance(T* (*f)(Args...)) noexcept {
        singleton_storage<T, Annotations...>::ss_data = f(::injector::get<Args>()...);
        return true;
    }

    template<typename I>
    static bool to(Impl<I> i) {
        singleton_storage<T, Annotations...>::ss_factory = []() {
            return std::make_shared<I>();
        };
        return true;
    }
};

template<typename T>
struct bind_multiple : multiple_storage<T> {
    bind_multiple() noexcept = default;

    template<typename I, typename R, typename ...Args>
    bind_multiple& add(R (*)(Args...)) {
        multiple_storage<T>::ms_factories[typeid(I).name()] = []() {
            return std::make_shared<I>(::injector::get<Args>()...);
        };

        return *this;
    }

    template<typename I,
        std::enable_if_t<has_injectable<I>::value, bool> = true>
    bind_multiple& add() {
        typename I::injectable *i = nullptr;

        return this->add<I>(i);
    }

    template<typename I,
        std::enable_if_t<!has_injectable<I>::value, bool> = true>
    bind_multiple& add() noexcept {
        multiple_storage<T>::ms_factories[typeid(I).name()] = []() {
            return std::make_shared<I>();
        };

        return *this;
    }
};

}

#endif
