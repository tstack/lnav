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

#include <vector>

#include "json_ptr.hh"
#include "pcrecpp.h"
#include "lnav.hh"
#include "log_format_loader.hh"

#include "command_executor.hh"

using namespace std;

static const string MSG_FORMAT_STMT =
        "SELECT count(*) as total, min(log_line) as log_line, log_msg_format "
                "FROM all_logs GROUP BY log_msg_format ORDER BY total desc";

static int sql_progress(const struct log_cursor &lc)
{
    static sig_atomic_t sql_counter = 0;

    size_t total = lnav_data.ld_log_source.text_line_count();
    off_t  off   = lc.lc_curr_line;

    if (lnav_data.ld_window == NULL) {
        return 0;
    }

    if (!lnav_data.ld_looping) {
        return 1;
    }

    if (ui_periodic_timer::singleton().time_to_update(sql_counter)) {
        lnav_data.ld_bottom_source.update_loading(off, total);
        lnav_data.ld_top_source.update_time();
        lnav_data.ld_status[LNS_TOP].do_update();
        lnav_data.ld_status[LNS_BOTTOM].do_update();
        refresh();
    }

    return 0;
}

string execute_from_file(const string &path, int line_number, char mode, const string &cmdline);

string execute_command(const string &cmdline)
{
    stringstream ss(cmdline);

    vector<string> args;
    string         buf, msg;

    log_info("Executing: %s", cmdline.c_str());

    while (ss >> buf) {
        args.push_back(buf);
    }

    if (args.size() > 0) {
        readline_context::command_map_t::iterator iter;

        if ((iter = lnav_commands.find(args[0])) ==
            lnav_commands.end()) {
            msg = "error: unknown command - " + args[0];
        }
        else {
            msg = iter->second.c_func(cmdline, args);
        }
    }

    return msg;
}

