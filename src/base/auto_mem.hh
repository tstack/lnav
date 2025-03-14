/**
 * Copyright (c) 2007-2019, Timothy Stack
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
 * @file auto_mem.hh
 */

#ifndef lnav_auto_mem_hh
#define lnav_auto_mem_hh

#include <iterator>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fmt/format.h"

using free_func_t = void (*)(void*);

/**
 * Resource management class for memory allocated by a custom allocator.
 *
 * @param T The object type.
 * @param auto_free The function to call to free the managed object.
 */
template<class T, free_func_t default_free = free>
class auto_mem {
public:
    static void noop_free(void*) {}

    static auto_mem<T> leak(T* ptr)
    {
        auto_mem<T> retval(noop_free);

        retval = ptr;

        return retval;
    }

    static auto_mem calloc(size_t count)
    {
        return auto_mem(static_cast<T*>(::calloc(count, sizeof(T))));
    }

    static auto_mem malloc(size_t sz)
    {
        return auto_mem(static_cast<T*>(::malloc(sz)));
    }

    explicit auto_mem(T* ptr = nullptr)
        : am_ptr(ptr), am_free_func(default_free)
    {
    }

    auto_mem(const auto_mem& am) = delete;

    template<typename F>
    explicit auto_mem(F free_func) noexcept
        : am_ptr(nullptr), am_free_func((free_func_t) free_func)
    {
    }

    auto_mem(auto_mem&& other) noexcept
        : am_ptr(other.release()), am_free_func(other.am_free_func)
    {
    }

    ~auto_mem() { this->reset(); }

    bool empty() const { return this->am_ptr == nullptr; }

    operator T*() const { return this->am_ptr; }

    T* operator->() { return this->am_ptr; }

    auto_mem& operator=(T* ptr)
    {
        this->reset(ptr);
        return *this;
    }

    auto_mem& operator=(const auto_mem&) = delete;
    auto_mem& operator=(auto_mem&) = delete;

    auto_mem& operator=(auto_mem&& am) noexcept
    {
        this->reset(am.release());
        this->am_free_func = am.am_free_func;
        return *this;
    }

    T* release()
    {
        T* retval = this->am_ptr;

        this->am_ptr = nullptr;
        return retval;
    }

    T* in() const { return this->am_ptr; }

    T** out()
    {
        this->reset();
        return &this->am_ptr;
    }

    template<typename F>
    F get_free_func() const
    {
        return (F) this->am_free_func;
    }

    void reset(T* ptr = nullptr)
    {
        if (this->am_ptr != ptr) {
            if (this->am_ptr != nullptr) {
                this->am_free_func((void*) this->am_ptr);
            }
            this->am_ptr = ptr;
        }
    }

private:
    T* am_ptr;
    void (*am_free_func)(void*);
};

template<typename T, void (*free_func)(T*)>
class static_root_mem {
public:
    static_root_mem() { memset(&this->srm_value, 0, sizeof(T)); }

    ~static_root_mem() { free_func(&this->srm_value); }

    const T* operator->() const { return &this->srm_value; }

    const T& in() const { return this->srm_value; }

    T* inout()
    {
        free_func(&this->srm_value);
        memset(&this->srm_value, 0, sizeof(T));
        return &this->srm_value;
    }

private:
    static_root_mem& operator=(T&) { return *this; }

    static_root_mem& operator=(static_root_mem&) { return *this; }

    T srm_value;
};

class auto_buffer {
public:
    using value_type = char;

    static auto_buffer alloc(size_t capacity)
    {
        return auto_buffer{capacity == 0 ? nullptr : (char*) malloc(capacity),
                           capacity};
    }

    static auto_buffer alloc_bitmap(size_t capacity_in_bits)
    {
        return alloc((capacity_in_bits + 7) / 8);
    }

    static auto_buffer from(const char* mem, size_t size)
    {
        auto retval = alloc(size);

        retval.resize(size);
        memcpy(retval.in(), mem, size);
        return retval;
    }

    auto_buffer(const auto_buffer&) = delete;

    auto_buffer(auto_buffer&& other) noexcept
        : ab_buffer(other.ab_buffer), ab_size(other.ab_size),
          ab_capacity(other.ab_capacity)
    {
        other.ab_buffer = nullptr;
        other.ab_size = 0;
        other.ab_capacity = 0;
    }

    ~auto_buffer()
    {
        free(this->ab_buffer);
        this->ab_buffer = nullptr;
        this->ab_size = 0;
        this->ab_capacity = 0;
    }

    auto_buffer& operator=(const auto_buffer&) = delete;

    auto_buffer& operator=(auto_buffer&& other) noexcept
    {
        free(this->ab_buffer);
        this->ab_buffer = std::exchange(other.ab_buffer, nullptr);
        this->ab_size = std::exchange(other.ab_size, 0);
        this->ab_capacity = std::exchange(other.ab_capacity, 0);
        return *this;
    }

    void swap(auto_buffer& other)
    {
        std::swap(this->ab_buffer, other.ab_buffer);
        std::swap(this->ab_size, other.ab_size);
        std::swap(this->ab_capacity, other.ab_capacity);
    }

    unsigned char* u_in()
    {
        return reinterpret_cast<unsigned char*>(this->ab_buffer);
    }

