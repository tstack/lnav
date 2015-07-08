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

#include "pcrecpp.h"
#include "lnav.hh"

#include "command_executor.hh"

using namespace std;

string execute_from_file(const string &path, int line_number, char mode, const string &cmdline);

string execute_command(string cmdline)
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
            msg = iter->second(cmdline, args);
        }
    }

    return msg;
}

string execute_sql(string sql, string &alt_msg)
{
    db_label_source &      dls = lnav_data.ld_db_rows;
    hist_source &          hs  = lnav_data.ld_db_source;
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

    hs.clear();
    hs.get_displayed_buckets().clear();
    dls.clear();
    dls.dls_stmt_str = stmt_str;
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
                const char *env_value;

                if ((env_value = getenv(&name[1])) != NULL) {
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

                hs.text_value_for_line(lnav_data.ld_views[LNV_DB], 1, row, true);
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

void execute_file(string path, bool multiline)
{
    FILE *file;

    if (path == "-") {
        file = stdin;
    }
    else if ((file = fopen(path.c_str(), "r")) == NULL) {
        return;
    }

    int    line_number = 0, starting_line_number = 0;
    char *line = NULL;
    size_t line_max_size;
    ssize_t line_size;
    string cmdline;
    char mode = '\0';

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
                    execute_from_file(path, starting_line_number, mode, trim(cmdline));
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
                    execute_from_file(path, line_number, ':', line);
                }
                break;
        }

    }

    if (mode) {
        execute_from_file(path, starting_line_number, mode, trim(cmdline));
    }

    if (file != stdin) {
        fclose(file);
    }
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
            execute_file(cmdline);
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
    if (lnav_data.ld_commands.empty()) {
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
            execute_file(iter->substr(1));
            break;
        }

        msgs.push_back(make_pair(msg, alt_msg));

        if (rescan_files()) {
            rebuild_indexes(true);
        }
    }
    lnav_data.ld_commands.clear();
}

int sql_callback(sqlite3_stmt *stmt)
{
    logfile_sub_source &lss = lnav_data.ld_log_source;
    db_label_source &   dls = lnav_data.ld_db_rows;
    hist_source &       hs  = lnav_data.ld_db_source;
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
                hs.set_role_for_type(bucket_type_t(lpc),
                                     view_colors::singleton().
                                     next_plain_highlight());
            }
        }
    }
    for (lpc = 0; lpc < ncols; lpc++) {
        const char *value     = (const char *)sqlite3_column_text(stmt, lpc);
        double      num_value = 0.0;

        dls.push_column(value);
        if (dls.dls_headers[lpc] == "log_line") {
            int line_number = -1;

            if (sscanf(value, "%d", &line_number) == 1) {
                lss.text_mark(&BM_QUERY, line_number, true);
            }
        }
        if (dls.dls_headers_to_graph[lpc]) {
            sscanf(value, "%lf", &num_value);
            hs.add_value(row_number, bucket_type_t(lpc), num_value);
        }
        else {
            hs.add_empty_value(row_number);
        }
    }

    return retval;
}