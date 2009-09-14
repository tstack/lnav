/**
 * @file bookmarks.hh
 */

#ifndef __bookmarks_hh
#define __bookmarks_hh

#include <assert.h>

#include <map>
#include <vector>
#include <algorithm>

#include "listview_curses.hh"

/**
 * Extension of the STL vector that is used to store bookmarks for
 * files being viewed, where a bookmark is just a particular line in
 * the file(s).  The value-added over the standard vector are some
 * methods for doing content-wise iteration.  In other words, given a
 * value that may or may not be in the vector, find the next or
 * previous value that is in the vector.
 *
 * @note The vector is expected to be sorted.
 */
class bookmark_vector
    : public std::vector<vis_line_t> {
public:

    /**
     * Insert a bookmark into this vector, but only if it is not already in the
     * vector.
     *
     * @param vl The line to bookmark.
     */
    iterator insert_once(vis_line_t vl)
    {
	iterator lb, retval;

	assert(vl >= 0);

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
    vis_line_t next(vis_line_t start);

    /**
     * @param start The value to start the search for the previous
     * bookmark.
     * @return The previous bookmark value in the vector or -1 if there
     * are no more prior bookmarks.
     * @see next
     */
    vis_line_t prev(vis_line_t start);
};

/**
 * Dummy type whose instances are used to distinguish between
 * bookmarks maintained by different source modules.
 */
class bookmark_type_t { };

/**
 * Map of bookmark types to bookmark vectors.
 */
typedef std::map<bookmark_type_t *, bookmark_vector> bookmarks;

#endif
