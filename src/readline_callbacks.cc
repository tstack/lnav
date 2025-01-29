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

#include "base/fs_util.hh"
#include "base/humanize.network.hh"
#include "base/injector.hh"
#include "base/paths.hh"
#include "bound_tags.hh"
#include "command_executor.hh"
#include "config.h"
#include "field_overlay_source.hh"
#include "help_text_formatter.hh"
#include "lnav.hh"
#include "lnav_config.hh"
#include "log_format_loader.hh"
#include "plain_text_source.hh"
#include "readline_curses.hh"
#include "readline_highlighters.hh"
#include "service_tags.hh"
#include "sql_help.hh"
#include "tailer/tailer.looper.hh"
#include "view_helpers.examples.hh"
#include "vtab_module.hh"
#include "yajlpp/yajlpp.hh"

using namespace std::chrono_literals;
using namespace lnav::roles::literals;

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

static const char* const LNAV_CMD_PROMPT = "Enter an lnav command: " ABORT_MSG;

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
rl_sql_help(readline_curses* rc)
{
    auto al = attr_line_t(rc->get_line_buffer());
    const auto& sa = al.get_attrs();
    size_t x = rc->get_cursor_x();
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
            sa, &lnav::sql::PRQL_IDENTIFIER_ATTR, al.nearest_text(x));
    }
    if (ident_iter != sa.end()) {
        auto ident = al.get_substring(ident_iter->sa_range);
        auto intern_ident = intern_string::lookup(ident);
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
            lnav_data.ld_preview_source[0]
                .replace_with(ddl)
                .set_text_format(text_format_t::TF_SQL)
                .truncate_to(30);
            lnav_data.ld_preview_status_source[0].get_description().set_value(
                "Definition for table -- %s", ident.c_str());
        }
    }

    return has_doc;
}

void
rl_change(readline_curses* rc)
{
    static const std::set<std::string> COMMANDS_WITH_SQL = {
        "filter-expr",
        "mark-expr",
    };

    static const std::set<std::string> COMMANDS_FOR_FIELDS = {
        "hide-fields",
        "show-fields",
    };

    auto* tc = get_textview_for_mode(lnav_data.ld_mode);

    tc->get_highlights().erase({highlight_source_t::PREVIEW, "preview"});
    tc->get_highlights().erase({highlight_source_t::PREVIEW, "bodypreview"});
    lnav_data.ld_log_source.set_preview_sql_filter(nullptr);
    lnav_data.ld_user_message_source.clear();

    switch (lnav_data.ld_mode) {
        case ln_mode_t::SEARCH: {
            if (rc->get_line_buffer().empty() && tc->tc_selected_text) {
                rc->set_suggestion(tc->tc_selected_text->sti_value);
            }
            break;
        }
        case ln_mode_t::SQL: {
            static const auto* sql_cmd_map
                = injector::get<readline_context::command_map_t*,
                                sql_cmd_map_tag>();

            const auto line = rc->get_line_buffer();
            std::vector<std::string> args;

            if (!lnav::sql::is_prql(line)) {
                clear_preview();
            }

            split_ws(line, args);
            if (!args.empty()) {
                auto cmd_iter = sql_cmd_map->find(args[0]);
                if (cmd_iter != sql_cmd_map->end()) {
                    const auto* sql_cmd = cmd_iter->second;
                    if (sql_cmd->c_prompt != nullptr) {
                        const auto prompt_res = sql_cmd->c_prompt(
                            lnav_data.ld_exec_context, line);

                        rc->set_suggestion(prompt_res.pr_suggestion);
                    }
                }
            }
            break;
        }
        case ln_mode_t::COMMAND: {
            clear_preview();

            static std::string last_command;
            static int generation = 0;

            const auto line = rc->get_line_buffer();
            std::vector<std::string> args;
            auto iter = lnav_commands.end();

            split_ws(line, args);

            if (args.empty()) {
                generation = 0;
            } else if (args[0] != last_command) {
                last_command = args[0];
                generation = 0;
            } else {
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
                || (args.size() == 1 && !endswith(line, " ")))
            {
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
                    char help_text[1024];

                    snprintf(help_text,
                             sizeof(help_text),
                             ANSI_BOLD("%s %s") " -- %s    " ABORT_MSG,
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
                        rc->rewrite_line(line.length(),
                                         prompt_res.pr_new_prompt);
                    }
                    rc->set_suggestion(prompt_res.pr_suggestion);
                }

                lnav_data.ld_bottom_source.grep_error("");
                lnav_data.ld_status[LNS_BOTTOM].window_change();
            }
            break;
        }
        case ln_mode_t::EXEC: {
            clear_preview();

            const auto line = rc->get_line_buffer();
            size_t name_end = line.find(' ');
            const auto script_name = line.substr(0, name_end);
            auto& scripts = injector::get<available_scripts&>();
            auto iter = scripts.as_scripts.find(script_name);

            if (iter == scripts.as_scripts.end()
                || iter->second[0].sm_description.empty())
            {
                lnav_data.ld_bottom_source.set_prompt(
                    "Enter a script to execute: " ABORT_MSG);
            } else {
                auto& meta = iter->second[0];
                char help_text[1024];

                snprintf(help_text,
                         sizeof(help_text),
                         ANSI_BOLD("%s") " -- %s   " ABORT_MSG,
                         meta.sm_synopsis.c_str(),
                         meta.sm_description.c_str());
                lnav_data.ld_bottom_source.set_prompt(help_text);
            }
            break;
        }
        default:
            break;
    }
}

