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

#include <wordexp.h>
#include <sys/stat.h>

#include <string>
#include <vector>
#include <fstream>

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

using namespace std;

static string remaining_args(const string &cmdline,
                             const vector<string> &args,
                             size_t index = 1)
{
    size_t start_pos = 0;

    require(index > 0);

    for (int lpc = 0; lpc < index; lpc++) {
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
    for (list<logfile *>::iterator iter = lnav_data.ld_files.begin();
         iter != lnav_data.ld_files.end();
         ++iter) {
        logfile *lf = *iter;

        if (startswith(lf->get_filename(), "pt:")) {
            lf->close();
        }
    }

    lnav_data.ld_curl_looper.close_request("papertrailapp.com");

    if (lnav_data.ld_pt_search.empty()) {
        return "info: no papertrail query is active";
    }
    auto_ptr<papertrail_proc> pt(new papertrail_proc(
            lnav_data.ld_pt_search.substr(3),
            lnav_data.ld_pt_min_time,
            lnav_data.ld_pt_max_time));
    lnav_data.ld_file_names.insert(
            make_pair(lnav_data.ld_pt_search, pt->copy_fd().release()));
    lnav_data.ld_curl_looper.add_request(pt.release());

    ensure_view(&lnav_data.ld_views[LNV_LOG]);

    retval = "info: opened papertrail query";
#else
    retval = "error: lnav not compiled with libcurl";
#endif

    return retval;
}

static string com_adjust_log_time(string cmdline, vector<string> &args)
{
    string retval = "error: expecting new time value";

    if (args.size() == 0) {
        args.push_back("line-time");
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
        logfile *lf;

        top_line = lnav_data.ld_views[LNV_LOG].get_top();
        top_content = lss.at(top_line);
        lf = lss.find(top_content);

        logline &ll = (*lf)[top_content];

        top_time = ll.get_timeval();

        dts.set_base_time(top_time.tv_sec);
        args[1] = remaining_args(cmdline, args);
        if (dts.scan(args[1].c_str(), args[1].size(), NULL, &tm, new_time) != NULL) {
            timersub(&new_time, &top_time, &time_diff);
            
            lf->adjust_content_time(top_content, time_diff, false);

            rebuild_indexes(true);

            retval = "info: adjusted time";
        }
    }

    return retval;
}

static string com_unix_time(string cmdline, vector<string> &args)
{
    string retval = "error: expecting a unix time value";

    if (args.size() == 0) { }
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

static string com_current_time(string cmdline, vector<string> &args)
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

static string com_goto(string cmdline, vector<string> &args)
{
    string retval = "error: expecting line number/percentage, timestamp, or relative time";

    if (args.size() == 0) {
        args.push_back("move-time");
    }
    else if (args.size() > 1) {
        string all_args = remaining_args(cmdline, args);
        textview_curses *tc = lnav_data.ld_view_stack.top();
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
                tc->set_top(vl);
                retval = "";
                if (!rt.is_absolute() && lnav_data.ld_rl_view != NULL) {
                    lnav_data.ld_rl_view->set_alt_value(
                            HELP_MSG_2(r, R, "to move forward/backward the same amount of time"));
                }
            } else {
                retval = "error: relative time values only work in the log view";
            }
        }
        else if (dts.scan(args[1].c_str(), args[1].size(), NULL, &tm, tv) != NULL) {
            if (tc == &lnav_data.ld_views[LNV_LOG]) {
                vis_line_t vl;

                vl = lnav_data.ld_log_source.find_from_time(tv);
                tc->set_top(vl);
                retval = "";
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
            tc->set_top(vis_line_t(line_number));

            retval = "";
        }
    }

    return retval;
}

static string com_relative_goto(string cmdline, vector<string> &args)
{
    string retval = "error: expecting line number/percentage";

    if (args.size() == 0) {
    }
    else if (args.size() > 1) {
        textview_curses *tc = lnav_data.ld_view_stack.top();
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
            tc->shift_top(vis_line_t(line_offset), true);

            retval = "";
        }
    }

    return retval;
}