string execute_sql(const string &sql, string &alt_msg)
{
    db_label_source &dls = lnav_data.ld_db_row_source;
    auto_mem<sqlite3_stmt> stmt(sqlite3_finalize);
    string stmt_str = trim(sql);
    string retval;
    int retcode;

    log_info("Executing SQL: %s", sql.c_str());

    lnav_data.ld_bottom_source.grep_error("");

    if (stmt_str == ".schema") {
        alt_msg = "";

        ensure_view(&lnav_data.ld_views[LNV_SCHEMA]);

        lnav_data.ld_mode = LNM_PAGING;
        return "";
    }
    else if (stmt_str == ".msgformats") {
        stmt_str = MSG_FORMAT_STMT;
    }

    dls.clear();
    sql_progress_guard progress_guard(sql_progress);
    retcode = sqlite3_prepare_v2(lnav_data.ld_db.in(),
       stmt_str.c_str(),
       -1,
       stmt.out(),
       NULL);
    if (retcode != SQLITE_OK) {
        const char *errmsg = sqlite3_errmsg(lnav_data.ld_db);

        retval = "error: " + string(errmsg);
        alt_msg = "";
    }
    else if (stmt == NULL) {
        retval = "";
        alt_msg = "";
    }
    else {
        bool done = false;
        int param_count;

        param_count = sqlite3_bind_parameter_count(stmt.in());
        for (int lpc = 0; lpc < param_count; lpc++) {
            const char *name;

            name = sqlite3_bind_parameter_name(stmt.in(), lpc + 1);
            if (name[0] == '$') {
                map<string, string> &vars = lnav_data.ld_local_vars.top();
                map<string, string>::iterator local_var;
                const char *env_value;

                if ((local_var = vars.find(&name[1])) != vars.end()) {
                    sqlite3_bind_text(stmt.in(), lpc + 1,
                                      local_var->second.c_str(), -1,
                                      SQLITE_TRANSIENT);
                }
                else if ((env_value = getenv(&name[1])) != NULL) {
                    sqlite3_bind_text(stmt.in(), lpc + 1, env_value, -1, SQLITE_STATIC);
                }
            }
        }

        if (lnav_data.ld_rl_view != NULL) {
            lnav_data.ld_rl_view->set_value("Executing query: " + sql + " ...");
            lnav_data.ld_rl_view->do_update();
        }

        lnav_data.ld_log_source.text_clear_marks(&BM_QUERY);
        while (!done) {
            retcode = sqlite3_step(stmt.in());

            switch (retcode) {
            case SQLITE_OK:
            case SQLITE_DONE:
                done = true;
                break;

            case SQLITE_ROW:
                sql_callback(stmt.in());
                break;

            default: {
                const char *errmsg;

                log_error("sqlite3_step error code: %d", retcode);
                errmsg = sqlite3_errmsg(lnav_data.ld_db);
                retval = "error: " + string(errmsg);
                done = true;
            }
                break;
            }
        }
    }

    if (retcode == SQLITE_DONE) {
        lnav_data.ld_views[LNV_LOG].reload_data();
        lnav_data.ld_views[LNV_DB].reload_data();
        lnav_data.ld_views[LNV_DB].set_left(0);

        if (dls.dls_rows.size() > 0) {
            vis_bookmarks &bm =
            lnav_data.ld_views[LNV_LOG].get_bookmarks();

            if (!(lnav_data.ld_flags & LNF_HEADLESS) &&
                    dls.dls_headers.size() == 1 && !bm[&BM_QUERY].empty()) {
                retval = "";
                alt_msg = HELP_MSG_2(
                  y, Y,
                  "to move forward/backward through query results "
                  "in the log view");
            }
            else if (!(lnav_data.ld_flags & LNF_HEADLESS) &&
                    dls.dls_rows.size() == 1) {
                string row;

                dls.text_value_for_line(lnav_data.ld_views[LNV_DB], 1, row, true);
                retval = "SQL Result: " + row;
            }
            else {
                char row_count[32];

                ensure_view(&lnav_data.ld_views[LNV_DB]);
                snprintf(row_count, sizeof(row_count),
                   ANSI_BOLD("%'d") " row(s) matched",
                   (int)dls.dls_rows.size());
                retval = row_count;
                alt_msg = HELP_MSG_2(
                  y, Y,
                  "to move forward/backward through query results "
                  "in the log view");
            }
        }
#ifdef HAVE_SQLITE3_STMT_READONLY
        else if (sqlite3_stmt_readonly(stmt.in())) {
            retval = "No rows matched";
            alt_msg = "";
        }
#endif
    }

    if (!(lnav_data.ld_flags & LNF_HEADLESS)) {
        lnav_data.ld_bottom_source.update_loading(0, 0);
        lnav_data.ld_status[LNS_BOTTOM].do_update();
        redo_search(LNV_DB);
    }
    lnav_data.ld_views[LNV_LOG].reload_data();

    return retval;
}

static string execute_file_contents(const string &path, bool multiline)
{
    string retval;
    FILE *file;

    if (path == "-") {
        file = stdin;
    }
    else if ((file = fopen(path.c_str(), "r")) == NULL) {
        return "error: unable to open file";
    }

    int    line_number = 0, starting_line_number = 0;
    char *line = NULL;
    size_t line_max_size;
    ssize_t line_size;
    string cmdline;
    char mode = '\0';
    pair<string, string> dir_and_base = split_path(path);

    lnav_data.ld_path_stack.push(dir_and_base.first);
    while ((line_size = getline(&line, &line_max_size, file)) != -1) {
        line_number += 1;

        if (trim(line).empty()) {
            continue;
        }
        if (line[0] == '#') {
            continue;
        }

        switch (line[0]) {
            case ':':
            case '/':
            case ';':
            case '|':
                if (mode) {
                    retval = execute_from_file(path, starting_line_number, mode, trim(cmdline));
                }

                starting_line_number = line_number;
                mode = line[0];
                cmdline = string(&line[1]);
                break;
            default:
                if (multiline) {
                    cmdline += line;
                }
                else {
                    retval = execute_from_file(path, line_number, ':', line);
                }
                break;
        }

    }

    if (mode) {
        retval = execute_from_file(path, starting_line_number, mode, trim(cmdline));
    }

    if (file != stdin) {
        fclose(file);
    }
    lnav_data.ld_path_stack.pop();

    return retval;
}

