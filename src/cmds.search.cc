/**
 * Copyright (c) 2026, Timothy Stack
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

#include "base/lnav.console.hh"
#include "lnav.hh"
#include "cmds.hh"
#include "lnav.prompt.hh"

static Result<std::string, lnav::console::user_message>
com_create_named_search(exec_context& ec,
                        std::string cmdline,
                        std::vector<std::string>& args)
{
    static const intern_string_t PATTERN_SRC = intern_string::lookup("pattern");

    if (args.size() < 2) {
        return ec.make_error("expecting a name for the search");
    }

    auto* tc = *lnav_data.ld_view_stack.top();
    const auto& name = args[1];
    std::string pattern;
    auto adoption = textview_curses::search_adoption_t::keep;

    if (args.size() > 2) {
        pattern = remaining_args(cmdline, args, 2);
    } else {
        // The compiled form, since this is what the named search will be
        // re-created from when the session is restored.
        pattern = tc->get_current_search_pattern();
        if (pattern.empty()) {
            return ec.make_error(
                "no active search to name, provide a pattern instead");
        }
        // Only the form that leaves the pattern off takes the active search
        // over; one that spells the pattern out leaves it running.
        adoption = textview_curses::search_adoption_t::promote;
    }

    if (ec.ec_dry_run) {
        TRY(textview_curses::validate_search_name(name));

        auto compile_res = lnav::pcre2pp::code::from(pattern, PCRE2_CASELESS);

        if (compile_res.isErr()) {
            return Err(lnav::console::to_user_message(
                PATTERN_SRC, compile_res.unwrapErr()));
        }

        highlighter hl(compile_res.unwrap().to_shared());
        // Preview with the same background the real thing will use.
        text_attrs hl_attrs;

        hl_attrs.ta_bg_color = view_colors::singleton().color_for_ident(
            string_fragment::from_str(name));
        hl_attrs |= text_attrs::style::blink;
        hl.with_attrs(hl_attrs);
        tc->get_highlights()[{highlight_source_t::PREVIEW, "preview"}] = hl;

        lnav_data.ld_preview_status_source[0].get_description().set_value(
            "Matches are highlighted in the view"_frag);
        lnav_data.ld_status[LNS_PREVIEW0].set_needs_update();
        tc->reload_data();

        return Ok(std::string());
    }

    TRY(tc->create_named_search(name, pattern, adoption));
    tc->reload_data();
    lnav::prompt::get().p_editor.set_alt_value(HELP_MSG_FOCUS_SEARCH);

    return Ok("info: created named search -- " + name);
}

static Result<std::string, lnav::console::user_message>
com_delete_named_search(exec_context& ec,
                        std::string cmdline,
                        std::vector<std::string>& args)
{
    if (args.size() < 2) {
        return ec.make_error("expecting the name of a search to delete");
    }

    auto* tc = *lnav_data.ld_view_stack.top();
    const auto& name = args[1];

    if (ec.ec_dry_run) {
        return Ok(std::string());
    }

    if (!tc->delete_named_search(name)) {
        return ec.make_error("unknown named search -- {}", name);
    }
    tc->reload_data();

    return Ok("info: deleted named search -- " + name);
}

static Result<std::string, lnav::console::user_message>
com_set_named_search_enabled(exec_context& ec,
                             std::string cmdline,
                             std::vector<std::string>& args)
{
    const auto enable = args[0] == "enable-named-search";

    if (args.size() < 2) {
        return ec.make_error("expecting the name of a search to {}",
                             enable ? "enable" : "disable");
    }

    auto* tc = *lnav_data.ld_view_stack.top();
    const auto& name = args[1];
    const auto* ns = tc->find_named_search(name);

    if (ns == nullptr) {
        return ec.make_error("unknown named search -- {}", name);
    }
    if (ns->ns_enabled == enable) {
        return Ok(fmt::format(FMT_STRING("info: named search is already {}"),
                              enable ? "enabled" : "disabled"));
    }
    if (ec.ec_dry_run) {
        return Ok(std::string());
    }

    tc->set_named_search_enabled(name, enable);
    tc->reload_data();

    return Ok(fmt::format(FMT_STRING("info: {} named search -- {}"),
                          enable ? "enabled" : "disabled",
                          name));
}

/**
 * The search term field is otherwise only refreshed when the search itself
 * changes, so a change of focus has to ask for it.
 */
