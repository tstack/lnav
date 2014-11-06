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
#include "lnav_commands.hh"
#include "session_data.hh"

using namespace std;

static bool wordexperr(int rc, string &msg)
{
    switch (rc) {
    case WRDE_BADCHAR:
        msg = "error: invalid filename character";
        return false;

    case WRDE_CMDSUB:
        msg = "error: command substitution is not allowed";
        return false;

    case WRDE_BADVAL:
        msg = "error: unknown environment variable in file name";
        return false;

    case WRDE_NOSPACE:
        msg = "error: out of memory";
        return false;

    case WRDE_SYNTAX:
        msg = "error: invalid syntax";
        return false;

    default:
        break;
    }
    
    return true;
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
        args[1] = cmdline.substr(cmdline.find(args[1], args[0].size()));
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

        args[1] = cmdline.substr(cmdline.find(args[1], args[0].size()));
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
    string retval = "error: expecting line number/percentage or timestamp";

    if (args.size() == 0) {
        args.push_back("line-time");
    }
    else if (args.size() > 1) {
        textview_curses *tc = lnav_data.ld_view_stack.top();
        int   line_number, consumed;
        date_time_scanner dts;
        struct timeval tv;
        struct exttm tm;
        float value;

        if (dts.scan(args[1].c_str(), args[1].size(), NULL, &tm, tv) != NULL) {
            if (tc == &lnav_data.ld_views[LNV_LOG]) {
                vis_line_t vl;

                vl = lnav_data.ld_log_source.find_from_time(tv);
                tc->set_top(vl);
                retval = "";
            } else {
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
    db_label_source &dls = lnav_data.ld_db_rows;
    yajlpp_map obj_map(handle);

    for (int col = 0; col < dls.dls_column_types.size(); col++) {
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

    if (args.size() == 0) {
        args.push_back("filename");
        return "";
    }

    if (args.size() < 2) {
        return "error: expecting file name";
    }

    fn = trim(cmdline.substr(cmdline.find(args[1], args[0].size())));

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
    db_label_source &dls = lnav_data.ld_db_rows;

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
        if (!(lnav_data.ld_flags & LNF_HEADLESS)) {
            return "error: writing to stdout is only available in headless mode";
        }
        outfile = stdout;
        lnav_data.ld_stdout_used = true;
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
            return "error: unable to allocate memory";
        }
        else {
            yajl_gen_config(handle, yajl_gen_beautify, 1);
            yajl_gen_config(handle,
                yajl_gen_print_callback, yajl_writer, outfile);

            {
                yajlpp_array root_array(handle);

                for (int row = 0; row < dls.dls_rows.size(); row++) {
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

    if (outfile != stdout) {
        fclose(outfile);
    }
    outfile = NULL;

    return "";
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

        args[1] = cmdline.substr(cmdline.find(args[1], args[0].size()));
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

        args[1] = cmdline.substr(cmdline.find(args[1], args[0].size()));
        hm_iter = hm.find(args[1]);
        if (hm_iter == hm.end()) {
            retval = "error: highlight does not exist";
        }
        else {
            hm.erase(hm_iter);
            retval = "info: highlight pattern cleared";
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

        args[1] = cmdline.substr(cmdline.find(args[1], args[0].size()));
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

            auto_ptr<grep_proc> gp(new grep_proc(code,
                                                 tc,
                                                 lnav_data.ld_max_fd,
                                                 lnav_data.ld_read_fds));

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

        args[1] = cmdline.substr(cmdline.find(args[1], args[0].size()));
        if (fs.get_filter(args[1]) != NULL) {
            retval = com_enable_filter(cmdline, args);
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

        args[1] = cmdline.substr(cmdline.find(args[1], args[0].size()));
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
            if (lnav_data.ld_rl_view != NULL) {
                lnav_data.ld_rl_view->rem_possibility(
                    LNM_COMMAND, "disabled-filter", args[1]);
                lnav_data.ld_rl_view->add_possibility(
                    LNM_COMMAND, "enabled-filter", args[1]);
            }
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

        args[1] = cmdline.substr(cmdline.find(args[1], args[0].size()));
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
            if (lnav_data.ld_rl_view != NULL) {
                lnav_data.ld_rl_view->rem_possibility(
                    LNM_COMMAND, "enabled-filter", args[1]);
                lnav_data.ld_rl_view->add_possibility(
                    LNM_COMMAND, "disabled-filter", args[1]);
            }
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
        lnav_data.ld_views[LNV_LOG].set_needs_update();
        lnav_data.ld_views[LNV_TEXT].set_word_wrap(true);
        lnav_data.ld_views[LNV_TEXT].set_needs_update();
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
        lnav_data.ld_views[LNV_LOG].set_needs_update();
        lnav_data.ld_views[LNV_TEXT].set_word_wrap(false);
        lnav_data.ld_views[LNV_TEXT].set_needs_update();
    }

    return retval;
}

static std::vector<string> custom_logline_tables;

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
            log_data_table *ldt = new log_data_table(cl, args[1]);
            string          errmsg;

            errmsg = lnav_data.ld_vtab_manager->register_vtab(ldt);
            if (errmsg.empty()) {
                if (lnav_data.ld_rl_view != NULL) {
                    lnav_data.ld_rl_view->add_possibility(LNM_COMMAND,
                      "custom-table",
                      args[1]);
                }
                retval = "info: created new log table -- " + args[1];
            }
            else {
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
        string rc = lnav_data.ld_vtab_manager->unregister_vtab(args[1]);

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
            string::size_type space;
            string            saved_cmd;

            space = cmdline.find(' ');
            while (isspace(cmdline[space])) {
                space += 1;
            }
            saved_cmd = cmdline.substr(space);

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

                    rename(new_file_name.c_str(), old_file_name.c_str());
                }
                else {
                    remove(new_file_name.c_str());
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

    pat = trim(cmdline.substr(cmdline.find(args[1], args[0].size())));

    int rc = wordexp(pat.c_str(), wordmem.inout(), WRDE_NOCMD | WRDE_UNDEF);

    if (!wordexperr(rc, retval)) {
        return retval;
    }

    for (int lpc = 0; lpc < wordmem->we_wordc; lpc++) {
        string fn = wordmem->we_wordv[lpc];

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

            if (is_glob(fn.c_str())) {
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

            if (tss.tss_files.empty()) {
                retval = "error: no text files are opened";
            }
            else {
                fn = tss.current_file()->get_filename();
                tss.current_file()->close();

                if (tss.tss_files.size() == 1) {
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

        args[1] = trim(cmdline.substr(cmdline.find(args[1], args[0].size())));

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

static string com_summarize(string cmdline, vector<string> &args)
{
    static pcrecpp::RE db_column_converter("\"");

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

        db_label_source &      dls = lnav_data.ld_db_rows;
        hist_source &          hs  = lnav_data.ld_db_source;

        hs.clear();
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

void init_lnav_commands(readline_context::command_map_t &cmd_map)
{
    cmd_map["adjust-log-time"]      = com_adjust_log_time;
    cmd_map["unix-time"]            = com_unix_time;
    cmd_map["current-time"]         = com_current_time;
    cmd_map["goto"]                 = com_goto;
    cmd_map["graph"]                = com_graph;
    cmd_map["help"]                 = com_help;
    cmd_map["highlight"]            = com_highlight;
    cmd_map["clear-highlight"]      = com_clear_highlight;
    cmd_map["filter-in"]            = com_filter;
    cmd_map["filter-out"]           = com_filter;
    cmd_map["append-to"]            = com_save_to;
    cmd_map["write-to"]             = com_save_to;
    cmd_map["write-csv-to"]         = com_save_to;
    cmd_map["write-json-to"]        = com_save_to;
    cmd_map["enable-filter"]        = com_enable_filter;
    cmd_map["disable-filter"]       = com_disable_filter;
    cmd_map["enable-word-wrap"]     = com_enable_word_wrap;
    cmd_map["disable-word-wrap"]    = com_disable_word_wrap;
    cmd_map["create-logline-table"] = com_create_logline_table;
    cmd_map["delete-logline-table"] = com_delete_logline_table;
    cmd_map["open"]                 = com_open;
    cmd_map["close"]                = com_close;
    cmd_map["partition-name"]       = com_partition_name;
    cmd_map["clear-partition"]      = com_clear_partition;
    cmd_map["session"]              = com_session;
    cmd_map["summarize"]            = com_summarize;
    cmd_map["switch-to-view"]       = com_switch_to_view;
    cmd_map["load-session"]         = com_load_session;
    cmd_map["save-session"]         = com_save_session;
    cmd_map["set-min-log-level"]    = com_set_min_log_level;

    if (getenv("LNAV_SRC") != NULL) {
        cmd_map["add-test"] = com_add_test;
    }
}
