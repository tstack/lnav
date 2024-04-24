/**
 * Copyright (c) 2013, Timothy Stack
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
 * @file state-extension-functions.cc
 */

#include <string>

#include <stdint.h>

#include "base/opt_util.hh"
#include "config.h"
#include "lnav.hh"
#include "sql_util.hh"
#include "sqlite3.h"
#include "vtab_module.hh"

static nonstd::optional<int64_t>
sql_log_top_line()
{
    const auto& tc = lnav_data.ld_views[LNV_LOG];

    if (tc.get_inner_height() == 0_vl) {
        return nonstd::nullopt;
    }
    return (int64_t) tc.get_selection();
}

static nonstd::optional<int64_t>
sql_log_msg_line()
{
    const auto& tc = lnav_data.ld_views[LNV_LOG];

    if (tc.get_inner_height() == 0_vl) {
        return nonstd::nullopt;
    }

    auto top_line = tc.get_selection();
    auto line_pair_opt = lnav_data.ld_log_source.find_line_with_file(top_line);
    if (!line_pair_opt) {
        return nonstd::nullopt;
    }

    auto ll = line_pair_opt.value().second;
    while (ll->is_continued()) {
        --ll;
        top_line -= 1_vl;
    }

    return (int64_t) top_line;
}

static nonstd::optional<std::string>
sql_log_top_datetime()
{
    const auto& tc = lnav_data.ld_views[LNV_LOG];

    if (tc.get_inner_height() == 0_vl) {
        return nonstd::nullopt;
    }

    auto top_ri = lnav_data.ld_log_source.time_for_row(
        lnav_data.ld_views[LNV_LOG].get_selection());
    if (!top_ri) {
        return nonstd::nullopt;
    }

    char buffer[64];

    sql_strftime(buffer, sizeof(buffer), top_ri->ri_time);
    return buffer;
}

static nonstd::optional<std::string>
sql_lnav_top_file()
{
    auto top_view_opt = lnav_data.ld_view_stack.top();

    if (!top_view_opt) {
        return nonstd::nullopt;
    }

    auto* top_view = top_view_opt.value();
    return top_view->map_top_row([](const auto& al) {
        return get_string_attr(al.get_attrs(), logline::L_FILE) |
            [](const auto wrapper) {
                auto lf = wrapper.get();

                return nonstd::make_optional(lf->get_filename());
            };
    });
}

static const char*
sql_lnav_version()
{
    return PACKAGE_VERSION;
}

static int64_t
sql_error(const char* str, nonstd::optional<string_fragment> reason)
{
    auto um = lnav::console::user_message::error(str);

    if (reason) {
        um.with_reason(reason->to_string());
    }
    throw um;
}

static nonstd::optional<std::string>
sql_echoln(nonstd::optional<std::string> arg)
{
    if (arg) {
        auto& ec = lnav_data.ld_exec_context;
        auto outfile = ec.get_output();

        if (outfile) {
            fmt::print(outfile.value(), FMT_STRING("{}\n"), arg.value());
            if (outfile.value() == stdout) {
                lnav_data.ld_stdout_used = true;
            }
        }
    }

    return arg;
}

int
state_extension_functions(struct FuncDef** basic_funcs,
                          struct FuncDefAgg** agg_funcs)
{
    static struct FuncDef state_funcs[] = {
        sqlite_func_adapter<decltype(&sql_log_top_line), sql_log_top_line>::
            builder(
                help_text(
                    "log_top_line",
                    "Return the number of the focused line of the log view.")
                    .sql_function()
                    .with_prql_path({"lnav", "view", "top_line"})),

        sqlite_func_adapter<decltype(&sql_log_msg_line), sql_log_msg_line>::
            builder(help_text("log_msg_line",
                              "Return the starting line number of the focused "
                              "log message.")
                        .sql_function()
                        .with_prql_path({"lnav", "view", "msg_line"})),

        sqlite_func_adapter<decltype(&sql_log_top_datetime),
                            sql_log_top_datetime>::
            builder(help_text("log_top_datetime",
                              "Return the timestamp of the line at the top of "
                              "the log view.")
                        .sql_function()
                        .with_prql_path({"lnav", "view", "top_datetime"})),

        sqlite_func_adapter<decltype(&sql_lnav_top_file), sql_lnav_top_file>::
            builder(help_text("lnav_top_file",
                              "Return the name of the file that the top line "
                              "in the current view came from.")
                        .sql_function()
                        .with_prql_path({"lnav", "view", "top_file"})),

        sqlite_func_adapter<decltype(&sql_lnav_version), sql_lnav_version>::
            builder(
                help_text("lnav_version", "Return the current version of lnav")
                    .sql_function()
                    .with_prql_path({"lnav", "version"})),

        sqlite_func_adapter<decltype(&sql_error), sql_error>::builder(
            help_text("raise_error",
                      "Raises an error with the given message when executed")
                .sql_function()
                .with_parameter({"msg", "The error message"})
                .with_parameter(
                    help_text("reason", "The reason the error occurred")
                        .optional())
                .with_example({
                    "To raise an error if a variable is not set",
                    "SELECT ifnull($val, raise_error('please set $val', "
                    "'because'))",
                }))
            .with_flags(SQLITE_UTF8),

        sqlite_func_adapter<decltype(&sql_echoln), sql_echoln>::builder(
            help_text("echoln",
                      "Echo the argument to the current output file and return "
                      "it")
                .sql_function()
                .with_parameter(
                    {"value", "The value to write to the current output file"})
                .with_tags({"io"}))
            .with_flags(SQLITE_UTF8),

        {nullptr},
    };

    *basic_funcs = state_funcs;

    return SQLITE_OK;
}
