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
#include <optional>
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

template<typename K, typename V, typename KeyCmp = std::less<K>>
class small : public lnav::set::small<K, KeyCmp> {
public:
    using value_type = V;
    using key_type = K;
    using mapped_type = V;
    using size_type = size_t;
    using reference = V&;
    using const_reference = const V&;
    using difference_type = ptrdiff_t;

    void insert(const K&) = delete;

    template<typename U = V>
    void insert(const K& key, U&& value)
    {
        auto index_opt = this->index_of(key);
        if (index_opt) {
            this->s_values[index_opt.value()] = std::forward<U>(value);
        } else {
            this->s_keys.emplace_back(key);
            this->s_values.emplace_back(std::forward<U>(value));
        }
    }

    std::optional<const V*> value_for(const K& key) const
    {
        auto index_opt = this->index_of(key);
        if (index_opt) {
            return &this->s_values[index_opt.value()];
        }
        return std::nullopt;
    }

    std::optional<V*> value_for(const K& key)
    {
        auto index_opt = this->index_of(key);
        if (index_opt) {
            return &this->s_values[index_opt.value()];
        }
        return std::nullopt;
    }

    V& value_for_key_or_default(const K& key)
    {
        auto index_opt = this->index_of(key);
        if (index_opt) {
            return this->s_values[index_opt.value()];
        }
        this->s_keys.emplace_back(key);
        this->s_values.resize(this->s_values.size() + 1);
        return this->s_values.back();
    }

    V& operator[](const K& key) { return this->value_for_key_or_default(key); }

    void clear()
    {
        this->s_keys.clear();
        this->s_values.clear();
    }

    const std::vector<V>& values() const { return this->s_values; }

    template<typename T>
    class iterator_T {
    public:
        using iterator_category = std::forward_iterator_tag;
        friend class small;
        const K& key() const { return this->i_parent.s_keys[this->i_index]; }

        const V& value() const
        {
            return this->i_parent.s_values[this->i_index];
        }

        std::conditional_t<std::is_const_v<T>, const V&, V&> value()
        {
            return this->i_parent.s_values[this->i_index];
        }

        bool operator==(const iterator_T& other) const
        {
            return &this->i_parent == &other.i_parent
                && this->i_index == other.i_index;
        }

        bool operator!=(const iterator_T& other) const
        {
            return !(*this == other);
        }

        iterator_T& operator++()
        {
            this->i_index += 1;
            return *this;
        }

        template<typename U = T>
        std::enable_if_t<!std::is_const_v<U>, std::pair<const K&, V&>>
        operator*()
        {
            return {this->key(), this->value()};
        }

        std::pair<const K&, const V&> operator*() const
        {
            return {this->key(), this->value()};
        }

    private:
        iterator_T(T& parent, size_t index) : i_parent(parent), i_index(index)
        {
        }
        T& i_parent;
        size_t i_index;
    };

    using iterator = iterator_T<small>;
    using const_iterator = iterator_T<const small>;

    iterator begin() { return iterator(*this, 0); }
    iterator end() { return iterator(*this, this->s_keys.size()); }

    const_iterator begin() const { return const_iterator(*this, 0); }
    const_iterator end() const
    {
        return const_iterator(*this, this->s_keys.size());
    }

private:
    std::vector<V> s_values;
};

}  // namespace lnav::map

#endif
