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

#include <vector>

#include "json_ptr.hh"
#include "pcrecpp.h"
#include "lnav.hh"
#include "log_format_loader.hh"
#include "shlex.hh"
#include "sql_util.hh"

#include "command_executor.hh"
#include "db_sub_source.hh"
#include "papertrail_proc.hh"

using namespace std;

exec_context INIT_EXEC_CONTEXT;

bookmark_type_t BM_QUERY("query");

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
            msg = ec.get_error_prefix() + "unknown command - " + args[0];
        }
        else {
            msg = iter->second->c_func(ec, cmdline, args);
        }
    }

    return msg;
}

string execute_sql(exec_context &ec, const string &sql, string &alt_msg)
{
    db_label_source &dls = lnav_data.ld_db_row_source;
    auto_mem<sqlite3_stmt> stmt(sqlite3_finalize);
    struct timeval start_tv, end_tv;
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

    ec.ec_accumulator.clear();

    pair<string, int> source = ec.ec_source.top();
    sql_progress_guard progress_guard(sql_progress,
                                      source.first,
                                      source.second);
    gettimeofday(&start_tv, NULL);
    retcode = sqlite3_prepare_v2(lnav_data.ld_db.in(),
       stmt_str.c_str(),
       -1,
       stmt.out(),
       NULL);
    if (retcode != SQLITE_OK) {
        const char *errmsg = sqlite3_errmsg(lnav_data.ld_db);

        retval = ec.get_error_prefix() + string(errmsg);
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
                map<string, string> &lvars = ec.ec_local_vars.top();
                map<string, string> &gvars = ec.ec_global_vars;
                map<string, string>::iterator local_var, global_var;
                const char *env_value;

                if ((local_var = lvars.find(&name[1])) != lvars.end()) {
                    sqlite3_bind_text(stmt.in(), lpc + 1,
                                      local_var->second.c_str(), -1,
                                      SQLITE_TRANSIENT);
                }
                else if ((global_var = gvars.find(&name[1])) != gvars.end()) {
                    sqlite3_bind_text(stmt.in(), lpc + 1,
                                      global_var->second.c_str(), -1,
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
                sqlite3_bind_null(stmt.in(), lpc + 1);
                log_warning("Could not bind variable: %s", name);
            }
        }

        if (lnav_data.ld_rl_view != NULL) {
            lnav_data.ld_rl_view->set_value("Executing query: " + sql + " ...");
        }

        ec.ec_sql_callback(ec, stmt.in());
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
                retval = ec.get_error_prefix() + string(errmsg);
                done = true;
            }
                break;
            }
        }

        if (!dls.dls_rows.empty() && !ec.ec_local_vars.empty() &&
            !ec.ec_dry_run) {
            auto &vars = ec.ec_local_vars.top();

            for (unsigned int lpc = 0; lpc < dls.dls_headers.size(); lpc++) {
                const string &column_name = dls.dls_headers[lpc].hm_name;

                if (sql_ident_needs_quote(column_name.c_str())) {
                    continue;
                }

                vars[column_name] = dls.dls_rows[0][lpc];
            }
        }

        if (lnav_data.ld_rl_view != nullptr) {
            lnav_data.ld_rl_view->set_value("");
        }
    }

    gettimeofday(&end_tv, NULL);
    if (retcode == SQLITE_DONE) {
        lnav_data.ld_filter_view.reload_data();
        lnav_data.ld_views[LNV_DB].reload_data();
        lnav_data.ld_views[LNV_DB].set_left(0);

        if (!ec.ec_accumulator.empty()) {
            retval = ec.ec_accumulator.get_string();
        }
        else if (!dls.dls_rows.empty()) {
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
                auto &row = dls.dls_rows[0];

                if (dls.dls_headers.size() == 1) {
                    retval = row[0];
                } else {
                    for (unsigned int lpc = 0; lpc < dls.dls_headers.size(); lpc++) {
                        if (lpc > 0) {
                            retval.append("; ");
                        }
                        retval.append(dls.dls_headers[lpc].hm_name);
                        retval.push_back('=');
                        retval.append(row[lpc]);
                    }
                }
            }
            else {
                int row_count = dls.dls_rows.size();
                char row_count_buf[128];
                struct timeval diff_tv;

                timersub(&end_tv, &start_tv, &diff_tv);
                snprintf(row_count_buf, sizeof(row_count_buf),
                         ANSI_BOLD("%'d") " row%s matched in "
                         ANSI_BOLD("%ld.%03ld") " seconds",
                         row_count,
                         row_count == 1 ? "" : "s",
                         diff_tv.tv_sec,
                         std::max((long) diff_tv.tv_usec / 1000, 1L));
                retval = row_count_buf;
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

            if (lnav_data.ld_flags & LNF_HEADLESS) {
                if (ec.ec_local_vars.size() == 1) {
                    ensure_view(&lnav_data.ld_views[LNV_DB]);
                }
            }
        }
#endif
    }

    if (!(lnav_data.ld_flags & LNF_HEADLESS)) {
        lnav_data.ld_bottom_source.update_loading(0, 0);
        lnav_data.ld_status[LNS_BOTTOM].do_update();
        lnav_data.ld_views[LNV_DB].redo_search();
    }

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
    else if ((file = fopen(path.c_str(), "r")) == nullptr) {
        return ec.get_error_prefix() + "unable to open file";
    }

    int    line_number = 0, starting_line_number = 0;
    char *line = nullptr;
    size_t line_max_size;
    ssize_t line_size;
    string cmdline;
    char mode = '\0';
    pair<string, string> dir_and_base = split_path(path);

    ec.ec_path_stack.push_back(dir_and_base.first);
    ec.ec_output_stack.emplace_back(nonstd::nullopt);
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
    ec.ec_output_stack.pop_back();
    ec.ec_path_stack.pop_back();

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
        retval = ec.get_error_prefix() + "unable to parse path";
    }
    else if (split_args.empty()) {
        retval = ec.get_error_prefix() + "no script specified";
    }
    else {
        ec.ec_local_vars.push(map<string, string>());

        string script_name = split_args[0];
        map<string, string> &vars = ec.ec_local_vars.top();
        char env_arg_name[32];
        string star, result, open_error = "file not found";

        add_ansi_vars(vars);

        snprintf(env_arg_name, sizeof(env_arg_name), "%d", (int) split_args.size() - 1);

        vars["#"] = env_arg_name;
        for (size_t lpc = 0; lpc < split_args.size(); lpc++) {
            snprintf(env_arg_name, sizeof(env_arg_name), "%lu", lpc);
            vars[env_arg_name] = split_args[lpc];
        }
        for (size_t lpc = 1; lpc < split_args.size(); lpc++) {
            if (lpc > 1) {
                star.append(" ");
            }
            star.append(split_args[lpc]);
        }
        vars["__all__"] = star;

        vector<script_metadata> paths_to_exec;
        map<string, const char *>::iterator internal_iter;

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
            string local_path = ec.ec_path_stack.back() + "/" + script_name;

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
            for (auto &path_iter : paths_to_exec) {
                result = execute_file_contents(ec, path_iter.sm_path, multiline);
            }
            retval = result;
        }
        else {
            retval = ec.get_error_prefix()
                + "unknown script -- " + script_name + " -- " + open_error;
        }
        ec.ec_local_vars.pop();
    }

    return retval;
}

