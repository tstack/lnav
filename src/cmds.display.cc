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

#include "base/auto_mem.hh"
#include "base/intern_string.hh"
#include "lnav.hh"
#include "lnav_commands.hh"
#include "readline_context.hh"
#include "readline_highlighters.hh"
#include "timeline_source.hh"

static Result<std::string, lnav::console::user_message>
com_set_text_view_mode(exec_context& ec,
                       std::string cmdline,
                       std::vector<std::string>& args)
{
    std::string retval;
    std::optional<textfile_sub_source::view_mode> vm_opt;

    if (args.size() > 1) {
        if (args[1] == "raw") {
            vm_opt = textfile_sub_source::view_mode::raw;
        } else if (args[1] == "rendered") {
            vm_opt = textfile_sub_source::view_mode::rendered;
        }
    }

    if (!vm_opt) {
        return ec.make_error("expecting a view mode of 'raw' or 'rendered'");
    }

    if (!ec.ec_dry_run) {
        lnav_data.ld_text_source.set_view_mode(vm_opt.value());
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_toggle_field(exec_context& ec,
                 std::string cmdline,
                 std::vector<std::string>& args)
{
    std::string retval;

    if (args.size() < 2) {
        return ec.make_error("Expecting a log message field name");
    }
    auto* tc = *lnav_data.ld_view_stack.top();
    const auto hide = args[0] == "hide-fields";
    std::vector<std::string> found_fields, missing_fields;

    if (tc != &lnav_data.ld_views[LNV_LOG] && tc != &lnav_data.ld_views[LNV_DB])
    {
        retval = "error: hiding fields only works in the log view";
    } else if (ec.ec_dry_run) {
        // TODO: highlight the fields to be hidden.
        retval = "";
    } else {
        if (tc == &lnav_data.ld_views[LNV_DB]) {
            auto& dls = *ec.ec_label_source_stack.back();

            for (size_t lpc = 1; lpc < args.size(); lpc++) {
                const auto& name = args[lpc];

                auto col_opt = dls.column_name_to_index(name);
                if (col_opt.has_value()) {
                    found_fields.emplace_back(name);

                    dls.dls_headers[col_opt.value()].hm_hidden = hide;
                } else {
                    missing_fields.emplace_back(name);
                }
            }
            tc->set_needs_update();
            tc->reload_data();
        } else if (tc == &lnav_data.ld_views[LNV_LOG]) {
            const auto& lss = lnav_data.ld_log_source;

            for (int lpc = 1; lpc < (int) args.size(); lpc++) {
                intern_string_t name;
                std::shared_ptr<log_format> format;
                auto sel = tc->get_selection();
                size_t dot;

                if ((dot = args[lpc].find('.')) != std::string::npos) {
                    const intern_string_t format_name
                        = intern_string::lookup(args[lpc].c_str(), dot);

                    format = log_format::find_root_format(format_name.get());
                    if (!format) {
                        return ec.make_error("unknown format -- {}",
                                             format_name.to_string());
                    }
                    name = intern_string::lookup(&(args[lpc].c_str()[dot + 1]),
                                                 args[lpc].length() - dot - 1);
                } else if (tc->get_inner_height() == 0) {
                    return ec.make_error("no log messages to hide");
                } else {
                    auto cl = lss.at(sel.value_or(0_vl));
                    auto lf = lss.find(cl);
                    format = lf->get_format();
                    name = intern_string::lookup(args[lpc]);
                }

                if (format && format->hide_field(name, hide)) {
                    found_fields.push_back(args[lpc]);
                    if (hide) {
#if 0
                            if (lnav_data.ld_rl_view != nullptr) {
                                lnav_data.ld_rl_view->set_alt_value(
                                    HELP_MSG_1(x,
                                               "to quickly show hidden "
                                               "fields"));
                            }
#endif
                    }
                    tc->set_needs_update();
                } else {
                    missing_fields.push_back(args[lpc]);
                }
            }
        }
        if (missing_fields.empty()) {
            auto visibility = hide ? "hiding" : "showing";
            retval = fmt::format(FMT_STRING("info: {} field(s) -- {}"),
                                 visibility,
                                 fmt::join(found_fields, ", "));
        } else {
            return ec.make_error("unknown field(s) -- {}",
                                 fmt::join(missing_fields, ", "));
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_timeline_row_type_visibility(exec_context& ec,
                                 std::string cmdline,
                                 std::vector<std::string>& args)
{
    std::string retval;

    if (args.size() < 2) {
        auto* tss = static_cast<timeline_source*>(
            lnav_data.ld_views[LNV_TIMELINE].get_sub_source());
        tss->ts_preview_hidden_row_types.clear();
        lnav_data.ld_views[LNV_TIMELINE].set_needs_update();
        return ec.make_error("Expecting a row type");
    }

    const auto hide = args[0] == "hide-in-timeline";
    std::vector<std::string> found_types, unknown_types;

    for (size_t lpc = 1; lpc < args.size(); lpc++) {
        auto rt_opt = timeline_source::row_type_from_string(args[lpc]);
        if (rt_opt) {
            found_types.emplace_back(args[lpc]);
        } else {
            unknown_types.emplace_back(args[lpc]);
        }
    }

    if (!unknown_types.empty()) {
        return ec.make_error("unknown row type(s) -- {}",
                             fmt::join(unknown_types, ", "));
    }

    auto* tss = static_cast<timeline_source*>(
        lnav_data.ld_views[LNV_TIMELINE].get_sub_source());
    if (ec.ec_dry_run) {
        tss->ts_preview_hidden_row_types.clear();
        if (hide) {
            for (const auto& type_name : found_types) {
                auto rt_opt = timeline_source::row_type_from_string(type_name);
                tss->ts_preview_hidden_row_types.insert(rt_opt.value());
            }
        }
        lnav_data.ld_views[LNV_TIMELINE].set_needs_update();
        retval = "";
    } else {
        tss->ts_preview_hidden_row_types.clear();
        for (const auto& type_name : found_types) {
            auto rt_opt = timeline_source::row_type_from_string(type_name);
            tss->set_row_type_visibility(rt_opt.value(), !hide);
        }
        tss->text_filters_changed();

        auto visibility = hide ? "hiding" : "showing";
        retval = fmt::format(FMT_STRING("info: {} row type(s) -- {}"),
                             visibility,
                             fmt::join(found_types, ", "));
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_timeline_metric(exec_context& ec,
                    std::string cmdline,
                    std::vector<std::string>& args)
{
    if (args.size() < 2) {
        return ec.make_error("Expecting a metric name (source.metric)");
    }

    auto* tss = static_cast<timeline_source*>(
        lnav_data.ld_views[LNV_TIMELINE].get_sub_source());

    // Validation runs in both live and dry-run modes so the prompt's
    // live preview surfaces the same errors the user would see on
    // commit.  Only the actual mutation (add_metric) is skipped under
    // dry-run.
    if (args[1].find_first_of(" \t\n\r") != std::string::npos) {
        return ec.make_error(
            "Metric name must not contain whitespace (got '{}')", args[1]);
    }

    if (ec.ec_dry_run) {
        return Ok(std::string());
    }

    timeline_source::metric_def md;
    md.md_kind = timeline_source::metric_kind::shorthand;
    md.md_label = args[1];
    md.md_query.clear();

    auto add_res = tss->add_metric(md);
    if (add_res.isErr()) {
        return Err(add_res.unwrapErr());
    }
    return Ok(fmt::format(FMT_STRING("info: tracking metric -- {}"), args[1]));
}

static Result<std::string, lnav::console::user_message>
com_timeline_metric_sql(exec_context& ec,
                        std::string cmdline,
                        std::vector<std::string>& args)
{
    if (args.size() < 3) {
        return ec.make_error(
            "Expecting a label and a SQL query returning log_time, value");
    }

    auto* tss = static_cast<timeline_source*>(
        lnav_data.ld_views[LNV_TIMELINE].get_sub_source());
    auto label = args[1];
    if (label.find_first_of(" \t\n\r") != std::string::npos) {
        return ec.make_error(
            "Metric label must not contain whitespace (got '{}')", label);
    }
    auto sql = trim(remaining_args(cmdline, args, 2));
    if (sql.empty()) {
        return ec.make_error("Expecting a SQL query");
    }
    // Collapse whitespace runs (including embedded newlines and tabs)
    // into single spaces so the command survives a session save/replay
    // cycle — session files are line-oriented and an embedded newline
    // would split the command across lines on reload.
    std::string normalized_sql;
    normalized_sql.reserve(sql.size());
    bool in_space = false;
    for (const auto ch : sql) {
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            if (!in_space) {
                normalized_sql.push_back(' ');
                in_space = true;
            }
        } else {
            normalized_sql.push_back(ch);
            in_space = false;
        }
    }

    // Prepare the user's query as-is so syntax/table/column errors
    // point at the text the user typed.  Then verify the output
    // columns separately — the timeline needs `log_time` and `value`,
    // and complaining about those ourselves gives a clearer message
    // than letting the collection-time wrapper fail later.
    auto_mem<sqlite3_stmt> stmt(sqlite3_finalize);
    auto prep_rc = sqlite3_prepare_v2(lnav_data.ld_db.in(),
                                      normalized_sql.c_str(),
                                      normalized_sql.size(),
                                      stmt.out(),
                                      nullptr);
    auto sql_al = attr_line_t(normalized_sql)
                      .with_attr_for_all(VC_ROLE.value(role_t::VCR_QUOTED_CODE))
                      .move();
    readline_sql_highlighter(sql_al, lnav::sql::dialect::sqlite, std::nullopt);
    if (prep_rc != SQLITE_OK) {
        const auto* errmsg = sqlite3_errmsg(lnav_data.ld_db.in());
        return Err(
            lnav::console::user_message::error(
                attr_line_t("invalid timeline metric query: ").append(sql_al))
                .with_reason(errmsg)
                .with_snippets(ec.ec_source));
    }
    bool has_log_time = false;
    bool has_value = false;
    const auto col_count = sqlite3_column_count(stmt.in());
    for (int i = 0; i < col_count; i++) {
        const auto* name = sqlite3_column_name(stmt.in(), i);
        if (name == nullptr) {
            continue;
        }
        if (strcmp(name, "log_time") == 0) {
            has_log_time = true;
        } else if (strcmp(name, "value") == 0) {
            has_value = true;
        }
    }
    if (!has_log_time || !has_value) {
        static const auto EXAMPLE_SQL
            = attr_line_t("SELECT log_time, rss AS value FROM procstate_procs")
                  .with_attr_for_all(VC_ROLE.value(role_t::VCR_QUOTED_CODE));

        std::vector<std::string> missing;
        if (!has_log_time) {
            missing.emplace_back("log_time");
        }
        if (!has_value) {
            missing.emplace_back("value");
        }
        return Err(
            lnav::console::user_message::error(
                attr_line_t("timeline metric query is missing required "
                            "column(s): ")
                    .append(lnav::roles::identifier(fmt::format(
                        FMT_STRING("{}"), fmt::join(missing, ", ")))))
                .with_reason(attr_line_t("the query: ").append(sql_al))
                .with_help(
                    attr_line_t("the query must return columns named "
                                "'log_time' and 'value' — use AS to alias if "
                                "needed, e.g. ")
                        .append(EXAMPLE_SQL))
                .with_snippets(ec.ec_source));
    }

    if (ec.ec_dry_run) {
        return Ok(std::string());
    }

    timeline_source::metric_def md;
    md.md_kind = timeline_source::metric_kind::sql;
    md.md_label = label;
    md.md_query = std::move(normalized_sql);

    auto add_res = tss->add_metric(md);
    if (add_res.isErr()) {
        return Err(add_res.unwrapErr());
    }
    return Ok(fmt::format(FMT_STRING("info: tracking metric -- {}"), label));
}

static Result<std::string, lnav::console::user_message>
com_clear_timeline_metric(exec_context& ec,
                          std::string cmdline,
                          std::vector<std::string>& args)
{
    if (args.size() < 2) {
        return ec.make_error("Expecting a metric label");
    }

    auto* tss = static_cast<timeline_source*>(
        lnav_data.ld_views[LNV_TIMELINE].get_sub_source());

    // Validation (label lookup) runs in both modes so a typo surfaces
    // live in the prompt; the actual removal is skipped under dry-run.
    const auto found = std::any_of(
        tss->ts_metrics.begin(), tss->ts_metrics.end(), [&](const auto& ms) {
            return ms.ms_def.md_label == args[1];
        });
    if (!found) {
        return ec.make_error("no such metric -- {}", args[1]);
    }

    if (ec.ec_dry_run) {
        return Ok(std::string());
    }

    tss->remove_metric(args[1]);
    return Ok(fmt::format(FMT_STRING("info: stopped tracking metric -- {}"),
                          args[1]));
}

static readline_context::command_t DISPLAY_COMMANDS[] = {
    {
        "set-text-view-mode",
        com_set_text_view_mode,
        help_text(":set-text-view-mode")
            .with_summary("Set the display mode for text files")
            .with_parameter(help_text{"mode"}
                                .with_summary("The display mode")
                                .with_enum_values({
                                    "raw"_frag,
                                    "rendered"_frag,
                                }))
            .with_tags({"display"}),
    },
    {
        "hide-fields",
        com_toggle_field,

        help_text(":hide-fields")
            .with_summary("Hide log message fields by replacing them "
                          "with an ellipsis")
            .with_parameter(
                help_text("field-name",
                          "The name of the field to hide in the format for "
                          "the focused log line.  A qualified name can be used "
                          "where the field name is prefixed by the format name "
                          "and a dot to hide any field.")
                    .one_or_more()
                    .with_format(help_parameter_format_t::HPF_FORMAT_FIELD))
            .with_example({"To hide the log_procname fields in all formats",
                           "log_procname"})
            .with_example({"To hide only the log_procname field in "
                           "the syslog format",
                           "syslog_log.log_procname"})
            .with_tags({"display"}),
    },
    {
        "show-fields",
        com_toggle_field,

        help_text(":show-fields")
            .with_summary("Show log message fields that were previously hidden")
            .with_parameter(
                help_text("field-name", "The name of the field to show")
                    .one_or_more()
                    .with_format(help_parameter_format_t::HPF_FORMAT_FIELD))
            .with_example({"To show all the log_procname fields in all formats",
                           "log_procname"})
            .with_opposites({"hide-fields"})
            .with_tags({"display"}),
    },
    {
        "hide-in-timeline",
        com_timeline_row_type_visibility,

        help_text(":hide-in-timeline")
            .with_summary("Hide rows of the given type(s) in the timeline "
                          "view")
            .with_parameter(help_text("row-type", "The type of row to hide")
                                .one_or_more()
                                .with_enum_values({
                                    "logfile"_frag,
                                    "thread"_frag,
                                    "opid"_frag,
                                    "tag"_frag,
                                    "partition"_frag,
                                }))
            .with_example({"To hide logfile and thread rows", "logfile thread"})
            .with_opposites({"show-in-timeline"})
            .with_tags({"display"}),
    },
    {
        "show-in-timeline",
        com_timeline_row_type_visibility,

        help_text(":show-in-timeline")
            .with_summary("Show rows of the given type(s) that were "
                          "previously hidden in the timeline view")
            .with_parameter(help_text("row-type", "The type of row to show")
                                .one_or_more()
                                .with_enum_values({
                                    "logfile"_frag,
                                    "thread"_frag,
                                    "opid"_frag,
                                    "tag"_frag,
                                    "partition"_frag,
                                }))
            .with_example({"To show logfile and thread rows", "logfile thread"})
            .with_opposites({"hide-in-timeline"})
            .with_tags({"display"}),
    },
    {
        "timeline-metric",
        com_timeline_metric,

        help_text(":timeline-metric")
            .with_summary("Add a sparkline to the timeline view header "
                          "for the given metric")
            .with_parameter(
                help_text("name",
                          "The metric name in 'source.metric' form, as it "
                          "appears in the all_metrics table. Existing "
                          "labels are replaced.")
                    .with_format(help_parameter_format_t::HPF_TIMELINE_METRIC))
            .with_example(
                {"To show the cpu_pct column from cpu.csv", "cpu.cpu_pct"})
            .with_opposites({"clear-timeline-metric"})
            .with_tags({"display"}),
    },
    {
        "timeline-metric-sql",
        com_timeline_metric_sql,

        help_text(":timeline-metric-sql")
            .with_summary("Add a sparkline driven by a SQL query. The "
                          "query must return two columns named log_time "
                          "and value; it is wrapped as "
                          "SELECT * FROM (<sql>) WHERE log_time BETWEEN ...")
            .with_parameter(
                help_text("label", "The label to display for this metric"))
            .with_parameter(
                help_text("query", "A SQL SELECT returning log_time, value")
                    .with_format(help_parameter_format_t::HPF_SQL))
            .with_example({"Plot rss from procstate rows",
                           "rss SELECT log_time, rss FROM procstate_procs "
                           "WHERE proc='lnav'"})
            .with_opposites({"clear-timeline-metric"})
            .with_tags({"display"}),
    },
    {
        "clear-timeline-metric",
        com_clear_timeline_metric,

        help_text(":clear-timeline-metric")
            .with_summary("Remove a sparkline from the timeline view header")
            .with_parameter(
                help_text("label", "The label of the metric to remove")
                    .with_format(
                        help_parameter_format_t::HPF_ACTIVE_TIMELINE_METRIC))
            .with_opposites({"timeline-metric", "timeline-metric-sql"})
            .with_tags({"display"}),
    },
};

void
init_lnav_display_commands(readline_context::command_map_t& cmd_map)
{
    for (auto& cmd : DISPLAY_COMMANDS) {
        cmd.c_help.index_tags();
        cmd_map[cmd.c_name] = &cmd;
    }
}
