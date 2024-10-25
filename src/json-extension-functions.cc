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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file json-extension-functions.cc
 */

#include <cstring>
#include <string>

#include "config.h"
#include "mapbox/variant.hpp"
#include "scn/scn.h"
#include "sqlite-extension-func.hh"
#include "sqlite3.h"
#include "vtab_module.hh"
#include "vtab_module_json.hh"
#include "yajl/api/yajl_gen.h"
#include "yajlpp/json_op.hh"
#include "yajlpp/yajlpp.hh"

#define JSON_SUBTYPE 74 /* Ascii for "J" */

namespace {

class sql_json_op : public json_op {
public:
    explicit sql_json_op(json_ptr& ptr) : json_op(ptr){};

    int sjo_type{-1};
    std::string sjo_str;
    int64_t sjo_int{0};
    double sjo_float{0.0};
};

void
null_or_default(sqlite3_context* context, int argc, sqlite3_value* argv[])
{
    if (argc > 2) {
        sqlite3_result_value(context, argv[2]);
    } else {
        sqlite3_result_null(context);
    }
}

struct contains_userdata {
    mapbox::util::variant<string_fragment, sqlite3_int64, bool> cu_match_value{
        false};
    size_t cu_depth{0};
    bool cu_result{false};
};

int
contains_string(void* ctx, const unsigned char* str, size_t len)
{
    auto sf = string_fragment::from_bytes(str, len);
    auto& cu = *((contains_userdata*) ctx);

    if (cu.cu_depth <= 1 && cu.cu_match_value.get<string_fragment>() == sf) {
        cu.cu_result = true;
    }

    return 1;
}

int
contains_integer(void* ctx, long long value)
{
    auto& cu = *((contains_userdata*) ctx);

    if (cu.cu_depth <= 1 && cu.cu_match_value.get<sqlite3_int64>() == value) {
        cu.cu_result = true;
    }

    return 1;
}

int
contains_null(void* ctx)
{
    auto& cu = *((contains_userdata*) ctx);

    cu.cu_result = true;

    return 1;
}

bool
json_contains(vtab_types::nullable<const char> nullable_json_in,
              sqlite3_value* value)
{
    if (nullable_json_in.n_value == nullptr
        || nullable_json_in.n_value[0] == '\0')
    {
        return false;
    }

    const auto* json_in = nullable_json_in.n_value;

    yajl_callbacks cb{};
    contains_userdata cu;
    auto handle = yajlpp::alloc_handle(&cb, &cu);

    cb.yajl_start_array = +[](void* ctx) {
        auto& cu = *((contains_userdata*) ctx);

        cu.cu_depth += 1;

        return 1;
    };
    cb.yajl_end_array = +[](void* ctx) {
        auto& cu = *((contains_userdata*) ctx);

        cu.cu_depth -= 1;

        return 1;
    };
    cb.yajl_start_map = +[](void* ctx) {
        auto& cu = *((contains_userdata*) ctx);

        cu.cu_depth += 2;

        return 1;
    };
    cb.yajl_end_map = +[](void* ctx) {
        auto& cu = *((contains_userdata*) ctx);

        cu.cu_depth -= 2;

        return 1;
    };

    switch (sqlite3_value_type(value)) {
        case SQLITE3_TEXT:
            cb.yajl_string = contains_string;
            cu.cu_match_value = string_fragment::from_bytes(
                sqlite3_value_text(value), sqlite3_value_bytes(value));
            break;
        case SQLITE_INTEGER:
            cb.yajl_integer = contains_integer;
            cu.cu_match_value = sqlite3_value_int64(value);
            break;
        case SQLITE_NULL:
            cb.yajl_null = contains_null;
            break;
    }

    if (yajl_parse(handle.in(), (const unsigned char*) json_in, strlen(json_in))
            != yajl_status_ok
        || yajl_complete_parse(handle.in()) != yajl_status_ok)
    {
        throw yajlpp_error(handle.in(), json_in, strlen(json_in));
    }

    return cu.cu_result;
}

int
gen_handle_null(void* ctx)
{
    sql_json_op* sjo = (sql_json_op*) ctx;
    yajl_gen gen = (yajl_gen) sjo->jo_ptr_data;

    if (sjo->jo_ptr.jp_state == json_ptr::match_state_t::DONE) {
        sjo->sjo_type = SQLITE_NULL;
    } else {
        sjo->jo_ptr_error_code = yajl_gen_null(gen);
    }

    return sjo->jo_ptr_error_code == yajl_gen_status_ok;
}

static int
gen_handle_boolean(void* ctx, int boolVal)
{
    sql_json_op* sjo = (sql_json_op*) ctx;
    yajl_gen gen = (yajl_gen) sjo->jo_ptr_data;

    if (sjo->jo_ptr.jp_state == json_ptr::match_state_t::DONE) {
        sjo->sjo_type = SQLITE_INTEGER;
        sjo->sjo_int = boolVal;
    } else {
        sjo->jo_ptr_error_code = yajl_gen_bool(gen, boolVal);
    }

    return sjo->jo_ptr_error_code == yajl_gen_status_ok;
}

static int
gen_handle_string(void* ctx, const unsigned char* stringVal, size_t len)
{
    sql_json_op* sjo = (sql_json_op*) ctx;
    yajl_gen gen = (yajl_gen) sjo->jo_ptr_data;

    if (sjo->jo_ptr.jp_state == json_ptr::match_state_t::DONE) {
        sjo->sjo_type = SQLITE3_TEXT;
        sjo->sjo_str = std::string((char*) stringVal, len);
    } else {
        sjo->jo_ptr_error_code = yajl_gen_string(gen, stringVal, len);
    }

    return sjo->jo_ptr_error_code == yajl_gen_status_ok;
}

static int
gen_handle_number(void* ctx, const char* numval, size_t numlen)
{
    sql_json_op* sjo = (sql_json_op*) ctx;
    yajl_gen gen = (yajl_gen) sjo->jo_ptr_data;

    if (sjo->jo_ptr.jp_state == json_ptr::match_state_t::DONE) {
        auto num_sv = scn::string_view{numval, numlen};
        auto scan_int_res = scn::scan_value<int64_t>(num_sv);

        if (scan_int_res && scan_int_res.empty()) {
            sjo->sjo_int = scan_int_res.value();
            sjo->sjo_type = SQLITE_INTEGER;
        } else {
            auto scan_float_res = scn::scan_value<double>(num_sv);

            sjo->sjo_float = scan_float_res.value();
            sjo->sjo_type = SQLITE_FLOAT;
        }
    } else {
        sjo->jo_ptr_error_code = yajl_gen_number(gen, numval, numlen);
    }

    return sjo->jo_ptr_error_code == yajl_gen_status_ok;
}

static void
sql_jget(sqlite3_context* context, int argc, sqlite3_value** argv)
{
    if (argc < 2) {
        sqlite3_result_error(context, "expecting JSON value and pointer", -1);
        return;
    }

    if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
        null_or_default(context, argc, argv);
        return;
    }

