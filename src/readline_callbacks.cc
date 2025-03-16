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
 */

#include <chrono>

#include "readline_callbacks.hh"

#include "base/fs_util.hh"
#include "base/humanize.network.hh"
#include "base/injector.hh"
#include "base/itertools.hh"
#include "base/paths.hh"
#include "bound_tags.hh"
#include "cmd.parser.hh"
#include "command_executor.hh"
#include "config.h"
#include "field_overlay_source.hh"
#include "help_text_formatter.hh"
#include "itertools.similar.hh"
#include "lnav.hh"
#include "lnav.prompt.hh"
#include "lnav_config.hh"
#include "log_format_loader.hh"
#include "plain_text_source.hh"
#include "readline_highlighters.hh"
#include "scn/scan.h"
#include "service_tags.hh"
#include "sql_help.hh"
#include "tailer/tailer.looper.hh"
#include "view_helpers.examples.hh"
#include "vtab_module.hh"
#include "yajlpp/yajlpp.hh"

using namespace std::chrono_literals;
using namespace lnav::roles::literals;

#define PERFORM_MSG \
    "(Press " ANSI_BOLD("CTRL+X") " to perform operation and " ANSI_BOLD( \
        "CTRL+]") " to abort)"
#define ABORT_MSG "(Press " ANSI_BOLD("CTRL+]") " to abort)"

#define ANSI_RE(msg) \
    ANSI_CSI ANSI_BOLD_PARAM ";" ANSI_COLOR_PARAM(COLOR_CYAN) "m" msg ANSI_NORM
#define ANSI_CLS(msg) \
    ANSI_CSI ANSI_BOLD_PARAM \
        ";" ANSI_COLOR_PARAM(COLOR_MAGENTA) "m" msg ANSI_NORM
#define ANSI_KW(msg) \
    ANSI_CSI ANSI_BOLD_PARAM ";" ANSI_COLOR_PARAM(COLOR_BLUE) "m" msg ANSI_NORM
#define ANSI_REV(msg) ANSI_CSI "7m" msg ANSI_NORM
#define ANSI_STR(msg) ANSI_CSI "32m" msg ANSI_NORM

const char * const RE_HELP =
    " "  ANSI_RE(".") "   Any character    "
    " "     "a" ANSI_RE("|") "b   a or b        "
    " " ANSI_RE("(?-i)") "   Case-sensitive search\n"

    " " ANSI_CLS("\\w") "  Word character   "
    " "     "a" ANSI_RE("?") "    0 or 1 a's    "
    " "                 ANSI_RE("$") "       End of string\n"

    " " ANSI_CLS("\\d") "  Digit            "
    " "     "a" ANSI_RE("*") "    0 or more a's "
    " " ANSI_RE("(") "..." ANSI_RE(")") "   Capture\n"

    " " ANSI_CLS("\\s") "  White space      "
    " "     "a" ANSI_RE("+") "    1 or more a's "
    " "                 ANSI_RE("^") "       Start of string\n"

    " " ANSI_RE("\\") "   Escape character "
    " " ANSI_RE("[^") "ab" ANSI_RE("]") " " ANSI_BOLD("Not") " a or b    "
    " " ANSI_RE("[") "ab" ANSI_RE("-") "d" ANSI_RE("]") "  Any of a, b, c, or d"
;

const char * const RE_EXAMPLE =
    ANSI_UNDERLINE("Examples") "\n"
    "  abc" ANSI_RE("*") "       matches  "
    ANSI_STR("'ab'") ", " ANSI_STR("'abc'") ", " ANSI_STR("'abccc'") "\n"

    "  key=" ANSI_RE("(\\w+)")
    "  matches  key=" ANSI_REV("123") ", key=" ANSI_REV("abc") " and captures 123 and abc\n"

    "  " ANSI_RE("\\") "[abc" ANSI_RE("\\") "]    matches  " ANSI_STR("'[abc]'") "\n"

    "  " ANSI_RE("(?-i)") "ABC   matches  " ANSI_STR("'ABC'") " and " ANSI_UNDERLINE("not") " " ANSI_STR("'abc'")
;

const char* const CMD_HELP =
    " " ANSI_KW(":goto") "              Go to a line #, timestamp, etc...\n"
    " " ANSI_KW(":filter-out") "        Filter out lines that match a pattern\n"
    " " ANSI_KW(":hide-lines-before") " Hide lines before a timestamp\n"
    " " ANSI_KW(":open") "              Open another file/directory\n";

const char* const CMD_EXAMPLE =
    ANSI_UNDERLINE("Examples") "\n"
    "  " ANSI_KW(":goto") " 123\n"
    "  " ANSI_KW(":filter-out") " spam\n"
    "  " ANSI_KW(":hide-lines-before") " here\n";

const char * const SQL_HELP =
    " " ANSI_KW("SELECT") "  Select rows from a table      "
    " " ANSI_KW("DELETE") "  Delete rows from a table\n"
    " " ANSI_KW("INSERT") "  Insert rows into a table      "
    " " ANSI_KW("UPDATE") "  Update rows in a table\n"
    " " ANSI_KW("CREATE") "  Create a table/index          "
    " " ANSI_KW("DROP") "    Drop a table/index\n"
    " " ANSI_KW("ATTACH") "  Attach a SQLite database file "
    " " ANSI_KW("DETACH") "  Detach a SQLite database"
;

const char * const SQL_EXAMPLE =
    ANSI_UNDERLINE("Examples") "\n"
    "  SELECT * FROM %s WHERE log_level >= 'warning' LIMIT 10\n"
    "  UPDATE %s SET log_mark = 1 WHERE log_line = log_top_line()\n"
    "  SELECT * FROM logline LIMIT 10"
;

const char * const PRQL_HELP =
    " " ANSI_KW("from") "    Specify a data source       "
    " " ANSI_KW("derive") "     Derive one or more columns\n"
    " " ANSI_KW("select") "  Select one or more columns  "
    " " ANSI_KW("aggregate") "  Summary many rows into one\n"
    " " ANSI_KW("group") "   Partition rows into groups  "
    " " ANSI_KW("filter") "     Pick rows based on their values\n"
    ;

const char * const PRQL_EXAMPLE =
    ANSI_UNDERLINE("Examples") "\n"
        "  from %s | stats.count_by { log_level }\n"
        "  from %s | filter log_line == lnav.view.top_line\n"
    ;

static const auto LNAV_MULTILINE_CMD_PROMPT
    = "Enter an lnav command: " PERFORM_MSG;
static const auto LNAV_CMD_PROMPT = "Enter an lnav command: " ABORT_MSG;

static attr_line_t
format_sql_example(const char* sql_example_fmt)
{
    auto& log_view = lnav_data.ld_views[LNV_LOG];
    auto* lss = (logfile_sub_source*) log_view.get_sub_source();
    attr_line_t retval;

    if (log_view.get_inner_height() > 0) {
        auto cl = lss->at(log_view.get_top());
        auto lf = lss->find(cl);
        const auto* format_name = lf->get_format()->get_name().get();

        retval.with_ansi_string(sql_example_fmt, format_name, format_name);
        readline_sqlite_highlighter(retval, std::nullopt);
    }
    return retval;
}

