/**
 * Copyright (c) 2007-2012, Timothy Stack
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

#include <glob.h>
#include <sys/stat.h>

#include <string>
#include <vector>
#include <fstream>
#include <unordered_map>

#include <pcrecpp.h>

#include "lnav.hh"
#include "lnav_config.hh"
#include "lnav_util.hh"
#include "auto_mem.hh"
#include "log_data_table.hh"
#include "log_data_helper.hh"
#include "lnav_commands.hh"
#include "session_data.hh"
#include "command_executor.hh"
#include "url_loader.hh"
#include "readline_curses.hh"
#include "relative_time.hh"
#include "log_search_table.hh"
#include "shlex.hh"
#include "yajl/api/yajl_parse.h"
#include "db_sub_source.hh"

using namespace std;

static string remaining_args(const string &cmdline,
                             const vector<string> &args,
                             size_t index = 1)
{
    size_t start_pos = 0;

    require(index > 0);

    for (size_t lpc = 0; lpc < index; lpc++) {
        start_pos += args[lpc].length();
    }

    size_t index_in_cmdline = cmdline.find(args[index], start_pos);

    require(index_in_cmdline != string::npos);

    return cmdline.substr(index_in_cmdline);
}

static string refresh_pt_search()
{
    string retval;

    if (!lnav_data.ld_cmd_init_done) {
        return "";
    }

#ifdef HAVE_LIBCURL
    for (auto lf : lnav_data.ld_files) {
        if (startswith(lf->get_filename(), "pt:")) {
            lf->close();
        }
    }

    lnav_data.ld_curl_looper.close_request("papertrailapp.com");

    if (lnav_data.ld_pt_search.empty()) {
        return "info: no papertrail query is active";
    }
    unique_ptr<papertrail_proc> pt(new papertrail_proc(
            lnav_data.ld_pt_search.substr(3),
            lnav_data.ld_pt_min_time,
            lnav_data.ld_pt_max_time));
    lnav_data.ld_file_names[lnav_data.ld_pt_search]
        .with_fd(pt->copy_fd());
    lnav_data.ld_curl_looper.add_request(pt.release());

    ensure_view(&lnav_data.ld_views[LNV_LOG]);

    retval = "info: opened papertrail query";
#else
    retval = "error: lnav not compiled with libcurl";
#endif

    return retval;
}

static string com_adjust_log_time(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting new time value";

    if (args.empty()) {
        args.emplace_back("line-time");
    }
    else if (lnav_data.ld_views[LNV_LOG].get_inner_height() == 0) {
        retval = "error: no log messages";
    }
    else if (args.size() >= 2) {
        logfile_sub_source &lss = lnav_data.ld_log_source;
        struct timeval top_time, time_diff;
        struct timeval new_time = { 0, 0 };
        content_line_t top_content;
        date_time_scanner dts;
        vis_line_t top_line;
        struct exttm tm;
        std::shared_ptr<logfile> lf;

        top_line = lnav_data.ld_views[LNV_LOG].get_top();
        top_content = lss.at(top_line);
        lf = lss.find(top_content);

        logline &ll = (*lf)[top_content];

        top_time = ll.get_timeval();

        dts.set_base_time(top_time.tv_sec);
        args[1] = remaining_args(cmdline, args);
        if (dts.scan(args[1].c_str(), args[1].size(), NULL, &tm, new_time) != NULL) {
            timersub(&new_time, &top_time, &time_diff);

            if (ec.ec_dry_run) {
                char buffer[1024];

                snprintf(buffer, sizeof(buffer),
                         "info: log timestamps will be adjusted by %ld.%06ld seconds",
                         time_diff.tv_sec, time_diff.tv_usec);

                retval = buffer;
            } else {
                lf->adjust_content_time(top_content, time_diff, false);

                rebuild_indexes(true);

                retval = "info: adjusted time";
            }
        } else {
            retval = "error: could not parse timestamp";
        }
    }

    return retval;
}

static string com_unix_time(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting a unix time value";

    if (args.empty()) { }
    else if (args.size() >= 2) {
        char      ftime[128] = "";
        bool      parsed     = false;
        struct tm log_time;
        time_t    u_time;
        size_t    millis;
        char *    rest;

        u_time   = time(NULL);
        log_time = *localtime(&u_time);

        log_time.tm_isdst = -1;

        args[1] = remaining_args(cmdline, args);
        if ((millis = args[1].find('.')) != string::npos ||
            (millis = args[1].find(',')) != string::npos) {
            args[1] = args[1].erase(millis, 4);
        }
        if (((rest = strptime(args[1].c_str(),
                              "%b %d %H:%M:%S %Y",
                              &log_time)) != NULL &&
             (rest - args[1].c_str()) >= 20) ||
            ((rest = strptime(args[1].c_str(),
                              "%Y-%m-%d %H:%M:%S",
                              &log_time)) != NULL &&
             (rest - args[1].c_str()) >= 19)) {
            u_time = mktime(&log_time);
            parsed = true;
        }
        else if (sscanf(args[1].c_str(), "%ld", &u_time)) {
            log_time = *localtime(&u_time);

            parsed = true;
        }
        if (parsed) {
            int len;

            strftime(ftime, sizeof(ftime),
                     "%a %b %d %H:%M:%S %Y  %z %Z",
                     localtime(&u_time));
            len = strlen(ftime);
            snprintf(ftime + len, sizeof(ftime) - len,
                     " -- %ld\n",
                     u_time);
            retval = string(ftime);
        }
    }

    return retval;
}

static string com_current_time(exec_context &ec, string cmdline, vector<string> &args)
{
    char      ftime[128];
    struct tm localtm;
    string    retval;
    time_t    u_time;
    size_t    len;

    memset(&localtm, 0, sizeof(localtm));
    u_time = time(NULL);
    strftime(ftime, sizeof(ftime),
             "%a %b %d %H:%M:%S %Y  %z %Z",
             localtime_r(&u_time, &localtm));
    len = strlen(ftime);
    snprintf(ftime + len, sizeof(ftime) - len,
             " -- %ld\n",
             u_time);
    retval = string(ftime);

    return retval;
}

static string com_goto(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting line number/percentage, timestamp, or relative time";

    if (args.empty()) {
        args.emplace_back("move-time");
    }
    else if (args.size() > 1) {
        string all_args = remaining_args(cmdline, args);
        textview_curses *tc = lnav_data.ld_view_stack.back();
        int   line_number, consumed;
        date_time_scanner dts;
        struct relative_time::parse_error pe;
        relative_time rt;
        struct timeval tv;
        struct exttm tm;
        float value;

        if (rt.parse(all_args, pe)) {
            if (tc == &lnav_data.ld_views[LNV_LOG]) {
                content_line_t cl;
                vis_line_t vl;
                logline *ll;

                if (!rt.is_absolute()) {
                    lnav_data.ld_last_relative_time = rt;
                }

                vl = tc->get_top();
                cl = lnav_data.ld_log_source.at(vl);
                ll = lnav_data.ld_log_source.find_line(cl);
                ll->to_exttm(tm);
                rt.add(tm);
                tv.tv_sec = timegm(&tm.et_tm);
                tv.tv_usec = tm.et_nsec / 1000;

                vl = lnav_data.ld_log_source.find_from_time(tv);
                if (ec.ec_dry_run) {
                    retval = "info: will move to line " + to_string((int) vl);
                } else {
                    tc->set_top(vl);
                    retval = "";
                    if (!rt.is_absolute() && lnav_data.ld_rl_view != NULL) {
                        lnav_data.ld_rl_view->set_alt_value(
                            HELP_MSG_2(r, R,
                                       "to move forward/backward the same amount of time"));
                    }
                }
            } else {
                retval = "error: relative time values only work in the log view";
            }
        }
        else if (dts.scan(args[1].c_str(), args[1].size(), NULL, &tm, tv) != NULL) {
            if (tc == &lnav_data.ld_views[LNV_LOG]) {
                vis_line_t vl;

                vl = lnav_data.ld_log_source.find_from_time(tv);
                if (ec.ec_dry_run) {
                    retval = "info: will move to line " + to_string((int) vl);
                } else {
                    tc->set_top(vl);
                    retval = "";
                }
            }
            else {
                retval = "error: time values only work in the log view";
            }
        }
        else if (sscanf(args[1].c_str(), "%f%n", &value, &consumed) == 1) {
            if (args[1][consumed] == '%') {
                line_number = (int)
                              ((double)tc->get_inner_height() *
                               (value / 100.0));
            }
            else {
                line_number = (int)value;
                if (line_number < 0) {
                    line_number = tc->get_inner_height() + line_number;
                }
            }
            if (ec.ec_dry_run) {
                retval = "info: will move to line " + to_string(line_number);
            } else {
                tc->set_top(vis_line_t(line_number));

                retval = "";
            }
        }
    }

    return retval;
}

static string com_relative_goto(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting line number/percentage";

    if (args.empty()) {
    }
    else if (args.size() > 1) {
        textview_curses *tc = lnav_data.ld_view_stack.back();
        int   line_offset, consumed;
        float value;

        if (sscanf(args[1].c_str(), "%f%n", &value, &consumed) == 1) {
            if (args[1][consumed] == '%') {
                line_offset = (int)
                        ((double)tc->get_inner_height() *
                                (value / 100.0));
            }
            else {
                line_offset = (int)value;
            }

            if (ec.ec_dry_run) {
                retval = "info: shifting top by " + to_string(line_offset) + " lines";
            } else {
                tc->shift_top(vis_line_t(line_offset), true);

                retval = "";
            }
        }
    }

    return retval;
}

static string com_mark(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval;

    if (args.empty() || lnav_data.ld_view_stack.empty()) {

    } else if (!ec.ec_dry_run) {
        textview_curses *tc = lnav_data.ld_view_stack.back();
        lnav_data.ld_last_user_mark[tc] = tc->get_top();
        tc->toggle_user_mark(&textview_curses::BM_USER,
                             vis_line_t(lnav_data.ld_last_user_mark[tc]));
        tc->reload_data();
    }

    return retval;
}

static string com_goto_mark(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval;

    if (args.empty()) {
        args.emplace_back("mark-type");
    }
    else {
        textview_curses *tc = lnav_data.ld_view_stack.back();
        string type_name = "user";

        if (args.size() > 1) {
            type_name = args[1];
        }

        bookmark_type_t *bt = bookmark_type_t::find_type(type_name);
        if (bt == nullptr) {
            retval = "error: unknown bookmark type";
        }
        else if (!ec.ec_dry_run) {
            if (args[0] == "next-mark") {
                moveto_cluster(&bookmark_vector<vis_line_t>::next,
                               bt,
                               search_forward_from(tc));
            } else {
                previous_cluster(bt, tc);
            }
            lnav_data.ld_bottom_source.grep_error("");
        }
    }

    return retval;
}

static bool csv_needs_quoting(const string &str)
{
    return (str.find_first_of(",\"") != string::npos);
}

static string csv_quote_string(const string &str)
{
    static pcrecpp::RE csv_column_quoter("\"");

    string retval = str;

    csv_column_quoter.GlobalReplace("\"\"", &retval);
    retval.insert(0, 1, '\"');
    retval.append(1, '\"');

    return retval;
}

static void csv_write_string(FILE *outfile, const string &str)
{
    if (csv_needs_quoting(str)) {
        string quoted_str = csv_quote_string(str);

        fprintf(outfile, "%s", quoted_str.c_str());
    }
    else {
        fprintf(outfile, "%s", str.c_str());
    }
}

static void yajl_writer(void *context, const char *str, size_t len)
{
    FILE *file = (FILE *)context;

    fwrite(str, len, 1, file);
}

static void json_write_row(yajl_gen handle, int row)
{
    db_label_source &dls = lnav_data.ld_db_row_source;
    yajlpp_map obj_map(handle);

    for (size_t col = 0; col < dls.dls_headers.size(); col++) {
        obj_map.gen(dls.dls_headers[col].hm_name);

        if (dls.dls_rows[row][col] == db_label_source::NULL_STR) {
            obj_map.gen();
            continue;
        }

        switch (dls.dls_headers[col].hm_column_type) {
        case SQLITE_FLOAT:
        case SQLITE_INTEGER:
            yajl_gen_number(handle, dls.dls_rows[row][col],
                strlen(dls.dls_rows[row][col]));
            break;
        default:
            obj_map.gen(dls.dls_rows[row][col]);
            break;
        }
    }
}

static string com_save_to(exec_context &ec, string cmdline, vector<string> &args)
{
    FILE *outfile = NULL, *toclose = NULL;
    const char *mode    = "";
    string fn, retval;
    bool to_term = false;

    if (args.empty()) {
        args.emplace_back("filename");
        return "";
    }

    if (lnav_data.ld_flags & LNF_SECURE_MODE) {
        return "error: " + args[0] + " -- unavailable in secure mode";
    }

    if (args.size() < 2) {
        return "error: expecting file name or '-' to write to the terminal";
    }

    fn = trim(remaining_args(cmdline, args));

    vector<string> split_args;
    shlex lexer(fn);
    scoped_resolver scopes = {
        &ec.ec_local_vars.top(),
        &ec.ec_global_vars,
    };

    if (!lexer.split(split_args, scopes)) {
        return "error: unable to parse arguments";
    }
    if (split_args.size() > 1) {
        return "error: more than one file name was matched";
    }

    if (args[0] == "append-to") {
        mode = "a";
    }
    else {
        mode = "w";
    }

    textview_curses *            tc = lnav_data.ld_view_stack.back();
    bookmark_vector<vis_line_t> &bv =
        tc->get_bookmarks()[&textview_curses::BM_USER];
    db_label_source &dls = lnav_data.ld_db_row_source;
    db_overlay_source &dos = lnav_data.ld_db_overlay;

    if (args[0] == "write-csv-to" ||
        args[0] == "write-json-to" ||
        args[0] == "write-cols-to") {
        if (dls.dls_headers.empty()) {
            return "error: no query result to write, use ';' to execute a query";
        }
    }
    else if (args[0] != "write-raw-to") {
        if (bv.empty()) {
            return "error: no lines marked to write, use 'm' to mark lines";
        }
    }

    if (ec.ec_dry_run) {
        outfile = tmpfile();
        toclose = outfile;
    }
    else if (split_args[0] == "-") {
        if (lnav_data.ld_output_stack.empty()) {
            outfile = stdout;
            nodelay(lnav_data.ld_window, 0);
            endwin();
            struct termios curr_termios;
            tcgetattr(1, &curr_termios);
            curr_termios.c_oflag |= ONLCR|OPOST;
            tcsetattr(1, TCSANOW, &curr_termios);
            setvbuf(stdout, NULL, _IONBF, 0);
            to_term = true;
            fprintf(outfile,
                    "\n---------------- Press any key to exit lo-fi display "
                            "----------------\n\n");
        }
        else {
            outfile = lnav_data.ld_output_stack.top();
        }
        if (outfile == stdout) {
            lnav_data.ld_stdout_used = true;
        }
    }
    else if ((outfile = fopen(split_args[0].c_str(), mode)) == NULL) {
        return "error: unable to open file -- " + split_args[0];
    }
    else {
        toclose = outfile;
    }

    if (args[0] == "write-csv-to") {
        std::vector<std::vector<const char *> >::iterator row_iter;
        std::vector<const char *>::iterator iter;
        std::vector<db_label_source::header_meta>::iterator hdr_iter;
        bool first = true;

        for (hdr_iter = dls.dls_headers.begin();
             hdr_iter != dls.dls_headers.end();
             ++hdr_iter) {
            if (!first) {
                fprintf(outfile, ",");
            }
            csv_write_string(outfile, hdr_iter->hm_name);
            first = false;
        }
        fprintf(outfile, "\n");

        for (row_iter = dls.dls_rows.begin();
             row_iter != dls.dls_rows.end();
             ++row_iter) {
            if (ec.ec_dry_run &&
                distance(dls.dls_rows.begin(), row_iter) > 10) {
                break;
            }

            first = true;
            for (iter = row_iter->begin();
                 iter != row_iter->end();
                 ++iter) {
                if (!first) {
                    fprintf(outfile, ",");
                }
                csv_write_string(outfile, *iter);
                first = false;
            }
            fprintf(outfile, "\n");
        }
    }
    else if (args[0] == "write-cols-to") {
        attr_line_t header_line;

        dos.list_value_for_overlay(lnav_data.ld_views[LNV_DB], 0, 1, 0_vl, header_line);
        fputs(header_line.get_string().c_str(), outfile);
        fputc('\n', outfile);
        for (size_t lpc = 0; lpc < dls.text_line_count(); lpc++) {
            if (ec.ec_dry_run && lpc > 10) {
                break;
            }

            string line;

            dls.text_value_for_line(lnav_data.ld_views[LNV_DB], lpc, line,
                                    text_sub_source::RF_RAW);
            fputs(line.c_str(), outfile);
            fputc('\n', outfile);
        }
    }
    else if (args[0] == "write-json-to") {
        yajl_gen handle = NULL;

        if ((handle = yajl_gen_alloc(NULL)) == NULL) {
            if (outfile != stdout) {
                fclose(outfile);
            }
            return "error: unable to allocate memory";
        }
        else {
            yajl_gen_config(handle, yajl_gen_beautify, 1);
            yajl_gen_config(handle,
                yajl_gen_print_callback, yajl_writer, outfile);

            {
                yajlpp_array root_array(handle);

                for (size_t row = 0; row < dls.dls_rows.size(); row++) {
                    if (ec.ec_dry_run && row > 10) {
                        break;
                    }

                    json_write_row(handle, row);
                }
            }
        }
    }
    else if (args[0] == "write-raw-to") {
        if (tc == &lnav_data.ld_views[LNV_DB]) {
            std::vector<std::vector<const char *> >::iterator row_iter;
            std::vector<const char *>::iterator iter;

            for (row_iter = dls.dls_rows.begin();
                 row_iter != dls.dls_rows.end();
                 ++row_iter) {
                if (ec.ec_dry_run &&
                    distance(dls.dls_rows.begin(), row_iter) > 10) {
                    break;
                }

                for (iter = row_iter->begin();
                     iter != row_iter->end();
                     ++iter) {
                    fputs(*iter, outfile);
                }
                fprintf(outfile, "\n");
            }
        } else {
            bool wrapped = tc->get_word_wrap();
            vis_line_t orig_top = tc->get_top();

            tc->set_word_wrap(to_term);

            vis_line_t top = tc->get_top();
            vis_line_t bottom = tc->get_bottom();
            vector<attr_line_t> rows(bottom - top + 1);

            tc->listview_value_for_rows(*tc, top, rows);
            for (auto &al : rows) {
                struct line_range lr = find_string_attr_range(
                    al.get_attrs(), &textview_curses::SA_ORIGINAL_LINE);
                log_perror(write(STDOUT_FILENO, lr.substr(al.get_string()),
                                 lr.sublen(al.get_string())));
                log_perror(write(STDOUT_FILENO, "\n", 1));
            }

            tc->set_word_wrap(wrapped);
            tc->set_top(orig_top);
        }
    }
    else {
        bookmark_vector<vis_line_t>::iterator iter;
        size_t count = 0;
        string line;

        for (iter = bv.begin(); iter != bv.end(); iter++, count++) {
            if (ec.ec_dry_run && count > 10) {
                break;
            }
            tc->grep_value_for_line(*iter, line);
            fprintf(outfile, "%s\n", line.c_str());
        }
    }

    fflush(outfile);

    if (to_term) {
        cbreak();
        getch();
        refresh();
        nodelay(lnav_data.ld_window, 1);
    }
    if (ec.ec_dry_run) {
        rewind(outfile);

        char buffer[32 * 1024];
        size_t rc = fread(buffer, 1, sizeof(buffer), outfile);

        attr_line_t al(string(buffer, rc));

        lnav_data.ld_preview_source
                 .replace_with(al)
                 .set_text_format(detect_text_format(buffer, rc))
                 .truncate_to(10);
        lnav_data.ld_preview_status_source.get_description()
                 .set_value("First lines of file: %s", fn.c_str());
    }
    if (toclose != NULL) {
        fclose(toclose);
    }
    outfile = NULL;

    return "";
}

static string com_pipe_to(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting command to execute";

    if (args.empty()) {
        args.emplace_back("filename");
        return "";
    }

    if (lnav_data.ld_flags & LNF_SECURE_MODE) {
        return "error: " + args[0] + " -- unavailable in secure mode";
    }

    if (args.size() < 2) {
        return retval;
    }

    if (ec.ec_dry_run) {
        return "";
    }

    textview_curses *            tc = lnav_data.ld_view_stack.back();
    bookmark_vector<vis_line_t> &bv =
            tc->get_bookmarks()[&textview_curses::BM_USER];
    bool pipe_line_to = (args[0] == "pipe-line-to");

    string cmd = trim(remaining_args(cmdline, args));
    auto_pipe in_pipe(STDIN_FILENO);
    auto_pipe out_pipe(STDOUT_FILENO);

    in_pipe.open();
    out_pipe.open();

    pid_t child_pid = fork();

    in_pipe.after_fork(child_pid);
    out_pipe.after_fork(child_pid);

    switch (child_pid) {
        case -1:
            return "error: unable to fork child process -- " + string(strerror(errno));

        case 0: {
            const char *args[] = {
                    "sh", "-c", cmd.c_str(), NULL,
            };
            vector<std::string> path_v = ec.ec_path_stack;
            string path;

            dup2(STDOUT_FILENO, STDERR_FILENO);
            path_v.push_back(dotlnav_path("formats/default"));

            if (pipe_line_to && tc == &lnav_data.ld_views[LNV_LOG]) {
                logfile_sub_source &lss = lnav_data.ld_log_source;
                log_data_helper ldh(lss);
                char tmp_str[64];

                ldh.parse_line(ec.ec_top_line, true);
                log_format *format = ldh.ldh_file->get_format();
                set<string> source_path = format->get_source_path();
                path_v.insert(path_v.end(),
                              source_path.begin(),
                              source_path.end());

                snprintf(tmp_str, sizeof(tmp_str), "%d", (int) ec.ec_top_line);
                setenv("log_line", tmp_str, 1);
                sql_strftime(tmp_str, sizeof(tmp_str), ldh.ldh_line->get_timeval());
                setenv("log_time", tmp_str, 1);
                setenv("log_path", ldh.ldh_file->get_filename().c_str(), 1);
                for (vector<logline_value>::iterator iter = ldh.ldh_line_values.begin();
                     iter != ldh.ldh_line_values.end();
                     ++iter) {
                    setenv(iter->lv_name.get(), iter->to_string().c_str(), 1);
                }
                data_parser::element_list_t::iterator iter =
                        ldh.ldh_parser->dp_pairs.begin();
                for (size_t lpc = 0; lpc < ldh.ldh_parser->dp_pairs.size(); lpc++, ++iter) {
                    std::string colname = ldh.ldh_parser->get_element_string(
                            iter->e_sub_elements->front());
                    colname = ldh.ldh_namer->add_column(colname);
                    string val = ldh.ldh_parser->get_element_string(
                            iter->e_sub_elements->back());
                    setenv(colname.c_str(), val.c_str(), 1);
                }
            }

            setenv("PATH", build_path(path_v).c_str(), 1);
            execvp(args[0], (char *const *) args);
            _exit(1);
            break;
        }

        default:
            bookmark_vector<vis_line_t>::iterator iter;
            string line;

            in_pipe.read_end().close_on_exec();
            in_pipe.write_end().close_on_exec();

            lnav_data.ld_children.push_back(child_pid);

            future<string> reader;

            if (out_pipe.read_end() != -1) {
                reader = ec.ec_pipe_callback(ec, cmdline, out_pipe.read_end());
            }

            if (pipe_line_to) {
                if (tc->get_inner_height() == 0) {
                    // Nothing to do
                }
                else if (tc == &lnav_data.ld_views[LNV_LOG]) {
                    logfile_sub_source &lss = lnav_data.ld_log_source;
                    content_line_t cl = lss.at(tc->get_top());
                    std::shared_ptr<logfile> lf = lss.find(cl);
                    shared_buffer_ref sbr;
                    lf->read_full_message(lf->message_start(lf->begin() + cl), sbr);
                    if (write(in_pipe.write_end(), sbr.get_data(), sbr.length()) == -1) {
                        return "warning: Unable to write to pipe -- " + string(strerror(errno));
                    }
                    log_perror(write(in_pipe.write_end(), "\n", 1));
                }
                else {
                    tc->grep_value_for_line(tc->get_top(), line);
                    if (write(in_pipe.write_end(), line.c_str(), line.size()) == -1) {
                        return "warning: Unable to write to pipe -- " + string(strerror(errno));
                    }
                    log_perror(write(in_pipe.write_end(), "\n", 1));
                }
            }
            else {
                for (iter = bv.begin(); iter != bv.end(); iter++) {
                    tc->grep_value_for_line(*iter, line);
                    if (write(in_pipe.write_end(), line.c_str(), line.size()) == -1) {
                        return "warning: Unable to write to pipe -- " + string(strerror(errno));
                    }
                    log_perror(write(in_pipe.write_end(), "\n", 1));
                }
            }

            in_pipe.write_end().reset();

            if (reader.valid()) {
                retval = reader.get();
            }
            else {
                retval = "";
            }
            break;
    }

    return retval;
}

static string com_highlight(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting regular expression to highlight";

    if (args.empty()) {
        args.emplace_back("filter");
    }
    else if (args.size() > 1) {
        textview_curses *tc = lnav_data.ld_view_stack.back();
        textview_curses::highlight_map_t &hm = tc->get_highlights();
        const char *errptr;
        auto_mem<pcre> code;
        int         eoff;

        args[1] = remaining_args(cmdline, args);
        if (hm.find(args[1]) != hm.end()) {
            retval = "error: highlight already exists";
        }
        else if ((code = pcre_compile(args[1].c_str(),
                                      PCRE_CASELESS,
                                      &errptr,
                                      &eoff,
                                      NULL)) == NULL) {
            retval = "error: " + string(errptr);
        }
        else {
            highlighter hl(code.release());
            attr_t hl_attrs = view_colors::singleton().attrs_for_ident(args[1]);

            if (ec.ec_dry_run) {
                hl_attrs |= A_BLINK;
            }

            hl.with_attrs(hl_attrs);

            if (ec.ec_dry_run) {
                hm["$preview"] = hl;

                lnav_data.ld_preview_status_source.get_description()
                         .set_value("Matches are highlighted in the view");

                retval = "";
            } else {
                hm[args[1]] = hl;

                if (lnav_data.ld_rl_view != NULL) {
                    lnav_data.ld_rl_view->add_possibility(
                        LNM_COMMAND, "highlight", args[1]);
                }

                retval = "info: highlight pattern now active";
            }
            tc->reload_data();
        }
    }

    return retval;
}

static string com_clear_highlight(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting highlight expression to clear";

    if (args.empty()) {
        args.emplace_back("highlight");
    }
    else if (args.size() > 1 && args[1][0] != '$') {
        textview_curses *tc = lnav_data.ld_view_stack.back();
        textview_curses::highlight_map_t &hm = tc->get_highlights();
        textview_curses::highlight_map_t::iterator hm_iter;

        args[1] = remaining_args(cmdline, args);
        hm_iter = hm.find(args[1]);
        if (hm_iter == hm.end()) {
            retval = "error: highlight does not exist";
        }
        else if (ec.ec_dry_run) {
            retval = "";
        }
        else {
            hm.erase(hm_iter);
            retval = "info: highlight pattern cleared";
            tc->reload_data();

            if (lnav_data.ld_rl_view != NULL) {
                lnav_data.ld_rl_view->rem_possibility(
                    LNM_COMMAND, "highlight", args[1]);
            }

        }
    }

    return retval;
}

static string com_help(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval;

    if (args.empty()) {}
    else if (!ec.ec_dry_run) {
        ensure_view(&lnav_data.ld_views[LNV_HELP]);
    }

    return retval;
}

class pcre_filter
    : public text_filter {
public:
    pcre_filter(type_t type, const string id, size_t index, pcre *code)
        : text_filter(type, id, index),
          pf_pcre(code) { };

    ~pcre_filter() override { };

    bool matches(const logfile &lf, const logline &ll, shared_buffer_ref &line) override {
        pcre_context_static<30> pc;
        pcre_input pi(line.get_data(), 0, line.length());

        return this->pf_pcre.match(pc, pi);
    };

    std::string to_command(void) override {
        return (this->lf_type == text_filter::INCLUDE ?
                "filter-in " : "filter-out ") +
               this->lf_id;
    };

protected:
    pcrepp pf_pcre;
};

static string com_enable_filter(exec_context &ec, string cmdline, vector<string> &args);

static string com_filter(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting regular expression to filter out";

    if (args.empty()) {
        args.emplace_back("filter");
    }
    else if (args.size() > 1) {
        textview_curses *tc = lnav_data.ld_view_stack.back();
        text_sub_source *tss = tc->get_sub_source();
        filter_stack &fs = tss->get_filters();
        const char *errptr;
        auto_mem<pcre> code;
        int         eoff;

        args[1] = remaining_args(cmdline, args);
        if (fs.get_filter(args[1]) != NULL) {
            retval = com_enable_filter(ec, cmdline, args);
        }
        else if (fs.full()) {
            retval = "error: filter limit reached, try combining "
                    "filters with a pipe symbol (e.g. foo|bar)";
        }
        else if ((code = pcre_compile(args[1].c_str(),
                                      PCRE_CASELESS,
                                      &errptr,
                                      &eoff,
                                      nullptr)) == NULL) {
            retval = "error: " + string(errptr);
        }
        else if (ec.ec_dry_run) {
            if (args[0] == "filter-in" && !fs.empty()) {
                lnav_data.ld_preview_status_source.get_description()
                    .set_value("Match preview for :filter-in only works if there are no other filters");
            } else {
                textview_curses::highlight_map_t &hm = tc->get_highlights();
                highlighter hl(code.release());
                int color;

                if (args[0] == "filter-out") {
                    color = COLOR_RED;
                } else {
                    color = COLOR_GREEN;
                }
                hl.with_attrs(
                    view_colors::ansi_color_pair(COLOR_BLACK, color) | A_BLINK);

                hm["$preview"] = hl;
                tc->reload_data();

                lnav_data.ld_preview_status_source.get_description()
                    .set_value(
                        "Matches are highlighted in %s in the text view",
                        color == COLOR_RED ? "red" : "green");

                retval = "";
            }
        }
        else {
            text_filter::type_t lt  = (args[0] == "filter-out") ?
                                         text_filter::EXCLUDE :
                                         text_filter::INCLUDE;
            auto pf = make_shared<pcre_filter>(lt, args[1], fs.next_index(), code.release());
            lnav_view_t view_index = lnav_view_t(tc - lnav_data.ld_views);

            log_debug("%s [%d] %s", args[0].c_str(), pf->get_index(), args[1].c_str());
            fs.add_filter(pf);
            tss->text_filters_changed();
            redo_search(view_index);
            if (lnav_data.ld_rl_view != NULL) {
                lnav_data.ld_rl_view->add_possibility(
                    LNM_COMMAND, "enabled-filter", args[1]);
            }

            retval = "info: filter now active";
        }
    }

    return retval;
}

static string com_delete_filter(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting a filter to delete";

    if (args.empty()) {
        args.emplace_back("all-filters");
    }
    else if (args.size() > 1) {
        textview_curses *tc = lnav_data.ld_view_stack.back();
        text_sub_source *tss = tc->get_sub_source();
        filter_stack &fs = tss->get_filters();

        args[1] = remaining_args(cmdline, args);
        if (fs.delete_filter(args[1])) {
            retval = "info: deleted filter";
            tss->text_filters_changed();
            redo_search(lnav_view_t(tc - lnav_data.ld_views));
        }
        else {
            retval = "error: unknown filter -- " + args[1];
        }
    }

    return retval;
}

static string com_enable_filter(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting disabled filter to enable";

    if (args.empty()) {
        args.emplace_back("disabled-filter");
    }
    else if (args.size() > 1) {
        textview_curses *tc = lnav_data.ld_view_stack.back();
        text_sub_source *tss = tc->get_sub_source();
        filter_stack &fs = tss->get_filters();
        shared_ptr<text_filter> lf;

        args[1] = remaining_args(cmdline, args);
        lf      = fs.get_filter(args[1]);
        if (lf == NULL) {
            retval = "error: no such filter -- " + args[1];
        }
        else if (lf->is_enabled()) {
            retval = "info: filter already enabled";
        }
        else if (ec.ec_dry_run) {
            retval = "";
        }
        else {
            fs.set_filter_enabled(lf, true);
            tss->text_filters_changed();
            redo_search(lnav_view_t(tc - lnav_data.ld_views));
            retval = "info: filter enabled";
        }
    }

    return retval;
}

static string com_disable_filter(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting enabled filter to disable";

    if (args.empty()) {
        args.emplace_back("enabled-filter");
    }
    else if (args.size() > 1) {
        textview_curses *tc = lnav_data.ld_view_stack.back();
        text_sub_source *tss = tc->get_sub_source();
        filter_stack &fs = tss->get_filters();
        shared_ptr<text_filter> lf;

        args[1] = remaining_args(cmdline, args);
        lf      = fs.get_filter(args[1]);
        if (lf == NULL) {
            retval = "error: no such filter -- " + args[1];
        }
        else if (!lf->is_enabled()) {
            retval = "info: filter already disabled";
        }
        else if (ec.ec_dry_run) {
            retval = "";
        }
        else {
            fs.set_filter_enabled(lf, false);
            tss->text_filters_changed();
            redo_search(lnav_view_t(tc - lnav_data.ld_views));
            retval = "info: filter disabled";
        }
    }

    return retval;
}

static string com_enable_word_wrap(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval;

    if (args.empty()) {

    }
    else if (!ec.ec_dry_run) {
        lnav_data.ld_views[LNV_LOG].set_word_wrap(true);
        lnav_data.ld_views[LNV_TEXT].set_word_wrap(true);
        lnav_data.ld_views[LNV_PRETTY].set_word_wrap(true);
    }

    return retval;
}

static string com_disable_word_wrap(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval;

    if (args.empty()) {

    }
    else if (!ec.ec_dry_run) {
        lnav_data.ld_views[LNV_LOG].set_word_wrap(false);
        lnav_data.ld_views[LNV_TEXT].set_word_wrap(false);
        lnav_data.ld_views[LNV_PRETTY].set_word_wrap(false);
    }

    return retval;
}

static std::set<string> custom_logline_tables;

static string com_create_logline_table(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting a table name";

    if (args.empty()) {}
    else if (args.size() == 2) {
        textview_curses &log_view = lnav_data.ld_views[LNV_LOG];

        if (log_view.get_inner_height() == 0) {
            retval = "error: no log data available";
        }
        else {
            vis_line_t      vl  = log_view.get_top();
            content_line_t  cl  = lnav_data.ld_log_source.at_base(vl);
            log_data_table *ldt = new log_data_table(cl, intern_string::lookup(args[1]));

            if (ec.ec_dry_run) {
                attr_line_t al(ldt->get_table_statement());

                lnav_data.ld_preview_status_source.get_description()
                    .set_value("The following table will be created:");
                lnav_data.ld_preview_source.replace_with(al)
                         .set_text_format(TF_SQL);

                delete ldt;

                return "";
            }
            else {
                string errmsg;

                errmsg = lnav_data.ld_vtab_manager->register_vtab(ldt);
                if (errmsg.empty()) {
                    custom_logline_tables.insert(args[1]);
                    if (lnav_data.ld_rl_view != NULL) {
                        lnav_data.ld_rl_view->add_possibility(LNM_COMMAND,
                                                              "custom-table",
                                                              args[1]);
                    }
                    retval = "info: created new log table -- " + args[1];
                } else {
                    delete ldt;
                    retval = "error: unable to create table -- " + errmsg;
                }
            }
        }
    }

    return retval;
}

static string com_delete_logline_table(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting a table name";

    if (args.empty()) {
        args.emplace_back("custom-table");
    }
    else if (args.size() == 2) {
        if (custom_logline_tables.find(args[1]) == custom_logline_tables.end()) {
            return "error: unknown logline table -- " + args[1];
        }

        if (ec.ec_dry_run) {
            return "";
        }

        string rc = lnav_data.ld_vtab_manager->unregister_vtab(
                intern_string::lookup(args[1]));

        if (rc.empty()) {
            if (lnav_data.ld_rl_view != NULL) {
                lnav_data.ld_rl_view->rem_possibility(LNM_COMMAND,
                  "custom-table",
                  args[1]);
            }
            retval = "info: deleted logline table";
        }
        else {
            retval = "error: " + rc;
        }
    }

    return retval;
}

static std::set<string> custom_search_tables;

static string com_create_search_table(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting a table name";

    if (args.empty()) {

    }
    else if (args.size() >= 2) {
        log_search_table *lst;
        auto_mem<pcre> code;
        const char *errptr;
        string regex;
        int eoff;

        if (args.size() >= 3) {
            regex = remaining_args(cmdline, args, 2);
        }
        else {
            regex = lnav_data.ld_last_search[LNV_LOG];
        }

        if ((code = pcre_compile(regex.c_str(),
                                 PCRE_CASELESS,
                                 &errptr,
                                 &eoff,
                                 NULL)) == NULL) {
            return "error: " + string(errptr);
        }

        try {
            lst = new log_search_table(regex.c_str(),
                                       intern_string::lookup(args[1]));
        } catch (pcrepp::error &e) {
            return "error: unable to compile regex -- " + regex;
        }

        if (ec.ec_dry_run) {
            textview_curses *tc = &lnav_data.ld_views[LNV_LOG];
            textview_curses::highlight_map_t &hm = tc->get_highlights();
            view_colors &vc = view_colors::singleton();
            highlighter hl(code.release());

            hl.with_attrs(
                vc.ansi_color_pair(COLOR_BLACK, COLOR_CYAN) | A_BLINK);

            hm["$preview"] = hl;
            tc->reload_data();

            attr_line_t al(lst->get_table_statement());

            lnav_data.ld_preview_status_source.get_description()
                     .set_value("The following table will be created:");

            lnav_data.ld_preview_source.replace_with(al)
                .set_text_format(TF_SQL);

            return "";
        }

        string errmsg;

        errmsg = lnav_data.ld_vtab_manager->register_vtab(lst);
        if (errmsg.empty()) {
            custom_search_tables.insert(args[1]);
            if (lnav_data.ld_rl_view != NULL) {
                lnav_data.ld_rl_view->add_possibility(LNM_COMMAND,
                                                      "search-table",
                                                      args[1]);
            }
            retval = "info: created new search table -- " + args[1];
        }
        else {
            delete lst;
            retval = "error: unable to create table -- " + errmsg;
        }
    }

    return retval;
}

static string com_delete_search_table(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting a table name";

    if (args.empty()) {
        args.emplace_back("search-table");
    }
    else if (args.size() == 2) {
        if (custom_search_tables.find(args[1]) == custom_search_tables.end()) {
            return "error: unknown search table -- " + args[1];
        }

        if (ec.ec_dry_run) {
            return "";
        }

        string rc = lnav_data.ld_vtab_manager->unregister_vtab(
                intern_string::lookup(args[1]));

        if (rc.empty()) {
            if (lnav_data.ld_rl_view != NULL) {
                lnav_data.ld_rl_view->rem_possibility(LNM_COMMAND,
                                                      "search-table",
                                                      args[1]);
            }
            retval = "info: deleted search table";
        }
        else {
            retval = "error: " + rc;
        }
    }

    return retval;
}

static string com_session(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting a command to save to the session file";

    if (args.empty()) {}
    else if (ec.ec_dry_run) {
        retval = "";
    }
    else if (args.size() >= 2) {
        /* XXX put these in a map */
        if (args[1] != "highlight" &&
            args[1] != "enable-word-wrap" &&
            args[1] != "disable-word-wrap" &&
            args[1] != "filter-in" &&
            args[1] != "filter-out" &&
            args[1] != "enable-filter" &&
            args[1] != "disable-filter") {
            retval = "error: only the highlight, filter, and word-wrap commands are "
                     "supported";
        }
        else if (getenv("HOME") == NULL) {
            retval = "error: the HOME environment variable is not set";
        }
        else {
            string            old_file_name, new_file_name;
            string            saved_cmd;

            saved_cmd = trim(remaining_args(cmdline, args));

            old_file_name = dotlnav_path("session");
            new_file_name = dotlnav_path("session.tmp");

            ifstream session_file(old_file_name.c_str());
            ofstream new_session_file(new_file_name.c_str());

            if (!new_session_file) {
                retval = "error: cannot write to session file";
            }
            else {
                bool   added = false;
                string line;

                if (session_file.is_open()) {
                    while (getline(session_file, line)) {
                        if (line == saved_cmd) {
                            added = true;
                            break;
                        }
                        new_session_file << line << endl;
                    }
                }
                if (!added) {
                    new_session_file << saved_cmd << endl;

                    log_perror(rename(new_file_name.c_str(),
                                      old_file_name.c_str()));
                }
                else {
                    log_perror(remove(new_file_name.c_str()));
                }

                retval = "info: session file saved";
            }
        }
    }

    return retval;
}

