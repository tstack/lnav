#ifndef SHORT_ALLOC_H
#define SHORT_ALLOC_H

// The MIT License (MIT)
//
// Copyright (c) 2015 Howard Hinnant
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

class arena {
    char* buf_;
    std::size_t capacity_;
    char* ptr_;

public:
    ~arena() { ptr_ = nullptr; }
    arena(char* buf, std::size_t capacity) noexcept
        : buf_(buf), capacity_(capacity), ptr_(buf)
    {
    }
    template<std::size_t N>
    explicit arena(char (&buf)[N]) noexcept : arena(buf, N)
    {
    }
    arena(arena&&) = delete;
    arena& operator=(arena&&) = delete;
    arena(const arena&) = delete;
    arena& operator=(const arena&) = delete;

    /**
     * Allocate `n` bytes aligned to `align` from the buffer.
     *
     * @return The allocated memory or nullptr if there is not enough room.
     */
    char* allocate(std::size_t n, std::size_t align) noexcept;

    /**
     * Allocate uninitialized space for `n` objects of type T.
     *
     * @return The allocated memory or nullptr if there is not enough room.
     */
    template<typename T>
    T* allocate(std::size_t n = 1) noexcept
    {
        return reinterpret_cast<T*>(this->allocate(n * sizeof(T), alignof(T)));
    }

    /**
     * Release memory returned by allocate().  The space is only reclaimed
     * if it was the most recent allocation.
     *
     * @return false if `p` does not point into this arena's buffer.
     */
    bool deallocate(char* p, std::size_t n) noexcept;

    std::size_t capacity() const noexcept { return capacity_; }
    std::size_t used() const noexcept
    {
        return static_cast<std::size_t>(ptr_ - buf_);
    }
    void reset() noexcept { ptr_ = buf_; }

private:
    bool pointer_in_buffer(const char* p) const noexcept
    {
        return std::uintptr_t(buf_) <= std::uintptr_t(p)
            && std::uintptr_t(p) <= std::uintptr_t(buf_) + capacity_;
    }
};

inline char*
arena::allocate(std::size_t n, std::size_t align) noexcept
{
    assert(pointer_in_buffer(ptr_) && "short_alloc has outlived arena");
    assert((align & (align - 1)) == 0 && "alignment must be a power of two");

    auto addr = std::uintptr_t(ptr_);
    auto pad = (align - (addr % align)) % align;
    auto avail = static_cast<std::size_t>(buf_ + capacity_ - ptr_);
    if (pad > avail || n > avail - pad) {
        return nullptr;
    }

    char* r = ptr_ + pad;
    ptr_ = r + n;
    return r;
}

inline bool
arena::deallocate(char* p, std::size_t n) noexcept
{
    assert(pointer_in_buffer(ptr_) && "short_alloc has outlived arena");
    if (!pointer_in_buffer(p)) {
        return false;
    }
    if (p + n == ptr_) {
        ptr_ = p;
    }
    return true;
}

template<class T>
class short_alloc {
public:
    using value_type = T;
    using arena_type = arena;

private:
    arena_type& a_;

public:
    short_alloc(const short_alloc&) = default;
    short_alloc& operator=(const short_alloc&) = delete;

    short_alloc(arena_type& a) noexcept : a_(a) {}
    template<class U>
    short_alloc(const short_alloc<U>& a) noexcept : a_(a.a_)
    {
    }

    template<class _Up>
    struct rebind {
        using other = short_alloc<_Up>;
    };

    T* allocate(std::size_t n)
    {
        auto* retval = a_.template allocate<T>(n);
        if (retval == nullptr) {
            retval = static_cast<T*>(::operator new(n * sizeof(T)));
        }
        return retval;
    }
    void deallocate(T* p, std::size_t n) noexcept
    {
        if (!a_.deallocate(reinterpret_cast<char*>(p), n * sizeof(T))) {
            ::operator delete(p);
        }
    }

    template<class T1, class U>
    friend bool operator==(const short_alloc<T1>& x,
                           const short_alloc<U>& y) noexcept;

    template<class U>
    friend class short_alloc;
};

template<class T, class U>
inline bool
operator==(const short_alloc<T>& x, const short_alloc<U>& y) noexcept
{
    return &x.a_ == &y.a_;
}

template<class T, class U>
inline bool
operator!=(const short_alloc<T>& x, const short_alloc<U>& y) noexcept
{
    return !(x == y);
}

template<typename T>
using stack_vector = std::vector<T, short_alloc<T>>;

struct stack_buf {
    static constexpr auto SIZE = 256;
    char sb_backing[SIZE];
    char* sb_value{nullptr};

    stack_buf() = default;

    stack_buf(const stack_buf&) = delete;
    stack_buf(stack_buf&&) = delete;

    stack_buf& operator=(const stack_buf&) = delete;
    stack_buf& operator=(stack_buf&&) = delete;

    ~stack_buf()
    {
        if (this->sb_value != nullptr) {
            delete[] this->sb_value;
        }
    }

    char* allocate(std::size_t amount)
    {
        if (amount <= SIZE) {
            return this->sb_backing;
        }
        this->sb_value = new char[amount];
        return this->sb_value;
    }
};

#endif  // SHORT_ALLOC_H