    const auto json_in = from_sqlite<string_fragment>()(argc, argv, 0);

    if (sqlite3_value_type(argv[1]) == SQLITE_NULL) {
        sqlite3_result_text(context, json_in.data(), -1, SQLITE_TRANSIENT);
        return;
    }

    const char* ptr_in = (const char*) sqlite3_value_text(argv[1]);
    json_ptr jp(ptr_in);
    sql_json_op jo(jp);
    unsigned char* err;
    yajlpp_gen gen;

    yajl_gen_config(gen, yajl_gen_beautify, false);

    jo.jo_ptr_callbacks = json_op::gen_callbacks;
    jo.jo_ptr_callbacks.yajl_null = gen_handle_null;
    jo.jo_ptr_callbacks.yajl_boolean = gen_handle_boolean;
    jo.jo_ptr_callbacks.yajl_string = gen_handle_string;
    jo.jo_ptr_callbacks.yajl_number = gen_handle_number;
    jo.jo_ptr_data = gen.get_handle();

    auto handle = yajlpp::alloc_handle(&json_op::ptr_callbacks, &jo);
    switch (yajl_parse(handle.in(), json_in.udata(), json_in.length())) {
        case yajl_status_error: {
            err = yajl_get_error(
                handle.in(), 1, json_in.udata(), json_in.length());
            auto um = lnav::console::user_message::error("invalid JSON")
                          .with_reason((const char*) err)
                          .move();

            to_sqlite(context, um);
            yajl_free_error(handle.in(), err);
            return;
        }
        case yajl_status_client_canceled:
            if (jo.jo_ptr.jp_state
                == json_ptr::match_state_t::ERR_INVALID_ESCAPE)
            {
                sqlite3_result_error(
                    context, jo.jo_ptr.error_msg().c_str(), -1);
            } else {
                null_or_default(context, argc, argv);
            }
            return;
        default:
            break;
    }

