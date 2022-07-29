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

#ifndef SCN_UTIL_MEMORY_H
#define SCN_UTIL_MEMORY_H

#include "meta.h"

#include <cstring>
#include <new>

SCN_GCC_PUSH
SCN_GCC_IGNORE("-Wnoexcept")
#include <iterator>
SCN_GCC_POP

#if SCN_MSVC && SCN_HAS_STRING_VIEW
#include <string_view>
#endif

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace detail {
        template <typename T>
        struct pointer_traits;

        template <typename T>
        struct pointer_traits<T*> {
            using pointer = T*;
            using element_type = T;
            using difference_type = std::ptrdiff_t;

            template <typename U>
            using rebind = U*;

            template <typename U = T,
                      typename std::enable_if<!std::is_void<U>::value>::type* =
                          nullptr>
            static constexpr pointer pointer_to(U& r) noexcept
            {
                return &r;
            }
        };

        template <typename T>
        constexpr T* to_address_impl(T* p, priority_tag<2>) noexcept
        {
            return p;
        }
        template <typename Ptr>
        SCN_CONSTEXPR14 auto to_address_impl(const Ptr& p,
                                             priority_tag<1>) noexcept
            -> decltype(::scn::detail::pointer_traits<Ptr>::to_address(p))
        {
            return ::scn::detail::pointer_traits<Ptr>::to_address(p);
        }
        template <typename Ptr>
        constexpr auto to_address_impl(const Ptr& p, priority_tag<0>) noexcept
            -> decltype(::scn::detail::to_address_impl(p.operator->(),
                                                       priority_tag<2>{}))
        {
            return ::scn::detail::to_address_impl(p.operator->(),
                                                  priority_tag<2>{});
        }

        template <typename Ptr>
        constexpr auto to_address(Ptr&& p) noexcept
            -> decltype(::scn::detail::to_address_impl(SCN_FWD(p),
                                                       priority_tag<2>{}))
        {
            return ::scn::detail::to_address_impl(SCN_FWD(p),
                                                  priority_tag<2>{});
        }

#if SCN_WINDOWS
        template <typename I, typename B, typename E>
        SCN_CONSTEXPR14 auto to_address_safe(I&& p, B begin, E end) noexcept
            -> decltype(to_address(SCN_FWD(p)))
        {
            if (p >= begin && p < end) {
                return to_address(SCN_FWD(p));
            }
            if (begin == end) {
                return to_address(SCN_FWD(p));
            }
            if (p == end) {
                return to_address(SCN_FWD(p) - 1) + 1;
            }
            SCN_ENSURE(false);
            SCN_UNREACHABLE;
        }
#else
        template <typename I, typename B, typename E>
        SCN_CONSTEXPR14 auto to_address_safe(I&& p, B, E) noexcept
            -> decltype(to_address(SCN_FWD(p)))
        {
            return to_address(SCN_FWD(p));
        }
#endif

        // Workaround for MSVC _String_view_iterator
#if SCN_MSVC && SCN_HAS_STRING_VIEW
        template <typename Traits>
        struct pointer_traits<std::_String_view_iterator<Traits>> {
            using iterator = std::_String_view_iterator<Traits>;
            using pointer = typename iterator::pointer;
            using element_type = typename iterator::value_type;
            using difference_type = typename iterator::difference_type;

            static constexpr pointer to_address(const iterator& it) noexcept
            {
                // operator-> of _String_view_iterator
                // is checked for past-the-end dereference,
                // even though operator-> isn't dereferencing anything :)))
                return it._Unwrapped();
            }
        };
#endif

        template <typename T>
        constexpr T* launder(T* p) noexcept
        {
#if SCN_HAS_LAUNDER
            return std::launder(p);
#else
            return p;
#endif
        }

        template <typename ForwardIt, typename T>
        void uninitialized_fill(ForwardIt first,
                                ForwardIt last,
                                const T& value,
                                std::true_type) noexcept
        {
            using value_type =
                typename std::iterator_traits<ForwardIt>::value_type;
            const auto dist = static_cast<size_t>(std::distance(first, last)) *
                              sizeof(value_type);
            std::memset(&*first, static_cast<unsigned char>(value), dist);
        }
        template <typename ForwardIt, typename T>
        void uninitialized_fill(ForwardIt first,
                                ForwardIt last,
                                const T& value,
                                std::false_type) noexcept
        {
            using value_type =
                typename std::iterator_traits<ForwardIt>::value_type;
            ForwardIt current = first;
            for (; current != last; ++current) {
                ::new (static_cast<void*>(std::addressof(*current)))
                    value_type(value);
            }
        }
        template <typename ForwardIt, typename T>
        void uninitialized_fill(ForwardIt first,
                                ForwardIt last,
                                const T& value) noexcept
        {
            constexpr bool B = std::is_trivially_copyable<T>::value &&
                               std::is_pointer<ForwardIt>::value &&
                               sizeof(T) == 1;
            return uninitialized_fill(first, last, value,
                                      std::integral_constant<bool, B>{});
        }

        template <typename ForwardIt>
        void uninitialized_fill_default_construct(ForwardIt first,
                                                  ForwardIt last) noexcept
        {
            using value_type =
                typename std::iterator_traits<ForwardIt>::value_type;
            ForwardIt current = first;
            for (; current != last; ++current) {
                ::new (static_cast<void*>(std::addressof(*current))) value_type;
            }
        }
        template <typename ForwardIt>
        void uninitialized_fill_value_init(ForwardIt first,
                                           ForwardIt last) noexcept
        {
            using value_type =
                typename std::iterator_traits<ForwardIt>::value_type;
            ForwardIt current = first;
            for (; current != last; ++current) {
                ::new (static_cast<void*>(std::addressof(*current)))
                    value_type();
            }
        }

        template <typename InputIt,
                  typename ForwardIt,
                  typename std::enable_if<
                      !std::is_trivially_copyable<typename std::iterator_traits<
                          ForwardIt>::value_type>::value>::type* = nullptr>
        ForwardIt uninitialized_copy(InputIt first,
                                     InputIt last,
                                     ForwardIt d_first) noexcept
        {
            using value_type =
                typename std::iterator_traits<ForwardIt>::value_type;
            ForwardIt current = d_first;
            for (; first != last; ++first, (void)++current) {
                ::new (static_cast<void*>(std::addressof(*current)))
                    value_type(*first);
            }
            return current;
        }
        template <typename InputIt,
                  typename ForwardIt,
                  typename std::enable_if<
                      std::is_trivially_copyable<typename std::iterator_traits<
                          ForwardIt>::value_type>::value>::type* = nullptr>
        ForwardIt uninitialized_copy(InputIt first,
                                     InputIt last,
                                     ForwardIt d_first) noexcept
        {
            using value_type =
                typename std::iterator_traits<ForwardIt>::value_type;
            using pointer = typename std::iterator_traits<ForwardIt>::pointer;
            auto ptr =
                std::memcpy(std::addressof(*d_first), std::addressof(*first),
                            static_cast<size_t>(std::distance(first, last)) *
                                sizeof(value_type));
            return ForwardIt{static_cast<pointer>(ptr)};
        }

        template <typename InputIt,
                  typename ForwardIt,
                  typename std::enable_if<
                      !std::is_trivially_copyable<typename std::iterator_traits<
                          ForwardIt>::value_type>::value>::type* = nullptr>
        ForwardIt uninitialized_move(InputIt first,
                                     InputIt last,
                                     ForwardIt d_first) noexcept
        {
            using value_type =
                typename std::iterator_traits<ForwardIt>::value_type;
            ForwardIt current = d_first;
            for (; first != last; ++first, (void)++current) {
                ::new (static_cast<void*>(std::addressof(*current)))
                    value_type(std::move(*first));
            }
            return current;
        }
        template <typename InputIt,
                  typename ForwardIt,
                  typename std::enable_if<
                      std::is_trivially_copyable<typename std::iterator_traits<
                          ForwardIt>::value_type>::value>::type* = nullptr>
        ForwardIt uninitialized_move(InputIt first,
                                     InputIt last,
                                     ForwardIt d_first) noexcept
        {
            using value_type =
                typename std::iterator_traits<ForwardIt>::value_type;
            using pointer = typename std::iterator_traits<ForwardIt>::pointer;
            auto ptr =
                std::memcpy(std::addressof(*d_first), std::addressof(*first),
                            static_cast<size_t>(std::distance(first, last)) *
                                sizeof(value_type));
            return ForwardIt(static_cast<pointer>(ptr));
        }

        template <typename T>
        class SCN_TRIVIAL_ABI erased_storage {
        public:
            using value_type = T;
            using pointer = T*;
            using storage_type = unsigned char[sizeof(T)];

            constexpr erased_storage() noexcept = default;

            erased_storage(T val) noexcept(
                std::is_nothrow_move_constructible<T>::value)
                : m_ptr(::new (static_cast<void*>(&m_data)) T(SCN_MOVE(val)))
            {
            }

            erased_storage(const erased_storage& other)
                : m_ptr(other ? ::new (static_cast<void*>(&m_data))
                                    T(other.get())
                              : nullptr)
            {
            }
            erased_storage& operator=(const erased_storage& other)
            {
                _destruct();
                if (other) {
                    m_ptr = ::new (static_cast<void*>(&m_data)) T(other.get());
                }
                return *this;
            }

            erased_storage(erased_storage&& other) noexcept
                : m_ptr(other ? ::new (static_cast<void*>(&m_data))
                                    T(SCN_MOVE(other.get()))
                              : nullptr)
            {
                other.m_ptr = nullptr;
            }
            erased_storage& operator=(erased_storage&& other) noexcept
            {
                _destruct();
                if (other) {
                    m_ptr = ::new (static_cast<void*>(&m_data))
                        T(SCN_MOVE(other.get()));
                    other.m_ptr = nullptr;
                }
                return *this;
            }

            ~erased_storage() noexcept
            {
                _destruct();
            }

            SCN_NODISCARD constexpr bool has_value() const noexcept
            {
                return m_ptr != nullptr;
            }
            constexpr explicit operator bool() const noexcept
            {
                return has_value();
            }

            SCN_CONSTEXPR14 T& get() noexcept
            {
                SCN_EXPECT(has_value());
                return _get();
            }
            SCN_CONSTEXPR14 const T& get() const noexcept
            {
                SCN_EXPECT(has_value());
                return _get();
            }

            SCN_CONSTEXPR14 T& operator*() noexcept
            {
                SCN_EXPECT(has_value());
                return _get();
            }
            SCN_CONSTEXPR14 const T& operator*() const noexcept
            {
                SCN_EXPECT(has_value());
                return _get();
            }

            SCN_CONSTEXPR14 T* operator->() noexcept
            {
                return m_ptr;
            }
            SCN_CONSTEXPR14 const T* operator->() const noexcept
            {
                return m_ptr;
            }

        private:
            void _destruct()
            {
                if (m_ptr) {
                    _get().~T();
                }
                m_ptr = nullptr;
            }
            static pointer _toptr(storage_type& data)
            {
                return ::scn::detail::launder(
                    reinterpret_cast<T*>(reinterpret_cast<void*>(data.data())));
            }
            SCN_CONSTEXPR14 T& _get() noexcept
            {
                return *m_ptr;
            }
            SCN_CONSTEXPR14 const T& _get() const noexcept
            {
                return *m_ptr;
            }

            alignas(T) storage_type m_data{};
            pointer m_ptr{nullptr};
        };
    }  // namespace detail

    SCN_END_NAMESPACE
}  // namespace scn

#endif
