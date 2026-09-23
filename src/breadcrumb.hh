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

#ifndef lnav_breadcrumb_hh
#define lnav_breadcrumb_hh

#include <functional>
#include <string>
#include <vector>

#include "base/attr_line.hh"
#include "base/intern_string.hh"
#include "fmt/format.h"
#include "mapbox/variant.hpp"

namespace breadcrumb {

struct possibility {
    possibility(std::string key, attr_line_t value)
        : p_key(std::move(key)), p_display_value(std::move(value))
    {
    }

    explicit possibility(std::string key) : p_key(key), p_display_value(key) {}

    possibility() = default;

    bool operator<(const possibility& rhs) const { return p_key < rhs.p_key; }
    bool operator>(const possibility& rhs) const { return rhs < *this; }
    bool operator<=(const possibility& rhs) const { return !(rhs < *this); }
    bool operator>=(const possibility& rhs) const { return !(*this < rhs); }

    static bool sort_cmp(const possibility& lhs, const possibility& rhs)
    {
        static constexpr const char* TOKENS = "[](){}";

        auto lhsf = string_fragment::from_str(lhs.p_key).trim(TOKENS);
        auto rhsf = string_fragment::from_str(rhs.p_key).trim(TOKENS);

        return strnatcasecmp(
                   lhsf.length(), lhsf.data(), rhsf.length(), rhsf.data())
            < 0;
    }

    std::string p_key;
    attr_line_t p_display_value;
};

/**
 * Gathers the possibilities that match a search out of a key set too large to
 * turn into possibilities wholesale.  It applies the same rules the
 * breadcrumb view does when it narrows the list: with no search, the first
 * `max_count` keys; otherwise, the `max_count` keys with the best fuzzy-match
 * score.  Keys are only copied into strings once they make the cut.  A key
 * added more than once is returned once, but its copies take up room while
 * collecting, so the result can come up short when duplicates are common.
 */
class possibility_collector {
public:
    explicit possibility_collector(string_fragment search,
                                   size_t max_count = 128);

    void add(string_fragment key);

    /**
     * @return The kept possibilities, best match first, or in the order they
     *   were added when there is no search.
     */
    std::vector<possibility> release();

private:
    struct scored_key {
        int sk_score;
        size_t sk_order;
        std::string sk_key;
    };

    struct worse_first {
        bool operator()(const scored_key& lhs, const scored_key& rhs) const
        {
            if (lhs.sk_score != rhs.sk_score) {
                return lhs.sk_score > rhs.sk_score;
            }
            return lhs.sk_order < rhs.sk_order;
        }
    };

    std::string pc_search;
    size_t pc_max_count;
    std::string pc_key_buf;
    std::vector<scored_key> pc_kept;
    size_t pc_added{0};
};

/**
 * Returns the possibilities for a crumb.
 *
 * @param search The text the user has typed so far.  A provider may use it to
 *   return only the possibilities that match; the caller still ranks and
 *   trims what comes back, so returning more is fine.
 */
using crumb_possibilities
    = std::function<std::vector<possibility>(string_fragment search)>;

struct crumb {
    using key_t = mapbox::util::variant<std::string, size_t>;

    using perform = std::function<void(const key_t& key)>;

    crumb(std::string key,
          attr_line_t al,
          crumb_possibilities cp,
          perform performer)
        : c_key(std::move(key)), c_display_value(std::move(al)),
          c_possibility_provider(std::move(cp)),
          c_performer(std::move(performer))
    {
    }

    crumb(std::string key, crumb_possibilities cp, perform performer)
        : c_key(key), c_display_value(key),
          c_possibility_provider(std::move(cp)),
          c_performer(std::move(performer))
    {
    }

    explicit crumb(size_t index, crumb_possibilities cp, perform performer)
        : c_key(index), c_display_value(fmt::format(FMT_STRING("[{}]"), index)),
          c_possibility_provider(std::move(cp)),
          c_performer(std::move(performer))
    {
    }

    explicit crumb(key_t key, crumb_possibilities cp, perform performer)
        : c_key(key), c_display_value(key.match(
                          [](std::string str) { return str; },
                          [](size_t index) {
                              return fmt::format(FMT_STRING("[{}]"), index);
                          })),
          c_possibility_provider(std::move(cp)),
          c_performer(std::move(performer))
    {
    }

    crumb& with_possible_range(size_t count)
    {
        this->c_possible_range = count;
        if (count == 0) {
            this->c_search_placeholder = "(Array is empty)";
        } else if (count == 1) {
            this->c_search_placeholder = "(Array contains a single element)";
        } else {
            this->c_search_placeholder = fmt::format(
                FMT_STRING("(Enter a number from 0 to {})"), count - 1);
        }
        return *this;
    }

    enum class expected_input_t {
        exact,
        index,
        index_or_exact,
        anything,
    };

    key_t c_key;
    attr_line_t c_display_value;
    crumb_possibilities c_possibility_provider;
    perform c_performer;
    std::optional<size_t> c_possible_range;
    expected_input_t c_expected_input{expected_input_t::exact};
    std::string c_search_placeholder;
};

}  // namespace breadcrumb

#endif
