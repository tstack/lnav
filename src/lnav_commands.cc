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

#include <string>
#include <vector>
#include <fstream>

#include "lnav.hh"
#include "log_data_table.hh"
#include "lnav_commands.hh"

using namespace std;

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

        args[1] = cmdline.substr(cmdline.find(args[1]));
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
    int       len;

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
    string retval = "error: expecting line number/percentage";

    if (args.size() == 0) { }
    else if (args.size() > 1) {
        textview_curses *tc = lnav_data.ld_view_stack.top();
        int   line_number, consumed;
        float value;

        if (sscanf(args[1].c_str(), "%f%n", &value, &consumed) == 1) {
            if (args[1][consumed] == '%') {
                line_number = (int)
                              ((double)tc->get_inner_height() *
                               (value / 100.0));
            }
            else {
                line_number = (int)value;
            }
            tc->set_top(vis_line_t(line_number));

            retval = "";
        }
    }

    return retval;
}

static string com_save_to(string cmdline, vector<string> &args)
{
    FILE *      outfile = NULL;
    FILE *      pfile   = NULL;
    char        command[1024];
    const char *mode = "";

    if (args.size() == 0) {
        args.push_back("filename");
        return "";
    }

    if (args.size() != 2) {
        return "error: expecting file name";
    }

    snprintf(command, sizeof(command), "/bin/echo -n %s", args[1].c_str());
    if ((pfile = popen(command, "r")) == NULL) {
        return "error: unable to compute file name";
    }

    if (fgets(command, sizeof(command), pfile) == 0) {
        perror("fgets");
        return "error: unable to compute file name";
    }
    fclose(pfile);
    pfile = NULL;

    if (args[0] == "append-to") {
        mode = "a";
    }
    else if (args[0] == "write-to") {
        mode = "w";
    }

    if ((outfile = fopen(command, mode)) == NULL) {
        return "error: unable to open file -- " + string(command);
    }

    textview_curses *tc             = lnav_data.ld_view_stack.top();
    bookmark_vector<vis_line_t> &bv =
        tc->get_bookmarks()[&textview_curses::BM_USER];
    bookmark_vector<vis_line_t>::iterator iter;
    string line;

    for (iter = bv.begin(); iter != bv.end(); iter++) {
        tc->grep_value_for_line(*iter, line);
        fprintf(outfile, "%s\n", line.c_str());
    }

    fclose(outfile);
    outfile = NULL;

    return "";
}

