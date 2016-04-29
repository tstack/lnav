/*
 * Written by Alexey Tourbin <at@altlinux.org>.
 *
 * The author has dedicated the code to the public domain.  Anyone is free
 * to copy, modify, publish, use, compile, sell, or distribute the original
 * code, either in source code form or as a compiled binary, for any purpose,
 * commercial or non-commercial, and by any means.
 */

#include "config.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <pcrecpp.h>

#include "pcrepp.hh"

#include "yajlpp.hh"
#include "column_namer.hh"
#include "yajl/api/yajl_gen.h"
#include "sqlite-extension-func.h"
#include "data_scanner.hh"
#include "data_parser.hh"
#include "elem_to_json.hh"

typedef struct {
    char *      s;
    pcrecpp::RE *re;
    pcrepp *re2;
} cache_entry;

#ifndef CACHE_SIZE
#define CACHE_SIZE    16
#endif

#define JSON_SUBTYPE  74    /* Ascii for "J" */

using namespace std;

static
cache_entry *find_re(sqlite3_context *ctx, const char *re)
{
    static cache_entry cache[CACHE_SIZE];
    int i;
    int found = 0;

    for (i = 0; i < CACHE_SIZE && cache[i].s; i++) {
        if (strcmp(re, cache[i].s) == 0) {
            found = 1;
            break;
        }
    }
    if (found) {
        if (i > 0) {
            cache_entry c = cache[i];
            memmove(cache + 1, cache, i * sizeof(cache_entry));
            cache[0] = c;
        }
    }
    else {
        cache_entry c;

        c.re = new pcrecpp::RE(re);
        if (!c.re->error().empty()) {
            char *e2 = sqlite3_mprintf("%s: %s", re, c.re->error().c_str());
            sqlite3_result_error(ctx, e2, -1);
            sqlite3_free(e2);
            delete c.re;

            return NULL;
        }
        c.s = strdup(re);
        if (!c.s) {
            sqlite3_result_error(ctx, "strdup: ENOMEM", -1);
            delete c.re;
            return NULL;
        }
        c.re2 = new pcrepp(c.s);
        i = CACHE_SIZE - 1;
        if (cache[i].s) {
            free(cache[i].s);
            assert(cache[i].re);
            delete cache[i].re;
            delete cache[i].re2;
        }
        memmove(cache + 1, cache, i * sizeof(cache_entry));
        cache[0] = c;
    }

    return &cache[0];
}

static
void regexp(sqlite3_context *ctx, int argc, sqlite3_value **argv)
{
    const char *re, *str;
    cache_entry *reobj;

    assert(argc == 2);

    re = (const char *)sqlite3_value_text(argv[0]);
    if (!re) {
        sqlite3_result_error(ctx, "no regexp", -1);
        return;
    }

    str = (const char *)sqlite3_value_text(argv[1]);
    if (!str) {
        sqlite3_result_error(ctx, "no string", -1);
        return;
    }

    reobj = find_re(ctx, re);
    if (reobj == NULL)
        return;

    {
        bool rc = reobj->re->PartialMatch(str);
        sqlite3_result_int(ctx, rc);
        return;
    }
}

static
void regexp_match(sqlite3_context *ctx, int argc, sqlite3_value **argv)
{
    const char *re, *str;
    cache_entry *reobj;

    assert(argc == 2);

    re = (const char *)sqlite3_value_text(argv[0]);
    if (!re) {
        sqlite3_result_error(ctx, "no regexp", -1);
        return;
    }

    str = (const char *)sqlite3_value_text(argv[1]);
    if (!str) {
        sqlite3_result_null(ctx);
        return;
    }

    reobj = find_re(ctx, re);
    if (reobj == NULL) {
        return;
    }

    pcre_context_static<30> pc;
    pcre_input pi(str);
    pcrepp &extractor = *reobj->re2;

    if (extractor.get_capture_count() == 0) {
        sqlite3_result_error(ctx,
                             "regular expression does not have any captures",
                             -1);
        return;
    }

    if (!extractor.match(pc, pi)) {
        sqlite3_result_null(ctx);
        return;
    }

    auto_mem<yajl_gen_t> gen(yajl_gen_free);

    gen = yajl_gen_alloc(NULL);
    yajl_gen_config(gen.in(), yajl_gen_beautify, false);

    if (extractor.get_capture_count() == 1) {
        pcre_context::capture_t *cap = pc[0];
        const char *cap_start = pi.get_substr_start(cap);

        if (!cap->is_valid()) {
            sqlite3_result_null(ctx);
        }
        else {
            char *cap_copy = (char *)alloca(cap->length() + 1);
            int64_t i_value;
            double d_value;
            int end_index;

            memcpy(cap_copy, cap_start, cap->length());
            cap_copy[cap->length()] = '\0';

            if (sscanf(cap_copy, "%lld%n", &i_value, &end_index) == 1 &&
                (end_index == cap->length())) {
                sqlite3_result_int64(ctx, i_value);
            }
            else if (sscanf(cap_copy, "%lf%n", &d_value, &end_index) == 1 &&
                     (end_index == cap->length())) {
                sqlite3_result_double(ctx, d_value);
            }
            else {
                sqlite3_result_text(ctx, cap_start, cap->length(), SQLITE_TRANSIENT);
            }
        }
        return;
    }
    else {
        yajlpp_map root_map(gen);
        column_namer cn;

        for (int lpc = 0; lpc < extractor.get_capture_count(); lpc++) {
            string colname = cn.add_column(extractor.name_for_capture(lpc));
            pcre_context::capture_t *cap = pc[lpc];

            yajl_gen_string(gen, colname);

            if (!cap->is_valid()) {
                yajl_gen_null(gen);
            }
            else {
                const char *cap_start = pi.get_substr_start(cap);
                char *cap_copy = (char *) alloca(cap->length() + 1);
                int64_t i_value;
                double d_value;
                int end_index;

                memcpy(cap_copy, cap_start, cap->length());
                cap_copy[cap->length()] = '\0';

                if (sscanf(cap_copy, "%lld%n", &i_value, &end_index) == 1 &&
                    (end_index == cap->length())) {
                    yajl_gen_integer(gen, i_value);
                }
                else if (sscanf(cap_copy, "%lf%n", &d_value, &end_index) == 1 &&
                         (end_index == cap->length())) {
                    yajl_gen_number(gen, cap_start, cap->length());
                }
                else {
                    yajl_gen_pstring(gen, cap_start, cap->length());
                }
            }
        }
    }

    const unsigned char *buf;
    size_t len;

    yajl_gen_get_buf(gen, &buf, &len);
    sqlite3_result_text(ctx, (const char *) buf, len, SQLITE_TRANSIENT);
#ifdef HAVE_SQLITE3_VALUE_SUBTYPE
    sqlite3_result_subtype(ctx, JSON_SUBTYPE);
#endif
}