    switch (yajl_complete_parse(handle.in())) {
        case yajl_status_error: {
            err = yajl_get_error(
                handle.in(), 1, json_in.udata(), json_in.length());
            auto um = lnav::console::user_message::error("invalid JSON")
                          .with_reason((const char*) err)
                          .move();

            to_sqlite(context, um);
            yajl_free_error(handle.in(), err);
            return;
        }
        case yajl_status_client_canceled:
            if (jo.jo_ptr.jp_state
                == json_ptr::match_state_t::ERR_INVALID_ESCAPE)
            {
                sqlite3_result_error(
                    context, jo.jo_ptr.error_msg().c_str(), -1);
            } else {
                null_or_default(context, argc, argv);
            }
            return;
        default:
            break;
    }

    switch (jo.sjo_type) {
        case SQLITE3_TEXT:
            to_sqlite(context, jo.sjo_str);
            return;
        case SQLITE_NULL:
            sqlite3_result_null(context);
            return;
        case SQLITE_INTEGER:
            sqlite3_result_int(context, jo.sjo_int);
            return;
        case SQLITE_FLOAT:
            sqlite3_result_double(context, jo.sjo_float);
            return;
    }

    const auto result = gen.to_string_fragment();

    if (result.empty()) {
        null_or_default(context, argc, argv);
        return;
    }

    sqlite3_result_text(
        context, result.data(), result.length(), SQLITE_TRANSIENT);
#ifdef HAVE_SQLITE3_VALUE_SUBTYPE
    sqlite3_result_subtype(context, JSON_SUBTYPE);
#endif
}

struct concat_context {
    concat_context(yajl_gen gen_handle) : cc_gen_handle(gen_handle) {}

    yajl_gen cc_gen_handle;
    int cc_depth{0};
};

static int
concat_gen_null(void* ctx)
{
    auto* cc = static_cast<concat_context*>(ctx);

    if (cc->cc_depth > 0) {
        return yajl_gen_null(cc->cc_gen_handle) == yajl_gen_status_ok;
    }

    return 1;
}

static int
concat_gen_boolean(void* ctx, int val)
{
    auto* cc = static_cast<concat_context*>(ctx);

    return yajl_gen_bool(cc->cc_gen_handle, val) == yajl_gen_status_ok;
}

static int
concat_gen_number(void* ctx, const char* val, size_t len)
{
    auto* cc = static_cast<concat_context*>(ctx);

    return yajl_gen_number(cc->cc_gen_handle, val, len) == yajl_gen_status_ok;
}

static int
concat_gen_string(void* ctx, const unsigned char* val, size_t len)
{
    auto* cc = static_cast<concat_context*>(ctx);

    return yajl_gen_string(cc->cc_gen_handle, val, len) == yajl_gen_status_ok;
}

static int
concat_gen_start_map(void* ctx)
{
    auto* cc = static_cast<concat_context*>(ctx);

    cc->cc_depth += 1;
    return yajl_gen_map_open(cc->cc_gen_handle) == yajl_gen_status_ok;
}