static string com_highlight(string cmdline, vector<string> &args)
{
    string retval = "error: expecting regular expression to highlight";

    if (args.size() == 0) { }
    else if (args.size() > 1) {
        const char *errptr;
        pcre *      code;
        int         eoff;

        args[1] = cmdline.substr(cmdline.find(args[1]));
        if ((code = pcre_compile(args[1].c_str(),
                                 PCRE_CASELESS,
                                 &errptr,
                                 &eoff,
                                 NULL)) == NULL) {
            retval = "error: " + string(errptr);
        }
        else {
            textview_curses *            tc = lnav_data.ld_view_stack.top();
            textview_curses::highlighter hl(code, false);

            textview_curses::highlight_map_t &hm = tc->get_highlights();

            hm[args[1]] = hl;

            retval = "info: highlight pattern now active";
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

        args[1] = cmdline.substr(cmdline.find(args[1]));
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
    : public logfile_filter {
public:
    pcre_filter(type_t type, string id, pcre *code)
        : logfile_filter(type, id),
          pf_code(code) { };
    virtual ~pcre_filter() { };

    bool matches(string line)
    {
        static const int MATCH_COUNT = 20 * 3;
        int  matches[MATCH_COUNT], rc;
        bool retval;

        rc = pcre_exec(this->pf_code,
                       NULL,
                       line.c_str(),
                       line.size(),
                       0,
                       0,
                       matches,
                       MATCH_COUNT);
        retval = (rc >= 0);

#if 0
        fprintf(stderr, " out %d %s\n",
                retval,
                line.c_str());
#endif

        return retval;
    };

protected:
    auto_mem<pcre> pf_code;
};

static string com_filter(string cmdline, vector<string> &args)
{
    string retval = "error: expecting regular expression to filter out";

    if (args.size() == 0) {
        args.push_back("filter");
    }
    else if (args.size() > 1) {
        const char *errptr;
        pcre *      code;
        int         eoff;

        args[1] = cmdline.substr(cmdline.find(args[1]));
        if ((code = pcre_compile(args[1].c_str(),
                                 0,
                                 &errptr,
                                 &eoff,
                                 NULL)) == NULL) {
            retval = "error: " + string(errptr);
        }
        else {
            logfile_sub_source &   lss = lnav_data.ld_log_source;
            logfile_filter::type_t lt  = (args[0] == "filter-out") ?
                                         logfile_filter::EXCLUDE :
                                         logfile_filter::INCLUDE;
            auto_ptr<pcre_filter> pf(new pcre_filter(lt, args[1], code));

            lss.get_filters().push_back(pf.release());
            lnav_data.ld_rl_view->
            add_possibility(LNM_COMMAND, "enabled-filter", args[1]);
            rebuild_indexes(true);

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
        logfile_filter *lf;

        args[1] = cmdline.substr(cmdline.find(args[1]));
        lf      = lnav_data.ld_log_source.get_filter(args[1]);
        if (lf == NULL) {
            retval = "error: no such filter -- " + args[1];
        }
        else if (lf->is_enabled()) {
            retval = "info: filter already enabled";
        }
        else {
            lf->enable();
            lnav_data.ld_rl_view->
            rem_possibility(LNM_COMMAND, "disabled-filter", args[1]);
            lnav_data.ld_rl_view->
            add_possibility(LNM_COMMAND, "enabled-filter", args[1]);
            rebuild_indexes(true);
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
        logfile_filter *lf;

        args[1] = cmdline.substr(cmdline.find(args[1]));
        lf      = lnav_data.ld_log_source.get_filter(args[1]);
        if (lf == NULL) {
            retval = "error: no such filter -- " + args[1];
        }
        else if (!lf->is_enabled()) {
            retval = "info: filter already disabled";
        }
        else {
            lf->disable();
            lnav_data.ld_rl_view->
            rem_possibility(LNM_COMMAND, "disabled-filter", args[1]);
            lnav_data.ld_rl_view->
            add_possibility(LNM_COMMAND, "enabled-filter", args[1]);
            rebuild_indexes(true);
            retval = "info: filter disabled";
        }
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
            vis_line_t     vl = log_view.get_top();
            content_line_t cl = lnav_data.ld_log_source.at(vl);
            log_data_table *ldt = new log_data_table(cl, args[1]);

            lnav_data.ld_vtab_manager->register_vtab(ldt);

            lnav_data.ld_rl_view->add_possibility(LNM_COMMAND,
                                                  "custom-table",
                                                  args[1]);
            retval = "info: created new log table -- " + args[1];
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
            lnav_data.ld_rl_view->rem_possibility(LNM_COMMAND,
                                                  "custom-table",
                                                  args[1]);
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
    else if (args.size() > 2) {
        /* XXX put these in a map */
        if (args[1] != "highlight" &&
            args[1] != "filter-in" &&
            args[1] != "filter-out" &&
            args[1] != "enable-filter" &&
            args[1] != "disable-filter") {
            retval = "error: only the highlight and filter commands are "
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

void init_lnav_commands(readline_context::command_map_t &cmd_map)
{
    cmd_map["unix-time"]      = com_unix_time;
    cmd_map["current-time"]   = com_current_time;
    cmd_map["goto"]           = com_goto;
    cmd_map["graph"]          = com_graph;
    cmd_map["help"]           = com_help;
    cmd_map["highlight"]      = com_highlight;
    cmd_map["filter-in"]      = com_filter;
    cmd_map["filter-out"]     = com_filter;
    cmd_map["append-to"]      = com_save_to;
    cmd_map["write-to"]       = com_save_to;
    cmd_map["enable-filter"]  = com_enable_filter;
    cmd_map["disable-filter"] = com_disable_filter;
    cmd_map["create-logline-table"] = com_create_logline_table;
    cmd_map["delete-logline-table"] = com_delete_logline_table;
    cmd_map["session"]        = com_session;
}