    char* in() { return this->ab_buffer; }

    char* at(size_t offset) { return &this->ab_buffer[offset]; }

    const char* at(size_t offset) const { return &this->ab_buffer[offset]; }

    char* begin() { return this->ab_buffer; }

    const char* begin() const { return this->ab_buffer; }

    char* next_available() { return &this->ab_buffer[this->ab_size]; }

    auto_buffer& push_back(char ch)
    {
        if (this->ab_size == this->ab_capacity) {
            this->expand_by(256);
        }
        this->ab_buffer[this->ab_size] = ch;
        this->ab_size += 1;

        return *this;
    }

    void pop_back() { this->ab_size -= 1; }

    bool is_bit_set(size_t bit_offset) const
    {
        size_t byte_offset = bit_offset / 8;
        auto bitmask = 1UL << (bit_offset % 8);

        return this->ab_buffer[byte_offset] & bitmask;
    }

    void set_bit(size_t bit_offset)
    {
        size_t byte_offset = bit_offset / 8;
        auto bitmask = 1UL << (bit_offset % 8);

        this->ab_buffer[byte_offset] |= bitmask;
    }

    void clear_bit(size_t bit_offset)
    {
        size_t byte_offset = bit_offset / 8;
        auto bitmask = 1UL << (bit_offset % 8);

        this->ab_buffer[byte_offset] &= ~bitmask;
    }

    std::reverse_iterator<char*> rbegin()
    {
        return std::reverse_iterator<char*>(this->end());
    }

    std::reverse_iterator<const char*> rbegin() const
    {
        return std::reverse_iterator<const char*>(this->end());
    }

    char* end() { return &this->ab_buffer[this->ab_size]; }

    const char* end() const { return &this->ab_buffer[this->ab_size]; }

    std::reverse_iterator<char*> rend()
    {
        return std::reverse_iterator<char*>(this->begin());
    }

    std::reverse_iterator<const char*> rend() const
    {
        return std::reverse_iterator<const char*>(this->begin());
    }

    std::pair<char*, size_t> release()
    {
        auto retval = std::make_pair(this->ab_buffer, this->ab_size);

        this->ab_buffer = nullptr;
        this->ab_size = 0;
        this->ab_capacity = 0;
        return retval;
    }

    std::unique_ptr<const unsigned char[]> to_unique() const
    {
        auto retval = std::make_unique<unsigned char[]>(this->ab_size);

        memcpy(retval.get(), this->ab_buffer, this->ab_size);
        return retval;
    }

    size_t size() const { return this->ab_size; }

    size_t bitmap_size() const { return this->ab_size * 8; }

    bool empty() const { return this->ab_size == 0; }

    bool full() const { return this->ab_size == this->ab_capacity; }

    size_t capacity() const { return this->ab_capacity; }

    template<typename T>
    std::enable_if_t<std::is_integral_v<T>, bool> has_capacity_for(
        T amount) const
    {
        if constexpr (std::is_signed_v<T>) {
            assert(amount >= 0);

            return amount <= static_cast<ssize_t>(this->ab_capacity);
        } else {
            return amount <= this->ab_capacity;
        }
    }

    size_t available() const { return this->ab_capacity - this->ab_size; }

    void clear() { this->resize(0); }

    auto_buffer& resize(size_t new_size)
    {
        assert(new_size <= this->ab_capacity);

        this->ab_size = new_size;
        return *this;
    }

    auto_buffer& resize_bitmap(size_t new_size_in_bits, int fill = 0)
    {
        auto new_size = (new_size_in_bits + 7) / 8;
        assert(new_size <= this->ab_capacity);

        auto old_size = std::exchange(this->ab_size, new_size);
        memset(this->at(old_size), 0, this->ab_size - old_size);
        return *this;
    }

    auto_buffer& resize_by(ssize_t amount)
    {
        return this->resize(this->ab_size + amount);
    }

    void expand_to(size_t new_capacity)
    {
        if (new_capacity <= this->ab_capacity) {
            return;
        }
        auto* new_buffer = (char*) realloc(this->ab_buffer, new_capacity);

        if (new_buffer == nullptr) {
            throw std::bad_alloc();
        }

        this->ab_buffer = new_buffer;
        this->ab_capacity = new_capacity;
    }

    void expand_bitmap_to(size_t new_capacity_in_bits)
    {
        this->expand_to((new_capacity_in_bits + 7) / 8);
    }

    void expand_by(size_t amount)
    {
        if (amount == 0) {
            return;
        }

        this->expand_to(this->ab_capacity + amount);
    }

    std::string to_string() const { return {this->ab_buffer, this->ab_size}; }

private:
    auto_buffer(char* buffer, size_t capacity)
        : ab_buffer(buffer), ab_capacity(capacity)
    {
    }

    char* ab_buffer;
    size_t ab_size{0};
    size_t ab_capacity;
};

struct text_auto_buffer {
    auto_buffer inner;
};

struct blob_auto_buffer {
    auto_buffer inner;
};

template<>
struct fmt::formatter<auto_buffer> : formatter<string_view> {
    template<typename FormatContext>
    auto format(const auto_buffer& buf, FormatContext& ctx) const
    {
        return formatter<string_view>::format(
            string_view{buf.begin(), buf.size()}, ctx);
    }
};

#endif