static int
concat_gen_end_map(void* ctx)
{
    auto* cc = static_cast<concat_context*>(ctx);

    cc->cc_depth -= 1;
    return yajl_gen_map_close(cc->cc_gen_handle) == yajl_gen_status_ok;
}

static int
concat_gen_map_key(void* ctx, const unsigned char* key, size_t len)
{
    auto* cc = static_cast<concat_context*>(ctx);

    return yajl_gen_string(cc->cc_gen_handle, key, len) == yajl_gen_status_ok;
}

static int
concat_gen_start_array(void* ctx)
{
    auto* cc = static_cast<concat_context*>(ctx);

    cc->cc_depth += 1;
    if (cc->cc_depth == 1) {
        return 1;
    }
    return yajl_gen_array_open(cc->cc_gen_handle) == yajl_gen_status_ok;
}

static int
concat_gen_end_array(void* ctx)
{
    auto* cc = static_cast<concat_context*>(ctx);

    cc->cc_depth -= 1;
    if (cc->cc_depth == 0) {
        return 1;
    }
    return yajl_gen_array_close(cc->cc_gen_handle) == yajl_gen_status_ok;
}

static void
concat_gen_elements(yajl_gen gen, const unsigned char* text, size_t len)
{
    yajl_callbacks callbacks = {nullptr};
    concat_context cc{gen};

    callbacks.yajl_null = concat_gen_null;
    callbacks.yajl_boolean = concat_gen_boolean;
    callbacks.yajl_number = concat_gen_number;
    callbacks.yajl_string = concat_gen_string;
    callbacks.yajl_start_map = concat_gen_start_map;
    callbacks.yajl_end_map = concat_gen_end_map;
    callbacks.yajl_map_key = concat_gen_map_key;
    callbacks.yajl_start_array = concat_gen_start_array;
    callbacks.yajl_end_array = concat_gen_end_array;

    auto handle = yajlpp::alloc_handle(&callbacks, &cc);
    yajl_config(handle, yajl_allow_comments, 1);
    if (yajl_parse(handle, (const unsigned char*) text, len) != yajl_status_ok
        || yajl_complete_parse(handle) != yajl_status_ok)
    {
        std::unique_ptr<unsigned char, decltype(&free)> err_msg(
            yajl_get_error(handle, 1, (const unsigned char*) text, len), free);

        throw sqlite_func_error("Invalid JSON: {}",
                                (const char*) err_msg.get());
    }
}

static json_string
json_concat(std::optional<const char*> json_in,
            const std::vector<sqlite3_value*>& values)
{
    yajlpp_gen gen;

    yajl_gen_config(gen, yajl_gen_beautify, false);

    {
        yajlpp_array array(gen);

        if (json_in) {
            concat_gen_elements(gen,
                                (const unsigned char*) json_in.value(),
                                strlen(json_in.value()));
        }

        for (auto* const val : values) {
            switch (sqlite3_value_type(val)) {
                case SQLITE_NULL:
                    array.gen();
                    break;
                case SQLITE_INTEGER:
                    array.gen(sqlite3_value_int64(val));
                    break;
                case SQLITE_FLOAT:
                    array.gen(sqlite3_value_double(val));
                    break;
                case SQLITE3_TEXT: {
                    const auto* text_val = sqlite3_value_text(val);

                    if (sqlite3_value_subtype(val) == JSON_SUBTYPE) {
                        concat_gen_elements(
                            gen, text_val, strlen((const char*) text_val));
                    } else {
                        array.gen((const char*) text_val);
                    }
                    break;
                }
            }
        }
    }

    return json_string(gen);
}