static void
rl_search_internal(readline_curses* rc, ln_mode_t mode, bool complete = false)
{
    static const intern_string_t SRC = intern_string::lookup("prompt");

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
                SRC,
                1,
                fmt::format(FMT_STRING(":{}"), rc->get_value().get_string()));
            readline_lnav_highlighter(ec.ec_source.back().s_content, -1);
            ec.ec_source.back().s_content.with_attr_for_all(
                VC_ROLE.value(role_t::VCR_QUOTED_CODE));
            auto result = execute_command(ec, rc->get_value().get_string());

            if (result.isOk()) {
                auto msg = result.unwrap();

                if (msg.empty()) {
                    lnav_data.ld_bottom_source.set_prompt(LNAV_CMD_PROMPT);
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
            term_val = trim(rc->get_value().get_string());

            if (!term_val.empty() && term_val[0] == '.') {
                lnav_data.ld_bottom_source.grep_error("");
            } else if (lnav::sql::is_prql(term_val)) {
                std::string alt_msg;

                lnav_data.ld_doc_source.replace_with(PRQL_HELP);
                lnav_data.ld_example_source.replace_with(
                    format_sql_example(PRQL_EXAMPLE));
                lnav_data.ld_db_preview_source[0].clear();
                lnav_data.ld_db_preview_source[1].clear();
                rc->clear_possibilities(ln_mode_t::SQL, "prql-expr");

                auto orig_prql_stmt = attr_line_t(term_val);
                orig_prql_stmt.rtrim("| \r\n\t");
                annotate_sql_statement(orig_prql_stmt);
                auto cursor_x = rc->get_cursor_x();
                if (cursor_x > orig_prql_stmt.get_string().length()) {
                    cursor_x = orig_prql_stmt.length() - 1;
                }
                auto curr_stage_iter
                    = find_string_attr_containing(orig_prql_stmt.get_attrs(),
                                                  &lnav::sql::PRQL_STAGE_ATTR,
                                                  cursor_x);
                auto curr_stage_prql = orig_prql_stmt.subline(
                    0, curr_stage_iter->sa_range.lr_end);
                for (auto riter = curr_stage_prql.get_attrs().rbegin();
                     riter != curr_stage_prql.get_attrs().rend();
                     ++riter)
                {
                    if (riter->sa_type != &lnav::sql::PRQL_STAGE_ATTR
                        || riter->sa_range.lr_start == 0)
                    {
                        continue;
                    }
                    curr_stage_prql.insert(riter->sa_range.lr_start,
                                           "| take 10000 ");
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
                            || riter->sa_range.lr_start == 0)
                        {
                            continue;
                        }
                        prev_stage_prql.insert(riter->sa_range.lr_start,
                                               "| take 10000 ");
                    }
                    prev_stage_prql.append(" | take 5");

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
                        for (const auto& hdr :
                             lnav_data.ld_db_preview_source[0].dls_headers)
                        {
                            rc->add_possibility(
                                ln_mode_t::SQL,
                                "prql-expr",
                                lnav::prql::quote_ident(hdr.hm_name));
                        }

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
                                          - 9);
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

                    curr_stage_prql.append(" | take 5");
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

                rc->add_possibility(
                    ln_mode_t::SQL, "prql-expr", lnav::sql::prql_keywords);
                for (const auto& pair : lnav::sql::prql_functions) {
                    rc->add_possibility(
                        ln_mode_t::SQL, "prql-expr", pair.first);
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
                        for (const auto& hdr :
                             lnav_data.ld_db_preview_source[curr_stage_index]
                                 .dls_headers)
                        {
                            rc->add_possibility(
                                ln_mode_t::SQL,
                                "prql-expr",
                                lnav::prql::quote_ident(hdr.hm_name));
                        }
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

                retcode
                    = sqlite3_prepare_v2(lnav_data.ld_db,
                                         rc->get_value().get_string().c_str(),
                                         -1,
                                         stmt.out(),
                                         nullptr);
                if (retcode != SQLITE_OK) {
                    const char* errmsg = sqlite3_errmsg(lnav_data.ld_db);

                    lnav_data.ld_bottom_source.grep_error(
                        fmt::format(FMT_STRING("SQL error: {}"), errmsg));
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
    tc->execute_search(rc->get_value().get_string());
}

void
rl_search(readline_curses* rc)
{
    auto* tc = get_textview_for_mode(lnav_data.ld_mode);

    rl_search_internal(rc, lnav_data.ld_mode);
    tc->set_follow_search_for(0, {});
}

void
lnav_rl_abort(readline_curses* rc)
{
    textview_curses* tc = get_textview_for_mode(lnav_data.ld_mode);

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
    lnav_data.ld_rl_view->set_value("");
    set_view_mode(ln_mode_t::PAGING);
}

static void
rl_callback_int(readline_curses* rc, bool is_alt)
{
    static const intern_string_t SRC = intern_string::lookup("prompt");

    textview_curses* tc = get_textview_for_mode(lnav_data.ld_mode);
    exec_context& ec = lnav_data.ld_exec_context;
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

    auto old_mode = std::exchange(lnav_data.ld_mode, new_mode);
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
            rc->set_alt_value("");
            auto src_guard = lnav_data.ld_exec_context.enter_source(
                SRC,
                1,
                fmt::format(FMT_STRING(":{}"), rc->get_value().get_string()));
            readline_lnav_highlighter(ec.ec_source.back().s_content, -1);
            ec.ec_source.back().s_content.with_attr_for_all(
                VC_ROLE.value(role_t::VCR_QUOTED_CODE));
            auto exec_res = execute_command(ec, rc->get_value().get_string());
            if (exec_res.isOk()) {
                rc->set_value(exec_res.unwrap());
            } else {
                auto um = exec_res.unwrapErr();

                lnav_data.ld_user_message_source.replace_with(
                    um.to_attr_line().rtrim());
                lnav_data.ld_user_message_view.reload_data();
                lnav_data.ld_user_message_expiration
                    = std::chrono::steady_clock::now() + 20s;
                rc->set_value("");
            }
            ec.ec_source.back().s_content.clear();
            break;
        }

        case ln_mode_t::USER:
            rc->set_alt_value("");
            ec.ec_local_vars.top()["value"] = rc->get_value().get_string();
            rc->set_value("");
            break;

        case ln_mode_t::SEARCH:
        case ln_mode_t::SEARCH_FILTERS:
        case ln_mode_t::SEARCH_FILES:
        case ln_mode_t::SEARCH_SPECTRO_DETAILS:
        case ln_mode_t::CAPTURE:
            rl_search_internal(rc, old_mode, true);
            if (!rc->get_value().empty()) {
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
                rc->set_attr_value(
                    attr_line_t("search: ").append(rc->get_value()));
                rc->set_alt_value(HELP_MSG_2(
                    n, N, "to move forward/backward through search results"));
            }
            break;

        case ln_mode_t::SQL: {
            auto sql_str = rc->get_value().get_string();
            auto src_guard = lnav_data.ld_exec_context.enter_source(
                SRC, 1, fmt::format(FMT_STRING(";{}"), sql_str));
            readline_lnav_highlighter(ec.ec_source.back().s_content, -1);
            ec.ec_source.back().s_content.with_attr_for_all(
                VC_ROLE.value(role_t::VCR_QUOTED_CODE));

            rc->set_attr_value(
                lnav::console::user_message::info(
                    attr_line_t("executing SQL statement, press ")
                        .append("CTRL+]"_hotkey)
                        .append(" to cancel"))
                    .to_attr_line());
            rc->set_needs_update();
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
                auto um = result.unwrapErr();
                lnav_data.ld_user_message_source.replace_with(
                    um.to_attr_line().rtrim());
                lnav_data.ld_user_message_view.reload_data();
                lnav_data.ld_user_message_expiration
                    = std::chrono::steady_clock::now() + 20s;
            }
            ec.ec_source.back().s_content.clear();

            rc->set_attr_value(prompt);
            rc->set_alt_value(alt_msg);
            break;
        }

        case ln_mode_t::EXEC: {
            std::error_code errc;
            std::filesystem::create_directories(lnav::paths::workdir(), errc);
            auto open_temp_res = lnav::filesystem::open_temp_file(
                lnav::paths::workdir() / "exec.XXXXXX");

            if (open_temp_res.isErr()) {
                rc->set_value(fmt::format(
                    FMT_STRING("Unable to open temporary output file: {}"),
                    open_temp_res.unwrapErr()));
            } else {
                char desc[256], timestamp[32];
                time_t current_time = time(nullptr);
                const auto path_and_args = rc->get_value();
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
                        SRC,
                        1,
                        fmt::format(FMT_STRING("|{}"),
                                    path_and_args.get_string()));
                    auto exec_res
                        = execute_file(ec, path_and_args.get_string());
                    if (exec_res.isOk()) {
                        rc->set_value(exec_res.unwrap());
                        tf = ec.ec_output_stack.back().od_format;
                    } else {
                        auto um = exec_res.unwrapErr();

                        lnav_data.ld_user_message_source.replace_with(
                            um.to_attr_line().rtrim());
                        lnav_data.ld_user_message_view.reload_data();
                        lnav_data.ld_user_message_expiration
                            = std::chrono::steady_clock::now() + 20s;
                        rc->set_value("");
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
                             path_and_args.get_string().c_str(),
                             timestamp);
                    lnav_data.ld_active_files.fc_file_names[tmp_pair.first]
                        .with_filename(desc)
                        .with_include_in_session(false)
                        .with_detect_format(false)
                        .with_text_format(tf)
                        .with_init_location(0_vl);
                    lnav_data.ld_files_to_front.emplace_back(desc, 0_vl);

                    if (lnav_data.ld_rl_view != nullptr) {
                        lnav_data.ld_rl_view->set_alt_value(
                            HELP_MSG_1(X, "to close the file"));
                    }
                }
            }
            break;
        }
    }
}

