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

#include <view_curses.hh>

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

static int
dummy_string_handler(void* ctx, const unsigned char* s, size_t len)
{
    return 1;
}

view_colors::
view_colors()
    : vc_dyn_pairs(0)
{
}

view_colors&
view_colors::singleton()
{
    static view_colors vc;
    return vc;
}

block_elem_t
view_colors::wchar_for_icon(ui_icon_t ic) const
{
    return this->vc_icons[lnav::enums::to_underlying(ic)];
}

int
main(int argc, char* argv[])
{
    static const auto TEST_SRC = intern_string::lookup("test_data");

    {
        struct dummy {};

        typed_json_path_container<dummy> dummy_handlers = {

        };

        std::string in1 = "{\"#\":{\"";
        auto parse_res = dummy_handlers.parser_for(TEST_SRC).of(in1);
    }

    {
        static const char UNICODE_BARF[] = "\"\\udb00\\\\0\"\n";

        yajl_callbacks cbs;
        memset(&cbs, 0, sizeof(cbs));
        cbs.yajl_string = dummy_string_handler;
        auto handle = yajl_alloc(&cbs, nullptr, nullptr);
        auto rc = yajl_parse(handle, (const unsigned char*) UNICODE_BARF, 12);
        assert(rc == yajl_status_ok);
        yajl_free(handle);
    }

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

    {
        const char* TEST_INPUT = R"({
    "msg": "Hello, World!",
    "parent1": {
        "child": {}
    },
    "parent2": {
        "child": {"name": "steve"}
    },
    "parent3": {
        "child": {},
        "sibling": {"name": "mongoose"}
    }
})";
        const std::string EXPECTED_OUTPUT
            = "{\"msg\":\"Hello, "
              "World!\",\"parent2\":{\"child\":{\"name\":\"steve\"}},"
              "\"parent3\":{\"sibling\":{\"name\":\"mongoose\"}}}";

        char errbuf[1024];

        auto tree = yajl_tree_parse(TEST_INPUT, errbuf, sizeof(errbuf));
        yajl_cleanup_tree(tree);

        yajlpp_gen gen;

        yajl_gen_tree(gen, tree);
        auto actual = gen.to_string_fragment().to_string();
        assert(EXPECTED_OUTPUT == actual);

        yajl_tree_free(tree);
    }
}
