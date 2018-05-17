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

#include "lnav_util.hh"
#include "sql_util.hh"
#include "relative_time.hh"

#include "vtab_module.hh"

using namespace std;

static string timeslice(const char *time_in, nonstd::optional<const char *> slice_in_opt)
{
    const char *slice_in = slice_in_opt.value_or("15m");
    relative_time::parse_error pe;
    date_time_scanner dts;
    relative_time rt;
    time_t now;

    time(&now);
    dts.set_base_time(now);

    if (!rt.parse(slice_in, strlen(slice_in), pe)) {
        throw sqlite_func_error("unable to parse time slice value");
    }

    if (rt.empty()) {
        throw sqlite_func_error("no time slice value given");
    }

    if (rt.is_absolute()) {
        throw sqlite_func_error("absolute time slices are not valid");
    }

    struct exttm tm;
    struct timeval tv;

    if (dts.scan(time_in, strlen(time_in), NULL, &tm, tv) == NULL) {
        throw sqlite_func_error("unable to parse time value");
    }

    int64_t us = tv.tv_sec * 1000 * 1000 + tv.tv_usec, remainder;

    remainder = us % rt.to_microseconds();
    us -= remainder;

    tv.tv_sec = us / (1000 * 1000);
    tv.tv_usec = us % (1000 * 1000);

    char ts[64];
    sql_strftime(ts, sizeof(ts), tv);

    return ts;
}

static
nonstd::optional<double> sql_timediff(const char *time1, const char *time2)
{
    struct timeval tv1, tv2, retval;
    date_time_scanner dts1, dts2;
    relative_time rt1, rt2;
    struct relative_time::parse_error pe;

    if (rt1.parse(time1, -1, pe)) {
        rt1.add_now()
            .to_timeval(tv1);
    } else if (!dts1.convert_to_timeval(time1, -1, NULL, tv1)) {
        return nonstd::nullopt;
    }

    if (rt2.parse(time2, -1, pe)) {
        rt2.add_now()
            .to_timeval(tv2);
    } else if (!dts2.convert_to_timeval(time2, -1, NULL, tv2)) {
        return nonstd::nullopt;
    }

    timersub(&tv1, &tv2, &retval);

    return (double) retval.tv_sec + (double) retval.tv_usec / 1000000.0;
}

int time_extension_functions(struct FuncDef **basic_funcs,
                             struct FuncDefAgg **agg_funcs)
{
    static struct FuncDef time_funcs[] = {
        sqlite_func_adapter<decltype(&timeslice), timeslice>::builder(
            help_text("timeslice",
                      "Return the start of the slice of time that the given timestamp falls in.")
                .sql_function()
                .with_parameter({"time", "The timestamp to get the time slice for."})
                .with_parameter({"slice", "The size of the time slices"})
                .with_tags({"datetime"})
                .with_example({"SELECT timeslice('2017-01-01T05:05:00', '10m')"})
                .with_example({"SELECT timeslice(log_time, '5m') AS slice, count(*) FROM lnav_example_log GROUP BY slice"})
        ),

        sqlite_func_adapter<decltype(&sql_timediff), sql_timediff>::builder(
            help_text("timediff",
                      "Compute the difference between two timestamps")
                .sql_function()
                .with_parameter({"time1", "The first timestamp"})
                .with_parameter({"time2", "The timestamp to subtract from the first"})
                .with_tags({"datetime"})
                .with_example({"SELECT timediff('2017-02-03T04:05:06', '2017-02-03T04:05:00')"})
                .with_example({"SELECT timediff('today', 'yesterday')"})
        ),

        { NULL }
    };

    *basic_funcs = time_funcs;
    *agg_funcs   = NULL;

    return SQLITE_OK;
}
