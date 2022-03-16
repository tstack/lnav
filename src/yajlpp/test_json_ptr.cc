/**
 * Copyright (c) 2014, Timothy Stack
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
 * THIS SOFTWARE IS PROVIDED BY TIMOTHY STACK AND CONTRIBUTORS ''AS IS'' AND ANY
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
 * @file test_json_ptr.cc
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "yajlpp/json_ptr.hh"

int
main(int argc, const char* argv[])
{
    int32_t depth, index;

    {
        json_ptr jptr("");

        depth = 0;
        index = -1;
        assert(jptr.at_index(depth, index));
    }

    {
        json_ptr jptr("/");

        depth = 0;
        index = -1;
        assert(!jptr.at_index(depth, index));
        assert(jptr.expect_map(depth, index));
        assert(jptr.at_index(depth, index));
    }

    {
        json_ptr jptr("/foo/bar");

        depth = 0;
        index = -1;
        assert(jptr.expect_map(depth, index));
        assert(jptr.at_key(depth, "foo"));
        assert(jptr.expect_map(depth, index));
        assert(jptr.at_key(depth, "bar"));
        assert(jptr.at_index(depth, index));
    }
}
