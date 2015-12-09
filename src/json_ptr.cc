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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file json_ptr.cc
 */

#include "config.h"

#include "json_ptr.hh"

using namespace std;

static int handle_null(void *ctx)
{
    json_ptr_walk *jpw = (json_ptr_walk *)ctx;

    jpw->jpw_values.push_back(
            json_ptr_walk::walk_triple(jpw->current_ptr(), yajl_t_null, "null"));
    jpw->inc_array_index();

    return 1;
}

static int handle_boolean(void *ctx, int boolVal)
{
    json_ptr_walk *jpw = (json_ptr_walk *)ctx;

    jpw->jpw_values.push_back(
            json_ptr_walk::walk_triple(jpw->current_ptr(),
                                       boolVal ? yajl_t_true : yajl_t_false,
                                       boolVal ? "true" : "false"));
    jpw->inc_array_index();

    return 1;
}

static int handle_number(void *ctx, const char *numberVal, size_t numberLen)
{
    json_ptr_walk *jpw = (json_ptr_walk *)ctx;

    jpw->jpw_values.push_back(
            json_ptr_walk::walk_triple(jpw->current_ptr(),
                                       yajl_t_number,
                                       string(numberVal, numberLen)));
    jpw->inc_array_index();

    return 1;
}

static void appender(void *ctx, const char *strVal, size_t strLen)
{
    string &str = *(string *)ctx;

    str.append(strVal, strLen);
}

static int handle_string(void *ctx, const unsigned char * stringVal, size_t len)
{
    json_ptr_walk *jpw = (json_ptr_walk *)ctx;
    auto_mem<yajl_gen_t> gen(yajl_gen_free);
    string str;

    gen = yajl_gen_alloc(NULL);
    yajl_gen_config(gen.in(), yajl_gen_print_callback, appender, &str);
    yajl_gen_string(gen.in(), stringVal, len);
    jpw->jpw_values.push_back(
            json_ptr_walk::walk_triple(jpw->current_ptr(), yajl_t_string, str));
    jpw->inc_array_index();

    return 1;
}

static int handle_start_map(void *ctx)
{
    json_ptr_walk *jpw = (json_ptr_walk *)ctx;

    jpw->jpw_keys.push_back("");
    jpw->jpw_array_indexes.push_back(-1);

    return 1;
}

static int handle_map_key(void *ctx, const unsigned char * key, size_t len)
{
    json_ptr_walk *jpw = (json_ptr_walk *)ctx;
    char partially_encoded_key[len + 32];
    size_t required_len;

    jpw->jpw_keys.pop_back();

    required_len = json_ptr::encode(partially_encoded_key, sizeof(partially_encoded_key),
        (const char *)key, len);
    if (required_len < sizeof(partially_encoded_key)) {
        jpw->jpw_keys.push_back(string(partially_encoded_key, required_len));
    }
    else {
        char fully_encoded_key[required_len];

        json_ptr::encode(fully_encoded_key, sizeof(fully_encoded_key),
            (const char *)key, len);
        jpw->jpw_keys.push_back(string(fully_encoded_key, required_len));
    }

    return 1;
}

static int handle_end_map(void *ctx)
{
    json_ptr_walk *jpw = (json_ptr_walk *)ctx;

    jpw->jpw_keys.pop_back();
    jpw->jpw_array_indexes.pop_back();

    jpw->inc_array_index();

    return 1;
}

static int handle_start_array(void *ctx)
{
    json_ptr_walk *jpw = (json_ptr_walk *)ctx;

    jpw->jpw_keys.push_back("");
    jpw->jpw_array_indexes.push_back(0);

    return 1;
}

static int handle_end_array(void *ctx)
{
    json_ptr_walk *jpw = (json_ptr_walk *)ctx;

    jpw->jpw_keys.pop_back();
    jpw->jpw_array_indexes.pop_back();
    jpw->inc_array_index();

    return 1;
}

const yajl_callbacks json_ptr_walk::callbacks = {
    handle_null,
    handle_boolean,
    NULL,
    NULL,
    handle_number,
    handle_string,
    handle_start_map,
    handle_map_key,
    handle_end_map,
    handle_start_array,
    handle_end_array
};