static string com_open(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting file name to open";

    if (args.empty()) {
        args.emplace_back("filename");
        return "";
    }
    else if (lnav_data.ld_flags & LNF_SECURE_MODE) {
        return "error: " + args[0] + " -- unavailable in secure mode";
    }
    else if (args.size() < 2) {
        return retval;
    }

    vector<string> word_exp;
    size_t colon_index;
    string pat;

    pat = trim(remaining_args(cmdline, args));

    vector<string> split_args;
    shlex lexer(pat);
    scoped_resolver scopes = {
        &ec.ec_local_vars.top(),
        &ec.ec_global_vars,
    };

    if (!lexer.split(split_args, scopes)) {
        return "error: unable to parse arguments";
    }

    map<string, logfile_open_options> file_names;
    vector<pair<string, int>> files_to_front;
    vector<string> closed_files;

    for (size_t lpc = 0; lpc < split_args.size(); lpc++) {
        string fn = split_args[lpc];
        int top = 0;

        if (startswith(fn, "pt:")) {
            if (!ec.ec_dry_run) {
                lnav_data.ld_pt_search = fn;

                refresh_pt_search();
            }
            continue;
        }

        if (access(fn.c_str(), R_OK) != 0 &&
            (colon_index = fn.rfind(':')) != string::npos) {
            if (sscanf(&fn.c_str()[colon_index + 1], "%d", &top) == 1) {
                fn = fn.substr(0, colon_index);
            }
        }

        auto file_iter = lnav_data.ld_files.begin();
        for (; file_iter != lnav_data.ld_files.end(); ++file_iter) {
            auto lf = *file_iter;

            if (lf->get_filename() == fn) {
                if (lf->get_format() != NULL) {
                    retval = "info: log file already loaded";
                    break;
                }
                else {
                    files_to_front.push_back(make_pair(fn, top));
                    retval = "";
                    break;
                }
            }
        }
        if (file_iter == lnav_data.ld_files.end()) {
            logfile_open_options default_loo;
            auto_mem<char> abspath;
            struct stat    st;

            if (is_url(fn.c_str())) {
#ifndef HAVE_LIBCURL
                retval = "error: lnav was not compiled with libcurl";
#else
                if (!ec.ec_dry_run) {
                    auto_ptr<url_loader> ul(new url_loader(fn));

                    lnav_data.ld_file_names[fn]
                        .with_fd(ul->copy_fd());
                    lnav_data.ld_curl_looper.add_request(ul.release());
                    lnav_data.ld_files_to_front.push_back(make_pair(fn, top));
                    retval = "info: opened URL";
                } else {
                    retval = "";
                }
#endif
            }
            else if (is_glob(fn.c_str())) {
                file_names[fn] = default_loo;
                retval = "info: watching -- " + fn;
            }
            else if (stat(fn.c_str(), &st) == -1) {
                retval = ("error: cannot stat file: " + fn + " -- "
                    + strerror(errno));
            }
            else if (S_ISFIFO(st.st_mode)) {
                auto_fd fifo_fd;

                if ((fifo_fd = open(fn.c_str(), O_RDONLY)) == -1) {
                    retval = "error: cannot open FIFO: " + fn + " -- "
                        + strerror(errno);
                } else if (ec.ec_dry_run) {
                    retval = "";
                } else {
                    auto fifo_piper = make_shared<piper_proc>(
                        fifo_fd.release(), false);
                    int fifo_out_fd = fifo_piper->get_fd();
                    char desc[128];

                    snprintf(desc, sizeof(desc),
                             "FIFO [%d]",
                             lnav_data.ld_fifo_counter++);
                    lnav_data.ld_file_names[desc]
                        .with_fd(fifo_out_fd);
                    lnav_data.ld_pipers.push_back(fifo_piper);
                }
            }
            else if ((abspath = realpath(fn.c_str(), NULL)) == NULL) {
                retval = "error: cannot find file";
            }
            else if (S_ISDIR(st.st_mode)) {
                string dir_wild(abspath.in());

                if (dir_wild[dir_wild.size() - 1] == '/') {
                    dir_wild.resize(dir_wild.size() - 1);
                }
                file_names[dir_wild + "/*"] = default_loo;
                retval = "info: watching -- " + dir_wild;
            }
            else if (!S_ISREG(st.st_mode)) {
                retval = "error: not a regular file or directory";
            }
            else if (access(fn.c_str(), R_OK) == -1) {
                retval = (string("error: cannot read file -- ") +
                    strerror(errno));
            }
            else {
                fn = abspath.in();
                file_names[fn] = default_loo;
                retval = "info: opened -- " + fn;
                files_to_front.push_back(make_pair(fn, top));

                closed_files.push_back(fn);
                if (lnav_data.ld_rl_view != NULL) {
                    lnav_data.ld_rl_view->set_alt_value(HELP_MSG_1(
                        X, "to close the file"));
                }
            }
        }
    }

    if (ec.ec_dry_run) {
        lnav_data.ld_preview_source.clear();
        if (!file_names.empty()) {
            auto iter = file_names.begin();
            string fn = iter->first;
            auto_fd preview_fd;

            if (is_glob(fn.c_str())) {
                static_root_mem<glob_t, globfree> gl;

                if (glob(fn.c_str(), GLOB_NOCHECK, NULL, gl.inout()) == 0) {
                    attr_line_t al;

                    for (size_t lpc = 0; lpc < gl->gl_pathc && lpc < 10; lpc++) {
                        al.append(gl->gl_pathv[lpc])
                          .append("\n");
                    }
                    if (gl->gl_pathc > 10) {
                        al.append(" ... ")
                          .append(to_string(gl->gl_pathc - 10),
                                  &view_curses::VC_STYLE,
                                  A_BOLD)
                          .append(" files not shown ...");
                    }
                    lnav_data.ld_preview_status_source.get_description()
                        .set_value("The following files will be loaded:");
                    lnav_data.ld_preview_source.replace_with(al);
                } else {
                    retval = "error: failed to evaluate glob -- " + fn;
                }
            }
            else if ((preview_fd = open(fn.c_str(), O_RDONLY)) == -1) {
                retval = "error: unable to open file: " + fn + " -- " +
                         strerror(errno);
            }
            else {
                char buffer[32 * 1024];
                ssize_t rc;

                rc = read(preview_fd, buffer, sizeof(buffer));

                attr_line_t al(string(buffer, rc));

                lnav_data.ld_preview_source
                         .replace_with(al)
                         .set_text_format(detect_text_format(buffer, rc))
                         .truncate_to(10);
                lnav_data.ld_preview_status_source.get_description()
                    .set_value("For file: %s", fn.c_str());
            }
        }
    } else {
        lnav_data.ld_files_to_front.insert(
            lnav_data.ld_files_to_front.end(),
            files_to_front.begin(),
            files_to_front.end());
        lnav_data.ld_file_names.insert(file_names.begin(), file_names.end());
        for (const auto &fn : closed_files) {
            lnav_data.ld_closed_files.erase(fn);
        }
    }

    return retval;
}

