/**
 * @file bookmarks.cc
 */

#include "bookmarks.hh"

vis_line_t bookmark_vector::next(vis_line_t start)
{
    std::vector<vis_line_t>::iterator ub;

    vis_line_t retval(-1);

    assert(start >= -1);

    ub = upper_bound(this->begin(), this->end(), start);
    if (ub != this->end()) {
	retval = *ub;
    }

    assert(retval == -1 || start < retval);

    return retval;
}

vis_line_t bookmark_vector::prev(vis_line_t start)
{
    std::vector<vis_line_t>::iterator lb;

    vis_line_t retval(-1);

    assert(start >= 0);

    lb = lower_bound(this->begin(), this->end(), start);
    if (lb != this->begin()) {
	lb    -= 1;
	retval = *lb;
    }

    assert(retval < start);

    return retval;
}
