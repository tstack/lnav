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
 * @file json_op.cc
 */

#include "json_op.hh"

#include "base/lnav_log.hh"
#include "config.h"
#include "yajl/api/yajl_gen.h"

static int
gen_handle_start_map(void* ctx)
{
    json_op* jo = (json_op*) ctx;
    yajl_gen gen = (yajl_gen) jo->jo_ptr_data;

    jo->jo_ptr_error_code = yajl_gen_map_open(gen);

    return jo->jo_ptr_error_code == yajl_gen_status_ok;
}

static int
gen_handle_map_key(void* ctx, const unsigned char* key, size_t len)
{
    json_op* jo = (json_op*) ctx;
    yajl_gen gen = (yajl_gen) jo->jo_ptr_data;

    jo->jo_ptr_error_code = yajl_gen_string(gen, key, len);

    return jo->jo_ptr_error_code == yajl_gen_status_ok;
}

static int
gen_handle_end_map(void* ctx)
{
    json_op* jo = (json_op*) ctx;
    yajl_gen gen = (yajl_gen) jo->jo_ptr_data;

    jo->jo_ptr_error_code = yajl_gen_map_close(gen);

    return jo->jo_ptr_error_code == yajl_gen_status_ok;
}

static int
gen_handle_null(void* ctx)
{
    json_op* jo = (json_op*) ctx;
    yajl_gen gen = (yajl_gen) jo->jo_ptr_data;

    jo->jo_ptr_error_code = yajl_gen_null(gen);

    return jo->jo_ptr_error_code == yajl_gen_status_ok;
}

static int
gen_handle_boolean(void* ctx, int boolVal)
{
    json_op* jo = (json_op*) ctx;
    yajl_gen gen = (yajl_gen) jo->jo_ptr_data;

    jo->jo_ptr_error_code = yajl_gen_bool(gen, boolVal);

    return jo->jo_ptr_error_code == yajl_gen_status_ok;
}

static int
gen_handle_number(void* ctx, const char* numberVal, size_t numberLen)
{
    json_op* jo = (json_op*) ctx;
    yajl_gen gen = (yajl_gen) jo->jo_ptr_data;

    jo->jo_ptr_error_code = yajl_gen_number(gen, numberVal, numberLen);

    return jo->jo_ptr_error_code == yajl_gen_status_ok;
}

static int
gen_handle_string(void* ctx, const unsigned char* stringVal, size_t len, yajl_string_props_t*)
{
    json_op* jo = (json_op*) ctx;
    yajl_gen gen = (yajl_gen) jo->jo_ptr_data;

    jo->jo_ptr_error_code = yajl_gen_string(gen, stringVal, len);

    return jo->jo_ptr_error_code == yajl_gen_status_ok;
}

static int
gen_handle_start_array(void* ctx)
{
    json_op* jo = (json_op*) ctx;
    yajl_gen gen = (yajl_gen) jo->jo_ptr_data;

    jo->jo_ptr_error_code = yajl_gen_array_open(gen);

    return jo->jo_ptr_error_code == yajl_gen_status_ok;
}

static int
gen_handle_end_array(void* ctx)
{
    json_op* jo = (json_op*) ctx;
    yajl_gen gen = (yajl_gen) jo->jo_ptr_data;

    jo->jo_ptr_error_code = yajl_gen_array_close(gen);

    return jo->jo_ptr_error_code == yajl_gen_status_ok;
}

const yajl_callbacks json_op::gen_callbacks = {
    gen_handle_null,
    gen_handle_boolean,
    nullptr,
    nullptr,
    gen_handle_number,
    gen_handle_string,
    gen_handle_start_map,
    gen_handle_map_key,
    gen_handle_end_map,
    gen_handle_start_array,
    gen_handle_end_array,
};

const yajl_callbacks json_op::ptr_callbacks = {
    handle_null,
    handle_boolean,
    nullptr,
    nullptr,
    handle_number,
    handle_string,
    handle_start_map,
    handle_map_key,
    handle_end_map,
    handle_start_array,
    handle_end_array,
};

