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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include "lnav.hh"
#include "lnav_util.hh"
#include "sysclip.hh"
#include "plain_text_source.hh"
#include "command_executor.hh"
#include "readline_curses.hh"
#include "log_search_table.hh"
#include "log_format_loader.hh"
#include "help_text_formatter.hh"
#include "sqlite-extension-func.hh"

using namespace std;

#define ABORT_MSG "(Press " ANSI_BOLD("CTRL+]") " to abort)"

static const char *LNAV_CMD_PROMPT = "Enter an lnav command: " ABORT_MSG;

void rl_change(void *dummy, readline_curses *rc)
{
    textview_curses *tc = lnav_data.ld_view_stack.back();

    tc->get_highlights().erase("$preview");
    tc->get_highlights().erase("$bodypreview");
    lnav_data.ld_preview_source.clear();
    lnav_data.ld_preview_status_source.get_description().clear();

    switch (lnav_data.ld_mode) {
        case LNM_COMMAND: {
            string line = rc->get_line_buffer();
            vector<string> args;
            readline_context::command_map_t::iterator iter = lnav_commands.end();

            split_ws(line, args);

            if (!args.empty()) {
                iter = lnav_commands.find(args[0]);
            }
            if (iter == lnav_commands.end()) {
                lnav_data.ld_doc_source.clear();
                lnav_data.ld_example_source.clear();
                lnav_data.ld_preview_source.clear();
                lnav_data.ld_preview_status_source.get_description().clear();
                lnav_data.ld_bottom_source.set_prompt(LNAV_CMD_PROMPT);
                lnav_data.ld_bottom_source.grep_error("");
            }
            else if (args[0] == "config" && args.size() > 1) {
                yajlpp_parse_context ypc("input", lnav_config_handlers);

                ypc.set_path(args[1])
                    .with_obj(lnav_config);
                ypc.update_callbacks();

                if (ypc.ypc_current_handler != NULL) {
                    const json_path_handler_base *jph = ypc.ypc_current_handler;
                    char help_text[1024];

                    snprintf(help_text, sizeof(help_text),
                             ANSI_BOLD("%s %s") " -- %s    " ABORT_MSG,
                             jph->jph_path,
                             jph->jph_synopsis,
                             jph->jph_description);
                    lnav_data.ld_bottom_source.set_prompt(help_text);
                    lnav_data.ld_bottom_source.grep_error("");
                }
                else {
                    lnav_data.ld_bottom_source.grep_error(
                            "Unknown configuration option: " + args[1]);
                }
            }
            else {
                readline_context::command_t &cmd = iter->second;
                const help_text &ht = cmd.c_help;

                if (ht.ht_name) {
                    textview_curses &dtc = lnav_data.ld_doc_view;
                    textview_curses &etc = lnav_data.ld_example_view;
                    vector<attr_line_t> lines;
                    unsigned long width;
                    vis_line_t height;
                    attr_line_t al;

                    dtc.get_dimensions(height, width);
                    format_help_text_for_term(ht, min(70UL, width), al);
                    al.split_lines(lines);
                    lnav_data.ld_doc_source.replace_with(al);

                    al.clear();
                    lines.clear();
                    etc.get_dimensions(height, width);
                    format_example_text_for_term(ht, width, al);
                    al.split_lines(lines);
                    lnav_data.ld_example_source.replace_with(al);
                }

                lnav_data.ld_bottom_source.grep_error("");
                lnav_data.ld_status[LNS_BOTTOM].window_change();
            }
            break;
        }
        case LNM_EXEC: {
            string line = rc->get_line_buffer();
            size_t name_end = line.find(' ');
            string script_name = line.substr(0, name_end);
            map<string, vector<script_metadata> > &scripts = lnav_data.ld_scripts;
            map<string, vector<script_metadata> >::iterator iter;

            if ((iter = scripts.find(script_name)) == scripts.end() ||
                    iter->second[0].sm_description.empty()) {
                lnav_data.ld_bottom_source.set_prompt(
                        "Enter a script to execute: " ABORT_MSG);
            }
            else {
                struct script_metadata &meta = iter->second[0];
                char help_text[1024];

                snprintf(help_text, sizeof(help_text),
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

static void rl_search_internal(void *dummy, readline_curses *rc, bool complete = false)
{
    textview_curses *tc = lnav_data.ld_view_stack.back();
    string term_val;
    string name;

    tc->get_highlights().erase("$preview");
    tc->get_highlights().erase("$bodypreview");
    tc->reload_data();

    switch (lnav_data.ld_mode) {
    case LNM_SEARCH:
        name = "$search";
        break;

    case LNM_CAPTURE:
        require(0);
        name = "$capture";
        break;

    case LNM_COMMAND: {
        lnav_data.ld_exec_context.ec_dry_run = true;

        lnav_data.ld_preview_status_source.get_description().clear();
        lnav_data.ld_preview_source.clear();
        string result = execute_command(lnav_data.ld_exec_context, rc->get_value());

        if (result.empty()) {
            lnav_data.ld_bottom_source.set_prompt(LNAV_CMD_PROMPT);
            lnav_data.ld_bottom_source.grep_error("");
        } else if (startswith(result, "error:")) {
            lnav_data.ld_bottom_source.set_prompt("");
            lnav_data.ld_bottom_source.grep_error(result);
        } else {
            lnav_data.ld_bottom_source.set_prompt(result);
            lnav_data.ld_bottom_source.grep_error("");
        }

        lnav_data.ld_preview_view.reload_data();

        lnav_data.ld_exec_context.ec_dry_run = false;
        return;
    }

    case LNM_SQL: {
        term_val = trim(rc->get_value() + ";");

        if (term_val.size() > 0 && term_val[0] == '.') {
            lnav_data.ld_bottom_source.grep_error("");
        } else if (!sqlite3_complete(term_val.c_str())) {
            lnav_data.ld_bottom_source.
                grep_error("sql error: incomplete statement");
        } else {
            auto_mem<sqlite3_stmt> stmt(sqlite3_finalize);
            int retcode;

            retcode = sqlite3_prepare_v2(lnav_data.ld_db,
                                         rc->get_value().c_str(),
                                         -1,
                                         stmt.out(),
                                         NULL);
            if (retcode != SQLITE_OK) {
                const char *errmsg = sqlite3_errmsg(lnav_data.ld_db);

                lnav_data.ld_bottom_source.
                    grep_error(string("sql error: ") + string(errmsg));
            } else {
                lnav_data.ld_bottom_source.grep_error("");
            }
        }

        attr_line_t al(rc->get_line_buffer());
        const string_attrs_t &sa = al.get_attrs();
        size_t x = rc->get_x() - 1;
        bool has_doc = false;

        annotate_sql_statement(al);

        if (x > 0 && x >= al.length()) {
            x -= 1;
        }

        while (x > 0 && isspace(al.get_string()[x])) {
            x -= 1;
        }

        auto iter = rfind_string_attr_if(sa, x, [](auto sa) {
            return (sa.sa_type == &SQL_FUNCTION_ATTR ||
                    sa.sa_type == &SQL_KEYWORD_ATTR);
        });

        if (iter != sa.end()) {
            const line_range &lr = iter->sa_range;
            const string &str = al.get_string();
            int lpc;

            for (lpc = lr.lr_start; lpc < lr.lr_end; lpc++) {
                if (!isalnum(str[lpc]) && str[lpc] != '_') {
                    break;
                }
            }

            name = str.substr(lr.lr_start, lpc - lr.lr_start);

            const auto &func_iter = sqlite_function_help.find(tolower(name));

            if (func_iter != sqlite_function_help.end()) {
                textview_curses &dtc = lnav_data.ld_doc_view;
                textview_curses &etc = lnav_data.ld_example_view;
                const help_text &ht = *func_iter->second;
                vector<attr_line_t> lines;
                unsigned long width;
                vis_line_t height;
                attr_line_t al;

                dtc.get_dimensions(height, width);
                format_help_text_for_term(ht, min(70UL, width), al);
                al.split_lines(lines);
                lnav_data.ld_doc_source.replace_with(al);
                dtc.reload_data();

                al.clear();
                lines.clear();
                etc.get_dimensions(height, width);
                format_example_text_for_term(ht, width, al);
                al.split_lines(lines);
                lnav_data.ld_example_source.replace_with(al);
                etc.reload_data();

                has_doc = true;
            }
        }

        auto ident_iter = find_string_attr_containing(sa, &SQL_IDENTIFIER_ATTR, x);
        if (ident_iter != sa.end()) {
            string ident = al.get_substring(ident_iter->sa_range);

            auto vtab = lnav_data.ld_vtab_manager->lookup_impl(
                intern_string::lookup(ident));
            string ddl;

            if (vtab != nullptr) {
                ddl = trim(vtab->get_table_statement());

            } else {
                auto table_ddl_iter = lnav_data.ld_table_ddl.find(ident);

                if (table_ddl_iter != lnav_data.ld_table_ddl.end()) {
                    ddl = table_ddl_iter->second;
                }
            }

            if (!ddl.empty()) {
                lnav_data.ld_preview_source.replace_with(ddl)
                    .set_text_format(TF_SQL)
                    .truncate_to(30);
                lnav_data.ld_preview_status_source.get_description()
                         .set_value("Definition for table -- %s",
                                    ident.c_str());
            }
        }

        if (!has_doc) {
            lnav_data.ld_doc_source.clear();
            lnav_data.ld_example_source.clear();
            lnav_data.ld_preview_source.clear();
        }
        return;
    }

    case LNM_EXEC:
        return;

    default:
        require(0);
        break;
    }

    lnav_view_t      index = (lnav_view_t)(tc - lnav_data.ld_views);

    if (!complete) {
        tc->set_top(lnav_data.ld_search_start_line);
    }
    execute_search(index, rc->get_value());
}

void rl_search(void *dummy, readline_curses *rc)
{
    textview_curses *tc = lnav_data.ld_view_stack.back();

    rl_search_internal(dummy, rc);
    tc->set_follow_search_for(0);
}

void rl_abort(void *dummy, readline_curses *rc)
{
    textview_curses *tc    = lnav_data.ld_view_stack.back();
    lnav_view_t      index = (lnav_view_t)(tc - lnav_data.ld_views);

    lnav_data.ld_bottom_source.set_prompt("");
    lnav_data.ld_example_source.clear();
    lnav_data.ld_doc_source.clear();
    lnav_data.ld_preview_status_source.get_description().clear();
    lnav_data.ld_preview_source.clear();
    tc->get_highlights().erase("$preview");
    tc->get_highlights().erase("$bodypreview");

    lnav_data.ld_bottom_source.grep_error("");
    switch (lnav_data.ld_mode) {
    case LNM_SEARCH:
        tc->set_top(lnav_data.ld_search_start_line);
        execute_search(index, lnav_data.ld_previous_search);
        break;
    case LNM_SQL:
        tc->reload_data();
        break;
    default:
        break;
    }
    lnav_data.ld_rl_view->set_value("");
    lnav_data.ld_mode = LNM_PAGING;
}

void rl_callback(void *dummy, readline_curses *rc)
{
    textview_curses *tc = lnav_data.ld_view_stack.back();
    exec_context &ec = lnav_data.ld_exec_context;
    string alt_msg;

    lnav_data.ld_bottom_source.set_prompt("");
    lnav_data.ld_doc_source.clear();
    lnav_data.ld_example_source.clear();
    lnav_data.ld_preview_status_source.get_description().clear();
    lnav_data.ld_preview_source.clear();
    tc->get_highlights().erase("$preview");
    tc->get_highlights().erase("$bodypreview");
    switch (lnav_data.ld_mode) {
    case LNM_PAGING:
        require(0);
        break;

    case LNM_COMMAND:
        rc->set_alt_value("");
        rc->set_value(execute_command(ec, rc->get_value()));
        break;

    case LNM_SEARCH:
    case LNM_CAPTURE:
        rl_search_internal(dummy, rc, true);
        if (rc->get_value().size() > 0) {
            auto_mem<FILE> pfile(pclose);
            textview_curses *tc = lnav_data.ld_view_stack.back();
            vis_bookmarks &bm = tc->get_bookmarks();
            const auto &bv = bm[&textview_curses::BM_SEARCH];
            vis_line_t vl = bv.next(tc->get_top());

            pfile = open_clipboard(CT_FIND);
            if (pfile.in() != NULL) {
                fprintf(pfile, "%s", rc->get_value().c_str());
            }
            if (vl != -1_vl) {
                tc->set_top(vl);
            } else {
                tc->set_follow_search_for(750);
            }
            rc->set_value("search: " + rc->get_value());
            rc->set_alt_value(HELP_MSG_2(
                                  n, N,
                                  "to move forward/backward through search results"));
        }
        break;

    case LNM_SQL: {
        string result = execute_sql(ec, rc->get_value(), alt_msg);
        db_label_source &dls = lnav_data.ld_db_row_source;

        if (!result.empty()) {
            result = "SQL Result: " + result;
        }

        if (dls.dls_rows.size() > 1) {
            ensure_view(&lnav_data.ld_views[LNV_DB]);
        }

        rc->set_value(result);
        rc->set_alt_value(alt_msg);
        break;
    }

    case LNM_EXEC: {
        auto_mem<FILE> tmpout(fclose);

        tmpout = std::tmpfile();

        if (!tmpout) {
            rc->set_value("Unable to open temporary output file: " + string(strerror(errno)));
        }
        else {
            auto_fd fd(fileno(tmpout));
            auto_fd fd_copy((const auto_fd &) fd);
            char desc[256], timestamp[32];
            time_t current_time = time(NULL);
            string path_and_args = rc->get_value();

	    lnav_data.ld_output_stack.push(tmpout);
	    string result = execute_file(ec, path_and_args);
	    string::size_type lf_index = result.find('\n');
	    if (lf_index != string::npos) {
		result = result.substr(0, lf_index);
	    }
	    rc->set_value(result);
	    lnav_data.ld_output_stack.pop();

            struct stat st;

            if (fstat(fd_copy, &st) != -1 && st.st_size > 0) {
                strftime(timestamp, sizeof(timestamp),
                         "%a %b %d %H:%M:%S %Z",
                         localtime(&current_time));
                snprintf(desc, sizeof(desc),
                         "Output of %s (%s)",
                         path_and_args.c_str(),
                         timestamp);
                lnav_data.ld_file_names[desc]
                    .with_fd(fd_copy)
                    .with_detect_format(false);
                lnav_data.ld_files_to_front.push_back(make_pair(desc, 0));

                if (lnav_data.ld_rl_view != NULL) {
                    lnav_data.ld_rl_view->set_alt_value(HELP_MSG_1(
                                                            X, "to close the file"));
                }
            }
        }
        break;
    }
    }

    lnav_data.ld_mode = LNM_PAGING;
}

void rl_display_matches(void *dummy, readline_curses *rc)
{
    const std::vector<std::string> &matches = rc->get_matches();
    textview_curses &tc = lnav_data.ld_match_view;
    unsigned long width, height;
    int max_len, cols, rows;

    getmaxyx(lnav_data.ld_window, height, width);

    max_len = rc->get_max_match_length() + 2;
    cols = max(1UL, width / max_len);
    rows = (matches.size() + cols - 1) / cols;

    if (matches.empty()) {
        lnav_data.ld_match_source.clear();
    }
    else if (cols == 1) {
        lnav_data.ld_match_source.replace_with(rc->get_matches());
    }
    else {
        std::vector<std::string> horiz_matches;

        horiz_matches.resize(rows);
        for (size_t lpc = 0; lpc < matches.size(); lpc++) {
            int curr_row = lpc % rows;

            horiz_matches[curr_row].append(matches[lpc]);
            horiz_matches[curr_row].append(
                max_len - matches[lpc].length(), ' ');
        }
        lnav_data.ld_match_source.replace_with(horiz_matches);
    }

    tc.reload_data();
}

void rl_display_next(void *dummy, readline_curses *rc)
{
    textview_curses &tc = lnav_data.ld_match_view;

    if (tc.get_top() >= (tc.get_top_for_last_row() - 1)) {
        tc.set_top(0_vl);
    }
    else {
        tc.shift_top(tc.get_height());
    }
}
