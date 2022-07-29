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

#ifndef SCN_UTIL_STRING_VIEW_H
#define SCN_UTIL_STRING_VIEW_H

#include "algorithm.h"
#include "span.h"

#include <cstdint>
#include <cstring>
#include <cwchar>

#if SCN_HAS_STRING_VIEW
#include <string_view>
#endif

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace detail {
        inline size_t strlen(const char* s) noexcept
        {
            return ::std::strlen(s);
        }
        inline size_t strlen(const wchar_t* s) noexcept
        {
            return ::std::wcslen(s);
        }
        inline size_t strlen(const char16_t* s) noexcept
        {
            SCN_EXPECT(s);
            auto end = s;
            for (; *end != u'\0'; ++end)
                ;
            return static_cast<size_t>(end - s);
        }
        inline size_t strlen(const char32_t* s) noexcept
        {
            SCN_EXPECT(s);
            auto end = s;
            for (; *end != U'\0'; ++end)
                ;
            return static_cast<size_t>(end - s);
        }
#if SCN_HAS_CHAR8
        inline size_t strlen(const char8_t* s) noexcept
        {
            return std::strlen(reinterpret_cast<const char*>(s));
        }
#endif
    }  // namespace detail

    /**
     * A view over a (sub)string.
     * Used even when std::string_view is available to avoid compatibility
     * issues.
     */
    template <typename CharT>
    class basic_string_view {
    public:
        using value_type = CharT;
        using span_type = span<const value_type>;

        using pointer = value_type*;
        using const_pointer = const value_type*;
        using reference = value_type&;
        using const_reference = const value_type&;
        using iterator = typename span_type::const_iterator;
        using const_iterator = iterator;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = reverse_iterator;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using span_index_type = typename span_type::index_type;

        static constexpr const size_type npos = size_type(-1);

        constexpr basic_string_view() noexcept = default;
        constexpr basic_string_view(const_pointer s, size_type c)
            : m_data(s, static_cast<span_index_type>(c))
        {
        }
        constexpr basic_string_view(const_pointer s)
            : m_data(s, static_cast<span_index_type>(detail::strlen(s)))
        {
        }
        template <size_t N>
        constexpr basic_string_view(const CharT (&s)[N]) : m_data(s, N)
        {
        }
        constexpr basic_string_view(const_pointer first, const_pointer last)
            : m_data(first, last)
        {
        }
#if SCN_HAS_STRING_VIEW
        constexpr basic_string_view(std::basic_string_view<value_type> str)
            : m_data(str.data(), str.size())
        {
        }
#endif

        template <typename T = char16_t,
                  typename std::enable_if<std::is_same<T, char16_t>::value &&
                                          std::is_same<CharT, wchar_t>::value &&
                                          sizeof(char16_t) ==
                                              sizeof(wchar_t)>::type* = nullptr>
        constexpr basic_string_view(const T* s)
            : basic_string_view(reinterpret_cast<const wchar_t*>(s))
        {
        }
        template <typename T = char32_t,
                  typename std::enable_if<std::is_same<T, char32_t>::value &&
                                          std::is_same<CharT, wchar_t>::value &&
                                          sizeof(char32_t) ==
                                              sizeof(wchar_t)>::type* = nullptr>
        constexpr basic_string_view(const T* s)
            : basic_string_view(reinterpret_cast<const wchar_t*>(s))
        {
        }
#if SCN_HAS_CHAR8
        template <typename T = char8_t,
                  typename std::enable_if<
                      std::is_same<T, char8_t>::value &&
                      std::is_same<CharT, char>::value>::type* = nullptr>
        constexpr basic_string_view(const T* s)
            : basic_string_view(reinterpret_cast<const char*>(s))
        {
        }
#endif

        constexpr const_iterator begin() const noexcept
        {
            return cbegin();
        }
        constexpr const_iterator cbegin() const noexcept
        {
            return m_data.cbegin();
        }
        constexpr const_iterator end() const noexcept
        {
            return cend();
        }
        constexpr const_iterator cend() const noexcept
        {
            return m_data.cend();
        }

        constexpr const_reverse_iterator rbegin() const noexcept
        {
            return crbegin();
        }
        constexpr const_reverse_iterator crbegin() const noexcept
        {
            return m_data.crbegin();
        }
        constexpr const_reverse_iterator rend() const noexcept
        {
            return crend();
        }
        constexpr const_reverse_iterator crend() const noexcept
        {
            return m_data.crend();
        }

        constexpr const_reference operator[](size_type pos) const
        {
            return m_data[static_cast<typename span_type::index_type>(pos)];
        }
        SCN_CONSTEXPR14 const_reference at(size_type pos) const
        {
            SCN_EXPECT(pos < size());
            return m_data.at(static_cast<typename span_type::index_type>(pos));
        }

        constexpr const_reference front() const
        {
            return operator[](0);
        }
        constexpr const_reference back() const
        {
            return operator[](size() - 1);
        }
        constexpr const_pointer data() const noexcept
        {
            return m_data.data();
        }

        SCN_NODISCARD constexpr size_type size() const noexcept
        {
            return static_cast<size_type>(m_data.size());
        }
        SCN_NODISCARD constexpr size_type length() const noexcept
        {
            return size();
        }
        SCN_NODISCARD constexpr size_type max_size() const noexcept
        {
            return SIZE_MAX - 1;
        }
        SCN_NODISCARD constexpr bool empty() const noexcept
        {
            return size() == 0;
        }

        SCN_CONSTEXPR14 void remove_prefix(size_type n)
        {
            SCN_EXPECT(n <= size());
            m_data = m_data.subspan(n);
        }
        SCN_CONSTEXPR14 void remove_suffix(size_type n)
        {
            SCN_EXPECT(n <= size());
            m_data = m_data.first(size() - n);
        }

        SCN_CONSTEXPR14 void swap(basic_string_view& v) noexcept
        {
            using std::swap;
            swap(m_data, v.m_data);
        }

        size_type copy(pointer dest, size_type count, size_type pos = 0) const
        {
            SCN_EXPECT(pos <= size());
            auto n = detail::min(count, size() - pos);
            /* std::copy(data() + pos, n, dest); */
            std::memcpy(dest, begin() + pos, n * sizeof(value_type));
            return n;
        }
        SCN_CONSTEXPR14 basic_string_view substr(size_type pos = 0,
                                                 size_type count = npos) const
        {
            SCN_EXPECT(pos <= size());
            auto n = detail::min(count, size() - pos);
            return m_data.subspan(pos, n);
        }

#if SCN_HAS_STRING_VIEW
        operator std::basic_string_view<value_type>() const noexcept
        {
            return {m_data.data(), m_data.size()};
        }
#endif

    private:
        span_type m_data{};
    };

    using string_view = basic_string_view<char>;
    using wstring_view = basic_string_view<wchar_t>;

    SCN_END_NAMESPACE
}  // namespace scn

#endif  // SCN_STRING_VIEW_H
