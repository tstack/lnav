/**
 * Copyright (c) 2022, Timothy Stack
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

#ifndef lnav_itertools_similar_hh
#define lnav_itertools_similar_hh

#include <queue>
#include <string>

#include "base/fts_fuzzy_match.hh"
#include "base/itertools.hh"

namespace lnav::itertools {

namespace details {

template<typename F>
struct similar_to {
    std::optional<F> st_mapper;
    const std::string st_pattern;
    size_t st_count{5};
};

struct identity {
    template<typename U>
    constexpr auto operator()(U&& v) const noexcept
        -> decltype(std::forward<U>(v))
    {
        return std::forward<U>(v);
    }
};

}  // namespace details

template<typename F>
inline details::similar_to<F>
similar_to(F mapper, std::string pattern, size_t count = 5)
{
    return lnav::itertools::details::similar_to<F>{
        mapper, std::move(pattern), count};
}

inline auto
similar_to(std::string pattern, size_t count = 5)
{
    return similar_to(details::identity{}, std::move(pattern), count);
}

}  // namespace lnav::itertools

template<typename T, typename F>
std::vector<typename T::value_type>
operator|(const T& in, const lnav::itertools::details::similar_to<F>& st)
{
    using score_pair = std::pair<int, typename T::value_type>;

    struct score_cmp {
        bool operator()(const score_pair& lhs, const score_pair& rhs)
        {
            return lhs.first > rhs.first;
        }
    };

    std::vector<std::remove_const_t<typename T::value_type>> retval;

    if (st.st_pattern.empty()) {
        retval.insert(retval.begin(), in.begin(), in.end());
        return retval;
    }

    std::priority_queue<score_pair, std::vector<score_pair>, score_cmp> pq;
    auto exact_match = false;

    for (const auto& elem : in) {
        int score = 0;

        auto elem_str = lnav::func::invoke(st.st_mapper.value(), elem);
        const char* estr;

        if constexpr (std::is_same_v<decltype(elem_str), const char*>) {
            estr = elem_str;
        } else {
            estr = elem_str.c_str();
        }

        if (!fts::fuzzy_match(st.st_pattern.c_str(), estr, score)) {
            continue;
        }
        if (score <= 0) {
            continue;
        }
        if (st.st_pattern == estr) {
            exact_match = true;
        }
        pq.push(std::make_pair(score, elem));

        if (pq.size() > st.st_count) {
            pq.pop();
        }
    }

    while (!pq.empty()) {
        retval.emplace_back(pq.top().second);
        pq.pop();
    }
    std::reverse(retval.begin(), retval.end());

    if (retval.size() == 1 && exact_match) {
        retval.pop_back();
    }

    return retval;
}

#endif