string execute_file(const string &path_and_args, bool multiline)
{
    map<string, vector<string> > scripts;
    map<string, vector<string> >::iterator iter;
    static_root_mem<wordexp_t, wordfree> wordmem;
    string msg, retval;

    log_info("Executing file: %s", path_and_args.c_str());

    int exp_rc = wordexp(path_and_args.c_str(),
                         wordmem.inout(),
                         WRDE_NOCMD | WRDE_UNDEF);

    if (!wordexperr(exp_rc, msg)) {
        retval = msg;
    }
    else if (wordmem->we_wordc == 0) {
        retval = "error: no script specified";
    }
    else {
        lnav_data.ld_local_vars.push(map<string, string>());

        string script_name = wordmem->we_wordv[0];
        map<string, string> &vars = lnav_data.ld_local_vars.top();
        char env_arg_name[32];
        string result, open_error = "file not found";

        snprintf(env_arg_name, sizeof(env_arg_name), "%d", (int) wordmem->we_wordc - 1);

        vars["#"] = env_arg_name;
        for (unsigned int lpc = 0; lpc < wordmem->we_wordc; lpc++) {
            snprintf(env_arg_name, sizeof(env_arg_name), "%d", lpc);
            vars[env_arg_name] = wordmem->we_wordv[lpc];
        }

        vector<string> paths_to_exec;

        find_format_scripts(lnav_data.ld_config_paths, scripts);
        if ((iter = scripts.find(script_name)) != scripts.end()) {
            paths_to_exec = iter->second;
        }
        if (access(script_name.c_str(), R_OK) == 0) {
            paths_to_exec.push_back(script_name);
        }
        else if (errno != ENOENT) {
            open_error = strerror(errno);
        }
        else {
            string local_path = lnav_data.ld_path_stack.top() + "/" + script_name;

            if (access(local_path.c_str(), R_OK) == 0) {
                paths_to_exec.push_back(local_path);
            }
            else if (errno != ENOENT) {
                open_error = strerror(errno);
            }
        }

        if (!paths_to_exec.empty()) {
            for (vector<string>::iterator path_iter = paths_to_exec.begin();
                 path_iter != paths_to_exec.end();
                 ++path_iter) {
                result = execute_file_contents(*path_iter, multiline);
            }
            retval = "Executed: " + script_name + " -- " + result;
        }
        else {
            retval = "error: unknown script -- " + script_name + " -- " + open_error;
        }
        lnav_data.ld_local_vars.pop();
    }

    return retval;
}

string execute_from_file(const string &path, int line_number, char mode, const string &cmdline)
{
    string retval, alt_msg;

    switch (mode) {
        case ':':
            retval = execute_command(cmdline);
            break;
        case '/':
        case ';':
            setup_logline_table();
            retval = execute_sql(cmdline, alt_msg);
            break;
        case '|':
            retval = execute_file(cmdline);
            break;
        default:
            retval = execute_command(cmdline);
            break;
    }

    if (rescan_files()) {
        rebuild_indexes(true);
    }

    log_info("%s:%d:execute result -- %s",
            path.c_str(),
            line_number,
            retval.c_str());

    return retval;
}

