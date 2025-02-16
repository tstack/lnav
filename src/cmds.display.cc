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
#include "readline_context.hh"

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
            auto& dls = lnav_data.ld_db_row_source;

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
                    auto cl = lss.at(tc->get_selection());
                    auto lf = lss.find(cl);
                    format = lf->get_format();
                    name = intern_string::lookup(args[lpc]);
                }

                if (format->hide_field(name, hide)) {
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

static readline_context::command_t DISPLAY_COMMANDS[] = {
    {
        "set-text-view-mode",
        com_set_text_view_mode,
        help_text(":set-text-view-mode")
            .with_summary("Set the display mode for text files")
            .with_parameter(help_text{"mode"}
                                .with_summary("The display mode")
                                .with_enum_values({"raw", "rendered"}))
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
};

void
init_lnav_display_commands(readline_context::command_map_t& cmd_map)
{
    for (auto& cmd : DISPLAY_COMMANDS) {
        cmd.c_help.index_tags();
        cmd_map[cmd.c_name] = &cmd;
    }
}
