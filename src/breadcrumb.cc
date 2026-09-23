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

#include <algorithm>
#include <string_view>
#include <unordered_set>

#include "breadcrumb.hh"

#include "base/fts_fuzzy_match.hh"

namespace breadcrumb {

possibility_collector::possibility_collector(string_fragment search,
                                             size_t max_count)
    : pc_search(search.to_string()), pc_max_count(max_count)
{
}

void
possibility_collector::add(string_fragment key)
{
    if (this->pc_search.empty() && this->pc_kept.size() >= this->pc_max_count)
    {
        return;
    }

    int score = 0;
    if (!this->pc_search.empty()) {
        // fuzzy_match() wants a NUL-terminated string, so reuse one buffer
        // rather than allocate for every key.
        this->pc_key_buf.assign(key.data(), key.length());
        if (!fts::fuzzy_match(
                this->pc_search.c_str(), this->pc_key_buf.c_str(), score)
            || score <= 0)
        {
            return;
        }
        if (this->pc_kept.size() >= this->pc_max_count) {
            const auto& worst = this->pc_kept.front();
            if (score <= worst.sk_score) {
                return;
            }
            std::pop_heap(
                this->pc_kept.begin(), this->pc_kept.end(), worse_first{});
            this->pc_kept.pop_back();
        }
        this->pc_kept.emplace_back(
            scored_key{score, this->pc_added, this->pc_key_buf});
        std::push_heap(
            this->pc_kept.begin(), this->pc_kept.end(), worse_first{});
    } else {
        this->pc_kept.emplace_back(
            scored_key{score, this->pc_added, key.to_string()});
    }
    this->pc_added += 1;
}

std::vector<possibility>
possibility_collector::release()
{
    std::vector<possibility> retval;

    if (!this->pc_search.empty()) {
        // worse_first puts the worst at the front of the heap, so sorting by
        // it leaves the best match at the front of the vector.
        std::sort_heap(
            this->pc_kept.begin(), this->pc_kept.end(), worse_first{});
    }
    retval.reserve(this->pc_kept.size());
    std::unordered_set<std::string_view> seen;
    for (const auto& sk : this->pc_kept) {
        if (seen.insert(sk.sk_key).second) {
            retval.emplace_back(sk.sk_key);
        }
    }
    this->pc_kept.clear();

    return retval;
}

}  // namespace breadcrumb
