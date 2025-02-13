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
 * @file injector.hh
 */

#ifndef lnav_injector_hh
#define lnav_injector_hh

#include <functional>
#include <map>
#include <memory>
#include <type_traits>
#include <vector>

#include <assert.h>

#include "base/lnav_log.hh"

namespace injector {

enum class scope {
    undefined,
    none,
    singleton,
};

template<typename Annotation>
void force_linking(Annotation anno);

template<class...>
using void_t = void;

template<typename T, typename... Annotations>
struct with_annotations {
    T value;
};

template<class, class = void>
struct has_injectable : std::false_type {};

template<class T>
struct has_injectable<T, void_t<typename T::injectable>> : std::true_type {};

template<typename T, typename... Annotations>
struct singleton_storage {
    static scope get_scope() { return ss_scope; }

    static T* get()
    {
        static int _[] = {0, (force_linking(Annotations{}), 0)...};
        (void) _;
        return ss_data;
    }

    static std::shared_ptr<T> get_owner()
    {
        static int _[] = {0, (force_linking(Annotations{}), 0)...};
        (void) _;
        return ss_owner;
    }

    static std::shared_ptr<T> create()
    {
        static int _[] = {0, (force_linking(Annotations{}), 0)...};
        (void) _;
        return ss_factory();
    }

protected:
    static scope ss_scope;
    static T* ss_data;
    static std::shared_ptr<T> ss_owner;
    static std::function<std::shared_ptr<T>()> ss_factory;
};

template<typename T, typename... Annotations>
T* singleton_storage<T, Annotations...>::ss_data = nullptr;

template<typename T, typename... Annotations>
scope singleton_storage<T, Annotations...>::ss_scope = scope::undefined;

template<typename T, typename... Annotations>
std::shared_ptr<T> singleton_storage<T, Annotations...>::ss_owner;

template<typename T, typename... Annotations>
std::function<std::shared_ptr<T>()>
    singleton_storage<T, Annotations...>::ss_factory;

template<typename T>
struct Impl {
    using type = T;
};

template<typename T>
struct multiple_storage {
    static std::vector<std::shared_ptr<T>> create()
    {
        std::vector<std::shared_ptr<T>> retval;

        for (const auto& pair : get_factories()) {
            retval.emplace_back(pair.second());
        }
        return retval;
    }

protected:
    using factory_map_t
        = std::map<std::string, std::function<std::shared_ptr<T>()>>;

    static factory_map_t& get_factories()
    {
        static factory_map_t retval;

        return retval;
    }
};

template<typename T,
         typename... Annotations,
         std::enable_if_t<std::is_reference<T>::value, bool> = true>
T
get()
{
    using plain_t = std::remove_const_t<std::remove_reference_t<T>>;

    return *singleton_storage<plain_t, Annotations...>::get();
}

template<typename T,
         typename... Annotations,
         std::enable_if_t<std::is_pointer<T>::value, bool> = true>
T
get()
{
    using plain_t = std::remove_const_t<std::remove_pointer_t<T>>;

    return singleton_storage<plain_t, Annotations...>::get();
}

template<class T>
struct is_shared_ptr : std::false_type {};

template<class T>
struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {};

template<class T>
struct is_vector : std::false_type {};

template<class T>
struct is_vector<std::vector<T>> : std::true_type {};

template<typename I, typename R, typename... IArgs, typename... Args>
std::function<std::shared_ptr<I>()> create_from_injectable(R (*)(IArgs...),
                                                           Args&... args);

template<typename T,
         typename... Args,
         std::enable_if_t<has_injectable<typename T::element_type>::value, bool>
         = true,
         std::enable_if_t<is_shared_ptr<T>::value, bool> = true>
T
get(Args&... args)
{
    typename T::element_type::injectable* i = nullptr;

    if (singleton_storage<typename T::element_type>::get_scope()
        == scope::singleton)
    {
        return singleton_storage<typename T::element_type>::get_owner();
    }
    return create_from_injectable<typename T::element_type>(i, args...)();
}

template<
    typename T,
    typename... Annotations,
    std::enable_if_t<!has_injectable<typename T::element_type>::value, bool>
    = true,
    std::enable_if_t<is_shared_ptr<T>::value, bool> = true>
T
get()
{
    if (singleton_storage<typename T::element_type>::get_scope()
        == scope::singleton)
    {
        return singleton_storage<typename T::element_type>::get_owner();
    }
    return std::make_shared<typename T::element_type>();
}

template<typename T, std::enable_if_t<is_vector<T>::value, bool> = true>
T
get()
{
    return multiple_storage<typename T::value_type::element_type>::create();
}

template<typename I, typename R, typename... IArgs, typename... Args>
std::function<std::shared_ptr<I>()>
create_from_injectable(R (*)(IArgs...), Args&... args)
{
    return [&]() {
        return std::make_shared<I>(args..., ::injector::get<IArgs>()...);
    };
}

}  // namespace injector

#endif
