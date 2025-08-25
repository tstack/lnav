/**
 * Copyright (c) 2025, Timothy Stack
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
 */

#ifndef lnav_distributed_slice
#define lnav_distributed_slice

#include <vector>

#define DS_STRINGIZE(A) #A

#if defined(__APPLE__)
#    define DIST_SLICE(id) \
        __attribute__((no_sanitize_address, \
                       used, \
                       section("__DATA,__ds_" DS_STRINGIZE( \
                           id) ",regular,no_dead_strip")))
#    define DIST_SLICE_BEGIN(id) \
        __asm("section$start$__DATA$__ds_" DS_STRINGIZE(id))
#    define DIST_SLICE_END(id) \
        __asm("section$end$__DATA$__ds_" DS_STRINGIZE(id))
#else
#    define DIST_SLICE(id) \
        __attribute__(( \
            no_sanitize_address, used, section("ds_" DS_STRINGIZE(id))))
#    define DIST_SLICE_BEGIN(id) __asm("__start_ds_" DS_STRINGIZE(id))
#    define DIST_SLICE_END(id)   __asm("__stop_ds_" DS_STRINGIZE(id))
#endif

template<typename T>
struct dist_slice_container {
    using iterator = T*;
    using const_iterator = const T*;

    constexpr dist_slice_container(iterator start, iterator end)
        : ds_start(start), ds_end(end)
    {
    }

    constexpr iterator begin() { return this->ds_start; }

    constexpr const_iterator begin() const { return this->ds_start; }

    constexpr iterator end() { return this->ds_end; }

    constexpr const_iterator end() const { return this->ds_end; }

    size_t size() const { return this->ds_end - this->ds_start; }

    template<typename U>
    struct slice_indexed_array {
        using iterator = typename std::vector<U>::iterator;

        explicit slice_indexed_array(const dist_slice_container& slices)
            : sia_slices(slices), sia_array(std::vector<U>(slices.size()))
        {
        }

        U& operator[](const T* slice)
        {
            auto index = std::distance(this->sia_slices.begin(), slice);
            return this->sia_array[index];
        }

        const U& operator[](const T* slice) const
        {
            auto index = std::distance(this->sia_slices.begin(), slice);
            return this->sia_array[index];
        }

        iterator begin() { return this->sia_array.begin(); }

        iterator end() { return this->sia_array.end(); }

        void clear()
        {
            for (auto& elem : this->sia_array) {
                elem.clear();
            }
        }

    private:
        const dist_slice_container& sia_slices;
        std::vector<U> sia_array;
    };

    template<typename U>
    slice_indexed_array<U> create_array_indexed_by() const
    {
        return slice_indexed_array<U>(*this);
    }

private:
    iterator ds_start;
    iterator ds_end;
};

#define DIST_SLICE_CONTAINER(T, id) \
    (+[]() -> auto { \
        extern T start DIST_SLICE_BEGIN(id); \
        extern T end DIST_SLICE_END(id); \
\
        return dist_slice_container<T>(&start, &end); \
    })()

#endif