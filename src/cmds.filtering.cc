/**
 * Copyright (c) 2025, Timothy Stack
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
 */

#include "lnav.hh"
#include "lnav_commands.hh"
#include "sql_util.hh"

static Result<std::string, lnav::console::user_message>
com_hide_line(exec_context& ec,
              std::string cmdline,
              std::vector<std::string>& args)
{
    auto* tc = *lnav_data.ld_view_stack.top();
    auto* ttt = dynamic_cast<text_time_translator*>(tc->get_sub_source());
    std::string retval;

    if (ttt == nullptr) {
        return ec.make_error("this view does not support time filtering");
    }

    if (args.size() == 1) {
        auto min_time_opt = ttt->get_min_row_time();
        auto max_time_opt = ttt->get_max_row_time();
        char min_time_str[32], max_time_str[32];

        if (min_time_opt) {
            sql_strftime(
                min_time_str, sizeof(min_time_str), min_time_opt.value());
        }
        if (max_time_opt) {
            sql_strftime(
                max_time_str, sizeof(max_time_str), max_time_opt.value());
        }
        if (min_time_opt && max_time_opt) {
            retval = fmt::format(FMT_STRING("info: hiding lines before {} and "
                                            "after {}"),
                                 min_time_str,
                                 max_time_str);
        } else if (min_time_opt) {
            retval = fmt::format(FMT_STRING("info: hiding lines before {}"),
                                 min_time_str);
        } else if (max_time_opt) {
            retval = fmt::format(FMT_STRING("info: hiding lines after {}"),
                                 max_time_str);
        } else {
            retval
                = "info: no lines hidden by time, pass an "
                  "absolute or "
                  "relative time";
        }
    } else if (args.size() >= 2) {
        std::string all_args = remaining_args(cmdline, args);
        date_time_scanner dts;
        timeval tv_abs;
        std::optional<timeval> tv_opt;
        auto parse_res = relative_time::from_str(all_args);

        if (parse_res.isOk()) {
            log_debug("parsed!")
            if (tc->get_inner_height() > 0) {
                exttm tm;

                auto vl = tc->get_selection();
                auto log_vl_ri = ttt->time_for_row(vl);
                if (log_vl_ri) {
                    tm = exttm::from_tv(log_vl_ri.value().ri_time);
                    tv_opt = parse_res.unwrap().adjust(tm).to_timeval();

                    log_debug("got line time");
                }
            }
        } else if (dts.convert_to_timeval(all_args, tv_abs)) {
            tv_opt = tv_abs;
        } else {
            log_debug("not parsed: '%s'", all_args.c_str());
        }

        if (tv_opt && !ec.ec_dry_run) {
            char time_text[256];
            std::string relation;

            sql_strftime(time_text, sizeof(time_text), tv_opt.value());
            if (args[0] == "hide-lines-before") {
                log_debug("set min");
                ttt->set_min_row_time(tv_opt.value());
                relation = "before";
            } else {
                ttt->set_max_row_time(tv_opt.value());
                relation = "after";
            }

            tc->get_sub_source()->text_filters_changed();
            tc->reload_data();

            retval = fmt::format(
                FMT_STRING("info: hiding lines {} {}"), relation, time_text);
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_show_lines(exec_context& ec,
               std::string cmdline,
               std::vector<std::string>& args)
{
    auto* tc = *lnav_data.ld_view_stack.top();
    auto* ttt = dynamic_cast<text_time_translator*>(tc->get_sub_source());
    std::string retval = "info: showing lines";

    if (ttt == nullptr) {
        return ec.make_error("this view does not support time filtering");
    }

    if (ec.ec_dry_run) {
        retval = "";
    } else if (!args.empty()) {
        ttt->clear_min_max_row_times();
        tc->get_sub_source()->text_filters_changed();
    }

    return Ok(retval);
}

static readline_context::command_t FILTERING_COMMANDS[] = {
    {
        "hide-lines-before",
        com_hide_line,

        help_text(":hide-lines-before")
            .with_summary("Hide lines that come before the given date")
            .with_parameter(
                help_text("date", "An absolute or relative date")
                    .with_format(
                        help_parameter_format_t::HPF_TIME_FILTER_POINT))
            .with_examples({
                {"To hide the lines before the focused line in the view",
                 "here"},
                {"To hide the log messages before 6 AM today", "6am"},
            })
            .with_tags({"filtering"}),
    },
    {
        "hide-lines-after",
        com_hide_line,

        help_text(":hide-lines-after")
            .with_summary("Hide lines that come after the given date")
            .with_parameter(
                help_text("date", "An absolute or relative date")
                    .with_format(
                        help_parameter_format_t::HPF_TIME_FILTER_POINT))
            .with_examples({
                {"To hide the lines after the focused line in the view",
                 "here"},
                {"To hide the lines after 6 AM today", "6am"},
            })
            .with_tags({"filtering"}),
    },
    {
        "show-lines-before-and-after",
        com_show_lines,

        help_text(":show-lines-before-and-after")
            .with_summary("Show lines that were hidden by the "
                          "'hide-lines' commands")
            .with_opposites({"hide-lines-before", "hide-lines-after"})
            .with_tags({"filtering"}),
    },
};

void
init_lnav_filtering_commands(readline_context::command_map_t& cmd_map)
{
    for (auto& cmd : FILTERING_COMMANDS) {
        cmd.c_help.index_tags();
        cmd_map[cmd.c_name] = &cmd;
    }
}
