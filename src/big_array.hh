/**
 * Copyright (c) 2017, Timothy Stack
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
 * @file big_array.hh
 */

#ifndef lnav_big_array_hh
#define lnav_big_array_hh

#include <sys/mman.h>
#include <unistd.h>

#include "base/lnav_log.hh"
#include "base/math_util.hh"

template<typename T>
struct big_array {
    static const size_t DEFAULT_INCREMENT = 100 * 1000;

    bool reserve(size_t size)
    {
        if (size < this->ba_capacity) {
            return false;
        }

        if (this->ba_ptr) {
            munmap(this->ba_ptr,
                   roundup_size(this->ba_capacity * sizeof(T), getpagesize()));
        }

        this->ba_capacity = size + DEFAULT_INCREMENT;
        void* result
            = mmap(nullptr,
                   roundup_size(this->ba_capacity * sizeof(T), getpagesize()),
                   PROT_READ | PROT_WRITE,
                   MAP_ANONYMOUS | MAP_PRIVATE,
                   -1,
                   0);

        ensure(result != MAP_FAILED);

        this->ba_ptr = (T*) result;

        return true;
    }

    void clear() { this->ba_size = 0; }

    size_t size() const { return this->ba_size; }

    void shrink_to(size_t new_size)
    {
        require(new_size <= this->ba_size);

        this->ba_size = new_size;
    }

    bool empty() const { return this->ba_size == 0; }

    void push_back(const T& val)
    {
        this->ba_ptr[this->ba_size] = val;
        this->ba_size += 1;
    }

    T& operator[](size_t index) { return this->ba_ptr[index]; }

    const T& operator[](size_t index) const { return this->ba_ptr[index]; }

    T& back() { return this->ba_ptr[this->ba_size - 1]; }

    using iterator = T*;

    iterator begin() { return this->ba_ptr; }

    iterator end() { return this->ba_ptr + this->ba_size; }

    T* ba_ptr{nullptr};
    size_t ba_size{0};
    size_t ba_capacity{0};
};

#endif
