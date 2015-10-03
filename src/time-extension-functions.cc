/**
 * Copyright (c) 2015, Timothy Stack
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
 * @file time-extension-functions.cc
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <stdint.h>

#include <string>
#include <stddef.h>

#include "lnav_util.hh"
#include "sql_util.hh"
#include "relative_time.hh"

#include "sqlite3.h"

#include "sqlite-extension-func.h"

static void timeslice(sqlite3_context *context,
                      int argc, sqlite3_value **argv)
{
    relative_time::parse_error pe;
    const char *slice_in, *time_in;
    date_time_scanner dts;
    relative_time rt;
    time_t now;

    time(&now);
    dts.set_base_time(now);

    if (sqlite3_value_type(argv[0]) == SQLITE_NULL ||
            sqlite3_value_type(argv[1]) == SQLITE_NULL) {
        sqlite3_result_null(context);
        return;
    }

    time_in = (const char *)sqlite3_value_text(argv[0]);
    slice_in = (const char *)sqlite3_value_text(argv[1]);

    if (!rt.parse(slice_in, strlen(slice_in), pe)) {
        sqlite3_result_error(context, "unable to parse time slice value", -1);
        return;
    }

    if (rt.empty()) {
        sqlite3_result_error(context, "no time slice value given", -1);
        return;
    }

    if (rt.is_absolute()) {
        sqlite3_result_error(context, "absolute time slices are not valid", -1);
        return;
    }

    struct exttm tm;
    struct timeval tv;

    if (dts.scan(time_in, strlen(time_in), NULL, &tm, tv) == NULL) {
        sqlite3_result_error(context, "unable to parse time value", -1);
        return;
    }

    int64_t us = tv.tv_sec * 1000 * 1000 + tv.tv_usec, remainder;

    remainder = us % rt.to_microseconds();
    us -= remainder;

    tv.tv_sec = us / (1000 * 1000);
    tv.tv_usec = us % (1000 * 1000);

    char ts[64];
    ssize_t tslen = sql_strftime(ts, sizeof(ts), tv);
    sqlite3_result_text(context, ts, tslen, SQLITE_TRANSIENT);
}

int time_extension_functions(const struct FuncDef **basic_funcs,
                             const struct FuncDefAgg **agg_funcs)
{
    static const struct FuncDef time_funcs[] = {
        { "timeslice",  2, 0, SQLITE_UTF8, 0, timeslice },

        /*
         * TODO: add other functions like readlink, normpath, ... 
         */

        { NULL }
    };

    *basic_funcs = time_funcs;
    *agg_funcs   = NULL;

    return SQLITE_OK;
}