static string com_close(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: close must be run in the log or text file views";

    if (args.empty()) {

    }
    else {
        textview_curses *tc = lnav_data.ld_view_stack.back();
        string fn;

        if (tc == &lnav_data.ld_views[LNV_TEXT]) {
            textfile_sub_source &tss = lnav_data.ld_text_source;

            if (tss.empty()) {
                retval = "error: no text files are opened";
            }
            else {
                fn = tss.current_file()->get_filename();
                tss.current_file()->close();

                if (tss.size() == 1) {
                    lnav_data.ld_view_stack.pop_back();
                }
            }
        }
        else if (tc == &lnav_data.ld_views[LNV_LOG]) {
            if (tc->get_inner_height() == 0) {
                retval = "error: no log files loaded";
            }
            else {
                logfile_sub_source &lss = lnav_data.ld_log_source;
                vis_line_t vl = tc->get_top();
                content_line_t cl = lss.at(vl);
                std::shared_ptr<logfile> lf = lss.find(cl);

                fn = lf->get_filename();
                lf->close();
            }
        }
        if (!fn.empty()) {
            if (ec.ec_dry_run) {
                retval = "";
            }
            else {
                if (is_url(fn.c_str())) {
                    lnav_data.ld_curl_looper.close_request(fn);
                }
                lnav_data.ld_file_names.erase(fn);
                lnav_data.ld_closed_files.insert(fn);
                retval = "info: closed -- " + fn;
            }
        }
    }

    return retval;
}