void
rl_callback(readline_curses* rc)
{
    rl_callback_int(rc, false);
}

void
rl_alt_callback(readline_curses* rc)
{
    rl_callback_int(rc, true);
}

void
rl_display_matches(readline_curses* rc)
{
    const auto& matches = rc->get_matches();
    auto& tc = lnav_data.ld_match_view;
    int cols;

    auto width = ncplane_dim_x(lnav_data.ld_window);
    auto max_len = rc->get_max_match_length() + 2;
    cols = std::max(1U, width / max_len);

    if (matches.empty()) {
        lnav_data.ld_match_source.clear();
    } else {
        const auto current_match = rc->get_match_string();
        int curr_col = 0;
        attr_line_t al;
        bool add_nl = false;

        for (const auto& match : matches) {
            if (add_nl) {
                al.append(1, '\n');
                add_nl = false;
            }
            if (match == current_match) {
                al.append(match, VC_STYLE.value(text_attrs::with_reverse()));
            } else {
                al.append(match);
            }
            curr_col += 1;
            if (curr_col < cols) {
                int padding = max_len - match.size();

                al.append(padding, ' ');
            } else {
                curr_col = 0;
                add_nl = true;
            }
        }
        lnav_data.ld_match_source.replace_with(al);
    }

    tc.reload_data();
}

