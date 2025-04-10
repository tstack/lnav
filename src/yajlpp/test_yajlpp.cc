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

#include "base/is_utf8.hh"
#include "yajlpp.hh"
#include "yajlpp_def.hh"

const char* TEST_DATA = R"([{ "foo": 0 }, 2, { "foo": 1 }])";

const char* TEST_UTF_DATA
    = "{\"path\":\""
      "\xd8\xb3\xd8\xa7\xd9\x85\xb3\xd9\x88\xd9\x86\xda\xaf-43\"}";

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
dummy_string_handler(void* ctx,
                     const unsigned char* s,
                     size_t len,
                     yajl_string_props_t*)
{
    return 1;
}

static int
read_json_field(yajlpp_parse_context* ypc,
                const unsigned char* str,
                size_t len,
                yajl_string_props_t*)
{
    auto* paths = (std::vector<intern_string_t>*) ypc->ypc_userdata;
    paths->emplace_back(ypc->get_path());
    return 1;
}

static const json_path_container json_log_handlers = {
    yajlpp::pattern_property_handler(".+").add_cb(read_json_field),
};

int
main(int argc, char* argv[])
{
    {
        static const auto STRING_SRC = intern_string::lookup("string");
        static const auto INPUT = R"({"abc~def": "bar", "abc": "foo"})"_frag;

        std::vector<intern_string_t> paths;
        yajlpp_parse_context ypc(STRING_SRC);
        auto_mem<yajl_handle_t> handle(yajl_free);
        handle = yajl_alloc(&ypc.ypc_callbacks, nullptr, &ypc);
        ypc.with_handle(handle);
        ypc.set_static_handler(json_log_handlers.jpc_children[0]);
        ypc.ypc_userdata = &paths;
        auto rc = ypc.parse_doc(INPUT);
        assert(rc);
        assert(paths[0] == intern_string::lookup("abc~0def"));
        assert(paths[1] == intern_string::lookup("abc"));
    }

    {
        struct test_struct {
            positioned_property<std::string> ts_path;
        };

        typed_json_path_container<test_struct> test_obj_handlers = {
            yajlpp::property_handler("path").for_field(&test_struct::ts_path),
        };

        auto test_utf_res = is_utf8(string_fragment::from_c_str(TEST_UTF_DATA));
        assert(!test_utf_res.is_valid());
        static const auto STRING_SRC = intern_string::lookup("string");
        {
            auto parse_res
                = test_obj_handlers.parser_for(STRING_SRC)
                      .of(string_fragment::from_c_str(TEST_UTF_DATA));
            if (parse_res.isErr()) {
                fprintf(
                    stderr,
                    "parse error: %s\n",
                    parse_res.unwrapErr()[0].to_attr_line().al_string.c_str());
            }
            assert(parse_res.isErr());
        }
        {
            const auto INPUT = R"({"path": "/foo/bar"})"_frag;
            auto parse_res = test_obj_handlers.parser_for(STRING_SRC).of(INPUT);
            assert(parse_res.isOk());
            auto val = parse_res.unwrap();
            printf("val %s\n", val.ts_path.pp_value.c_str());
        }
    }

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
