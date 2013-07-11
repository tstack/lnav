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

#include "sqlite-extension-func.h"

typedef struct {
    char *      s;
    pcrecpp::RE *re;
} cache_entry;

#ifndef CACHE_SIZE
#define CACHE_SIZE    16
#endif

static
pcrecpp::RE *find_re(sqlite3_context *ctx, const char *re)
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
        i = CACHE_SIZE - 1;
        if (cache[i].s) {
            free(cache[i].s);
            assert(cache[i].re);
            delete cache[i].re;
        }
        memmove(cache + 1, cache, i * sizeof(cache_entry));
        cache[0] = c;
    }

    return cache[0].re;
}

static
void regexp(sqlite3_context *ctx, int argc, sqlite3_value **argv)
{
    const char *re, *str;
    pcrecpp::RE *reobj;

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
        bool rc = reobj->PartialMatch(str);
        sqlite3_result_int(ctx, rc);
        return;
    }
}

static
void regexp_replace(sqlite3_context *ctx, int argc, sqlite3_value **argv)
{
    const char *re, *str, *repl;
    pcrecpp::RE *reobj;

    assert(argc == 3);

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
        reobj->GlobalReplace(repl, &dest);
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

        { "startswith", 2, 0, SQLITE_UTF8, 0, sql_startswith },
        { "endswith", 2, 0, SQLITE_UTF8, 0, sql_endswith },

        { NULL }
    };

    *basic_funcs = string_funcs;

    return SQLITE_OK;
}