void
rl_set_help()
{
    switch (lnav_data.ld_mode) {
        case ln_mode_t::SEARCH: {
            lnav_data.ld_doc_source.replace_with(RE_HELP);
            lnav_data.ld_example_source.replace_with(RE_EXAMPLE);
            break;
        }
        case ln_mode_t::SQL: {
            auto example_al = format_sql_example(SQL_EXAMPLE);
            lnav_data.ld_doc_source.replace_with(SQL_HELP);
            lnav_data.ld_example_source.replace_with(example_al);
            break;
        }
        case ln_mode_t::COMMAND: {
            lnav_data.ld_doc_source.replace_with(CMD_HELP);
            lnav_data.ld_example_source.replace_with(CMD_EXAMPLE);
            break;
        }
        default:
            break;
    }
}

static bool
rl_sql_help(textinput_curses& rc)
{
    auto al = attr_line_t(rc.get_content());
    const auto& sa = al.get_attrs();
    auto x = rc.get_cursor_offset();
    bool has_doc = false;

    if (x > 0) {
        x -= 1;
    }

    annotate_sql_statement(al);

    auto avail_help = find_sql_help_for_line(al, x);
    auto lang = help_example::language::undefined;
    if (lnav::sql::is_prql(al.get_string())) {
        lang = help_example::language::prql;
    }

    if (!avail_help.empty()) {
        size_t help_count = avail_help.size();
        auto& dtc = lnav_data.ld_doc_view;
        auto& etc = lnav_data.ld_example_view;
        unsigned long doc_width, ex_width;
        vis_line_t doc_height, ex_height;
        attr_line_t doc_al, ex_al;

        dtc.get_dimensions(doc_height, doc_width);
        etc.get_dimensions(ex_height, ex_width);

        for (const auto& ht : avail_help) {
            format_help_text_for_term(*ht,
                                      std::min(70UL, doc_width),
                                      doc_al,
                                      help_count > 1
                                          ? help_text_content::synopsis
                                          : help_text_content::full);
            if (help_count == 1) {
                format_example_text_for_term(
                    *ht, eval_example, std::min(70UL, ex_width), ex_al, lang);
            } else {
                doc_al.append("\n");
            }
        }

        if (!doc_al.empty()) {
            lnav_data.ld_doc_source.replace_with(doc_al);
            dtc.reload_data();

            lnav_data.ld_example_source.replace_with(ex_al);
            etc.reload_data();

            has_doc = true;
        }
    }

    auto ident_iter = find_string_attr_containing(
        sa, &SQL_IDENTIFIER_ATTR, al.nearest_text(x));
    if (ident_iter == sa.end()) {
        ident_iter = find_string_attr_containing(
            sa, &lnav::sql::PRQL_FQID_ATTR, al.nearest_text(x));
    }
    if (ident_iter != sa.end()) {
        auto ident = al.get_substring(ident_iter->sa_range);
        const intern_string_t intern_ident = intern_string::lookup(ident);
        auto vtab = lnav_data.ld_vtab_manager->lookup_impl(intern_ident);
        auto vtab_module_iter = vtab_module_ddls.find(intern_ident);
        std::string ddl;

        if (vtab != nullptr) {
            ddl = trim(vtab->get_table_statement());
        } else if (vtab_module_iter != vtab_module_ddls.end()) {
            ddl = vtab_module_iter->second;
        } else {
            auto table_ddl_iter = lnav_data.ld_table_ddl.find(ident);

            if (table_ddl_iter != lnav_data.ld_table_ddl.end()) {
                ddl = table_ddl_iter->second;
            }
        }

        if (!ddl.empty()) {
            lnav_data.ld_preview_view[0].set_sub_source(
                &lnav_data.ld_preview_source[0]);
            lnav_data.ld_preview_view[0].set_overlay_source(nullptr);
            lnav_data.ld_preview_source[0].replace_with(ddl).set_text_format(
                text_format_t::TF_SQL);
            lnav_data.ld_preview_status_source[0].get_description().set_value(
                "Definition for table -- %s", ident.c_str());
        }
    }

    return has_doc;
}

