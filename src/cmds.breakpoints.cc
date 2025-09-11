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

#include <string>
#include <vector>

#include "base/intern_string.hh"
#include "base/itertools.hh"
#include "base/lnav.console.hh"
#include "base/result.h"
#include "command_executor.hh"
#include "fmt/format.h"
#include "lnav.hh"
#include "lnav_commands.hh"
#include "log_data_helper.hh"
#include "logfile_sub_source.hh"
#include "logline_window.hh"
#include "pcrepp/pcre2pp.hh"
#include "readline_context.hh"
#include "shlex.hh"
#include "sqlitepp.client.hh"

static Result<std::string, lnav::console::user_message>
com_breakpoint(exec_context& ec,
               std::string cmdline,
               std::vector<std::string>& args)
{
    static const intern_string_t SRC = intern_string::lookup("point");
    static const auto POINT_RE
        = lnav::pcre2pp::code::from_const(R"(^(?:([^:\s]+):)?([^:]+):(\d+)$)");
    static const auto STMT = R"(
    REPLACE INTO lnav_log_breakpoints (schema_id, description) VALUES (?, ?)
)";
    thread_local auto md = lnav::pcre2pp::match_data::unitialized();
    std::string retval;

    auto pat = trim(remaining_args(cmdline, args));
    shlex lexer(pat);
    auto split_args_res = lexer.split(ec.create_resolver());
    if (split_args_res.isErr()) {
        auto split_err = split_args_res.unwrapErr();
        auto um
            = lnav::console::user_message::error("unable to parse breakpoint")
                  .with_reason(split_err.se_error.te_msg)
                  .with_snippet(lnav::console::snippet::from(
                      SRC, lexer.to_attr_line(split_err.se_error)))
                  .move();

        return Err(um);
    }

    auto* tc = *lnav_data.ld_view_stack.top();
    auto* lss = dynamic_cast<logfile_sub_source*>(tc->get_sub_source());
    auto split_args = split_args_res.unwrap()
        | lnav::itertools::map([](const auto& elem) { return elem.se_value; });

    if (split_args.empty()) {
        if (lss == nullptr) {
            return ec.make_error(
                "A full breakpoint definition must be given if the "
                "top view is not the LOG view");
        }

        if (ec.ec_dry_run) {
            return Ok(retval);
        }

        auto win = lss->window_at(tc->get_selection().value());
        for (const auto& msg : *win) {
            auto format_name
                = msg.get_file_ptr()->get_format_name().to_string_fragment();
            auto src_file_sf = msg.get_string_for_attr(SA_SRC_FILE);
            auto src_line_sf = msg.get_string_for_attr(SA_SRC_LINE);
            if (src_file_sf && src_line_sf) {
                auto point = fmt::format(FMT_STRING("{}:{}:{}"),
                                         format_name,
                                         src_file_sf.value(),
                                         src_line_sf.value());
                split_args.emplace_back(point);
            } else {
                log_data_helper ldh(*lss);

                ldh.load_line(tc->get_selection().value(), true);
                ldh.parse_body();

                if (ldh.ldh_parser) {
                    auto desc = fmt::format(FMT_STRING("{}:#:0"), format_name);
                    auto prep_res
                        = prepare_stmt(lnav_data.ld_db.in(),
                                       STMT,
                                       ldh.ldh_parser->dp_schema_id.to_string(),
                                       desc);
                    auto stmt = prep_res.unwrap();
                    auto exec_res = stmt.execute();
                    if (exec_res.isErr()) {
                        return ec.make_error("failed to insert breakpoint: {}",
                                             exec_res.unwrapErr());
                    }
                }
            }
        }
    }

    std::vector<std::string> added;
    for (const auto& point : split_args) {
        if (POINT_RE.capture_from(point).into(md).found_p()) {
            std::string format_name;

            if (md[1]) {
                format_name = md[1]->to_string();
            } else {
                if (lss == nullptr) {
                    return ec.make_error(
                        "A format must be included with the breakpoint if the "
                        "top view is not the LOG view");
                }

                auto line_pair = lss->find_line_with_file(tc->get_selection());
                if (!line_pair) {
                    return ec.make_error("cannot find line");
                }

                format_name = line_pair->first->get_format_name().to_string();
            }

            auto desc = fmt::format(FMT_STRING("{}:{}:{}"),
                                    format_name,
                                    md[2].value(),
                                    md[3].value());

            added.emplace_back(desc);
            if (ec.ec_dry_run) {
                continue;
            }

            auto h = hasher();
            h.update(format_name);
            h.update(md[2].value());
            h.update(md[3].value());

            auto schema_id = h.to_string();

            log_info(
                "adding breakpoint: %s %s", schema_id.c_str(), desc.c_str());
            auto prep_res
                = prepare_stmt(lnav_data.ld_db.in(), STMT, schema_id, desc);
            auto stmt = prep_res.unwrap();
            auto exec_res = stmt.execute();
            if (exec_res.isErr()) {
                return ec.make_error("failed to insert breakpoint: {}",
                                     exec_res.unwrapErr());
            }
        } else {
            auto um = ec.make_error_msg("Invalid breakpoint: {}", point)
                          .with_help(
                              "Expecting an argument of the form: "
                              "<format>:<file>:<line>");
            return Err(um);
        }
    }

    if (!added.empty()) {
        retval = fmt::format(FMT_STRING("info: added breakpoints -- {}"),
                             fmt::join(added, ", "));
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_clear_breakpoint(exec_context& ec,
                     std::string cmdline,
                     std::vector<std::string>& args)
{
    static const intern_string_t SRC = intern_string::lookup("pattern");
    static const auto STMT = R"(
    DELETE FROM lnav_log_breakpoints WHERE description GLOB ?
)";
    static const auto DELETE_CURRENT = R"(
    DELETE FROM lnav_log_breakpoints WHERE schema_id = (
      SELECT log_msg_schema FROM all_logs WHERE log_line = log_msg_line())
)";
    thread_local auto md = lnav::pcre2pp::match_data::unitialized();
    std::string retval;

    auto pat = trim(remaining_args(cmdline, args));
    shlex lexer(pat);
    auto split_args_res = lexer.split(ec.create_resolver());
    if (split_args_res.isErr()) {
        auto split_err = split_args_res.unwrapErr();
        auto um = lnav::console::user_message::error(
                      "unable to parse breakpoint pattern")
                      .with_reason(split_err.se_error.te_msg)
                      .with_snippet(lnav::console::snippet::from(
                          SRC, lexer.to_attr_line(split_err.se_error)))
                      .move();

        return Err(um);
    }

    if (ec.ec_dry_run) {
        return Ok(retval);
    }

    auto split_args = split_args_res.unwrap()
        | lnav::itertools::map([](const auto& elem) { return elem.se_value; });

    if (split_args.empty()) {
        auto* tc = *lnav_data.ld_view_stack.top();
        auto* lss = dynamic_cast<logfile_sub_source*>(tc->get_sub_source());
        if (lss == nullptr) {
            return ec.make_error(
                "A pattern must be given if not in the LOG view");
        }

        auto prep_res = prepare_stmt(lnav_data.ld_db.in(), DELETE_CURRENT);
        auto stmt = prep_res.unwrap();
        auto exec_res = stmt.execute();
        if (exec_res.isErr()) {
            return ec.make_error("failed to clear breakpoint: {}",
                                 exec_res.unwrapErr());
        }
    }

    for (const auto& pat_arg : split_args) {
        auto prep_res = prepare_stmt(lnav_data.ld_db.in(), STMT, pat_arg);
        auto stmt = prep_res.unwrap();
        auto exec_res = stmt.execute();
        if (exec_res.isErr()) {
            return ec.make_error("failed to clear breakpoint: {}",
                                 exec_res.unwrapErr());
        }
    }

    retval = "info: deleted breakpoints";

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_toggle_breakpoint(exec_context& ec,
                      std::string cmdline,
                      std::vector<std::string>& args)
{
    if (args.size() > 1) {
        return ec.make_error("This command does not take any arguments");
    }

    if (!lnav_data.ld_views[LNV_LOG].get_selection()) {
        return ec.make_error("The LOG view is empty");
    }

    std::string retval;

    static const auto CHECK_STMT = R"(
    SELECT schema_id FROM lnav_log_breakpoints WHERE schema_id IN (
        SELECT log_msg_schema FROM all_logs WHERE log_line = log_msg_line())
)";
    auto prep_res = prepare_stmt(lnav_data.ld_db.in(), CHECK_STMT);
    auto stmt = prep_res.unwrap();
    auto row = stmt.fetch_row<std::string>();
    if (row.is<prepared_stmt::end_of_rows>()) {
        return com_breakpoint(ec, cmdline, args);
    }
    return com_clear_breakpoint(ec, cmdline, args);
}

