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

#include "command_executor.hh"
#include "lnav.hh"
#include "lnav_commands.hh"
#include "log.annotate.hh"
#include "logfile_sub_source.hh"
#include "md2attr_line.hh"
#include "readline_context.hh"

static Result<std::string, lnav::console::user_message>
com_annotate(exec_context& ec,
             std::string cmdline,
             std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
    } else if (!ec.ec_dry_run) {
        auto* tc = *lnav_data.ld_view_stack.top();
        auto* lss = dynamic_cast<logfile_sub_source*>(tc->get_sub_source());

        if (lss != nullptr) {
            auto sel = tc->get_selection();
            if (sel) {
                auto applicable_annos
                    = lnav::log::annotate::applicable(sel.value());

                if (applicable_annos.empty()) {
                    return ec.make_error(
                        "no annotations available for this log message");
                }

                auto apply_res
                    = lnav::log::annotate::apply(sel.value(), applicable_annos);
                if (apply_res.isErr()) {
                    return Err(apply_res.unwrapErr());
                }
            }
        } else {
            return ec.make_error(
                ":annotate is only supported for the LOG view");
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_comment(exec_context& ec,
            std::string cmdline,
            std::vector<std::string>& args)
{
    std::string retval;

    if (args.size() > 1) {
        args[1] = trim(remaining_args(cmdline, args));

        if (ec.ec_dry_run) {
            md2attr_line mdal;

            auto parse_res = md4cpp::parse(args[1], mdal);
            if (parse_res.isOk()) {
                auto al = parse_res.unwrap();
                lnav_data.ld_preview_status_source[0]
                    .get_description()
                    .set_value("Comment rendered as markdown:"_frag);
                lnav_data.ld_status[LNS_PREVIEW0].set_needs_update();
                lnav_data.ld_preview_view[0].set_sub_source(
                    &lnav_data.ld_preview_source[0]);
                lnav_data.ld_preview_source[0].replace_with(al);
            }

            return Ok(std::string());
        }
        auto* tc = *lnav_data.ld_view_stack.top();

        if (tc != &lnav_data.ld_views[LNV_LOG]) {
            return ec.make_error(
                "The :comment command only works in the log view");
        }
        auto& lss = lnav_data.ld_log_source;

        auto unquoted = auto_buffer::alloc(args[1].size() + 1);
        auto unquoted_len = unquote_content(
            unquoted.in(), args[1].c_str(), args[1].size(), 0);
        unquoted.resize(unquoted_len + 1);

        auto vl = ec.ec_top_line;
        tc->set_user_mark(&textview_curses::BM_META, vl, true);

        auto& line_meta = lss.get_bookmark_metadata(vl);

        line_meta.bm_comment = unquoted.in();
        lss.set_line_meta_changed();
        lss.text_filters_changed();
        tc->reload_data();

        retval = "info: comment added to line";
    } else {
        return ec.make_error("expecting some comment text");
    }

    return Ok(retval);
}

static readline_context::prompt_result_t
com_comment_prompt(exec_context& ec, const std::string& cmdline)
{
    auto* tc = *lnav_data.ld_view_stack.top();

    if (tc != &lnav_data.ld_views[LNV_LOG]) {
        return {""};
    }
    auto& lss = lnav_data.ld_log_source;

    auto line_meta_opt
        = lss.find_bookmark_metadata(tc->get_selection().value_or(0_vl));

    if (line_meta_opt && !line_meta_opt.value()->bm_comment.empty()) {
        auto trimmed_comment = trim(line_meta_opt.value()->bm_comment);

        return {trim(cmdline) + " " + trimmed_comment};
    }

    return {""};
}

static Result<std::string, lnav::console::user_message>
com_clear_comment(exec_context& ec,
                  std::string cmdline,
                  std::vector<std::string>& args)
{
    std::string retval;

    if (ec.ec_dry_run) {
        return Ok(std::string());
    }
    auto* tc = *lnav_data.ld_view_stack.top();
    if (tc != &lnav_data.ld_views[LNV_LOG]) {
        return ec.make_error(
            "The :clear-comment command only works in the log "
            "view");
    }
    auto& lss = lnav_data.ld_log_source;

    auto sel = tc->get_selection().value_or(0_vl);
    auto line_meta_opt = lss.find_bookmark_metadata(sel);
    if (line_meta_opt) {
        auto& line_meta = *(line_meta_opt.value());

        line_meta.bm_comment.clear();
        if (line_meta.empty(bookmark_metadata::categories::notes)) {
            tc->set_user_mark(&textview_curses::BM_META, sel, false);
            if (line_meta.empty(bookmark_metadata::categories::any)) {
                lss.erase_bookmark_metadata(sel);
            }
        }

        lss.set_line_meta_changed();
        lss.text_filters_changed();
        tc->reload_data();

        retval = "info: cleared comment";
    }
    tc->search_new_data();

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_tag(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    std::string retval;

    if (args.size() > 1) {
        if (ec.ec_dry_run) {
            return Ok(std::string());
        }
        textview_curses* tc = *lnav_data.ld_view_stack.top();

        if (tc != &lnav_data.ld_views[LNV_LOG]) {
            return ec.make_error("The :tag command only works in the log view");
        }
        auto sel = tc->get_selection();
        if (!sel) {
            return ec.make_error("no focused message");
        }
        auto& lss = lnav_data.ld_log_source;

        tc->set_user_mark(&textview_curses::BM_META, sel.value(), true);
        auto& line_meta = lss.get_bookmark_metadata(sel.value());
        for (size_t lpc = 1; lpc < args.size(); lpc++) {
            std::string tag = args[lpc];

            if (!startswith(tag, "#")) {
                tag = "#" + tag;
            }
            bookmark_metadata::KNOWN_TAGS.insert(tag);
            line_meta.add_tag(tag);
        }
        tc->search_new_data();
        lss.set_line_meta_changed();
        lss.text_filters_changed();
        tc->reload_data();

        retval = "info: tag(s) added to line";
    } else {
        return ec.make_error("expecting one or more tags");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_untag(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    std::string retval;

    if (args.size() > 1) {
        if (ec.ec_dry_run) {
            return Ok(std::string());
        }
        textview_curses* tc = *lnav_data.ld_view_stack.top();

        if (tc != &lnav_data.ld_views[LNV_LOG]) {
            return ec.make_error(
                "The :untag command only works in the log view");
        }
        auto sel = tc->get_selection();
        if (!sel) {
            return ec.make_error("no focused message");
        }
        auto& lss = lnav_data.ld_log_source;

        auto line_meta_opt = lss.find_bookmark_metadata(sel.value());
        if (line_meta_opt) {
            auto& line_meta = *(line_meta_opt.value());

            for (size_t lpc = 1; lpc < args.size(); lpc++) {
                std::string tag = args[lpc];

                if (!startswith(tag, "#")) {
                    tag = "#" + tag;
                }
                line_meta.remove_tag(tag);
            }
            if (line_meta.empty(bookmark_metadata::categories::notes)) {
                tc->set_user_mark(
                    &textview_curses::BM_META, sel.value(), false);
            }
        }
        tc->search_new_data();
        lss.set_line_meta_changed();
        lss.text_filters_changed();
        tc->reload_data();

        retval = "info: tag(s) removed from line";
    } else {
        return ec.make_error("expecting one or more tags");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_delete_tags(exec_context& ec,
                std::string cmdline,
                std::vector<std::string>& args)
{
    std::string retval;

    if (args.size() > 1) {
        if (ec.ec_dry_run) {
            return Ok(std::string());
        }
        textview_curses* tc = *lnav_data.ld_view_stack.top();

        if (tc != &lnav_data.ld_views[LNV_LOG]) {
            return ec.make_error(
                "The :delete-tag command only works in the log "
                "view");
        }

        auto& known_tags = bookmark_metadata::KNOWN_TAGS;
        std::vector<std::string> tags;

        for (size_t lpc = 1; lpc < args.size(); lpc++) {
            std::string tag = args[lpc];

            if (!startswith(tag, "#")) {
                tag = "#" + tag;
            }
            if (known_tags.find(tag) == known_tags.end()) {
                return ec.make_error("Unknown tag -- {}", tag);
            }

            tags.emplace_back(tag);
            known_tags.erase(tag);
        }

        auto& lss = lnav_data.ld_log_source;
        auto& vbm = tc->get_bookmarks()[&textview_curses::BM_META];

        for (auto iter = vbm.bv_tree.begin(); iter != vbm.bv_tree.end();) {
            auto line_meta_opt = lss.find_bookmark_metadata(*iter);

            if (!line_meta_opt) {
                ++iter;
                continue;
            }

            auto& line_meta = line_meta_opt.value();
            for (const auto& tag : tags) {
                line_meta->remove_tag(tag);
            }

            if (line_meta->empty(bookmark_metadata::categories::notes)) {
                size_t off = std::distance(vbm.bv_tree.begin(), iter);
                auto vl = *iter;
                tc->set_user_mark(&textview_curses::BM_META, vl, false);
                if (line_meta->empty(bookmark_metadata::categories::any)) {
                    lss.erase_bookmark_metadata(vl);
                }

                iter = std::next(vbm.bv_tree.begin(), off);
            } else {
                ++iter;
            }
        }

        retval = "info: deleted tag(s)";
    } else {
        return ec.make_error("expecting one or more tags");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_partition_name(exec_context& ec,
                   std::string cmdline,
                   std::vector<std::string>& args)
{
    std::string retval;

    if (args.size() > 1) {
        if (ec.ec_dry_run) {
            retval = "";
        } else {
            auto& tc = lnav_data.ld_views[LNV_LOG];
            auto& lss = lnav_data.ld_log_source;
            auto sel = tc.get_selection();
            if (!sel) {
                return ec.make_error("no focused message");
            }

            args[1] = trim(remaining_args(cmdline, args));

            tc.set_user_mark(&textview_curses::BM_PARTITION, sel.value(), true);

            auto& line_meta = lss.get_bookmark_metadata(sel.value());

            line_meta.bm_name = args[1];
            retval = "info: name set for partition";
        }
    } else {
        return ec.make_error("expecting partition name");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_clear_partition(exec_context& ec,
                    std::string cmdline,
                    std::vector<std::string>& args)
{
    std::string retval;

    if (args.size() == 1) {
        auto& tc = lnav_data.ld_views[LNV_LOG];
        auto& lss = lnav_data.ld_log_source;
        auto& bv = tc.get_bookmarks()[&textview_curses::BM_PARTITION];
        std::optional<vis_line_t> part_start;
        auto sel = tc.get_selection();
        if (!sel) {
            return ec.make_error("no focused message");
        }

        if (bv.bv_tree.exists(sel.value())) {
            part_start = sel.value();
        } else {
            part_start = bv.prev(sel.value());
        }
        if (!part_start) {
            return ec.make_error("focused line is not in a partition");
        }

        if (!ec.ec_dry_run) {
            auto& line_meta = lss.get_bookmark_metadata(part_start.value());

            line_meta.bm_name.clear();
            if (line_meta.empty(bookmark_metadata::categories::partition)) {
                tc.set_user_mark(
                    &textview_curses::BM_PARTITION, part_start.value(), false);
                if (line_meta.empty(bookmark_metadata::categories::any)) {
                    lss.erase_bookmark_metadata(part_start.value());
                }
            }

            retval = "info: cleared partition name";
        }
    }

    return Ok(retval);
}

static readline_context::command_t METADATA_COMMANDS[] = {

    {
        "annotate",
        com_annotate,

        help_text(":annotate")
            .with_summary("Analyze the focused log message and "
                          "attach annotations")
            .with_tags({"metadata"}),
    },

    {
        "comment",
        com_comment,

        help_text(":comment")
            .with_summary("Attach a comment to the focused log line.  The "
                          "comment will be "
                          "displayed right below the log message it is "
                          "associated with. "
                          "The comment can contain Markdown directives for "
                          "styling and linking.")
            .with_parameter(
                help_text("text", "The comment text")
                    .with_format(help_parameter_format_t::HPF_MULTILINE_TEXT))
            .with_example({"To add the comment 'This is where it all went "
                           "wrong' to the focused line",
                           "This is where it all went wrong"})
            .with_tags({"metadata"}),

        com_comment_prompt,
    },
    {"clear-comment",
     com_clear_comment,

     help_text(":clear-comment")
         .with_summary("Clear the comment attached to the focused log line")
         .with_opposites({"comment"})
         .with_tags({"metadata"})},
    {
        "tag",
        com_tag,

        help_text(":tag")
            .with_summary("Attach tags to the focused log line")
            .with_parameter(help_text("tag", "The tags to attach")
                                .one_or_more()
                                .with_format(help_parameter_format_t::HPF_TAG))
            .with_example({"To add the tags '#BUG123' and '#needs-review' to "
                           "the focused line",
                           "#BUG123 #needs-review"})
            .with_tags({"metadata"}),
    },
    {
        "untag",
        com_untag,

        help_text(":untag")
            .with_summary("Detach tags from the focused log line")
            .with_parameter(
                help_text("tag", "The tags to detach")
                    .one_or_more()
                    .with_format(help_parameter_format_t::HPF_LINE_TAG))
            .with_example({"To remove the tags '#BUG123' and "
                           "'#needs-review' from the focused line",
                           "#BUG123 #needs-review"})
            .with_opposites({"tag"})
            .with_tags({"metadata"}),
    },
    {
        "delete-tags",
        com_delete_tags,

        help_text(":delete-tags")
            .with_summary("Remove the given tags from all log lines")
            .with_parameter(help_text("tag", "The tags to delete")
                                .one_or_more()
                                .with_format(help_parameter_format_t::HPF_TAG))
            .with_example({"To remove the tags '#BUG123' and "
                           "'#needs-review' from "
                           "all log lines",
                           "#BUG123 #needs-review"})
            .with_opposites({"tag"})
            .with_tags({"metadata"}),
    },
    {
        "partition-name",
        com_partition_name,

        help_text(":partition-name")
            .with_summary(
                "Mark the focused line in the log view as the start of a "
                "new partition with the given name")
            .with_parameter(help_text("name", "The name for the new partition")
                                .with_format(help_parameter_format_t::HPF_TEXT))
            .with_example(
                {"To mark the focused line as the start of the partition "
                 "named 'boot #1'",
                 "boot #1"})
            .with_tags({"metadata"}),
    },
    {
        "clear-partition",
        com_clear_partition,

        help_text(":clear-partition")
            .with_summary("Clear the partition the focused line is a part of")
            .with_opposites({"partition-name"})
            .with_tags({"metadata"}),
    },
};

void
init_lnav_metadata_commands(readline_context::command_map_t& cmd_map)
{
    for (auto& cmd : METADATA_COMMANDS) {
        cmd.c_help.index_tags();
        cmd_map[cmd.c_name] = &cmd;
    }
}
