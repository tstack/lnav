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
 *
 * @file lnav.cc
 *
 * XXX This file has become a dumping ground for code and needs to be broken up
 * a bit.
 */

#include "config.h"

#include <stdio.h>
#include <errno.h>

#include <math.h>
#include <time.h>
#include <glob.h>
#include <locale.h>

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/socket.h>

#include <readline/readline.h>

#include <map>
#include <set>
#include <stack>
#include <vector>
#include <memory>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <functional>

#include <sqlite3.h>

#include "lnav.hh"
#include "help.hh"
#include "init-sql.hh"
#include "logfile.hh"
#include "lnav_util.hh"
#include "ansi_scrubber.hh"
#include "listview_curses.hh"
#include "statusview_curses.hh"
#include "vt52_curses.hh"
#include "readline_curses.hh"
#include "textview_curses.hh"
#include "logfile_sub_source.hh"
#include "textfile_sub_source.hh"
#include "grep_proc.hh"
#include "bookmarks.hh"
#include "hist_source.hh"
#include "top_status_source.hh"
#include "bottom_status_source.hh"
#include "piper_proc.hh"
#include "log_vtab_impl.hh"
#include "db_sub_source.hh"
#include "pcrecpp.h"
#include "termios_guard.hh"
#include "data_parser.hh"
#include "xterm_mouse.hh"
#include "lnav_commands.hh"
#include "column_namer.hh"
#include "log_data_table.hh"
#include "session_data.hh"
#include "lnav_config.hh"
#include "sql_util.hh"
#include "sqlite-extension-func.h"

#include "yajlpp.hh"

using namespace std;