static string com_comment(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting some comment text";

    if (args.empty()) {
        return "";
    }
    else if (args.size() > 1) {
        if (ec.ec_dry_run) {
            return "";
        }
        textview_curses *tc = lnav_data.ld_view_stack.back();

        if (tc != &lnav_data.ld_views[LNV_LOG]) {
            return "error: The :comment command only works in the log view";
        }
        logfile_sub_source &lss = lnav_data.ld_log_source;
        std::map<content_line_t, bookmark_metadata> &bm = lss.get_user_bookmark_metadata();

        args[1] = trim(remaining_args(cmdline, args));

        tc->set_user_mark(&textview_curses::BM_META, tc->get_top(), true);

        bookmark_metadata &line_meta = bm[lss.at(tc->get_top())];

        line_meta.bm_comment = args[1];

        retval = "info: comment added to line";
    }

    return retval;
}

static string com_clear_comment(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval;

    if (args.empty()) {
        return "";
    }
    else if (ec.ec_dry_run) {
        return "";
    } else {
        textview_curses *tc = lnav_data.ld_view_stack.back();

        if (tc != &lnav_data.ld_views[LNV_LOG]) {
            return "error: The :clear-comment command only works in the log view";
        }
        logfile_sub_source &lss = lnav_data.ld_log_source;
        std::map<content_line_t, bookmark_metadata> &bm = lss.get_user_bookmark_metadata();

        auto iter = bm.find(lss.at(tc->get_top()));
        if (iter != bm.end()) {
            bookmark_metadata &line_meta = iter->second;

            line_meta.bm_comment.clear();
            if (line_meta.empty()) {
                bm.erase(iter);
                tc->set_user_mark(&textview_curses::BM_META, tc->get_top(), false);
            }

            retval = "info: cleared comment";
        }
    }

    return retval;
}

static string com_tag(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting one or more tags";

    if (args.empty()) {
        args.emplace_back("tag");
        return "";
    }
    else if (args.size() > 1) {
        if (ec.ec_dry_run) {
            return "";
        }
        textview_curses *tc = lnav_data.ld_view_stack.back();

        if (tc != &lnav_data.ld_views[LNV_LOG]) {
            return "error: The :tag command only works in the log view";
        }
        logfile_sub_source &lss = lnav_data.ld_log_source;
        std::map<content_line_t, bookmark_metadata> &bm = lss.get_user_bookmark_metadata();

        tc->set_user_mark(&textview_curses::BM_META, tc->get_top(), true);
        bookmark_metadata &line_meta = bm[lss.at(tc->get_top())];
        for (int lpc = 1; lpc < args.size(); lpc++) {
            string tag = args[lpc];

            if (!startswith(tag, "#")) {
                tag = "#" + tag;
            }
            bookmark_metadata::KNOWN_TAGS.insert(tag);
            line_meta.add_tag(tag);
        }

        retval = "info: tag(s) added to line";
    }

    return retval;
}

static string com_untag(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting one or more tags";

    if (args.empty()) {
        args.emplace_back("line-tags");
        return "";
    }
    else if (args.size() > 1) {
        if (ec.ec_dry_run) {
            return "";
        }
        textview_curses *tc = lnav_data.ld_view_stack.back();

        if (tc != &lnav_data.ld_views[LNV_LOG]) {
            return "error: The :untag command only works in the log view";
        }
        logfile_sub_source &lss = lnav_data.ld_log_source;
        std::map<content_line_t, bookmark_metadata> &bm = lss.get_user_bookmark_metadata();

        auto iter = bm.find(lss.at(tc->get_top()));
        if (iter != bm.end()) {
            bookmark_metadata &line_meta = iter->second;

            for (int lpc = 1; lpc < args.size(); lpc++) {
                string tag = args[lpc];

                if (!startswith(tag, "#")) {
                    tag = "#" + tag;
                }
                line_meta.remove_tag(tag);
            }
            if (line_meta.empty()) {
                tc->set_user_mark(&textview_curses::BM_META, tc->get_top(), false);
            }
        }

        retval = "info: tag(s) removed from line";
    }

    return retval;
}

static string com_delete_tags(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting one or more tags";

    if (args.empty()) {
        args.emplace_back("tag");
        return "";
    }
    else if (args.size() > 1) {
        if (ec.ec_dry_run) {
            return "";
        }
        textview_curses *tc = lnav_data.ld_view_stack.back();

        if (tc != &lnav_data.ld_views[LNV_LOG]) {
            return "error: The :delete-tag command only works in the log view";
        }

        set<string> &known_tags = bookmark_metadata::KNOWN_TAGS;
        vector<string> tags;

        for (int lpc = 1; lpc < args.size(); lpc++) {
            string tag = args[lpc];

            if (!startswith(tag, "#")) {
                tag = "#" + tag;
            }
            if (known_tags.find(tag) == known_tags.end()) {
                return "error: Unknown tag -- " + tag;
            }

            tags.emplace_back(tag);
            known_tags.erase(tag);
        }

        logfile_sub_source &lss = lnav_data.ld_log_source;
        bookmark_vector<vis_line_t> &vbm = tc->get_bookmarks()[&textview_curses::BM_META];
        std::map<content_line_t, bookmark_metadata> &bm = lss.get_user_bookmark_metadata();

        for (auto iter = vbm.begin(); iter != vbm.end();) {
            content_line_t cl = lss.at(*iter);
            auto line_meta = bm.find(cl);

            if (line_meta == bm.end()) {
                ++iter;
                continue;
            }

            for (const auto &tag : tags) {
                line_meta->second.remove_tag(tag);
            }

            if (line_meta->second.empty()) {
                size_t off = distance(vbm.begin(), iter);

                tc->set_user_mark(&textview_curses::BM_META, *iter, false);
                iter = next(vbm.begin(), off);
            } else {
                ++iter;
            }
        }

        retval = "info: deleted tag(s)";
    }

    return retval;
}

static string com_partition_name(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting partition name";

    if (args.empty()) {
        return "";
    }
    else if (args.size() > 1) {
        if (ec.ec_dry_run) {
            retval = "";
        }
        else {
            textview_curses &tc = lnav_data.ld_views[LNV_LOG];
            logfile_sub_source &lss = lnav_data.ld_log_source;
            std::map<content_line_t, bookmark_metadata> &bm = lss.get_user_bookmark_metadata();

            args[1] = trim(remaining_args(cmdline, args));

            tc.set_user_mark(&textview_curses::BM_META, tc.get_top(), true);

            bookmark_metadata &line_meta = bm[lss.at(tc.get_top())];

            line_meta.bm_name = args[1];
            retval = "info: name set for partition";
        }
    }

    return retval;
}

