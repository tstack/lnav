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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file time-extension-functions.cc
 */

#include <string>
#include <unordered_map>

#include <string.h>

#include "base/attr_line.builder.hh"
#include "base/date_time_scanner.hh"
#include "base/humanize.time.hh"
#include "config.h"
#include "date/tz.h"
#include "ptimec.hh"
#include "relative_time.hh"
#include "sql_util.hh"
#include "vtab_module.hh"

static nonstd::optional<text_auto_buffer>
timeslice(sqlite3_value* time_in, nonstd::optional<const char*> slice_in_opt)
{
    thread_local date_time_scanner dts;
    thread_local struct {
        std::string c_slice_str;
        relative_time c_rel_time;
    } cache;
    const auto slice_in
        = string_fragment::from_c_str(slice_in_opt.value_or("15m"));

    if (slice_in.empty()) {
        throw sqlite_func_error("no time slice value given");
    }

    if (slice_in != cache.c_slice_str.c_str()) {
        auto parse_res = relative_time::from_str(slice_in);
        if (parse_res.isErr()) {
            throw sqlite_func_error(
                "unable to parse time slice value: {} -- {}",
                slice_in,
                parse_res.unwrapErr().pe_msg);
        }

        cache.c_rel_time = parse_res.unwrap();
        if (cache.c_rel_time.empty()) {
            throw sqlite_func_error("could not determine a time slice from: {}",
                                    slice_in);
        }

        cache.c_slice_str = slice_in.to_string();
    }

    struct timeval tv;
    struct exttm tm;

    switch (sqlite3_value_type(time_in)) {
        case SQLITE_BLOB:
        case SQLITE3_TEXT: {
            const char* time_in_str
                = reinterpret_cast<const char*>(sqlite3_value_text(time_in));

            if (dts.scan(
                    time_in_str, strlen(time_in_str), nullptr, &tm, tv, false)
                == nullptr)
            {
                dts.unlock();
                if (dts.scan(time_in_str,
                             strlen(time_in_str),
                             nullptr,
                             &tm,
                             tv,
                             false)
                    == nullptr)
                {
                    throw sqlite_func_error("unable to parse time value -- {}",
                                            time_in_str);
                }
            }
            break;
        }
        case SQLITE_INTEGER: {
            auto msecs
                = std::chrono::milliseconds(sqlite3_value_int64(time_in));

            tv.tv_sec = std::chrono::duration_cast<std::chrono::seconds>(msecs)
                            .count();
            tm.et_tm = *gmtime(&tv.tv_sec);
            tm.et_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             msecs % 1000)
                             .count();
            break;
        }
        case SQLITE_FLOAT: {
            auto secs = sqlite3_value_double(time_in);
            double integ;
            auto fract = modf(secs, &integ);

            tv.tv_sec = integ;
            tm.et_tm = *gmtime(&tv.tv_sec);
            tm.et_nsec = floor(fract * 1000000000.0);
            break;
        }
        case SQLITE_NULL: {
            return nonstd::nullopt;
        }
    }

    auto win_start_opt = cache.c_rel_time.window_start(tm);

    if (!win_start_opt) {
        return nonstd::nullopt;
    }

    auto win_start = *win_start_opt;
    auto ts = auto_buffer::alloc(64);
    auto actual_length
        = sql_strftime(ts.in(), ts.size(), win_start.to_timeval());

    ts.resize(actual_length);
    return text_auto_buffer{std::move(ts)};
}

static nonstd::optional<double>
sql_timediff(string_fragment time1, string_fragment time2)
{
    struct timeval tv1, tv2, retval;
    date_time_scanner dts1, dts2;
    auto parse_res1 = relative_time::from_str(time1);

    if (parse_res1.isOk()) {
        tv1 = parse_res1.unwrap().adjust_now().to_timeval();
    } else if (!dts1.convert_to_timeval(
                   time1.data(), time1.length(), nullptr, tv1))
    {
        return nonstd::nullopt;
    }

    auto parse_res2 = relative_time::from_str(time2);
    if (parse_res2.isOk()) {
        tv2 = parse_res2.unwrap().adjust_now().to_timeval();
    } else if (!dts2.convert_to_timeval(
                   time2.data(), time2.length(), nullptr, tv2))
    {
        return nonstd::nullopt;
    }

    timersub(&tv1, &tv2, &retval);

    return (double) retval.tv_sec + (double) retval.tv_usec / 1000000.0;
}

static std::string
sql_humanize_duration(double value)
{
    auto secs = std::trunc(value);
    auto usecs = (value - secs) * 1000000.0;
    timeval tv;

    tv.tv_sec = secs;
    tv.tv_usec = usecs;
    return humanize::time::duration::from_tv(tv).to_string();
}