static void
rl_cmd_change(textinput_curses& rc, bool is_req)
{
    static const std::set<std::string> COMMANDS_WITH_SQL = {
        "filter-expr",
        "mark-expr",
    };

    static const std::set<std::string> COMMANDS_FOR_FIELDS = {
        "hide-fields",
        "show-fields",
    };

    static auto& prompt = lnav::prompt::get();
    auto* tc = get_textview_for_mode(lnav_data.ld_mode);

    clear_preview();

    static std::string last_command;
    static int generation = 0;

    const auto line = rc.get_content();
    std::vector<std::string> args;
    auto iter = lnav_commands.end();

    split_ws(line, args);

    if (args.empty()) {
        generation = 0;
    } else if (args[0] != last_command) {
        last_command = args[0];
        generation = 0;
    } else if (args.size() > 1) {
        generation += 1;
    }

    auto* os = tc->get_overlay_source();
    if (!args.empty() && os != nullptr) {
        auto* fos = dynamic_cast<field_overlay_source*>(os);

        if (fos != nullptr) {
            if (generation == 0) {
                auto& top_ctx = fos->fos_contexts.top();

                if (COMMANDS_WITH_SQL.count(args[0]) > 0) {
                    top_ctx.c_prefix = ":";
                    top_ctx.c_show = true;
                    top_ctx.c_show_discovered = false;
                } else if (COMMANDS_FOR_FIELDS.count(args[0]) > 0) {
                    top_ctx.c_prefix = "";
                    top_ctx.c_show = true;
                    top_ctx.c_show_discovered = false;
                } else {
                    top_ctx.c_prefix = "";
                    top_ctx.c_show = false;
                }
                tc->set_sync_selection_and_top(top_ctx.c_show);
            }
        }
    }

    if (!args.empty()) {
        iter = lnav_commands.find(args[0]);
    }
    if (iter == lnav_commands.end()
        || (args.size() == 1 && !endswith(line, " ") && !endswith(line, "\n")))
    {
        auto poss_str = lnav_commands | lnav::itertools::first()
            | lnav::itertools::similar_to(args.empty() ? "" : args[0], 10);
        auto poss_width = poss_str | lnav::itertools::map(&std::string::size)
            | lnav::itertools::max();

        auto poss = poss_str
            | lnav::itertools::map([&args, &poss_width](const auto& x) {
                        return attr_line_t()
                            .append(x, VC_ROLE.value(role_t::VCR_KEYWORD))
                            .highlight_fuzzy_matches(cget(args, 0).value_or(""))
                            .append(" ")
                            .pad_to(poss_width.value_or(0) + 1)
                            .append(lnav_commands[x]->c_help.ht_summary)
                            .with_attr_for_all(
                                lnav::prompt::SUBST_TEXT.value(x + " "));
                    });

        rc.open_popup_for_completion(0, poss);
        rc.tc_popup.set_title("Command");
        prompt.p_editor.tc_height = std::min(
            prompt.p_editor.tc_height, (int) prompt.p_editor.tc_lines.size());
        lnav_data.ld_doc_source.replace_with(CMD_HELP);
        lnav_data.ld_example_source.replace_with(CMD_EXAMPLE);
        lnav_data.ld_bottom_source.set_prompt(LNAV_CMD_PROMPT);
        lnav_data.ld_bottom_source.grep_error("");
    } else if (args[0] == "config" && args.size() > 1) {
        static const auto INPUT_SRC = intern_string::lookup("input");
        yajlpp_parse_context ypc(INPUT_SRC, &lnav_config_handlers);

        ypc.set_path(args[1]).with_obj(lnav_config);
        ypc.update_callbacks();

        if (ypc.ypc_current_handler != nullptr) {
            const json_path_handler_base* jph = ypc.ypc_current_handler;
            auto help_text = fmt::format(
                FMT_STRING(ANSI_BOLD("{} {}") " -- {}    " ABORT_MSG),
                jph->jph_property.c_str(),
                jph->jph_synopsis,
                jph->jph_description);
            lnav_data.ld_bottom_source.set_prompt(help_text);
            lnav_data.ld_bottom_source.grep_error("");
        } else {
            lnav_data.ld_bottom_source.grep_error(
                "Unknown configuration option: " + args[1]);
        }
    } else if ((args[0] != "filter-expr" && args[0] != "mark-expr")
               || !rl_sql_help(rc))
    {
        const auto& cmd = *iter->second;
        const auto& ht = cmd.c_help;

        if (ht.ht_name) {
            auto& dtc = lnav_data.ld_doc_view;
            auto& etc = lnav_data.ld_example_view;
            unsigned long width;
            vis_line_t height;
            attr_line_t al;

            dtc.get_dimensions(height, width);
            format_help_text_for_term(ht, std::min(70UL, width), al);
            lnav_data.ld_doc_source.replace_with(al);
            dtc.set_needs_update();

            al.clear();
            etc.get_dimensions(height, width);
            format_example_text_for_term(ht, eval_example, width, al);
            lnav_data.ld_example_source.replace_with(al);
            etc.set_needs_update();
        }

        if (cmd.c_prompt != nullptr) {
            const auto prompt_res
                = cmd.c_prompt(lnav_data.ld_exec_context, line);

            if (generation == 0 && trim(line) == args[0]
                && !prompt_res.pr_new_prompt.empty())
            {
                log_debug("replacing prompt with one suggested by command");
                prompt.p_editor.tc_selection
                    = textinput_curses::selected_range::from_key(
                        textinput_curses::input_point::home(),
                        prompt.p_editor.tc_cursor);
                prompt.p_editor.replace_selection(prompt_res.pr_new_prompt);
                prompt.p_editor.move_cursor_to({(int) args[0].length() + 1, 0});
                generation += 1;
                iter = lnav_commands.end();
            }
            rc.tc_suggestion = prompt_res.pr_suggestion;
        }

        if (!ht.ht_parameters.empty()
            && ht.ht_parameters.front().ht_format
                == help_parameter_format_t::HPF_MULTILINE_TEXT)
        {
            prompt.p_editor.tc_height = 5;
        } else if (prompt.p_editor.tc_height > 1) {
            auto ml_content = prompt.p_editor.get_content();
            std::replace(ml_content.begin(), ml_content.end(), '\n', ' ');
            prompt.p_editor.set_content(ml_content);
            prompt.p_editor.tc_height = 1;
        }

        lnav_data.ld_bottom_source.set_prompt(prompt.p_editor.tc_height > 1
                                                  ? LNAV_MULTILINE_CMD_PROMPT
                                                  : LNAV_CMD_PROMPT);
        lnav_data.ld_bottom_source.grep_error("");
        lnav_data.ld_status[LNS_BOTTOM].window_change();
    }

    if (iter != lnav_commands.end() && (args.size() > 1 || endswith(line, " ")))
    {
        auto line_sf = string_fragment(line);
        auto args_sf = line_sf.split_when(string_fragment::tag1{' '}).second;
        auto parsed_cmd = lnav::command::parse_for_prompt(
            lnav_data.ld_exec_context, args_sf, iter->second->c_help);
        auto x
            = args_sf.column_to_byte_index(rc.tc_cursor.x - args_sf.sf_begin);
        auto arg_res_opt = parsed_cmd.arg_at(x);

        if (arg_res_opt) {
            auto arg_res = arg_res_opt.value();
            log_debug(
                "apair %s [%d:%d) -- %s",
                arg_res.aar_help->ht_name,
                arg_res.aar_element.se_origin.sf_begin,
                arg_res.aar_element.se_origin.sf_end,
                arg_res.aar_element.se_value.c_str());
            auto left = arg_res.aar_element.se_origin.empty()
                ? rc.tc_cursor.x
                : line_sf.byte_to_column_index(
                      args_sf.sf_begin
                      + arg_res.aar_element.se_origin.sf_begin);
            if (arg_res.aar_help->ht_format
                == help_parameter_format_t::HPF_CONFIG_VALUE)
            {
                log_debug(
                    "arg path %s",
                    parsed_cmd.p_args["option"].a_values[0].se_value.c_str());
                auto poss = prompt.get_config_value_completion(
                    parsed_cmd.p_args["option"].a_values[0].se_value,
                    arg_res.aar_element.se_origin.to_string());
                rc.open_popup_for_completion(left, poss);
                rc.tc_popup.set_title(arg_res.aar_help->ht_name);
            } else if (is_req || arg_res.aar_required
                       || rc.tc_popup_type
                           != textinput_curses::popup_type_t::none)
            {
                auto poss = prompt.get_cmd_parameter_completion(
                    *tc,
                    &iter->second->c_help,
                    arg_res.aar_help,
                    arg_res.aar_element.se_value.empty()
                        ? arg_res.aar_element.se_origin.to_string()
                        : arg_res.aar_element.se_value);
                rc.open_popup_for_completion(left, poss);
                rc.tc_popup.set_title(arg_res.aar_help->ht_name);
            } else if (arg_res.aar_help->ht_format
                           == help_parameter_format_t::HPF_REGEX
                       && arg_res.aar_element.se_value.empty()
                       && rc.is_cursor_at_end_of_line())
            {
                auto re_arg = parsed_cmd.p_args[arg_res.aar_help->ht_name];
                if (!re_arg.a_values.empty()) {
                    rc.tc_suggestion = prompt.get_regex_suggestion(
                        *tc, re_arg.a_values[0].se_value);
                } else {
                    rc.tc_suggestion.clear();
                }
            } else {
                rc.tc_suggestion.clear();
            }
        } else {
            log_info("no arg at %d", x);
        }
    }
}