void
rl_display_next(readline_curses* rc)
{
    textview_curses& tc = lnav_data.ld_match_view;

    if (tc.get_top() >= (tc.get_top_for_last_row() - 1)) {
        tc.set_top(0_vl);
    } else {
        tc.shift_top(tc.get_height());
    }
}

void
rl_completion_request(readline_curses* rc)
{
    isc::to<tailer::looper&, services::remote_tailer_t>().send(
        [rc](auto& tlooper) {
            auto rp_opt = humanize::network::path::from_str(
                rc->get_remote_complete_path());
            if (rp_opt) {
                tlooper.complete_path(*rp_opt);
            }
        });
}

void
rl_focus(readline_curses* rc)
{
    auto fos = (field_overlay_source*) lnav_data.ld_views[LNV_LOG]
                   .get_overlay_source();

    fos->fos_contexts.emplace("", false, true, true);

    get_textview_for_mode(lnav_data.ld_mode)->save_current_search();
}

void
rl_blur(readline_curses* rc)
{
    auto fos = (field_overlay_source*) lnav_data.ld_views[LNV_LOG]
                   .get_overlay_source();

    fos->fos_contexts.pop();
    ensure(!fos->fos_contexts.empty());
    for (auto& tc : lnav_data.ld_views) {
        tc.set_sync_selection_and_top(false);
    }
    lnav_data.ld_preview_generation += 1;
}

readline_context::split_result_t
prql_splitter(readline_context& rc, const std::string& cmdline)
{
    auto stmt = attr_line_t(cmdline);
    readline_context::split_result_t retval;
    readline_context::stage st;

    lnav::sql::annotate_prql_statement(stmt);
    for (const auto& attr : stmt.get_attrs()) {
        if (attr.sa_type == &lnav::sql::PRQL_STAGE_ATTR) {
        } else if (attr.sa_type == &lnav::sql::PRQL_PIPE_ATTR) {
            retval.sr_stages.emplace_back(st);
            st.s_args.clear();
        } else {
            st.s_args.emplace_back(attr.sa_range);
        }
    }
    if (!cmdline.empty() && isspace(cmdline.back())) {
        st.s_args.emplace_back(cmdline.length(), cmdline.length());
    }
    retval.sr_stages.emplace_back(st);

    return retval;
}
