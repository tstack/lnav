/**
 * Copyright (c) 2007-2012, Timothy Stack
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
 * @file bookmarks.hh
 */

#ifndef bookmarks_hh
#define bookmarks_hh

#include <algorithm>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

#include "base/intern_string.hh"
#include "base/lnav_log.hh"

struct logmsg_annotations {
    std::map<std::string, std::string> la_pairs;
};

struct bookmark_metadata {
    static std::unordered_set<std::string> KNOWN_TAGS;

    enum class categories : int {
        any = 0,
        partition = 0x01,
        notes = 0x02,
        opid = 0x04,
    };

    bool has(categories props) const
    {
        if (props == categories::any) {
            return true;
        }

        if (props == categories::partition && !this->bm_name.empty()) {
            return true;
        }

        if (props == categories::notes
            && (!this->bm_comment.empty()
                || !this->bm_annotations.la_pairs.empty()
                || !this->bm_tags.empty()))
        {
            return true;
        }

        if (props == categories::opid && !this->bm_opid.empty()) {
            return true;
        }

        return false;
    }

    std::string bm_name;
    std::string bm_opid;
    std::string bm_comment;
    logmsg_annotations bm_annotations;
    std::vector<std::string> bm_tags;

    void add_tag(const std::string& tag);

    bool remove_tag(const std::string& tag);

    bool empty(categories props) const;

    void clear();
};

/**
 * Extension of the STL vector that is used to store bookmarks for
 * files being viewed, where a bookmark is just a particular line in
 * the file(s).  The value-added over the standard vector are some
 * methods for doing content-wise iteration.  In other words, given a
 * value that may or may not be in the vector, find the next or
 * previous value that is in the vector.
 *
 * @param LineType The type used to store line numbers.  (e.g.
 *   vis_line_t or content_line_t)
 *
 * @note The vector is expected to be sorted.
 */
template<typename LineType>
class bookmark_vector : public std::vector<LineType> {
    using base_vector = std::vector<LineType>;

public:
    using size_type = typename base_vector::size_type;
    using iterator = typename base_vector::iterator;
    using const_iterator = typename base_vector::const_iterator;

    /**
     * Insert a bookmark into this vector, but only if it is not already in the
     * vector.
     *
     * @param vl The line to bookmark.
     */
    iterator insert_once(LineType vl)
    {
        iterator retval;

        require(vl >= 0);

        auto lb = std::lower_bound(this->begin(), this->end(), vl);
        if (lb == this->end() || *lb != vl) {
            this->insert(lb, vl);
            retval = this->end();
        } else {
            retval = lb;
        }

        return retval;
    }

    std::pair<iterator, iterator> equal_range(LineType start, LineType stop)
    {
        auto lb = std::lower_bound(this->begin(), this->end(), start);

        if (stop == LineType(-1)) {
            return std::make_pair(lb, this->end());
        }

        auto up = std::upper_bound(this->begin(), this->end(), stop);

        return std::make_pair(lb, up);
    }

    /**
     * @param start The value to start the search for the next bookmark.
     * @return The next bookmark value in the vector or -1 if there are
     * no more remaining bookmarks.  If the 'start' value is a bookmark,
     * the next bookmark is returned.  If the 'start' value is not a
     * bookmark, the next highest value in the vector is returned.
     */
    std::optional<LineType> next(LineType start) const;

    /**
     * @param start The value to start the search for the previous
     * bookmark.
     * @return The previous bookmark value in the vector or -1 if there
     * are no more prior bookmarks.
     * @see next
     */
    std::optional<LineType> prev(LineType start) const;
};

/**
 * Dummy type whose instances are used to distinguish between
 * bookmarks maintained by different source modules.
 */
class bookmark_type_t {
public:
    using type_iterator = std::vector<bookmark_type_t*>::iterator;

    static type_iterator type_begin() { return get_all_types().begin(); }

    static type_iterator type_end() { return get_all_types().end(); }

    static std::optional<bookmark_type_t*> find_type(const std::string& name);

    static std::vector<bookmark_type_t*>& get_all_types();

    template<typename T, std::size_t N>
    explicit bookmark_type_t(const T (&name)[N])
        : bt_name(string_fragment::from_const(name))
    {
        get_all_types().push_back(this);
    }

    const string_fragment& get_name() const { return this->bt_name; }

private:
    const string_fragment bt_name;
};

template<typename LineType>
std::optional<LineType>
bookmark_vector<LineType>::next(LineType start) const
{
    std::optional<LineType> retval;

    require(start >= -1);

    auto ub = std::upper_bound(this->cbegin(), this->cend(), start);
    if (ub != this->cend()) {
        retval = *ub;
    }

    ensure(!retval || start < retval.value());

    return retval;
}

template<typename LineType>
std::optional<LineType>
bookmark_vector<LineType>::prev(LineType start) const
{
    std::optional<LineType> retval;

    require(start >= 0);

    auto lb = std::lower_bound(this->cbegin(), this->cend(), start);
    if (lb != this->cbegin()) {
        lb -= 1;
        retval = *lb;
    }

    ensure(!retval || retval.value() < start);

    return retval;
}

/**
 * Map of bookmark types to bookmark vectors.
 */
template<typename LineType>
struct bookmarks {
    using type = std::map<const bookmark_type_t*, bookmark_vector<LineType>>;
};

#endif
