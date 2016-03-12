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

#include <wordexp.h>

#include "lnav.hh"
#include "lnav_util.hh"
#include "sysclip.hh"
#include "plain_text_source.hh"
#include "command_executor.hh"
#include "readline_curses.hh"
#include "log_search_table.hh"
#include "log_format_loader.hh"

using namespace std;

#define ABORT_MSG "(Press " ANSI_BOLD("CTRL+]") " to abort)"

void rl_change(void *dummy, readline_curses *rc)
{
    switch (lnav_data.ld_mode) {
        case LNM_COMMAND: {
            string line = rc->get_line_buffer();
            vector<string> args;
            readline_context::command_map_t::iterator iter = lnav_commands.end();

            split_ws(line, args);

            if (!args.empty()) {
                iter = lnav_commands.find(args[0]);
            }
            if (iter == lnav_commands.end() ||
                iter->second.c_description == NULL) {
                lnav_data.ld_bottom_source.set_prompt(
                        "Enter an lnav command: " ABORT_MSG);
                lnav_data.ld_bottom_source.grep_error("");
            }
            else if (args[0] == "config" && args.size() > 1) {
                json_schema::path_to_handler_t::iterator path_iter;

                path_iter = lnav_config_schema.js_path_to_handler.find(args[1]);
                if (path_iter != lnav_config_schema.js_path_to_handler.end()) {
                    const json_path_handler_base *jph = path_iter->second;
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
                char args_text[128] = {0};
                char help_text[1024];

                if (cmd.c_args != NULL && strlen(cmd.c_args) > 0) {
                    snprintf(args_text, sizeof(args_text),
                             " %s",
                             cmd.c_args);
                }
                snprintf(help_text, sizeof(help_text),
                         ANSI_BOLD("%s%s") " -- %s    " ABORT_MSG,
                         cmd.c_name,
                         args_text,
                         cmd.c_description);

                lnav_data.ld_bottom_source.set_prompt(help_text);
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
        }
        default:
            break;
    }
}

static void rl_search_internal(void *dummy, readline_curses *rc, bool complete = false)
{
    string term_val;
    string name;

    switch (lnav_data.ld_mode) {
    case LNM_SEARCH:
        name = "$search";
        break;

    case LNM_CAPTURE:
        require(0);
        name = "$capture";
        break;

    case LNM_COMMAND:
        return;

    case LNM_SQL:
        term_val = trim(rc->get_value() + ";");

        if (term_val.size() > 0 && term_val[0] == '.') {
            lnav_data.ld_bottom_source.grep_error("");
        }
        else if (!sqlite3_complete(term_val.c_str())) {
            lnav_data.ld_bottom_source.
            grep_error("sql error: incomplete statement");
        }
        else {
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
            }
            else {
                lnav_data.ld_bottom_source.grep_error("");
            }
        }
        return;

    case LNM_EXEC:
        return;

    default:
        require(0);
        break;
    }

    textview_curses *tc    = lnav_data.ld_view_stack.top();
    lnav_view_t      index = (lnav_view_t)(tc - lnav_data.ld_views);

    if (!complete) {
        tc->set_top(lnav_data.ld_search_start_line);
    }
    execute_search(index, rc->get_value());
}

void rl_search(void *dummy, readline_curses *rc)
{
    rl_search_internal(dummy, rc);
}

void rl_abort(void *dummy, readline_curses *rc)
{
    textview_curses *tc    = lnav_data.ld_view_stack.top();
    lnav_view_t      index = (lnav_view_t)(tc - lnav_data.ld_views);

    lnav_data.ld_bottom_source.set_prompt("");

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
    string alt_msg;

    lnav_data.ld_bottom_source.set_prompt("");
    switch (lnav_data.ld_mode) {
    case LNM_PAGING:
        require(0);
        break;

    case LNM_COMMAND:
        rc->set_alt_value("");
        rc->set_value(execute_command(rc->get_value()));
        break;

    case LNM_SEARCH:
    case LNM_CAPTURE:
        rl_search_internal(dummy, rc, true);
        if (rc->get_value().size() > 0) {
            auto_mem<FILE> pfile(pclose);

            pfile = open_clipboard(CT_FIND);
            if (pfile.in() != NULL) {
                fprintf(pfile, "%s", rc->get_value().c_str());
            }
            lnav_data.ld_view_stack.top()->set_follow_search(false);
            rc->set_value("search: " + rc->get_value());
            rc->set_alt_value(HELP_MSG_2(
                                  n, N,
                                  "to move forward/backward through search results"));
        }
        break;

    case LNM_SQL:
        rc->set_value(execute_sql(rc->get_value(), alt_msg));
        rc->set_alt_value(alt_msg);
        break;

    case LNM_EXEC: {
        char fn_template[PATH_MAX];
        auto_mem<char> abspath;
        auto_fd fd;

        snprintf(fn_template, sizeof(fn_template),
                 "/%s/lnav-script-out.XXXXXX",
                 getenv("TMPDIR"));
        if ((fd = mkstemp(fn_template)) == -1) {
            rc->set_value("Unable to open temporary output file: " + string(strerror(errno)));
        }
        else if ((abspath = realpath(fn_template, NULL)) == NULL) {
            rc->set_value("Unable to get real path to temporary file");
        }
        else {
            auto_fd fd_copy((const auto_fd &) fd);
            char desc[256], timestamp[32];
            time_t current_time = time(NULL);
            string path_and_args = rc->get_value();

            {
                auto_mem<FILE> tmpout(fclose);

                if ((tmpout = fdopen(fd, "w+")) != NULL) {
                    lnav_data.ld_output_stack.push(tmpout);
                    string result = execute_file(path_and_args);
                    string::size_type lf_index = result.find('\n');
                    if (lf_index != string::npos) {
                        result = result.substr(0, lf_index);
                    }
                    rc->set_value(result);
                    lnav_data.ld_output_stack.pop();
                }
            }

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

            remove(abspath.in());
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
    int max_len, cols, rows, match_height, bottom_height;

    getmaxyx(lnav_data.ld_window, height, width);

    max_len = rc->get_max_match_length() + 2;
    cols = max(1UL, width / max_len);
    rows = (matches.size() + cols - 1) / cols;

    match_height = min((unsigned long)rows, (height - 4) / 2);
    bottom_height = match_height + 1 + rc->get_height();

    for (int lpc = 0; lpc < LNV__MAX; lpc++) {
        lnav_data.ld_views[lpc].set_height(vis_line_t(-bottom_height));
    }
    lnav_data.ld_status[LNS_BOTTOM].set_top(-bottom_height);

    delete tc.get_sub_source();

    if (cols == 1) {
        tc.set_sub_source(new plain_text_source(rc->get_matches()));
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
        tc.set_sub_source(new plain_text_source(horiz_matches));
    }

    if (match_height > 0) {
        tc.set_window(lnav_data.ld_window);
        tc.set_y(height - bottom_height + 1);
        tc.set_height(vis_line_t(match_height));
        tc.reload_data();
    }
    else {
        tc.set_window(NULL);
    }
}

void rl_display_next(void *dummy, readline_curses *rc)
{
    textview_curses &tc = lnav_data.ld_match_view;

    if (tc.get_top() >= (tc.get_top_for_last_row() - 1)) {
        tc.set_top(vis_line_t(0));
    }
    else {
        tc.shift_top(tc.get_height());
    }
}