static void
refresh_search_status(textview_curses* tc)
{
    lnav_data.ld_bottom_source.update_search_term(*tc);
    lnav_data.ld_status[LNS_BOTTOM].set_needs_update();
    tc->set_needs_update();
}

static Result<std::string, lnav::console::user_message>
com_focus_search(exec_context& ec,
                 std::string cmdline,
                 std::vector<std::string>& args)
{
    auto* tc = *lnav_data.ld_view_stack.top();

    if (args.size() < 2) {
        if (!ec.ec_dry_run) {
            tc->clear_search_focus();
            refresh_search_status(tc);
        }
        return Ok(std::string("info: searching all of the searches again"));
    }

    const auto& name = args[1];
    const auto* ns = tc->find_named_search(name);

    if (ns == nullptr) {
        return ec.make_error("unknown named search -- {}", name);
    }
    if (!ns->ns_enabled) {
        return Err(
            lnav::console::user_message::error(
                attr_line_t("cannot focus ").append_quoted(name))
                .with_reason("the search is disabled")
                .with_help(attr_line_t("use :enable-named-search to turn it "
                                       "back on")));
    }
    if (ec.ec_dry_run) {
        return Ok(std::string());
    }

    tc->focus_named_search(name);
    refresh_search_status(tc);

    return Ok("info: focused named search -- " + name);
}

static Result<std::string, lnav::console::user_message>
com_cycle_search_focus(exec_context& ec,
                       std::string cmdline,
                       std::vector<std::string>& args)
{
    const auto dir = args[0] == "focus-next-search" ? 1 : -1;
    auto* tc = *lnav_data.ld_view_stack.top();

    if (ec.ec_dry_run) {
        return Ok(std::string());
    }

    auto slot = tc->cycle_search_focus(dir);

    refresh_search_status(tc);
    if (!slot) {
        return Ok(std::string("info: searching all of the searches"));
    }

    return Ok(fmt::format(
        FMT_STRING("info: focused search -- {}"),
        tc->get_focused_search_name().value_or("the current search")));
}

static Result<std::string, lnav::console::user_message>
com_goto_search_hit(exec_context& ec,
                    std::string cmdline,
                    std::vector<std::string>& args)
{
    const auto forward = args[0] == "next-search-hit";
    auto* tc = get_textview_for_mode(lnav_data.ld_mode);
    // An explicit name moves through that search without disturbing the
    // focus, which is what a script or a one-off keystroke wants.
    auto slot = tc->get_focused_search_slot();
    auto slot_name = tc->get_focused_search_name();

    if (args.size() > 1) {
        const auto& name = args[1];
        const auto* ns = tc->find_named_search(name);

        if (ns == nullptr) {
            return ec.make_error("unknown named search -- {}", name);
        }
        slot = ns->ns_slot;
        slot_name = name;
    }

    if (ec.ec_dry_run) {
        return Ok(std::string());
    }

    auto func = forward ? &bookmark_vector<vis_line_t>::next
                        : &bookmark_vector<vis_line_t>::prev;
    auto from = forward ? search_forward_from(tc) : tc->get_selection();

    if (!from) {
        return Ok(std::string());
    }

    // With nothing focused, the BM_SEARCH union is walked, which is the same
    // set of hits that :next-mark search moves through.
    auto new_top = slot
        ? next_cluster(func, tc->search_matches_for_slot(slot.value()),
                       from.value())
        : next_cluster(func, &textview_curses::BM_SEARCH, from.value());

    if (!new_top) {
        return Err(lnav::console::user_message::info(fmt::format(
            FMT_STRING("no more {}hits {} here"),
            slot_name ? fmt::format(FMT_STRING("{} "), slot_name.value()) : "",
            forward ? "after" : "before")));
    }

    tc->get_sub_source()->get_location_history() |
        [new_top](auto lh) { lh->loc_history_append(new_top.value()); };
    tc->set_selection(new_top.value());
    lnav_data.ld_bottom_source.grep_error("");
    lnav_data.ld_status[LNS_BOTTOM].set_needs_update();

    return Ok(std::string());
}