static void
rl_sql_change(textinput_curses& rc, bool is_req)
{
    static const auto* sql_cmd_map
        = injector::get<readline_context::command_map_t*, sql_cmd_map_tag>();
    static auto& prompt = lnav::prompt::get();

    const auto line = rc.get_content();
    std::vector<std::string> args;
    auto is_prql = lnav::sql::is_prql(line);

    log_debug("rl_sql_change");

    if (is_prql) {
        auto anno_line = attr_line_t(line);
        lnav::sql::annotate_prql_statement(anno_line);
        auto cursor_offset = prompt.p_editor.get_cursor_offset();

        log_debug("curs %d", cursor_offset);
        for (const auto& attr : anno_line.al_attrs) {
            log_debug("attr [%d:%d) %s",
                      attr.sa_range.lr_start,
                      attr.sa_range.lr_end,
                      attr.sa_type->sat_name);
        }

        auto attr_iter = rfind_string_attr_if(
            anno_line.al_attrs, cursor_offset, [](const auto& x) {
                return x.sa_type != &lnav::sql::PRQL_STAGE_ATTR;
            });
        auto stage_iter = rfind_string_attr_if(
            anno_line.al_attrs, cursor_offset, [](const auto& x) {
                return x.sa_type == &lnav::sql::PRQL_STAGE_ATTR;
            });
        if (attr_iter != anno_line.al_attrs.end()) {
            auto to_complete_sf = anno_line.to_string_fragment(attr_iter);
            auto to_complete = to_complete_sf.to_string();
            std::vector<attr_line_t> poss;
            std::string title;

            log_debug("prql attr [%d:%d) %s",
                      attr_iter->sa_range.lr_start,
                      attr_iter->sa_range.lr_end,
                      attr_iter->sa_type->sat_name);
            auto prev_attr_iter = std::prev(attr_iter);
            if (attr_iter->sa_type == &lnav::sql::PRQL_PIPE_ATTR
                || (attr_iter->sa_type == &lnav::sql::PRQL_FQID_ATTR
                    && (prev_attr_iter->sa_type == &lnav::sql::PRQL_PIPE_ATTR
                        || prev_attr_iter->sa_type
                            == &lnav::sql::PRQL_STAGE_ATTR)))
            {
                if (attr_iter->sa_type == &lnav::sql::PRQL_PIPE_ATTR) {
                    to_complete.clear();
                }
                auto poss_str
                    = (*sql_cmd_map)
                    | lnav::itertools::filter_in(
                          [](const readline_context::command_map_t::value_type&
                                 p) {
                              return !p.second->c_dependencies.empty();
                          })
                    | lnav::itertools::first()
                    | lnav::itertools::similar_to(to_complete, 10);
                auto width = poss_str | lnav::itertools::map(&std::string::size)
                    | lnav::itertools::max();

                title = "transform";
                poss = poss_str
                    | lnav::itertools::map([&width,
                                            &to_complete](const auto& x) {
                           const auto& ht = sql_cmd_map->at(x)->c_help;
                           auto sub_value = x + " ";
                           if (!ht.ht_parameters.empty()
                               && ht.ht_parameters[0].ht_group_start)
                           {
                               sub_value.append(
                                   ht.ht_parameters[0].ht_group_start);
                               sub_value.push_back(' ');
                           }
                           return attr_line_t()
                               .append(x, VC_ROLE.value(role_t::VCR_FUNCTION))
                               .highlight_fuzzy_matches(to_complete)
                               .append(" ")
                               .pad_to(width.value_or(0) + 1)
                               .append(ht.ht_summary)
                               .with_attr_for_all(
                                   lnav::prompt::SUBST_TEXT.value(sub_value));
                       });
            } else if (attr_iter->sa_type == &lnav::sql::PRQL_FQID_ATTR
                       && attr_iter->sa_range.lr_end == cursor_offset)
            {
                poss = prompt.p_prql_completions | lnav::itertools::first()
                    | lnav::itertools::similar_to(to_complete, 10)
                    | lnav::itertools::map([&to_complete](const auto& x) {
                           return prompt.get_sql_completion_text(
                               to_complete, *prompt.p_prql_completions.find(x));
                       });
            }
            auto left = rc.tc_cursor.x - to_complete_sf.column_width();
            rc.open_popup_for_completion(left, poss);
            rc.tc_popup.set_title(title);
        }
    } else {
        clear_preview();

        auto anno_line = attr_line_t(line);
        annotate_sql_statement(anno_line);
        auto cursor_offset = prompt.p_editor.get_cursor_offset();

        auto attr_iter = rfind_string_attr_if(
            anno_line.al_attrs,
            cursor_offset,
            [cursor_offset](const string_attr& attr) {
                return attr.sa_range.lr_end == cursor_offset;
            });
        if (attr_iter != anno_line.al_attrs.end()) {
            auto to_complete_sf = anno_line.to_string_fragment(attr_iter);
            auto to_complete = to_complete_sf.to_string();
            std::vector<std::string> poss_strs;
            std::vector<attr_line_t> poss;

            if (attr_iter->sa_range.lr_start == 0) {
                poss_strs = *sql_cmd_map
                    | lnav::itertools::filter_in([](const auto& pair) {
                          return pair.second->c_dependencies.empty();
                      })
                    | lnav::itertools::first()
                    | lnav::itertools::similar_to(to_complete, 10);
                auto width = poss_strs
                    | lnav::itertools::map(&std::string::size)
                    | lnav::itertools::max();
                for (const auto& str : poss_strs) {
                    poss.emplace_back(prompt.get_db_completion_text(
                        to_complete, str, width.value_or(0)));
                }
            } else {
                poss_strs = prompt.p_sql_completions | lnav::itertools::first()
                    | lnav::itertools::similar_to(to_complete, 10);
                for (const auto& str : poss_strs) {
                    auto eq_range = prompt.p_sql_completions.equal_range(str);

                    for (auto iter = eq_range.first; iter != eq_range.second;
                         ++iter)
                    {
                        auto al = prompt.get_sql_completion_text(to_complete,
                                                                 *iter);
                        poss.emplace_back(al);
                    }
                }
            }

            auto left = rc.tc_cursor.x - to_complete_sf.column_width();
            rc.open_popup_for_completion(left, poss);
        }
    }

    split_ws(line, args);
    if (!args.empty()) {
        auto cmd_iter = sql_cmd_map->find(args[0]);
        if (cmd_iter != sql_cmd_map->end()) {
            const auto* sql_cmd = cmd_iter->second;
            if (sql_cmd->c_prompt != nullptr) {
                const auto prompt_res
                    = sql_cmd->c_prompt(lnav_data.ld_exec_context, line);

                rc.tc_suggestion = prompt_res.pr_suggestion;
            }
        }
    }
}

static void
rl_search_change(textinput_curses& rc, bool is_req)
{
    static const auto SEARCH_HELP
        = help_text("search", "search the view for a pattern")
              .with_parameter(
                  help_text("pattern", "The pattern to search for")
                      .with_format(help_parameter_format_t::HPF_REGEX));
    static auto& prompt = lnav::prompt::get();

    auto* tc = get_textview_for_mode(lnav_data.ld_mode);
    auto line = rc.get_content();
    auto line_sf = string_fragment::from_str(line);
    if (line.empty() && tc->tc_selected_text) {
        rc.tc_suggestion = tc->tc_selected_text->sti_value;
    } else {
        rc.tc_suggestion.clear();
    }
    if (rc.tc_suggestion.empty()) {
        auto parse_res = lnav::command::parse_for_prompt(
            lnav_data.ld_exec_context, line, SEARCH_HELP);

        auto byte_x = line_sf.column_to_byte_index(rc.tc_cursor.x);
        auto arg_res_opt = parse_res.arg_at(byte_x);
        if (arg_res_opt) {
            auto arg_pair = arg_res_opt.value();
            if (is_req
                || rc.tc_popup_type != textinput_curses::popup_type_t::none)
            {
                auto poss = prompt.get_cmd_parameter_completion(
                    *tc,
                    &SEARCH_HELP,
                    arg_pair.aar_help,
                    arg_pair.aar_element.se_value);
                auto left = arg_pair.aar_element.se_value.empty()
                    ? rc.tc_cursor.x
                    : line_sf.byte_to_column_index(
                          arg_pair.aar_element.se_origin.sf_begin);
                rc.open_popup_for_completion(left, poss);
                rc.tc_popup.set_title(arg_pair.aar_help->ht_name);
            } else if (!line.empty() && arg_pair.aar_element.se_value.empty()
                       && rc.is_cursor_at_end_of_line())
            {
                rc.tc_suggestion = prompt.get_regex_suggestion(*tc, line);
            } else {
                log_debug("not at end of line %d %d",
                          arg_pair.aar_element.se_value.empty(),
                          rc.is_cursor_at_end_of_line());
                rc.tc_suggestion.clear();
            }
        } else {
            log_debug("no arg");
        }
    }
}

