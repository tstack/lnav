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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file injector.bind.hh
 */

#ifndef lnav_injector_bind_hh
#define lnav_injector_bind_hh

#include "injector.hh"

namespace injector {

namespace details {

template<typename I, typename R, typename... Args>
std::function<std::shared_ptr<I>()>
create_factory(R (*)(Args...))
{
    return []() { return std::make_shared<I>(::injector::get<Args>()...); };
}

template<typename I, std::enable_if_t<has_injectable<I>::value, bool> = true>
std::function<std::shared_ptr<I>()>
create_factory()
{
    typename I::injectable* i = nullptr;

    return create_factory<I>(i);
}

template<typename I, std::enable_if_t<!has_injectable<I>::value, bool> = true>
std::function<std::shared_ptr<I>()>
create_factory() noexcept
{
    return []() { return std::make_shared<I>(); };
}

}  // namespace details

template<typename T, typename... Annotations>
struct bind : singleton_storage<T, Annotations...> {
    template<typename I = T,
             std::enable_if_t<has_injectable<I>::value, bool> = true>
    static bool to_singleton() noexcept
    {
        typename I::injectable* i = nullptr;
        singleton_storage<T, Annotations...>::ss_owner
            = create_from_injectable<I>(i)();
        singleton_storage<T, Annotations...>::ss_data
            = singleton_storage<T, Annotations...>::ss_owner.get();
        singleton_storage<T, Annotations...>::ss_scope = scope::singleton;

        return true;
    }

    template<typename I = T,
             std::enable_if_t<!has_injectable<I>::value, bool> = true>
    static bool to_singleton() noexcept
    {
        singleton_storage<T, Annotations...>::ss_owner = std::make_shared<T>();
        singleton_storage<T, Annotations...>::ss_data
            = singleton_storage<T, Annotations...>::ss_owner.get();
        singleton_storage<T, Annotations...>::ss_scope = scope::singleton;
        return true;
    }

    struct lifetime {
        ~lifetime()
        {
            singleton_storage<T, Annotations...>::ss_owner = nullptr;
            singleton_storage<T, Annotations...>::ss_data = nullptr;
        }
    };

    template<typename I = T,
             std::enable_if_t<has_injectable<I>::value, bool> = true>
    static lifetime to_scoped_singleton() noexcept
    {
        typename I::injectable* i = nullptr;
        singleton_storage<T, Annotations...>::ss_owner
            = create_from_injectable<I>(i)();
        singleton_storage<T, Annotations...>::ss_data
            = singleton_storage<T, Annotations...>::ss_owner.get();
        singleton_storage<T, Annotations...>::ss_scope = scope::singleton;

        return {};
    }

    template<typename... Args>
    static bool to_instance(T* (*f)(Args...)) noexcept
    {
        singleton_storage<T, Annotations...>::ss_data
            = f(::injector::get<Args>()...);
        singleton_storage<T, Annotations...>::ss_scope = scope::singleton;
        return true;
    }

    static bool to_instance(T* data) noexcept
    {
        singleton_storage<T, Annotations...>::ss_data = data;
        singleton_storage<T, Annotations...>::ss_scope = scope::singleton;
        return true;
    }

    template<typename I>
    static bool to() noexcept
    {
        singleton_storage<T, Annotations...>::ss_factory
            = details::create_factory<I>();
        singleton_storage<T, Annotations...>::ss_scope = scope::none;
        return true;
    }
};

template<typename T>
struct bind_multiple : multiple_storage<T> {
    bind_multiple() noexcept = default;

    template<typename I>
    bind_multiple& add() noexcept
    {
        multiple_storage<T>::get_factories()[typeid(I).name()]
            = details::create_factory<I>();

        return *this;
    }

    template<typename I, typename... Annotations>
    bind_multiple& add_singleton() noexcept
    {
        auto factory = details::create_factory<I>();
        auto single = factory();

        if (sizeof...(Annotations) > 0) {
            bind<T, Annotations...>::to_instance(single.get());
        }
        bind<I, Annotations...>::to_instance(single.get());
        multiple_storage<T>::get_factories()[typeid(I).name()]
            = [single]() { return single; };

        return *this;
    }
};

}  // namespace injector

#endif
