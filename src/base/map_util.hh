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
#include <iterator>
#include <map>
#include <optional>
#include <tuple>
#include <type_traits>
#include <vector>

namespace lnav::set {

template<typename K, typename KeyCmp = std::less<K>>
class small {
public:
    using key_type = K;
    using key_compare = KeyCmp;

    small() = default;
    small(std::initializer_list<K> il) : s_keys(il) {}

    std::optional<size_t> index_of(const K& key) const
    {
        for (size_t index = 0; index < this->s_keys.size(); ++index) {
            if (!key_compare{}(this->s_keys[index], key)
                && !key_compare{}(key, this->s_keys[index]))
            {
                return index;
            }
        }
        return std::nullopt;
    }

    bool contains(const K& key) const
    {
        return this->index_of(key).has_value();
    }

    void insert(const K& key)
    {
        auto index_opt = this->index_of(key);
        if (!index_opt) {
            this->s_keys.emplace_back(key);
        }
    }

    void clear() { this->s_keys.clear(); }

    size_t size() const { return this->s_keys.size(); }

    bool empty() const { return this->s_keys.empty(); }

    const std::vector<K>& keys() const { return this->s_keys; }

protected:
    std::vector<K> s_keys;
};

}  // namespace lnav::set

namespace lnav::map {

template<typename C>
std::optional<std::conditional_t<
    std::is_trivially_copyable_v<typename C::mapped_type>,
    typename C::mapped_type,
    std::reference_wrapper<std::conditional_t<std::is_const_v<C>,
                                              const typename C::mapped_type,
                                              typename C::mapped_type>>>>
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

/**
 * A map for a handful of entries, kept as a single vector of pairs.
 *
 * Lookup is a linear scan, which beats a tree or a hash at these sizes.  One
 * vector rather than parallel key and value vectors because these are held
 * per-operation in log_op_description, where a big file has hundreds of
 * thousands of them and a second empty vector costs 24 bytes apiece.
 */
template<typename K, typename V, typename KeyCmp = std::less<K>>
class small {
public:
    using value_type = std::pair<K, V>;
    using key_type = K;
    using mapped_type = V;
    using key_compare = KeyCmp;
    using size_type = size_t;
    using reference = V&;
    using const_reference = const V&;
    using difference_type = ptrdiff_t;
    using container_type = std::vector<value_type>;
    using iterator = typename container_type::iterator;
    using const_iterator = typename container_type::const_iterator;

    small() = default;

    std::optional<size_t> index_of(const K& key) const
    {
        for (size_t index = 0; index < this->s_entries.size(); ++index) {
            const auto& elem_key = this->s_entries[index].first;
            if (!key_compare{}(elem_key, key) && !key_compare{}(key, elem_key))
            {
                return index;
            }
        }
        return std::nullopt;
    }

    bool contains(const K& key) const
    {
        return this->index_of(key).has_value();
    }

    template<typename U = V>
    void insert(const K& key, U&& value)
    {
        auto index_opt = this->index_of(key);
        if (index_opt) {
            this->s_entries[index_opt.value()].second = std::forward<U>(value);
        } else {
            this->s_entries.emplace_back(key, std::forward<U>(value));
        }
    }

    std::optional<const V*> value_for(const K& key) const
    {
        auto index_opt = this->index_of(key);
        if (index_opt) {
            return &this->s_entries[index_opt.value()].second;
        }
        return std::nullopt;
    }

    std::optional<V*> value_for(const K& key)
    {
        auto index_opt = this->index_of(key);
        if (index_opt) {
            return &this->s_entries[index_opt.value()].second;
        }
        return std::nullopt;
    }

    V& value_for_key_or_default(const K& key)
    {
        auto index_opt = this->index_of(key);
        if (index_opt) {
            return this->s_entries[index_opt.value()].second;
        }
        // Built in place so a value that cannot be copied or moved still
        // works here.
        this->s_entries.emplace_back(std::piecewise_construct,
                                     std::forward_as_tuple(key),
                                     std::forward_as_tuple());
        return this->s_entries.back().second;
    }

    V& operator[](const K& key) { return this->value_for_key_or_default(key); }

    void clear() { this->s_entries.clear(); }

    size_t size() const { return this->s_entries.size(); }

    bool empty() const { return this->s_entries.empty(); }

    const container_type& entries() const { return this->s_entries; }

    iterator begin() { return this->s_entries.begin(); }
    iterator end() { return this->s_entries.end(); }

    const_iterator begin() const { return this->s_entries.begin(); }
    const_iterator end() const { return this->s_entries.end(); }

private:
    container_type s_entries;
};

}  // namespace lnav::map

#endif
