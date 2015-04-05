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
 * @file bookmarks.cc
 */

#include "config.h"

#include "bookmarks.hh"

using namespace std;

template<typename LineType>
LineType bookmark_vector<LineType>::next(LineType start)
{
    typename bookmark_vector::iterator ub;

    LineType retval(-1);

    require(start >= -1);

    ub = upper_bound(this->begin(), this->end(), start);
    if (ub != this->end()) {
        retval = *ub;
    }

    ensure(retval == -1 || start < retval);

    return retval;
}

template<typename LineType>
LineType bookmark_vector<LineType>::prev(LineType start)
{
    typename bookmark_vector::iterator lb;

    LineType retval(-1);

    require(start >= 0);

    lb = lower_bound(this->begin(), this->end(), start);
    if (lb != this->begin()) {
        lb    -= 1;
        retval = *lb;
    }

    ensure(retval < start);

    return retval;
}

template class bookmark_vector<vis_line_t>;
