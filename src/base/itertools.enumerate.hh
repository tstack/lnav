/**
 * Copyright (c) 2024, Timothy Stack
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

#ifndef lnav_itertools_enumerate_hh
#define lnav_itertools_enumerate_hh

#include <tuple>

namespace lnav::itertools {

namespace details {

template<typename Iterator, typename CounterType = size_t>
class EnumerateIterator {
    Iterator iter;
    CounterType index;

public:
    EnumerateIterator(Iterator iter, CounterType index)
        : iter(std::move(iter)), index(index)
    {
    }

    EnumerateIterator& operator++()
    {
        ++iter;
        ++index;
        return *this;
    }

    auto operator*() { return std::forward_as_tuple(index, *iter); }
    bool operator!=(EnumerateIterator const& rhs) const
    {
        return iter != rhs.iter;
    }
};

}  // namespace details

template<typename Iterable, typename CounterType = size_t>
class enumerate {
public:
    enumerate(Iterable& iterable, CounterType start = 0)
        : iterable(iterable), index(start)
    {
    }

    auto begin()
    {
        return details::EnumerateIterator{std::begin(iterable), index};
    }

    auto end() { return details::EnumerateIterator{std::end(iterable), index}; }

private:
    Iterable& iterable;
    CounterType index;
};

}  // namespace lnav::itertools

#endif
