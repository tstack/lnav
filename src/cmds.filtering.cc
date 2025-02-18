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
            if (tc->get_inner_height() > 0) {
                exttm tm;

                auto vl = tc->get_selection();
                auto log_vl_ri = ttt->time_for_row(vl);
                if (log_vl_ri) {
                    tm = exttm::from_tv(log_vl_ri.value().ri_time);
                    tv_opt = parse_res.unwrap().adjust(tm).to_timeval();
                }
            }
        } else if (dts.convert_to_timeval(all_args, tv_abs)) {
            tv_opt = tv_abs;
        } else {
            return ec.make_error(FMT_STRING("invalid time value: {}"),
                                 all_args);
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

static Result<std::string, lnav::console::user_message> com_enable_filter(
    exec_context& ec, std::string cmdline, std::vector<std::string>& args);

static Result<std::string, lnav::console::user_message>
com_filter(exec_context& ec,
           std::string cmdline,
           std::vector<std::string>& args)
{
    std::string retval;

    auto tc = *lnav_data.ld_view_stack.top();
    auto tss = tc->get_sub_source();

    if (!tss->tss_supports_filtering) {
        return ec.make_error("{} view does not support filtering",
                             lnav_view_strings[tc - lnav_data.ld_views]);
    }
    if (args.size() > 1) {
        const static intern_string_t PATTERN_SRC
            = intern_string::lookup("pattern");

        auto* tss = tc->get_sub_source();
        auto& fs = tss->get_filters();
        auto re_frag = remaining_args_frag(cmdline, args);
        args[1] = re_frag.to_string();
        if (fs.get_filter(args[1]) != nullptr) {
            return com_enable_filter(ec, cmdline, args);
        }

        if (fs.full()) {
            return ec.make_error(
                "filter limit reached, try combining "
                "filters with a pipe symbol (e.g. foo|bar)");
        }

        auto compile_res = lnav::pcre2pp::code::from(args[1], PCRE2_CASELESS);

        if (compile_res.isErr()) {
            auto ce = compile_res.unwrapErr();
            auto um = lnav::console::to_user_message(PATTERN_SRC, ce);
            return Err(um);
        }
        if (ec.ec_dry_run) {
            if (args[0] == "filter-in" && !fs.empty()) {
                lnav_data.ld_preview_status_source[0]
                    .get_description()
                    .set_value(
                        "Match preview for :filter-in only works if there are "
                        "no "
                        "other filters");
                retval = "";
            } else {
                auto& hm = tc->get_highlights();
                highlighter hl(compile_res.unwrap().to_shared());
                auto role = (args[0] == "filter-out") ? role_t::VCR_DIFF_DELETE
                                                      : role_t::VCR_DIFF_ADD;

                hl.with_role(role);
                hl.with_attrs(text_attrs::with_styles(
                    text_attrs::style::blink, text_attrs::style::reverse));

                hm[{highlight_source_t::PREVIEW, "preview"}] = hl;
                tc->reload_data();

                lnav_data.ld_preview_status_source[0]
                    .get_description()
                    .set_value(
                        "Matches are highlighted in %s in the text view",
                        role == role_t::VCR_DIFF_DELETE ? "red" : "green");

                retval = "";
            }
        } else {
            auto lt = (args[0] == "filter-out") ? text_filter::EXCLUDE
                                                : text_filter::INCLUDE;
            auto filter_index = fs.next_index();
            if (!filter_index) {
                return ec.make_error("too many filters");
            }
            auto pf = std::make_shared<pcre_filter>(
                lt, args[1], *filter_index, compile_res.unwrap().to_shared());

            log_debug("%s [%d] %s",
                      args[0].c_str(),
                      pf->get_index(),
                      args[1].c_str());
            fs.add_filter(pf);
            const auto start_time = std::chrono::steady_clock::now();
            tss->text_filters_changed();
            const auto end_time = std::chrono::steady_clock::now();
            const double duration
                = std::chrono::duration_cast<std::chrono::milliseconds>(
                      end_time - start_time)
                      .count()
                / 1000.0;

            retval = fmt::format(FMT_STRING("info: filter activated in {:.3}s"),
                                 duration);
        }
    } else {
        return ec.make_error("expecting a regular expression to filter");
    }

    return Ok(retval);
}

static readline_context::prompt_result_t
com_filter_prompt(exec_context& ec, const std::string& cmdline)
{
    const auto* tc = lnav_data.ld_view_stack.top().value();
    std::vector<std::string> args;

    split_ws(cmdline, args);
    if (args.size() > 1) {
        return {};
    }

    if (tc->tc_selected_text) {
        return {"", tc->tc_selected_text->sti_value};
    }

    return {"", tc->get_current_search()};
}

static Result<std::string, lnav::console::user_message>
com_enable_filter(exec_context& ec,
                  std::string cmdline,
                  std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("disabled-filter");
    } else if (args.size() > 1) {
        auto* tc = *lnav_data.ld_view_stack.top();
        auto* tss = tc->get_sub_source();
        auto& fs = tss->get_filters();
        std::shared_ptr<text_filter> lf;

        args[1] = remaining_args(cmdline, args);
        lf = fs.get_filter(args[1]);
        if (lf == nullptr) {
            return ec.make_error("no such filter -- {}", args[1]);
        }
        if (lf->is_enabled()) {
            retval = "info: filter already enabled";
        } else if (ec.ec_dry_run) {
            retval = "";
        } else {
            fs.set_filter_enabled(lf, true);
            tss->text_filters_changed();
            retval = "info: filter enabled";
        }
    } else {
        return ec.make_error("expecting disabled filter to enable");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_disable_filter(exec_context& ec,
                   std::string cmdline,
                   std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("enabled-filter");
    } else if (args.size() > 1) {
        auto* tc = *lnav_data.ld_view_stack.top();
        auto* tss = tc->get_sub_source();
        auto& fs = tss->get_filters();
        std::shared_ptr<text_filter> lf;

        args[1] = remaining_args(cmdline, args);
        lf = fs.get_filter(args[1]);
        if (lf == nullptr) {
            return ec.make_error("no such filter -- {}", args[1]);
        }
        if (!lf->is_enabled()) {
            retval = "info: filter already disabled";
        } else if (ec.ec_dry_run) {
            retval = "";
        } else {
            fs.set_filter_enabled(lf, false);
            tss->text_filters_changed();
            retval = "info: filter disabled";
        }
    } else {
        return ec.make_error("expecting enabled filter to disable");
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
    {
        "filter-in",
        com_filter,

        help_text(":filter-in")
            .with_summary("Only show lines that match the given regular "
                          "expression in the current view")
            .with_parameter(
                help_text("pattern", "The regular expression to match")
                    .with_format(help_parameter_format_t::HPF_REGEX))
            .with_tags({"filtering"})
            .with_example({"To filter out log messages that do not have the "
                           "string 'dhclient'",
                           "dhclient"}),
        com_filter_prompt,
    },
    {
        "filter-out",
        com_filter,

        help_text(":filter-out")
            .with_summary("Remove lines that match the given "
                          "regular expression "
                          "in the current view")
            .with_parameter(
                help_text("pattern", "The regular expression to match")
                    .with_format(help_parameter_format_t::HPF_REGEX))
            .with_tags({"filtering"})
            .with_example({"To filter out log messages that "
                           "contain the string "
                           "'last message repeated'",
                           "last message repeated"}),
        com_filter_prompt,
    },
    {
        "enable-filter",
        com_enable_filter,

        help_text(":enable-filter")
            .with_summary("Enable a previously created and disabled filter")
            .with_parameter(help_text(
                "pattern", "The regular expression used in the filter command"))
            .with_tags({"filtering"})
            .with_opposites({"disable-filter"})
            .with_example({"To enable the disabled filter with the "
                           "pattern 'last "
                           "message repeated'",
                           "last message repeated"}),
    },
    {
        "disable-filter",
        com_disable_filter,

        help_text(":disable-filter")
            .with_summary("Disable a filter created with filter-in/filter-out")
            .with_parameter(help_text(
                "pattern", "The regular expression used in the filter command"))
            .with_tags({"filtering"})
            .with_opposites({"filter-out", "filter-in"})
            .with_example({"To disable the filter with the pattern 'last "
                           "message repeated'",
                           "last message repeated"}),
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
