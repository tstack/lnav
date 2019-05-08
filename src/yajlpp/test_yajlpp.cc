/**
 * Copyright (c) 2013, Timothy Stack
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
 * @file test_yajlpp.cc
 */

#include "config.h"

#include <stdio.h>
#include <assert.h>

#include "yajlpp/yajlpp.hh"
#include "yajlpp/yajlpp_def.hh"

const char *TEST_DATA =
    "[{ \"foo\": 0 }, { \"foo\": 1 }]";

static int FOO_COUNT = 0;

static int read_foo(yajlpp_parse_context *ypc, long long value)
{
    assert(value == FOO_COUNT);
    assert(ypc->ypc_array_index.back() == FOO_COUNT);

    FOO_COUNT += 1;

    return 1;
}

int main(int argc, char *argv[])
{
    struct json_path_handler test_handlers[] = {
        json_path_handler("#/foo", read_foo),

        json_path_handler()
    };

    yajlpp_parse_context ypc("test_data", test_handlers);
    yajl_handle handle;

    handle = yajl_alloc(&ypc.ypc_callbacks, NULL, &ypc);
    yajl_parse(handle, (const unsigned char *)TEST_DATA, strlen(TEST_DATA));
    yajl_complete_parse(handle);
    yajl_free(handle);

    assert(FOO_COUNT == 2);
}