#define HELP_MSG_1(x, msg) \
    "Press '" ANSI_BOLD(#x) "' " msg

#define HELP_MSG_2(x, y, msg) \
    "Press " ANSI_BOLD(#x) "/" ANSI_BOLD(#y) " " msg

static multimap<lnav_flags_t, string> DEFAULT_FILES;

struct _lnav_data lnav_data;

struct hist_level {
    int hl_bucket_size;
    int hl_group_size;
};

static struct hist_level HIST_ZOOM_VALUES[] = {
    { 24 * 60 * 60, 7 * 24 * 60 * 60 },
    {  4 * 60 * 60,     24 * 60 * 60 },
    {      60 * 60,     24 * 60 * 60 },
    {      10 * 60,          60 * 60 },
    {           60,          60 * 60 },
};

static const int HIST_ZOOM_LEVELS = sizeof(HIST_ZOOM_VALUES) /
                                    sizeof(struct hist_level);

static bookmark_type_t BM_EXAMPLE;
static bookmark_type_t BM_QUERY;

const char *lnav_view_strings[LNV__MAX] = {
    "log",
    "text",
    "help",
    "histogram",
    "graph",
    "db",
    "example",
};

class field_overlay_source : public list_overlay_source {
public:
    field_overlay_source()
        : fos_active(false),
          fos_active_prev(false),
          fos_scanner(NULL),
          fos_parser(NULL),
          fos_namer(NULL) { };

    ~field_overlay_source()
    {
        if (this->fos_scanner) {
            delete this->fos_scanner;
        }
        if (this->fos_parser) {
            delete this->fos_parser;
        }
        if (this->fos_namer) {
            delete this->fos_namer;
        }
    };

    size_t list_overlay_count(const listview_curses &lv)
    {
        logfile_sub_source &lss = lnav_data.ld_log_source;

        if (!this->fos_active) {
            return 0;
        }

        if (lss.text_line_count() == 0) {
            return 0;
        }

        content_line_t    cl   = lss.at(lv.get_top());
        logfile *         lf   = lss.find(cl);
        std::string       line = lf->read_line(lf->begin() + cl);
        struct line_range body;
        string_attrs_t    sa;

        this->fos_line_values.clear();
        this->fos_key_size = 0;

        lf->get_format()->annotate(line, sa, this->fos_line_values);

        for (std::vector<logline_value>::iterator iter =
                 this->fos_line_values.begin();
             iter != this->fos_line_values.end();
             ++iter) {
            this->fos_key_size = max(this->fos_key_size,
                                     (int)iter->lv_name.length());
        }

        body = find_string_attr_range(sa, "body");
        if (body.lr_end != -1) {
            line = line.substr(body.lr_start);
        }

        if (this->fos_parser) {
            delete this->fos_parser;
            delete this->fos_scanner;
            delete this->fos_namer;
        }

        this->fos_scanner = new data_scanner(line);
        this->fos_parser  = new data_parser(this->fos_scanner);
        this->fos_parser->parse();
        this->fos_namer = new column_namer();

        for (data_parser::element_list_t::iterator iter =
                 this->fos_parser->dp_pairs.begin();
             iter != this->fos_parser->dp_pairs.end();
             ++iter) {
            std::string colname = this->fos_parser->get_element_string(
                iter->e_sub_elements->front());

            colname            = this->fos_namer->add_column(colname);
            this->fos_key_size = max(this->fos_key_size,
                                     (int)colname.length());
        }

        return 1 +
               this->fos_line_values.size() +
               1 +
               this->fos_parser->dp_pairs.size();
    };

    bool list_value_for_overlay(const listview_curses &lv,
                                vis_line_t y,
                                attr_line_t &value_out)
    {
        if (!this->fos_active || this->fos_parser == NULL) {
            return false;
        }

        size_t total_count = (1 +
                              this->fos_line_values.size() +
                              1 +
                              this->fos_parser->dp_pairs.size());
        int  row       = (int)y - 1;
        bool last_line = (row == (int)(total_count - 1));

        if (row < 0 || row >= (int)total_count) {
            return false;
        }

        std::string &str = value_out.get_string();

        if (row == 0) {
            if (this->fos_line_values.empty()) {
                str = " No known message fields";
            }
            else{
                str = " Known message fields:";
            }
            return true;
        }
        row -= 1;
        if (row == (int)this->fos_line_values.size()) {
            if (this->fos_parser->dp_pairs.empty()) {
                str = " No discovered message fields";
            }
            else{
                str = " Discovered message fields:";
            }
            return true;
        }

        if (row < (int)this->fos_line_values.size()) {
            str = "   " + this->fos_line_values[row].lv_name;

            int padding = this->fos_key_size - str.length() + 3;

            str.append(padding, ' ');
            str += " = " + this->fos_line_values[row].to_string();
        }
        else {
            data_parser::element_list_t::iterator iter;

            row -= this->fos_line_values.size() + 1;

            iter = this->fos_parser->dp_pairs.begin();
            std::advance(iter, row);

            str = "   " + this->fos_namer->cn_names[row];
            int padding = this->fos_key_size - str.length() + 3;

            str.append(padding, ' ');
            str += " = " + this->fos_parser->get_element_string(
                iter->e_sub_elements->back());
        }

        string_attrs_t &  sa = value_out.get_attrs();
        struct line_range lr = { 1, 2 };
        sa[lr].insert(make_string_attr("graphic",
                                       last_line ? ACS_LLCORNER : ACS_LTEE));

        lr.lr_start = 3 + this->fos_key_size + 3;
        lr.lr_end   = -1;
        sa[lr].insert(make_string_attr("style", A_BOLD));

        return true;
    };

    bool          fos_active;
    bool          fos_active_prev;
    data_scanner *fos_scanner;
    data_parser * fos_parser;
    column_namer *fos_namer;
    std::vector<logline_value> fos_line_values;
    int fos_key_size;
};

static int handle_collation_list(void *ptr,
                                 int ncols,
                                 char **colvalues,
                                 char **colnames)
{
    lnav_data.ld_rl_view->add_possibility(LNM_SQL, "*", colvalues[1]);

    return 0;
}

static int handle_db_list(void *ptr,
                          int ncols,
                          char **colvalues,
                          char **colnames)
{
    lnav_data.ld_rl_view->add_possibility(LNM_SQL, "*", colvalues[1]);

    return 0;
}

static int handle_table_list(void *ptr,
                             int ncols,
                             char **colvalues,
                             char **colnames)
{
    lnav_data.ld_rl_view->add_possibility(LNM_SQL, "*", colvalues[0]);

    return 0;
}

static int handle_table_info(void *ptr,
                             int ncols,
                             char **colvalues,
                             char **colnames)
{
    lnav_data.ld_rl_view->add_possibility(LNM_SQL, "*", colvalues[1]);
    if (strcmp(colvalues[5], "1") == 0) {
        lnav_data.ld_db_key_names.push_back(colvalues[1]);
    }
    return 0;
}

static int handle_foreign_key_list(void *ptr,
                                   int ncols,
                                   char **colvalues,
                                   char **colnames)
{
    lnav_data.ld_db_key_names.push_back(colvalues[3]);
    lnav_data.ld_db_key_names.push_back(colvalues[4]);
    return 0;
}

struct sqlite_metadata_callbacks lnav_sql_meta_callbacks = {
    handle_collation_list,
    handle_db_list,
    handle_table_list,
    handle_table_info,
    handle_foreign_key_list,
};

bool setup_logline_table()
{
    textview_curses &log_view = lnav_data.ld_views[LNV_LOG];
    bool             retval   = false;

    if (log_view.get_inner_height()) {
        vis_line_t     vl = log_view.get_top();
        content_line_t cl = lnav_data.ld_log_source.at(vl);

        lnav_data.ld_vtab_manager->unregister_vtab("logline");
        lnav_data.ld_vtab_manager->register_vtab(new log_data_table(cl));

        retval = true;
    }

    lnav_data.ld_db_key_names.clear();
    lnav_data.ld_rl_view->clear_possibilities(LNM_SQL, "*");

    lnav_data.ld_rl_view->add_possibility(LNM_SQL, "*", sql_keywords);
    lnav_data.ld_rl_view->add_possibility(LNM_SQL, "*", sql_function_names);

    walk_sqlite_metadata(lnav_data.ld_db.in(), lnav_sql_meta_callbacks);

    {
        log_vtab_manager::iterator iter;

        for (iter = lnav_data.ld_vtab_manager->begin();
             iter != lnav_data.ld_vtab_manager->end();
             ++iter) {
            iter->second->get_foreign_keys(lnav_data.ld_db_key_names);
        }
    }

    stable_sort(lnav_data.ld_db_key_names.begin(),
                lnav_data.ld_db_key_names.end());

    return retval;
}

/**
 * Observer for loading progress that updates the bottom status bar.
 */
class loading_observer
    : public logfile_sub_source::observer {
public:
    loading_observer()
        : lo_last_offset(0),
          lo_last_line(0) { };

    void logfile_indexing(logfile &lf, off_t off, size_t total)
    {
        /* XXX assert(off <= total); */
        if (off > (off_t)total) {
            off = total;
        }

        if ((std::abs((long int)(off - this->lo_last_offset)) >
             (off_t)(128 * 1024)) ||
            (size_t)off == total) {
            lnav_data.ld_bottom_source.update_loading(off, total);
            this->do_update();
            this->lo_last_offset = off;
        }

        if (!lnav_data.ld_looping) {
            throw logfile::error(lf.get_filename(), EINTR);
        }
    };

    void logfile_sub_source_filtering(logfile_sub_source &lss,
                                      content_line_t cl,
                                      size_t total)
    {
        if (std::abs(cl - this->lo_last_line) > 1024 || (size_t)cl ==
            (total - 1)) {
            lnav_data.ld_bottom_source.update_loading(cl, (total - 1));
            this->do_update();
            this->lo_last_line = cl;
        }

        if (!lnav_data.ld_looping) {
            throw logfile::error("", EINTR);
        }
    };

private:
    void do_update(void)
    {
        lnav_data.ld_top_source.update_time();
        lnav_data.ld_status[LNS_TOP].do_update();
        lnav_data.ld_status[LNS_BOTTOM].do_update();
        refresh();
    };

    off_t          lo_last_offset;
    content_line_t lo_last_line;
};

static void rebuild_hist(size_t old_count, bool force)
{
    textview_curses &   hist_view = lnav_data.ld_views[LNV_HISTOGRAM];
    logfile_sub_source &lss       = lnav_data.ld_log_source;
    size_t       new_count        = lss.text_line_count();
    hist_source &hs         = lnav_data.ld_hist_source;
    int          zoom_level = lnav_data.ld_hist_zoom;
    time_t       old_time;
    int          lpc;

    old_time = hs.value_for_row(hist_view.get_top());
    hs.set_bucket_size(HIST_ZOOM_VALUES[zoom_level].hl_bucket_size);
    hs.set_group_size(HIST_ZOOM_VALUES[zoom_level].hl_group_size);
    if (force) {
        hs.clear();
    }
    for (lpc = old_count; lpc < (int)new_count; lpc++) {
        logline *ll = lss.find_line(lss.at(vis_line_t(lpc)));

        if (!(ll->get_level() & logline::LEVEL_CONTINUED)) {
            hs.add_value(ll->get_time(),
                         bucket_type_t(ll->get_level() &
                                       ~logline::LEVEL__FLAGS));
        }
    }
    hs.analyze();
    hist_view.reload_data();
    hist_view.set_top(hs.row_for_value(old_time));
}

void rebuild_indexes(bool force)
{
    static loading_observer obs;

    logfile_sub_source &lss       = lnav_data.ld_log_source;
    textview_curses &   log_view  = lnav_data.ld_views[LNV_LOG];
    textview_curses &   text_view = lnav_data.ld_views[LNV_TEXT];
    vis_line_t          old_bottom(0), height(0);
    content_line_t      top_content = content_line_t(-1);

    unsigned long width;
    bool          scroll_down;
    size_t        old_count;
    time_t        old_time;


    old_count = lss.text_line_count();

    if (old_count) {
        top_content = lss.at(log_view.get_top());
    }

    {
        textfile_sub_source *          tss = &lnav_data.ld_text_source;
        std::list<logfile *>::iterator iter;
        size_t new_count;

        text_view.get_dimensions(height, width);
        old_bottom  = text_view.get_top() + height;
        scroll_down = (size_t)old_bottom > tss->text_line_count();

        for (iter = tss->tss_files.begin();
             iter != tss->tss_files.end(); ) {
            (*iter)->rebuild_index(&obs);
            if ((*iter)->get_format() != NULL) {
                logfile *lf = *iter;

                if (lnav_data.ld_log_source.insert_file(lf)) {
                    iter  = tss->tss_files.erase(iter);
                    force = true;
                }
                else {
                    ++iter;
                }
            }
            else {
                ++iter;
            }
        }

        text_view.reload_data();

        new_count = tss->text_line_count();
        if (scroll_down && new_count >= (size_t)old_bottom) {
            text_view.set_top(vis_line_t(new_count - height + 1));
        }
    }

    old_time = lnav_data.ld_top_time;
    log_view.get_dimensions(height, width);
    old_bottom  = log_view.get_top() + height;
    scroll_down = (size_t)old_bottom > old_count;
    if (force) {
        old_count = 0;
    }
    if (lss.rebuild_index(&obs, force)) {
        size_t      new_count = lss.text_line_count();
        grep_line_t start_line;
        int         lpc;

        log_view.reload_data();

        if (scroll_down && new_count >= (size_t)old_bottom) {
            log_view.set_top(vis_line_t(new_count - height + 1));
        }
        else if (!scroll_down && force) {
            content_line_t new_top_content = content_line_t(-1);

            if (new_count) {
                new_top_content = lss.at(log_view.get_top());
            }

            if (new_top_content != top_content) {
                log_view.set_top(lss.find_from_time(old_time));
            }
        }

        rebuild_hist(old_count, force);

        start_line = force ? grep_line_t(0) : grep_line_t(-1);

        if (force) {
            log_view.match_reset();
        }

        for (lpc = 0; lpc < LG__MAX; lpc++) {
            if (lnav_data.ld_grep_child[lpc].get() != NULL) {
                lnav_data.ld_grep_child[lpc]->get_grep_proc()->
                queue_request(start_line);
                lnav_data.ld_grep_child[lpc]->get_grep_proc()->start();
            }
        }
        if (lnav_data.ld_search_child[LNV_LOG].get() != NULL) {
            lnav_data.ld_search_child[LNV_LOG]->get_grep_proc()->reset();
            lnav_data.ld_search_child[LNV_LOG]->get_grep_proc()->
            queue_request(start_line);
            lnav_data.ld_search_child[LNV_LOG]->get_grep_proc()->start();
        }
    }

    lnav_data.ld_bottom_source.update_filtered(lss);
    lnav_data.ld_scroll_broadcaster.invoke(lnav_data.ld_view_stack.top());
}

class plain_text_source
    : public text_sub_source {
public:
    plain_text_source(string text)
    {
        size_t start = 0, end;

        while ((end = text.find('\n', start)) != string::npos) {
            this->tds_lines.push_back(text.substr(start, end - start));
            start = end + 1;
        }
        this->tds_lines.push_back(text.substr(start));
    };

    size_t text_line_count()
    {
        return this->tds_lines.size();
    };

    void text_value_for_line(textview_curses &tc,
                             int row,
                             string &value_out,
                             bool no_scrub)
    {
        value_out = this->tds_lines[row];
    };

private:
    vector<string> tds_lines;
};

class time_label_source
    : public hist_source::label_source {
public:
    time_label_source() { };

    void hist_label_for_bucket(int bucket_start_value,
                               const hist_source::bucket_t &bucket,
                               string &label_out)
    {
        hist_source::bucket_t::const_iterator iter;
        int        total       = 0, errors = 0, warnings = 0;
        time_t     bucket_time = bucket_start_value;
        struct tm *bucket_tm;
        char       buffer[128];
        int        len;

        bucket_tm = gmtime((time_t *)&bucket_time);
        if (bucket_tm) {
            strftime(buffer, sizeof(buffer),
                     " %a %b %d %H:%M  ",
                     bucket_tm);
        }
        else {
            fprintf(stderr, "bad time %d\n", bucket_start_value);
            buffer[0] = '\0';
        }
        for (iter = bucket.begin(); iter != bucket.end(); iter++) {
            total += (int)iter->second;
            switch (iter->first) {
            case logline::LEVEL_FATAL:
            case logline::LEVEL_ERROR:
            case logline::LEVEL_CRITICAL:
                errors += (int)iter->second;
                break;

            case logline::LEVEL_WARNING:
                warnings += (int)iter->second;
                break;
            }
        }

        len = strlen(buffer);
        snprintf(&buffer[len], sizeof(buffer) - len,
                 " %8d total  %8d errors  %8d warnings",
                 total, errors, warnings);

        label_out = string(buffer);
    };
};

static bool append_default_files(lnav_flags_t flag)
{
    bool retval = true;

    if (lnav_data.ld_flags & flag) {
        pair<multimap<lnav_flags_t, string>::iterator,
             multimap<lnav_flags_t, string>::iterator> range;
        bool found = false;

        for (range = DEFAULT_FILES.equal_range(flag);
             range.first != range.second && !found;
             range.first++) {
            string      path = range.first->second;
            struct stat st;

            if (access(path.c_str(), R_OK) == 0) {
                auto_mem<char> abspath;

                path = get_current_dir() + range.first->second;
                if ((abspath = realpath(path.c_str(), NULL)) == NULL) {
                    perror("Unable to resolve path");
                }
                else {
                    lnav_data.ld_file_names.insert(make_pair(abspath.in(),
                                                             -1));
                    found = true;
                }
            }
            else if (stat(path.c_str(), &st) == 0) {
                fprintf(stderr,
                        "error: cannot read -- %s%s\n",
                        get_current_dir().c_str(),
                        path.c_str());
                retval = false;
            }
        }
    }

    return retval;
}

static void sigint(int sig)
{
    lnav_data.ld_looping = false;
}

static void sigwinch(int sig)
{
    lnav_data.ld_winched = true;
}

static void back_ten(int ten_minute)
{
    textview_curses *   tc  = lnav_data.ld_view_stack.top();
    logfile_sub_source *lss;

    lss = dynamic_cast<logfile_sub_source *>(tc->get_sub_source());

    if (!lss)
        return;

    time_t hour = rounddown_offset(lnav_data.ld_top_time,
                                   60 * 60,
                                   ten_minute * 10 * 60);
    vis_line_t line = lss->find_from_time(hour);

    --line;
    lnav_data.ld_view_stack.top()->set_top(line);
}

static void update_view_name(void)
{
    static const char *view_names[LNV__MAX] = {
        "LOG",
        "TEXT",
        "HELP",
        "HIST",
        "GRAPH",
        "DB",
        "EXAMPLE",
    };

    status_field &sf = lnav_data.ld_top_source.statusview_value_for_field(
        top_status_source::TSF_VIEW_NAME);
    textview_curses * tc = lnav_data.ld_view_stack.top();
    struct line_range lr = { 0, -1 };

    sf.set_value("% 5s ", view_names[tc - lnav_data.ld_views]);
    sf.get_value().get_attrs()[lr].insert(make_string_attr(
                                              "style", A_REVERSE |
                                              COLOR_PAIR(view_colors::
                                                         VC_BLUE_ON_WHITE)));
}

bool toggle_view(textview_curses *toggle_tc)
{
    textview_curses *tc     = lnav_data.ld_view_stack.top();
    bool             retval = false;

    if (tc == toggle_tc) {
        lnav_data.ld_view_stack.pop();
    }
    else {
        lnav_data.ld_view_stack.push(toggle_tc);
        retval = true;
    }
    tc = lnav_data.ld_view_stack.top();
    tc->set_needs_update();
    lnav_data.ld_scroll_broadcaster.invoke(tc);

    update_view_name();

    return retval;
}

static void change_text_file(void)
{
    textview_curses *tc = &lnav_data.ld_views[LNV_TEXT];

    tc->reload_data();
    if (lnav_data.ld_search_child[LNV_TEXT].get() != NULL) {
        grep_proc *gp = lnav_data.ld_search_child[LNV_TEXT]->get_grep_proc();

        tc->match_reset();
        gp->reset();
        gp->queue_request(grep_line_t(0));
        gp->start();
    }
    lnav_data.ld_scroll_broadcaster.invoke(tc);
}

/**
 * Ensure that the view is on the top of the view stack.
 *
 * @param expected_tc The text view that should be on top.
 */
void ensure_view(textview_curses *expected_tc)
{
    textview_curses *tc = lnav_data.ld_view_stack.top();

    if (tc != expected_tc) {
        toggle_view(expected_tc);
    }
}

static void moveto_cluster(vis_line_t(bookmark_vector<vis_line_t>::*f) (
                               vis_line_t),
                           bookmark_type_t *bt,
                           vis_line_t top)
{
    textview_curses *tc = lnav_data.ld_view_stack.top();

    if (tc != &lnav_data.ld_views[LNV_LOG]) {
        flash();
    }
    else {
        logfile_sub_source &lss = lnav_data.ld_log_source;
        vis_bookmarks &     bm  = tc->get_bookmarks();
        vis_line_t          vl(-1), last_top(top);

        logline::level_t last_level;
        bool             done = false;
        time_t           last_time;
        logline *        ll;

        ll         = lss.find_line(lss.at(top));
        last_time  = ll->get_time();
        last_level = ll->get_level();
        while (vl == -1 && (top = (bm[bt].*f)(top)) != -1) {
            ll = lss.find_line(lss.at(top));
            if (std::abs(last_top - top) > 1 ||
                ll->get_level() != last_level ||
                ll->get_time() != last_time) {
                last_time  = ll->get_time();
                last_level = ll->get_level();
                vl         = top;
            }
            last_top = top;
        }
        while (vl > 0 && !done) {
            ll = lss.find_line(lss.at(vis_line_t(vl - 1)));
            if (ll->get_level() != last_level || ll->get_time() != last_time) {
                done = true;
            }
            else {
                --vl;
            }
        }
        tc->set_top(vl);
    }
}

static void check_for_clipboard(FILE **pfile, const char *execstr)
{
    if (execstr == NULL || pfile == NULL || *pfile != NULL) {
        return;
    }

    if ((*pfile = popen(execstr, "w")) != NULL && pclose(*pfile) == 0) {
        *pfile = popen(execstr, "w");
    }
    else {
        *pfile = NULL;
    }

    return;
}

/* XXX For one, this code is kinda crappy.  For two, we should probably link
 * directly with X so we don't need to have xclip installed and it'll work if
 * we're ssh'd into a box.
 */
static void copy_to_xclip(void)
{
    textview_curses *tc = lnav_data.ld_view_stack.top();

    bookmark_vector<vis_line_t> &bv =
        tc->get_bookmarks()[&textview_curses::BM_USER];
    bookmark_vector<vis_line_t>::iterator iter;
    FILE * pfile      = NULL;
    int    line_count = 0;
    string line;

    /* XXX : Check if this is linux or MAC. Probably not the best solution but */
    /* better than traversing the PATH to stat for the binaries or trying to */
    /* forkexec. */
    check_for_clipboard(&pfile, "xclip -i > /dev/null 2>&1");
    check_for_clipboard(&pfile, "pbcopy > /dev/null 2>&1");

    if (!pfile) {
        flash();
        lnav_data.ld_rl_view->set_value(
            "error: Unable to copy to clipboard.  "
            "Make sure xclip or pbcopy is installed.");
        return;
    }

    for (iter = bv.begin(); iter != bv.end(); iter++) {
        tc->grep_value_for_line(*iter, line);
        fprintf(pfile, "%s\n", line.c_str());
        line_count += 1;
    }

    pclose(pfile);
    pfile = NULL;

    char buffer[128];

    snprintf(buffer, sizeof(buffer),
             "Copied " ANSI_BOLD("%d") " lines to the clipboard",
             line_count);
    lnav_data.ld_rl_view->set_value(buffer);
}

static void handle_paging_key(int ch)
{
    textview_curses *   tc  = lnav_data.ld_view_stack.top();
    logfile_sub_source *lss = NULL;
    vis_bookmarks &     bm  = tc->get_bookmarks();

    if (tc->handle_key(ch)) {
        return;
    }

    lss = dynamic_cast<logfile_sub_source *>(tc->get_sub_source());

    /* process the command keystroke */
    switch (ch) {
    case 'q':
    case 'Q':
    {
        string msg = "";

        if (tc == &lnav_data.ld_views[LNV_DB]) {
            msg = HELP_MSG_2(v, V, "to switch to the SQL result view");
        }
        else if (tc == &lnav_data.ld_views[LNV_HISTOGRAM]) {
            msg = HELP_MSG_2(i, I, "to switch to the histogram view");
        }
        else if (tc == &lnav_data.ld_views[LNV_TEXT]) {
            msg = HELP_MSG_1(t, "to switch to the text file view");
        }
        else if (tc == &lnav_data.ld_views[LNV_GRAPH]) {
            msg = HELP_MSG_1(g, "to switch to the graph view");
        }

        lnav_data.ld_rl_view->set_alt_value(msg);
    }
        lnav_data.ld_view_stack.pop();
        if (lnav_data.ld_view_stack.empty() ||
            (lnav_data.ld_view_stack.size() == 1 &&
             lnav_data.ld_log_source.text_line_count() == 0)) {
            lnav_data.ld_looping = false;
        }
        else {
            tc = lnav_data.ld_view_stack.top();
            tc->set_needs_update();
            lnav_data.ld_scroll_broadcaster.invoke(tc);
            update_view_name();
        }
        break;

    case 'c':
        copy_to_xclip();
        break;

    case 'C':
        if (lss) {
            lss->get_user_bookmarks()[&textview_curses::BM_USER].clear();
            tc->reload_data();

            lnav_data.ld_rl_view->set_value("Cleared bookmarks");
        }
        break;

    case 'e':
        moveto_cluster(&bookmark_vector<vis_line_t>::next,
                       &logfile_sub_source::BM_ERRORS,
                       tc->get_top());
        lnav_data.ld_rl_view->set_alt_value(HELP_MSG_2(
                                                w, W,
                                                "to move forward/backward through warning messages"));
        break;

    case 'E':
        moveto_cluster(&bookmark_vector<vis_line_t>::prev,
                       &logfile_sub_source::BM_ERRORS,
                       tc->get_top());
        lnav_data.ld_rl_view->set_alt_value(HELP_MSG_2(
                                                w, W,
                                                "to move forward/backward through warning messages"));
        break;

    case 'w':
        moveto_cluster(&bookmark_vector<vis_line_t>::next,
                       &logfile_sub_source::BM_WARNINGS,
                       tc->get_top());
        lnav_data.ld_rl_view->set_alt_value(HELP_MSG_2(
                                                o, O,
                                                "to move forward/backward an hour"));
        break;

    case 'W':
        moveto_cluster(&bookmark_vector<vis_line_t>::prev,
                       &logfile_sub_source::BM_WARNINGS,
                       tc->get_top());
        lnav_data.ld_rl_view->set_alt_value(HELP_MSG_2(
                                                o, O,
                                                "to move forward/backward an hour"));
        break;

    case 'n':
        tc->set_top(bm[&textview_curses::BM_SEARCH].next(tc->get_top()));
        lnav_data.ld_rl_view->set_alt_value(
            "Press '" ANSI_BOLD(">") "' or '" ANSI_BOLD("<")
            "' to scroll horizontally to a search result");
        break;

    case 'N':
        tc->set_top(bm[&textview_curses::BM_SEARCH].prev(tc->get_top()));
        lnav_data.ld_rl_view->set_alt_value(
            "Press '" ANSI_BOLD(">") "' or '" ANSI_BOLD("<")
            "' to scroll horizontally to a search result");
        break;

    case 'y':
        tc->set_top(bm[&BM_QUERY].next(tc->get_top()));
        break;

    case 'Y':
        tc->set_top(bm[&BM_QUERY].prev(tc->get_top()));
        break;

    case '>':
    {
        std::pair<int, int> range;

        tc->horiz_shift(tc->get_top(),
                        tc->get_bottom(),
                        tc->get_left(),
                        "$search",
                        range);
        if (range.second != INT_MAX) {
            tc->set_left(range.second);
            lnav_data.ld_rl_view->set_alt_value(
                HELP_MSG_1(m, "to bookmark a line"));
        }
        else{
            flash();
        }
    }
    break;

    case '<':
        if (tc->get_left() == 0) {
            flash();
        }
        else {
            std::pair<int, int> range;

            tc->horiz_shift(tc->get_top(),
                            tc->get_bottom(),
                            tc->get_left(),
                            "$search",
                            range);
            if (range.first != -1) {
                tc->set_left(range.first);
            }
            else{
                tc->set_left(0);
            }
            lnav_data.ld_rl_view->set_alt_value(
                HELP_MSG_1(m, "to bookmark a line"));
        }
        break;

    case 'f':
        if (tc == &lnav_data.ld_views[LNV_LOG]) {
            tc->set_top(bm[&logfile_sub_source::BM_FILES].next(tc->get_top()));
        }
        else if (tc == &lnav_data.ld_views[LNV_TEXT]) {
            textfile_sub_source &tss = lnav_data.ld_text_source;

            if (!tss.tss_files.empty()) {
                tss.tss_files.push_front(tss.tss_files.back());
                tss.tss_files.pop_back();
                change_text_file();
            }
        }
        break;

    case 'F':
        if (tc == &lnav_data.ld_views[LNV_LOG]) {
            tc->set_top(bm[&logfile_sub_source::BM_FILES].prev(tc->get_top()));
        }
        else if (tc == &lnav_data.ld_views[LNV_TEXT]) {
            textfile_sub_source &tss = lnav_data.ld_text_source;

            if (!tss.tss_files.empty()) {
                tss.tss_files.push_back(tss.tss_files.front());
                tss.tss_files.pop_front();
                change_text_file();
            }
        }
        break;

    case 'z':
        if (tc == &lnav_data.ld_views[LNV_HISTOGRAM]) {
            if ((lnav_data.ld_hist_zoom + 1) >= HIST_ZOOM_LEVELS) {
                flash();
            }
            else {
                lnav_data.ld_hist_zoom += 1;
                rebuild_hist(0, true);
            }

            lnav_data.ld_rl_view->set_alt_value(HELP_MSG_1(
                                                    I,
                                                    "to switch to the log view at the top displayed time"));
        }
        break;

    case 'Z':
        if (tc == &lnav_data.ld_views[LNV_HISTOGRAM]) {
            if (lnav_data.ld_hist_zoom == 0) {
                flash();
            }
            else {
                lnav_data.ld_hist_zoom -= 1;
                rebuild_hist(0, true);
            }

            lnav_data.ld_rl_view->set_alt_value(HELP_MSG_1(
                                                    I,
                                                    "to switch to the log view at the top displayed time"));
        }
        break;

    case 'u':
        tc->set_top(tc->get_bookmarks()[&textview_curses::BM_USER].
                    next(tc->get_top()));
        break;

    case 'U':
        tc->set_top(tc->get_bookmarks()[&textview_curses::BM_USER].
                    prev(tc->get_top()));
        break;

    case 'm':
        lnav_data.ld_last_user_mark[tc] = tc->get_top();
        tc->toggle_user_mark(&textview_curses::BM_USER,
                             vis_line_t(lnav_data.ld_last_user_mark[tc]));
        tc->reload_data();

        lnav_data.ld_rl_view->set_alt_value(HELP_MSG_2(
                                            u, U,
                                            "to move forward/backward through user bookmarks"));
        break;

    case 'J':
        if (lnav_data.ld_last_user_mark.find(tc) ==
            lnav_data.ld_last_user_mark.end() ||
            !tc->is_visible(vis_line_t(lnav_data.ld_last_user_mark[tc]))) {
            lnav_data.ld_last_user_mark[tc] = tc->get_top();
        }
        else {
            vis_line_t    height;
            unsigned long width;

            tc->get_dimensions(height, width);
            if (lnav_data.ld_last_user_mark[tc] > tc->get_bottom() - 2 &&
                tc->get_top() + height < tc->get_inner_height()) {
                tc->shift_top(vis_line_t(1));
            }
            if (lnav_data.ld_last_user_mark[tc] + 1 >=
                tc->get_inner_height()) {
                break;
            }
            lnav_data.ld_last_user_mark[tc] += 1;
        }
        tc->toggle_user_mark(&textview_curses::BM_USER,
                             vis_line_t(lnav_data.ld_last_user_mark[tc]));
        tc->reload_data();

        lnav_data.ld_rl_view->set_alt_value(HELP_MSG_1(
                                            c,
                                            "to copy marked lines to the clipboard"));
        break;

    case 'K':
        {
            int new_mark;

            if (lnav_data.ld_last_user_mark.find(tc) ==
                lnav_data.ld_last_user_mark.end() ||
                !tc->is_visible(vis_line_t(lnav_data.ld_last_user_mark[tc]))) {
                lnav_data.ld_last_user_mark[tc] = -1;
                new_mark = tc->get_top();
            }
            else {
                new_mark = lnav_data.ld_last_user_mark[tc];
            }

            tc->toggle_user_mark(&textview_curses::BM_USER,
                                 vis_line_t(new_mark));
            if (new_mark == tc->get_top()) {
                tc->shift_top(vis_line_t(-1));
            }
            if (new_mark > 0) {
                lnav_data.ld_last_user_mark[tc] = new_mark - 1;
            }
            else {
                lnav_data.ld_last_user_mark[tc] = new_mark;
                flash();
            }
            tc->reload_data();

            lnav_data.ld_rl_view->set_alt_value(HELP_MSG_1(
                                                    c,
                                                    "to copy marked lines to the clipboard"));
        }
        break;

    case 'M':
        if (lnav_data.ld_last_user_mark.find(tc) ==
            lnav_data.ld_last_user_mark.end()) {
            flash();
        }
        else {
            int start_line = min((int)tc->get_top(),
                                 lnav_data.ld_last_user_mark[tc] + 1);
            int end_line = max((int)tc->get_top(),
                               lnav_data.ld_last_user_mark[tc] - 1);

            tc->toggle_user_mark(&textview_curses::BM_USER,
                                  vis_line_t(start_line),
                                  vis_line_t(end_line));
            tc->reload_data();
        }
        break;

    case 'S':
        {
            bookmark_vector<vis_line_t>::iterator iter;

            for (iter = bm[&textview_curses::BM_SEARCH].begin();
                 iter != bm[&textview_curses::BM_SEARCH].end();
                 ++iter) {
                tc->toggle_user_mark(&textview_curses::BM_USER, *iter);
            }

            lnav_data.ld_last_user_mark[tc] = -1;
            tc->reload_data();
        }
        break;

    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
        if (lss) {
            int    ten_minute = (ch - '0') * 10 * 60;
            time_t hour       = rounddown(lnav_data.ld_top_time +
                                          (60 * 60) -
                                          ten_minute +
                                          1,
                                          60 * 60);
            vis_line_t line = lss->find_from_time(hour + ten_minute);

            tc->set_top(line);
        }
        break;

    case '!':
        back_ten(1);
        break;

    case '@':
        back_ten(2);
        break;

    case '#':
        back_ten(3);
        break;

    case '$':
        back_ten(4);
        break;

    case '%':
        back_ten(5);
        break;

    case '^':
        back_ten(6);
        break;

    case '9':
        if (lss) {
            double tenth = ((double)tc->get_inner_height()) / 10.0;

            tc->shift_top(vis_line_t(tenth));
        }
        break;

    case '(':
        if (lss) {
            double tenth = ((double)tc->get_inner_height()) / 10.0;

            tc->shift_top(vis_line_t(-tenth));
        }
        break;

    case '0':
        if (lss) {
            time_t     first_time = lnav_data.ld_top_time;
            int        step       = 24 * 60 * 60;
            vis_line_t line       =
                lss->find_from_time(roundup(first_time, step));

            tc->set_top(line);
        }
        break;

    case ')':
        if (lss) {
            time_t     day  = rounddown(lnav_data.ld_top_time, 24 * 60 * 60);
            vis_line_t line = lss->find_from_time(day);

            --line;
            tc->set_top(line);
        }
        break;

    case 'D':
    case 'O':
        if (tc->get_top() == 0) {
            flash();
        }
        else if (lss) {
            int        step     = ch == 'D' ? (24 * 60 * 60) : (60 * 60);
            time_t     top_time = lnav_data.ld_top_time;
            vis_line_t line     = lss->find_from_time(top_time - step);

            if (line != 0) {
                --line;
            }
            tc->set_top(line);

            lnav_data.ld_rl_view->set_alt_value(HELP_MSG_1(/, "to search"));
        }
        break;

    case 'd':
    case 'o':
        if (lss) {
            int        step = ch == 'd' ? (24 * 60 * 60) : (60 * 60);
            vis_line_t line =
                lss->find_from_time(lnav_data.ld_top_time + step);

            tc->set_top(line);

            lnav_data.ld_rl_view->set_alt_value(HELP_MSG_1(/, "to search"));
        }
        break;

    case 's':
        lnav_data.ld_log_source.toggle_scrub();
        tc->reload_data();
        break;

    case ':':
        if (lnav_data.ld_views[LNV_LOG].get_inner_height() > 0) {
            logfile_sub_source &lss      = lnav_data.ld_log_source;
            textview_curses &   log_view = lnav_data.ld_views[LNV_LOG];
            content_line_t      cl       = lss.at(log_view.get_top());
            logfile *           lf       = lss.find(cl);
            std::string         line     = lf->read_line(
                lf->begin() + cl);
            struct line_range          body;
            string_attrs_t             sa;
            std::vector<logline_value> line_values;

            lf->get_format()->annotate(line, sa, line_values);

            body = find_string_attr_range(sa, "body");
            if (body.lr_end != -1) {
                line = line.substr(body.lr_start);
            }

            data_scanner ds(line);
            data_parser  dp(&ds);
            dp.parse();

            column_namer namer;

            lnav_data.ld_rl_view->clear_possibilities(LNM_COMMAND, "colname");
            for (data_parser::element_list_t::iterator iter =
                     dp.dp_pairs.begin();
                 iter != dp.dp_pairs.end();
                 ++iter) {
                std::string colname = dp.get_element_string(
                    iter->e_sub_elements->front());

                colname = namer.add_column(colname);
                lnav_data.ld_rl_view->add_possibility(LNM_COMMAND, "colname",
                                                      colname);
            }
        }
        lnav_data.ld_mode = LNM_COMMAND;
        lnav_data.ld_rl_view->focus(LNM_COMMAND, ":");
        lnav_data.ld_bottom_source.set_prompt("Enter an lnav command:");
        break;

    case '/':
        lnav_data.ld_mode = LNM_SEARCH;
        lnav_data.ld_search_start_line = lnav_data.ld_view_stack.top()->
                                         get_top();
        lnav_data.ld_rl_view->focus(LNM_SEARCH, "/");
        lnav_data.ld_bottom_source.set_prompt(
            "Enter a regular expression to search for:");
        break;

    case ';':
        if (tc == &lnav_data.ld_views[LNV_LOG] ||
            tc == &lnav_data.ld_views[LNV_DB]) {
            textview_curses &log_view = lnav_data.ld_views[LNV_LOG];

            lnav_data.ld_mode = LNM_SQL;
            setup_logline_table();
            lnav_data.ld_rl_view->focus(LNM_SQL, ";");

            lnav_data.ld_bottom_source.update_loading(0, 0);
            lnav_data.ld_status[LNS_BOTTOM].do_update();

            field_overlay_source *fos;

            fos = (field_overlay_source *)log_view.get_overlay_source();
            fos->fos_active_prev = fos->fos_active;
            if (!fos->fos_active) {
                fos->fos_active = true;
                tc->reload_data();
            }
            lnav_data.ld_bottom_source.set_prompt(
                "Enter an SQL query:");
        }
        break;

    case 'p':
        field_overlay_source *fos;

        fos =
            (field_overlay_source *)lnav_data.ld_views[LNV_LOG].
            get_overlay_source();
        fos->fos_active = !fos->fos_active;
        tc->reload_data();
        break;

    case 't':
        if (lnav_data.ld_text_source.current_file() == NULL) {
            flash();
            lnav_data.ld_rl_view->set_value("No text files loaded");
        }
        else if (toggle_view(&lnav_data.ld_views[LNV_TEXT])) {
            lnav_data.ld_rl_view->set_alt_value(HELP_MSG_2(
                                                    f, F,
                                                    "to switch to the next/previous file"));
        }
        break;

    case 'T':
        lnav_data.ld_log_source.toggle_time_offset();
        tc->reload_data();
        break;

    case 'i':
        if (toggle_view(&lnav_data.ld_views[LNV_HISTOGRAM])) {
            lnav_data.ld_rl_view->set_alt_value(
                HELP_MSG_2(z, Z, "to zoom in/out"));
        }
        else {
            lnav_data.ld_rl_view->set_alt_value("");
        }
        break;

    case 'I':
    {
        time_t log_top = lnav_data.ld_top_time;

        time_t hist_top =
            lnav_data.ld_hist_source.value_for_row(tc->get_top());

        if (toggle_view(&lnav_data.ld_views[LNV_HISTOGRAM])) {
            hist_source &hs = lnav_data.ld_hist_source;

            tc = lnav_data.ld_view_stack.top();
            tc->set_top(hs.row_for_value(log_top));
        }
        else {
            tc  = &lnav_data.ld_views[LNV_LOG];
            lss = &lnav_data.ld_log_source;
            tc->set_top(lss->find_from_time(hist_top));
            tc->set_needs_update();
        }
    }
    break;

    case 'g':
        toggle_view(&lnav_data.ld_views[LNV_GRAPH]);
        break;

    case '?':
        toggle_view(&lnav_data.ld_views[LNV_HELP]);
        break;

    case 'v':
        toggle_view(&lnav_data.ld_views[LNV_DB]);
        break;

    case 'V':
    {
        textview_curses *db_tc = &lnav_data.ld_views[LNV_DB];
        db_label_source &dls   = lnav_data.ld_db_rows;
        hist_source &    hs    = lnav_data.ld_db_source;

        if (toggle_view(db_tc)) {
            unsigned int lpc;

            for (lpc = 0; lpc < dls.dls_headers.size(); lpc++) {
                if (dls.dls_headers[lpc] != "log_line") {
                    continue;
                }

                char         linestr[64];
                int          line_number = (int)tc->get_top();
                unsigned int row;

                snprintf(linestr, sizeof(linestr), "%d", line_number);
                for (row = 0; row < dls.dls_rows.size(); row++) {
                    if (strcmp(dls.dls_rows[row][lpc].c_str(),
                               linestr) == 0) {
                        vis_line_t db_line(hs.row_for_value(row));

                        db_tc->set_top(db_line);
                        db_tc->set_needs_update();
                        break;
                    }
                }
                break;
            }
        }
        else {
            int          db_row = hs.value_for_row(db_tc->get_top());
            unsigned int lpc;

            for (lpc = 0; lpc < dls.dls_headers.size(); lpc++) {
                if (dls.dls_headers[lpc] != "log_line") {
                    continue;
                }

                unsigned int line_number;

                tc = &lnav_data.ld_views[LNV_LOG];
                if (sscanf(dls.dls_rows[db_row][lpc].c_str(),
                           "%d",
                           &line_number) &&
                    line_number < tc->listview_rows(*tc)) {
                    tc->set_top(vis_line_t(line_number));
                    tc->set_needs_update();
                }
                break;
            }
        }
    }
    break;

    case 'x':
        if (tc == &lnav_data.ld_views[LNV_LOG]) {
            lnav_data.ld_log_source.toggle_user_mark(&BM_EXAMPLE,
                                                     vis_line_t(tc->get_top()));
        }
        break;

    case '\\':
    {
        vis_bookmarks &bm = tc->get_bookmarks();
        string         ex;

        for (bookmark_vector<vis_line_t>::iterator iter =
                 bm[&BM_EXAMPLE].begin();
             iter != bm[&BM_EXAMPLE].end();
             ++iter) {
            string line;

            tc->get_sub_source()->text_value_for_line(*tc, *iter, line);
            ex += line + "\n";
        }
        lnav_data.ld_views[LNV_EXAMPLE].set_sub_source(new plain_text_source(
                                                           ex));
        ensure_view(&lnav_data.ld_views[LNV_EXAMPLE]);
    }
    break;

    case 'r':
        lnav_data.ld_session_file_index =
            (lnav_data.ld_session_file_index + 1) %
            lnav_data.ld_session_file_names.size();
        reset_session();
        load_session();
        rebuild_indexes(true);
        break;

    case 'R':
        if (lnav_data.ld_session_file_index == 0) {
            lnav_data.ld_session_file_index =
                lnav_data.ld_session_file_names.size() - 1;
        }
        else{
            lnav_data.ld_session_file_index -= 1;
        }
        reset_session();
        load_session();
        rebuild_indexes(true);
        break;

    case KEY_CTRL_R:
        reset_session();
        rebuild_indexes(true);
        lnav_data.ld_rl_view->set_alt_value(HELP_MSG_2(
                                                r, R,
                                                "to restore the next/previous session"));
        break;

    default:
        fprintf(stderr, "unhandled %d\n", ch);
        lnav_data.ld_rl_view->set_value("Unrecognized keystroke, press "
                                        ANSI_BOLD("?")
                                        " to view help");
        flash();
        break;
    }
}

static void handle_rl_key(int ch)
{
    switch (ch) {
    case KEY_PPAGE:
    case KEY_NPAGE:
        handle_paging_key(ch);
        break;

    default:
        lnav_data.ld_rl_view->handle_key(ch);
        break;
    }
}

readline_context::command_map_t lnav_commands;

string execute_command(string cmdline)
{
    stringstream ss(cmdline);

    vector<string> args;
    string         buf, msg;

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

static void execute_file(string path)
{
    ifstream cmd_file(path.c_str());

    if (cmd_file.is_open()) {
        int    line_number = 0;
        string line;

        while (getline(cmd_file, line)) {
            line_number += 1;

            if (line.empty()) {
                continue;
            }
            if (line[0] == '#') {
                continue;
            }

            string rc = execute_command(line);

            fprintf(stderr,
                    "%s:%d:execute result -- %s\n",
                    path.c_str(),
                    line_number,
                    rc.c_str());
        }
    }
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

        if (value == NULL) {
            value = "<NULL>";
        }
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

void execute_search(lnav_view_t view, const std::string &regex)
{
    auto_ptr<grep_highlighter> &gc = lnav_data.ld_search_child[view];
    textview_curses &           tc = lnav_data.ld_views[view];

    if ((gc.get() == NULL) || (regex != lnav_data.ld_last_search[view])) {
        const char *errptr;
        pcre *      code;
        int         eoff;

        if (regex.empty() && gc.get() != NULL) {
            tc.grep_begin(*(gc->get_grep_proc()));
            tc.grep_end(*(gc->get_grep_proc()));
        }
        gc.reset();

        fprintf(stderr, "start search for: %s\n", regex.c_str());

        tc.match_reset();

        if (regex.empty()) {
            lnav_data.ld_bottom_source.grep_error("");
        }
        else if ((code = pcre_compile(regex.c_str(),
                                      PCRE_CASELESS,
                                      &errptr,
                                      &eoff,
                                      NULL)) == NULL) {
            lnav_data.ld_bottom_source.
            grep_error("regexp error: " + string(errptr));
        }
        else {
            textview_curses::highlighter
                hl(code, false, view_colors::VCR_SEARCH);

            lnav_data.ld_bottom_source.set_prompt("");

            textview_curses::highlight_map_t &hm = tc.get_highlights();
            hm["$search"] = hl;

            auto_ptr<grep_proc> gp(new grep_proc(code,
                                                 tc,
                                                 lnav_data.ld_max_fd,
                                                 lnav_data.ld_read_fds));

            gp->queue_request(grep_line_t(tc.get_top()));
            if (tc.get_top() > 0) {
                gp->queue_request(grep_line_t(0),
                                  grep_line_t(tc.get_top()));
            }
            gp->start();
            gp->set_sink(lnav_data.ld_view_stack.top());

            tc.set_follow_search(true);

            auto_ptr<grep_highlighter> gh(new grep_highlighter(gp, "$search",
                                                               hm));
            gc = gh;
        }
    }

    lnav_data.ld_last_search[view] = regex;
}

static void rl_search(void *dummy, readline_curses *rc)
{
    string name;

    switch (lnav_data.ld_mode) {
    case LNM_SEARCH:
        name = "$search";
        break;

    case LNM_CAPTURE:
        assert(0);
        name = "$capture";
        break;

    case LNM_COMMAND:
        return;

    case LNM_SQL:
        if (!sqlite3_complete(rc->get_value().c_str())) {
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

    default:
        assert(0);
        break;
    }

    textview_curses *tc    = lnav_data.ld_view_stack.top();
    lnav_view_t      index = (lnav_view_t)(tc - lnav_data.ld_views);

    tc->set_top(lnav_data.ld_search_start_line);
    execute_search(index, rc->get_value());
}

static void rl_callback(void *dummy, readline_curses *rc)
{
    lnav_data.ld_bottom_source.set_prompt("");

    switch (lnav_data.ld_mode) {
    case LNM_PAGING:
        assert(0);
        break;

    case LNM_COMMAND:
        lnav_data.ld_mode = LNM_PAGING;
        rc->set_value(execute_command(rc->get_value()));
        rc->set_alt_value("");
        break;

    case LNM_SEARCH:
    case LNM_CAPTURE:
        rl_search(dummy, rc);
        if (rc->get_value().size() > 0) {
            lnav_data.ld_view_stack.top()->set_follow_search(false);
            lnav_data.ld_rl_view->
            add_possibility(LNM_COMMAND, "filter", rc->get_value());
            rc->set_value("search: " + rc->get_value());
            rc->set_alt_value(HELP_MSG_2(
                                  n, N,
                                  "to move forward/backward through search results"));
        }
        lnav_data.ld_mode = LNM_PAGING;
        break;

    case LNM_SQL:
    {
        db_label_source &      dls = lnav_data.ld_db_rows;
        hist_source &          hs  = lnav_data.ld_db_source;
        auto_mem<sqlite3_stmt> stmt(sqlite3_finalize);
        int retcode;

        lnav_data.ld_bottom_source.grep_error("");
        hs.clear();
        dls.clear();
        retcode = sqlite3_prepare_v2(lnav_data.ld_db.in(),
                                     rc->get_value().c_str(),
                                     -1,
                                     stmt.out(),
                                     NULL);
        if (retcode != SQLITE_OK) {
            const char *errmsg = sqlite3_errmsg(lnav_data.ld_db);

            rc->set_value(errmsg);
            rc->set_alt_value("");
        }
        else if (stmt == NULL) {
            rc->set_value("");
            rc->set_alt_value("");
        }
        else {
            bool done = false;

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

                default:
                {
                    const char *errmsg;

                    fprintf(stderr, "code %d\n", retcode);
                    errmsg = sqlite3_errmsg(lnav_data.ld_db);
                    rc->set_value(errmsg);
                    done = true;
                }
                break;
                }
            }

            if (retcode == SQLITE_DONE) {
                hs.analyze();
                lnav_data.ld_views[LNV_LOG].reload_data();
                lnav_data.ld_views[LNV_DB].reload_data();
                lnav_data.ld_views[LNV_DB].set_left(0);

                if (dls.dls_rows.size() > 0) {
                    char row_count[32];

                    ensure_view(&lnav_data.ld_views[LNV_DB]);

                    snprintf(row_count, sizeof(row_count),
                             ANSI_BOLD("%'d") " row(s) matched",
                             (int)dls.dls_rows.size());
                    rc->set_value(row_count);
                    rc->set_alt_value(HELP_MSG_2(
                                          y, Y,
                                          "to move forward/backward through query results "
                                          "in the log view"));
                }
                else {
                    rc->set_value("No rows matched");
                    rc->set_alt_value("");
                }
            }

            lnav_data.ld_bottom_source.update_loading(0, 0);
            lnav_data.ld_status[LNS_BOTTOM].do_update();
        }

        field_overlay_source *fos;

        fos =
            (field_overlay_source *)lnav_data.ld_views[LNV_LOG].
            get_overlay_source();
        fos->fos_active = fos->fos_active_prev;
        lnav_data.ld_views[LNV_LOG].reload_data();
    }

        lnav_data.ld_mode = LNM_PAGING;
        break;
    }

    curs_set(0);
}

static void usage(void)
{
    const char *usage_msg =
        "usage: %s [-hVsar] [logfile1 logfile2 ...]\n"
        "\n"
        "A curses-based log file viewer that indexes log messages by type\n"
        "and time to make it easier to navigate through files quickly.\n"
        "\n"
        "Key bindings:\n"
        "  ?     View/leave the online help text.\n"
        "  q     Quit the program.\n"
        "\n"
        "Options:\n"
        "  -h         Print this message, then exit.\n"
        "  -d file    Write debug messages to the given file.\n"
        "  -V         Print version information.\n"
        "  -s         Load the most recent syslog messages file.\n"
        "  -a         Load all of the most recent log file types.\n"
        "  -r         Load older rotated log files as well.\n"
        "  -t         Prepend timestamps to the lines of data being read in\n"
        "             on the standard input.\n"
        "  -w file    Write the contents of the standard input to this file.\n"
        "\n"
        "Optional arguments:\n"
        "  logfile1          The log files or directories to view.  If a\n"
        "                    directory is given, all of the files in the\n"
        "                    directory will be loaded.\n"
        "\n"
        "Examples:\n"
        "  To load and follow the syslog file:\n"
        "    $ lnav -s\n"
        "\n"
        "  To load all of the files in /var/log:\n"
        "    $ lnav /var/log\n"
        "\n"
        "  To watch the output of make with timestamps prepended:\n"
        "    $ make 2>&1 | lnav -t\n"
        "\n"
        "Version: " PACKAGE_STRING "\n";

    fprintf(stderr, usage_msg, lnav_data.ld_program_name);
}

static pcre *xpcre_compile(const char *pattern, int options = 0)
{
    const char *errptr;
    pcre *      retval;
    int         eoff;

    if ((retval = pcre_compile(pattern,
                               options,
                               &errptr,
                               &eoff,
                               NULL)) == NULL) {
        fprintf(stderr, "internal error: failed to compile -- %s\n", pattern);
        fprintf(stderr, "internal error: %s\n", errptr);

        exit(1);
    }

    return retval;
}

/**
 * Callback used to keep track of the timestamps for the top and bottom lines
 * in the log view.  This function is intended to be used as the callback
 * function in a view_action.
 *
 * @param lv The listview object that contains the log
 */
static void update_times(void *, listview_curses *lv)
{
    if (lv == &lnav_data.ld_views[LNV_LOG] && lv->get_inner_height() > 0) {
        logfile_sub_source &lss = lnav_data.ld_log_source;

        lnav_data.ld_top_time =
            lss.find_line(lss.at(lv->get_top()))->get_time();
        lnav_data.ld_bottom_time =
            lss.find_line(lss.at(lv->get_bottom()))->get_time();
    }
    if (lv == &lnav_data.ld_views[LNV_HISTOGRAM] &&
        lv->get_inner_height() > 0) {
        hist_source &hs = lnav_data.ld_hist_source;

        lnav_data.ld_top_time    = hs.value_for_row(lv->get_top());
        lnav_data.ld_bottom_time = hs.value_for_row(lv->get_bottom());
    }
}

/**
 * Functor used to compare files based on their device and inode number.
 */
struct same_file {
    same_file(const struct stat &stat) : sf_stat(stat) { };

    /**
     * Compare the given log file against the 'stat' given in the constructor.
     * @param  lf The log file to compare.
     * @return    True if the dev/inode values in the stat given in the
     *   constructor matches the stat in the logfile object.
     */
    bool operator()(const logfile *lf) const
    {
        return this->sf_stat.st_dev == lf->get_stat().st_dev &&
               this->sf_stat.st_ino == lf->get_stat().st_ino;
    };

    const struct stat &sf_stat;
};

/**
 * Try to load the given file as a log file.  If the file has not already been
 * loaded, it will be loaded.  If the file has already been loaded, the file
 * name will be updated.
 *
 * @param filename The file name to check.
 * @param fd       An already-opened descriptor for 'filename'.
 * @param required Specifies whether or not the file must exist and be valid.
 */
static void watch_logfile(string filename, int fd, bool required)
{
    list<logfile *>::iterator file_iter;
    struct stat st;
    int         rc;

    if (fd != -1) {
        rc = fstat(fd, &st);
    }
    else {
        rc = stat(filename.c_str(), &st);
    }

    if (rc == 0) {
        if (!S_ISREG(st.st_mode)) {
            if (required) {
                rc    = -1;
                errno = EINVAL;
            }
            else {
                return;
            }
        }
    }
    if (rc == -1) {
        if (required) {
            throw logfile::error(filename, errno);
        }
        else{
            return;
        }
    }

    file_iter = find_if(lnav_data.ld_files.begin(),
                        lnav_data.ld_files.end(),
                        same_file(st));

    if (file_iter == lnav_data.ld_files.end()) {
        if (find(lnav_data.ld_other_files.begin(),
                 lnav_data.ld_other_files.end(),
                 filename) == lnav_data.ld_other_files.end()) {
            file_format_t ff = detect_file_format(filename);

            switch (ff) {
            case FF_SQLITE_DB:
                lnav_data.ld_other_files.push_back(filename);
                attach_sqlite_db(lnav_data.ld_db.in(), filename);
                break;

            default:
                /* It's a new file, load it in. */
                logfile *lf = new logfile(filename, fd);

                lnav_data.ld_files.push_back(lf);
                lnav_data.ld_text_source.tss_files.push_back(lf);
                break;
            }
        }
    }
    else {
        /* The file is already loaded, but has been found under a different
         * name.  We just need to update the stored file name.
         */
        (*file_iter)->set_filename(filename);
    }
}

/**
 * Expand a glob pattern and call watch_logfile with the file names that match
 * the pattern.
 * @param path     The glob pattern to expand.
 * @param required Passed to watch_logfile.
 */
static void expand_filename(string path, bool required)
{
    static_root_mem<glob_t, globfree> gl;

    if (glob(path.c_str(), GLOB_NOCHECK, NULL, gl.inout()) == 0) {
        int lpc;

        if (gl->gl_pathc == 1 /*&& gl.gl_matchc == 0*/) {
            /* It's a pattern that doesn't match any files
             * yet, allow it through since we'll load it in
             * dynamically.
             */
            required = false;
        }
        if (gl->gl_pathc > 1 ||
            strcmp(path.c_str(), gl->gl_pathv[0]) != 0) {
            required = false;
        }
        for (lpc = 0; lpc < (int)gl->gl_pathc; lpc++) {
            watch_logfile(gl->gl_pathv[lpc], -1, required);
        }
    }
}

static bool rescan_files(bool required = false)
{
    set<pair<string, int> >::iterator iter;
    list<logfile *>::iterator         file_iter;
    bool retval = false;

    for (iter = lnav_data.ld_file_names.begin();
         iter != lnav_data.ld_file_names.end();
         iter++) {
        if (iter->second == -1) {
            expand_filename(iter->first, required);
            if (lnav_data.ld_flags & LNF_ROTATED) {
                string path = iter->first + ".*";

                expand_filename(path, false);
            }
        }
        else {
            watch_logfile(iter->first, iter->second, required);
        }
    }

    for (file_iter = lnav_data.ld_files.begin();
         file_iter != lnav_data.ld_files.end(); ) {
        if (!(*file_iter)->exists()) {
            std::list<logfile *>::iterator tss_iter;

            fprintf(stderr, "file has been deleted -- %s\n",
                    (*file_iter)->get_filename().c_str());
            lnav_data.ld_log_source.remove_file(*file_iter);
            file_iter = lnav_data.ld_files.erase(file_iter);
            tss_iter  = find(lnav_data.ld_text_source.tss_files.begin(),
                             lnav_data.ld_text_source.tss_files.end(),
                             *file_iter);
            if (tss_iter != lnav_data.ld_text_source.tss_files.end()) {
                lnav_data.ld_text_source.tss_files.erase(tss_iter);
            }
            retval = true;
        }
        else {
            ++file_iter;
        }
    }

    return retval;
}

class lnav_behavior : public mouse_behavior {
public:
    enum lb_mode_t {
        LB_MODE_NONE,
        LB_MODE_DOWN,
        LB_MODE_UP,
        LB_MODE_DRAG
    };

    lnav_behavior()
        : lb_selection_start(-1),
          lb_selection_last(-1),
          lb_scrollbar_y(-1),
          lb_scroll_repeat(0),
          lb_mode(LB_MODE_NONE) {};

    int scroll_polarity(int button)
    {
        return button == xterm_mouse::XT_SCROLL_UP ? -1 : 1;
    };

    void mouse_event(int button, int x, int y)
    {
        logfile_sub_source *lss = NULL;
        textview_curses *   tc  = lnav_data.ld_view_stack.top();
        vis_line_t          vis_y(tc->get_top() + y - 2);
        struct timeval      now, diff;
        unsigned long       width;
        vis_line_t          height;

        tc->get_dimensions(height, width);

        gettimeofday(&now, NULL);
        timersub(&now, &this->lb_last_event_time, &diff);
        this->lb_last_event_time = now;

        lss = dynamic_cast<logfile_sub_source *>(tc->get_sub_source());
        switch (button) {
        case xterm_mouse::XT_BUTTON1:
            if (this->lb_selection_start == vis_line_t(-1) &&
                tc->get_inner_height() &&
                ((this->lb_scrollbar_y != -1) || (x >= (int)(width - 2)))) {
                double top_pct, bot_pct, pct;
                int    scroll_top, scroll_bottom, shift_amount = 0, new_top =
                    0;

                top_pct = (double)tc->get_top() /
                          (double)tc->get_inner_height();
                bot_pct = (double)tc->get_bottom() /
                          (double)tc->get_inner_height();
                scroll_top =
                    (tc->get_y() + (int)(top_pct * (double)height));
                scroll_bottom =
                    (tc->get_y() + (int)(bot_pct * (double)height));
                if (this->lb_mode == LB_MODE_NONE) {
                    if (scroll_top <= y && y <= scroll_bottom) {
                        this->lb_mode        = LB_MODE_DRAG;
                        this->lb_scrollbar_y = y - scroll_top;
                    }
                    else if (y < scroll_top) {
                        this->lb_mode = LB_MODE_UP;
                    }
                    else {
                        this->lb_mode = LB_MODE_DOWN;
                    }
                }
                switch (this->lb_mode) {
                case LB_MODE_NONE:
                    assert(0);
                    break;

                case LB_MODE_UP:
                    if (y < scroll_top) {
                        shift_amount = -1 * height;
                    }
                    break;

                case LB_MODE_DOWN:
                    if (y > scroll_bottom) {
                        shift_amount = height;
                    }
                    break;

                case LB_MODE_DRAG:
                    pct     = (double)tc->get_inner_height() / (double)height;
                    new_top = y - tc->get_y() - this->lb_scrollbar_y;
                    new_top = (int)floor(((double)new_top * pct) + 0.5);
                    tc->set_top(vis_line_t(new_top));
                    break;
                }
                if (shift_amount != 0) {
                    tc->shift_top(vis_line_t(shift_amount));
                }
                return;
            }
            if (this->lb_selection_start == vis_line_t(-1)) {
                this->lb_selection_start = vis_y;
                this->lb_selection_last  = vis_line_t(-1);
            }
            else {
                if (this->lb_selection_last != vis_line_t(-1)) {
                    tc->toggle_user_mark(&textview_curses::BM_USER,
                                         this->lb_selection_start,
                                         this->lb_selection_last);
                }
                if (this->lb_selection_start == vis_y) {
                    this->lb_selection_last = vis_line_t(-1);
                }
                else {
                    tc->toggle_user_mark(&textview_curses::BM_USER,
                                         this->lb_selection_start,
                                         vis_y);
                    this->lb_selection_last = vis_y;
                }
            }
            tc->reload_data();
            break;

        case xterm_mouse::XT_BUTTON_RELEASE:
            this->lb_mode            = LB_MODE_NONE;
            this->lb_scrollbar_y     = -1;
            this->lb_selection_start = vis_line_t(-1);
            break;

        case xterm_mouse::XT_SCROLL_UP:
        case xterm_mouse::XT_SCROLL_DOWN:
            if (this->lb_scroll_repeat ||
                (diff.tv_sec == 0 && diff.tv_usec < 30000)) {
                if (this->lb_scroll_repeat) {
                    struct timeval scroll_diff;

                    timersub(&now, &this->lb_last_scroll_time, &scroll_diff);
                    if (scroll_diff.tv_usec > 50000) {
                        tc->shift_top(vis_line_t(this->scroll_polarity(button)
                                                 *
                                                 this->lb_scroll_repeat),
                                      true);
                        this->lb_scroll_repeat = 0;
                    }
                    else {
                        this->lb_scroll_repeat += 1;
                    }
                }
                else {
                    this->lb_scroll_repeat    = 1;
                    this->lb_last_scroll_time = now;
                    tc->shift_top(vis_line_t(this->scroll_polarity(
                                                 button)), true);
                }
            }
            else {
                tc->shift_top(vis_line_t(this->scroll_polarity(button)), true);
            }
            break;
        }
    };

private:
    struct timeval lb_last_event_time;
    vis_line_t     lb_selection_start;
    vis_line_t     lb_selection_last;

    int lb_scrollbar_y;

    struct timeval lb_last_scroll_time;
    int            lb_scroll_repeat;

    lb_mode_t lb_mode;
};

static void looper(void)
{
    int fd;

    fd =
        open(lnav_data.ld_debug_log_name, O_WRONLY | O_CREAT | O_APPEND, 0666);
    dup2(fd, STDERR_FILENO);
    close(fd);
    fprintf(stderr, "startup\n");

    try {
        readline_context command_context("cmd", &lnav_commands);

        readline_context search_context("search");
        readline_context index_context("capture");
        readline_context sql_context("sql", NULL, false);
        textview_curses *tc;
        readline_curses  rlc;
        int lpc;

        listview_curses::action::broadcaster &sb =
            lnav_data.ld_scroll_broadcaster;

        rlc.add_context(LNM_COMMAND, command_context);
        rlc.add_context(LNM_SEARCH, search_context);
        rlc.add_context(LNM_CAPTURE, index_context);
        rlc.add_context(LNM_SQL, sql_context);
        rlc.start();

        lnav_data.ld_rl_view = &rlc;

        lnav_data.ld_rl_view->
        add_possibility(LNM_COMMAND, "graph", "\\d+(?:\\.\\d+)?");
        lnav_data.ld_rl_view->
        add_possibility(LNM_COMMAND, "graph", "([:= \\t]\\d+(?:\\.\\d+)?)");

        (void)signal(SIGINT, sigint);
        (void)signal(SIGTERM, sigint);
        (void)signal(SIGWINCH, sigwinch);

        screen_curses sc;
        xterm_mouse   mouse;
        lnav_behavior lb;

        mouse.set_enabled(check_experimental("mouse"));
        mouse.set_behavior(&lb);

        lnav_data.ld_window = sc.get_window();
        keypad(stdscr, TRUE);
        (void)nonl();
        (void)cbreak();
        (void)noecho();
        (void)nodelay(lnav_data.ld_window, 1);

        define_key("\033Od", KEY_BEG);
        define_key("\033Oc", KEY_END);

        view_colors::singleton().init();

        rlc.set_window(lnav_data.ld_window);
        rlc.set_y(-1);
        rlc.set_perform_action(readline_curses::action(rl_callback));
        rlc.set_timeout_action(readline_curses::action(rl_search));
        rlc.set_alt_value(HELP_MSG_2(
                              e, E,
                              "to move forward/backward through error messages"));

        (void)curs_set(0);

        lnav_data.ld_view_stack.push(&lnav_data.ld_views[LNV_LOG]);
        update_view_name();

        tc = lnav_data.ld_view_stack.top();

        for (lpc = 0; lpc < LNV__MAX; lpc++) {
            lnav_data.ld_views[lpc].set_window(lnav_data.ld_window);
            lnav_data.ld_views[lpc].set_y(1);
            lnav_data.ld_views[lpc].
            set_height(vis_line_t(-(rlc.get_height() + 1 + 1)));
            lnav_data.ld_views[lpc].
            set_scroll_action(sb.get_functor());
            lnav_data.ld_views[lpc].
            set_search_action(&lnav_data.ld_bottom_source.hits_wire);
        }

        lnav_data.ld_status[LNS_TOP].set_top(0);
        for (lpc = 0; lpc < LNS__MAX; lpc++) {
            lnav_data.ld_status[lpc].set_window(lnav_data.ld_window);
        }
        lnav_data.ld_status[LNS_TOP].
        set_data_source(&lnav_data.ld_top_source);
        lnav_data.ld_status[LNS_BOTTOM].
        set_data_source(&lnav_data.ld_bottom_source);
        sb.push_back(view_action<listview_curses>(update_times));
        sb.push_back(&lnav_data.ld_top_source.filename_wire);
        sb.push_back(&lnav_data.ld_bottom_source.line_number_wire);
        sb.push_back(&lnav_data.ld_bottom_source.percent_wire);
        sb.push_back(&lnav_data.ld_bottom_source.marks_wire);

        {
            vis_line_t top(0), height(0);

            unsigned long width;

            tc->get_dimensions(height, width);
            top = vis_line_t(tc->get_inner_height()) - height + vis_line_t(1);
            if (top > 0) {
                tc->set_top(top);
            }
        }

        {
            hist_source &hs = lnav_data.ld_hist_source;

            lnav_data.ld_hist_zoom = 2;
            hs.set_role_for_type(bucket_type_t(logline::LEVEL_FATAL),
                                 view_colors::VCR_ERROR);
            hs.set_role_for_type(bucket_type_t(logline::LEVEL_CRITICAL),
                                 view_colors::VCR_ERROR);
            hs.set_role_for_type(bucket_type_t(logline::LEVEL_ERROR),
                                 view_colors::VCR_ERROR);
            hs.set_role_for_type(bucket_type_t(logline::LEVEL_WARNING),
                                 view_colors::VCR_WARNING);
            hs.set_label_source(new time_label_source());
        }

        {
            hist_source &hs = lnav_data.ld_graph_source;

            hs.set_bucket_size(1);
            hs.set_group_size(100);
        }

        {
            hist_source &hs = lnav_data.ld_db_source;

            hs.set_bucket_size(1);
            hs.set_group_size(10);
            hs.set_label_source(&lnav_data.ld_db_rows);
        }

        FD_ZERO(&lnav_data.ld_read_fds);
        FD_SET(STDIN_FILENO, &lnav_data.ld_read_fds);
        lnav_data.ld_max_fd =
            max(STDIN_FILENO, rlc.update_fd_set(lnav_data.ld_read_fds));

        lnav_data.ld_status[0].window_change();
        lnav_data.ld_status[1].window_change();

        execute_file(dotlnav_path("session"));

        lnav_data.ld_scroll_broadcaster.invoke(lnav_data.ld_view_stack.top());

        bool session_loaded = false;

        while (lnav_data.ld_looping) {
            fd_set         ready_rfds = lnav_data.ld_read_fds;
            struct timeval to         = { 0, 330000 };
            int            rc;

            lnav_data.ld_top_source.update_time();

            if (rescan_files()) {
                rebuild_indexes(true);
            }

            for (lpc = 0; lpc < LNV__MAX; lpc++) {
                lnav_data.ld_views[lpc]
                .set_height(vis_line_t(-(rlc.get_height() + 1)));
            }
            lnav_data.ld_status[LNS_BOTTOM].set_top(-(rlc.get_height() + 1));

            lnav_data.ld_view_stack.top()->do_update();
            lnav_data.ld_status[LNS_TOP].do_update();
            lnav_data.ld_status[LNS_BOTTOM].do_update();
            rlc.do_update();
            refresh();

            rc = select(lnav_data.ld_max_fd + 1,
                        &ready_rfds, NULL, NULL,
                        &to);

            if (rc < 0) {
                switch (errno) {
                case EINTR:
                    break;

                default:
                    fprintf(stderr, "select %s\n", strerror(errno));
                    lnav_data.ld_looping = false;
                    break;
                }
            }
            else if (rc == 0) {
                static bool initial_build = false;

                rebuild_indexes(false);
                if (!initial_build &&
                    lnav_data.ld_log_source.text_line_count() == 0 &&
                    lnav_data.ld_text_source.text_line_count() > 0) {
                    toggle_view(&lnav_data.ld_views[LNV_TEXT]);
                    lnav_data.ld_views[LNV_TEXT].set_top(vis_line_t(0));
                    lnav_data.ld_rl_view->set_alt_value(
                        HELP_MSG_2(f, F,
                                   "to switch to the next/previous file"));
                }
                initial_build = true;

                if (!session_loaded) {
                    load_session();
                    if (!lnav_data.ld_session_file_names.empty()) {
                        std::string ago;

                        ago = time_ago(lnav_data.ld_session_save_time);
                        lnav_data.ld_rl_view->set_value(
                            ("restored session from " ANSI_BOLD_START) +
                            ago +
                            (ANSI_NORM "; press Ctrl-R to reset session"));
                    }
                    rebuild_indexes(true);
                    session_loaded = true;
                }
            }
            else {
                if (FD_ISSET(STDIN_FILENO, &ready_rfds)) {
                    int ch;

                    while ((ch = getch()) != ERR) {
                        switch (ch) {
                        case CEOF:
                        case KEY_RESIZE:
                            break;

                        case KEY_MOUSE:
                            mouse.handle_mouse(ch);
                            break;

                        default:
                            switch (lnav_data.ld_mode) {
                            case LNM_PAGING:
                                handle_paging_key(ch);
                                break;

                            case LNM_COMMAND:
                            case LNM_SEARCH:
                            case LNM_CAPTURE:
                            case LNM_SQL:
                                handle_rl_key(ch);
                                break;

                            default:
                                assert(0);
                                break;
                            }
                            break;
                        }
                    }
                }
                for (lpc = 0; lpc < LG__MAX; lpc++) {
                    auto_ptr<grep_highlighter> &gc =
                        lnav_data.ld_grep_child[lpc];

                    if (gc.get() != NULL) {
                        gc->get_grep_proc()->check_fd_set(ready_rfds);
                        if (lpc == LG_GRAPH) {
                            lnav_data.ld_views[LNV_GRAPH].reload_data();
                            /* XXX */
                        }
                    }
                }
                for (lpc = 0; lpc < LNV__MAX; lpc++) {
                    auto_ptr<grep_highlighter> &gc =
                        lnav_data.ld_search_child[lpc];

                    if (gc.get() != NULL) {
                        gc->get_grep_proc()->check_fd_set(ready_rfds);

                        if (!lnav_data.ld_view_stack.empty()) {
                            lnav_data.ld_bottom_source.
                            update_hits(lnav_data.ld_view_stack.top());
                        }
                    }
                }
                rlc.check_fd_set(ready_rfds);
            }

            if (lnav_data.ld_winched) {
                struct winsize size;

                fprintf(stderr, "WINCHED\n");

                if (ioctl(fileno(stdout), TIOCGWINSZ, &size) == 0) {
                    resizeterm(size.ws_row, size.ws_col);
                }
                rlc.window_change();
                lnav_data.ld_status[0].window_change();
                lnav_data.ld_status[1].window_change();
                lnav_data.ld_view_stack.top()->set_needs_update();
                lnav_data.ld_winched = false;
            }
        }
    }
    catch (readline_curses::error & e) {
        fprintf(stderr, "error: %s\n", strerror(e.e_err));
    }
}

class access_log_table : public log_vtab_impl {
public:

    access_log_table()
        : log_vtab_impl("access_log") {};

    void get_columns(vector<vtab_column> &cols)
    {
        cols.push_back(vtab_column("c_ip", SQLITE3_TEXT, "ipaddress"));
        cols.push_back(vtab_column("cs_username", SQLITE3_TEXT));
        cols.push_back(vtab_column("cs_method", SQLITE3_TEXT));
        cols.push_back(vtab_column("cs_uri_stem", SQLITE3_TEXT));
        cols.push_back(vtab_column("cs_uri_query", SQLITE3_TEXT));
        cols.push_back(vtab_column("cs_version", SQLITE3_TEXT));
        cols.push_back(vtab_column("sc_status", SQLITE_INTEGER));
        cols.push_back(vtab_column("sc_bytes", SQLITE_INTEGER));
        cols.push_back(vtab_column("cs_referer", SQLITE3_TEXT));
        cols.push_back(vtab_column("cs_user_agent", SQLITE3_TEXT));
    };

    void get_foreign_keys(vector<string> &keys_inout)
    {
        this->log_vtab_impl::get_foreign_keys(keys_inout);

        keys_inout.push_back("sc_status");
    };
};

class syslog_log_table : public log_vtab_impl {
public:

    syslog_log_table()
        : log_vtab_impl("syslog_log") {};

    void get_columns(vector<vtab_column> &cols)
    {
        cols.push_back(vtab_column("log_hostname", SQLITE3_TEXT));
        cols.push_back(vtab_column("log_pid", SQLITE_INTEGER));
    };

    void get_foreign_keys(vector<string> &keys_inout)
    {
        this->log_vtab_impl::get_foreign_keys(keys_inout);

        keys_inout.push_back("log_pid");
    };
};

class glog_log_table : public log_vtab_impl {
public:

    glog_log_table()
        : log_vtab_impl("glog_log") {};

    void get_columns(vector<vtab_column> &cols)
    {
        cols.push_back(vtab_column("micros", SQLITE_INTEGER));
        cols.push_back(vtab_column("thread", SQLITE3_TEXT));
        cols.push_back(vtab_column("src_file", SQLITE3_TEXT));
        cols.push_back(vtab_column("src_line", SQLITE_TEXT));
        cols.push_back(vtab_column("message", SQLITE3_TEXT));
    };
};

class strace_log_table : public log_vtab_impl {
public:

    strace_log_table()
        : log_vtab_impl("strace_log") {};

    void get_columns(vector<vtab_column> &cols)
    {
        cols.push_back(vtab_column("funcname", SQLITE_TEXT));
        cols.push_back(vtab_column("args", SQLITE_TEXT));
        cols.push_back(vtab_column("result", SQLITE_TEXT));
        cols.push_back(vtab_column("duration", SQLITE_TEXT));
#if 0
        cols.push_back(vtab_column("arg0", SQLITE_TEXT));
        cols.push_back(vtab_column("arg1", SQLITE_TEXT));
        cols.push_back(vtab_column("arg2", SQLITE_TEXT));
        cols.push_back(vtab_column("arg3", SQLITE_TEXT));
        cols.push_back(vtab_column("arg4", SQLITE_TEXT));
        cols.push_back(vtab_column("arg5", SQLITE_TEXT));
        cols.push_back(vtab_column("arg6", SQLITE_TEXT));
        cols.push_back(vtab_column("arg7", SQLITE_TEXT));
        cols.push_back(vtab_column("arg8", SQLITE_TEXT));
        cols.push_back(vtab_column("arg9", SQLITE_TEXT));
#endif
    };

#if 0
    void extract(logfile *lf,
                 const std::string &line,
                 int column,
                 sqlite3_context *ctx)
    {
        string function, args, result, duration = "0";

        if (!this->slt_regex.FullMatch(line,
                                       &function,
                                       &args,
                                       &result,
                                       &duration)) {
            fprintf(stderr, "bad match! %s\n", line.c_str());
        }
        switch (column) {
        case 0:
            sqlite3_result_text(ctx,
                                function.c_str(),
                                function.length(),
                                SQLITE_TRANSIENT);
            break;

        case 1:
            sqlite3_result_text(ctx,
                                result.c_str(),
                                result.length(),
                                SQLITE_TRANSIENT);
            break;

        case 2:
            sqlite3_result_text(ctx,
                                duration.c_str(),
                                duration.length(),
                                SQLITE_TRANSIENT);
            break;

        default:
        {
            const char *arg_start = args.c_str();
            int         in_struct = 0, in_list = 0;
            int         lpc, argnum, curarg = 0;
            bool        in_quote = false;

            argnum = column - 3;
            for (lpc = 0; lpc < (int)args.length(); lpc++) {
                switch (args[lpc]) {
                case '{':
                    if (!in_quote) {
                        in_struct += 1;
                    }
                    break;

                case '}':
                    if (!in_quote) {
                        in_struct -= 1;
                    }
                    break;

                case '[':
                    if (!in_quote) {
                        in_list += 1;
                    }
                    break;

                case ']':
                    if (!in_quote) {
                        in_list -= 1;
                    }
                    break;

                case '"':
                    if (!in_quote) {
                        in_quote = true;
                    }
                    else if (lpc > 0 && args[lpc - 1] != '\\') {
                        in_quote = false;
                    }
                    break;

                case ',':
                    if (!in_quote && !in_struct && !in_list) {
                        if (curarg == argnum) {
                            sqlite3_result_text(ctx,
                                                arg_start,
                                                &(args.c_str()[lpc]) -
                                                arg_start,
                                                SQLITE_TRANSIENT);
                            return;
                        }

                        curarg   += 1;
                        arg_start = &(args.c_str()[lpc + 1]);
                    }
                    break;
                }
            }
            if (curarg == argnum) {
                sqlite3_result_text(ctx,
                                    arg_start,
                                    &(args.c_str()[lpc]) -
                                    arg_start,
                                    SQLITE_TRANSIENT);
            }
            else {
                sqlite3_result_text(ctx,
                                    "",
                                    0,
                                    SQLITE_TRANSIENT);
            }
        }
        break;
        }
    };

private:
    pcrecpp::RE slt_regex;
#endif
};

static void setup_highlights(textview_curses::highlight_map_t &hm)
{
    hm["$sql"] = textview_curses::
                 highlighter(xpcre_compile(
                                 "(?: alter | select | insert | update "
                                 "| create "
                                 "| from | where | order by "
                                 "| group by )", PCRE_CASELESS));
    hm["$srcfile"] = textview_curses::
                     highlighter(xpcre_compile(
                                     "[\\w\\-_]+\\."
                                     "(?:java|a|o|so|c|cc|cpp|cxx|h|hh|hpp|hxx|py|pyc|rb):"
                                     "\\d+"));
    hm["$xml"] = textview_curses::
                 highlighter(xpcre_compile("<(/?[^ >]+)[^>]*>"));
    hm["$stringd"] = textview_curses::
                     highlighter(xpcre_compile("\"(?:\\\\.|[^\"])*\""));
    hm["$strings"] = textview_curses::
                     highlighter(xpcre_compile(
                                     "(?<![A-Za-qstv-z])\'(?:\\\\.|[^'])*\'"));
    hm["$diffp"] = textview_curses::
                   highlighter(xpcre_compile(
                                   "^\\+.*"), false,
                               view_colors::VCR_DIFF_ADD);
    hm["$diffm"] = textview_curses::
                   highlighter(xpcre_compile(
                                   "^\\-.*"), false,
                               view_colors::VCR_DIFF_DELETE);
    hm["$diffs"] = textview_curses::
                   highlighter(xpcre_compile(
                                   "^\\@@ .*"), false,
                               view_colors::VCR_DIFF_SECTION);
    hm["$ip"] = textview_curses::
                highlighter(xpcre_compile("\\d+\\.\\d+\\.\\d+\\.\\d+"));
    hm["$cdef"] = textview_curses::
                  highlighter(xpcre_compile(
                                  "^#\\s*(?:if|ifndef|ifdef|else|define|undef|endif)\\b"));
}

int sql_progress(const struct log_cursor &lc)
{
    size_t total = lnav_data.ld_log_source.text_line_count();
    off_t  off   = lc.lc_curr_line;

    if (lnav_data.ld_window == NULL) {
        return 0;
    }

    if (!lnav_data.ld_looping) {
        return 1;
    }

    lnav_data.ld_bottom_source.update_loading(off, total);
    lnav_data.ld_top_source.update_time();
    lnav_data.ld_status[LNS_TOP].do_update();
    lnav_data.ld_status[LNS_BOTTOM].do_update();
    refresh();

    return 0;
}

int main(int argc, char *argv[])
{
    int lpc, c, retval = EXIT_SUCCESS;

    auto_ptr<piper_proc> stdin_reader;
    const char *         stdin_out = NULL;

    setlocale(LC_NUMERIC, "");

    /* If we statically linked against an ncurses library that had a non-
     * standard path to the terminfo database, we need to set this variable
     * so that it will try the default path.
     */
    setenv("TERMINFO_DIRS", "/usr/share/terminfo", 0);

    ensure_dotlnav();

    if (sqlite3_open(":memory:", lnav_data.ld_db.out()) != SQLITE_OK) {
        fprintf(stderr, "error: unable to create sqlite memory database\n");
        exit(EXIT_FAILURE);
    }

    {
        int register_collation_functions(sqlite3 * db);

        register_sqlite_funcs(lnav_data.ld_db.in(), sqlite_registration_funcs);
        register_collation_functions(lnav_data.ld_db.in());
    }

    lnav_data.ld_program_name = argv[0];

    lnav_data.ld_vtab_manager =
        new log_vtab_manager(lnav_data.ld_db,
                             lnav_data.ld_log_source,
                             sql_progress);

    {
        auto_mem<char, sqlite3_free> errmsg;

        if (sqlite3_exec(lnav_data.ld_db.in(),
                         init_sql,
                         NULL,
                         NULL,
                         errmsg.out()) != SQLITE_OK) {
            fprintf(stderr,
                    "error: unable to execute DB init -- %s\n",
                    errmsg.in());
        }
    }

    lnav_data.ld_vtab_manager->register_vtab(new syslog_log_table());
    lnav_data.ld_vtab_manager->register_vtab(new log_vtab_impl("generic_log"));
    lnav_data.ld_vtab_manager->register_vtab(new access_log_table());
    lnav_data.ld_vtab_manager->register_vtab(new glog_log_table());
    lnav_data.ld_vtab_manager->register_vtab(new strace_log_table());

    DEFAULT_FILES.insert(make_pair(LNF_SYSLOG, string("var/log/messages")));
    DEFAULT_FILES.insert(make_pair(LNF_SYSLOG, string("var/log/system.log")));
    DEFAULT_FILES.insert(make_pair(LNF_SYSLOG, string("var/log/syslog")));
    DEFAULT_FILES.insert(make_pair(LNF_SYSLOG, string("var/log/syslog.log")));

    init_lnav_commands(lnav_commands);

    lnav_data.ld_views[LNV_HELP].
    set_sub_source(new plain_text_source(help_txt));
    lnav_data.ld_views[LNV_LOG].
    set_sub_source(&lnav_data.ld_log_source);
    lnav_data.ld_views[LNV_TEXT].
    set_sub_source(&lnav_data.ld_text_source);
    lnav_data.ld_views[LNV_HISTOGRAM].
    set_sub_source(&lnav_data.ld_hist_source);
    lnav_data.ld_views[LNV_GRAPH].
    set_sub_source(&lnav_data.ld_graph_source);
    lnav_data.ld_views[LNV_DB].
    set_sub_source(&lnav_data.ld_db_source);
    lnav_data.ld_db_overlay.dos_labels = &lnav_data.ld_db_rows;
    lnav_data.ld_views[LNV_DB].
    set_overlay_source(&lnav_data.ld_db_overlay);
    lnav_data.ld_views[LNV_LOG].
    set_overlay_source(new field_overlay_source());
    lnav_data.ld_db_overlay.dos_hist_source = &lnav_data.ld_db_source;

    {
        setup_highlights(lnav_data.ld_views[LNV_LOG].get_highlights());
        setup_highlights(lnav_data.ld_views[LNV_TEXT].get_highlights());
    }

    lnav_data.ld_looping        = true;
    lnav_data.ld_mode           = LNM_PAGING;
    lnav_data.ld_debug_log_name = "/dev/null";
    while ((c = getopt(argc, argv, "harsd:tw:V")) != -1) {
        switch (c) {
        case 'h':
            usage();
            exit(retval);
            break;

        case 'd':
            lnav_data.ld_debug_log_name = optarg;
            break;

        case 'a':
            lnav_data.ld_flags |= LNF__ALL;
            break;

        case 'r':
            lnav_data.ld_flags |= LNF_ROTATED;
            break;

        case 's':
            lnav_data.ld_flags |= LNF_SYSLOG;
            break;

        case 't':
            lnav_data.ld_flags |= LNF_TIMESTAMP;
            break;

        case 'w':
            stdin_out = optarg;
            break;

        case 'V':
            printf("%s\n", PACKAGE_STRING);
            exit(0);
            break;

        default:
            retval = EXIT_FAILURE;
            break;
        }
    }

    argc -= optind;
    argv += optind;

    if (isatty(STDIN_FILENO) && argc == 0 &&
        !(lnav_data.ld_flags & LNF__ALL)) {
        lnav_data.ld_flags |= LNF_SYSLOG;
    }

    if (lnav_data.ld_flags != 0) {
        char start_dir[FILENAME_MAX];

        if (getcwd(start_dir, sizeof(start_dir)) == NULL) {
            perror("getcwd");
        }
        else {
            do {
                for (lpc = 0; lpc < LNB__MAX; lpc++) {
                    if (!append_default_files((lnav_flags_t)(1L << lpc))) {
                        retval = EXIT_FAILURE;
                    }
                }
            } while (lnav_data.ld_file_names.empty() &&
                     change_to_parent_dir());

            if (chdir(start_dir) == -1) {
                perror("chdir(start_dir)");
            }
        }
    }

    for (lpc = 0; lpc < argc; lpc++) {
        auto_mem<char> abspath;
        struct stat    st;

        if (stat(argv[lpc], &st) == -1) {
            fprintf(stderr,
                    "Cannot stat file: %s -- %s\n",
                    argv[lpc],
                    strerror(errno));
            retval = EXIT_FAILURE;
        }
        else if ((abspath = realpath(argv[lpc], NULL)) == NULL) {
            perror("Cannot find file");
            retval = EXIT_FAILURE;
        }
        else if (S_ISDIR(st.st_mode)) {
            string dir_wild(abspath.in());

            if (dir_wild[dir_wild.size() - 1] == '/') {
                dir_wild.resize(dir_wild.size() - 1);
            }
            lnav_data.ld_file_names.insert(make_pair(dir_wild + "/*", -1));
        }
        else {
            lnav_data.ld_file_names.insert(make_pair(abspath.in(), -1));
        }
    }

    if (!isatty(STDOUT_FILENO)) {
        fprintf(stderr, "error: stdout is not a tty.\n");
        retval = EXIT_FAILURE;
    }

    if (!isatty(STDIN_FILENO)) {
        stdin_reader =
            auto_ptr<piper_proc>(new piper_proc(STDIN_FILENO,
                                                lnav_data.ld_flags &
                                                LNF_TIMESTAMP, stdin_out));
        lnav_data.ld_file_names.insert(make_pair("stdin",
                                                 stdin_reader->get_fd()));
        if (dup2(STDOUT_FILENO, STDIN_FILENO) == -1) {
            perror("cannot dup stdout to stdin");
        }
    }

    if (lnav_data.ld_file_names.empty()) {
        fprintf(stderr, "error: no log files given/found.\n");
        retval = EXIT_FAILURE;
    }

    if (retval != EXIT_SUCCESS) {
        usage();
    }
    else {
        try {
            rescan_files(true);

            init_session();

            scan_sessions();

            guard_termios gt(STDIN_FILENO);
            looper();

            save_session();
        }
        catch (line_buffer::error & e) {
            fprintf(stderr, "error: %s\n", strerror(e.e_err));
        }
        catch (logfile::error & e) {
            if (e.e_err != EINTR) {
                fprintf(stderr,
                        "error: %s -- '%s'\n",
                        strerror(e.e_err),
                        e.e_filename.c_str());
            }
        }
    }

    return retval;
}