#if 0
static flattened_json_string
sql_flatten_json_object(string_fragment sf)
{
    yajlpp_gen gen;

    {
        json_ptr jp("/");
        json_op jo(jp);
        auto_mem<yajl_handle_t> handle(yajl_free);

        jo.jo_ptr_data = gen.get_handle();
        yajl_gen_config(gen, yajl_gen_beautify, false);
        handle.reset(yajl_alloc(&json_op::gen_callbacks, nullptr, &jo));
        switch (yajl_parse(
            handle.in(), (const unsigned char*) sf.data(), sf.length())) {
            case yajl_status_error:
            case yajl_status_client_canceled:
                throw yajlpp_error(handle.in(), sf.data(), sf.length());
            case yajl_status_ok:
                break;
        }
        switch (yajl_complete_parse(handle.in())) {
            case yajl_status_error:
            case yajl_status_client_canceled:
                throw yajlpp_error(handle.in(), sf.data(), sf.length());
            case yajl_status_ok:
                break;
        }
    }

    auto result = gen.to_string_fragment();
    if (!result.startswith("{") || !result.endswith("}")) {
        throw std::runtime_error(
            "flatten_json_object() requires a JSON object");
    }

    return flattened_json_string(gen);
}
#endif

struct json_agg_context {
    yajl_gen_t* jac_yajl_gen;
};

