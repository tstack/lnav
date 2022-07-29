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

#ifndef SCN_UTIL_SPAN_H
#define SCN_UTIL_SPAN_H

#include "memory.h"

SCN_GCC_PUSH
SCN_GCC_IGNORE("-Wnoexcept")
#include <iterator>
SCN_GCC_POP

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace custom_ranges {
        // iterator_category
        using std::bidirectional_iterator_tag;
        using std::forward_iterator_tag;
        using std::input_iterator_tag;
        using std::output_iterator_tag;
        using std::random_access_iterator_tag;
        struct contiguous_iterator_tag : random_access_iterator_tag {
        };
    }  // namespace custom_ranges

    /**
     * A view over a contiguous range.
     * Stripped-down version of `std::span`.
     */
    template <typename T>
    class span {
    public:
        using element_type = T;
        using value_type = typename std::remove_cv<T>::type;
        using index_type = std::size_t;
        using ssize_type = std::ptrdiff_t;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using const_pointer = const T*;
        using reference = T&;
        using const_reference = const T&;

        using iterator = pointer;
        using const_iterator = const_pointer;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        constexpr span() noexcept = default;

        template <typename I,
                  typename = decltype(detail::to_address(SCN_DECLVAL(I)))>
        SCN_CONSTEXPR14 span(I begin, index_type count) noexcept
            : m_ptr(detail::to_address(begin)),
              m_end(detail::to_address(begin) + count)
        {
        }

        template <typename I,
                  typename S,
                  typename = decltype(detail::to_address(SCN_DECLVAL(I)),
                                      detail::to_address(SCN_DECLVAL(S)))>
        SCN_CONSTEXPR14 span(I first, S last) noexcept
            : m_ptr(detail::to_address(first)),
              m_end(detail::to_address_safe(last, first, last))
        {
        }

        template <typename U = typename std::add_const<T>::type,
                  typename E = element_type,
                  typename = typename std::enable_if<
                      std::is_same<E, value_type>::value>::type>
        constexpr span(span<U> other) : m_ptr(other.m_ptr), m_end(other.m_end)
        {
        }

        template <size_t N>
        constexpr span(element_type (&arr)[N]) noexcept
            : m_ptr(&arr), m_end(&arr + N)
        {
        }

        SCN_CONSTEXPR14 iterator begin() noexcept
        {
            return m_ptr;
        }
        SCN_CONSTEXPR14 iterator end() noexcept
        {
            return m_end;
        }
        SCN_CONSTEXPR14 reverse_iterator rbegin() noexcept
        {
            return reverse_iterator{end()};
        }
        SCN_CONSTEXPR14 reverse_iterator rend() noexcept
        {
            return reverse_iterator{begin()};
        }

        constexpr const_iterator begin() const noexcept
        {
            return m_ptr;
        }
        constexpr const_iterator end() const noexcept
        {
            return m_end;
        }
        constexpr const_reverse_iterator rbegin() const noexcept
        {
            return reverse_iterator{end()};
        }
        constexpr const_reverse_iterator rend() const noexcept
        {
            return reverse_iterator{begin()};
        }

        constexpr const_iterator cbegin() const noexcept
        {
            return m_ptr;
        }
        constexpr const_iterator cend() const noexcept
        {
            return m_end;
        }
        constexpr const_reverse_iterator crbegin() const noexcept
        {
            return reverse_iterator{cend()};
        }
        constexpr const_reverse_iterator crend() const noexcept
        {
            return reverse_iterator{cbegin()};
        }

        SCN_CONSTEXPR14 reference operator[](index_type i) const noexcept
        {
            SCN_EXPECT(size() > i);
            return *(m_ptr + i);
        }

        constexpr pointer data() const noexcept
        {
            return m_ptr;
        }
        SCN_NODISCARD constexpr index_type size() const noexcept
        {
            return static_cast<index_type>(m_end - m_ptr);
        }
        SCN_NODISCARD constexpr ssize_type ssize() const noexcept
        {
            return m_end - m_ptr;
        }

        SCN_CONSTEXPR14 span<T> first(index_type n) const
        {
            SCN_EXPECT(size() >= n);
            return span<T>(data(), data() + n);
        }
        SCN_CONSTEXPR14 span<T> last(index_type n) const
        {
            SCN_EXPECT(size() >= n);
            return span<T>(data() + size() - n, data() + size());
        }
        SCN_CONSTEXPR14 span<T> subspan(index_type off) const
        {
            SCN_EXPECT(size() >= off);
            return span<T>(data() + off, size() - off);
        }
        SCN_CONSTEXPR14 span<T> subspan(index_type off,
                                        difference_type count) const
        {
            SCN_EXPECT(size() > off + count);
            SCN_EXPECT(count > 0);
            return span<T>(data() + off, count);
        }

        constexpr operator span<typename std::add_const<T>::type>() const
        {
            return {m_ptr, m_end};
        }
        constexpr span<typename std::add_const<T>::type> as_const() const
        {
            return {m_ptr, m_end};
        }

    private:
        pointer m_ptr{nullptr};
        pointer m_end{nullptr};
    };

    template <typename I,
              typename S,
              typename Ptr = decltype(detail::to_address(SCN_DECLVAL(I))),
              typename SPtr = decltype(detail::to_address(SCN_DECLVAL(S))),
              typename ValueT = typename detail::remove_reference<
                  typename std::remove_pointer<Ptr>::type>::type>
    SCN_CONSTEXPR14 auto make_span(I first, S last) noexcept -> span<ValueT>
    {
        return {first, last};
    }
    template <typename I,
              typename Ptr = decltype(detail::to_address(SCN_DECLVAL(I))),
              typename ValueT = typename detail::remove_reference<
                  typename std::remove_pointer<Ptr>::type>::type>
    SCN_CONSTEXPR14 auto make_span(I first, std::size_t len) noexcept
        -> span<ValueT>
    {
        return {first, len};
    }

    template <typename T>
    SCN_CONSTEXPR14 span<typename T::value_type> make_span(
        T& container) noexcept
    {
        using std::begin;
        using std::end;
        return span<typename T::value_type>(
            detail::to_address(begin(container)),
            detail::to_address_safe(end(container), begin(container),
                                    end(container)));
    }

    SCN_END_NAMESPACE
}  // namespace scn

#endif  // SCN_SPAN_H