static lnav::commands::command_t SEARCH_COMMANDS[] = {
    {
        "create-named-search",
        com_create_named_search,

        help_text(":create-named-search")
            .with_summary("Give a name to a search so that it stays active "
                          "and highlighted while other searches are run")
            .with_parameter(
                help_text("name", "The name to give to the search"))
            .with_parameter(
                help_text("pattern",
                          "The regular expression to search for.  If not "
                          "given, the currently active search is used and "
                          "then cleared.")
                    .with_format(help_parameter_format_t::HPF_REGEX)
                    .optional())
            .with_tags({"search"})
            .with_opposites({"delete-named-search"})
            .with_example({"To name the currently active search 'req'",
                           "req"}),
    },
    {
        "delete-named-search",
        com_delete_named_search,

        help_text(":delete-named-search")
            .with_summary("Delete a search created with create-named-search")
            .with_parameter(
                help_text("name", "The name of the search to delete")
                    .with_format(
                        help_parameter_format_t::HPF_NAMED_SEARCHES))
            .with_tags({"search"})
            .with_opposites({"create-named-search"})
            .with_example({"To delete the named search 'req'", "req"}),
    },
    {
        "enable-named-search",
        com_set_named_search_enabled,

        help_text(":enable-named-search")
            .with_summary("Enable a named search that was disabled with "
                          "disable-named-search")
            .with_parameter(
                help_text("name", "The name of the search to enable")
                    .with_format(
                        help_parameter_format_t::HPF_DISABLED_NAMED_SEARCHES))
            .with_tags({"search"})
            .with_opposites({"disable-named-search"})
            .with_example({"To enable the named search 'req'", "req"}),
    },
    {
        "disable-named-search",
        com_set_named_search_enabled,

        help_text(":disable-named-search")
            .with_summary("Stop highlighting the hits of a named search "
                          "without deleting it")
            .with_parameter(
                help_text("name", "The name of the search to disable")
                    .with_format(
                        help_parameter_format_t::HPF_ENABLED_NAMED_SEARCHES))
            .with_tags({"search"})
            .with_opposites({"enable-named-search"})
            .with_example({"To disable the named search 'req'", "req"}),
    },
    {
        "focus-search",
        com_focus_search,

        help_text(":focus-search")
            .with_summary("Narrow the search hotkeys to a single search so "
                          "that only its hits are moved through")
            .with_parameter(
                help_text("name",
                          "The name of the search to focus.  If not given, "
                          "the hotkeys move through the hits of all of the "
                          "searches again.")
                    .with_format(
                        help_parameter_format_t::HPF_ENABLED_NAMED_SEARCHES)
                    .optional())
            .with_tags({"search", "navigation"})
            .with_example({"To move through the hits of 'req' alone", "req"}),
    },
    {
        "focus-next-search",
        com_cycle_search_focus,

        help_text(":focus-next-search")
            .with_summary("Focus the search after the one that is focused "
                          "now, wrapping around to all of them")
            .with_tags({"search", "navigation"})
            .with_opposites({"focus-prev-search"}),
    },
    {
        "focus-prev-search",
        com_cycle_search_focus,

        help_text(":focus-prev-search")
            .with_summary("Focus the search before the one that is focused "
                          "now, wrapping around to all of them")
            .with_tags({"search", "navigation"})
            .with_opposites({"focus-next-search"}),
    },
    {
        "next-search-hit",
        com_goto_search_hit,

        help_text(":next-search-hit")
            .with_summary("Move to the next hit of the focused search, or of "
                          "all of the searches when none is focused")
            .with_parameter(
                help_text("name",
                          "The name of the search to move through instead of "
                          "the focused one")
                    .with_format(
                        help_parameter_format_t::HPF_ENABLED_NAMED_SEARCHES)
                    .optional())
            .with_tags({"search", "navigation"})
            .with_opposites({"prev-search-hit"})
            .with_example({"To move to the next hit of 'req'", "req"}),
    },
    {
        "prev-search-hit",
        com_goto_search_hit,

        help_text(":prev-search-hit")
            .with_summary("Move to the previous hit of the focused search, or "
                          "of all of the searches when none is focused")
            .with_parameter(
                help_text("name",
                          "The name of the search to move through instead of "
                          "the focused one")
                    .with_format(
                        help_parameter_format_t::HPF_ENABLED_NAMED_SEARCHES)
                    .optional())
            .with_tags({"search", "navigation"})
            .with_opposites({"next-search-hit"})
            .with_example({"To move to the previous hit of 'req'", "req"}),
    },
};

void
init_lnav_search_commands(lnav::commands::command_map_t& cmd_map)
{
    for (auto& cmd : SEARCH_COMMANDS) {
        cmd.c_help.index_tags();
        cmd_map[cmd.c_name] = &cmd;
    }
}