static string com_clear_partition(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval;

    if (args.empty()) {
        return "";
    }
    else if (args.size() == 1) {
        textview_curses &tc = lnav_data.ld_views[LNV_LOG];
        logfile_sub_source &lss = lnav_data.ld_log_source;
        bookmark_vector<vis_line_t> &bv = tc.get_bookmarks()[
            &textview_curses::BM_META];
        std::map<content_line_t, bookmark_metadata> &bm = lss.get_user_bookmark_metadata();
        vis_line_t part_start;

        if (binary_search(bv.begin(), bv.end(), tc.get_top())) {
            part_start = tc.get_top();
        }
        else {
            part_start = bv.prev(tc.get_top());
        }
        if (part_start == -1) {
            retval = "error: top line is not in a partition";
        }
        else if (!ec.ec_dry_run) {
            content_line_t cl = lss.at(part_start);
            bookmark_metadata &line_meta = bm[cl];

            line_meta.bm_name.clear();
            if (line_meta.empty()) {
                tc.set_user_mark(&textview_curses::BM_META, part_start, false);
            }

            retval = "info: cleared partition name";
        }
    }

    return retval;
}

static string com_pt_time(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting a time value";

    if (args.empty()) {
        args.emplace_back("move-time");
        retval = "";
    }
    else if (args.size() == 1) {
        char ftime[64];

        if (args[0] == "pt-min-time") {
            if (lnav_data.ld_pt_min_time == 0) {
                retval = "info: minimum time is not set, pass a time value to this command to set it";
            }
            else {
                ctime_r(&lnav_data.ld_pt_min_time, ftime);
                retval = "info: papertrail minimum time is " + string(ftime);
            }
        }
        if (args[0] == "pt-max-time") {
            if (lnav_data.ld_pt_max_time == 0) {
                retval = "info: maximum time is not set, pass a time value to this command to set it";
            }
            else {
                ctime_r(&lnav_data.ld_pt_max_time, ftime);
                retval = "info: papertrail maximum time is " + string(ftime);
            }
        }
    }
    else if (args.size() >= 2) {
        string all_args = remaining_args(cmdline, args);
        struct timeval new_time = { 0, 0 };
        relative_time rt;
        struct relative_time::parse_error pe;
        date_time_scanner dts;
        struct exttm tm;
        time_t now;

        time(&now);
        dts.dts_keep_base_tz = true;
        dts.set_base_time(now);
        if (rt.parse(all_args, pe)) {
            tm.et_tm = *gmtime(&now);
            rt.add(tm);
            new_time.tv_sec = timegm(&tm.et_tm);
        }
        else {
            dts.scan(args[1].c_str(), args[1].size(), NULL, &tm, new_time);
        }
        if (ec.ec_dry_run) {
            retval = "";
        }
        else if (new_time.tv_sec != 0) {
            if (args[0] == "pt-min-time") {
                lnav_data.ld_pt_min_time = new_time.tv_sec;
                retval = refresh_pt_search();
            }
            if (args[0] == "pt-max-time") {
                lnav_data.ld_pt_max_time = new_time.tv_sec;
                retval = refresh_pt_search();
            }
        }
    }

    return retval;
}

static string com_summarize(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval;

    if (args.empty()) {
        args.emplace_back("colname");
        return retval;
    }
    else if (!setup_logline_table()) {
        retval = "error: no log data available";
    }
    else if (args.size() == 1) {
        retval = "error: no columns specified";
    }
    else {
        auto_mem<char, sqlite3_free> query_frag;
        std::vector<string>          other_columns;
        std::vector<string>          num_columns;
        sql_progress_guard progress_guard(sql_progress,
                                          ec.ec_source.top().first,
                                          ec.ec_source.top().second);
        auto_mem<sqlite3_stmt> stmt(sqlite3_finalize);
        int retcode;
        string query;

        query = "SELECT ";
        for (size_t lpc = 1; lpc < args.size(); lpc++) {
            if (lpc > 1)
                query += ", ";
            query += args[lpc];
        }
        query += " FROM logline ";

        retcode = sqlite3_prepare_v2(lnav_data.ld_db.in(),
                                     query.c_str(),
                                     -1,
                                     stmt.out(),
                                     NULL);
        if (retcode != SQLITE_OK) {
            const char *errmsg = sqlite3_errmsg(lnav_data.ld_db);

            return "error: " + string(errmsg);
        }

        switch (sqlite3_step(stmt.in())) {
            case SQLITE_OK:
            case SQLITE_DONE:
            {
                return "error: no data";
            }
            break;
            case SQLITE_ROW:
            break;
            default:
            {
                const char *errmsg;

                errmsg = sqlite3_errmsg(lnav_data.ld_db);
                return "error: " + string(errmsg);
            }
            break;
        }

        if (ec.ec_dry_run) {
            return "";
        }

        for (int lpc = 0; lpc < sqlite3_column_count(stmt.in()); lpc++) {
            switch (sqlite3_column_type(stmt.in(), lpc)) {
            case SQLITE_INTEGER:
            case SQLITE_FLOAT:
                num_columns.push_back(args[lpc + 1]);
                break;
            default:
                other_columns.push_back(args[lpc + 1]);
                break;
            }
        }

        query = "SELECT";
        for (std::vector<string>::iterator iter = other_columns.begin();
             iter != other_columns.end();
             ++iter) {
            if (iter != other_columns.begin()) {
                query += ",";
            }
            query_frag = sqlite3_mprintf(" %s as \"c_%s\", count(*) as \"count_%s\"",
                                         iter->c_str(),
                                         iter->c_str(),
                                         iter->c_str());
            query += query_frag;
        }

        if (!other_columns.empty() && !num_columns.empty()) {
            query += ", ";
        }

        for (std::vector<string>::iterator iter = num_columns.begin();
             iter != num_columns.end();
             ++iter) {
            if (iter != num_columns.begin()) {
                query += ",";
            }
            query_frag = sqlite3_mprintf(" sum(\"%s\"), "
                                         " min(\"%s\"), "
                                         " avg(\"%s\"), "
                                         " median(\"%s\"), "
                                         " stddev(\"%s\"), "
                                         " max(\"%s\") ",
                                         iter->c_str(),
                                         iter->c_str(),
                                         iter->c_str(),
                                         iter->c_str(),
                                         iter->c_str(),
                                         iter->c_str());
            query += query_frag;
        }

        query += (
            " FROM logline "
                "WHERE (logline.log_part is null or "
                "startswith(logline.log_part, '.') = 0) ");

        for (std::vector<string>::iterator iter = other_columns.begin();
             iter != other_columns.end();
             ++iter) {
            if (iter == other_columns.begin()) {
                query += " GROUP BY ";
            }
            else{
                query += ",";
            }
            query_frag = sqlite3_mprintf(" \"c_%s\"", iter->c_str());
            query     += query_frag;
        }

        for (std::vector<string>::iterator iter = other_columns.begin();
             iter != other_columns.end();
             ++iter) {
            if (iter == other_columns.begin()) {
                query += " ORDER BY ";
            }
            else{
                query += ",";
            }
            query_frag = sqlite3_mprintf(" \"count_%s\" desc, \"c_%s\" collate naturalnocase asc",
                                         iter->c_str(),
                                         iter->c_str());
            query += query_frag;
        }
        log_debug("query %s", query.c_str());

        db_label_source &dls = lnav_data.ld_db_row_source;

        dls.clear();
        retcode = sqlite3_prepare_v2(lnav_data.ld_db.in(),
                                     query.c_str(),
                                     -1,
                                     stmt.out(),
                                     NULL);

        if (retcode != SQLITE_OK) {
            const char *errmsg = sqlite3_errmsg(lnav_data.ld_db);

            retval = "error: " + string(errmsg);
        }
        else if (stmt == NULL) {
            retval = "";
        }
        else {
            bool done = false;

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

                default:
                {
                    const char *errmsg;

                    errmsg = sqlite3_errmsg(lnav_data.ld_db);
                    retval = "error: " + string(errmsg);
                    done   = true;
                }
                break;
                }
            }

            if (retcode == SQLITE_DONE) {
                lnav_data.ld_views[LNV_LOG].reload_data();
                lnav_data.ld_views[LNV_DB].reload_data();
                lnav_data.ld_views[LNV_DB].set_left(0);

                if (dls.dls_rows.size() > 0) {
                    ensure_view(&lnav_data.ld_views[LNV_DB]);
                }
            }

            lnav_data.ld_bottom_source.update_loading(0, 0);
            lnav_data.ld_status[LNS_BOTTOM].do_update();
        }
    }

    return retval;
}

static string com_add_test(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval;

    if (args.empty()) {}
    else if (args.size() > 1) {
        retval = "error: not expecting any arguments";
    }
    else if (ec.ec_dry_run) {

    }
    else {
        textview_curses *tc = lnav_data.ld_view_stack.back();

        bookmark_vector<vis_line_t> &bv =
            tc->get_bookmarks()[&textview_curses::BM_USER];
        bookmark_vector<vis_line_t>::iterator iter;

        for (iter = bv.begin(); iter != bv.end(); ++iter) {
            auto_mem<FILE> file(fclose);
            char           path[PATH_MAX];
            string         line;

            tc->grep_value_for_line(*iter, line);

            line.insert(0, 13, ' ');

            snprintf(path, sizeof(path),
                     "%s/test/log-samples/sample-%s.txt",
                     getenv("LNAV_SRC"),
                     hash_string(line).c_str());

            if ((file = fopen(path, "w")) == NULL) {
                perror("fopen failed");
            }
            else {
                fprintf(file, "%s\n", line.c_str());
            }
        }
    }

    return retval;
}

static string com_switch_to_view(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval;

    if (args.empty()) {
        args.emplace_back("viewname");
    }
    else if (args.size() > 1) {
        bool found = false;

        for (int lpc = 0; lnav_view_strings[lpc] && !found; lpc++) {
            if (strcasecmp(args[1].c_str(), lnav_view_strings[lpc]) == 0) {
                if (!ec.ec_dry_run) {
                    ensure_view(&lnav_data.ld_views[lpc]);
                }
                found = true;
            }
        }

        if (!found) {
            retval = "error: invalid view name -- " + args[1];
        }
    }

    return retval;
}

static string com_zoom_to(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval;

    if (args.empty()) {
        args.emplace_back("zoomlevel");
    }
    else if (ec.ec_dry_run) {

    }
    else if (args.size() > 1) {
        bool found = false;

        for (int lpc = 0; lnav_zoom_strings[lpc] && !found; lpc++) {
            if (strcasecmp(args[1].c_str(), lnav_zoom_strings[lpc]) == 0) {
                spectrogram_source &ss = lnav_data.ld_spectro_source;
                time_t old_time;

                lnav_data.ld_zoom_level = lpc;

                textview_curses &hist_view = lnav_data.ld_views[LNV_HISTOGRAM];

                if (hist_view.get_inner_height() > 0) {
                    old_time = lnav_data.ld_hist_source2.time_for_row(
                        lnav_data.ld_views[LNV_HISTOGRAM].get_top());
                    rebuild_hist();
                    lnav_data.ld_views[LNV_HISTOGRAM].set_top(
                        vis_line_t(
                            lnav_data.ld_hist_source2.row_for_time(old_time)));
                }

                textview_curses &spectro_view = lnav_data.ld_views[LNV_SPECTRO];

                if (spectro_view.get_inner_height() > 0) {
                    old_time = lnav_data.ld_spectro_source.time_for_row(
                        lnav_data.ld_views[LNV_SPECTRO].get_top());
                    ss.ss_granularity = ZOOM_LEVELS[lnav_data.ld_zoom_level];
                    ss.invalidate();
                    lnav_data.ld_views[LNV_SPECTRO].set_top(
                        vis_line_t(lnav_data.ld_spectro_source.row_for_time(
                            old_time)));
                }

                if (!lnav_data.ld_view_stack.empty()) {
                    lnav_data.ld_view_stack.back()->set_needs_update();
                }

                found = true;
            }
        }

        if (!found) {
            retval = "error: invalid zoom level -- " + args[1];
        }
    }

    return retval;
}

static string com_reset_session(exec_context &ec, string cmdline, vector<string> &args)
{
    if (args.empty()) {

    }
    else if (!ec.ec_dry_run) {
        reset_session();
        lnav_data.ld_views[LNV_LOG].reload_data();
    }

    return "";
}

static string com_load_session(exec_context &ec, string cmdline, vector<string> &args)
{
    if (args.empty()) {

    }
    else if (!ec.ec_dry_run) {
        scan_sessions();
        load_session();
        lnav_data.ld_views[LNV_LOG].reload_data();
    }

    return "";
}

static string com_save_session(exec_context &ec, string cmdline, vector<string> &args)
{
    if (args.empty()) {

    }
    else if (!ec.ec_dry_run) {
        save_session();
    }

    return "";
}

static string com_set_min_log_level(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting log level name";

    if (args.empty()) {
        args.emplace_back("levelname");
    }
    else if (ec.ec_dry_run) {
        retval = "";
    }
    else if (args.size() == 2) {
        logfile_sub_source &lss = lnav_data.ld_log_source;
        logline::level_t new_level;

        new_level = logline::string2level(
            args[1].c_str(), args[1].size(), false);
        lss.set_min_log_level(new_level);
        rebuild_indexes(true);

        retval = ("info: minimum log level is now -- " +
            string(logline::level_names[new_level]));
    }

    return retval;
}

static string com_toggle_field(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval;

    if (args.empty()) {
        args.emplace_back("colname");
    } else if (args.size() < 2) {
        retval = "error: Expecting a log message field name";
    } else {
        textview_curses *tc = lnav_data.ld_view_stack.back();

        if (tc != &lnav_data.ld_views[LNV_LOG]) {
            retval = "error: hiding fields only works in the log view";
        } else if (ec.ec_dry_run) {
            // TODO: highlight the fields to be hidden.
            retval = "";
        } else {
            logfile_sub_source &lss = lnav_data.ld_log_source;
            bool hide = args[0] == "hide-fields";
            vector<string> found_fields, missing_fields;

            for (int lpc = 1; lpc < (int)args.size(); lpc++) {
                intern_string_t name;
                log_format *format = nullptr;
                size_t dot;

                if ((dot = args[lpc].find('.')) != string::npos) {
                    const intern_string_t format_name = intern_string::lookup(args[lpc].c_str(), dot);

                    format = log_format::find_root_format(format_name.get());
                    if (!format) {
                        return "error: unknown format -- " + format_name.to_string();
                    }
                    name = intern_string::lookup(&(args[lpc].c_str()[dot + 1]), args[lpc].length() - dot - 1);
                } else if (tc->get_inner_height() == 0) {
                    return "error: no log messages to hide";
                } else {
                    content_line_t cl = lss.at(tc->get_top());
                    std::shared_ptr<logfile> lf = lss.find(cl);
                    format = lf->get_format();
                    name = intern_string::lookup(args[lpc]);
                }

                if (format->hide_field(name, hide)) {
                    found_fields.push_back(args[lpc]);
                    if (hide) {
                        if (lnav_data.ld_rl_view != NULL) {
                            lnav_data.ld_rl_view->set_alt_value(
                                HELP_MSG_1(x, "to quickly show hidden fields"));
                        }
                    }
                    tc->set_needs_update();
                } else {
                    missing_fields.push_back(args[lpc]);
                }
            }

            if (missing_fields.empty()) {
                string all_fields = join(found_fields.begin(),
                                         found_fields.end(),
                                         ", ");

                if (hide) {
                    retval = "info: hiding field(s) -- " + all_fields;
                } else {
                    retval = "info: showing field(s) -- " + all_fields;
                }
            } else {
                string all_fields = join(missing_fields.begin(),
                                         missing_fields.end(),
                                         ", ");

                retval = "error: unknown field(s) -- " + all_fields;
            }
        }
    }

    return retval;
}