string execute_from_file(exec_context &ec, const string &path, int line_number, char mode, const string &cmdline)
{
    string retval, alt_msg;

    ec.ec_source.emplace(path, line_number);
    switch (mode) {
        case ':':
            retval = execute_command(ec, cmdline);
            break;
        case '/':
            lnav_data.ld_view_stack.top() | [cmdline] (auto tc) {
                tc->execute_search(cmdline.substr(1));
            };
            break;
        case ';':
            setup_logline_table(ec);
            retval = execute_sql(ec, cmdline, alt_msg);
            break;
        case '|':
            retval = execute_file(ec, cmdline);
            break;
        default:
            retval = execute_command(ec, cmdline);
            break;
    }

    rescan_files();
    rebuild_indexes();

    log_info("%s:%d:execute result -- %s",
            path.c_str(),
            line_number,
            retval.c_str());

    ec.ec_source.pop();

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
            lnav_data.ld_view_stack.top() | [cmdline] (auto tc) {
                tc->execute_search(cmdline.substr(1));
            };
            break;
        case ';':
            setup_logline_table(ec);
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

    rescan_files();
    rebuild_indexes();

    return retval;
}

void execute_init_commands(exec_context &ec, vector<pair<string, string> > &msgs)
{
    if (lnav_data.ld_cmd_init_done) {
        return;
    }

    db_label_source &dls = lnav_data.ld_db_row_source;
    int option_index = 1;

    log_info("Executing initial commands");
    for (auto &cmd : lnav_data.ld_commands) {
        string msg, alt_msg;

        wait_for_children();

        ec.ec_source.emplace("command-option", option_index++);
        switch (cmd.at(0)) {
        case ':':
            msg = execute_command(ec, cmd.substr(1));
            break;
        case '/':
            lnav_data.ld_view_stack.top() | [cmd] (auto tc) {
                tc->execute_search(cmd.substr(1));
            };
            break;
        case ';':
            setup_logline_table(ec);
            msg = execute_sql(ec, cmd.substr(1), alt_msg);
            break;
        case '|':
            msg = execute_file(ec, cmd.substr(1));
            break;
        }

        msgs.emplace_back(msg, alt_msg);

        rescan_files();
        rebuild_indexes();

        ec.ec_source.pop();
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

    if (dls.dls_rows.size() > 1) {
        ensure_view(&lnav_data.ld_views[LNV_DB]);
    }

    lnav_data.ld_cmd_init_done = true;
}

int sql_callback(exec_context &ec, sqlite3_stmt *stmt)
{
    db_label_source &dls = lnav_data.ld_db_row_source;
    logfile_sub_source &lss = lnav_data.ld_log_source;

    if (!sqlite3_stmt_busy(stmt)) {
        dls.clear();
        lss.text_clear_marks(&BM_QUERY);

        return 0;
    }

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
        const char *value = (const char *)sqlite3_column_text(stmt, lpc);
        db_label_source::header_meta &hm = dls.dls_headers[lpc];

        dls.push_column(value);
        if ((hm.hm_column_type == SQLITE_TEXT ||
             hm.hm_column_type == SQLITE_NULL) && hm.hm_sub_type == 0) {
            sqlite3_value *raw_value = sqlite3_column_value(stmt, lpc);

            switch (sqlite3_value_type(raw_value)) {
                case SQLITE_TEXT:
                    hm.hm_column_type = SQLITE_TEXT;
                    hm.hm_sub_type = sqlite3_value_subtype(raw_value);
                    break;
            }
        }
        if (value != nullptr &&
            (dls.dls_headers[lpc].hm_name == "log_line" ||
             strstr(dls.dls_headers[lpc].hm_name.c_str(), "log_line"))) {
            int line_number = -1;

            if (sscanf(value, "%d", &line_number) == 1) {
                lnav_data.ld_views[LNV_LOG].toggle_user_mark(
                    &BM_QUERY, vis_line_t(line_number));
            }
        }
    }

    return retval;
}