static void
rl_exec_change(textinput_curses& rc, bool is_req)
{
    static auto& prompt = lnav::prompt::get();
    auto* tc = get_textview_for_mode(lnav_data.ld_mode);

    clear_preview();

    const auto line = rc.get_content();
    shlex lexer(line);
    auto split_res = lexer.split(lnav_data.ld_exec_context.create_resolver());
    if (split_res.isErr()) {
        lnav_data.ld_bottom_source.grep_error(
            split_res.unwrapErr().se_error.te_msg);
    } else {
        auto split_args = split_res.unwrap();
        auto script_name = split_args.empty() ? std::string()
                                              : split_args[0].se_value;
        const auto& scripts = prompt.p_scripts;
        const auto iter = scripts.as_scripts.find(script_name);

        if (iter == scripts.as_scripts.end()
            || iter->second[0].sm_description.empty())
        {
            lnav_data.ld_bottom_source.set_prompt(
                "Enter a script to execute: " ABORT_MSG);

            std::vector<attr_line_t> poss;
            auto width = scripts.as_scripts | lnav::itertools::first()
                | lnav::itertools::map(&std::string::size)
                | lnav::itertools::max();
            if (script_name.empty()) {
                poss
                    = scripts.as_scripts
                    | lnav::itertools::map([&width](const auto& p) {
                          return attr_line_t()
                              .append(p.first,
                                      VC_ROLE.value(role_t::VCR_VARIABLE))
                              .append(" ")
                              .pad_to(width.value_or(0) + 1)
                              .append(p.second[0].sm_description)
                              .with_attr_for_all(lnav::prompt::SUBST_TEXT.value(
                                  p.first + " "));
                      });
            } else {
                auto x = prompt.p_editor.get_cursor_offset();
                if (!script_name.empty() && split_args[0].se_origin.sf_end == x)
                {
                    poss = scripts.as_scripts | lnav::itertools::first()
                        | lnav::itertools::similar_to(script_name, 10)
                        | lnav::itertools::map([&width, &script_name, &scripts](
                                                   const auto& x) {
                               auto siter = scripts.as_scripts.find(x);
                               auto desc = siter->second[0].sm_description;
                               return attr_line_t()
                                   .append(x,
                                           VC_ROLE.value(role_t::VCR_VARIABLE))
                                   .highlight_fuzzy_matches(script_name)
                                   .append(" ")
                                   .pad_to(width.value_or(0) + 1)
                                   .append(desc)
                                   .with_attr_for_all(
                                       lnav::prompt::SUBST_TEXT.value(x + " "));
                           });
                }
            }

            prompt.p_editor.open_popup_for_completion(0, poss);
        } else {
            auto& meta = iter->second[0];
            auto help_text
                = fmt::format(FMT_STRING(ANSI_BOLD("{}") " -- {}   " ABORT_MSG),
                              meta.sm_synopsis,
                              meta.sm_description);
            lnav_data.ld_bottom_source.set_prompt(help_text);
        }
    }
}

void
rl_change(textinput_curses& rc)
{
    static auto& prompt = lnav::prompt::get();
    auto* tc = get_textview_for_mode(lnav_data.ld_mode);

    rc.tc_suggestion.clear();
    tc->get_highlights().erase({highlight_source_t::PREVIEW, "preview"});
    tc->get_highlights().erase({highlight_source_t::PREVIEW, "bodypreview"});
    lnav_data.ld_log_source.set_preview_sql_filter(nullptr);
    lnav_data.ld_user_message_source.clear();

    log_debug("rl_change");

    if (prompt.p_editor.tc_mode == textinput_curses::mode_t::show_help) {
        return;
    }

    if (rc.tc_popup_type == textinput_curses::popup_type_t::history
        && !prompt.p_replace_from_history)
    {
        rc.tc_on_history_search(rc);
        return;
    }

    switch (lnav_data.ld_mode) {
        case ln_mode_t::SEARCH:
        case ln_mode_t::SEARCH_FILES:
        case ln_mode_t::SEARCH_FILTERS:
        case ln_mode_t::SEARCH_SPECTRO_DETAILS: {
            rl_search_change(rc, false);
            break;
        }
        case ln_mode_t::SQL: {
            rl_sql_change(rc, false);
            break;
        }
        case ln_mode_t::COMMAND: {
            rl_cmd_change(rc, false);
            break;
        }
        case ln_mode_t::EXEC: {
            rl_exec_change(rc, false);
            break;
        }
        default:
            break;
    }
}

