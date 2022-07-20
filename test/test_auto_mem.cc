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

#include <algorithm>

#include <assert.h>
#include <stdlib.h>

#include "base/auto_mem.hh"
#include "config.h"

struct my_data {
    int dummy1;
    int dummy2;
};

int free_count;
void* last_free;

void
my_free(void* mem)
{
    free_count += 1;
    last_free = mem;
}

int
main(int argc, char* argv[])
{
    int retval = EXIT_SUCCESS;
    auto_mem<struct my_data, my_free> md1, md2;
    struct my_data md1_val, md2_val;

    md1 = &md1_val;
    assert(free_count == 0);
    md1 = std::move(md2);
    assert(free_count == 1);
    assert(last_free == &md1_val);
    assert(md1 == NULL);

    md1 = &md2_val;
    assert(free_count == 1);
    assert(last_free == &md1_val);
    *md1.out() = &md1_val;
    assert(free_count == 2);
    assert(last_free == &md2_val);
    assert(md1.in() == &md1_val);

    {
        auto_mem<struct my_data, my_free> md_cp(std::move(md1));

        assert(md1 == NULL);
        assert(free_count == 2);
        assert(md_cp == &md1_val);
    }

    assert(free_count == 3);
    assert(last_free == &md1_val);

    {
        static const char* msg = "Hello, World!\nGoodbye, World!\nTest";

        auto buf = auto_buffer::from(msg, strlen(msg));
        auto first_lf = std::find(buf.begin(), buf.end(), '\n');
        auto last_lf = std::find(buf.rbegin(), buf.rend(), '\n');

        assert(std::distance(buf.begin(), first_lf) == 13);
        assert(*first_lf == '\n');
        assert(*last_lf == '\n');
        auto last_lf_index = std::distance(last_lf, buf.rend()) - 1;
        auto* last_lf_rchr = strrchr(msg, '\n');
        assert(last_lf_index == (last_lf_rchr - msg));
    }

    {
        auto bitmap = auto_buffer::alloc_bitmap(15);

        assert(bitmap.capacity() == 2);
        bitmap.resize_bitmap(15);
        assert(bitmap.size() == 2);

        memset(bitmap.in(), 0, bitmap.size());
        for (size_t lpc = 0; lpc < 15; lpc++) {
            assert(!bitmap.is_bit_set(lpc));
        }
    }

    return retval;
}
