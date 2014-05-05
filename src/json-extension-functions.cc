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
 * @file json-extension-functions.cc
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <stdint.h>

#include <string>

#include "sqlite3.h"

#include "json_op.hh"

#include "sqlite-extension-func.h"

using namespace std;

class sql_json_op : public json_op {
public:
    sql_json_op(json_ptr &ptr) : json_op(ptr), sjo_type(-1) { };

    int sjo_type;
    string sjo_str;
    int sjo_int;
};

static void printer(void *ctx, const char *numberVal, size_t numberLen)
{
    string &str = *(string *)ctx;

    str.append(numberVal, numberLen);
}

static void null_or_default(sqlite3_context *context, int argc, sqlite3_value **argv)
{
    if (argc > 2) {
        sqlite3_result_value(context, argv[2]);
    }
    else {
        sqlite3_result_null(context);
    }
}

static int gen_handle_null(void *ctx)
{
    sql_json_op *sjo = (sql_json_op *)ctx;
    yajl_gen gen = (yajl_gen)sjo->jo_ptr_data;

    if (sjo->jo_ptr.jp_state == json_ptr::MS_DONE) {
        sjo->sjo_type = SQLITE_NULL;
    }
    else {
        sjo->jo_ptr_error_code = yajl_gen_null(gen);
    }

    return sjo->jo_ptr_error_code == yajl_gen_status_ok;
}

static int gen_handle_boolean(void *ctx, int boolVal)
{
    sql_json_op *sjo = (sql_json_op *)ctx;
    yajl_gen gen = (yajl_gen)sjo->jo_ptr_data;

    if (sjo->jo_ptr.jp_state == json_ptr::MS_DONE) {
        sjo->sjo_type = SQLITE_INTEGER;
        sjo->sjo_int = boolVal;
    }
    else {
        sjo->jo_ptr_error_code = yajl_gen_bool(gen, boolVal);
    }

    return sjo->jo_ptr_error_code == yajl_gen_status_ok;
}

static int gen_handle_string(void *ctx, const unsigned char * stringVal, size_t len)
{
    sql_json_op *sjo = (sql_json_op *)ctx;
    yajl_gen gen = (yajl_gen)sjo->jo_ptr_data;

    if (sjo->jo_ptr.jp_state == json_ptr::MS_DONE) {
        sjo->sjo_type = SQLITE3_TEXT;
        sjo->sjo_str = string((char *)stringVal, len);
    }
    else {
        sjo->jo_ptr_error_code = yajl_gen_string(gen, stringVal, len);
    }

    return sjo->jo_ptr_error_code == yajl_gen_status_ok;
}

static void sql_jget(sqlite3_context *context,
                     int argc, sqlite3_value **argv)
{
    if (argc < 2) {
        sqlite3_result_error(context, "expecting JSON value and pointer", -1);
        return;
    }

    if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
        null_or_default(context, argc, argv);
        return;
    }

    const char *json_in = (const char *)sqlite3_value_text(argv[0]);

    if (sqlite3_value_type(argv[1]) == SQLITE_NULL) {
        sqlite3_result_text(context, json_in, -1, SQLITE_TRANSIENT);
        return;
    }

    const char *ptr_in = (const char *)sqlite3_value_text(argv[1]);
    json_ptr jp(ptr_in);
    sql_json_op jo(jp);
    auto_mem<yajl_gen_t> gen(yajl_gen_free);
    auto_mem<yajl_handle_t> handle(yajl_free);
    const unsigned char *err;
    string result;

    gen = yajl_gen_alloc(NULL);
    yajl_gen_config(gen.in(), yajl_gen_print_callback, printer, &result);
    yajl_gen_config(gen.in(), yajl_gen_beautify, false);

    jo.jo_ptr_callbacks = json_op::gen_callbacks;
    jo.jo_ptr_callbacks.yajl_null = gen_handle_null;
    jo.jo_ptr_callbacks.yajl_boolean = gen_handle_boolean;
    jo.jo_ptr_callbacks.yajl_string = gen_handle_string;
    jo.jo_ptr_data = gen.in();

    handle.reset(yajl_alloc(&json_op::ptr_callbacks, NULL, &jo));
    switch (yajl_parse(handle.in(), (const unsigned char *)json_in, strlen(json_in))) {
    case yajl_status_error:
        err = yajl_get_error(handle.in(), 0, (const unsigned char *)json_in, strlen(json_in));
        sqlite3_result_error(context, (const char *)err, -1);
        return;
    case yajl_status_client_canceled:
        if (jo.jo_ptr.jp_state == json_ptr::MS_ERR_INVALID_ESCAPE) {
            sqlite3_result_error(context, jo.jo_ptr.error_msg().c_str(), -1);
        }
        else {
            null_or_default(context, argc, argv);
        }
        return;
    default:
        break;
    }

    switch (yajl_complete_parse(handle.in())) {
    case yajl_status_error:
        err = yajl_get_error(handle.in(), 0, (const unsigned char *)json_in, strlen(json_in));
        sqlite3_result_error(context, (const char *)err, -1);
        return;
    case yajl_status_client_canceled:
        if (jo.jo_ptr.jp_state == json_ptr::MS_ERR_INVALID_ESCAPE) {
            sqlite3_result_error(context, jo.jo_ptr.error_msg().c_str(), -1);
        }
        else {
            null_or_default(context, argc, argv);
        }
        return;
    default:
        break;
    }

    switch (jo.sjo_type) {
    case SQLITE3_TEXT:
        sqlite3_result_text(context, jo.sjo_str.c_str(), jo.sjo_str.size(), SQLITE_TRANSIENT);
        return;
    case SQLITE_NULL:
        sqlite3_result_null(context);
        return;
    case SQLITE_INTEGER:
        sqlite3_result_int(context, jo.sjo_int);
        return;
    }

    if (result.empty()) {
        null_or_default(context, argc, argv);
        return;
    }

    sqlite3_result_text(context, result.c_str(), result.size(), SQLITE_TRANSIENT);
}

int json_extension_functions(const struct FuncDef **basic_funcs,
                             const struct FuncDefAgg **agg_funcs)
{
    static const struct FuncDef fs_funcs[] = {
        { "jget",  -1, 0, SQLITE_UTF8, 0, sql_jget },

        { NULL }
    };

    *basic_funcs = fs_funcs;
    *agg_funcs   = NULL;

    return SQLITE_OK;
}
