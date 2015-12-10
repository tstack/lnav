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

#include "yajl/api/yajl_gen.h"
#include "sqlite-extension-func.h"

using namespace std;

#define JSON_SUBTYPE  74    /* Ascii for "J" */

class sql_json_op : public json_op {
public:
    sql_json_op(json_ptr &ptr) : json_op(ptr), sjo_type(-1), sjo_int(0) { };

    int sjo_type;
    string sjo_str;
    int sjo_int;
};

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

    gen = yajl_gen_alloc(NULL);
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

    const unsigned char *buf;
    size_t len;

    yajl_gen_get_buf(gen, &buf, &len);

    if (len == 0) {
        null_or_default(context, argc, argv);
        return;
    }

    sqlite3_result_text(context, (const char *) buf, len, SQLITE_TRANSIENT);
}

struct json_agg_context {
    yajl_gen_t *jac_yajl_gen;
};

static void sql_json_group_object_step(sqlite3_context *context,
                                       int argc,
                                       sqlite3_value **argv)
{
    if ((argc % 2) == 1) {
        sqlite3_result_error(
                context,
                "Uneven number of arguments to json_group_object(), "
                        "expecting key and value pairs",
                -1);
        return;
    }

    json_agg_context *jac = (json_agg_context *) sqlite3_aggregate_context(
            context, sizeof(json_agg_context));


    if (jac->jac_yajl_gen == NULL) {
        jac->jac_yajl_gen = yajl_gen_alloc(NULL);
        yajl_gen_config(jac->jac_yajl_gen, yajl_gen_beautify, false);

        yajl_gen_map_open(jac->jac_yajl_gen);
    }

    for (int lpc = 0; (lpc + 1) < argc; lpc += 2) {
        if (sqlite3_value_type(argv[lpc]) == SQLITE_NULL) {
            continue;
        }

        const unsigned char *key = sqlite3_value_text(argv[lpc]);

        yajl_gen_string(jac->jac_yajl_gen, key, strlen((const char *) key));

        switch (sqlite3_value_type(argv[lpc + 1])) {
            case SQLITE_NULL:
                yajl_gen_null(jac->jac_yajl_gen);
                break;
            case SQLITE3_TEXT: {
                const unsigned char *value = sqlite3_value_text(argv[lpc + 1]);
#ifdef HAVE_SQLITE3_VALUE_SUBTYPE
                int subtype = sqlite3_value_subtype(argv[lpc + 1]);

                if (subtype == JSON_SUBTYPE) {
                    yajl_gen_number(jac->jac_yajl_gen,
                                    (const char *) value,
                                    strlen((const char *)value));
                }
                else {
#endif
                    yajl_gen_string(jac->jac_yajl_gen,
                                    value,
                                    strlen((const char *) value));
#ifdef HAVE_SQLITE3_VALUE_SUBTYPE
                }
#endif
                break;
            }
            case SQLITE_INTEGER: {
                const unsigned char *value = sqlite3_value_text(argv[lpc + 1]);

                yajl_gen_number(jac->jac_yajl_gen,
                                (const char *) value,
                                strlen((const char *) value));
                break;
            }
            case SQLITE_FLOAT: {
                double value = sqlite3_value_double(argv[lpc + 1]);

                yajl_gen_double(jac->jac_yajl_gen, value);
                break;
            }
        }
    }

}

static void sql_json_group_object_final(sqlite3_context *context)
{
    json_agg_context *jac = (json_agg_context *) sqlite3_aggregate_context(
            context, 0);

    if (jac == NULL) {
        sqlite3_result_text(context, "{}", -1, SQLITE_STATIC);
    }
    else {
        const unsigned char *buf;
        size_t len;

        yajl_gen_map_close(jac->jac_yajl_gen);
        yajl_gen_get_buf(jac->jac_yajl_gen, &buf, &len);
        sqlite3_result_text(context, (const char *) buf, len, SQLITE_TRANSIENT);
#ifdef HAVE_SQLITE3_VALUE_SUBTYPE
        sqlite3_result_subtype(context, JSON_SUBTYPE);
#endif
        yajl_gen_free(jac->jac_yajl_gen);
    }
}

static void sql_json_group_array_step(sqlite3_context *context,
                                      int argc,
                                      sqlite3_value **argv)
{
    json_agg_context *jac = (json_agg_context *) sqlite3_aggregate_context(
            context, sizeof(json_agg_context));

    if (jac->jac_yajl_gen == NULL) {
        jac->jac_yajl_gen = yajl_gen_alloc(NULL);
        yajl_gen_config(jac->jac_yajl_gen, yajl_gen_beautify, false);

        yajl_gen_array_open(jac->jac_yajl_gen);
    }

    for (int lpc = 0; lpc < argc; lpc++) {
        switch (sqlite3_value_type(argv[lpc])) {
            case SQLITE_NULL:
                yajl_gen_null(jac->jac_yajl_gen);
                break;
            case SQLITE3_TEXT: {
                const unsigned char *value = sqlite3_value_text(argv[lpc]);
#ifdef HAVE_SQLITE3_VALUE_SUBTYPE
                int subtype = sqlite3_value_subtype(argv[lpc]);

                if (subtype == JSON_SUBTYPE) {
                    yajl_gen_number(jac->jac_yajl_gen,
                                    (const char *) value,
                                    strlen((const char *)value));
                }
                else {
#endif
                    yajl_gen_string(jac->jac_yajl_gen,
                                    value,
                                    strlen((const char *) value));
#ifdef HAVE_SQLITE3_VALUE_SUBTYPE
                }
#endif
                break;
            }
            case SQLITE_INTEGER: {
                const unsigned char *value = sqlite3_value_text(argv[lpc]);

                yajl_gen_number(jac->jac_yajl_gen,
                                (const char *) value,
                                strlen((const char *) value));
                break;
            }
            case SQLITE_FLOAT: {
                double value = sqlite3_value_double(argv[lpc]);

                yajl_gen_double(jac->jac_yajl_gen, value);
                break;
            }
        }
    }

}

static void sql_json_group_array_final(sqlite3_context *context)
{
    json_agg_context *jac = (json_agg_context *) sqlite3_aggregate_context(
            context, 0);

    if (jac == NULL) {
        sqlite3_result_text(context, "{}", -1, SQLITE_STATIC);
    }
    else {
        const unsigned char *buf;
        size_t len;

        yajl_gen_array_close(jac->jac_yajl_gen);
        yajl_gen_get_buf(jac->jac_yajl_gen, &buf, &len);
        sqlite3_result_text(context, (const char *) buf, len, SQLITE_TRANSIENT);
#ifdef HAVE_SQLITE3_VALUE_SUBTYPE
        sqlite3_result_subtype(context, JSON_SUBTYPE);
#endif
        yajl_gen_free(jac->jac_yajl_gen);
    }
}

int json_extension_functions(const struct FuncDef **basic_funcs,
                             const struct FuncDefAgg **agg_funcs)
{
    static const struct FuncDef json_funcs[] = {
        { "jget",  -1, 0, SQLITE_UTF8, 0, sql_jget },

        { NULL }
    };

    static const struct FuncDefAgg json_agg_funcs[] = {
            { "json_group_object", -1, 0, 0,
                    sql_json_group_object_step, sql_json_group_object_final, },
            { "json_group_array", -1, 0, 0,
                    sql_json_group_array_step, sql_json_group_array_final, },

            { NULL }
    };

    *basic_funcs = json_funcs;
    *agg_funcs   = json_agg_funcs;

    return SQLITE_OK;
}
