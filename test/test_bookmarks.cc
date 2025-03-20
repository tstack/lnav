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
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "bookmarks.hh"
#include "config.h"
#include "textview_curses.hh"

int
main(int argc, char* argv[])
{
    int lpc, retval = EXIT_SUCCESS;
    bookmark_vector<vis_line_t> bv, bv_cp;

    bv.insert_once(vis_line_t(2));
    bv.insert_once(vis_line_t(2));
    assert(bv.size() == 1);

    bv.insert_once(vis_line_t(4));
    bv.insert_once(vis_line_t(3));
#if 0
    assert(bv[0] == 2);
    assert(bv[1] == 3);
    assert(bv[2] == 4);
#endif

    {
        auto range = bv.equal_range(0_vl, 5_vl);

        assert(range.first != range.second);
        assert(*range.first == 2_vl);
        ++range.first;
        assert(range.first != range.second);
        assert(*range.first == 3_vl);
        ++range.first;
        assert(range.first != range.second);
        assert(*range.first == 4_vl);
        ++range.first;
        assert(range.first == range.second);
    }

    {
        auto range = bv.equal_range(0_vl, 1_vl);

        assert(range.first == range.second);
    }

    {
        auto range = bv.equal_range(10_vl, 10_vl);

        assert(range.first == range.second);
    }

    bv.clear();
    assert(!bv.next(vis_line_t(0)));
    assert(!bv.prev(vis_line_t(0)));
    assert(!bv.next(vis_line_t(100)));
    assert(!bv.prev(vis_line_t(100)));

    bv.insert_once(vis_line_t(2));

    assert(bv.next(vis_line_t(0)).value() == 2);
    assert(!bv.next(vis_line_t(2)));
    assert(!bv.next(vis_line_t(3)));

    assert(bv.prev(vis_line_t(3)).value() == 2);
    assert(!bv.prev(vis_line_t(2)));

    bv.insert_once(vis_line_t(4));

    assert(bv.next(vis_line_t(0)).value() == 2);
    assert(bv.next(vis_line_t(2)).value() == 4);
    assert(bv.next(vis_line_t(3)).value() == 4);
    assert(!bv.next(vis_line_t(4)));

    assert(bv.prev(vis_line_t(10)).value() == 4);
    assert(bv.prev(vis_line_t(5)).value() == 4);
    assert(bv.prev(vis_line_t(4)).value() == 2);
    assert(!bv.prev(vis_line_t(2)));

    bv.clear();

    const int LINE_COUNT = 10000;

    for (lpc = 0; lpc < 1000; lpc++) {
        bv.insert_once(vis_line_t(random() % LINE_COUNT));
    }
    bv_cp = bv;

    {
        vis_line_t last_line(-1);

        for (lpc = 0; lpc != -1; lpc = bv.next(vis_line_t(lpc)).value_or(-1_vl))
        {
            assert(lpc >= 0);
            assert(lpc < LINE_COUNT);
            assert(last_line < lpc);

            last_line = vis_line_t(lpc);
        }

        last_line = vis_line_t(10000);
        for (lpc = LINE_COUNT - 1; lpc != -1;
             lpc = bv.prev(vis_line_t(lpc)).value_or(-1_vl))
        {
            assert(lpc >= 0);
            assert(lpc < LINE_COUNT);
            assert(last_line > lpc);

            last_line = vis_line_t(lpc);
        }
    }

    return retval;
}