static void
rl_search_internal(textinput_curses& rc, ln_mode_t mode, bool complete = false)
{
    static const intern_string_t SRC = intern_string::lookup("prompt");
    static auto& prompt = lnav::prompt::get();

    auto* tc = get_textview_for_mode(mode);
    std::string term_val;
    std::string name;

    tc->get_highlights().erase({highlight_source_t::PREVIEW, "preview"});
    tc->get_highlights().erase({highlight_source_t::PREVIEW, "bodypreview"});
    lnav_data.ld_log_source.set_preview_sql_filter(nullptr);
    tc->reload_data();
    lnav_data.ld_user_message_source.clear();

    switch (mode) {
        case ln_mode_t::SEARCH:
        case ln_mode_t::SEARCH_FILTERS:
        case ln_mode_t::SEARCH_FILES:
        case ln_mode_t::SEARCH_SPECTRO_DETAILS:
            name = "$search";
            break;

        case ln_mode_t::CAPTURE:
            require(0);
            name = "$capture";
            break;

        case ln_mode_t::COMMAND: {
            auto& ec = lnav_data.ld_exec_context;
            ec.ec_dry_run = true;

            lnav_data.ld_preview_generation += 1;
            clear_preview();
            auto src_guard = ec.enter_source(
                SRC, 1, fmt::format(FMT_STRING(":{}"), rc.get_content()));
            readline_lnav_highlighter(ec.ec_source.back().s_content, -1);
            ec.ec_source.back().s_content.with_attr_for_all(
                VC_ROLE.value(role_t::VCR_QUOTED_CODE));
            auto result = execute_command(ec, rc.get_content());

            if (result.isOk()) {
                auto msg = result.unwrap();

                if (msg.empty()) {
                    lnav_data.ld_bottom_source.set_prompt(
                        prompt.p_editor.tc_height > 1
                            ? LNAV_MULTILINE_CMD_PROMPT
                            : LNAV_CMD_PROMPT);
                    lnav_data.ld_bottom_source.grep_error("");
                } else {
                    lnav_data.ld_bottom_source.set_prompt(msg);
                    lnav_data.ld_bottom_source.grep_error("");
                }
            } else {
                lnav_data.ld_bottom_source.set_prompt("");
                lnav_data.ld_bottom_source.grep_error(
                    result.unwrapErr().um_message.get_string());
            }

            lnav_data.ld_preview_view[0].reload_data();

            ec.ec_dry_run = false;
            return;
        }

        case ln_mode_t::SQL: {
            term_val = trim(rc.get_content());

            if (!term_val.empty() && term_val[0] == '.') {
                lnav_data.ld_bottom_source.grep_error("");
            } else if (lnav::sql::is_prql(term_val)) {
                std::string alt_msg;

                lnav_data.ld_doc_source.replace_with(PRQL_HELP);
                lnav_data.ld_example_source.replace_with(
                    format_sql_example(PRQL_EXAMPLE));
                lnav_data.ld_db_preview_source[0].clear();
                lnav_data.ld_db_preview_source[1].clear();

                auto orig_prql_stmt = attr_line_t(term_val);
                orig_prql_stmt.rtrim("| \r\n\t");
                annotate_sql_statement(orig_prql_stmt);

                auto cursor_x = rc.get_cursor_offset();
                if (cursor_x >= orig_prql_stmt.get_string().length()) {
                    cursor_x = orig_prql_stmt.get_string().length() - 1;
                }

                auto curr_stage_iter
                    = find_string_attr_containing(orig_prql_stmt.get_attrs(),
                                                  &lnav::sql::PRQL_STAGE_ATTR,
                                                  cursor_x);
                ensure(curr_stage_iter != orig_prql_stmt.al_attrs.end());
                auto curr_stage_prql = orig_prql_stmt.subline(
                    0, curr_stage_iter->sa_range.lr_end);
                for (auto riter = curr_stage_prql.get_attrs().rbegin();
                     riter != curr_stage_prql.get_attrs().rend();
                     ++riter)
                {
                    if (riter->sa_type != &lnav::sql::PRQL_STAGE_ATTR
                        || riter->sa_range.lr_start == 0
                        || riter->sa_range.empty())
                    {
                        continue;
                    }
                    auto take10k = std::string("\ntake 10000 ");
                    if (curr_stage_prql.al_string[riter->sa_range.lr_start]
                        != '|')
                    {
                        take10k.append("\n ");
                    }
                    curr_stage_prql.insert(riter->sa_range.lr_start, take10k);
                }
                curr_stage_prql.rtrim();
                curr_stage_prql.append(" | take 5");
                log_debug("preview prql: %s",
                          curr_stage_prql.get_string().c_str());

                size_t curr_stage_index = 0;
                if (curr_stage_iter->sa_range.lr_start > 0) {
                    auto prev_stage_iter = find_string_attr_containing(
                        orig_prql_stmt.get_attrs(),
                        &lnav::sql::PRQL_STAGE_ATTR,
                        curr_stage_iter->sa_range.lr_start - 1);
                    auto prev_stage_prql = orig_prql_stmt.subline(
                        0, prev_stage_iter->sa_range.lr_end);
                    for (auto riter = prev_stage_prql.get_attrs().rbegin();
                         riter != prev_stage_prql.get_attrs().rend();
                         ++riter)
                    {
                        if (riter->sa_type != &lnav::sql::PRQL_STAGE_ATTR
                            || riter->sa_range.lr_start == 0
                            || riter->sa_range.empty())
                        {
                            continue;
                        }
                        auto take10k = std::string("\ntake 10000 ");
                        if (prev_stage_prql.al_string[riter->sa_range.lr_start]
                            != '|')
                        {
                            take10k.append("\n ");
                        }
                        prev_stage_prql.insert(riter->sa_range.lr_start,
                                               take10k);
                    }
                    prev_stage_prql.append("\ntake 5");

                    curr_stage_index = 1;
                    auto src_guard = lnav_data.ld_exec_context.enter_source(
                        SRC, 1, prev_stage_prql.get_string());
                    auto db_guard = lnav_data.ld_exec_context.enter_db_source(
                        &lnav_data.ld_db_preview_source[0]);
                    auto exec_res = execute_sql(lnav_data.ld_exec_context,
                                                prev_stage_prql.get_string(),
                                                alt_msg);
                    lnav_data.ld_preview_status_source[0]
                        .get_description()
                        .set_value("Result for query: %s",
                                   prev_stage_prql.get_string().c_str());
                    if (exec_res.isOk()) {
                        lnav_data.ld_preview_view[0].set_sub_source(
                            &lnav_data.ld_db_preview_source[0]);
                        lnav_data.ld_preview_view[0].set_overlay_source(
                            &lnav_data.ld_db_preview_overlay_source[0]);
                    } else {
                        lnav_data.ld_preview_source[0].replace_with(
                            exec_res.unwrapErr().to_attr_line());
                        lnav_data.ld_preview_view[0].set_sub_source(
                            &lnav_data.ld_preview_source[0]);
                        lnav_data.ld_preview_view[0].set_overlay_source(
                            nullptr);
                    }
                }

                auto src_guard = lnav_data.ld_exec_context.enter_source(
                    SRC, 1, curr_stage_prql.get_string());
                auto db_guard = lnav_data.ld_exec_context.enter_db_source(
                    &lnav_data.ld_db_preview_source[curr_stage_index]);
                auto exec_res = execute_sql(lnav_data.ld_exec_context,
                                            curr_stage_prql.get_string(),
                                            alt_msg);
                auto err = exec_res.isErr()
                    ? exec_res.unwrapErr()
                    : lnav::console::user_message::ok({});
                if (exec_res.isErr()) {
                    lnav_data.ld_bottom_source.grep_error(
                        err.um_reason.get_string());

                    curr_stage_prql.erase(curr_stage_prql.get_string().length()
                                          - 8);
                    auto near = curr_stage_prql.get_string().length() - 1;
                    while (near > 0) {
                        auto paren_iter = rfind_string_attr_if(
                            curr_stage_prql.get_attrs(),
                            near,
                            [](const string_attr& sa) {
                                return sa.sa_type
                                    == &lnav::sql::PRQL_UNTERMINATED_PAREN_ATTR;
                            });

                        if (paren_iter == curr_stage_prql.get_attrs().end()) {
                            break;
                        }
                        switch (
                            curr_stage_prql
                                .get_string()[paren_iter->sa_range.lr_start])
                        {
                            case '(':
                                curr_stage_prql.append(")");
                                break;
                            case '{':
                                curr_stage_prql.append("}");
                                break;
                        }
                        near = paren_iter->sa_range.lr_start - 1;
                    }

                    curr_stage_prql.append("\ntake 5");
                    auto exec_termed_res
                        = execute_sql(lnav_data.ld_exec_context,
                                      curr_stage_prql.get_string(),
                                      alt_msg);
                    if (exec_termed_res.isErr()) {
                        err = exec_termed_res.unwrapErr();
                    }
                } else {
                    lnav_data.ld_bottom_source.grep_error("");
                }

                rl_sql_help(rc);

                lnav_data.ld_preview_status_source[curr_stage_index]
                    .get_description()
                    .set_value("Result for query: %s",
                               curr_stage_prql.get_string().c_str());
                if (!lnav_data.ld_db_preview_source[curr_stage_index]
                         .dls_headers.empty())
                {
                    if (curr_stage_index == 0) {
#if 0
                        for (const auto& hdr :
                             lnav_data.ld_db_preview_source[curr_stage_index]
                                 .dls_headers)
                        {
                            rc->add_possibility(
                                ln_mode_t::SQL,
                                "prql-expr",
                                lnav::prql::quote_ident(hdr.hm_name));
                        }
#endif
                    }

                    lnav_data.ld_preview_view[curr_stage_index].set_sub_source(
                        &lnav_data.ld_db_preview_source[curr_stage_index]);
                    lnav_data.ld_preview_view[curr_stage_index]
                        .set_overlay_source(
                            &lnav_data.ld_db_preview_overlay_source
                                 [curr_stage_index]);
                } else if (exec_res.isErr()) {
                    lnav_data.ld_preview_source[curr_stage_index].replace_with(
                        err.to_attr_line());
                    lnav_data.ld_preview_view[curr_stage_index].set_sub_source(
                        &lnav_data.ld_preview_source[curr_stage_index]);
                    lnav_data.ld_preview_view[curr_stage_index]
                        .set_overlay_source(nullptr);
                }
                return;
            }

            term_val += ";";
            if (!sqlite3_complete(term_val.c_str())) {
                lnav_data.ld_bottom_source.grep_error(
                    "SQL error: incomplete statement");
            } else {
                auto_mem<sqlite3_stmt> stmt(sqlite3_finalize);
                int retcode;

                retcode = sqlite3_prepare_v2(lnav_data.ld_db,
                                             rc.get_content().c_str(),
                                             -1,
                                             stmt.out(),
                                             nullptr);
                if (retcode != SQLITE_OK) {
                    const char* errmsg = sqlite3_errmsg(lnav_data.ld_db);

                    lnav_data.ld_bottom_source.grep_error(
                        fmt::format(FMT_STRING("SQL error: {}"), errmsg));

#if defined(HAVE_SQLITE3_ERROR_OFFSET)
                    {
                        auto erroff = sqlite3_error_offset(lnav_data.ld_db);

                        if (erroff >= 0) {
                            auto mark = rc.get_point_for_offset(erroff);
                            auto um
                                = lnav::console::user_message::error(errmsg);

                            rc.add_mark(mark, um);
                        }
                    }
#endif
                } else {
                    lnav_data.ld_bottom_source.grep_error("");
                }
            }

            if (!rl_sql_help(rc)) {
                rl_set_help();
            }
            return;
        }

        case ln_mode_t::BREADCRUMBS:
        case ln_mode_t::PAGING:
        case ln_mode_t::FILTER:
        case ln_mode_t::FILES:
        case ln_mode_t::FILE_DETAILS:
        case ln_mode_t::EXEC:
        case ln_mode_t::USER:
        case ln_mode_t::SPECTRO_DETAILS:
        case ln_mode_t::BUSY:
            return;
    }

    if (!complete) {
        tc->set_selection(lnav_data.ld_search_start_line);
    }
    tc->execute_search(rc.get_content());
}

