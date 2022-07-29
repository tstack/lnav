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

#ifndef SCN_UTIL_EXPECTED_H
#define SCN_UTIL_EXPECTED_H

#include "memory.h"

namespace scn {
    SCN_BEGIN_NAMESPACE

    /**
     * expected-like type.
     * For situations where there can be a value in case of success or an error
     * code.
     */
    template <typename T, typename Error, typename Enable>
    class expected;

    /**
     * expected-like type for default-constructible success values.
     * Not optimized for space-efficiency (both members are stored
     * simultaneously).
     * `error` is used as the error value and discriminant flag.
     */
    template <typename T, typename Error>
    class expected<T,
                   Error,
                   typename std::enable_if<
                       std::is_default_constructible<T>::value>::type> {
    public:
        using success_type = T;
        using error_type = Error;

        constexpr expected() = default;
        constexpr expected(success_type s) : m_s(s) {}
        constexpr expected(error_type e) : m_e(e) {}

        SCN_NODISCARD constexpr bool has_value() const noexcept
        {
            return m_e == Error{};
        }
        constexpr explicit operator bool() const noexcept
        {
            return has_value();
        }
        constexpr bool operator!() const noexcept
        {
            return !operator bool();
        }

        SCN_CONSTEXPR14 success_type& value() & noexcept
        {
            return m_s;
        }
        constexpr success_type value() const& noexcept
        {
            return m_s;
        }
        SCN_CONSTEXPR14 success_type value() && noexcept
        {
            return SCN_MOVE(m_s);
        }

        SCN_CONSTEXPR14 error_type& error() noexcept
        {
            return m_e;
        }
        constexpr error_type error() const noexcept
        {
            return m_e;
        }

    private:
        success_type m_s{};
        error_type m_e{error_type::success_tag()};
    };

    /**
     * expected-like type for non-default-constructible success values.
     * Not optimized for space-efficiency.
     * `error` is used as the error value and discriminant flag.
     */
    template <typename T, typename Error>
    class expected<T,
                   Error,
                   typename std::enable_if<
                       !std::is_default_constructible<T>::value>::type> {
    public:
        using success_type = T;
        using success_storage = detail::erased_storage<T>;
        using error_type = Error;

        expected(success_type s) : m_s(SCN_MOVE(s)) {}
        constexpr expected(error_type e) : m_e(e) {}

        SCN_NODISCARD constexpr bool has_value() const noexcept
        {
            return m_e == Error{};
        }
        constexpr explicit operator bool() const noexcept
        {
            return has_value();
        }
        constexpr bool operator!() const noexcept
        {
            return !operator bool();
        }

        SCN_CONSTEXPR14 success_type& value() noexcept
        {
            return *m_s;
        }
        constexpr const success_type& value() const noexcept
        {
            return *m_s;
        }

        SCN_CONSTEXPR14 error_type& error() noexcept
        {
            return m_e;
        }
        constexpr error_type error() const noexcept
        {
            return m_e;
        }

    private:
        success_storage m_s{};
        error_type m_e{error_type::success_tag()};
    };

    template <typename T,
              typename U = typename std::remove_cv<
                  typename std::remove_reference<T>::type>::type>
    expected<U> make_expected(T&& val)
    {
        return expected<U>(std::forward<T>(val));
    }

    SCN_END_NAMESPACE
}  // namespace scn

#endif