static readline_context::command_t BREAKPOINT_COMMANDS[] = {
    {
        "breakpoint",
        com_breakpoint,
        help_text(":breakpoint")
            .with_summary("Set a breakpoint for the given "
                          "[<format>:]<file>:<line> tuples "
                          "or the current line")
            .with_parameter(
                help_text("point")
                    .with_summary(
                        "The file and line number of the breakpoint.  If the "
                        "format is different from the currently focused one, "
                        "the format name should be used as the prefix")
                    .with_format(help_parameter_format_t::HPF_BREAKPOINT)
                    .zero_or_more())
            .with_example({
                "To set a breakpoint for a log message at foo.cc:32",
                "foo.cc:32",
            }),
    },
    {
        "toggle-breakpoint",
        com_toggle_breakpoint,
        help_text(":toggle-breakpoint")
            .with_summary(
                "Toggle a breakpoint for the focused line in the LOG view"),
    },
    {
        "clear-breakpoint",
        com_clear_breakpoint,
        help_text(":clear-breakpoint")
            .with_summary(
                "Clear the breakpoints that match the given glob pattern")
            .with_parameter(
                help_text("pattern")
                    .with_summary(
                        "The glob pattern to use when matching the breakpoint "
                        "definition of the format <format>:<file>:<line>")
                    .with_format(help_parameter_format_t::HPF_KNOWN_BREAKPOINT)
                    .one_or_more())
            .with_example({"To clear all breakpoints", "*"}),
    },
};

void
init_lnav_breakpoint_commands(readline_context::command_map_t& cmd_map)
{
    for (auto& cmd : BREAKPOINT_COMMANDS) {
        cmd.c_help.index_tags();
        cmd_map[cmd.c_name] = &cmd;
    }
}
