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

#include "base/itertools.hh"
#include "lnav.hh"
#include "lnav_commands.hh"

static Result<std::string, lnav::console::user_message>
com_mark(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty() || lnav_data.ld_view_stack.empty()) {
    } else if (!ec.ec_dry_run) {
        auto* tc = *lnav_data.ld_view_stack.top();
        lnav_data.ld_last_user_mark[tc] = tc->get_selection();
        tc->toggle_user_mark(&textview_curses::BM_USER,
                             vis_line_t(lnav_data.ld_last_user_mark[tc]));
        tc->reload_data();
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_goto_mark(exec_context& ec,
              std::string cmdline,
              std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("mark-type");
    } else {
        static const std::set<const bookmark_type_t*> DEFAULT_TYPES = {
            &textview_curses::BM_USER,
            &textview_curses::BM_USER_EXPR,
            &textview_curses::BM_META,
        };

        textview_curses* tc = get_textview_for_mode(lnav_data.ld_mode);
        std::set<const bookmark_type_t*> mark_types;

        if (args.size() > 1) {
            for (size_t lpc = 1; lpc < args.size(); lpc++) {
                auto bt_opt = bookmark_type_t::find_type(args[lpc]);
                if (!bt_opt) {
                    auto um
                        = lnav::console::user_message::error(
                              attr_line_t("unknown bookmark type: ")
                                  .append(args[lpc]))
                              .with_snippets(ec.ec_source)
                              .with_help(
                                  attr_line_t("available types: ")
                                      .join(bookmark_type_t::get_all_types()
                                                | lnav::itertools::map(
                                                    &bookmark_type_t::get_name)
                                                | lnav::itertools::sorted(),
                                            ", "))
                              .move();
                    return Err(um);
                }
                mark_types.insert(bt_opt.value());
            }
        } else {
            mark_types = DEFAULT_TYPES;
        }

        if (!ec.ec_dry_run) {
            std::optional<vis_line_t> new_top;

            if (args[0] == "next-mark") {
                auto search_from_top = search_forward_from(tc);

                for (const auto& bt : mark_types) {
                    auto bt_top
                        = next_cluster(&bookmark_vector<vis_line_t>::next,
                                       bt,
                                       search_from_top);

                    if (bt_top && (!new_top || bt_top < new_top.value())) {
                        new_top = bt_top;
                    }
                }

                if (!new_top) {
                    auto um = lnav::console::user_message::info(fmt::format(
                        FMT_STRING("no more {} bookmarks after here"),
                        fmt::join(mark_types
                                      | lnav::itertools::map(
                                          &bookmark_type_t::get_name),
                                  ", ")));

                    return Err(um);
                }
            } else {
                for (const auto& bt : mark_types) {
                    auto bt_top
                        = next_cluster(&bookmark_vector<vis_line_t>::prev,
                                       bt,
                                       tc->get_selection());

                    if (bt_top && (!new_top || bt_top > new_top.value())) {
                        new_top = bt_top;
                    }
                }

                if (!new_top) {
                    auto um = lnav::console::user_message::info(fmt::format(
                        FMT_STRING("no more {} bookmarks before here"),
                        fmt::join(mark_types
                                      | lnav::itertools::map(
                                          &bookmark_type_t::get_name),
                                  ", ")));

                    return Err(um);
                }
            }

            if (new_top) {
                tc->get_sub_source()->get_location_history() |
                    [new_top](auto lh) {
                        lh->loc_history_append(new_top.value());
                    };
                tc->set_selection(new_top.value());
            }
            lnav_data.ld_bottom_source.grep_error("");
        }
    }

    return Ok(retval);
}

void
init_lnav_bookmark_commands(readline_context::command_map_t& cmd_map)
{
    static readline_context::command_t BOOKMARK_COMMANDS[] = {

        {
            "mark",
            com_mark,

            help_text(":mark")
                .with_summary(
                    "Toggle the bookmark state for the focused line in the "
                    "current view")
                .with_tags({"bookmarks"}),
        },

        {
            "next-mark",
            com_goto_mark,

            help_text(":next-mark")
                .with_summary(
                    "Move to the next bookmark of the given type in the "
                    "current view")
                .with_parameter(
                    help_text("type",
                              "The type of bookmark -- error, warning, "
                              "search, user, file, meta")
                        .one_or_more()
                        .with_enum_values(bookmark_type_t::get_type_names()))
                .with_example({"To go to the next error", "error"})
                .with_tags({"bookmarks", "navigation"}),
        },
        {
            "prev-mark",
            com_goto_mark,

            help_text(":prev-mark")
                .with_summary("Move to the previous bookmark of the given "
                              "type in the "
                              "current view")
                .with_parameter(
                    help_text("type",
                              "The type of bookmark -- error, warning, "
                              "search, user, file, meta")
                        .one_or_more()
                        .with_enum_values(bookmark_type_t::get_type_names()))
                .with_example({"To go to the previous error", "error"})
                .with_tags({"bookmarks", "navigation"}),
        },
    };

    for (auto& cmd : BOOKMARK_COMMANDS) {
        cmd.c_help.index_tags();
        cmd_map[cmd.c_name] = &cmd;
    }
}
