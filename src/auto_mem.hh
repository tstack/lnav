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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file auto_mem.hh
 */

#ifndef lnav_auto_mem_hh
#define lnav_auto_mem_hh

#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include <exception>

#include "base/result.h"

typedef void (*free_func_t)(void *);

/**
 * Resource management class for memory allocated by a custom allocator.
 *
 * @param T The object type.
 * @param auto_free The function to call to free the managed object.
 */
template<class T, free_func_t default_free = free>
class auto_mem {
public:
    explicit auto_mem(T *ptr = nullptr)
        : am_ptr(ptr), am_free_func(default_free) {
    };

    auto_mem(const auto_mem &am) = delete;

    template<typename F>
    explicit auto_mem(F free_func) noexcept
        : am_ptr(nullptr), am_free_func((free_func_t)free_func) { };

    auto_mem(auto_mem &&other) noexcept
        : am_ptr(other.release()),
          am_free_func(other.am_free_func) {
    };

    ~auto_mem() {
        this->reset();
    };

    operator T *() const { return this->am_ptr; };

    auto_mem &operator =(T *ptr)
    {
        this->reset(ptr);
        return *this;
    };

    auto_mem &operator=(auto_mem &) = delete;

    auto_mem &operator =(auto_mem && am) noexcept
    {
        this->reset(am.release());
        this->am_free_func = am.am_free_func;
        return *this;
    };

    T *release()
    {
        T *retval = this->am_ptr;

        this->am_ptr = nullptr;
        return retval;
    };

    T *in() const
    {
        return this->am_ptr;
    };

    T **out()
    {
        this->reset();
        return &this->am_ptr;
    };

    void reset(T *ptr = nullptr)
    {
        if (this->am_ptr != ptr) {
            if (this->am_ptr != nullptr) {
                this->am_free_func((void *)this->am_ptr);
            }
            this->am_ptr = ptr;
        }
    };

private:
    T *  am_ptr;
    void (*am_free_func)(void *);
};

template<typename T, void(*free_func) (T *)>
class static_root_mem {
public:
    static_root_mem() {
        memset(&this->srm_value, 0, sizeof(T));
    };

    ~static_root_mem() { free_func(&this->srm_value); };

    const T *operator->() const { return &this->srm_value; };

    const T &in() const { return this->srm_value; };

    T *inout() {
        free_func(&this->srm_value);
        memset(&this->srm_value, 0, sizeof(T));
        return &this->srm_value;
    };

private:
    static_root_mem &operator =(T &) { return *this; };

    static_root_mem &operator =(static_root_mem &) { return *this; };

    T srm_value;
};

class auto_buffer {
public:
    static auto_buffer alloc(size_t size) {
        return auto_buffer{ (char *) malloc(size), size };
    }

    auto_buffer(auto_buffer&& other) noexcept
        : ab_buffer(other.ab_buffer), ab_size(other.ab_size) {
        other.ab_buffer = nullptr;
        other.ab_size = 0;
    }

    ~auto_buffer() {
        free(this->ab_buffer);
        this->ab_buffer = nullptr;
        this->ab_size = 0;
    }

    char *in() {
        return this->ab_buffer;
    }

    std::pair<char *, size_t> release() {
        auto retval = std::make_pair(this->ab_buffer, this->ab_size);

        this->ab_buffer = nullptr;
        this->ab_size = 0;
        return retval;
    }

    size_t size() const {
        return this->ab_size;
    }

    void expand_by(size_t amount) {
        if (amount == 0) {
            return;
        }
        auto new_size = this->ab_size + amount;
        auto new_buffer = (char *) realloc(this->ab_buffer, new_size);

        if (new_buffer == nullptr) {
            throw std::bad_alloc();
        }

        this->ab_buffer = new_buffer;
        this->ab_size = new_size;
    }

    auto_buffer& shrink_to(size_t new_size) {
        this->ab_size = new_size;
        return *this;
    }
private:
    auto_buffer(char *buffer, size_t size) : ab_buffer(buffer), ab_size(size) {
    }

    char *ab_buffer;
    size_t ab_size;
};

#endif
