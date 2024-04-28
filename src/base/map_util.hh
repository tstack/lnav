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

#ifndef lnav_map_util_hh
#define lnav_map_util_hh

#include <functional>
#include <map>
#include <type_traits>
#include <vector>


namespace lnav {
namespace map {

template<typename C>
std::optional<
    std::reference_wrapper<std::conditional_t<std::is_const<C>::value,
                                              const typename C::mapped_type,
                                              typename C::mapped_type>>>
find(C& container, const typename C::key_type& key)
{
    auto iter = container.find(key);
    if (iter != container.end()) {
        return std::make_optional(std::ref(iter->second));
    }

    return std::nullopt;
}

template<typename K, typename V, typename M = std::map<K, V>>
M
from_vec(const std::vector<std::pair<K, V>>& container)
{
    M retval;

    for (const auto& elem : container) {
        retval[elem.first] = elem.second;
    }

    return retval;
}

template<typename K, typename V>
class small : public std::vector<std::pair<K, V>> {
public:
    auto insert(const K& key, const V& value)
    {
        auto pos = this->begin();

        while (pos != this->end() && pos->first < key) {
            ++pos;
        }
        return this->emplace(pos, std::make_pair(key, value));
    }

    auto find(const K& key)
    {
        auto retval = this->begin();

        while (retval != this->end()
               && (retval->first < key || key < retval->first))
        {
            ++retval;
        }

        return retval;
    }

    auto find(const K& key) const
    {
        auto retval = this->begin();

        while (retval != this->end()
               && (retval->first < key || key < retval->first))
        {
            ++retval;
        }

        return retval;
    }

    V& operator[](const K& key)
    {
        auto iter = this->find(key);
        if (iter != this->end()) {
            return iter->second;
        }

        this->emplace_back(key, V{});

        return this->back().second;
    }
};

}  // namespace map
}  // namespace lnav

#endif