static string com_hide_line(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval;

    if (args.empty()) {
        args.emplace_back("move-time");
    }
    else if (args.size() == 1) {
        textview_curses *tc = lnav_data.ld_view_stack.back();
        logfile_sub_source &lss = lnav_data.ld_log_source;

        if (tc == &lnav_data.ld_views[LNV_LOG]) {
            struct timeval min_time, max_time;
            bool have_min_time = lss.get_min_log_time(min_time);
            bool have_max_time = lss.get_max_log_time(max_time);
            char min_time_str[32], max_time_str[32];

            sql_strftime(min_time_str, sizeof(min_time_str), min_time);
            sql_strftime(max_time_str, sizeof(max_time_str), max_time);
            if (have_min_time && have_max_time) {
                retval = "info: hiding lines before " +
                        string(min_time_str) +
                        " and after " +
                        string(max_time_str);
            }
            else if (have_min_time) {
                retval = "info: hiding lines before " + string(min_time_str);
            }
            else if (have_max_time) {
                retval = "info: hiding lines after " + string(max_time_str);
            }
            else {
                retval = "info: no lines hidden by time, pass an absolute or relative time";
            }
        }
        else {
            retval = "error: hiding lines by time only works in the log view";
        }
    }
    else if (args.size() >= 2) {
        string all_args = remaining_args(cmdline, args);
        textview_curses *tc = lnav_data.ld_view_stack.back();
        logfile_sub_source &lss = lnav_data.ld_log_source;
        date_time_scanner dts;
        struct timeval tv;
        relative_time rt;
        struct relative_time::parse_error pe;
        bool tv_set = false;

        if (rt.parse(all_args, pe)) {
            if (tc == &lnav_data.ld_views[LNV_LOG]) {
                content_line_t cl;
                struct exttm tm;
                vis_line_t vl;
                logline *ll;

                vl = tc->get_top();
                cl = lnav_data.ld_log_source.at(vl);
                ll = lnav_data.ld_log_source.find_line(cl);
                ll->to_exttm(tm);
                rt.add(tm);

                tv.tv_sec = timegm(&tm.et_tm);
                tv.tv_usec = tm.et_nsec / 1000;

                tv_set = true;
            }
            else {
                retval = "error: relative time values only work in the log view";
            }
        }
        else if (dts.convert_to_timeval(all_args, tv)) {
            if (tc == &lnav_data.ld_views[LNV_LOG]) {
                tv_set = true;
            }
            else {
                retval = "error: time values only work in the log view";
            }
        }

        if (tv_set && !ec.ec_dry_run) {
            char time_text[256];
            string relation;

            sql_strftime(time_text, sizeof(time_text), tv);
            if (args[0] == "hide-lines-before") {
                lss.set_min_log_time(tv);
                relation = "before";
            }
            else {
                lss.set_max_log_time(tv);
                relation = "after";
            }
            rebuild_indexes(true);

            retval = "info: hiding lines " + relation + " " + time_text;
        }
    }

    return retval;
}

static string com_show_lines(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "info: showing lines";

    if (ec.ec_dry_run) {
        retval = "";
    }
    else if (!args.empty()) {
        logfile_sub_source &lss = lnav_data.ld_log_source;
        textview_curses *tc = lnav_data.ld_view_stack.back();

        if (tc == &lnav_data.ld_views[LNV_LOG]) {
            lss.clear_min_max_log_times();
        }

        rebuild_indexes(true);
    }

    return retval;
}

static string com_hide_unmarked(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "info: hid unmarked lines";

    if (ec.ec_dry_run) {
        retval = "";
    } else {
        lnav_data.ld_log_source.set_marked_only(true);
    }

    return retval;
}

static string com_show_unmarked(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "info: showing unmarked lines";

    if (ec.ec_dry_run) {
        retval = "";
    } else {
        lnav_data.ld_log_source.set_marked_only(false);
    }

    return retval;
}

static string com_rebuild(exec_context &ec, string cmdline, vector<string> &args)
{
    if (args.empty()) {

    }
    else if (!ec.ec_dry_run) {
        rebuild_indexes(false);
    }

    return "";
}

static string com_shexec(exec_context &ec, string cmdline, vector<string> &args)
{
    if (args.empty()) {

    }
    else if (!ec.ec_dry_run) {
        log_perror(system(cmdline.substr(args[0].size()).c_str()));
    }

    return "";
}

static string com_poll_now(exec_context &ec, string cmdline, vector<string> &args)
{
    if (args.empty()) {

    }
    else if (!ec.ec_dry_run) {
        lnav_data.ld_curl_looper.process_all();
    }

    return "";
}

static string com_redraw(exec_context &ec, string cmdline, vector<string> &args)
{
    if (args.empty()) {

    }
    else if (ec.ec_dry_run) {

    }
    else if (lnav_data.ld_window) {
        redrawwin(lnav_data.ld_window);
    }

    return "";
}

static string com_echo(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting a message";

    if (args.empty()) {

    }
    else if (args.size() >= 1) {
        bool lf = true;

        if (args.size() > 2 && args[1] == "-n") {
            string::size_type index_in_cmdline = cmdline.find(args[1]);

            lf = false;
            retval = cmdline.substr(index_in_cmdline + args[1].length() + 1);
        }
        else if (args.size() >= 2) {
            retval = cmdline.substr(args[0].length() + 1);
        }
        else {
            retval = "";
        }

        if (ec.ec_dry_run) {
            lnav_data.ld_preview_status_source.get_description()
                .set_value("The text to output:");
            lnav_data.ld_preview_source.replace_with(attr_line_t(retval));
            retval = "";
        }
        else if (!lnav_data.ld_output_stack.empty()) {
            FILE *outfile = lnav_data.ld_output_stack.top();

            if (outfile == stdout) {
                lnav_data.ld_stdout_used = true;
            }

            fprintf(outfile, "%s", retval.c_str());
            if (lf) {
                putc('\n', outfile);
            }
            fflush(outfile);
        }
    }

    return retval;
}

static string com_alt_msg(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting a message";

    if (args.empty()) {

    }
    else if (ec.ec_dry_run) {
        retval = "";
    }
    else if (args.size() == 1) {
        if (lnav_data.ld_rl_view != NULL) {
            lnav_data.ld_rl_view->set_alt_value("");
        }
        retval = "";
    }
    else {
        string msg = remaining_args(cmdline, args);

        if (lnav_data.ld_rl_view != NULL) {
            lnav_data.ld_rl_view->set_alt_value(msg);
        }

        retval = "";
    }

    return retval;
}

static string com_eval(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting a command or query to evaluate";

    if (args.empty()) {
        args.emplace_back("*");
    }
    else if (args.size() > 1) {
        string all_args = remaining_args(cmdline, args);
        string expanded_cmd;
        shlex lexer(all_args.c_str(), all_args.size());

        log_debug("Evaluating: %s", all_args.c_str());
        if (!lexer.eval(expanded_cmd, {
            &ec.ec_local_vars.top(),
            &ec.ec_global_vars,
        })) {
            return "error: invalid arguments";
        }
        log_debug("Expanded command to evaluate: %s", expanded_cmd.c_str());

        if (expanded_cmd.empty()) {
            return "error: empty result after evaluation";
        }

        if (ec.ec_dry_run) {
            attr_line_t al(expanded_cmd);

            lnav_data.ld_preview_status_source.get_description()
                .set_value("The command to be executed:");

            lnav_data.ld_preview_source.replace_with(al);

            return "";
        }

        string alt_msg;
        switch (expanded_cmd[0]) {
            case ':':
                retval = execute_command(ec, expanded_cmd.substr(1));
                break;
            case ';':
                retval = execute_sql(ec, expanded_cmd.substr(1), alt_msg);
                break;
            case '|':
                retval = "info: executed file -- " + expanded_cmd.substr(1) +
                        " -- " + execute_file(ec, expanded_cmd.substr(1));
                break;
            default:
                retval = "error: expecting argument to start with ':', ';', "
                         "or '|' to signify a command, SQL query, or script to execute";
                break;
        }
    }

    return retval;
}

static string com_config(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting a configuration option to read or write";

    if (args.empty()) {
        args.emplace_back("config-option");
    }
    else if (args.size() > 1) {
        yajlpp_parse_context ypc("input", lnav_config_handlers);
        string option = args[1];

        ypc.set_path(option)
           .with_obj(lnav_config);
        ypc.ypc_active_paths.insert(option);
        ypc.update_callbacks();

        if (ypc.ypc_current_handler != NULL) {
            if (args.size() == 2) {
                auto_mem<yajl_gen_t> handle(yajl_gen_free);

                handle = yajl_gen_alloc(NULL);

                const json_path_handler_base *jph = ypc.ypc_current_handler;
                yajlpp_gen_context ygc(handle, lnav_config_handlers);
                ygc.with_context(ypc);

                jph->gen(ygc, handle);

                const unsigned char *buffer;
                size_t len;

                yajl_gen_get_buf(handle, &buffer, &len);

                retval = "info: " + option + " = " + string((char *) buffer, len);
            }
            else {
                string value = remaining_args(cmdline, args, 2);

                if (ypc.ypc_current_handler->jph_callbacks.yajl_string) {
                    if (ec.ec_dry_run) {
                        retval = "";
                    } else {
                        ypc.ypc_callbacks.yajl_string(
                            &ypc, (const unsigned char *) value.c_str(),
                            value.size());
                        retval = "info: changed config option -- " + option;
                    }
                }
                else if (ypc.ypc_current_handler->jph_callbacks.yajl_boolean) {
                    if (ec.ec_dry_run) {
                        retval = "";
                    } else {
                        bool bvalue = false;

                        if (strcasecmp(value.c_str(), "true") == 0) {
                            bvalue = true;
                        }
                        ypc.ypc_callbacks.yajl_boolean(&ypc, bvalue);
                        retval = "info: changed config option -- " + option;
                    }
                }
                else {
                    retval = "error: unhandled type";
                }

                reload_config();
            }
        }
        else {
            retval = "error: unknown configuration option -- " + option;
        }
    }

    return retval;
}

static string com_save_config(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval;

    if (args.empty()) {
    }
    else if (!ec.ec_dry_run) {
        retval = save_config();
    }

    return retval;
}

static string com_reset_config(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting a configuration option to reset";

    if (args.empty()) {
        args.emplace_back("config-option");
    }
    else if (!ec.ec_dry_run) {
        yajlpp_parse_context ypc("input", lnav_config_handlers);
        string option = args[1];

        ypc.set_path(option)
           .with_obj(lnav_config);
        ypc.ypc_active_paths.insert(option);
        ypc.update_callbacks();

        if (option == "*" || ypc.ypc_current_handler != NULL) {
            reset_config(option);
            if (option == "*") {
                retval = "info: reset all options";
            }
            else {
                retval = "info: reset option";
            }
        }
        else {
            retval = "error: unknown configuration option -- " + option;
        }
    }

    return retval;
}

class log_spectro_value_source : public spectrogram_value_source {
public:
    log_spectro_value_source(intern_string_t colname)
        : lsvs_colname(colname),
          lsvs_begin_time(0),
          lsvs_end_time(0),
          lsvs_found(false) {
        this->update_stats();
    };

    void update_stats() {
        logfile_sub_source &lss = lnav_data.ld_log_source;
        logfile_sub_source::iterator iter;

        this->lsvs_begin_time = 0;
        this->lsvs_end_time = 0;
        this->lsvs_stats.clear();
        for (iter = lss.begin(); iter != lss.end(); iter++) {
            std::shared_ptr<logfile> lf = (*iter)->get_file();

            if (lf == NULL) {
                continue;
            }

            log_format *format = lf->get_format();
            const logline_value_stats *stats = format->stats_for_value(this->lsvs_colname);

            if (stats == NULL) {
                continue;
            }

            logfile::iterator ll = lf->begin();

            if (this->lsvs_begin_time == 0 || ll->get_time() < this->lsvs_begin_time) {
                this->lsvs_begin_time = ll->get_time();
            }
            ll = lf->end();
            --ll;
            if (ll->get_time() > this->lsvs_end_time) {
                this->lsvs_end_time = ll->get_time();
            }

            this->lsvs_found = true;
            this->lsvs_stats.merge(*stats);
        }

        if (this->lsvs_begin_time) {
            time_t filtered_begin_time = lss.find_line(lss.at(0_vl))->get_time();
            time_t filtered_end_time = lss.find_line(lss.at(vis_line_t(lss.text_line_count() - 1)))->get_time();

            if (filtered_begin_time > this->lsvs_begin_time) {
                this->lsvs_begin_time = filtered_begin_time;
            }
            if (filtered_end_time < this->lsvs_end_time) {
                this->lsvs_end_time = filtered_end_time;
            }
        }
    };

    void spectro_bounds(spectrogram_bounds &sb_out) {
        logfile_sub_source &lss = lnav_data.ld_log_source;

        if (lss.text_line_count() == 0) {
            return;
        }

        this->update_stats();

        sb_out.sb_begin_time = this->lsvs_begin_time;
        sb_out.sb_end_time = this->lsvs_end_time;
        sb_out.sb_min_value_out = this->lsvs_stats.lvs_min_value;
        sb_out.sb_max_value_out = this->lsvs_stats.lvs_max_value;
        sb_out.sb_count = this->lsvs_stats.lvs_count;
    };

