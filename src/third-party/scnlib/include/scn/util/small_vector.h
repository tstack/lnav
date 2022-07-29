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

#ifndef SCN_UTIL_SMALL_VECTOR_H
#define SCN_UTIL_SMALL_VECTOR_H

#include "math.h"
#include "memory.h"

#include <cstdint>
#include <cstring>

SCN_GCC_PUSH
SCN_GCC_IGNORE("-Wnoexcept")
#include <iterator>
SCN_GCC_POP

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace detail {
        template <typename Iter>
        std::reverse_iterator<Iter> make_reverse_iterator(Iter i)
        {
            return std::reverse_iterator<Iter>(i);
        }

        class small_vector_base {
            static SCN_CONSTEXPR14 uint64_t _next_pow2_64(uint64_t x) noexcept
            {
                --x;
                x |= (x >> 1);
                x |= (x >> 2);
                x |= (x >> 4);
                x |= (x >> 8);
                x |= (x >> 16);
                x |= (x >> 32);
                return x + 1;
            }
            static SCN_CONSTEXPR14 uint32_t _next_pow2_32(uint32_t x) noexcept
            {
                --x;
                x |= (x >> 1);
                x |= (x >> 2);
                x |= (x >> 4);
                x |= (x >> 8);
                x |= (x >> 16);
                return x + 1;
            }

        protected:
            size_t next_pow2(size_t x)
            {
                SCN_MSVC_PUSH
                SCN_MSVC_IGNORE(4127)  // conditional expression is constant
                if (sizeof(size_t) == sizeof(uint64_t)) {
                    return static_cast<size_t>(
                        _next_pow2_64(static_cast<uint64_t>(x)));
                }
                SCN_MSVC_POP
                return static_cast<size_t>(
                    _next_pow2_32(static_cast<uint32_t>(x)));
            }
        };

        SCN_CLANG_PUSH
        SCN_CLANG_IGNORE("-Wpadded")

        template <typename T, size_t N>
        struct basic_stack_storage {
            alignas(T) unsigned char data[N * sizeof(T)];

            T* reinterpret_data()
            {
                return ::scn::detail::launder(reinterpret_unconstructed_data());
            }
            const T* reinterpret_data() const
            {
                return ::scn::detail::launder(reinterpret_unconstructed_data());
            }

            SCN_NODISCARD T* reinterpret_unconstructed_data()
            {
                return static_cast<T*>(static_cast<void*>(data));
            }
            SCN_NODISCARD const T* reinterpret_unconstructed_data() const
            {
                return static_cast<const T*>(static_cast<const void*>(data));
            }

            SCN_NODISCARD SCN_CONSTEXPR14 unsigned char*
            get_unconstructed_data()
            {
                return data;
            }
            SCN_NODISCARD constexpr const unsigned char*
            get_unconstructed_data() const
            {
                return data;
            }
        };

        // -Wpadded
        SCN_CLANG_POP

        template <typename T>
        constexpr T constexpr_max(T val)
        {
            return val;
        }
        template <typename T, typename... Ts>
        constexpr T constexpr_max(T val, Ts... a)
        {
            return val > constexpr_max(a...) ? val : constexpr_max(a...);
        }

        template <typename T>
        struct alignas(constexpr_max(alignof(T),
                                     alignof(T*))) basic_stack_storage<T, 0> {
            T* reinterpret_data()
            {
                return nullptr;
            }
            const T* reinterpret_data() const
            {
                return nullptr;
            }

            T* reinterpret_unconstructed_data()
            {
                return nullptr;
            }
            const T* reinterpret_unconstructed_data() const
            {
                return nullptr;
            }

            unsigned char* get_unconstructed_data()
            {
                return nullptr;
            }
            const unsigned char* get_unconstructed_data() const
            {
                return nullptr;
            }
        };

        SCN_CLANG_PUSH
        SCN_CLANG_IGNORE("-Wpadded")

        /**
         * A contiguous container, that stores its values in the stack, if
         * `size() <= StackN`
         */
        template <typename T, size_t StackN>
        class small_vector : protected small_vector_base {
        public:
            using value_type = T;
            using size_type = size_t;
            using difference_type = std::ptrdiff_t;
            using reference = T&;
            using const_reference = const T&;
            using pointer = T*;
            using const_pointer = const T*;
            using iterator = pointer;
            using const_iterator = const_pointer;
            using reverse_iterator = std::reverse_iterator<pointer>;
            using const_reverse_iterator = std::reverse_iterator<const_pointer>;

            struct stack_storage : basic_stack_storage<T, StackN> {
            };
            struct heap_storage {
                size_type cap{0};
            };

            small_vector() noexcept
                : m_ptr(_construct_stack_storage()
                            .reinterpret_unconstructed_data())
            {
                SCN_MSVC_PUSH
                SCN_MSVC_IGNORE(4127)  // conditional expression is constant

                if (StackN == 0) {
                    _destruct_stack_storage();
                    _construct_heap_storage();
                }

                SCN_MSVC_POP

                SCN_ENSURE(size() == 0);
            }

            explicit small_vector(size_type count, const T& value)
            {
                if (!can_be_small(count)) {
                    auto& heap = _construct_heap_storage();
                    auto cap = next_pow2(count);
                    auto storage_ptr = new unsigned char[count * sizeof(T)];
                    auto ptr =
                        static_cast<pointer>(static_cast<void*>(storage_ptr));
                    uninitialized_fill(ptr, ptr + count, value);

                    heap.cap = cap;
                    m_size = count;
                    m_ptr = ::scn::detail::launder(ptr);
                }
                else {
                    auto& stack = _construct_stack_storage();
                    uninitialized_fill(
                        stack.reinterpret_unconstructed_data(),
                        stack.reinterpret_unconstructed_data() + StackN, value);
                    m_size = count;
                    m_ptr = stack.reinterpret_data();
                }

                SCN_ENSURE(data());
                SCN_ENSURE(size() == count);
                SCN_ENSURE(capacity() >= size());
            }

            explicit small_vector(size_type count)
            {
                if (!can_be_small(count)) {
                    auto& heap = _construct_heap_storage();
                    auto cap = next_pow2(count);
                    auto storage_ptr = new unsigned char[count * sizeof(T)];
                    auto ptr =
                        static_cast<pointer>(static_cast<void*>(storage_ptr));
                    uninitialized_fill_value_init(ptr, ptr + count);
                    heap.cap = cap;
                    m_size = count;
                    m_ptr = ::scn::detail::launder(ptr);
                }
                else {
                    auto& stack = _construct_stack_storage();
                    uninitialized_fill_value_init(
                        stack.reinterpret_unconstructed_data(),
                        stack.reinterpret_unconstructed_data() + count);
                    m_size = count;
                    m_ptr = stack.reinterpret_data();
                }

                SCN_ENSURE(data());
                SCN_ENSURE(size() == count);
                SCN_ENSURE(capacity() >= size());
            }

            small_vector(const small_vector& other)
            {
                if (other.empty()) {
                    auto& stack = _construct_stack_storage();
                    m_ptr = stack.reinterpret_unconstructed_data();
                    return;
                }

                auto s = other.size();
                if (!other.is_small()) {
                    auto& heap = _construct_heap_storage();
                    auto cap = other.capacity();
                    auto optr = other.data();

                    auto storage_ptr = new unsigned char[cap * sizeof(T)];
                    auto ptr =
                        static_cast<pointer>(static_cast<void*>(storage_ptr));
                    uninitialized_copy(optr, optr + s, ptr);

                    m_ptr = ::scn::detail::launder(ptr);
                    m_size = s;
                    heap.cap = cap;
                }
                else {
                    auto& stack = _construct_stack_storage();
                    auto optr = other.data();
                    uninitialized_copy(optr, optr + s,
                                       stack.reinterpret_unconstructed_data());
                    m_size = s;
                    m_ptr = stack.reinterpret_data();
                }

                SCN_ENSURE(data());
                SCN_ENSURE(other.data());
                SCN_ENSURE(other.size() == size());
                SCN_ENSURE(other.capacity() == capacity());
            }
            small_vector(small_vector&& other) noexcept
            {
                if (other.empty()) {
                    auto& stack = _construct_stack_storage();
                    m_ptr = stack.reinterpret_unconstructed_data();
                    return;
                }

                auto s = other.size();
                if (!other.is_small()) {
                    auto& heap = _construct_heap_storage();
                    m_ptr = other.data();

                    m_size = s;
                    heap.cap = other.capacity();
                }
                else {
                    auto& stack = _construct_stack_storage();
                    auto optr = other.data();
                    uninitialized_move(optr, optr + s,
                                       stack.reinterpret_unconstructed_data());

                    m_size = s;
                    other._destruct_elements();
                }
                other.m_ptr = nullptr;

                SCN_ENSURE(data());
            }

            small_vector& operator=(const small_vector& other)
            {
                _destruct_elements();

                if (other.empty()) {
                    return *this;
                }

                SCN_ASSERT(size() == 0, "");

                // this other
                // s s      false || true
                // s h      false || false second
                // h s      true || true
                // h h      true || false
                if (!is_small() || other.is_small()) {
                    uninitialized_copy(other.data(),
                                       other.data() + other.size(), data());
                    m_ptr = ::scn::detail::launder(data());
                    m_size = other.size();
                    if (!other.is_small()) {
                        _get_heap().cap = other.capacity();
                    }
                }
                else {
                    _destruct_stack_storage();
                    auto& heap = _construct_heap_storage();

                    auto cap = next_pow2(other.size());
                    auto storage_ptr = new unsigned char[cap * sizeof(T)];
                    auto ptr =
                        static_cast<pointer>(static_cast<void*>(storage_ptr));
                    uninitialized_copy(other.data(),
                                       other.data() + other.size(), ptr);
                    m_ptr = ::scn::detail::launder(ptr);
                    m_size = other.size();
                    heap.cap = cap;
                }
                return *this;
            }

            small_vector& operator=(small_vector&& other) noexcept
            {
                _destruct_elements();

                if (other.empty()) {
                    return *this;
                }

                SCN_ASSERT(size() == 0, "");

                if (!is_small() && !other.is_small()) {
                    if (!is_small()) {
                        if (capacity() != 0) {
                            delete[] ::scn::detail::launder(
                                static_cast<unsigned char*>(
                                    static_cast<void*>(m_ptr)));
                        }
                    }

                    m_ptr = other.data();
                    m_size = other.size();
                    _get_heap().cap = other.capacity();
                }
                else if (!is_small() || other.is_small()) {
                    uninitialized_move(other.data(),
                                       other.data() + other.size(), data());
                    m_size = other.size();
                    other._destruct_elements();
                }
                else {
                    _destruct_stack_storage();
                    auto& heap = _construct_heap_storage();

                    m_ptr = other.data();
                    m_size = other.size();
                    heap.cap = other.capacity();
                }

                other.m_ptr = nullptr;

                return *this;
            }

            ~small_vector()
            {
                _destruct();
            }

            SCN_NODISCARD SCN_CONSTEXPR14 pointer data() noexcept
            {
                return m_ptr;
            }
            SCN_NODISCARD constexpr const_pointer data() const noexcept
            {
                return m_ptr;
            }
            SCN_NODISCARD constexpr size_type size() const noexcept
            {
                return m_size;
            }
            SCN_NODISCARD size_type capacity() const noexcept
            {
                if (SCN_LIKELY(is_small())) {
                    return StackN;
                }
                return _get_heap().cap;
            }

            SCN_NODISCARD constexpr bool empty() const noexcept
            {
                return size() == 0;
            }

            SCN_NODISCARD bool is_small() const noexcept
            {
                // oh so very ub
                return m_ptr == reinterpret_cast<const_pointer>(
                                    std::addressof(m_stack_storage));
            }
            constexpr static bool can_be_small(size_type n) noexcept
            {
                return n <= StackN;
            }

            SCN_CONSTEXPR14 reference operator[](size_type pos)
            {
                SCN_EXPECT(pos < size());
                return *(begin() + pos);
            }
            SCN_CONSTEXPR14 const_reference operator[](size_type pos) const
            {
                SCN_EXPECT(pos < size());
                return *(begin() + pos);
            }

            SCN_CONSTEXPR14 reference front()
            {
                SCN_EXPECT(!empty());
                return *begin();
            }
            SCN_CONSTEXPR14 const_reference front() const
            {
                SCN_EXPECT(!empty());
                return *begin();
            }

            SCN_CONSTEXPR14 reference back()
            {
                SCN_EXPECT(!empty());
                return *(end() - 1);
            }
            SCN_CONSTEXPR14 const_reference back() const
            {
                SCN_EXPECT(!empty());
                return *(end() - 1);
            }

            SCN_CONSTEXPR14 iterator begin() noexcept
            {
                return data();
            }
            constexpr const_iterator begin() const noexcept
            {
                return data();
            }
            constexpr const_iterator cbegin() const noexcept
            {
                return begin();
            }

            SCN_CONSTEXPR14 iterator end() noexcept
            {
                return begin() + size();
            }
            constexpr const_iterator end() const noexcept
            {
                return begin() + size();
            }
            constexpr const_iterator cend() const noexcept
            {
                return end();
            }

            SCN_CONSTEXPR14 reverse_iterator rbegin() noexcept
            {
                return make_reverse_iterator(end());
            }
            constexpr const_reverse_iterator rbegin() const noexcept
            {
                return make_reverse_iterator(end());
            }
            constexpr const_reverse_iterator crbegin() const noexcept
            {
                return rbegin();
            }

            SCN_CONSTEXPR14 reverse_iterator rend() noexcept
            {
                return make_reverse_iterator(begin());
            }
            constexpr const_reverse_iterator rend() const noexcept
            {
                return make_reverse_iterator(begin());
            }
            constexpr const_reverse_iterator crend() const noexcept
            {
                return rend();
            }

            SCN_NODISCARD
            constexpr size_type max_size() const noexcept
            {
                return std::numeric_limits<size_type>::max();
            }

            void make_small() noexcept
            {
                if (is_small() || !can_be_small(size())) {
                    return;
                }

                stack_storage s;
                uninitialized_move(begin(), end(),
                                   s.reinterpret_unconstructed_data());
                auto tmp_size = size();

                _destruct();
                auto& stack = _construct_stack_storage();
                uninitialized_move(s.reinterpret_data(),
                                   s.reinterpret_data() + tmp_size,
                                   stack.reinterpret_unconstructed_data());
                m_size = tmp_size;
            }

            void reserve(size_type new_cap)
            {
                if (new_cap <= capacity()) {
                    return;
                }
                _realloc(next_pow2(new_cap));
            }

            void shrink_to_fit()
            {
                if (is_small()) {
                    return;
                }
                if (!can_be_small(size())) {
                    _realloc(size());
                }
                else {
                    make_small();
                }
            }

            void clear() noexcept
            {
                _destruct_elements();
            }

            iterator erase(iterator pos)
            {
                if (pos == end()) {
                    pos->~T();
                    m_size = size() - 1;
                    return end();
                }
                else {
                    for (auto it = pos; it != end(); ++it) {
                        it->~T();
                        ::new (static_cast<void*>(it)) T(std::move(*(it + 1)));
                    }
                    (end() - 1)->~T();
                    m_size = size() - 1;
                    return pos;
                }
            }

            iterator erase(iterator b, iterator e)
            {
                if (begin() == end()) {
                    return b;
                }
                if (e == end()) {
                    auto n = static_cast<size_t>(std::distance(b, e));
                    for (auto it = b; it != e; ++it) {
                        it->~T();
                    }
                    m_size = size() - n;
                    return end();
                }
                SCN_ENSURE(false);
                SCN_UNREACHABLE;
            }

            void push_back(const T& value)
            {
                ::new (_prepare_push_back()) T(value);
                m_size = size() + 1;
            }
            void push_back(T&& value)
            {
                ::new (_prepare_push_back()) T(std::move(value));
                m_size = size() + 1;
            }

            template <typename... Args>
            reference emplace_back(Args&&... args)
            {
                ::new (_prepare_push_back()) T(SCN_FWD(args)...);
                m_size = size() + 1;
                return back();
            }

            void pop_back()
            {
                back().~T();
                m_size = size() - 1;
            }

            void resize(size_type count)
            {
                if (count > size()) {
                    if (count > capacity()) {
                        _realloc(next_pow2(capacity()));
                    }
                    uninitialized_fill_value_init(begin() + size(),
                                                  begin() + count);
                }
                else {
                    for (auto it = begin() + count; it != end(); ++it) {
                        it->~T();
                    }
                }
                m_size = count;
            }

            SCN_CONSTEXPR14 void swap(small_vector& other) noexcept
            {
                small_vector tmp{SCN_MOVE(other)};
                other = std::move(*this);
                *this = std::move(tmp);
            }

        private:
            stack_storage& _construct_stack_storage() noexcept
            {
                ::new (std::addressof(m_stack_storage)) stack_storage;
                m_ptr = m_stack_storage.reinterpret_unconstructed_data();
                return m_stack_storage;
            }
            heap_storage& _construct_heap_storage() noexcept
            {
                ::new (std::addressof(m_heap_storage)) heap_storage;
                m_ptr = nullptr;
                return m_heap_storage;
            }

            void _destruct_stack_storage() noexcept
            {
                _get_stack().~stack_storage();
            }
            void _destruct_heap_storage() noexcept
            {
                if (capacity() != 0) {
                    delete[] static_cast<unsigned char*>(
                        static_cast<void*>(m_ptr));
                }
                _get_heap().~heap_storage();
            }

            void _destruct_elements() noexcept
            {
                const auto s = size();
                for (size_type i = 0; i != s; ++i) {
                    m_ptr[i].~T();
                }
                m_size = 0;
            }

            void _destruct() noexcept
            {
                _destruct_elements();
                if (SCN_UNLIKELY(!is_small())) {
                    _destruct_heap_storage();
                }
                else {
                    _destruct_stack_storage();
                }
            }

            void _realloc(size_type new_cap)
            {
                auto storage_ptr = new unsigned char[new_cap * sizeof(T)];
                auto ptr =
                    static_cast<pointer>(static_cast<void*>(storage_ptr));
                auto n = size();
                uninitialized_move(begin(), end(), ptr);
                _destruct();
                auto& heap = [this]() -> heap_storage& {
                    if (is_small()) {
                        return _construct_heap_storage();
                    }
                    return _get_heap();
                }();
                m_ptr = ptr;
                m_size = n;
                heap.cap = new_cap;
            }

            void* _prepare_push_back()
            {
                if (SCN_UNLIKELY(size() == capacity())) {
                    _realloc(next_pow2(size() + 1));
                }
                return m_ptr + size();
            }

            stack_storage& _get_stack() noexcept
            {
                return m_stack_storage;
            }
            const stack_storage& _get_stack() const noexcept
            {
                return m_stack_storage;
            }

            heap_storage& _get_heap() noexcept
            {
                return m_heap_storage;
            }
            const heap_storage& _get_heap() const noexcept
            {
                return m_heap_storage;
            }

            pointer m_ptr{nullptr};
            size_type m_size{0};
            union {
                stack_storage m_stack_storage;
                heap_storage m_heap_storage;
            };
        };

        template <typename T, size_t N>
        SCN_CONSTEXPR14 void swap(
            small_vector<T, N>& l,
            small_vector<T, N>& r) noexcept(noexcept(l.swap(r)))
        {
            l.swap(r);
        }

        SCN_CLANG_POP  // -Wpadded
    }                  // namespace detail

    SCN_END_NAMESPACE
}  // namespace scn

#endif  // SCN_SMALL_VECTOR_H