static nonstd::optional<std::string>
sql_timezone(std::string tz_str, string_fragment ts_str)
{
    thread_local date_time_scanner dts;
    struct timeval tv;
    exttm tm1;

    auto scan_end
        = dts.scan(ts_str.data(), ts_str.length(), nullptr, &tm1, tv, false);
    if (scan_end == nullptr) {
        auto um = lnav::console::user_message::error(
            attr_line_t("unrecognized timestamp: ").append(ts_str));
        throw um;
    }
    size_t matched_size = scan_end - ts_str.data();
    auto ts_remaining = ts_str.substr(matched_size);
    if (!ts_remaining.empty()) {
        auto um
            = lnav::console::user_message::error(
                  attr_line_t("invalid timestamp: ").append(ts_str))
                  .with_reason(attr_line_t("the leading part of the timestamp "
                                           "was matched, however, the trailing "
                                           "text ")
                                   .append_quoted(ts_remaining)
                                   .append(" was not"))
                  .with_note(attr_line_t("input matched time format ")
                                 .append_quoted(
                                     PTIMEC_FORMATS[dts.dts_fmt_lock].pf_fmt))
                  .with_help("fix the timestamp or remove the trailing text");

        auto ts_attr = attr_line_t().append(ts_str);
        attr_line_builder alb(ts_attr);

        alb.append("\n").append(matched_size, ' ');
        {
            auto attr_guard = alb.with_attr(VC_ROLE.value(role_t::VCR_COMMENT));

            alb.append("^");
            if (ts_remaining.length() > 1) {
                if (ts_remaining.length() > 2) {
                    alb.append(ts_remaining.length() - 2, '-');
                }
                alb.append("^");
            }
            alb.append(" unrecognized input");
        }
        um.with_note(ts_attr);
        throw um;
    }

    thread_local std::string last_tz;
    thread_local const date::time_zone* tz;

    if (tz_str != last_tz) {
        tz = date::locate_zone(tz_str);
    }

    auto stime = std::chrono::time_point_cast<std::chrono::microseconds>(
        std::chrono::system_clock::from_time_t(tv.tv_sec));
    stime += std::chrono::microseconds{tv.tv_usec};
    auto ztime = date::make_zoned(tz, stime);

    auto retval = date::format("%FT%T%z", ztime);

    return retval;
}

int
time_extension_functions(struct FuncDef** basic_funcs,
                         struct FuncDefAgg** agg_funcs)
{
    static struct FuncDef time_funcs[] = {
        sqlite_func_adapter<decltype(&timeslice), timeslice>::builder(
            help_text(
                "timeslice",
                "Return the start of the slice of time that the given "
                "timestamp falls in.  "
                "If the time falls outside of the slice, NULL is returned.")
                .sql_function()
                .with_prql_path({"time", "slice"})
                .with_parameter(
                    {"time", "The timestamp to get the time slice for."})
                .with_parameter({"slice", "The size of the time slices"})
                .with_tags({"datetime"})
                .with_example({
                    "To get the timestamp rounded down to the start of the "
                    "ten minute slice",
                    "SELECT timeslice('2017-01-01T05:05:00', '10m')",
                })
                .with_example({
                    "To group log messages into five minute buckets and count "
                    "them",
                    "SELECT timeslice(log_time_msecs, '5m') AS slice, "
                    "count(1)\n    FROM lnav_example_log GROUP BY slice",
                })
                .with_example({
                    "To group log messages by those before 4:30am and after",
                    "SELECT timeslice(log_time_msecs, 'before 4:30am') AS "
                    "slice, count(1) FROM lnav_example_log GROUP BY slice",
                })),

        sqlite_func_adapter<decltype(&sql_timediff), sql_timediff>::builder(
            help_text(
                "timediff",
                "Compute the difference between two timestamps in seconds")
                .sql_function()
                .with_prql_path({"time", "diff"})
                .with_parameter({"time1", "The first timestamp"})
                .with_parameter(
                    {"time2", "The timestamp to subtract from the first"})
                .with_tags({"datetime"})
                .with_example({
                    "To get the difference between two timestamps",
                    "SELECT timediff('2017-02-03T04:05:06', "
                    "'2017-02-03T04:05:00')",
                })
                .with_example({
                    "To get the difference between relative timestamps",
                    "SELECT timediff('today', 'yesterday')",
                })),

        sqlite_func_adapter<decltype(&sql_humanize_duration),
                            sql_humanize_duration>::
            builder(
                help_text("humanize_duration",
                          "Format the given seconds value as an abbreviated "
                          "duration string")
                    .sql_function()
                    .with_prql_path({"humanize", "duration"})
                    .with_parameter({"secs", "The duration in seconds"})
                    .with_tags({"datetime", "string"})
                    .with_example({
                        "To format a duration",
                        "SELECT humanize_duration(15 * 60)",
                    })
                    .with_example({
                        "To format a sub-second value",
                        "SELECT humanize_duration(1.5)",
                    })),

        sqlite_func_adapter<decltype(&sql_timezone), sql_timezone>::builder(
            help_text("timezone", "Convert a timestamp to the given timezone")
                .sql_function()
                .with_prql_path({"time", "to_zone"})
                .with_parameter({"tz", "The target timezone"})
                .with_parameter({"ts", "The source timestamp"})
                .with_tags({"datetime", "string"})
                .with_example({
                    "To convert a time to America/Los_Angeles",
                    "SELECT timezone('America/Los_Angeles', "
                    "'2022-03-02T10:00')",
                })),

        {nullptr},
    };

    *basic_funcs = time_funcs;
    *agg_funcs = nullptr;

    return SQLITE_OK;
}