    void spectro_row(spectrogram_request &sr, spectrogram_row &row_out) {
        logfile_sub_source &lss = lnav_data.ld_log_source;
        vis_line_t begin_line = lss.find_from_time(sr.sr_begin_time);
        vis_line_t end_line = lss.find_from_time(sr.sr_end_time);
        vector<logline_value> values;
        string_attrs_t sa;

        if (begin_line == -1) {
            begin_line = 0_vl;
        }
        if (end_line == -1) {
            end_line = vis_line_t(lss.text_line_count());
        }
        for (vis_line_t curr_line = begin_line; curr_line < end_line; ++curr_line) {
            content_line_t cl = lss.at(curr_line);
            std::shared_ptr<logfile> lf = lss.find(cl);
            logfile::iterator ll = lf->begin() + cl;
            log_format *format = lf->get_format();
            shared_buffer_ref sbr;

            if (ll->is_continued()) {
                continue;
            }

            lf->read_full_message(ll, sbr);
            sa.clear();
            values.clear();
            format->annotate(sbr, sa, values);

            vector<logline_value>::iterator lv_iter;

            lv_iter = find_if(values.begin(), values.end(),
                              logline_value_cmp(&this->lsvs_colname));

            if (lv_iter != values.end()) {
                switch (lv_iter->lv_kind) {
                    case logline_value::VALUE_FLOAT:
                        row_out.add_value(sr, lv_iter->lv_value.d, ll->is_marked());
                        break;
                    case logline_value::VALUE_INTEGER:
                        row_out.add_value(sr, lv_iter->lv_value.i, ll->is_marked());
                        break;
                    default:
                        break;
                }
            }
        }
    };