void
rl_search(textinput_curses& rc)
{
    auto* tc = get_textview_for_mode(lnav_data.ld_mode);

    rl_search_internal(rc, lnav_data.ld_mode);
    tc->set_follow_search_for(0, {});
}

void
lnav_rl_abort(textinput_curses& rc)
{
    auto* tc = get_textview_for_mode(lnav_data.ld_mode);

    lnav_data.ld_bottom_source.set_prompt("");
    lnav_data.ld_example_source.clear();
    lnav_data.ld_doc_source.clear();
    clear_preview();
    tc->get_highlights().erase({highlight_source_t::PREVIEW, "preview"});
    tc->get_highlights().erase({highlight_source_t::PREVIEW, "bodypreview"});
    lnav_data.ld_log_source.set_preview_sql_filter(nullptr);

    std::vector<lnav::console::user_message> errors;
    lnav_config = rollback_lnav_config;
    reload_config(errors);

    lnav_data.ld_bottom_source.grep_error("");
    switch (lnav_data.ld_mode) {
        case ln_mode_t::SEARCH:
            tc->set_selection(lnav_data.ld_search_start_line);
            tc->revert_search();
            break;
        case ln_mode_t::SQL:
            tc->reload_data();
            break;
        default:
            break;
    }
    rc.clear_inactive_value();

    set_view_mode(ln_mode_t::PAGING);
}

