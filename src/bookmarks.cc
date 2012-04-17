/**
 * @file bookmarks.cc
 */

#include "config.h"

#include "bookmarks.hh"

template<typename LineType>
LineType bookmark_vector<LineType>::next(LineType start)
{
    typename bookmark_vector::iterator ub;

    LineType retval(-1);

    assert(start >= -1);

    ub = upper_bound(this->begin(), this->end(), start);
    if (ub != this->end()) {
	retval = *ub;
    }

    assert(retval == -1 || start < retval);

    return retval;
}

template<typename LineType>
LineType bookmark_vector<LineType>::prev(LineType start)
{
    typename bookmark_vector::iterator lb;

    LineType retval(-1);

    assert(start >= 0);

    lb = lower_bound(this->begin(), this->end(), start);
    if (lb != this->begin()) {
	lb    -= 1;
	retval = *lb;
    }

    assert(retval < start);

    return retval;
}

template class bookmark_vector<vis_line_t>;

