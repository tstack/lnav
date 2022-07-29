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

#ifndef SCN_UTIL_UNIQUE_PTR_H
#define SCN_UTIL_UNIQUE_PTR_H

#include "../detail/fwd.h"

#include <type_traits>

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace detail {
        /**
         * `std::unique_ptr` implementation with [[clang::trivial_abi]], without
         * including `<memory>`
         */
        template <typename T>
        class SCN_TRIVIAL_ABI unique_ptr {
        public:
            using element_type = T;
            using pointer = T*;

            constexpr unique_ptr() noexcept = default;
            constexpr unique_ptr(std::nullptr_t) noexcept {}

            constexpr explicit unique_ptr(pointer p) noexcept : m_ptr(p) {}

            template <
                typename U,
                typename std::enable_if<
                    std::is_convertible<U*, pointer>::value>::type* = nullptr>
            SCN_CONSTEXPR14 unique_ptr(unique_ptr<U>&& u) noexcept
                : m_ptr(SCN_MOVE(u.get()))
            {
                u.reset();
            }

            unique_ptr(const unique_ptr&) = delete;
            unique_ptr& operator=(const unique_ptr&) = delete;

            SCN_CONSTEXPR14 unique_ptr(unique_ptr&& p) noexcept
                : m_ptr(SCN_MOVE(p.m_ptr))
            {
                p.m_ptr = nullptr;
            }
            unique_ptr& operator=(unique_ptr&& p) noexcept
            {
                delete m_ptr;
                m_ptr = p.m_ptr;
                p.m_ptr = nullptr;
                return *this;
            }

            ~unique_ptr() noexcept
            {
                SCN_CLANG_PUSH_IGNORE_UNDEFINED_TEMPLATE
                delete m_ptr;
                SCN_CLANG_POP_IGNORE_UNDEFINED_TEMPLATE
            }

            constexpr explicit operator bool() const noexcept
            {
                return get() != nullptr;
            }

            constexpr pointer get() const noexcept
            {
                return m_ptr;
            }

            constexpr pointer operator->() const noexcept
            {
                return m_ptr;
            }
            constexpr typename std::add_lvalue_reference<T>::type operator*()
                const
            {
                return *m_ptr;
            }

            SCN_CONSTEXPR14 void reset()
            {
                m_ptr = nullptr;
            }

        private:
            pointer m_ptr{nullptr};
        };

        template <typename T, typename... Args>
        unique_ptr<T> make_unique(Args&&... a)
        {
            SCN_CLANG_PUSH_IGNORE_UNDEFINED_TEMPLATE
            return unique_ptr<T>(new T(SCN_FWD(a)...));
            SCN_CLANG_POP_IGNORE_UNDEFINED_TEMPLATE
        }
    }  // namespace detail

    SCN_END_NAMESPACE
}  // namespace scn

#endif