void
rl_callback(textinput_curses& rc)
{
    static const intern_string_t SRC = intern_string::lookup("prompt");
    static auto& prompt = lnav::prompt::get();
    auto is_alt = prompt.p_alt_mode;

    auto* tc = get_textview_for_mode(lnav_data.ld_mode);
    auto& ec = lnav_data.ld_exec_context;
    std::string alt_msg;

    lnav_data.ld_bottom_source.set_prompt("");
    lnav_data.ld_doc_source.clear();
    lnav_data.ld_example_source.clear();
    clear_preview();
    tc->get_highlights().erase({highlight_source_t::PREVIEW, "preview"});
    tc->get_highlights().erase({highlight_source_t::PREVIEW, "bodypreview"});
    lnav_data.ld_log_source.set_preview_sql_filter(nullptr);

    auto new_mode = ln_mode_t::PAGING;

    switch (lnav_data.ld_mode) {
        case ln_mode_t::SEARCH_FILTERS:
            new_mode = ln_mode_t::FILTER;
            break;
        case ln_mode_t::SEARCH_FILES:
            new_mode = ln_mode_t::FILES;
            break;
        case ln_mode_t::SEARCH_SPECTRO_DETAILS:
            new_mode = ln_mode_t::SPECTRO_DETAILS;
            break;
        default:
            break;
    }

    auto old_mode = lnav_data.ld_mode;
    set_view_mode(new_mode);
    switch (old_mode) {
        case ln_mode_t::BREADCRUMBS:
        case ln_mode_t::PAGING:
        case ln_mode_t::FILTER:
        case ln_mode_t::FILES:
        case ln_mode_t::FILE_DETAILS:
        case ln_mode_t::SPECTRO_DETAILS:
        case ln_mode_t::BUSY:
            require(0);
            break;

        case ln_mode_t::COMMAND: {
            rc.clear_alt_value();
            auto cmdline = rc.get_content();
            auto src_guard = lnav_data.ld_exec_context.enter_source(
                SRC, 1, fmt::format(FMT_STRING(":{}"), cmdline));
            readline_lnav_highlighter(ec.ec_source.back().s_content, -1);
            ec.ec_source.back().s_content.with_attr_for_all(
                VC_ROLE.value(role_t::VCR_QUOTED_CODE));
            auto hist_guard = prompt.p_cmd_history.start_operation(cmdline);
            auto exec_res = execute_command(ec, cmdline);
            if (exec_res.isOk()) {
                rc.set_inactive_value(exec_res.unwrap());
            } else {
                auto um = exec_res.unwrapErr();

                hist_guard.og_status = log_level_t::LEVEL_ERROR;
                lnav_data.ld_user_message_source.replace_with(
                    um.to_attr_line().rtrim());
                lnav_data.ld_user_message_view.reload_data();
                lnav_data.ld_user_message_expiration
                    = std::chrono::steady_clock::now() + 20s;
                rc.clear_inactive_value();
            }
            ec.ec_source.back().s_content.clear();
            break;
        }

        case ln_mode_t::USER:
            rc.clear_alt_value();
            ec.ec_local_vars.top()["value"] = rc.get_content();
            rc.clear_inactive_value();
            break;

        case ln_mode_t::SEARCH:
        case ln_mode_t::SEARCH_FILTERS:
        case ln_mode_t::SEARCH_FILES:
        case ln_mode_t::SEARCH_SPECTRO_DETAILS:
        case ln_mode_t::CAPTURE: {
            log_debug("search here!");
            auto cmdline = rc.get_content();
            rl_search_internal(rc, old_mode, true);
            if (!cmdline.empty()) {
                auto hist_guard
                    = prompt.p_search_history.start_operation(cmdline);
                auto& bm = tc->get_bookmarks();
                const auto& bv = bm[&textview_curses::BM_SEARCH];
                auto vl = is_alt ? bv.prev(tc->get_selection())
                                 : bv.next(tc->get_top());

                if (vl) {
                    tc->set_selection(vl.value());
                } else {
                    tc->set_follow_search_for(2000, [tc, is_alt, &bm]() {
                        if (bm[&textview_curses::BM_SEARCH].empty()) {
                            return false;
                        }

                        if (is_alt && tc->is_searching()) {
                            return false;
                        }

                        std::optional<vis_line_t> first_hit;

                        if (is_alt) {
                            first_hit = bm[&textview_curses::BM_SEARCH].prev(
                                vis_line_t(tc->get_selection()));
                        } else {
                            first_hit = bm[&textview_curses::BM_SEARCH].next(
                                vis_line_t(tc->get_top() - 1));
                        }
                        if (first_hit) {
                            auto first_hit_vl = first_hit.value();
                            if (tc->is_selectable()) {
                                tc->set_selection(first_hit_vl);
                            } else {
                                if (first_hit_vl > 0_vl) {
                                    --first_hit_vl;
                                }
                                tc->set_top(first_hit_vl);
                            }
                        }

                        return true;
                    });
                }
                rc.set_inactive_value(
                    attr_line_t("search: ").append(rc.get_content()));
                rc.set_alt_value(HELP_MSG_2(
                    n, N, "to move forward/backward through search results"));
            }
            break;
        }

        case ln_mode_t::SQL: {
            auto sql_str = rc.get_content();

            if (sql_str.empty()) {
                return;
            }
            auto src_guard = lnav_data.ld_exec_context.enter_source(
                SRC, 1, fmt::format(FMT_STRING(";{}"), sql_str));
            readline_lnav_highlighter(ec.ec_source.back().s_content, -1);
            ec.ec_source.back().s_content.with_attr_for_all(
                VC_ROLE.value(role_t::VCR_QUOTED_CODE));

            rc.set_inactive_value(
                lnav::console::user_message::info(
                    attr_line_t("executing SQL statement, press ")
                        .append("CTRL+]"_hotkey)
                        .append(" to cancel"))
                    .to_attr_line());
            rc.set_needs_update();
            auto hist_guard = prompt.p_sql_history.start_operation(sql_str);
            auto result = execute_sql(ec, sql_str, alt_msg);
            auto& dls = lnav_data.ld_db_row_source;
            attr_line_t prompt;

            if (result.isOk()) {
                auto msg = result.unwrap();

                if (!msg.empty()) {
                    prompt = lnav::console::user_message::ok(
                                 attr_line_t("SQL Result: ")
                                     .append(attr_line_t::from_ansi_str(
                                         msg.c_str())))
                                 .to_attr_line();
                    if (dls.dls_row_cursors.size() > 1) {
                        ensure_view(&lnav_data.ld_views[LNV_DB]);
                    }
                }
            } else {
                hist_guard.og_status = log_level_t::LEVEL_ERROR;
                auto um = result.unwrapErr();
                lnav_data.ld_user_message_source.replace_with(
                    um.to_attr_line().rtrim());
                lnav_data.ld_user_message_view.reload_data();
                lnav_data.ld_user_message_expiration
                    = std::chrono::steady_clock::now() + 20s;
            }
            ec.ec_source.back().s_content.clear();

            rc.set_inactive_value(prompt);
            rc.set_alt_value(alt_msg);
            break;
        }

        case ln_mode_t::EXEC: {
            std::error_code errc;
            std::filesystem::create_directories(lnav::paths::workdir(), errc);
            auto open_temp_res = lnav::filesystem::open_temp_file(
                lnav::paths::workdir() / "exec.XXXXXX");

            if (open_temp_res.isErr()) {
                rc.set_inactive_value(fmt::format(
                    FMT_STRING("Unable to open temporary output file: {}"),
                    open_temp_res.unwrapErr()));
            } else {
                char desc[256], timestamp[32];
                time_t current_time = time(nullptr);
                const auto path_and_args = rc.get_content();
                auto tmp_pair = open_temp_res.unwrap();
                auto fd_copy = tmp_pair.second.dup();
                auto tf = text_format_t::TF_UNKNOWN;

                {
                    exec_context::output_guard og(
                        ec,
                        "tmp",
                        std::make_pair(fdopen(tmp_pair.second.release(), "w"),
                                       fclose));
                    auto src_guard = lnav_data.ld_exec_context.enter_source(
                        SRC, 1, fmt::format(FMT_STRING("|{}"), path_and_args));
                    auto hist_guard = prompt.p_script_history.start_operation(
                        path_and_args);
                    auto exec_res = execute_file(ec, path_and_args);
                    if (exec_res.isOk()) {
                        rc.set_inactive_value(exec_res.unwrap());
                        tf = ec.ec_output_stack.back().od_format;
                    } else {
                        auto um = exec_res.unwrapErr();

                        hist_guard.og_status = log_level_t::LEVEL_ERROR;
                        lnav_data.ld_user_message_source.replace_with(
                            um.to_attr_line().rtrim());
                        lnav_data.ld_user_message_view.reload_data();
                        lnav_data.ld_user_message_expiration
                            = std::chrono::steady_clock::now() + 20s;
                        rc.clear_inactive_value();
                    }
                }

                tm current_tm;
                struct stat st;

                if (fstat(fd_copy, &st) != -1 && st.st_size > 0) {
                    strftime(timestamp,
                             sizeof(timestamp),
                             "%a %b %d %H:%M:%S %Z",
                             localtime_r(&current_time, &current_tm));
                    snprintf(desc,
                             sizeof(desc),
                             "Output of %s (%s)",
                             path_and_args.c_str(),
                             timestamp);
                    lnav_data.ld_active_files.fc_file_names[tmp_pair.first]
                        .with_filename(desc)
                        .with_include_in_session(false)
                        .with_detect_format(false)
                        .with_text_format(tf)
                        .with_init_location(0_vl);
                    lnav_data.ld_files_to_front.emplace_back(desc);

                    rc.set_alt_value(HELP_MSG_1(X, "to close the file"));
                }
            }
            break;
        }
    }
}

void
rl_completion_request(textinput_curses& rc)
{
    switch (lnav_data.ld_mode) {
        case ln_mode_t::SEARCH:
        case ln_mode_t::SEARCH_FILES:
        case ln_mode_t::SEARCH_FILTERS:
        case ln_mode_t::SEARCH_SPECTRO_DETAILS: {
            rl_search_change(rc, true);
            break;
        }
        case ln_mode_t::COMMAND: {
            rl_cmd_change(rc, true);
            break;
        }
        case ln_mode_t::SQL: {
            rl_sql_change(rc, true);
            break;
        }
        case ln_mode_t::EXEC: {
            rl_exec_change(rc, true);
            break;
        }
    }
}

void
rl_focus(textinput_curses& rc)
{
    auto fos = (field_overlay_source*) lnav_data.ld_views[LNV_LOG]
                   .get_overlay_source();

    fos->fos_contexts.emplace("", false, true, true);

    get_textview_for_mode(lnav_data.ld_mode)->save_current_search();
}

void
rl_blur(textinput_curses& rc)
{
    auto fos = (field_overlay_source*) lnav_data.ld_views[LNV_LOG]
                   .get_overlay_source();

    fos->fos_contexts.pop();
    ensure(!fos->fos_contexts.empty());
    for (auto& tc : lnav_data.ld_views) {
        tc.set_sync_selection_and_top(false);
    }
    lnav_data.ld_preview_generation += 1;
    lnav_data.ld_bottom_source.grep_error("");
}

readline_context::split_result_t
prql_splitter(const attr_line_t& stmt)
{
    readline_context::split_result_t retval;
    readline_context::stage st;

    for (const auto& attr : stmt.get_attrs()) {
        if (attr.sa_type == &lnav::sql::PRQL_STAGE_ATTR) {
        } else if (attr.sa_type == &lnav::sql::PRQL_PIPE_ATTR) {
            retval.sr_stages.emplace_back(st);
            st.s_args.clear();
        } else {
            st.s_args.emplace_back(attr.sa_range);
        }
    }
    if (!stmt.empty() && isspace(stmt.al_string.back())) {
        st.s_args.emplace_back(stmt.length(), stmt.length());
    }
    retval.sr_stages.emplace_back(st);

    return retval;
}
