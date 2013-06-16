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

#include "pcrepp.hh"

#include "sqlite-extension-func.h"

typedef struct {
    char *      s;
    pcre *      p;
    pcre_extra *e;
} cache_entry;

#ifndef CACHE_SIZE
#define CACHE_SIZE    16
#endif

static
void regexp(sqlite3_context *ctx, int argc, sqlite3_value **argv)
{
    const char *re, *str;
    pcre *      p;
    pcre_extra *e;

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

    /* simple LRU cache */
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
            const char *err;
            int         pos;
            c.p = pcre_compile(re, 0, &err, &pos, NULL);
            if (!c.p) {
                char *e2 = sqlite3_mprintf("%s: %s (offset %d)", re, err, pos);
                sqlite3_result_error(ctx, e2, -1);
                sqlite3_free(e2);
                return;
            }
            c.e = pcre_study(c.p, 0, &err);
            c.s = strdup(re);
            if (!c.s) {
                sqlite3_result_error(ctx, "strdup: ENOMEM", -1);
                pcre_free(c.p);
                pcre_free(c.e);
                return;
            }
            i = CACHE_SIZE - 1;
            if (cache[i].s) {
                free(cache[i].s);
                assert(cache[i].p);
                pcre_free(cache[i].p);
                pcre_free(cache[i].e);
            }
            memmove(cache + 1, cache, i * sizeof(cache_entry));
            cache[0] = c;
        }
        p = cache[0].p;
        e = cache[0].e;
    }

    {
        int rc;
        assert(p);
        rc = pcre_exec(p, e, str, strlen(str), 0, 0, NULL, 0);
        sqlite3_result_int(ctx, rc >= 0);
        return;
    }
}

int string_extension_functions(const struct FuncDef **basic_funcs,
                               const struct FuncDefAgg **agg_funcs)
{
    static const struct FuncDef string_funcs[] = {
        { "regexp", 2, 0, SQLITE_UTF8, 0, regexp },

        { NULL }
    };

    *basic_funcs = string_funcs;

    return SQLITE_OK;
}