future<string> pipe_callback(exec_context &ec, const string &cmdline, auto_fd &fd)
{
    auto out = ec.get_output();

    if (out) {
        FILE *file = *out;

        return std::async(std::launch::async, [&fd, file]() {
            char buffer[1024];
            ssize_t rc;

            if (file == stdout) {
                lnav_data.ld_stdout_used = true;
            }

            while ((rc = read(fd, buffer, sizeof(buffer))) > 0) {
                fwrite(buffer, rc, 1, file);
            }

            return string();
        });
    } else {
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
        lnav_data.ld_files_to_front.emplace_back(desc, 0);
        if (lnav_data.ld_rl_view != nullptr) {
            lnav_data.ld_rl_view->set_alt_value(HELP_MSG_1(
                                                    X, "to close the file"));
        }

        packaged_task<string()> task([]() { return ""; });

        task();

        return task.get_future();
    }
}

void add_global_vars(exec_context &ec)
{
    for (const auto &iter : lnav_config.lc_global_vars) {
        shlex subber(iter.second);
        string str;

        if (!subber.eval(str, ec.ec_global_vars)) {
            log_error("Unable to evaluate global variable value: %s",
                      iter.second.c_str());
            continue;
        }

        ec.ec_global_vars[iter.first] = str;
    }
}
