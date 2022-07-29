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

#ifndef SCN_UTIL_ARRAY_H
#define SCN_UTIL_ARRAY_H

#include "../detail/fwd.h"

#include <cstdint>

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace detail {
        /**
         * Implementation of `std::array` without including `<array>` (can be
         * heavy-ish)
         */
        template <typename T, std::size_t N>
        struct array {
            static_assert(N > 0, "zero-sized array not supported");

            using value_type = T;
            using size_type = std::size_t;
            using difference_type = std::ptrdiff_t;
            using reference = T&;
            using const_reference = const T&;
            using pointer = T*;
            using const_pointer = const T*;
            using iterator = pointer;
            using const_iterator = const_pointer;

            SCN_CONSTEXPR14 reference operator[](size_type i)
            {
                SCN_EXPECT(i < size());
                return m_data[i];
            }
            SCN_CONSTEXPR14 const_reference operator[](size_type i) const
            {
                SCN_EXPECT(i < size());
                return m_data[i];
            }

            SCN_CONSTEXPR14 iterator begin() noexcept
            {
                return m_data;
            }
            constexpr const_iterator begin() const noexcept
            {
                return m_data;
            }
            constexpr const_iterator cbegin() const noexcept
            {
                return m_data;
            }

            SCN_CONSTEXPR14 iterator end() noexcept
            {
                return m_data + N;
            }
            constexpr const_iterator end() const noexcept
            {
                return m_data + N;
            }
            constexpr const_iterator cend() const noexcept
            {
                return m_data + N;
            }

            SCN_CONSTEXPR14 pointer data() noexcept
            {
                return m_data;
            }
            constexpr const_pointer data() const noexcept
            {
                return m_data;
            }

            SCN_NODISCARD constexpr size_type size() const noexcept
            {
                return N;
            }

            T m_data[N];
        };
    }  // namespace detail

    SCN_END_NAMESPACE
}  // namespace scn

#endif
