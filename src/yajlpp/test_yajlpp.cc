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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file test_yajlpp.cc
 */

#include <assert.h>
#include <stdio.h>

#include "yajlpp.hh"
#include "yajlpp_def.hh"

const char* TEST_DATA = R"([{ "foo": 0 }, 2, { "foo": 1 }])";

const char* TEST_OBJ_DATA = "{ \"foo\": 0 }";

static int FOO_COUNT = 0;
static int CONST_COUNT = 0;

static int
read_foo(yajlpp_parse_context* ypc, long long value)
{
    assert(value == FOO_COUNT);
    assert(ypc->ypc_array_index.empty()
           || ypc->ypc_array_index.back() == FOO_COUNT);

    FOO_COUNT += 1;

    return 1;
}

static int
read_const(yajlpp_parse_context* ypc, long long value)
{
    CONST_COUNT += 1;

    return 1;
}

int
main(int argc, char* argv[])
{
    static const auto TEST_SRC = intern_string::lookup("test_data");

    struct json_path_container test_obj_handler = {
        json_path_handler("foo", read_foo),
    };

    {
        struct json_path_container test_array_handlers = {
            json_path_handler("#")
                .add_cb(read_const)
                .with_children(test_obj_handler),
        };

        yajlpp_parse_context ypc(TEST_SRC, &test_array_handlers);
        auto handle = yajl_alloc(&ypc.ypc_callbacks, nullptr, &ypc);
        ypc.with_handle(handle);
        ypc.parse((const unsigned char*) TEST_DATA, strlen(TEST_DATA));
        yajl_free(handle);

        assert(FOO_COUNT == 2);
        assert(CONST_COUNT == 1);
    }

    {
        FOO_COUNT = 0;

        yajlpp_parse_context ypc(TEST_SRC, &test_obj_handler);
        auto handle = yajl_alloc(&ypc.ypc_callbacks, nullptr, &ypc);
        ypc.with_handle(handle);

        ypc.parse(reinterpret_cast<const unsigned char*>(TEST_OBJ_DATA),
                  strlen(TEST_OBJ_DATA));
        yajl_free(handle);

        assert(FOO_COUNT == 1);
    }
}