int
json_op::handle_null(void* ctx)
{
    json_op* jo = (json_op*) ctx;
    int retval = 1;

    if (jo->check_index()) {
        if (jo->jo_ptr_callbacks.yajl_null != nullptr) {
            retval = jo->jo_ptr_callbacks.yajl_null(ctx);
        }
    }

    return retval;
}

int
json_op::handle_boolean(void* ctx, int boolVal)
{
    json_op* jo = (json_op*) ctx;
    int retval = 1;

    if (jo->check_index()) {
        if (jo->jo_ptr_callbacks.yajl_boolean != nullptr) {
            retval = jo->jo_ptr_callbacks.yajl_boolean(ctx, boolVal);
        }
    }

    return retval;
}

int
json_op::handle_number(void* ctx, const char* numberVal, size_t numberLen)
{
    json_op* jo = (json_op*) ctx;
    int retval = 1;

    if (jo->check_index()) {
        if (jo->jo_ptr_callbacks.yajl_number != nullptr) {
            retval
                = jo->jo_ptr_callbacks.yajl_number(ctx, numberVal, numberLen);
        }
    }

    return retval;
}

int
json_op::handle_string(void* ctx,
                       const unsigned char* stringVal,
                       size_t stringLen,
                       yajl_string_props_t* props)
{
    json_op* jo = (json_op*) ctx;
    int retval = 1;

    if (jo->check_index()) {
        if (jo->jo_ptr_callbacks.yajl_string != nullptr) {
            retval
                = jo->jo_ptr_callbacks.yajl_string(ctx, stringVal, stringLen, props);
        }
    }

    return retval;
}

int
json_op::handle_start_map(void* ctx)
{
    json_op* jo = (json_op*) ctx;
    int retval = 1;

    if (jo->check_index(false)) {
        if (jo->jo_ptr_callbacks.yajl_start_map != nullptr) {
            retval = jo->jo_ptr_callbacks.yajl_start_map(ctx);
        }
    }

    if (!jo->jo_ptr.expect_map(jo->jo_depth, jo->jo_array_index)) {
        retval = 0;
    }

    return retval;
}

int
json_op::handle_map_key(void* ctx, const unsigned char* key, size_t len)
{
    json_op* jo = (json_op*) ctx;
    int retval = 1;

    if (jo->check_index(false)) {
        if (jo->jo_ptr_callbacks.yajl_map_key != nullptr) {
            retval = jo->jo_ptr_callbacks.yajl_map_key(ctx, key, len);
        }
    }

    if (!jo->jo_ptr.at_key(jo->jo_depth, (const char*) key, len)) {
        retval = 0;
    }

    return retval;
}

int
json_op::handle_end_map(void* ctx)
{
    json_op* jo = (json_op*) ctx;
    int retval = 1;

    if (jo->check_index()) {
        if (jo->jo_ptr_callbacks.yajl_end_map != nullptr) {
            retval = jo->jo_ptr_callbacks.yajl_end_map(ctx);
        }
    }

    jo->jo_ptr.exit_container(jo->jo_depth, jo->jo_array_index);

    return retval;
}

int
json_op::handle_start_array(void* ctx)
{
    json_op* jo = (json_op*) ctx;
    int retval = 1;

    if (jo->check_index(false)) {
        if (jo->jo_ptr_callbacks.yajl_start_array != nullptr) {
            retval = jo->jo_ptr_callbacks.yajl_start_array(ctx);
        }
    }

    jo->jo_ptr.expect_array(jo->jo_depth, jo->jo_array_index);

    return retval;
}

int
json_op::handle_end_array(void* ctx)
{
    json_op* jo = (json_op*) ctx;
    int retval = 1;

    if (jo->check_index()) {
        if (jo->jo_ptr_callbacks.yajl_end_array != nullptr) {
            retval = jo->jo_ptr_callbacks.yajl_end_array(ctx);
        }
    }

    jo->jo_ptr.exit_container(jo->jo_depth, jo->jo_array_index);

    return retval;
}