    void spectro_mark(textview_curses &tc,
                      time_t begin_time, time_t end_time,
                      double range_min, double range_max) {
        // XXX need to refactor this and the above method
        textview_curses &log_tc = lnav_data.ld_views[LNV_LOG];
        logfile_sub_source &lss = lnav_data.ld_log_source;
        vis_line_t begin_line = lss.find_from_time(begin_time);
        vis_line_t end_line = lss.find_from_time(end_time);
        vector<logline_value> values;
        string_attrs_t sa;

        if (begin_line == -1) {
            begin_line = 0_vl;
        }
        if (end_line == -1) {
            end_line = vis_line_t(lss.text_line_count());
        }
        for (vis_line_t curr_line = begin_line; curr_line < end_line; ++curr_line) {
            content_line_t cl = lss.at(curr_line);
            std::shared_ptr<logfile> lf = lss.find(cl);
            logfile::iterator ll = lf->begin() + cl;
            log_format *format = lf->get_format();
            shared_buffer_ref sbr;

            if (ll->is_continued()) {
                continue;
            }

            lf->read_full_message(ll, sbr);
            sa.clear();
            values.clear();
            format->annotate(sbr, sa, values);

            vector<logline_value>::iterator lv_iter;

            lv_iter = find_if(values.begin(), values.end(),
                              logline_value_cmp(&this->lsvs_colname));

            if (lv_iter != values.end()) {
                switch (lv_iter->lv_kind) {
                    case logline_value::VALUE_FLOAT:
                        if (range_min <= lv_iter->lv_value.d &&
                            lv_iter->lv_value.d <= range_max) {
                            log_tc.toggle_user_mark(&textview_curses::BM_USER,
                                                    curr_line);
                        }
                        break;
                    case logline_value::VALUE_INTEGER:
                        if (range_min <= lv_iter->lv_value.i &&
                            lv_iter->lv_value.i <= range_max) {
                            log_tc.toggle_user_mark(&textview_curses::BM_USER,
                                                    curr_line);
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    };

    intern_string_t lsvs_colname;
    logline_value_stats lsvs_stats;
    time_t lsvs_begin_time;
    time_t lsvs_end_time;
    bool lsvs_found;
};

class db_spectro_value_source : public spectrogram_value_source {
public:
    db_spectro_value_source(string colname)
        : dsvs_colname(colname),
          dsvs_begin_time(0),
          dsvs_end_time(0) {
        this->update_stats();
    };

    void update_stats() {
        this->dsvs_begin_time = 0;
        this->dsvs_end_time = 0;
        this->dsvs_stats.clear();

        db_label_source &dls = lnav_data.ld_db_row_source;
        stacked_bar_chart<string> &chart = dls.dls_chart;
        date_time_scanner dts;

        this->dsvs_column_index = dls.column_name_to_index(this->dsvs_colname);

        if (!dls.has_log_time_column()) {
            this->dsvs_error_msg = "no 'log_time' column found or not in ascending order, unable to create spectrogram";
            return;
        }

        if (this->dsvs_column_index == -1) {
            this->dsvs_error_msg = "unknown column -- " + this->dsvs_colname;
            return;
        }

        if (!dls.dls_headers[this->dsvs_column_index].hm_graphable) {
            this->dsvs_error_msg = "column is not numeric -- " + this->dsvs_colname;
            return;
        }

        if (dls.dls_rows.empty()) {
            this->dsvs_error_msg = "empty result set";
            return;
        }

        stacked_bar_chart<string>::bucket_stats_t bs = chart.get_stats_for(this->dsvs_colname);

        this->dsvs_begin_time = dls.dls_time_column.front().tv_sec;
        this->dsvs_end_time = dls.dls_time_column.back().tv_sec;
        this->dsvs_stats.lvs_min_value = bs.bs_min_value;
        this->dsvs_stats.lvs_max_value = bs.bs_max_value;
        this->dsvs_stats.lvs_count = dls.dls_rows.size();
    };

    void spectro_bounds(spectrogram_bounds &sb_out) {
        db_label_source &dls = lnav_data.ld_db_row_source;

        if (dls.text_line_count() == 0) {
            return;
        }

        this->update_stats();

        sb_out.sb_begin_time = this->dsvs_begin_time;
        sb_out.sb_end_time = this->dsvs_end_time;
        sb_out.sb_min_value_out = this->dsvs_stats.lvs_min_value;
        sb_out.sb_max_value_out = this->dsvs_stats.lvs_max_value;
        sb_out.sb_count = this->dsvs_stats.lvs_count;
    };

    void spectro_row(spectrogram_request &sr, spectrogram_row &row_out) {
        db_label_source &dls = lnav_data.ld_db_row_source;
        int begin_row = dls.row_for_time(sr.sr_begin_time);
        int end_row = dls.row_for_time(sr.sr_end_time);

        if (begin_row == -1) {
            begin_row = 0;
        }
        if (end_row == -1) {
            end_row = dls.dls_rows.size();
        }

        for (int lpc = begin_row; lpc < end_row; lpc++) {
            double value = 0.0;

            sscanf(dls.dls_rows[lpc][this->dsvs_column_index], "%lf", &value);

            row_out.add_value(sr, value, false);
        }
    };

    void spectro_mark(textview_curses &tc,
                      time_t begin_time, time_t end_time,
                      double range_min, double range_max) {
    };

    string dsvs_colname;
    logline_value_stats dsvs_stats;
    time_t dsvs_begin_time;
    time_t dsvs_end_time;
    int dsvs_column_index;
    string dsvs_error_msg;
};

static string com_spectrogram(exec_context &ec, string cmdline, vector<string> &args)
{
    string retval = "error: expecting a message field name";

    if (args.empty()) {
        args.emplace_back("numeric-colname");
    }
    else if (ec.ec_dry_run) {
        retval = "";
    }
    else if (args.size() == 2) {
        string colname = remaining_args(cmdline, args);
        spectrogram_source &ss = lnav_data.ld_spectro_source;
        bool found = false;

        ss.ss_granularity = ZOOM_LEVELS[lnav_data.ld_zoom_level];
        if (ss.ss_value_source != NULL) {
            delete ss.ss_value_source;
            ss.ss_value_source = NULL;
        }
        ss.invalidate();

        if (lnav_data.ld_view_stack.back() == &lnav_data.ld_views[LNV_DB]) {
            unique_ptr<db_spectro_value_source> dsvs(
                new db_spectro_value_source(colname));

            if (!dsvs->dsvs_error_msg.empty()) {
                retval = "error: " + dsvs->dsvs_error_msg;
            }
            else {
                ss.ss_value_source = dsvs.release();
                found = true;

            }
        }
        else {
            unique_ptr<log_spectro_value_source> lsvs(
                new log_spectro_value_source(intern_string::lookup(colname)));

            if (!lsvs->lsvs_found) {
                retval = "error: unknown numeric message field -- " + colname;
            }
            else {
                ss.ss_value_source = lsvs.release();
                found = true;
            }
        }

        if (found) {
            ensure_view(&lnav_data.ld_views[LNV_SPECTRO]);

            if (lnav_data.ld_rl_view != NULL) {
                lnav_data.ld_rl_view->set_alt_value(
                    HELP_MSG_2(z, Z, "to zoom in/out"));
            }

            retval = "info: visualizing field -- " + colname;
        }
    }

    return retval;
}

static string com_quit(exec_context &ec, string cmdline, vector<string> &args)
{
    if (args.empty()) {

    }
    else if (!ec.ec_dry_run) {
        lnav_data.ld_looping = false;
    }
    return "";
}

readline_context::command_t STD_COMMANDS[] = {
    {
        "adjust-log-time",
        com_adjust_log_time,

        help_text(":adjust-log-time")
            .with_summary("Change the timestamps of the top file to be relative to the given date")
            .with_parameter(help_text("timestamp", "The new timestamp for the top line in the view")
                                .with_format(HPF_DATETIME))
            .with_example({"2017-01-02T05:33:00", ""})
    },

    {
        "unix-time",
        com_unix_time,

        help_text(":unix-time")
            .with_summary("Convert epoch time to a human-readable form")
            .with_parameter(help_text("seconds", "The epoch timestamp to convert")
                                .with_format(HPF_INTEGER))
            .with_example({"1490191111", "Wed Mar 22 06:58:31 2017  -0700 PDT -- 1490191111"})
    },
    {
        "current-time",
        com_current_time,

        help_text(":current-time")
            .with_summary("Print the current time in human-readable form and seconds since the epoch")
    },
    {
        "goto",
        com_goto,

        help_text(":goto")
            .with_summary("Go to the given location in the top view")
            .with_parameter(help_text("line#|N%|date", "A line number, percent into the file, or a timestamp"))
            .with_examples(
                {
                    {"22"},
                    {"75%"},
                    {"2017-01-01"}
                })
            .with_tags({"navigation"})
    },
    {
        "relative-goto",
        com_relative_goto,

        help_text(":relative-goto")
            .with_summary("Move the current view up or down by the given amount")
            .with_parameter({"line-count|N%", "The amount to move the view by."})
            .with_examples(
                {
                    {"+22"},
                    {"-10%"},
                })
            .with_tags({"navigation"})
    },
    {
        "mark",
        com_mark,

        help_text(":mark")
            .with_summary("Toggle the bookmark state for the top line in the current view")
            .with_tags({"bookmarks"})
    },
    {
        "next-mark",
        com_goto_mark,

        help_text(":next-mark")
            .with_summary("Move to the next bookmark of the given type in the current view")
            .with_parameter(help_text("type", "The type of bookmark -- error, warning, search, user, file, meta"))
            .with_example({"error"})
            .with_tags({"bookmarks", "navigation"})
    },
    {
        "prev-mark",
        com_goto_mark,

        help_text(":prev-mark")
            .with_summary("Move to the previous bookmark of the given type in the current view")
            .with_parameter(help_text("type", "The type of bookmark -- error, warning, search, user, file, meta"))
            .with_example({"error"})
            .with_tags({"bookmarks", "navigation"})
    },
    {
        "help",
        com_help,

        help_text(":help")
            .with_summary("Open the help text view")
    },
    {
        "hide-fields",
        com_toggle_field,

        help_text(":hide-fields")
            .with_summary("Hide log message fields by replacing them with an ellipsis")
            .with_parameter(help_text("field-name",
                                      "The name of the field to hide in the format for the top log line.  "
                                          "A qualified name can be used where the field name is prefixed "
                                          "by the format name and a dot to hide any field.")
                                .one_or_more())
            .with_example({"log_procname"})
            .with_example({"syslog_log.log_procname"})
            .with_tags({"display"})
    },
    {
        "show-fields",
        com_toggle_field,

        help_text(":show-fields")
            .with_summary("Show log message fields that were previously hidden")
            .with_parameter(help_text("field-name", "The name of the field to show")
                                .one_or_more())
            .with_example({"log_procname"})
            .with_opposites({"hide-fields"})
            .with_tags({"display"})
    },
    {
        "hide-lines-before",
        com_hide_line,

        help_text(":hide-lines-before")
            .with_summary("Hide lines that come before the given date")
            .with_parameter(help_text("date", "An absolute or relative date"))
            .with_examples(
                {
                    {"here"},
                    {"6am"},
                })
            .with_tags({"filtering"})
    },
    {
        "hide-lines-after",
        com_hide_line,

        help_text(":hide-lines-after")
            .with_summary("Hide lines that come after the given date")
            .with_parameter(help_text("date", "An absolute or relative date"))
            .with_examples(
                {
                    {"here"},
                    {"6am"},
                })
            .with_tags({"filtering"})
    },
    {
        "show-lines-before-and-after",
        com_show_lines,

        help_text(":show-lines-before-and-after")
            .with_summary("Show lines that were hidden by the 'hide-lines' commands")
            .with_opposites({"hide-lines-before", "hide-lines-after"})
            .with_tags({"filtering"})
    },
    {
        "hide-unmarked-lines",
        com_hide_unmarked,

        help_text(":hide-unmarked-lines")
            .with_summary("Hide lines that have not been bookmarked")
            .with_tags({"filtering", "bookmarks"})
    },
    {
        "show-unmarked-lines",
        com_show_unmarked,

        help_text(":show-unmarked-lines")
            .with_summary("Show lines that have not been bookmarked")
            .with_opposites({"show-unmarked-lines"})
            .with_tags({"filtering", "bookmarks"})
    },
    {
        "highlight",
        com_highlight,

        help_text(":highlight")
            .with_summary("Add coloring to log messages fragments that match the given regular expression")
            .with_parameter(help_text("pattern", "The regular expression to match"))
            .with_tags({"display"})
            .with_example({R"(\d{3,})"})
    },
    {
        "clear-highlight",
        com_clear_highlight,

        help_text(":clear-highlight")
            .with_summary("Remove a previously set highlight regular expression")
            .with_parameter(help_text("pattern", "The regular expression previously used with :highlight"))
            .with_tags({"display"})
            .with_opposites({"highlight"})
            .with_example({"foobar"})
    },
    {
        "filter-in",
        com_filter,

        help_text(":filter-in")
            .with_summary("Only show lines that match the given regular expression in the current view")
            .with_parameter(help_text("pattern", "The regular expression to match"))
            .with_tags({"filtering"})
            .with_example({"dhclient"})
    },
    {
        "filter-out",
        com_filter,

        help_text(":filter-out")
            .with_summary("Remove lines that match the given regular expression in the current view")
            .with_parameter(help_text("pattern", "The regular expression to match"))
            .with_tags({"filtering"})
            .with_example({"last message repeated"})
    },
    {
        "delete-filter",
        com_delete_filter,

        help_text(":delete-filter")
            .with_summary("Delete the filter created with "
                              ANSI_BOLD(":filter-in") " or " ANSI_BOLD(":filter-out"))
            .with_parameter(help_text("pattern", "The regular expression to match"))
            .with_opposites({"filter-in", "filter-out"})
            .with_tags({"filtering"})
            .with_example({"last message repeated"})
    },
    {
        "append-to",
        com_save_to,

        help_text(":append-to")
            .with_summary("Append marked lines in the current view to the given file")
            .with_parameter(help_text("path", "The path to the file to append to"))
            .with_tags({"io"})
            .with_example({"/tmp/interesting-lines.txt"})
    },
    {
        "write-to",
        com_save_to,

        help_text(":write-to")
            .with_summary("Overwrite the given file with any marked lines in the current view")
            .with_parameter(help_text("path", "The path to the file to write"))
            .with_tags({"io", "scripting"})
            .with_example({"/tmp/interesting-lines.txt"})
    },
    {
        "write-csv-to",
        com_save_to,

        help_text(":write-csv-to")
            .with_summary("Write SQL results to the given file in CSV format")
            .with_parameter(help_text("path", "The path to the file to write"))
            .with_tags({"io", "scripting", "sql"})
            .with_example({"/tmp/table.csv"})
    },
    {
        "write-json-to",
        com_save_to,

        help_text(":write-json-to")
            .with_summary("Write SQL results to the given file in JSON format")
            .with_parameter(help_text("path", "The path to the file to write"))
            .with_tags({"io", "scripting", "sql"})
            .with_example({"/tmp/table.json"})
    },
    {
        "write-cols-to",
        com_save_to,

        help_text(":write-cols-to")
            .with_summary("Write SQL results to the given file in a columnar format")
            .with_parameter(help_text("path", "The path to the file to write"))
            .with_tags({"io", "scripting", "sql"})
            .with_example({"/tmp/table.txt"})
    },
    {
        "write-raw-to",
        com_save_to,

        help_text(":write-raw-to")
            .with_summary("Write the displayed text or SQL results to the given file without any formatting")
            .with_parameter(help_text("path", "The path to the file to write"))
            .with_tags({"io", "scripting", "sql"})
            .with_example({"/tmp/table.txt"})
    },
    {
        "pipe-to",
        com_pipe_to,

        help_text(":pipe-to")
            .with_summary("Pipe the marked lines to the given shell command")
            .with_parameter(help_text("shell-cmd", "The shell command-line to execute"))
            .with_tags({"io"})
            .with_example({"sed -e s/foo/bar/g"})
    },
    {
        "pipe-line-to",
        com_pipe_to,

        help_text(":pipe-line-to")
            .with_summary("Pipe the top line to the given shell command")
            .with_parameter(help_text("shell-cmd", "The shell command-line to execute"))
            .with_tags({"io"})
            .with_example({"sed -e 's/foo/bar/g'"})
    },
    {
        "enable-filter",
        com_enable_filter,

        help_text(":enable-filter")
            .with_summary("Enable a previously created and disabled filter")
            .with_parameter(help_text("pattern", "The regular expression used in the filter command"))
            .with_tags({"filtering"})
            .with_opposites({"disable-filter"})
            .with_example({"last message repeated"})
    },
    {
        "disable-filter",
        com_disable_filter,

        help_text(":disable-filter")
            .with_summary("Disable a filter created with filter-in/filter-out")
            .with_parameter(help_text("pattern", "The regular expression used in the filter command"))
            .with_tags({"filtering"})
            .with_opposites({"filter-out", "filter-in"})
            .with_example({"last message repeated"})
    },
    {
        "enable-word-wrap",
        com_enable_word_wrap,

        help_text(":enable-word-wrap")
            .with_summary("Enable word-wrapping for the current view")
            .with_tags({"display"})
    },
    {
        "disable-word-wrap",
        com_disable_word_wrap,

        help_text(":disable-word-wrap")
            .with_summary("Disable word-wrapping for the current view")
            .with_opposites({"enable-word-wrap"})
            .with_tags({"display"})
    },
    {
        "create-logline-table",
        com_create_logline_table,

        help_text(":create-logline-table")
            .with_summary("Create an SQL table using the top line of the log view as a template")
            .with_parameter(help_text("table-name", "The name for the new table"))
            .with_tags({"vtables", "sql"})
            .with_example({"task_durations"})
    },
    {
        "delete-logline-table",
        com_delete_logline_table,

        help_text(":delete-logline-table")
            .with_summary("Delete a table created with create-logline-table")
            .with_parameter(help_text("table-name", "The name of the table to delete"))
            .with_opposites({"delete-logline-table"})
            .with_tags({"vtables", "sql"})
            .with_example({"task_durations"})
    },
    {
        "create-search-table",
        com_create_search_table,

        help_text(":create-search-table")
            .with_summary("Create an SQL table based on a regex search")
            .with_parameter(help_text("table-name", "The name of the table to create"))
            .with_parameter(help_text(
                "pattern",
                "The regular expression used to capture the table columns.  "
                    "If not given, the current search pattern is used.")
                                .optional())
            .with_tags({"vtables", "sql"})
            .with_example({R"(task_durations duration=(?<duration>\d+))"})
    },
    {
        "delete-search-table",
        com_delete_search_table,

        help_text(":delete-search-table")
            .with_summary("Create an SQL table based on a regex search")
            .with_parameter(help_text("table-name", "The name of the table to create"))
            .with_opposites({"create-search-table"})
            .with_tags({"vtables", "sql"})
            .with_example({"task_durations"})
    },
    {
        "open",
        com_open,

        help_text(":open")
            .with_summary(
#ifdef HAVE_LIBCURL
                "Open the given file(s) or URLs in lnav"
#else
                "Open the given file(s) in lnav"
#endif
            )
            .with_parameter(
                help_text{"path", "The path to the file to open"}
                    .one_or_more())
            .with_example({"/path/to/file"})
    },
    {
        "close",
        com_close,

        help_text(":close")
            .with_summary("Close the top file in the view")
            .with_opposites({"open"})
    },
    {
        "comment",
        com_comment,

        help_text(":comment")
            .with_summary("Attach a comment to the top log line")
            .with_parameter(help_text("text", "The comment text"))
            .with_example({"This is where it all went wrong"})
            .with_tags({"metadata"})
    },
    {
        "clear-comment",
        com_clear_comment,

        help_text(":clear-comment")
            .with_summary("Clear the comment attached to the top log line")
            .with_opposites({"comment"})
            .with_tags({"metadata"})
    },
    {
        "tag",
        com_tag,

        help_text(":tag")
            .with_summary("Attach tags to the top log line")
            .with_parameter(help_text("tag", "The tags to attach")
                                .one_or_more())
            .with_example({"#BUG123 #needs-review"})
            .with_tags({"metadata"})
    },
    {
        "untag",
        com_untag,

        help_text(":untag")
            .with_summary("Detach tags from the top log line")
            .with_parameter(help_text("tag", "The tags to detach")
                                .one_or_more())
            .with_example({"#BUG123 #needs-review"})
            .with_opposites({"tag"})
            .with_tags({"metadata"})
    },
    {
        "delete-tags",
        com_delete_tags,

        help_text(":delete-tags")
            .with_summary("Remove the given tags from all log lines")
            .with_parameter(help_text("tag", "The tags to delete")
                                .one_or_more())
            .with_example({"#BUG123 #needs-review"})
            .with_opposites({"tag"})
            .with_tags({"metadata"})
    },
    {
        "partition-name",
        com_partition_name,

        help_text(":partition-name")
            .with_summary("Mark the top line in the log view as the start of a new partition with the given name")
            .with_parameter(help_text("name", "The name for the new partition"))
            .with_example({"reboot"})
    },
    {
        "clear-partition",
        com_clear_partition,

        help_text(":clear-partition")
            .with_summary("Clear the partition the top line is a part of")
            .with_opposites({"partition-name"})
    },
    {
        "pt-min-time",
        com_pt_time,
    },
    {
        "pt-max-time",
        com_pt_time,
    },
    {
        "session",
        com_session,

        help_text(":session")
            .with_summary("Add the given command to the session file (~/.lnav/session)")
            .with_parameter(help_text("lnav-command", "The lnav command to save."))
            .with_example({":highlight foobar"})
    },
    {
        "summarize",
        com_summarize,

        help_text(":summarize")
            .with_summary("Execute a SQL query that computes the characteristics of the values in the given column")
            .with_parameter(help_text("column-name", "The name of the column to analyze."))
            .with_example({"sc_bytes"})
    },
    {
        "switch-to-view",
        com_switch_to_view,

        help_text(":switch-to-view")
            .with_summary("Switch to the given view")
            .with_parameter(help_text("view-name", "The name of the view to switch to."))
            .with_example({"schema"})
    },
    {
        "reset-session",
        com_reset_session,

        help_text(":reset-session")
            .with_summary("Reset the session state, clearing all filters, highlights, and bookmarks")
    },
    {
        "load-session",
        com_load_session,

        help_text(":load-session")
            .with_summary("Load the latest session state")
    },
    {
        "save-session",
        com_save_session,

        help_text(":save-session")
            .with_summary("Save the current state as a session")
    },
    {
        "set-min-log-level",
        com_set_min_log_level,

        help_text(":set-min-log-level")
            .with_summary("Set the minimum log level to display in the log view")
            .with_parameter(help_text("log-level", "The new minimum log level"))
            .with_example({"error"})
    },
    {
        "redraw",
        com_redraw,

        help_text(":redraw")
            .with_summary("Do a full redraw of the screen")
    },
    {
        "zoom-to",
        com_zoom_to,

        help_text(":zoom-to")
            .with_summary("Zoom the histogram view to the given level")
            .with_parameter(help_text("zoom-level", "The zoom level"))
            .with_example({"1-week"})
    },
    {
        "echo",
        com_echo,

        help_text(":echo")
            .with_summary("Echo the given message")
            .with_parameter(help_text("msg", "The message to display"))
            .with_tags({"scripting"})
            .with_example({"Hello, World!"})
    },
    {
        "alt-msg",
        com_alt_msg,

        help_text(":alt-msg")
            .with_summary("Display a message in the alternate command position")
            .with_parameter(help_text("msg", "The message to display"))
            .with_tags({"scripting"})
            .with_example({"Press t to switch to the text view"})
    },
    {
        "eval",
        com_eval,

        help_text(":eval")
            .with_summary(
                "Evaluate the given command/query after doing environment variable substitution")
            .with_parameter(help_text("command",
                                      "The command or query to perform substitution on."))
            .with_tags({"scripting"})
            .with_examples(
                {
                    {":echo $HOME"},
                    {";SELECT * FROM ${table}"}
                })
    },
    {
        "config",
        com_config,

        help_text(":config")
            .with_summary("Read or write a configuration option")
            .with_parameter(help_text("option", "The path to the option to read or write"))
            .with_parameter(help_text("value", "The value to write.  If not given, the current value is returned")
                                .optional())
            .with_example({"/ui/clock-format"})
            .with_tags({"configuration"})
    },
    {
        "save-config",
        com_save_config,

        help_text(":save-config")
            .with_summary("Save the current configuration state")
            .with_tags({"configuration"})
    },
    {
        "reset-config",
        com_reset_config,

        help_text(":reset-config")
            .with_summary("Reset the configuration option to its default value")
            .with_parameter(help_text("option", "The path to the option to reset"))
            .with_example({"/ui/clock-format"})
            .with_tags({"configuration"})
    },
    {
        "spectrogram",
        com_spectrogram,

        help_text(":spectrogram")
            .with_summary("Visualize the given message field using a spectrogram")
            .with_parameter(help_text("field-name", "The name of the numeric field to visualize."))
            .with_example({"sc_bytes"})
    },
    {
        "quit",
        com_quit,

        help_text(":quit")
            .with_summary("Quit lnav")
    },

    { NULL },
};

unordered_map<char const *, vector<char const *>> aliases = {
    { "quit", { "q" } },
};

void init_lnav_commands(readline_context::command_map_t &cmd_map)
{
    for (int lpc = 0; STD_COMMANDS[lpc].c_name != NULL; lpc++) {
        readline_context::command_t &cmd = STD_COMMANDS[lpc];

        cmd.c_help.index_tags();
        cmd_map[cmd.c_name] = &cmd;

        auto itr = aliases.find(cmd.c_name);
        if (itr != aliases.end()) {
            for (char const * alias: itr->second) {
                cmd_map[alias] = &cmd;
            }
        }
    }

    if (getenv("LNAV_SRC") != NULL) {
        static readline_context::command_t add_test;

        add_test = com_add_test;
        cmd_map["add-test"] = &add_test;
    }
    if (getenv("lnav_test") != NULL) {
        static readline_context::command_t rebuild, shexec, poll_now;

        rebuild = com_rebuild;
        cmd_map["rebuild"] = &rebuild;
        shexec = com_shexec;
        cmd_map["shexec"] = &shexec;
        poll_now = com_poll_now;
        cmd_map["poll-now"] = &poll_now;
    }
}