static void
sql_json_group_object_step(sqlite3_context* context,
                           int argc,
                           sqlite3_value** argv)
{
    if ((argc % 2) == 1) {
        sqlite3_result_error(
            context,
            "Uneven number of arguments to json_group_object(), "
            "expecting key and value pairs",
            -1);
        return;
    }

    auto* jac = (json_agg_context*) sqlite3_aggregate_context(
        context, sizeof(json_agg_context));

    if (jac->jac_yajl_gen == nullptr) {
        jac->jac_yajl_gen = yajl_gen_alloc(nullptr);
        yajl_gen_config(jac->jac_yajl_gen, yajl_gen_beautify, false);

        yajl_gen_map_open(jac->jac_yajl_gen);
    }

    for (int lpc = 0; (lpc + 1) < argc; lpc += 2) {
        if (sqlite3_value_type(argv[lpc]) == SQLITE_NULL) {
            continue;
        }

        const unsigned char* key = sqlite3_value_text(argv[lpc]);

        yajl_gen_string(jac->jac_yajl_gen, key, strlen((const char*) key));

        switch (sqlite3_value_type(argv[lpc + 1])) {
            case SQLITE_NULL:
                yajl_gen_null(jac->jac_yajl_gen);
                break;
            case SQLITE3_TEXT: {
                const unsigned char* value = sqlite3_value_text(argv[lpc + 1]);
#ifdef HAVE_SQLITE3_VALUE_SUBTYPE
                int subtype = sqlite3_value_subtype(argv[lpc + 1]);

                if (subtype == JSON_SUBTYPE) {
                    yajl_gen_number(jac->jac_yajl_gen,
                                    (const char*) value,
                                    strlen((const char*) value));
                } else {
#endif
                    yajl_gen_string(
                        jac->jac_yajl_gen, value, strlen((const char*) value));
#ifdef HAVE_SQLITE3_VALUE_SUBTYPE
                }
#endif
                break;
            }
            case SQLITE_INTEGER: {
                const unsigned char* value = sqlite3_value_text(argv[lpc + 1]);

                yajl_gen_number(jac->jac_yajl_gen,
                                (const char*) value,
                                strlen((const char*) value));
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

void
sql_json_group_object_final(sqlite3_context* context)
{
    auto* jac = (json_agg_context*) sqlite3_aggregate_context(context, 0);

    if (jac == nullptr) {
        sqlite3_result_text(context, "{}", -1, SQLITE_STATIC);
    } else {
        const unsigned char* buf;
        size_t len;

        yajl_gen_map_close(jac->jac_yajl_gen);
        yajl_gen_get_buf(jac->jac_yajl_gen, &buf, &len);
        sqlite3_result_text(context, (const char*) buf, len, SQLITE_TRANSIENT);
#ifdef HAVE_SQLITE3_VALUE_SUBTYPE
        sqlite3_result_subtype(context, JSON_SUBTYPE);
#endif
        yajl_gen_free(jac->jac_yajl_gen);
    }
}

void
sql_json_group_array_step(sqlite3_context* context,
                          int argc,
                          sqlite3_value** argv)
{
    auto* jac = (json_agg_context*) sqlite3_aggregate_context(
        context, sizeof(json_agg_context));

    if (jac->jac_yajl_gen == nullptr) {
        jac->jac_yajl_gen = yajl_gen_alloc(nullptr);
        yajl_gen_config(jac->jac_yajl_gen, yajl_gen_beautify, false);

        yajl_gen_array_open(jac->jac_yajl_gen);
    }

    for (int lpc = 0; lpc < argc; lpc++) {
        switch (sqlite3_value_type(argv[lpc])) {
            case SQLITE_NULL:
                yajl_gen_null(jac->jac_yajl_gen);
                break;
            case SQLITE3_TEXT: {
                const unsigned char* value = sqlite3_value_text(argv[lpc]);
#ifdef HAVE_SQLITE3_VALUE_SUBTYPE
                int subtype = sqlite3_value_subtype(argv[lpc]);

                if (subtype == JSON_SUBTYPE) {
                    yajl_gen_number(jac->jac_yajl_gen,
                                    (const char*) value,
                                    strlen((const char*) value));
                } else {
#endif
                    yajl_gen_string(
                        jac->jac_yajl_gen, value, strlen((const char*) value));
#ifdef HAVE_SQLITE3_VALUE_SUBTYPE
                }
#endif
                break;
            }
            case SQLITE_INTEGER: {
                const unsigned char* value = sqlite3_value_text(argv[lpc]);

                yajl_gen_number(jac->jac_yajl_gen,
                                (const char*) value,
                                strlen((const char*) value));
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

static void
sql_json_group_array_final(sqlite3_context* context)
{
    json_agg_context* jac
        = (json_agg_context*) sqlite3_aggregate_context(context, 0);

    if (jac == nullptr) {
        sqlite3_result_text(context, "[]", -1, SQLITE_STATIC);
    } else {
        const unsigned char* buf;
        size_t len;

        yajl_gen_array_close(jac->jac_yajl_gen);
        yajl_gen_get_buf(jac->jac_yajl_gen, &buf, &len);
        sqlite3_result_text(context, (const char*) buf, len, SQLITE_TRANSIENT);
#ifdef HAVE_SQLITE3_VALUE_SUBTYPE
        sqlite3_result_subtype(context, JSON_SUBTYPE);
#endif
        yajl_gen_free(jac->jac_yajl_gen);
    }
}

}  // namespace

int
json_extension_functions(struct FuncDef** basic_funcs,
                         struct FuncDefAgg** agg_funcs)
{
    static struct FuncDef json_funcs[] = {
        sqlite_func_adapter<decltype(&json_concat), json_concat>::builder(
            help_text("json_concat",
                      "Returns an array with the given values concatenated "
                      "onto the end.  "
                      "If the initial value is null, the result will be an "
                      "array with "
                      "the given elements.  If the initial value is an array, "
                      "the result "
                      "will be an array with the given values at the end.  If "
                      "the initial "
                      "value is not null or an array, the result will be an "
                      "array with "
                      "two elements: the initial value and the given value.")
                .sql_function()
                .with_prql_path({"json", "concat"})
                .with_parameter({"json", "The initial JSON value."})
                .with_parameter(
                    help_text("value",
                              "The value(s) to add to the end of the array.")
                        .one_or_more())
                .with_tags({"json"})
                .with_example({
                    "To append the number 4 to null",
                    "SELECT json_concat(NULL, 4)",
                })
                .with_example({
                    "To append 4 and 5 to the array [1, 2, 3]",
                    "SELECT json_concat('[1, 2, 3]', 4, 5)",
                })
                .with_example({
                    "To concatenate two arrays together",
                    "SELECT json_concat('[1, 2, 3]', json('[4, 5]'))",
                }))
            .with_result_subtype(),

        sqlite_func_adapter<decltype(&json_contains), json_contains>::builder(
            help_text("json_contains",
                      "Check if a JSON value contains the given element.")
                .sql_function()
                .with_prql_path({"json", "contains"})
                .with_parameter({"json", "The JSON value to query."})
                .with_parameter(
                    {"value", "The value to look for in the first argument"})
                .with_tags({"json"})
                .with_example({
                    "To test if a JSON array contains the number 4",
                    "SELECT json_contains('[1, 2, 3]', 4)",
                })
                .with_example({
                    "To test if a JSON array contains the string 'def'",
                    "SELECT json_contains('[\"abc\", \"def\"]', 'def')",
                })),

        {
            "jget",
            -1,
            SQLITE_UTF8 | SQLITE_DETERMINISTIC
#ifdef SQLITE_RESULT_SUBTYPE
                | SQLITE_RESULT_SUBTYPE
#endif
            ,
            0,
            sql_jget,
            help_text("jget",
                      "Get the value from a JSON object using a JSON-Pointer.")
                .sql_function()
                .with_prql_path({"json", "get"})
                .with_parameter({"json", "The JSON object to query."})
                .with_parameter(
                    {"ptr", "The JSON-Pointer to lookup in the object."})
                .with_parameter(
                    help_text("default",
                              "The default value if the value was not found")
                        .optional())
                .with_tags({"json"})
                .with_example(
                    {"To get the root of a JSON value", "SELECT jget('1', '')"})
                .with_example({
                    "To get the property named 'b' in a JSON object",
                    "SELECT jget('{ \"a\": 1, \"b\": 2 }', '/b')",
                })
                .with_example({
                    "To get the 'msg' property and return a default if "
                    "it does not exist",
                    "SELECT jget(null, '/msg', 'Hello')",
                }),
        },

#if 0
        sqlite_func_adapter<decltype(&sql_flatten_json_object),
                            sql_flatten_json_object>::
            builder(help_text("flatten_json_object", "hello")
                        .sql_function()
                        .with_parameter({"json", "The JSON"})),
#endif

        {nullptr},
    };

    static struct FuncDefAgg json_agg_funcs[] = {
        {
            "json_group_object",
            -1,
            SQLITE_UTF8 | SQLITE_DETERMINISTIC
#ifdef SQLITE_RESULT_SUBTYPE
                | SQLITE_RESULT_SUBTYPE
#endif
            ,
            0,
            sql_json_group_object_step,
            sql_json_group_object_final,
            help_text("json_group_object")
                .sql_function()
                .with_prql_path({"json", "group_object"})
                .with_summary(
                    "Collect the given values from a query into a JSON object")
                .with_parameter(
                    help_text("name", "The property name for the value"))
                .with_parameter(
                    help_text("value", "The value to add to the object")
                        .one_or_more())
                .with_tags({"json"})
                .with_example({"To create an object from arguments",
                               "SELECT json_group_object('a', 1, 'b', 2)"})
                .with_example({
                    "To create an object from a pair of columns",
                    "SELECT json_group_object(column1, column2) FROM "
                    "(VALUES ('a', 1), ('b', 2))",
                }),
        },
        {
            "json_group_array",
            -1,
            SQLITE_UTF8 | SQLITE_DETERMINISTIC
#ifdef SQLITE_RESULT_SUBTYPE
                | SQLITE_RESULT_SUBTYPE
#endif
            ,
            0,
            sql_json_group_array_step,
            sql_json_group_array_final,
            help_text("json_group_array")
                .sql_function()
                .with_prql_path({"json", "group_array"})
                .with_summary(
                    "Collect the given values from a query into a JSON array")
                .with_parameter(
                    help_text("value", "The values to append to the array")
                        .one_or_more())
                .with_tags({"json"})
                .with_example({
                    "To create an array from arguments",
                    "SELECT json_group_array('one', 2, 3.4)",
                })
                .with_example({
                    "To create an array from a column of values",
                    "SELECT json_group_array(column1) FROM (VALUES "
                    "(1), (2), (3))",
                }),
        },

        {nullptr},
    };

    *basic_funcs = json_funcs;
    *agg_funcs = json_agg_funcs;

    return SQLITE_OK;
}
