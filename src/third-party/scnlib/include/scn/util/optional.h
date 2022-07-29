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

#ifndef SCN_UTIL_OPTIONAL_H
#define SCN_UTIL_OPTIONAL_H

#include "memory.h"

namespace scn {
    SCN_BEGIN_NAMESPACE

    struct nullopt_t {
    };
    namespace {
        static constexpr auto& nullopt = detail::static_const<nullopt_t>::value;
    }

    /**
     * A very lackluster optional implementation.
     * Useful when scanning non-default-constructible types, especially with
     * <tuple_return.h>:
     *
     * \code{.cpp}
     * // implement scn::scanner for optional<mytype>
     * optional<mytype> val;
     * scn::scan(source, "{}", val);
     *
     * // with tuple_return:
     * auto [result, val] = scn::scan_tuple<optional<mytype>>(source, "{}");
     * \endcode
     */
    template <typename T>
    class optional {
    public:
        using value_type = T;
        using storage_type = detail::erased_storage<T>;

        optional() = default;
        optional(nullopt_t) : m_storage{} {}

        optional(value_type val) : m_storage(SCN_MOVE(val)) {}
        optional& operator=(value_type val)
        {
            m_storage = storage_type(SCN_MOVE(val));
            return *this;
        }

        SCN_NODISCARD constexpr bool has_value() const noexcept
        {
            return m_storage.operator bool();
        }
        constexpr explicit operator bool() const noexcept
        {
            return has_value();
        }

        SCN_CONSTEXPR14 T& get() noexcept
        {
            return m_storage.get();
        }
        SCN_CONSTEXPR14 const T& get() const noexcept
        {
            return m_storage.get();
        }

        SCN_CONSTEXPR14 T& operator*() noexcept
        {
            return get();
        }
        SCN_CONSTEXPR14 const T& operator*() const noexcept
        {
            return get();
        }

        SCN_CONSTEXPR14 T* operator->() noexcept
        {
            return m_storage.operator->();
        }
        SCN_CONSTEXPR14 const T* operator->() const noexcept
        {
            return m_storage.operator->();
        }

    private:
        storage_type m_storage;
    };

    SCN_END_NAMESPACE
}  // namespace scn

#endif
