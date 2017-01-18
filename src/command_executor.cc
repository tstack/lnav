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
#include "shlex.hh"

#include "command_executor.hh"
#include "db_sub_source.hh"

using namespace std;

exec_context INIT_EXEC_CONTEXT;

static const string MSG_FORMAT_STMT =
        "SELECT count(*) as total, min(log_line) as log_line, log_msg_format "
                "FROM all_logs GROUP BY log_msg_format ORDER BY total desc";

int sql_progress(const struct log_cursor &lc)
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

string execute_from_file(exec_context &ec, const string &path, int line_number, char mode, const string &cmdline);

string execute_command(exec_context &ec, const string &cmdline)
{
    vector<string> args;
    string         msg;

    log_info("Executing: %s", cmdline.c_str());

    split_ws(cmdline, args);

    if (args.size() > 0) {
        readline_context::command_map_t::iterator iter;

        if ((iter = lnav_commands.find(args[0])) ==
            lnav_commands.end()) {
            msg = "error: unknown command - " + args[0];
        }
        else {
            msg = iter->second.c_func(ec, cmdline, args);
        }
    }

    return msg;
}

string execute_sql(exec_context &ec, const string &sql, string &alt_msg)
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
            map<string, string>::iterator ov_iter;
            const char *name;

            name = sqlite3_bind_parameter_name(stmt.in(), lpc + 1);
            ov_iter = ec.ec_override.find(name);
            if (ov_iter != ec.ec_override.end()) {
                sqlite3_bind_text(stmt.in(),
                                  lpc,
                                  ov_iter->second.c_str(),
                                  ov_iter->second.length(),
                                  SQLITE_TRANSIENT);
            }
            else if (name[0] == '$') {
                map<string, string> &vars = ec.ec_local_vars.top();
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
            else if (name[0] == ':' && ec.ec_line_values != NULL) {
                vector<logline_value> &lvalues = *ec.ec_line_values;
                vector<logline_value>::iterator iter;

                for (iter = lvalues.begin(); iter != lvalues.end(); ++iter) {
                    if (strcmp(&name[1], iter->lv_name.get()) != 0) {
                        continue;
                    }
                    switch (iter->lv_kind) {
                        case logline_value::VALUE_BOOLEAN:
                            sqlite3_bind_int64(stmt.in(), lpc + 1, iter->lv_value.i);
                            break;
                        case logline_value::VALUE_FLOAT:
                            sqlite3_bind_double(stmt.in(), lpc + 1, iter->lv_value.d);
                            break;
                        case logline_value::VALUE_INTEGER:
                            sqlite3_bind_int64(stmt.in(), lpc + 1, iter->lv_value.i);
                            break;
                        case logline_value::VALUE_NULL:
                            sqlite3_bind_null(stmt.in(), lpc + 1);
                            break;
                        default:
                            sqlite3_bind_text(stmt.in(),
                                              lpc + 1,
                                              iter->text_value(),
                                              iter->text_length(),
                                              SQLITE_TRANSIENT);
                            break;
                    }
                }
            }
            else {
                log_warning("Could not bind variable: %s", name);
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
                ec.ec_sql_callback(ec, stmt.in());
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

        if (!ec.ec_accumulator.empty()) {
            retval = ec.ec_accumulator;
            ec.ec_accumulator.clear();
        }
        else if (dls.dls_rows.size() > 0) {
            vis_bookmarks &bm = lnav_data.ld_views[LNV_LOG].get_bookmarks();

            if (lnav_data.ld_flags & LNF_HEADLESS) {
                if (ec.ec_local_vars.size() == 1) {
                    ensure_view(&lnav_data.ld_views[LNV_DB]);
                }

                retval = "";
                alt_msg = "";
            }
            else if (dls.dls_headers.size() == 1 && !bm[&BM_QUERY].empty()) {
                retval = "";
                alt_msg = HELP_MSG_2(
                  y, Y,
                  "to move forward/backward through query results "
                  "in the log view");
            }
            else if (dls.dls_rows.size() == 1) {
                string row;

                dls.text_value_for_line(lnav_data.ld_views[LNV_DB], 0, row, true);
                retval = row;
            }
            else {
                char row_count[32];

                if (ec.ec_local_vars.size() == 1) {
                    ensure_view(&lnav_data.ld_views[LNV_DB]);
                }
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

static string execute_file_contents(exec_context &ec, const string &path, bool multiline)
{
    string retval;
    FILE *file;

    if (path == "-" || path == "/dev/stdin") {
        if (isatty(STDIN_FILENO)) {
            return "error: stdin has already been consumed";
        }
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

    ec.ec_path_stack.push(dir_and_base.first);
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
                    retval = execute_from_file(ec, path, starting_line_number, mode, trim(cmdline));
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
                    retval = execute_from_file(ec, path, line_number, ':', line);
                }
                break;
        }

    }

    if (mode) {
        retval = execute_from_file(ec, path, starting_line_number, mode, trim(cmdline));
    }

    if (file == stdin) {
        if (isatty(STDOUT_FILENO)) {
            log_perror(dup2(STDOUT_FILENO, STDIN_FILENO));
        }
    } else {
        fclose(file);
    }
    ec.ec_path_stack.pop();

    return retval;
}

string execute_file(exec_context &ec, const string &path_and_args, bool multiline)
{
    map<string, vector<script_metadata> > scripts;
    map<string, vector<script_metadata> >::iterator iter;
    vector<string> split_args;
    string msg, retval;
    shlex lexer(path_and_args);

    log_info("Executing file: %s", path_and_args.c_str());

    if (!lexer.split(split_args, ec.ec_local_vars.top())) {
        retval = "error: unable to parse path";
    }
    else if (split_args.empty()) {
        retval = "error: no script specified";
    }
    else {
        ec.ec_local_vars.push(map<string, string>());

        string script_name = split_args[0];
        map<string, string> &vars = ec.ec_local_vars.top();
        char env_arg_name[32];
        string result, open_error = "file not found";

        add_ansi_vars(vars);

        snprintf(env_arg_name, sizeof(env_arg_name), "%d", (int) split_args.size() - 1);

        vars["#"] = env_arg_name;
        for (unsigned int lpc = 0; lpc < split_args.size(); lpc++) {
            snprintf(env_arg_name, sizeof(env_arg_name), "%d", lpc);
            vars[env_arg_name] = split_args[lpc];
        }

        vector<script_metadata> paths_to_exec;

        find_format_scripts(lnav_data.ld_config_paths, scripts);
        if ((iter = scripts.find(script_name)) != scripts.end()) {
            paths_to_exec = iter->second;
        }
        if (script_name == "-" || script_name == "/dev/stdin") {
            paths_to_exec.push_back({script_name});
        }
        else if (access(script_name.c_str(), R_OK) == 0) {
            struct script_metadata meta;

            meta.sm_path = script_name;
            extract_metadata_from_file(meta);
            paths_to_exec.push_back(meta);
        }
        else if (errno != ENOENT) {
            open_error = strerror(errno);
        }
        else {
            string local_path = ec.ec_path_stack.top() + "/" + script_name;

            if (access(local_path.c_str(), R_OK) == 0) {
                struct script_metadata meta;

                meta.sm_path = local_path;
                extract_metadata_from_file(meta);
                paths_to_exec.push_back(meta);
            }
            else if (errno != ENOENT) {
                open_error = strerror(errno);
            }
        }

        if (!paths_to_exec.empty()) {
            for (vector<script_metadata>::iterator path_iter = paths_to_exec.begin();
                 path_iter != paths_to_exec.end();
                 ++path_iter) {
                result = execute_file_contents(ec, path_iter->sm_path, multiline);
            }
            retval = result;
        }
        else {
            retval = "error: unknown script -- " + script_name + " -- " + open_error;
        }
        ec.ec_local_vars.pop();
    }

    return retval;
}

string execute_from_file(exec_context &ec, const string &path, int line_number, char mode, const string &cmdline)
{
    string retval, alt_msg;

    switch (mode) {
        case ':':
            retval = execute_command(ec, cmdline);
            break;
        case '/':
        case ';':
            setup_logline_table();
            retval = execute_sql(ec, cmdline, alt_msg);
            break;
        case '|':
            retval = execute_file(ec, cmdline);
            break;
        default:
            retval = execute_command(ec, cmdline);
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

string execute_any(exec_context &ec, const string &cmdline_with_mode)
{
    string retval, alt_msg, cmdline = cmdline_with_mode.substr(1);

    switch (cmdline_with_mode[0]) {
        case ':':
            retval = execute_command(ec, cmdline);
            break;
        case '/':
        case ';':
            setup_logline_table();
            retval = execute_sql(ec, cmdline, alt_msg);
            break;
        case '|': {
            retval = execute_file(ec, cmdline);
            break;
        }
        default:
            retval = execute_command(ec, cmdline);
            break;
    }

    if (rescan_files()) {
        rebuild_indexes(true);
    }

    return retval;
}

void execute_init_commands(exec_context &ec, vector<pair<string, string> > &msgs)
{
    if (lnav_data.ld_cmd_init_done) {
        return;
    }

    log_info("Executing initial commands");
    for (auto &cmd : lnav_data.ld_commands) {
        string msg, alt_msg;

        switch (cmd.at(0)) {
        case ':':
            msg = execute_command(ec, cmd.substr(1));
            break;
        case '/':
        case ';':
            setup_logline_table();
            msg = execute_sql(ec, cmd.substr(1), alt_msg);
            break;
        case '|':
            msg = execute_file(ec, cmd.substr(1));
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
        unique_ptr<papertrail_proc> pt(new papertrail_proc(
                lnav_data.ld_pt_search.substr(3),
                lnav_data.ld_pt_min_time,
                lnav_data.ld_pt_max_time));
        lnav_data.ld_file_names[lnav_data.ld_pt_search]
            .with_fd(pt->copy_fd());
        lnav_data.ld_curl_looper.add_request(pt.release());
#endif
    }

    lnav_data.ld_cmd_init_done = true;
}

int sql_callback(exec_context &ec, sqlite3_stmt *stmt)
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

        dls.push_column(value);
        if (value != NULL &&
            (dls.dls_headers[lpc].hm_name == "log_line" ||
             dls.dls_headers[lpc].hm_name == "min(log_line)")) {
            int line_number = -1;

            if (sscanf(value, "%d", &line_number) == 1) {
                lss.text_mark(&BM_QUERY, line_number, true);
            }
        }
    }

    return retval;
}

future<string> pipe_callback(exec_context &ec, const string &cmdline, auto_fd &fd)
{
    auto pp = make_shared<piper_proc>(fd, false);
    static int exec_count = 0;
    char desc[128];

    lnav_data.ld_pipers.push_back(pp);
    snprintf(desc,
             sizeof(desc), "[%d] Output of %s",
             exec_count++,
             cmdline.c_str());
    lnav_data.ld_file_names[desc]
        .with_fd(pp->get_fd())
        .with_detect_format(false);
    lnav_data.ld_files_to_front.push_back(make_pair(desc, 0));
    if (lnav_data.ld_rl_view != NULL) {
        lnav_data.ld_rl_view->set_alt_value(HELP_MSG_1(
                                                X, "to close the file"));
    }

    packaged_task<string()> task([]() { return ""; });

    task();

    return task.get_future();
}
