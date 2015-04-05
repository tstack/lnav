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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file bookmarks.hh
 */

#ifndef __bookmarks_hh
#define __bookmarks_hh

#include <map>
#include <string>
#include <vector>
#include <algorithm>

#include "lnav_log.hh"
#include "listview_curses.hh"

struct bookmark_metadata {
    std::string bm_name;
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
public:

    /**
     * Insert a bookmark into this vector, but only if it is not already in the
     * vector.
     *
     * @param vl The line to bookmark.
     */
    typename bookmark_vector::iterator insert_once(LineType vl)
    {
        typename bookmark_vector::iterator lb, retval;

        require(vl >= 0);

        lb = std::lower_bound(this->begin(), this->end(), vl);
        if (lb == this->end() || *lb != vl) {
            this->insert(lb, vl);
            retval = this->end();
        }
        else {
            retval = lb;
        }

        return retval;
    };

    /**
     * @param start The value to start the search for the next bookmark.
     * @return The next bookmark value in the vector or -1 if there are
     * no more remaining bookmarks.  If the 'start' value is a bookmark,
     * the next bookmark is returned.  If the 'start' value is not a
     * bookmark, the next highest value in the vector is returned.
     */
    LineType next(LineType start);

    /**
     * @param start The value to start the search for the previous
     * bookmark.
     * @return The previous bookmark value in the vector or -1 if there
     * are no more prior bookmarks.
     * @see next
     */
    LineType prev(LineType start);
};

/**
 * Dummy type whose instances are used to distinguish between
 * bookmarks maintained by different source modules.
 */
class bookmark_type_t {
public:
    typedef std::vector<bookmark_type_t *>::iterator type_iterator;

    static type_iterator type_begin() {
        return get_all_types().begin();
    };

    static type_iterator type_end() {
        return get_all_types().end();
    };

    static bookmark_type_t *find_type(const std::string &name) {
        type_iterator iter = find_if(type_begin(), type_end(), mark_eq(name));
        bookmark_type_t *retval = NULL;

        if (iter != type_end()) {
            retval = (*iter);
        }
        return retval;
    };

    static std::vector<bookmark_type_t *> &get_all_types() {
        static std::vector<bookmark_type_t *> all_types;

        return all_types;
    };

    bookmark_type_t(const std::string &name) : bt_name(name) {
        get_all_types().push_back(this);
    };

    const std::string &get_name() const {
        return this->bt_name;
    };

private:
    struct mark_eq {
        mark_eq(const std::string &name) : me_name(name) { };

        bool operator()(bookmark_type_t *bt) {
            return bt->bt_name == this->me_name;
        };

        const std::string &me_name;
    };

    const std::string bt_name;
};

/**
 * Map of bookmark types to bookmark vectors.
 */
template<typename LineType>
struct bookmarks {
    typedef std::map<bookmark_type_t *, bookmark_vector<LineType> > type;
};

typedef bookmarks<vis_line_t>::type
    vis_bookmarks;
#endif