static string com_goto_mark(string cmdline, vector<string> &args)
{
    string retval = "";

    if (args.empty()) {
        args.push_back("mark-type");
    }
    else {
        textview_curses *tc = lnav_data.ld_view_stack.top();
        string type_name = "user";

        if (args.size() > 1) {
            type_name = args[1];
        }

        bookmark_type_t *bt = bookmark_type_t::find_type(type_name);
        if (bt == NULL) {
            retval = "error: unknown bookmark type";
        }
        else {
            moveto_cluster(args[0] == "next-mark" ?
                    &bookmark_vector<vis_line_t>::next :
                    &bookmark_vector<vis_line_t>::prev,
                    bt,
                    tc->get_top());
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

    for (size_t col = 0; col < dls.dls_column_types.size(); col++) {
        obj_map.gen(dls.dls_headers[col]);

        if (dls.dls_rows[row][col] == db_label_source::NULL_STR) {
            obj_map.gen();
            continue;
        }

        switch (dls.dls_column_types[col]) {
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

static string com_save_to(string cmdline, vector<string> &args)
{
    FILE *      outfile = NULL;
    const char *mode    = "";
    string fn, retval;
    bool to_term = false;

    if (args.size() == 0) {
        args.push_back("filename");
        return "";
    }

    if (args.size() < 2) {
        return "error: expecting file name or '-' to write to the terminal";
    }

    fn = trim(remaining_args(cmdline, args));

    static_root_mem<wordexp_t, wordfree> wordmem;

    int rc = wordexp(fn.c_str(), wordmem.inout(), WRDE_NOCMD | WRDE_UNDEF);

    if (!wordexperr(rc, retval)) {
        return retval;
    }

    if (wordmem->we_wordc > 1) {
        return "error: more than one file name was matched";
    }

    if (args[0] == "append-to") {
        mode = "a";
    }
    else {
        mode = "w";
    }


    textview_curses *            tc = lnav_data.ld_view_stack.top();
    bookmark_vector<vis_line_t> &bv =
        tc->get_bookmarks()[&textview_curses::BM_USER];
    db_label_source &dls = lnav_data.ld_db_row_source;

    if (args[0] == "write-csv-to" || args[0] == "write-json-to") {
        if (dls.dls_headers.empty()) {
            return "error: no query result to write, use ';' to execute a query";
        }
    }
    else {
        if (bv.empty()) {
            return "error: no lines marked to write, use 'm' to mark lines";
        }
    }

    if (strcmp(wordmem->we_wordv[0], "-") == 0) {
        outfile = stdout;
        if (lnav_data.ld_flags & LNF_HEADLESS) {
            lnav_data.ld_stdout_used = true;
        }
        else {
            nodelay(lnav_data.ld_window, 0);
            endwin();
            struct termios curr_termios;
            tcgetattr(1, &curr_termios);
            curr_termios.c_oflag |= ONLCR|OPOST;
            tcsetattr(1, TCSANOW, &curr_termios);
            setvbuf(stdout, NULL, _IONBF, 0);
            to_term = true;
            fprintf(outfile,
                    "\n---------------- %s output, press any key to exit "
                            "----------------\n\n",
                    args[0].c_str());
        }
    }
    else if ((outfile = fopen(wordmem->we_wordv[0], mode)) == NULL) {
        return "error: unable to open file -- " + string(wordmem->we_wordv[0]);
    }

    if (args[0] == "write-csv-to") {
        std::vector<std::vector<const char *> >::iterator row_iter;
        std::vector<const char *>::iterator iter;
        std::vector<string>::iterator hdr_iter;
        bool first = true;

        for (hdr_iter = dls.dls_headers.begin();
             hdr_iter != dls.dls_headers.end();
             ++hdr_iter) {
            if (!first) {
                fprintf(outfile, ",");
            }
            csv_write_string(outfile, *hdr_iter);
            first = false;
        }
        fprintf(outfile, "\n");

        for (row_iter = dls.dls_rows.begin();
             row_iter != dls.dls_rows.end();
             ++row_iter) {
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
                    json_write_row(handle, row);
                }
            }
        }
    }
    else {
        bookmark_vector<vis_line_t>::iterator iter;
        string line;

        for (iter = bv.begin(); iter != bv.end(); iter++) {
            tc->grep_value_for_line(*iter, line);
            fprintf(outfile, "%s\n", line.c_str());
        }
    }

    if (to_term) {
        cbreak();
        getch();
        refresh();
        nodelay(lnav_data.ld_window, 1);
    }
    if (outfile != stdout) {
        fclose(outfile);
    }
    outfile = NULL;

    return "";
}

static string com_pipe_to(string cmdline, vector<string> &args)
{
    string retval = "error: expecting command to execute";

    if (args.size() == 0) {
        args.push_back("filename");
        return "";
    }

    if (args.size() < 2) {
        return retval;
    }

    textview_curses *            tc = lnav_data.ld_view_stack.top();
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
            vector<std::string> path_v;
            string path;

            dup2(STDOUT_FILENO, STDERR_FILENO);
            path_v.push_back(dotlnav_path("formats/default"));
            setenv("PATH", build_path(path_v).c_str(), 1);

            if (pipe_line_to && tc == &lnav_data.ld_views[LNV_LOG]) {
                logfile_sub_source &lss = lnav_data.ld_log_source;
                log_data_helper ldh(lss);
                char tmp_str[64];

                ldh.parse_line(tc->get_top(), true);

                snprintf(tmp_str, sizeof(tmp_str), "%d", (int) tc->get_top());
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

            execvp(args[0], (char *const *) args);
            _exit(1);
            break;
        }

        default:
            bookmark_vector<vis_line_t>::iterator iter;
            static int exec_count = 0;
            string line;

            in_pipe.read_end().close_on_exec();
            in_pipe.write_end().close_on_exec();

            lnav_data.ld_children.push_back(child_pid);
            if (out_pipe.read_end() != -1) {
                piper_proc *pp = new piper_proc(out_pipe.read_end(), false);
                char desc[128];

                lnav_data.ld_pipers.push_back(pp);
                snprintf(desc,
                        sizeof(desc), "[%d] Output of %s",
                        exec_count++,
                        cmdline.c_str());
                lnav_data.ld_file_names.insert(make_pair(
                        desc,
                        pp->get_fd()));
                lnav_data.ld_files_to_front.push_back(make_pair(desc, 0));
                if (lnav_data.ld_rl_view != NULL) {
                    lnav_data.ld_rl_view->set_alt_value(HELP_MSG_1(
                            X, "to close the file"));
                }
            }

            if (pipe_line_to) {
                if (tc->get_inner_height() == 0) {
                    // Nothing to do
                }
                else if (tc == &lnav_data.ld_views[LNV_LOG]) {
                    logfile_sub_source &lss = lnav_data.ld_log_source;
                    content_line_t cl = lss.at(tc->get_top());
                    logfile *lf = lss.find(cl);
                    shared_buffer_ref sbr;
                    lf->read_full_message(lf->message_start(lf->begin() + cl), sbr);
                    if (write(in_pipe.write_end(), sbr.get_data(), sbr.length()) == -1) {
                        return "warning: Unable to write to pipe -- " + string(strerror(errno));
                    }
                    write(in_pipe.write_end(), "\n", 1);
                }
                else {
                    tc->grep_value_for_line(tc->get_top(), line);
                    if (write(in_pipe.write_end(), line.c_str(), line.size()) == -1) {
                        return "warning: Unable to write to pipe -- " + string(strerror(errno));
                    }
                    write(in_pipe.write_end(), "\n", 1);
                }
            }
            else {
                for (iter = bv.begin(); iter != bv.end(); iter++) {
                    tc->grep_value_for_line(*iter, line);
                    if (write(in_pipe.write_end(), line.c_str(), line.size()) == -1) {
                        return "warning: Unable to write to pipe -- " + string(strerror(errno));
                    }
                    write(in_pipe.write_end(), "\n", 1);
                }
            }

            retval = "";
            break;
    }

    return retval;
}

static string com_highlight(string cmdline, vector<string> &args)
{
    string retval = "error: expecting regular expression to highlight";

    if (args.size() == 0) {
        args.push_back("filter");
    }
    else if (args.size() > 1) {
        textview_curses *tc = lnav_data.ld_view_stack.top();
        textview_curses::highlight_map_t &hm = tc->get_highlights();
        const char *errptr;
        pcre *      code;
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
            textview_curses::highlighter hl(code, false);

            hm[args[1]] = hl;

            if (lnav_data.ld_rl_view != NULL) {
                lnav_data.ld_rl_view->add_possibility(
                        LNM_COMMAND, "highlight", args[1]);
            }

            retval = "info: highlight pattern now active";
            tc->reload_data();
        }
    }

    return retval;
}

static string com_clear_highlight(string cmdline, vector<string> &args)
{
    string retval = "error: expecting highlight expression to clear";

    if (args.size() == 0) {
        args.push_back("highlight");
    }
    else if (args.size() > 1 && args[1][0] != '$') {
        textview_curses *tc = lnav_data.ld_view_stack.top();
        textview_curses::highlight_map_t &hm = tc->get_highlights();
        textview_curses::highlight_map_t::iterator hm_iter;

        args[1] = remaining_args(cmdline, args);
        hm_iter = hm.find(args[1]);
        if (hm_iter == hm.end()) {
            retval = "error: highlight does not exist";
        }
        else {
            hm.erase(hm_iter);
            retval = "info: highlight pattern cleared";
            tc->reload_data();
        }
    }

    return retval;
}

static string com_graph(string cmdline, vector<string> &args)
{
    string retval = "error: expecting regular expression to graph";

    if (args.size() == 0) {
        args.push_back("graph");
    }
    else if (args.size() > 1) {
        const char *errptr;
        pcre *      code;
        int         eoff;

        args[1] = remaining_args(cmdline, args);
        if ((code = pcre_compile(args[1].c_str(),
                                 PCRE_CASELESS,
                                 &errptr,
                                 &eoff,
                                 NULL)) == NULL) {
            retval = "error: " + string(errptr);
        }
        else {
            textview_curses &            tc = lnav_data.ld_views[LNV_LOG];
            textview_curses::highlighter hl(code, true);

            textview_curses::highlight_map_t &hm = tc.get_highlights();

            hm["(graph"] = hl;
            lnav_data.ld_graph_source.set_highlighter(&hm["(graph"]);

            auto_ptr<grep_proc> gp(new grep_proc(code, tc));

            gp->queue_request();
            gp->start();
            gp->set_sink(&lnav_data.ld_graph_source);

            auto_ptr<grep_highlighter>
            gh(new grep_highlighter(gp, "(graph", hm));
            lnav_data.ld_grep_child[LG_GRAPH] = gh;

            toggle_view(&lnav_data.ld_views[LNV_GRAPH]);

            retval = "";
        }
    }

    return retval;
}

static string com_help(string cmdline, vector<string> &args)
{
    string retval = "";

    if (args.size() == 0) {}
    else {
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
    virtual ~pcre_filter() { };

    bool matches(const logfile &lf, const logline &ll, shared_buffer_ref &line)
    {
        pcre_context_static<30> pc;
        pcre_input pi(line.get_data(), 0, line.length());

        return this->pf_pcre.match(pc, pi);
    };

    std::string to_command(void)
    {
        return (this->lf_type == text_filter::INCLUDE ?
                "filter-in " : "filter-out ") +
               this->lf_id;
    };

protected:
    pcrepp pf_pcre;
};

static string com_enable_filter(string cmdline, vector<string> &args);

static string com_filter(string cmdline, vector<string> &args)
{
    string retval = "error: expecting regular expression to filter out";

    if (args.size() == 0) {
        args.push_back("filter");
    }
    else if (args.size() > 1) {
        textview_curses *tc = lnav_data.ld_view_stack.top();
        text_sub_source *tss = tc->get_sub_source();
        filter_stack &fs = tss->get_filters();
        const char *errptr;
        pcre *      code;
        int         eoff;

        args[1] = remaining_args(cmdline, args);
        if (fs.get_filter(args[1]) != NULL) {
            retval = com_enable_filter(cmdline, args);
        }
        else if (fs.full()) {
            retval = "error: filter limit reached, try combining "
                    "filters with a pipe symbol (e.g. foo|bar)";
        }
        else if ((code = pcre_compile(args[1].c_str(),
                                      PCRE_CASELESS,
                                      &errptr,
                                      &eoff,
                                      NULL)) == NULL) {
            retval = "error: " + string(errptr);
        }
        else {
            text_filter::type_t lt  = (args[0] == "filter-out") ?
                                         text_filter::EXCLUDE :
                                         text_filter::INCLUDE;
            auto_ptr<pcre_filter> pf(new pcre_filter(lt, args[1], fs.next_index(), code));

            log_debug("%s [%d] %s", args[0].c_str(), pf->get_index(), args[1].c_str());
            fs.add_filter(pf.release());
            tss->text_filters_changed();
            tc->reload_data();
            if (lnav_data.ld_rl_view != NULL) {
                lnav_data.ld_rl_view->add_possibility(
                    LNM_COMMAND, "enabled-filter", args[1]);
            }

            retval = "info: filter now active";
        }
    }

    return retval;
}

static string com_delete_filter(string cmdline, vector<string> &args)
{
    string retval = "error: expecting a filter to delete";

    if (args.size() == 0) {
        args.push_back("filter");
    }
    else if (args.size() > 1) {
        textview_curses *tc = lnav_data.ld_view_stack.top();
        text_sub_source *tss = tc->get_sub_source();
        filter_stack &fs = tss->get_filters();

        args[1] = remaining_args(cmdline, args);
        if (fs.delete_filter(args[1])) {
            retval = "info: deleted filter";
            tss->text_filters_changed();
            tc->reload_data();
        }
        else {
            retval = "error: unknown filter -- " + args[1];
        }
    }

    return retval;
}

static string com_enable_filter(string cmdline, vector<string> &args)
{
    string retval = "error: expecting disabled filter to enable";

    if (args.size() == 0) {
        args.push_back("disabled-filter");
    }
    else if (args.size() > 1) {
        textview_curses *tc = lnav_data.ld_view_stack.top();
        text_sub_source *tss = tc->get_sub_source();
        filter_stack &fs = tss->get_filters();
        text_filter *lf;

        args[1] = remaining_args(cmdline, args);
        lf      = fs.get_filter(args[1]);
        if (lf == NULL) {
            retval = "error: no such filter -- " + args[1];
        }
        else if (lf->is_enabled()) {
            retval = "info: filter already enabled";
        }
        else {
            fs.set_filter_enabled(lf, true);
            tss->text_filters_changed();
            tc->reload_data();
            retval = "info: filter enabled";
        }
    }

    return retval;
}

static string com_disable_filter(string cmdline, vector<string> &args)
{
    string retval = "error: expecting enabled filter to disable";

    if (args.size() == 0) {
        args.push_back("enabled-filter");
    }
    else if (args.size() > 1) {
        textview_curses *tc = lnav_data.ld_view_stack.top();
        text_sub_source *tss = tc->get_sub_source();
        filter_stack &fs = tss->get_filters();
        text_filter *lf;

        args[1] = remaining_args(cmdline, args);
        lf      = fs.get_filter(args[1]);
        if (lf == NULL) {
            retval = "error: no such filter -- " + args[1];
        }
        else if (!lf->is_enabled()) {
            retval = "info: filter already disabled";
        }
        else {
            fs.set_filter_enabled(lf, false);
            tss->text_filters_changed();
            tc->reload_data();
            retval = "info: filter disabled";
        }
    }

    return retval;
}

static string com_enable_word_wrap(string cmdline, vector<string> &args)
{
    string retval = "";

    if (args.size() == 0) {

    }
    else {
        lnav_data.ld_views[LNV_LOG].set_word_wrap(true);
        lnav_data.ld_views[LNV_TEXT].set_word_wrap(true);
        lnav_data.ld_views[LNV_PRETTY].set_word_wrap(true);
    }

    return retval;
}

static string com_disable_word_wrap(string cmdline, vector<string> &args)
{
    string retval = "";

    if (args.size() == 0) {

    }
    else {
        lnav_data.ld_views[LNV_LOG].set_word_wrap(false);
        lnav_data.ld_views[LNV_TEXT].set_word_wrap(false);
        lnav_data.ld_views[LNV_PRETTY].set_word_wrap(false);
    }

    return retval;
}

static std::set<string> custom_logline_tables;

static string com_create_logline_table(string cmdline, vector<string> &args)
{
    string retval = "error: expecting a table name";

    if (args.size() == 0) {}
    else if (args.size() == 2) {
        textview_curses &log_view = lnav_data.ld_views[LNV_LOG];

        if (log_view.get_inner_height() == 0) {
            retval = "error: no log data available";
        }
        else {
            vis_line_t      vl  = log_view.get_top();
            content_line_t  cl  = lnav_data.ld_log_source.at_base(vl);
            log_data_table *ldt = new log_data_table(cl, intern_string::lookup(args[1]));
            string          errmsg;

            errmsg = lnav_data.ld_vtab_manager->register_vtab(ldt);
            if (errmsg.empty()) {
                custom_logline_tables.insert(args[1]);
                if (lnav_data.ld_rl_view != NULL) {
                    lnav_data.ld_rl_view->add_possibility(LNM_COMMAND,
                      "custom-table",
                      args[1]);
                }
                retval = "info: created new log table -- " + args[1];
            }
            else {
                delete ldt;
                retval = "error: unable to create table -- " + errmsg;
            }
        }
    }

    return retval;
}

static string com_delete_logline_table(string cmdline, vector<string> &args)
{
    string retval = "error: expecting a table name";

    if (args.size() == 0) {
        args.push_back("custom-table");
    }
    else if (args.size() == 2) {
        if (custom_logline_tables.find(args[1]) == custom_logline_tables.end()) {
            return "error: unknown logline table -- " + args[1];
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

static string com_create_search_table(string cmdline, vector<string> &args)
{
    string retval = "error: expecting a table name";

    if (args.size() == 0) {

    }
    else if (args.size() >= 2) {
        log_search_table *lst;
        string regex;

        if (args.size() >= 3) {
            regex = remaining_args(cmdline, args, 2);
        }
        else {
            regex = lnav_data.ld_last_search[LNV_LOG];
        }

        try {
            lst = new log_search_table(regex.c_str(),
                                       intern_string::lookup(args[1]));
        } catch (pcrepp::error &e) {
            return "error: unable to compile regex -- " + regex;
        }

        string          errmsg;

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

static string com_delete_search_table(string cmdline, vector<string> &args)
{
    string retval = "error: expecting a table name";

    if (args.size() == 0) {
        args.push_back("search-table");
    }
    else if (args.size() == 2) {
        if (custom_search_tables.find(args[1]) == custom_search_tables.end()) {
            return "error: unknown search table -- " + args[1];
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

static string com_session(string cmdline, vector<string> &args)
{
    string retval = "error: expecting a command to save to the session file";

    if (args.size() == 0) {}
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

static string com_open(string cmdline, vector<string> &args)
{
    string retval = "error: expecting file name to open";

    if (args.size() == 0) {
        args.push_back("filename");
        return "";
    }
    else if (args.size() < 2) {
        return retval;
    }

    static_root_mem<wordexp_t, wordfree> wordmem;
    list<logfile *>::iterator file_iter;
    size_t colon_index;
    int top = 0;
    string pat;

    pat = trim(remaining_args(cmdline, args));

    int rc = wordexp(pat.c_str(), wordmem.inout(), WRDE_NOCMD | WRDE_UNDEF);

    if (!wordexperr(rc, retval)) {
        return retval;
    }

    for (size_t lpc = 0; lpc < wordmem->we_wordc; lpc++) {
        string fn = wordmem->we_wordv[lpc];

        if (startswith(fn, "pt:")) {
            lnav_data.ld_pt_search = fn;

            refresh_pt_search();
            continue;
        }

        if (access(fn.c_str(), R_OK) != 0 &&
            (colon_index = fn.rfind(':')) != string::npos) {
            if (sscanf(&fn.c_str()[colon_index + 1], "%d", &top) == 1) {
                fn = fn.substr(0, colon_index);
            }
        }

        for (file_iter = lnav_data.ld_files.begin();
             file_iter != lnav_data.ld_files.end();
             ++file_iter) {
            logfile *lf = *file_iter;

            if (lf->get_filename() == fn) {
                if (lf->get_format() != NULL) {
                    retval = "info: log file already loaded";
                    break;
                }
                else {
                    lnav_data.ld_files_to_front.push_back(make_pair(fn, top));
                    retval = "";
                    break;
                }
            }
        }
        if (file_iter == lnav_data.ld_files.end()) {
            auto_mem<char> abspath;
            struct stat    st;

            if (is_url(fn.c_str())) {
#ifndef HAVE_LIBCURL
                retval = "error: lnav was not compiled with libcurl";
#else
                auto_ptr<url_loader> ul(new url_loader(fn));

                lnav_data.ld_file_names.insert(make_pair(fn, ul->copy_fd().release()));
                lnav_data.ld_curl_looper.add_request(ul.release());
                lnav_data.ld_files_to_front.push_back(make_pair(fn, top));

                retval = "info: opened URL";
#endif
            }
            else if (is_glob(fn.c_str())) {
                lnav_data.ld_file_names.insert(make_pair(fn, -1));
                retval = "info: watching -- " + fn;
            }
            else if (stat(fn.c_str(), &st) == -1) {
                retval = ("error: cannot stat file: " + fn + " -- "
                    + strerror(errno));
            }
            else if ((abspath = realpath(fn.c_str(), NULL)) == NULL) {
                retval = "error: cannot find file";
            }
            else if (S_ISDIR(st.st_mode)) {
                string dir_wild(abspath.in());

                if (dir_wild[dir_wild.size() - 1] == '/') {
                    dir_wild.resize(dir_wild.size() - 1);
                }
                lnav_data.ld_file_names.insert(make_pair(dir_wild + "/*", -1));
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
                lnav_data.ld_file_names.insert(make_pair(fn, -1));
                retval = "info: opened -- " + fn;
                lnav_data.ld_files_to_front.push_back(make_pair(fn, top));

                lnav_data.ld_closed_files.erase(fn);
                if (lnav_data.ld_rl_view != NULL) {
                    lnav_data.ld_rl_view->set_alt_value(HELP_MSG_1(
                        X, "to close the file"));
                }
            }
        }
    }

    return retval;
}

static string com_close(string cmdline, vector<string> &args)
{
    string retval = "error: close must be run in the log or text file views";

    if (args.empty()) {

    }
    else {
        textview_curses *tc = lnav_data.ld_view_stack.top();
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
                    lnav_data.ld_view_stack.pop();
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
                logfile *lf = lss.find(cl);

                fn = lf->get_filename();
                lf->close();
            }
        }
        if (!fn.empty()) {
            if (is_url(fn.c_str())) {
                lnav_data.ld_curl_looper.close_request(fn);
            }
            lnav_data.ld_file_names.erase(make_pair(fn, -1));
            lnav_data.ld_closed_files.insert(fn);
            retval = "info: closed -- " + fn;
        }
    }

    return retval;
}

static string com_partition_name(string cmdline, vector<string> &args)
{
    string retval = "error: expecting partition name";

    if (args.size() == 0) {
        return "";
    }
    else if (args.size() > 1) {
        textview_curses &tc = lnav_data.ld_views[LNV_LOG];
        logfile_sub_source &lss = lnav_data.ld_log_source;
        std::map<content_line_t, bookmark_metadata> &bm = lss.get_user_bookmark_metadata();

        args[1] = trim(remaining_args(cmdline, args));

        tc.set_user_mark(&textview_curses::BM_PARTITION, tc.get_top(), true);

        bookmark_metadata &line_meta = bm[lss.at(tc.get_top())];

        line_meta.bm_name = args[1];
        retval = "info: name set for partition";
    }

    return retval;
}

static string com_clear_partition(string cmdline, vector<string> &args)
{
    string retval = "";

    if (args.size() == 0) {
        return "";
    }
    else if (args.size() == 1) {
        textview_curses &tc = lnav_data.ld_views[LNV_LOG];
        logfile_sub_source &lss = lnav_data.ld_log_source;
        bookmark_vector<vis_line_t> &bv = tc.get_bookmarks()[
            &textview_curses::BM_PARTITION];
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
        else {
            tc.set_user_mark(
                &textview_curses::BM_PARTITION, part_start, false);

            bm.erase(lss.at(part_start));
            retval = "info: cleared partition name";
        }
    }

    return retval;
}

static string com_pt_time(string cmdline, vector<string> &args)
{
    string retval = "error: expecting a time value";

    if (args.size() == 0) {
        args.push_back("move-time");
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
        if (new_time.tv_sec != 0) {
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

static string com_summarize(string cmdline, vector<string> &args)
{
    string retval = "";

    if (args.size() == 0) {
        args.push_back("colname");
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

        query += " FROM logline WHERE startswith(logline.log_part, '.') = 0 ";

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
                    sql_callback(stmt.in());
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

static string com_add_test(string cmdline, vector<string> &args)
{
    string retval = "";

    if (args.size() == 0) {}
    else if (args.size() > 1) {
        retval = "error: not expecting any arguments";
    }
    else {
        textview_curses *tc = lnav_data.ld_view_stack.top();

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

static string com_switch_to_view(string cmdline, vector<string> &args)
{
    string retval = "";

    if (args.size() == 0) {
        args.push_back("viewname");
    }
    else if (args.size() > 1) {
        bool found = false;

        for (int lpc = 0; lnav_view_strings[lpc] && !found; lpc++) {
            if (strcasecmp(args[1].c_str(), lnav_view_strings[lpc]) == 0) {
                ensure_view(&lnav_data.ld_views[lpc]);
                found = true;
            }
        }

        if (!found) {
            retval = "error: invalid view name -- " + args[1];
        }
    }

    return retval;
}

static string com_zoom_to(string cmdline, vector<string> &args)
{
    string retval = "";

    if (args.size() == 0) {
        args.push_back("zoomlevel");
    }
    else if (args.size() > 1) {
        bool found = false;

        for (int lpc = 0; lnav_zoom_strings[lpc] && !found; lpc++) {
            if (strcasecmp(args[1].c_str(), lnav_zoom_strings[lpc]) == 0) {
                lnav_data.ld_hist_zoom = lpc;
                rebuild_hist(0, true);
                found = true;
            }
        }

        if (!found) {
            retval = "error: invalid zoom level -- " + args[1];
        }
    }

    return retval;
}

static string com_load_session(string cmdline, vector<string> &args)
{
    if (args.empty()) {

    }
    else {
        scan_sessions();
        load_session();
        lnav_data.ld_views[LNV_LOG].reload_data();
    }

    return "";
}

static string com_save_session(string cmdline, vector<string> &args)
{
    if (args.empty()) {

    }
    else {
        save_session();
    }

    return "";
}

static string com_set_min_log_level(string cmdline, vector<string> &args)
{
    string retval = "error: expecting log level name";

    if (args.empty()) {
        args.push_back("levelname");
    }
    else if (args.size() == 2) {
        logfile_sub_source &lss = lnav_data.ld_log_source;
        logline::level_t new_level;

        new_level = logline::string2level(
            args[1].c_str(), args[1].size(), true);
        lss.set_min_log_level(new_level);
        rebuild_indexes(true);

        retval = ("info: minimum log level is now -- " +
            string(logline::level_names[new_level]));
    }

    return retval;
}

static string com_hide_line(string cmdline, vector<string> &args)
{
    string retval;

    if (args.empty()) {
        args.push_back("move-time");
    }
    else if (args.size() == 1) {
        textview_curses *tc = lnav_data.ld_view_stack.top();
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
        textview_curses *tc = lnav_data.ld_view_stack.top();
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

        if (tv_set) {
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

static string com_show_lines(string cmdline, vector<string> &args)
{
    string retval = "info: showing lines";

    if (!args.empty()) {
        logfile_sub_source &lss = lnav_data.ld_log_source;
        textview_curses *tc = lnav_data.ld_view_stack.top();

        if (tc == &lnav_data.ld_views[LNV_LOG]) {
            lss.clear_min_max_log_times();
        }

        rebuild_indexes(true);
    }

    return retval;
}

static string com_rebuild(string cmdline, vector<string> &args)
{
    if (args.empty()) {

    }
    else {
        rebuild_indexes(false);
    }

    return "";
}

static string com_shexec(string cmdline, vector<string> &args)
{
    if (args.empty()) {

    }
    else {
        system(cmdline.substr(args[0].size()).c_str());
    }

    return "";
}

static string com_poll_now(string cmdline, vector<string> &args)
{
    if (args.empty()) {

    }
    else {
        lnav_data.ld_curl_looper.process_all();
    }

    return "";
}

static string com_redraw(string cmdline, vector<string> &args)
{
    if (args.empty()) {

    }
    else if (lnav_data.ld_window) {
        redrawwin(lnav_data.ld_window);
    }

    return "";
}

static string com_echo(string cmdline, vector<string> &args)
{
    string retval = "error: expecting a message";

    if (args.empty()) {

    }
    else if (args.size() > 1) {
        bool lf = true;

        if (args.size() > 2 && args[1] == "-n") {
            lf = false;
        }
        retval = remaining_args(cmdline, args, lf ? 1 : 2);
        if (lnav_data.ld_flags & LNF_HEADLESS) {
            printf("%s", retval.c_str());
            if (lf) {
                putc('\n', stdout);
            }
            fflush(stdout);
        }
    }

    return retval;
}

static string com_eval(string cmdline, vector<string> &args)
{
    string retval = "error: expecting a command or query to evaluate";

    if (args.empty()) {

    }
    else if (args.size() > 1) {
        string all_args = remaining_args(cmdline, args);
        string expanded_cmd;
        shlex lexer(all_args.c_str(), all_args.size());

        log_debug("Evaluating: %s", all_args.c_str());
        if (!lexer.eval(expanded_cmd, lnav_data.ld_local_vars.top())) {
            return "error: invalid arguments";
        }
        log_debug("Expanded command to evaluate: %s", expanded_cmd.c_str());

        if (expanded_cmd.empty()) {
            return "error: empty result after evaluation";
        }

        string alt_msg;
        switch (expanded_cmd[0]) {
            case ':':
                retval = execute_command(expanded_cmd.substr(1));
                break;
            case ';':
                retval = execute_sql(expanded_cmd.substr(1), alt_msg);
                break;
            case '|':
                retval = "info: executed file -- " + expanded_cmd.substr(1) +
                        " -- " + execute_file(expanded_cmd.substr(1));
                break;
            default:
                retval = "error: expecting argument to start with ':', ';', "
                         "or '|' to signify a command, SQL query, or script to execute";
                break;
        }
    }

    return retval;
}

readline_context::command_t STD_COMMANDS[] = {
    {
        "adjust-log-time",
        "<date>",
        "Change the timestamps of the top file to be relative to the given date",
        com_adjust_log_time
    },

    {
        "unix-time",
        "<seconds>",
        "Convert epoch time to a human-readable form",
        com_unix_time,
    },
    {
        "current-time",
        NULL,
        "Print the current time in human-readable form and seconds since the epoch",
        com_current_time,
    },
    {
        "goto",
        "<line#|N%|date>",
        "Go to the given line number, N percent into the file, or the given timestamp in the log view",
        com_goto,
    },
    {
        "relative-goto",
        "<line#|N%>",
        "Move the current view up or down by the given amount",
        com_relative_goto,
    },
    {
        "next-mark",
        "error|warning|search|user|file|partition",
        "Move to the next bookmark of the given type in the current view",
        com_goto_mark,
    },
    {
        "prev-mark",
        "error|warning|search|user|file|partition",
        "Move to the previous bookmark of the given type in the current view",
        com_goto_mark,
    },
    {
        "graph",
        "<regex>",
        "Graph the number values captured by the given regex that are found in the log view",
        com_graph,
    },
    {
        "help",
        NULL,
        "Open the help text view",
        com_help,
    },
    {
        "hide-lines-before",
        "<line#|date>",
        "Hide lines that come before the given line number or date",
        com_hide_line,
    },
    {
        "hide-lines-after",
        "<line#|date>",
        "Hide lines that come after the given line number or date",
        com_hide_line,
    },
    {
        "show-lines-before-and-after",
        NULL,
        "Show lines that were hidden by the 'hide-lines' commands",
        com_show_lines,
    },
    {
        "highlight",
        "<regex>",
        "Add coloring to log messages fragments that match the given regular expression",
        com_highlight,
    },
    {
        "clear-highlight",
        "<regex>",
        "Remove a previously set highlight regular expression",
        com_clear_highlight,
    },
    {
        "filter-in",
        "<regex>",
        "Only show lines that match the given regular expression in the current view",
        com_filter,
    },
    {
        "filter-out",
        "<regex>",
        "Remove lines that match the given regular expression in the current view",
        com_filter,
    },
    {
        "delete-filter",
        "<regex>",
        "Delete the given filter",
        com_delete_filter,
    },
    {
        "append-to",
        "<filename>",
        "Append marked lines in the current view to the given file",
        com_save_to,
    },
    {
        "write-to",
        "<filename>",
        "Overwrite the given file with any marked lines in the current view",
        com_save_to,
    },
    {
        "write-csv-to",
        "<filename>",
        "Write SQL results to the given file in CSV format",
        com_save_to,
    },
    {
        "write-json-to",
        "<filename>",
        "Write SQL results to the given file in JSON format",
        com_save_to,
    },
    {
        "pipe-to",
        "<shell-cmd>",
        "Pipe the marked lines to the given shell command",
        com_pipe_to,
    },
    {
        "pipe-line-to",
        "<shell-cmd>",
        "Pipe the top line to the given shell command",
        com_pipe_to,
    },
    {
        "enable-filter",
        "<regex>",
        "Enable a previously created and disabled filter",
        com_enable_filter,
    },
    {
        "disable-filter",
        "<regex>",
        "Disable a filter created with filter-in/filter-out",
        com_disable_filter,
    },
    {
        "enable-word-wrap",
        NULL,
        "Enable word-wrapping for the current view",
        com_enable_word_wrap,
    },
    {
        "disable-word-wrap",
        NULL,
        "Disable word-wrapping for the current view",
        com_disable_word_wrap,
    },
    {
        "create-logline-table",
        "<table-name>",
        "Create an SQL table using the top line of the log view as a template",
        com_create_logline_table,
    },
    {
        "delete-logline-table",
        "<table-name>",
        "Delete a table created with create-logline-table",
        com_delete_logline_table,
    },
    {
        "create-search-table",
        "<table-name> [<regex>]",
        "Create an SQL table based on a regex search",
        com_create_search_table,
    },
    {
        "delete-search-table",
        "<table-name>",
        "Delete a table created with create-search-table",
        com_delete_search_table,
    },
    {
        "open",
        "<filename>",
#ifdef HAVE_LIBCURL
        "Open the given file(s) or URLs in lnav",
#else
        "Open the given file(s) in lnav",
#endif
        com_open,
    },
    {
        "close",
        NULL,
        "Close the top file",
        com_close,
    },
    {
        "partition-name",
        "<name>",
        "Mark the top line in the log view as the start of a new partition with the given name",
        com_partition_name,
    },
    {
        "clear-partition",
        NULL,
        "Clear the partition the top line is a part of",
        com_clear_partition,
    },
    {
        "pt-min-time",
        "[<time>]",
        "Set/get the minimum time for papertrail searches",
        com_pt_time,
    },
    {
        "pt-max-time",
        "[<time>]",
        "Set/get the maximum time for papertrail searches",
        com_pt_time,
    },
    {
        "session",
        "<lnav-command>",
        "Add the given command to the session file (~/.lnav/session)",
        com_session,
    },
    {
        "summarize",
        "<column-name>",
        "Execute a SQL query that computes the characteristics of the values in the given column",
        com_summarize,
    },
    {
        "switch-to-view",
        "<view-name>",
        "Switch to the given view",
        com_switch_to_view,
    },
    {
        "load-session",
        NULL,
        "Load the latest session state",
        com_load_session,
    },
    {
        "save-session",
        NULL,
        "Save the current state as a session",
        com_save_session,
    },
    {
        "set-min-log-level",
        "<log-level>",
        "Set the minimum log level to display in the log view",
        com_set_min_log_level,
    },
    {
        "redraw",
        NULL,
        "Do a full redraw of the screen",
        com_redraw,
    },
    {
        "zoom-to",
        "<zoom-level>",
        "Zoom the histogram view to the given level",
        com_zoom_to,
    },
    {
        "echo",
        "[-n] <msg>",
        "Echo the given message",
        com_echo,
    },
    {
        "eval",
        "<msg>",
        "Evaluate the given command/query after doing environment variable substitution",
        com_eval,
    },

    { NULL },
};

void init_lnav_commands(readline_context::command_map_t &cmd_map)
{
    for (int lpc = 0; STD_COMMANDS[lpc].c_name != NULL; lpc++) {
        readline_context::command_t &cmd = STD_COMMANDS[lpc];

        cmd_map[cmd.c_name] = cmd;
    }

    if (getenv("LNAV_SRC") != NULL) {
        cmd_map["add-test"] = com_add_test;
    }
    if (getenv("lnav_test") != NULL) {
        cmd_map["rebuild"] = com_rebuild;
        cmd_map["shexec"] = com_shexec;
        cmd_map["poll-now"] = com_poll_now;
    }
}