void execute_init_commands(vector<pair<string, string> > &msgs)
{
    if (lnav_data.ld_cmd_init_done) {
        return;
    }

    for (list<std::string>::iterator iter = lnav_data.ld_commands.begin();
         iter != lnav_data.ld_commands.end();
         ++iter) {
        string msg, alt_msg;

        switch (iter->at(0)) {
        case ':':
            msg = execute_command(iter->substr(1));
            break;
        case '/':
        case ';':
            setup_logline_table();
            msg = execute_sql(iter->substr(1), alt_msg);
            break;
        case '|':
            msg = execute_file(iter->substr(1));
            break;
        }

        msgs.push_back(make_pair(msg, alt_msg));

        if (rescan_files()) {
            rebuild_indexes(true);
        }
    }
    lnav_data.ld_commands.clear();

    if (!lnav_data.ld_pt_search.empty()) {
#ifdef HAVE_LIBCURL
        auto_ptr<papertrail_proc> pt(new papertrail_proc(
                lnav_data.ld_pt_search.substr(3),
                lnav_data.ld_pt_min_time,
                lnav_data.ld_pt_max_time));
        lnav_data.ld_file_names.insert(
                make_pair(lnav_data.ld_pt_search, pt->copy_fd().release()));
        lnav_data.ld_curl_looper.add_request(pt.release());
#endif
    }

    lnav_data.ld_cmd_init_done = true;
}

int sql_callback(sqlite3_stmt *stmt)
{
    logfile_sub_source &lss = lnav_data.ld_log_source;
    db_label_source &dls = lnav_data.ld_db_row_source;
    stacked_bar_chart<std::string> &chart = dls.dls_chart;
    view_colors &vc = view_colors::singleton();
    int ncols = sqlite3_column_count(stmt);
    int row_number;
    int lpc, retval = 0;

    row_number = dls.dls_rows.size();
    dls.dls_rows.resize(row_number + 1);
    if (dls.dls_headers.empty()) {
        for (lpc = 0; lpc < ncols; lpc++) {
            int    type    = sqlite3_column_type(stmt, lpc);
            string colname = sqlite3_column_name(stmt, lpc);
            bool   graphable;

            graphable = ((type == SQLITE_INTEGER || type == SQLITE_FLOAT) &&
                         !binary_search(lnav_data.ld_db_key_names.begin(),
                                        lnav_data.ld_db_key_names.end(),
                                        colname));

            dls.push_header(colname, type, graphable);
            if (graphable) {
                int attrs = vc.attrs_for_ident(colname);
                chart.with_attrs_for_ident(colname, attrs);
            }
        }
    }
    for (lpc = 0; lpc < ncols; lpc++) {
        const char *value     = (const char *)sqlite3_column_text(stmt, lpc);
        double      num_value = 0.0;
        size_t value_len;

        dls.push_column(value);
        if (value == NULL) {
            value_len = 0;
        }
        else {
            value_len = strlen(value);
        }
        if (value != NULL &&
            (dls.dls_headers[lpc] == "log_line" ||
             dls.dls_headers[lpc] == "min(log_line)")) {
            int line_number = -1;

            if (sscanf(value, "%d", &line_number) == 1) {
                lss.text_mark(&BM_QUERY, line_number, true);
            }
        }
        if (value != NULL && dls.dls_headers_to_graph[lpc]) {
            if (sscanf(value, "%lf", &num_value) != 1) {
                num_value = 0.0;
            }
            chart.add_value(dls.dls_headers[lpc], num_value);
        }
        else if (value_len > 2 &&
                 ((value[0] == '{' && value[value_len - 1] == '}') ||
                  (value[0] == '[' && value[value_len - 1] == ']'))) {
            json_ptr_walk jpw;

            if (jpw.parse(value, value_len) == yajl_status_ok &&
                jpw.complete_parse() == yajl_status_ok) {
                for (json_ptr_walk::walk_list_t::iterator iter = jpw.jpw_values.begin();
                     iter != jpw.jpw_values.end();
                     ++iter) {
                    if (iter->wt_type == yajl_t_number &&
                        sscanf(iter->wt_value.c_str(), "%lf", &num_value) == 1) {
                        chart.add_value(iter->wt_ptr, num_value);
                        chart.with_attrs_for_ident(iter->wt_ptr, vc.attrs_for_ident(iter->wt_ptr));
                    }
                }
            }
        }
    }

    return retval;
}
