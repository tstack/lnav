/**
 * Copyright (c) 2023, Timothy Stack
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

#ifndef lnav_lazy_vector_hh
#define lnav_lazy_vector_hh

#include <memory>
#include <utility>
#include <vector>

#include <stddef.h>

namespace lnav {

/**
 * A vector that costs one pointer until something is put in it.
 *
 * For a member that is usually empty and held in bulk.  The elements live in
 * a vector as usual once there are any, so the only price for a non-empty one
 * is the extra allocation holding it.
 *
 * Copies deep, so the value semantics are a vector's.
 */
template<typename T>
class lazy_vector {
public:
    using container_type = std::vector<T>;
    using value_type = T;
    using size_type = size_t;
    using iterator = typename container_type::iterator;
    using const_iterator = typename container_type::const_iterator;
    using reverse_iterator = typename container_type::reverse_iterator;
    using const_reverse_iterator = typename container_type::const_reverse_iterator;

    lazy_vector() = default;
    lazy_vector(lazy_vector&&) = default;
    lazy_vector& operator=(lazy_vector&&) = default;

    lazy_vector(const lazy_vector& other)
        : lv_vec(other.lv_vec == nullptr
                     ? nullptr
                     : std::make_unique<container_type>(*other.lv_vec))
    {
    }

    lazy_vector& operator=(const lazy_vector& other)
    {
        if (this != &other) {
            this->lv_vec = other.lv_vec == nullptr
                ? nullptr
                : std::make_unique<container_type>(*other.lv_vec);
        }
        return *this;
    }

    bool empty() const
    {
        return this->lv_vec == nullptr || this->lv_vec->empty();
    }

    size_t size() const
    {
        return this->lv_vec == nullptr ? 0 : this->lv_vec->size();
    }

    void clear() { this->lv_vec.reset(); }

    T& operator[](size_t index) { return (*this->lv_vec)[index]; }
    const T& operator[](size_t index) const { return (*this->lv_vec)[index]; }

    T& back() { return this->lv_vec->back(); }
    const T& back() const { return this->lv_vec->back(); }

    template<typename... Args>
    T& emplace_back(Args&&... args)
    {
        if (this->lv_vec == nullptr) {
            this->lv_vec = std::make_unique<container_type>();
        }
        return this->lv_vec->emplace_back(std::forward<Args>(args)...);
    }

    // A value-initialized iterator compares equal to another of its type, so
    // an empty one yields an empty range without allocating.
    iterator begin() { return this->lv_vec ? this->lv_vec->begin() : iterator{}; }
    iterator end() { return this->lv_vec ? this->lv_vec->end() : iterator{}; }

    const_iterator begin() const
    {
        return this->lv_vec ? this->lv_vec->begin() : const_iterator{};
    }
    const_iterator end() const
    {
        return this->lv_vec ? this->lv_vec->end() : const_iterator{};
    }

    reverse_iterator rbegin()
    {
        return this->lv_vec ? this->lv_vec->rbegin() : reverse_iterator{};
    }
    reverse_iterator rend()
    {
        return this->lv_vec ? this->lv_vec->rend() : reverse_iterator{};
    }

private:
    std::unique_ptr<container_type> lv_vec;
};

}  // namespace lnav

#endif