static
void extract(sqlite3_context *ctx, int argc, sqlite3_value **argv)
{
    const char *str;

    assert(argc == 1);

    str = (const char *)sqlite3_value_text(argv[0]);
    if (!str) {
        sqlite3_result_null(ctx);
        return;
    }

    data_scanner ds(str);
    data_parser dp(&ds);

    dp.parse();
    // dp.print(stderr, dp.dp_pairs);

    auto_mem<yajl_gen_t> gen(yajl_gen_free);

    gen = yajl_gen_alloc(NULL);
    yajl_gen_config(gen.in(), yajl_gen_beautify, false);

    elements_to_json(gen, dp, &dp.dp_pairs);

    const unsigned char *buf;
    size_t len;

    yajl_gen_get_buf(gen, &buf, &len);
    sqlite3_result_text(ctx, (const char *) buf, len, SQLITE_TRANSIENT);
#ifdef HAVE_SQLITE3_VALUE_SUBTYPE
    sqlite3_result_subtype(ctx, JSON_SUBTYPE);
#endif
}

static
void regexp_replace(sqlite3_context *ctx, int argc, sqlite3_value **argv)
{
    const char *re, *str, *repl;
    cache_entry *reobj;

    assert(argc == 3);

    str = (const char *)sqlite3_value_text(argv[0]);
    if (!str) {
        sqlite3_result_error(ctx, "no string", -1);
        return;
    }

    re = (const char *)sqlite3_value_text(argv[1]);
    if (!re) {
        sqlite3_result_error(ctx, "no regexp", -1);
        return;
    }

    repl = (const char *)sqlite3_value_text(argv[2]);
    if (!repl) {
        sqlite3_result_error(ctx, "no string", -1);
        return;
    }

    reobj = find_re(ctx, re);
    if (reobj == NULL)
        return;

    {
        string dest(str);
        reobj->re->GlobalReplace(repl, &dest);
        sqlite3_result_text(ctx, dest.c_str(), dest.length(), SQLITE_TRANSIENT);
        return;
    }
}

static
void sql_startswith(sqlite3_context *context,
                    int argc, sqlite3_value **argv)
{
    const char *str_in;
    const char *prefix;

    if ((sqlite3_value_type(argv[0]) == SQLITE_NULL) ||
        (sqlite3_value_type(argv[1]) == SQLITE_NULL)) {
        sqlite3_result_null(context);
        return;
    }

    str_in = (const char *)sqlite3_value_text(argv[0]);
    prefix = (const char *)sqlite3_value_text(argv[1]);

    if (strncmp(str_in, prefix, strlen(prefix)) == 0)
        sqlite3_result_int(context, 1);
    else
        sqlite3_result_int(context, 0);
}

static
void sql_endswith(sqlite3_context *context,
                  int argc, sqlite3_value **argv)
{
    const char *str_in;
    const char *suffix;

    if ((sqlite3_value_type(argv[0]) == SQLITE_NULL) ||
        (sqlite3_value_type(argv[1]) == SQLITE_NULL)) {
        sqlite3_result_null(context);
        return;
    }

    str_in = (const char *)sqlite3_value_text(argv[0]);
    suffix = (const char *)sqlite3_value_text(argv[1]);

    int str_len = strlen(str_in);
    int suffix_len = strlen(suffix);

    if (str_len < suffix_len) {
        sqlite3_result_int(context, 0);
    }
    else if (strcmp(&str_in[str_len - suffix_len], suffix) == 0) {
        sqlite3_result_int(context, 1);
    }
    else {
        sqlite3_result_int(context, 0);
    }
}

int string_extension_functions(const struct FuncDef **basic_funcs,
                               const struct FuncDefAgg **agg_funcs)
{
    static const struct FuncDef string_funcs[] = {
        { "regexp", 2, 0, SQLITE_UTF8, 0, regexp },
        { "regexp_replace", 3, 0, SQLITE_UTF8, 0, regexp_replace },
        { "regexp_match", 2, 0, SQLITE_UTF8, 0, regexp_match },

        { "extract", 1, 0, SQLITE_UTF8, 0, extract },

        { "startswith", 2, 0, SQLITE_UTF8, 0, sql_startswith },
        { "endswith", 2, 0, SQLITE_UTF8, 0, sql_endswith },

        { NULL }
    };

    *basic_funcs = string_funcs;

    return SQLITE_OK;
}
