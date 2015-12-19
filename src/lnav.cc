/**
 * Copyright (c) 2007-2015, Timothy Stack
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
#include <libgen.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/wait.h>

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

#ifdef HAVE_BZLIB_H
#include <bzlib.h>
#endif

#include "lnav.hh"
#include "help.hh"
#include "init-sql.hh"
#include "logfile.hh"
#include "lnav_log.hh"
#include "log_accel.hh"
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
#include "log_format_loader.hh"
#include "session_data.hh"
#include "lnav_config.hh"
#include "sql_util.hh"
#include "sqlite-extension-func.h"
#include "sysclip.hh"
#include "term_extra.hh"
#include "log_data_helper.hh"
#include "readline_highlighters.hh"
#include "environ_vtab.hh"
#include "views_vtab.hh"
#include "pretty_printer.hh"
#include "all_logs_vtab.hh"

#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#endif

#include "papertrail_proc.hh"
#include "yajlpp.hh"
#include "readline_callbacks.hh"
#include "command_executor.hh"
#include "plain_text_source.hh"
#include "hotkeys.hh"
#include "readline_possibilities.hh"
#include "field_overlay_source.hh"
#include "url_loader.hh"
#include "log_search_table.hh"

using namespace std;

static multimap<lnav_flags_t, string> DEFAULT_FILES;

struct _lnav_data lnav_data;

struct hist_level {
    int hl_time_slice;
};

static struct hist_level HIST_ZOOM_VALUES[] = {
        { 24 * 60 * 60, },
        {  4 * 60 * 60, },
        {      60 * 60, },
        {      10 * 60, },
        {           60, },
};

const int HIST_ZOOM_LEVELS = sizeof(HIST_ZOOM_VALUES) / sizeof(struct hist_level);

bookmark_type_t BM_QUERY("query");

const char *lnav_view_strings[LNV__MAX + 1] = {
    "log",
    "text",
    "help",
    "histogram",
    "graph",
    "db",
    "example",
    "schema",
    "pretty",

    NULL
};

const char *lnav_zoom_strings[] = {
        "day",
        "4-hour",
        "hour",
        "10-minute",
        "minute",

        NULL
};

static const char *view_titles[LNV__MAX] = {
    "LOG",
    "TEXT",
    "HELP",
    "HIST",
    "GRAPH",
    "DB",
    "EXAMPLE",
    "SCHEMA",
    "PRETTY",
};

class log_gutter_source : public list_gutter_source {
public:
    void listview_gutter_value_for_range(
        const listview_curses &lv, int start, int end, chtype &ch, int &fg_out) {
        textview_curses *tc = (textview_curses *)&lv;
        vis_bookmarks &bm = tc->get_bookmarks();
        vis_line_t next;
        bool search_hit = false;

        start -= 1;

        next = bm[&textview_curses::BM_SEARCH].next(vis_line_t(start));
        search_hit = (next != -1 && next <= end);

        next = bm[&BM_QUERY].next(vis_line_t(start));
        search_hit = search_hit || (next != -1 && next <= end);

        next = bm[&textview_curses::BM_USER].next(vis_line_t(start));
        if (next == -1) {
            next = bm[&textview_curses::BM_PARTITION].next(vis_line_t(start));
        }
        if (next != -1 && next <= end) {
            ch = search_hit ? ACS_PLUS : ACS_LTEE;
        }
        else {
            ch = search_hit ? ACS_RTEE : ACS_VLINE;
        }
        next = bm[&logfile_sub_source::BM_ERRORS].next(vis_line_t(start));
        if (next != -1 && next <= end) {
            fg_out = COLOR_RED;
        }
        else {
            next = bm[&logfile_sub_source::BM_WARNINGS].next(vis_line_t(start));
            if (next != -1 && next <= end) {
                fg_out = COLOR_YELLOW;
            }
        }
    };
};

bool setup_logline_table()
{
    // Hidden columns don't show up in the table_info pragma.
    static const char *hidden_table_columns[] = {
        "log_path",
        "log_text",

        NULL
    };

    static const char *commands[] = {
        ".schema",
        ".msgformats",

        NULL
    };

    textview_curses &log_view = lnav_data.ld_views[LNV_LOG];
    bool             retval   = false;

    if (lnav_data.ld_rl_view != NULL) {
        lnav_data.ld_rl_view->clear_possibilities(LNM_SQL, "*");
        add_view_text_possibilities(LNM_SQL, "*", &log_view);
    }

    if (log_view.get_inner_height()) {
        static intern_string_t logline = intern_string::lookup("logline");
        vis_line_t     vl = log_view.get_top();
        content_line_t cl = lnav_data.ld_log_source.at_base(vl);

        lnav_data.ld_vtab_manager->unregister_vtab(logline);
        lnav_data.ld_vtab_manager->register_vtab(new log_data_table(cl, logline));

        if (lnav_data.ld_rl_view != NULL) {
            log_data_helper ldh(lnav_data.ld_log_source);

            ldh.parse_line(cl);

            std::map<const intern_string_t, json_ptr_walk::walk_list_t>::const_iterator pair_iter;
            for (pair_iter = ldh.ldh_json_pairs.begin();
                 pair_iter != ldh.ldh_json_pairs.end();
                 ++pair_iter) {
                for (size_t lpc = 0; lpc < pair_iter->second.size(); lpc++) {
                    lnav_data.ld_rl_view->add_possibility(LNM_SQL, "*",
                        ldh.format_json_getter(pair_iter->first, lpc));
                }
            }
        }

        retval = true;
    }

    lnav_data.ld_db_key_names.clear();

    if (lnav_data.ld_rl_view != NULL) {
        add_env_possibilities(LNM_SQL);

        lnav_data.ld_rl_view->add_possibility(LNM_SQL, "*", sql_keywords);
        lnav_data.ld_rl_view->add_possibility(LNM_SQL, "*", sql_function_names);
        lnav_data.ld_rl_view->add_possibility(LNM_SQL, "*",
            hidden_table_columns);
        lnav_data.ld_rl_view->add_possibility(LNM_SQL, "*", commands);

        for (int lpc = 0; sqlite_registration_funcs[lpc]; lpc++) {
            const struct FuncDef *basic_funcs;
            const struct FuncDefAgg *agg_funcs;

            sqlite_registration_funcs[lpc](&basic_funcs, &agg_funcs);
            for (int lpc2 = 0; basic_funcs && basic_funcs[lpc2].zName; lpc2++) {
                lnav_data.ld_rl_view->add_possibility(LNM_SQL,
                  "*",
                  basic_funcs[lpc2].zName);
            }
            for (int lpc2 = 0; agg_funcs && agg_funcs[lpc2].zName; lpc2++) {
                lnav_data.ld_rl_view->add_possibility(LNM_SQL,
                  "*",
                  agg_funcs[lpc2].zName);
            }
        }
    }

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
    : public logfile_observer {
public:
    loading_observer()
        : lo_last_offset(0) {

    };

    void logfile_indexing(logfile &lf, off_t off, size_t total)
    {
        static sig_atomic_t index_counter = 0;

        if (lnav_data.ld_flags & (LNF_HEADLESS|LNF_CHECK_CONFIG)) {
            return;
        }

        /* XXX require(off <= total); */
        if (off > (off_t)total) {
            off = total;
        }

        if ((((size_t)off == total) && (this->lo_last_offset != off)) ||
            ui_periodic_timer::singleton().time_to_update(index_counter)) {
            lnav_data.ld_bottom_source.update_loading(off, total);
            this->do_update();
            this->lo_last_offset = off;
        }

        if (!lnav_data.ld_looping) {
            throw logfile::error(lf.get_filename(), EINTR);
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
};

class hist_index_delegate : public index_delegate {
public:
    hist_index_delegate(hist_source2 &hs, textview_curses &tc)
            : hid_source(hs), hid_view(tc) {

    };

    void index_start(logfile_sub_source &lss) {
        this->hid_source.clear();
    };

    void index_line(logfile_sub_source &lss, logfile *lf, logfile::iterator ll) {
        if (ll->get_level() & logline::LEVEL_CONTINUED) {
            return;
        }

        hist_source2::hist_type_t ht;

        switch (ll->get_level()) {
            case logline::LEVEL_FATAL:
            case logline::LEVEL_CRITICAL:
            case logline::LEVEL_ERROR:
                ht = hist_source2::HT_ERROR;
                break;
            case logline::LEVEL_WARNING:
                ht = hist_source2::HT_WARNING;
                break;
            default:
                ht = hist_source2::HT_NORMAL;
                break;
        }

        this->hid_source.add_value(ll->get_time(), ht);
        if (ll->is_marked()) {
            this->hid_source.add_value(ll->get_time(), hist_source2::HT_MARK);
        }
    };

    void index_complete(logfile_sub_source &lss) {
        this->hid_view.reload_data();
    };

private:
    hist_source2 &hid_source;
    textview_curses &hid_view;
};

void rebuild_hist(size_t old_count, bool force)
{
    logfile_sub_source &lss = lnav_data.ld_log_source;
    hist_source2 &hs = lnav_data.ld_hist_source2;
    int zoom = lnav_data.ld_hist_zoom;

    hs.set_time_slice(HIST_ZOOM_VALUES[zoom].hl_time_slice);
    lss.text_filters_changed();
}

class textfile_callback {
public:
    textfile_callback() : force(false), front_file(NULL), front_top(-1) { };

    void closed_file(logfile *lf) {
        log_info("closed text file: %s", lf->get_filename().c_str());
        lnav_data.ld_file_names.erase(make_pair(lf->get_filename(), lf->get_fd()));
        lnav_data.ld_files.remove(lf);
        delete lf;
    };

    void promote_file(logfile *lf) {
        if (lnav_data.ld_log_source.insert_file(lf)) {
            force = true;
        }
        else {
            this->closed_file(lf);
        }
    };

    void scanned_file(logfile *lf) {
        if (!lnav_data.ld_files_to_front.empty() &&
                lnav_data.ld_files_to_front.front().first ==
                        lf->get_filename()) {
            front_file = lf;
            front_top = lnav_data.ld_files_to_front.front().second;

            lnav_data.ld_files_to_front.pop_front();
        }
    };

    bool force;
    logfile *front_file;
    int front_top;
};

void rebuild_indexes(bool force)
{
    logfile_sub_source &lss       = lnav_data.ld_log_source;
    textview_curses &   log_view  = lnav_data.ld_views[LNV_LOG];
    textview_curses &   text_view = lnav_data.ld_views[LNV_TEXT];
    vis_line_t          old_bottom(0);
    content_line_t      top_content = content_line_t(-1);

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
        bool new_data;

        old_bottom  = text_view.get_top_for_last_row();
        scroll_down = (text_view.get_top() >= old_bottom &&
            !(lnav_data.ld_flags & LNF_HEADLESS));

        textfile_callback cb;

        new_data = tss->rescan_files(cb);
        force = force || cb.force;

        if (cb.front_file != NULL) {
            ensure_view(&text_view);

            if (tss->current_file() != cb.front_file) {
                tss->to_front(cb.front_file);
                redo_search(LNV_TEXT);
                text_view.reload_data();
                old_bottom = vis_line_t(-1);

                new_data = false;
            }

            if (cb.front_top < 0) {
                cb.front_top += text_view.get_inner_height();
            }
            if (cb.front_top < text_view.get_inner_height()) {
                text_view.set_top(vis_line_t(cb.front_top));
                scroll_down = false;
            }
        }

        if (new_data && lnav_data.ld_search_child[LNV_TEXT].get() != NULL) {
            lnav_data.ld_search_child[LNV_TEXT]->get_grep_proc()->reset();
            lnav_data.ld_search_child[LNV_TEXT]->get_grep_proc()->
            queue_request(grep_line_t(-1));
            lnav_data.ld_search_child[LNV_TEXT]->get_grep_proc()->start();
        }
        text_view.reload_data();

        if (scroll_down && text_view.get_top_for_last_row() > text_view.get_top()) {
            text_view.set_top(text_view.get_top_for_last_row());
        }
    }

    old_time = lnav_data.ld_top_time;
    old_bottom  = log_view.get_top_for_last_row();
    scroll_down = (log_view.get_top() >= old_bottom &&
        !(lnav_data.ld_flags & LNF_HEADLESS));
    if (force) {
        old_count = 0;
    }

    list<logfile *>::iterator         file_iter;
    for (file_iter = lnav_data.ld_files.begin();
         file_iter != lnav_data.ld_files.end(); ) {
        logfile *lf = *file_iter;

        if (!lf->exists() || lf->is_closed()) {
            log_info("closed log file: %s", lf->get_filename().c_str());
            lnav_data.ld_file_names.erase(make_pair(lf->get_filename(), lf->get_fd()));
            lnav_data.ld_text_source.remove(lf);
            lnav_data.ld_log_source.remove_file(lf);
            file_iter = lnav_data.ld_files.erase(file_iter);
            force = true;

            delete lf;
        }
        else {
            ++file_iter;
        }
    }

    if (lss.rebuild_index(force)) {
        size_t      new_count = lss.text_line_count();
        grep_line_t start_line;
        int         lpc;

        log_view.reload_data();

        if (scroll_down && log_view.get_top_for_last_row() > log_view.get_top()) {
            log_view.set_top(log_view.get_top_for_last_row());
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

        start_line = force ? grep_line_t(0) : grep_line_t(-1);

        if (force) {
            if (lnav_data.ld_search_child[LNV_LOG].get() != NULL) {
                lnav_data.ld_search_child[LNV_LOG]->get_grep_proc()->invalidate();
            }
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

    if (!lnav_data.ld_view_stack.empty()) {
        textview_curses *tc = lnav_data.ld_view_stack.top();
        lnav_data.ld_bottom_source.update_filtered(tc->get_sub_source());
        lnav_data.ld_scroll_broadcaster.invoke(tc);
    }
}

static bool append_default_files(lnav_flags_t flag)
{
    bool retval = true;

    if (lnav_data.ld_flags & flag) {
        pair<multimap<lnav_flags_t, string>::iterator,
             multimap<lnav_flags_t, string>::iterator> range;
        for (range = DEFAULT_FILES.equal_range(flag);
             range.first != range.second;
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

static void sigchld(int sig)
{
    lnav_data.ld_child_terminated = true;
}

static void open_schema_view(void)
{
    textview_curses *schema_tc = &lnav_data.ld_views[LNV_SCHEMA];
    string schema;

    dump_sqlite_schema(lnav_data.ld_db, schema);

    schema += "\n\n-- Virtual Table Definitions --\n\n";
    schema += ENVIRON_CREATE_STMT;
    schema += LNAV_VIEWS_CREATE_STMT;
    for (log_vtab_manager::iterator vtab_iter =
            lnav_data.ld_vtab_manager->begin();
         vtab_iter != lnav_data.ld_vtab_manager->end();
         ++vtab_iter) {
        schema += "\n" + vtab_iter->second->get_table_statement();
    }

    delete schema_tc->get_sub_source();

    schema_tc->set_sub_source(new plain_text_source(schema));
}

static void open_pretty_view(void)
{
    static const char *NOTHING_MSG =
            "Nothing to pretty-print";

    textview_curses *top_tc = lnav_data.ld_view_stack.top();
    textview_curses *pretty_tc = &lnav_data.ld_views[LNV_PRETTY];
    textview_curses *log_tc = &lnav_data.ld_views[LNV_LOG];
    textview_curses *text_tc = &lnav_data.ld_views[LNV_TEXT];
    ostringstream stream;

    delete pretty_tc->get_sub_source();
    if (top_tc->get_inner_height() == 0) {
        pretty_tc->set_sub_source(new plain_text_source(NOTHING_MSG));
        return;
    }

    if (top_tc == log_tc) {
        logfile_sub_source &lss = lnav_data.ld_log_source;
        bool first_line = true;

        for (vis_line_t vl = log_tc->get_top(); vl <= log_tc->get_bottom(); ++vl) {
            content_line_t cl = lss.at(vl);
            logfile *lf = lss.find(cl);
            logfile::iterator ll = lf->begin() + cl;
            shared_buffer_ref sbr;

            if (!first_line && ll->is_continued()) {
                continue;
            }
            ll = lf->message_start(ll);

            lf->read_full_message(ll, sbr);
            data_scanner ds(sbr);
            pretty_printer pp(&ds);

            // TODO: dump more details of the line in the output.
            stream << trim(pp.print()) << endl;
            first_line = false;
        }
    }
    else if (top_tc == text_tc) {
        logfile *lf = lnav_data.ld_text_source.current_file();

        for (vis_line_t vl = text_tc->get_top(); vl <= text_tc->get_bottom(); ++vl) {
            logfile::iterator ll = lf->begin() + vl;
            shared_buffer_ref sbr;

            lf->read_full_message(ll, sbr);
            data_scanner ds(sbr);
            pretty_printer pp(&ds);

            stream << pp.print() << endl;
        }
    }
    pretty_tc->set_sub_source(new plain_text_source(stream.str()));
    if (lnav_data.ld_last_pretty_print_top != log_tc->get_top()) {
        pretty_tc->set_top(vis_line_t(0));
    }
    lnav_data.ld_last_pretty_print_top = log_tc->get_top();
    redo_search(LNV_PRETTY);
}

bool toggle_view(textview_curses *toggle_tc)
{
    textview_curses *tc     = lnav_data.ld_view_stack.empty() ? NULL : lnav_data.ld_view_stack.top();
    bool             retval = false;

    require(toggle_tc != NULL);
    require(toggle_tc >= &lnav_data.ld_views[0]);
    require(toggle_tc < &lnav_data.ld_views[LNV__MAX]);

    if (tc == toggle_tc) {
        lnav_data.ld_view_stack.pop();
    }
    else {
        if (toggle_tc == &lnav_data.ld_views[LNV_SCHEMA]) {
            open_schema_view();
        }
        else if (toggle_tc == &lnav_data.ld_views[LNV_PRETTY]) {
            open_pretty_view();
        }
        lnav_data.ld_view_stack.push(toggle_tc);
        retval = true;
    }
    tc = lnav_data.ld_view_stack.top();
    tc->set_needs_update();
    lnav_data.ld_scroll_broadcaster.invoke(tc);

    update_view_name();

    return retval;
}

void redo_search(lnav_view_t view_index)
{
    textview_curses *tc = &lnav_data.ld_views[view_index];

    tc->reload_data();
    if (lnav_data.ld_search_child[view_index].get() != NULL) {
        grep_proc *gp = lnav_data.ld_search_child[view_index]->get_grep_proc();

        tc->match_reset();
        gp->reset();
        gp->queue_request(grep_line_t(0));
        gp->start();
    }
    if (tc == lnav_data.ld_view_stack.top()) {
        lnav_data.ld_scroll_broadcaster.invoke(tc);
    }
}

/**
 * Ensure that the view is on the top of the view stack.
 *
 * @param expected_tc The text view that should be on top.
 * @return True if the view was already on the top of the stack.
 */
bool ensure_view(textview_curses *expected_tc)
{
    textview_curses *tc = lnav_data.ld_view_stack.empty() ? NULL : lnav_data.ld_view_stack.top();
    bool retval = true;

    if (tc != expected_tc) {
        toggle_view(expected_tc);
        retval = false;
    }
    return retval;
}

vis_line_t next_cluster(
    vis_line_t(bookmark_vector<vis_line_t>::*f) (vis_line_t),
    bookmark_type_t *bt,
    vis_line_t top)
{
    textview_curses *tc = lnav_data.ld_view_stack.top();
    vis_bookmarks &bm = tc->get_bookmarks();
    bookmark_vector<vis_line_t> &bv = bm[bt];
    bool top_is_marked = binary_search(bv.begin(), bv.end(), top);
    vis_line_t last_top(top);

    while ((top = (bv.*f)(top)) != -1) {
        int diff = top - last_top;

        if (!top_is_marked || diff > 1) {
            return top;
        }
        else if (diff < -1) {
            last_top = top;
            while ((top = (bv.*f)(top)) != -1) {
                if (std::abs(last_top - top) > 1)
                    break;
                last_top = top;
            }
            return last_top;
        }
        last_top = top;
    }

    return vis_line_t(-1);
}

bool moveto_cluster(vis_line_t(bookmark_vector<vis_line_t>::*f) (vis_line_t),
        bookmark_type_t *bt,
        vis_line_t top)
{
    textview_curses *tc = lnav_data.ld_view_stack.top();
    vis_line_t new_top;

    new_top = next_cluster(f, bt, top);
    if (new_top != -1) {
        tc->set_top(new_top);
        return true;
    }

    alerter::singleton().chime();

    return false;
}

static void handle_rl_key(int ch)
{
    switch (ch) {
    case KEY_PPAGE:
    case KEY_NPAGE:
        handle_paging_key(ch);
        break;

    case KEY_CTRL_RBRACKET:
        lnav_data.ld_rl_view->abort();
        break;

    default:
        lnav_data.ld_rl_view->handle_key(ch);
        break;
    }
}

void rl_blur(void *dummy, readline_curses *rc)
{
    field_overlay_source *fos;

    fos = (field_overlay_source *)lnav_data.ld_views[LNV_LOG].get_overlay_source();
    fos->fos_active = fos->fos_active_prev;
}

readline_context::command_map_t lnav_commands;

void execute_search(lnav_view_t view, const std::string &regex_orig)
{
    auto_ptr<grep_highlighter> &gc = lnav_data.ld_search_child[view];
    textview_curses &           tc = lnav_data.ld_views[view];
    std::string regex = regex_orig;
    pcre *      code = NULL;

    if ((gc.get() == NULL) || (regex != lnav_data.ld_last_search[view])) {
        const char *errptr;
        int         eoff;
        bool quoted = false;

        tc.match_reset();

        if (regex.empty() && gc.get() != NULL) {
            tc.grep_begin(*(gc->get_grep_proc()));
            tc.grep_end(*(gc->get_grep_proc()));
        }
        gc.reset();

        log_debug("start search for: '%s'", regex.c_str());

        if (regex.empty()) {
            lnav_data.ld_bottom_source.grep_error("");
        }
        else if ((code = pcre_compile(regex.c_str(),
                                      PCRE_CASELESS,
                                      &errptr,
                                      &eoff,
                                      NULL)) == NULL) {
            lnav_data.ld_bottom_source.grep_error(
                "regexp error: " + string(errptr));

            quoted = true;
            regex = pcrecpp::RE::QuoteMeta(regex);

            log_info("invalid search regex, using quoted: %s", regex.c_str());
            if ((code = pcre_compile(regex.c_str(),
                                     PCRE_CASELESS,
                                     &errptr,
                                     &eoff,
                                     NULL)) == NULL) {
                log_error("Unable to compile quoted regex: %s", regex.c_str());
            }
        }

        if (code != NULL) {
            textview_curses::highlighter hl(
                code, false, view_colors::VCR_SEARCH);

            if (!quoted) {
                lnav_data.ld_bottom_source.grep_error("");
            }
            lnav_data.ld_bottom_source.set_prompt("");

            textview_curses::highlight_map_t &hm = tc.get_highlights();
            hm["$search"] = hl;

            auto_ptr<grep_proc> gp(new grep_proc(code, tc));

            gp->queue_request(grep_line_t(tc.get_top()));
            if (tc.get_top() > 0) {
                gp->queue_request(grep_line_t(0), grep_line_t(tc.get_top()));
            }
            gp->start();
            gp->set_sink(&tc);

            tc.set_follow_search(true);

            auto_ptr<grep_highlighter> gh(
                new grep_highlighter(gp, "$search", hm));
            gc = gh;
        }

        if (view == LNV_LOG) {
            static intern_string_t log_search_name = intern_string::lookup("log_search");

            lnav_data.ld_vtab_manager->unregister_vtab(log_search_name);
            if (code != NULL) {
                lnav_data.ld_vtab_manager->register_vtab(new log_search_table(
                        regex.c_str(), log_search_name));
            }
        }
    }

    lnav_data.ld_last_search[view] = regex;
}

static void usage(void)
{
    const char *usage_msg =
        "usage: %s [options] [logfile1 logfile2 ...]\n"
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
        "  -H         Display the internal help text.\n"
        "  -I path    An additional configuration directory.\n"
        "  -i         Install the given format files and exit.  Pass 'extra'\n"
        "             to install the default set of third-party formats.\n"
        "  -u         Update formats installed from git repositories.\n"
        "  -C         Check configuration and then exit.\n"
        "  -d file    Write debug messages to the given file.\n"
        "  -V         Print version information.\n"
        "\n"
        "  -a         Load all of the most recent log file types.\n"
        "  -r         Load older rotated log files as well.\n"
        "  -t         Prepend timestamps to the lines of data being read in\n"
        "             on the standard input.\n"
        "  -w file    Write the contents of the standard input to this file.\n"
        "\n"
        "  -c cmd     Execute a command after the files have been loaded.\n"
        "  -f path    Execute the commands in the given file.\n"
        "  -n         Run without the curses UI. (headless mode)\n"
        "  -q         Do not print the log messages after executing all\n"
        "             of the commands or when lnav is reading from stdin.\n"
        "\n"
        "Optional arguments:\n"
        "  logfile1          The log files or directories to view.  If a\n"
        "                    directory is given, all of the files in the\n"
        "                    directory will be loaded.\n"
        "\n"
        "Examples:\n"
        "  To load and follow the syslog file:\n"
        "    $ lnav\n"
        "\n"
        "  To load all of the files in /var/log:\n"
        "    $ lnav /var/log\n"
        "\n"
        "  To watch the output of make with timestamps prepended:\n"
        "    $ make 2>&1 | lnav -t\n"
        "\n"
        "Version: " VCS_PACKAGE_STRING "\n";

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
        logline *ll;

        ll = lss.find_line(lss.at(lv->get_top()));
        lnav_data.ld_top_time = ll->get_time();
        lnav_data.ld_top_time_millis = ll->get_millis();
        ll = lss.find_line(lss.at(lv->get_bottom()));
        lnav_data.ld_bottom_time = ll->get_time();
        lnav_data.ld_bottom_time_millis = ll->get_millis();
    }
    if (lv == &lnav_data.ld_views[LNV_HISTOGRAM] &&
        lv->get_inner_height() > 0) {
        hist_source2 &hs = lnav_data.ld_hist_source2;

        lnav_data.ld_top_time    = hs.time_for_row(lv->get_top());
        lnav_data.ld_top_time_millis = 0;
        lnav_data.ld_bottom_time = hs.time_for_row(lv->get_bottom());
        lnav_data.ld_bottom_time_millis = 0;
    }
}

static void clear_last_user_mark(void *, listview_curses *lv)
{
    textview_curses *tc = (textview_curses *) lv;
    if (lnav_data.ld_select_start.find(tc) != lnav_data.ld_select_start.end() &&
            lnav_data.ld_select_start[tc] != tc->get_top()) {
        lnav_data.ld_select_start.erase(tc);
        lnav_data.ld_last_user_mark.erase(tc);
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
    static loading_observer obs;
    list<logfile *>::iterator file_iter;
    struct stat st;
    int         rc;

    if (lnav_data.ld_closed_files.count(filename)) {
        return;
    }

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

                log_info("loading new file: %s", filename.c_str());
                    lf->set_logfile_observer(&obs);
                lnav_data.ld_files.push_back(lf);
                lnav_data.ld_text_source.push_back(lf);
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

    if (is_url(path.c_str())) {
        return;
    }
    else if (glob(path.c_str(), GLOB_NOCHECK, NULL, gl.inout()) == 0) {
        int lpc;

        if (gl->gl_pathc == 1 /*&& gl.gl_matchc == 0*/) {
            /* It's a pattern that doesn't match any files
             * yet, allow it through since we'll load it in
             * dynamically.
             */
            if (access(path.c_str(), F_OK) == -1) {
                required = false;
            }
        }
        if (gl->gl_pathc > 1 ||
            strcmp(path.c_str(), gl->gl_pathv[0]) != 0) {
            required = false;
        }
        for (lpc = 0; lpc < (int)gl->gl_pathc; lpc++) {
            auto_mem<char> abspath;

            if ((abspath = realpath(gl->gl_pathv[lpc], NULL)) == NULL) {
                if (required) {
                    fprintf(stderr, "Cannot find file: %s -- %s",
                        gl->gl_pathv[lpc], strerror(errno));
                }
            }
            else if (required || access(abspath.in(), R_OK) == 0){
                watch_logfile(abspath.in(), -1, required);
            }
        }
    }
}

bool rescan_files(bool required)
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
        logfile *lf = *file_iter;

        if (!lf->exists() || lf->is_closed()) {
            return true;
        }
        else {
            ++file_iter;
        }
    }

    return retval;
}

static string execute_action(log_data_helper &ldh,
                             int value_index,
                             const string &action_name)
{
    std::map<string, log_format::action_def>::const_iterator iter;
    logline_value &lv = ldh.ldh_line_values[value_index];
    logfile *lf = ldh.ldh_file;
    const log_format *format = lf->get_format();
    pid_t child_pid;
    string retval;

    iter = format->lf_action_defs.find(action_name);

    const log_format::action_def &action = iter->second;

    auto_pipe in_pipe(STDIN_FILENO);
    auto_pipe out_pipe(STDOUT_FILENO);
    auto_pipe err_pipe(STDERR_FILENO);

    in_pipe.open();
    if (action.ad_capture_output)
        out_pipe.open();
    err_pipe.open();

    child_pid = fork();

    in_pipe.after_fork(child_pid);
    out_pipe.after_fork(child_pid);
    err_pipe.after_fork(child_pid);

    switch (child_pid) {
    case -1:
        retval = "error: unable to fork child process -- " + string(strerror(errno));
        break;
    case 0: {
            const char *args[action.ad_cmdline.size() + 1];
            set<std::string> path_set(format->get_source_path());
            char env_buffer[64];
            int value_line;
            string path;

            setenv("LNAV_ACTION_FILE", lf->get_filename().c_str(), 1);
            snprintf(env_buffer, sizeof(env_buffer),
                "%ld",
                (ldh.ldh_line - lf->begin()) + 1);
            setenv("LNAV_ACTION_FILE_LINE", env_buffer, 1);
            snprintf(env_buffer, sizeof(env_buffer), "%d", ldh.ldh_y_offset + 1);
            setenv("LNAV_ACTION_MSG_LINE", env_buffer, 1);
            setenv("LNAV_ACTION_VALUE_NAME", lv.lv_name.get(), 1);
            value_line = ldh.ldh_y_offset - ldh.get_value_line(lv) + 1;
            snprintf(env_buffer, sizeof(env_buffer), "%d", value_line);
            setenv("LNAV_ACTION_VALUE_LINE", env_buffer, 1);

            for (set<string>::iterator path_iter = path_set.begin();
                 path_iter != path_set.end();
                 ++path_iter) {
                if (!path.empty()) {
                    path += ":";
                }
                path += *path_iter;
            }
            path += ":" + string(getenv("PATH"));
            setenv("PATH", path.c_str(), 1);
            for (size_t lpc = 0; lpc < action.ad_cmdline.size(); lpc++) {
                args[lpc] = action.ad_cmdline[lpc].c_str();
            }
            args[action.ad_cmdline.size()] = NULL;
            execvp(args[0], (char *const *) args);
            fprintf(stderr,
                "error: could not exec process -- %s:%s\n",
                args[0],
                strerror(errno));
            _exit(0);
        }
        break;
    default: {
            static int exec_count = 0;

            string value = lv.to_string();
            line_buffer lb;
            off_t off = 0;
            line_value lv;

            lnav_data.ld_children.push_back(child_pid);

            if (write(in_pipe.write_end(), value.c_str(), value.size()) == -1) {
                perror("execute_action write");
            }
            in_pipe.close();

            lb.set_fd(err_pipe.read_end());

            lb.read_line(off, lv);

            if (out_pipe.read_end() != -1) {
                piper_proc *pp = new piper_proc(out_pipe.read_end(), false);
                char desc[128];

                lnav_data.ld_pipers.push_back(pp);
                snprintf(desc,
                    sizeof(desc), "[%d] Output of %s",
                    exec_count++,
                    action.ad_cmdline[0].c_str());
                lnav_data.ld_file_names.insert(make_pair(
                    desc,
                    pp->get_fd()));
                lnav_data.ld_files_to_front.push_back(make_pair(desc, 0));
            }

            retval = string(lv.lv_start, lv.lv_len);
        }
        break;
    }

    return retval;
}

class action_delegate : public text_delegate {
public:
    action_delegate(logfile_sub_source &lss)
            : ad_log_helper(lss),
              ad_press_line(-1),
              ad_press_value(-1),
              ad_line_index(0) {

    };

    virtual bool text_handle_mouse(textview_curses &tc, mouse_event &me) {
        bool retval = false;

        if (me.me_button != BUTTON_LEFT) {
            return false;
        }

        vis_line_t mouse_line = vis_line_t(tc.get_top() + me.me_y);
        int mouse_left = tc.get_left() + me.me_x;

        switch (me.me_state) {
        case BUTTON_STATE_PRESSED:
            if (mouse_line >= vis_line_t(0) && mouse_line <= tc.get_bottom()) {
                size_t line_end_index = 0;
                int x_offset;

                this->ad_press_line = mouse_line;
                this->ad_log_helper.parse_line(mouse_line, true);

                this->ad_log_helper.get_line_bounds(this->ad_line_index, line_end_index);

                struct line_range lr(this->ad_line_index, line_end_index);

                this->ad_press_value = -1;

                x_offset = this->ad_line_index + mouse_left;
                if (lr.contains(x_offset)) {
                    for (size_t lpc = 0;
                         lpc < this->ad_log_helper.ldh_line_values.size();
                         lpc++) {
                        logline_value &lv = this->ad_log_helper.ldh_line_values[lpc];

                        if (lv.lv_origin.contains(x_offset)) {
                            this->ad_press_value = lpc;
                            break;
                        }
                    }
                }
            }
            break;
        case BUTTON_STATE_DRAGGED:
            if (mouse_line != this->ad_press_line) {
                this->ad_press_value = -1;
            }
            if (this->ad_press_value != -1) {
                retval = true;
            }
            break;
        case BUTTON_STATE_RELEASED:
            if (this->ad_press_value != -1 && this->ad_press_line == mouse_line) {
                logline_value &lv = this->ad_log_helper.ldh_line_values[this->ad_press_value];
                int x_offset = this->ad_line_index + mouse_left;

                if (lv.lv_origin.contains(x_offset)) {
                    logfile *lf = this->ad_log_helper.ldh_file;
                    const vector<string> *actions;

                    actions = lf->get_format()->get_actions(lv);
                    if (actions != NULL && !actions->empty()) {
                        string rc = execute_action(
                            this->ad_log_helper, this->ad_press_value, actions->at(0));

                        lnav_data.ld_rl_view->set_value(rc);
                    }
                }
                retval = true;
            }
            break;
        }

        return retval;
    };

    log_data_helper ad_log_helper;
    vis_line_t ad_press_line;
    int ad_press_value;
    size_t ad_line_index;
};

class lnav_behavior : public mouse_behavior {
public:
    enum lb_mode_t {
        LB_MODE_NONE,
        LB_MODE_DOWN,
        LB_MODE_UP,
        LB_MODE_DRAG
    };

    lnav_behavior() {};

    int scroll_polarity(int button)
    {
        return button == xterm_mouse::XT_SCROLL_UP ? -1 : 1;
    };

    void mouse_event(int button, bool release, int x, int y)
    {
        textview_curses *   tc  = lnav_data.ld_view_stack.top();
        struct mouse_event me;

        switch (button & xterm_mouse::XT_BUTTON__MASK) {
        case xterm_mouse::XT_BUTTON1:
            me.me_button = BUTTON_LEFT;
            break;
        case xterm_mouse::XT_BUTTON2:
            me.me_button = BUTTON_MIDDLE;
            break;
        case xterm_mouse::XT_BUTTON3:
            me.me_button = BUTTON_RIGHT;
            break;
        case xterm_mouse::XT_SCROLL_UP:
            me.me_button = BUTTON_SCROLL_UP;
            break;
        case xterm_mouse::XT_SCROLL_DOWN:
            me.me_button = BUTTON_SCROLL_DOWN;
            break;
        }

        if (button & xterm_mouse::XT_DRAG_FLAG) {
            me.me_state = BUTTON_STATE_DRAGGED;
        }
        else if (release) {
            me.me_state = BUTTON_STATE_RELEASED;
        }
        else {
            me.me_state = BUTTON_STATE_PRESSED;
        }

        gettimeofday(&me.me_time, NULL);
        me.me_x = x - 1;
        me.me_y = y - tc->get_y() - 1;

        tc->handle_mouse(me);
    };

private:
};

static void handle_key(int ch)
{
    lnav_data.ld_input_state.push_back(ch);

    switch (ch) {
    case CEOF:
    case KEY_RESIZE:
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
        case LNM_EXEC:
            handle_rl_key(ch);
            break;

        default:
            require(0);
            break;
        }
    }
}

void update_hits(void *dummy, textview_curses *tc)
{
    if (!lnav_data.ld_view_stack.empty() &&
        tc == lnav_data.ld_view_stack.top()) {
        lnav_data.ld_bottom_source.update_hits(tc);
    }
}

static void gather_pipers(void)
{
    for (std::list<piper_proc *>::iterator iter = lnav_data.ld_pipers.begin();
         iter != lnav_data.ld_pipers.end(); ) {
        piper_proc *pp = *iter;
        pid_t child_pid = pp->get_child_pid();
        if (pp->has_exited()) {
            log_info("child piper has exited -- %d", child_pid);
            delete pp;
            iter = lnav_data.ld_pipers.erase(iter);
        } else {
            ++iter;
        }
    }
}

static void wait_for_pipers(void)
{
    for (;;) {
        gather_pipers();
        if (lnav_data.ld_pipers.empty()) {
            log_debug("all pipers finished");
            break;
        }
        else {
            usleep(10000);
            rebuild_indexes(false);
        }
        log_debug("%d pipers still active",
                lnav_data.ld_pipers.size());
    }
}

static void looper(void)
{
    try {
        readline_context command_context("cmd", &lnav_commands);

        readline_context search_context("search");
        readline_context index_context("capture");
        readline_context sql_context("sql", NULL, false);
        readline_context exec_context("exec");
        readline_curses  rlc;
        int lpc;

        command_context.set_highlighter(readline_command_highlighter);
        search_context
                .set_append_character(0)
                .set_highlighter(readline_regex_highlighter);
        sql_context
                .set_highlighter(readline_sqlite_highlighter)
                .set_quote_chars("\"");

        listview_curses::action::broadcaster &sb =
            lnav_data.ld_scroll_broadcaster;

        rlc.add_context(LNM_COMMAND, command_context);
        rlc.add_context(LNM_SEARCH, search_context);
        rlc.add_context(LNM_CAPTURE, index_context);
        rlc.add_context(LNM_SQL, sql_context);
        rlc.add_context(LNM_EXEC, exec_context);
        rlc.start();

        lnav_data.ld_rl_view = &rlc;

        lnav_data.ld_rl_view->add_possibility(
            LNM_COMMAND, "graph", "\\d+(?:\\.\\d+)?");
        lnav_data.ld_rl_view->add_possibility(
            LNM_COMMAND, "graph", "([:= \\t]\\d+(?:\\.\\d+)?)");

        lnav_data.ld_rl_view->add_possibility(
            LNM_COMMAND, "viewname", lnav_view_strings);

        lnav_data.ld_rl_view->add_possibility(
                LNM_COMMAND, "zoomlevel", lnav_zoom_strings);

        lnav_data.ld_rl_view->add_possibility(
            LNM_COMMAND, "levelname", logline::level_names);

        (void)signal(SIGINT, sigint);
        (void)signal(SIGTERM, sigint);
        (void)signal(SIGWINCH, sigwinch);
        (void)signal(SIGCHLD, sigchld);

        screen_curses sc;
        lnav_behavior lb;

        ui_periodic_timer::singleton();

        lnav_data.ld_mouse.set_behavior(&lb);
        lnav_data.ld_mouse.set_enabled(check_experimental("mouse"));

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
        rlc.set_change_action(readline_curses::action(rl_change));
        rlc.set_perform_action(readline_curses::action(rl_callback));
        rlc.set_timeout_action(readline_curses::action(rl_search));
        rlc.set_abort_action(readline_curses::action(rl_abort));
        rlc.set_display_match_action(
            readline_curses::action(rl_display_matches));
        rlc.set_display_next_action(
            readline_curses::action(rl_display_next));
        rlc.set_blur_action(readline_curses::action(rl_blur));
        rlc.set_alt_value(HELP_MSG_2(
            e, E, "to move forward/backward through error messages"));

        (void)curs_set(0);

        lnav_data.ld_view_stack.push(&lnav_data.ld_views[LNV_LOG]);
        update_view_name();

        for (lpc = 0; lpc < LNV__MAX; lpc++) {
            lnav_data.ld_views[lpc].set_window(lnav_data.ld_window);
            lnav_data.ld_views[lpc].set_y(1);
            lnav_data.ld_views[lpc].
            set_height(vis_line_t(-(rlc.get_height() + 1)));
            lnav_data.ld_views[lpc].
            set_scroll_action(sb.get_functor());
            lnav_data.ld_views[lpc].set_search_action(
                textview_curses::action(update_hits));
        }

        lnav_data.ld_status[LNS_TOP].set_top(0);
        lnav_data.ld_status[LNS_BOTTOM].set_top(-(rlc.get_height() + 1));
        for (lpc = 0; lpc < LNS__MAX; lpc++) {
            lnav_data.ld_status[lpc].set_window(lnav_data.ld_window);
        }
        lnav_data.ld_status[LNS_TOP].set_data_source(
            &lnav_data.ld_top_source);
        lnav_data.ld_status[LNS_BOTTOM].set_data_source(
            &lnav_data.ld_bottom_source);

        lnav_data.ld_match_view.set_show_bottom_border(true);

        sb.push_back(view_action<listview_curses>(update_times));
        sb.push_back(view_action<listview_curses>(clear_last_user_mark));
        sb.push_back(&lnav_data.ld_top_source.filename_wire);
        sb.push_back(&lnav_data.ld_bottom_source.line_number_wire);
        sb.push_back(&lnav_data.ld_bottom_source.percent_wire);
        sb.push_back(&lnav_data.ld_bottom_source.marks_wire);
        sb.push_back(&lnav_data.ld_term_extra.filename_wire);

        lnav_data.ld_status[0].window_change();
        lnav_data.ld_status[1].window_change();

        execute_file(dotlnav_path("session"));

        lnav_data.ld_scroll_broadcaster.invoke(lnav_data.ld_view_stack.top());

        bool session_loaded = false;
        ui_periodic_timer &timer = ui_periodic_timer::singleton();
        static sig_atomic_t index_counter;

        timer.start_fade(index_counter, 1);
        while (lnav_data.ld_looping) {
            vector<struct pollfd> pollfds;
            struct timeval to = { 0, 333000 };
            int            rc;

            lnav_data.ld_top_source.update_time();

            if (rescan_files()) {
                rebuild_indexes(true);
            }

            lnav_data.ld_status[LNS_TOP].do_update();
            lnav_data.ld_view_stack.top()->do_update();
            lnav_data.ld_match_view.do_update();
            lnav_data.ld_status[LNS_BOTTOM].do_update();
            rlc.do_update();
            refresh();

            pollfds.push_back((struct pollfd) {
                    STDIN_FILENO,
                    POLLIN,
                    0
            });
            rlc.update_poll_set(pollfds);
            for (lpc = 0; lpc < LG__MAX; lpc++) {
                auto_ptr<grep_highlighter> &gc =
                        lnav_data.ld_grep_child[lpc];

                if (gc.get() != NULL) {
                    gc->get_grep_proc()->update_poll_set(pollfds);
                }
            }
            for (lpc = 0; lpc < LNV__MAX; lpc++) {
                auto_ptr<grep_highlighter> &gc =
                        lnav_data.ld_search_child[lpc];

                if (gc.get() != NULL) {
                    gc->get_grep_proc()->update_poll_set(pollfds);
                }
            }

            rc = poll(&pollfds[0], pollfds.size(), to.tv_usec / 1000);

            if (rc < 0) {
                switch (errno) {
                break;
                case 0:
                case EINTR:
                    break;

                default:
                    log_error("select %s", strerror(errno));
                    lnav_data.ld_looping = false;
                    break;
                }
            }
            else {
                if (pollfd_ready(pollfds, STDIN_FILENO)) {
                    static size_t escape_index = 0;
                    static char escape_buffer[32];

                    int ch;

                    while ((ch = getch()) != ERR) {
                        alerter::singleton().new_input(ch);

                        /* Check to make sure there is enough space for a
                         * character and a string terminator.
                         */
                        if (escape_index >= sizeof(escape_buffer) - 2) {
                            escape_index = 0;
                        }
                        else if (escape_index > 0) {
                            escape_buffer[escape_index++] = ch;
                            escape_buffer[escape_index] = '\0';

                            if (strcmp("\x1b[", escape_buffer) == 0) {
                                lnav_data.ld_mouse.handle_mouse(ch);
                            }
                            else {
                                for (size_t lpc = 0; lpc < escape_index; lpc++) {
                                    handle_key(escape_buffer[lpc]);
                                }
                            }
                            escape_index = 0;
                            continue;
                        }
                        switch (ch) {
                        case CEOF:
                        case KEY_RESIZE:
                            break;

                        case '\x1b':
                            escape_index = 0;
                            escape_buffer[escape_index++] = ch;
                            escape_buffer[escape_index] = '\0';
                            break;

                        case KEY_MOUSE:
                            lnav_data.ld_mouse.handle_mouse(ch);
                            break;

                        default:
                            handle_key(ch);
                            break;
                        }

                        if (!lnav_data.ld_looping) {
                            // No reason to keep processing input after the
                            // user has quit.  The view stack will also be
                            // empty, which will cause issues.
                            break;
                        }
                    }
                }
                for (lpc = 0; lpc < LG__MAX; lpc++) {
                    auto_ptr<grep_highlighter> &gc =
                        lnav_data.ld_grep_child[lpc];

                    if (gc.get() != NULL) {
                        gc->get_grep_proc()->check_poll_set(pollfds);
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
                        gc->get_grep_proc()->check_poll_set(pollfds);

                        if (!lnav_data.ld_view_stack.empty()) {
                            lnav_data.ld_bottom_source.
                            update_hits(lnav_data.ld_view_stack.top());
                        }
                    }
                }
                rlc.check_poll_set(pollfds);
            }

            if (timer.fade_diff(index_counter) == 0) {
                static bool initial_build = false;

                if (lnav_data.ld_mode == LNM_PAGING) {
                    timer.start_fade(index_counter, 1);
                }
                else {
                    timer.start_fade(index_counter, 3);
                }
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
                if (!initial_build &&
                        lnav_data.ld_log_source.text_line_count() == 0 &&
                        !lnav_data.ld_other_files.empty()) {
                    ensure_view(&lnav_data.ld_views[LNV_SCHEMA]);
                }

                if (!initial_build && lnav_data.ld_flags & LNF_HELP) {
                    toggle_view(&lnav_data.ld_views[LNV_HELP]);
                    initial_build = true;
                }
                if (lnav_data.ld_log_source.text_line_count() > 0 ||
                        lnav_data.ld_text_source.text_line_count() > 0 ||
                        !lnav_data.ld_other_files.empty()) {
                    initial_build = true;
                }

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

                    vector<pair<string, string> > msgs;

                    execute_init_commands(msgs);

                    if (!msgs.empty()) {
                        pair<string, string> last_msg = msgs.back();

                        lnav_data.ld_rl_view->set_value(last_msg.first);
                        lnav_data.ld_rl_view->set_alt_value(last_msg.second);
                    }

                    rebuild_indexes(true);
                    session_loaded = true;
                }
            }

            if (lnav_data.ld_winched) {
                struct winsize size;

                lnav_data.ld_winched = false;

                if (ioctl(fileno(stdout), TIOCGWINSZ, &size) == 0) {
                    resizeterm(size.ws_row, size.ws_col);
                }
                rlc.window_change();
                lnav_data.ld_status[0].window_change();
                lnav_data.ld_status[1].window_change();
                lnav_data.ld_view_stack.top()->set_needs_update();
            }

            if (lnav_data.ld_child_terminated) {
                lnav_data.ld_child_terminated = false;

                for (std::list<pid_t>::iterator iter = lnav_data.ld_children.begin();
                    iter != lnav_data.ld_children.end();
                    ++iter) {
                    int rc, child_stat;

                    rc = waitpid(*iter, &child_stat, WNOHANG);
                    if (rc == -1 || rc == 0)
                        continue;

                    iter = lnav_data.ld_children.erase(iter);
                }

                gather_pipers();
            }
        }
    }
    catch (readline_curses::error & e) {
        log_error("error: %s", strerror(e.e_err));
    }
}

static void setup_highlights(textview_curses::highlight_map_t &hm)
{
    hm["$kw"] = textview_curses::highlighter(xpcre_compile(
        "(?:"
          "\\balter |"
          "\\band\\b|"
          "\\bas |"
          "\\bbetween\\b|"
          "\\bbool\\b|"
          "\\bboolean\\b|"
          "\\bbreak\\b|"
          "\\bcase\\b|"
          "\\bcatch\\b|"
          "\\bchar\\b|"
          "\\bclass\\b|"
          "\\bcollate\\b|"
          "\\bconst\\b|"
          "\\bcontinue\\b|"
          "\\bcreate\\s+(?:virtual)?|"
          "\\bdatetime\\b|"
          "\\bdef |"
          "\\bdefault[:\\s]|"
          "\\bdo\\b|"
          "\\bdone\\b|"
          "\\bdouble\\b|"
          "\\bdrop\\b|"
          "\\belif |"
          "\\belse\\b|"
          "\\benum\\b|"
          "\\bendif\\b|"
          "\\besac\\b|"
          "\\bexcept[\\s:]|"
          "\\bexists\\b|"
          "\\bexport\\b|"
          "\\bextends\\b|"
          "\\bextern\\b|"
          "\\bfalse\\b|"
          "\\bfi\\b|"
          "\\bfloat\\b|"
          "\\bfor\\b|"
          "\\bforeign\\s+key\\b|"
          "\\bfrom |"
          "\\bgoto\\b|"
          "\\bgroup by |"
          "\\bif\\b|"
          "\\bimport |"
          "\\bimplements\\b|"
          "\\bin\\b|"
          "\\binline\\b|"
          "\\binner\\b|"
          "\\binsert |"
          "\\bint\\b|"
          "\\binto\\b|"
          "\\binterface\\b|"
          "\\bjoin\\b|"
          "\\blambda\\b|"
          "\\blet\\b|"
          "\\blong\\b|"
          "\\bnamespace\\b|"
          "\\bnew\\b|"
          "\\bnot\\b|"
          "\\bnull\\b|"
          "\\boperator\\b|"
          "\\bor\\b|"
          "\\border by |"
          "\\bpackage\\b|"
          "\\bprimary\\s+key\\b|"
          "\\bprivate\\b|"
          "\\bprotected\\b|"
          "\\bpublic\\b|"
          "\\braise\\b|"
          "\\breferences\\b|"
          "\\b(?<!@)return\\b|"
          "\\bselect |"
          "\\bself\\b|"
          "\\bshift\\b|"
          "\\bshort\\b|"
          "\\bsizeof\\b|"
          "\\bstatic\\b|"
          "\\bstruct\\b|"
          "\\bswitch\\b|"
          "\\btable\\b|"
          "\\btemplate\\b|"
          "\\bthen\\b|"
          "\\bthis\\b|"
          "\\b(?<!@)throws?\\b|"
          "\\btrue\\b|"
          "\\btry\\b|"
          "\\btypedef |"
          "\\btypename |"
          "\\bunion\\b|"
          "\\bunsigned |"
          "\\bupdate |"
          "\\busing |"
          "\\bvar\\b|"
          "\\bview\\b|"
          "\\bvoid\\b|"
          "\\bvolatile\\b|"
          "\\bwhere |"
          "\\bwhile\\b|"
          "\\b[a-zA-Z][\\w]+_t\\b"
          ")", PCRE_CASELESS),
        false, view_colors::VCR_KEYWORD);
    hm["$srcfile"] = textview_curses::
                     highlighter(xpcre_compile(
                                     "[\\w\\-_]+\\."
                                     "(?:java|a|o|so|c|cc|cpp|cxx|h|hh|hpp|hxx|py|pyc|rb):"
                                     "\\d+"));
    hm["$xml"] = textview_curses::
                 highlighter(xpcre_compile("<(/?[^ >=]+)[^>]*>"));
    hm["$stringd"] = textview_curses::
                     highlighter(xpcre_compile("\"(?:\\\\.|[^\"])*\""),
                                 false, view_colors::VCR_STRING);
    hm["$strings"] = textview_curses::
                     highlighter(xpcre_compile(
                                     "(?<![A-WY-Za-qstv-z])\'(?:\\\\.|[^'])*\'"),
                     false, view_colors::VCR_STRING);
    hm["$stringb"] = textview_curses::
                     highlighter(xpcre_compile("`(?:\\\\.|[^`])*`"),
                                 false, view_colors::VCR_STRING);
    hm["$diffp"] = textview_curses::
                   highlighter(xpcre_compile(
                                   "^\\+.*"), false,
                               view_colors::VCR_DIFF_ADD);
    hm["$diffm"] = textview_curses::
                   highlighter(xpcre_compile(
                                   "^(?:--- .*|-$|-[^-].*)"), false,
                               view_colors::VCR_DIFF_DELETE);
    hm["$diffs"] = textview_curses::
                   highlighter(xpcre_compile(
                                   "^\\@@ .*"), false,
                               view_colors::VCR_DIFF_SECTION);
    hm["$ip"] = textview_curses::
                highlighter(xpcre_compile("\\d+\\.\\d+\\.\\d+\\.\\d+"));
    hm["$comment"] = textview_curses::highlighter(xpcre_compile(
        "(?<=[\\s;])//.*|/\\*.*\\*/|\\(\\*.*\\*\\)|^#.*|\\s+#.*|dnl.*"), false, view_colors::VCR_COMMENT);
    hm["$javadoc"] = textview_curses::highlighter(xpcre_compile(
        "@(?:author|deprecated|exception|file|param|return|see|since|throws|todo|version)"));
    hm["$var"] = textview_curses::highlighter(xpcre_compile(
        "(?:"
          "(?:var\\s+)?([\\-\\w]+)\\s*=|"
          "(?<!\\$)\\$(\\w+)|"
          "(?<!\\$)\\$\\((\\w+)\\)|"
          "(?<!\\$)\\$\\{(\\w+)\\}"
          ")"),
        false, view_colors::VCR_VARIABLE);
}

static void print_errors(vector<string> error_list)
{
    for (std::vector<std::string>::iterator iter = error_list.begin();
         iter != error_list.end();
         ++iter) {
        fprintf(stderr, "%s%s", iter->c_str(),
                (*iter)[iter->size() - 1] == '\n' ? "" : "\n");
    }
}

int main(int argc, char *argv[])
{
    std::vector<std::string> loader_errors;
    int lpc, c, retval = EXIT_SUCCESS;

    auto_ptr<piper_proc> stdin_reader;
    const char *         stdin_out = NULL;
    int                  stdin_out_fd = -1;

    (void)signal(SIGPIPE, SIG_IGN);
    setlocale(LC_NUMERIC, "");
    umask(077);

    lnav_data.ld_program_name = argv[0];
    lnav_data.ld_local_vars.push(map<string, string>());
    lnav_data.ld_path_stack.push(".");

    rl_readline_name = "lnav";

    ensure_dotlnav();

    log_install_handlers();
    sql_install_logger();

#ifdef HAVE_LIBCURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
#endif

    lnav_data.ld_debug_log_name = "/dev/null";
    while ((c = getopt(argc, argv, "hHarsCc:I:iuf:d:nqtw:VW")) != -1) {
        switch (c) {
        case 'h':
            usage();
            exit(retval);
            break;

        case 'H':
            lnav_data.ld_flags |= LNF_HELP;
            break;

        case 'C':
            lnav_data.ld_flags |= LNF_CHECK_CONFIG;
            break;

        case 'c':
            switch (optarg[0]) {
            case ':':
            case '/':
            case ';':
            case '|':
                break;
            default:
                fprintf(stderr, "error: command arguments should start with a "
                    "colon, semi-colon, or pipe-symbol to denote:\n");
                fprintf(stderr, "error: a built-in command, SQL query, "
                    "or a file path that contains commands to execute\n");
                usage();
                exit(EXIT_FAILURE);
                break;
            }
            lnav_data.ld_commands.push_back(optarg);
            break;

        case 'f':
            if (access(optarg, R_OK) != 0) {
                perror("invalid command file");
                exit(EXIT_FAILURE);
            }
            lnav_data.ld_commands.push_back("|" + string(optarg));
            break;

        case 'I':
            if (access(optarg, X_OK) != 0) {
                perror("invalid config path");
                exit(EXIT_FAILURE);
            }
            lnav_data.ld_config_paths.push_back(optarg);
            break;

        case 'i':
            lnav_data.ld_flags |= LNF_INSTALL;
            break;

        case 'u':
            lnav_data.ld_flags |= LNF_UPDATE_FORMATS;
            break;

        case 'd':
            lnav_data.ld_debug_log_name = optarg;
            lnav_log_level = LOG_LEVEL_TRACE;
            break;

        case 'a':
            lnav_data.ld_flags |= LNF__ALL;
            break;

        case 'n':
            lnav_data.ld_flags |= LNF_HEADLESS;
            break;

        case 'q':
            lnav_data.ld_flags |= LNF_QUIET;
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

        case 'W':
        {
            char b;
            read(STDIN_FILENO, &b, 1);
        }
            break;

        case 'V':
            printf("%s\n", VCS_PACKAGE_STRING);
            exit(0);
            break;

        default:
            retval = EXIT_FAILURE;
            break;
        }
    }

    argc -= optind;
    argv += optind;

    lnav_log_file = fopen(lnav_data.ld_debug_log_name, "a");
    log_info("lnav started");

    string formats_path = dotlnav_path("formats/");

    if (lnav_data.ld_flags & LNF_UPDATE_FORMATS) {
        static_root_mem<glob_t, globfree> gl;
        string git_formats = formats_path + "*/.git";
        bool found = false;

        if (glob(git_formats.c_str(), GLOB_NOCHECK, NULL, gl.inout()) == 0) {
            for (lpc = 0; lpc < gl->gl_pathc; lpc++) {
                char *git_dir = dirname(gl->gl_pathv[lpc]);
                char pull_cmd[1024];

                printf("Updating formats in %s\n", git_dir);
                snprintf(pull_cmd, sizeof(pull_cmd),
                         "cd %s && git pull",
                         git_dir);
                system(pull_cmd);
                found = true;
            }
        }

        if (!found) {
            printf("No formats from git repositories found, "
                           "use 'lnav -i extra' to install third-party foramts\n");
        }

        return EXIT_SUCCESS;
    }

    if (lnav_data.ld_flags & LNF_INSTALL) {
        string installed_path = dotlnav_path("formats/installed/");

        if (argc == 0) {
            fprintf(stderr, "error: expecting file format paths\n");
            return EXIT_FAILURE;
        }

        for (lpc = 0; lpc < argc; lpc++) {
            if (endswith(argv[lpc], ".git")) {
                install_git_format(argv[lpc]);
                continue;
            }

            if (strcmp(argv[lpc], "extra") == 0) {
                install_extra_formats();
                continue;
            }

            vector<intern_string_t> format_list = load_format_file(argv[lpc], loader_errors);

            if (!loader_errors.empty()) {
                print_errors(loader_errors);
                return EXIT_FAILURE;
            }
            if (format_list.empty()) {
                fprintf(stderr, "error: format file is empty: %s\n", argv[lpc]);
                return EXIT_FAILURE;
            }

            string dst_name = format_list[0].to_string() + ".json";
            string dst_path = installed_path + dst_name;
            auto_fd in_fd, out_fd;

            if ((in_fd = open(argv[lpc], O_RDONLY)) == -1) {
                perror("unable to open file to install");
            }
            else if ((out_fd = open(dst_path.c_str(),
                    O_WRONLY | O_CREAT, 0644)) == -1) {
                fprintf(stderr, "error: unable to open destination: %s -- %s\n",
                        dst_path.c_str(), strerror(errno));
            }
            else {
                char buffer[2048];
                ssize_t rc;

                while ((rc = read(in_fd, buffer, sizeof(buffer))) > 0) {
                    write(out_fd, buffer, rc);
                }

                fprintf(stderr, "info: installed: %s\n", dst_path.c_str());
            }
        }
        return EXIT_SUCCESS;
    }

    if (sqlite3_open(":memory:", lnav_data.ld_db.out()) != SQLITE_OK) {
        fprintf(stderr, "error: unable to create sqlite memory database\n");
        exit(EXIT_FAILURE);
    }

    load_formats(lnav_data.ld_config_paths, loader_errors);

    /* If we statically linked against an ncurses library that had a non-
     * standard path to the terminfo database, we need to set this variable
     * so that it will try the default path.
     */
    setenv("TERMINFO_DIRS",
           "/usr/share/terminfo:/lib/terminfo:/usr/share/lib/terminfo",
           0);

    {
        int register_collation_functions(sqlite3 * db);

        register_sqlite_funcs(lnav_data.ld_db.in(), sqlite_registration_funcs);
        register_collation_functions(lnav_data.ld_db.in());
    }

    register_environ_vtab(lnav_data.ld_db.in());
    register_views_vtab(lnav_data.ld_db.in());

    lnav_data.ld_vtab_manager =
        new log_vtab_manager(lnav_data.ld_db,
                             lnav_data.ld_views[LNV_LOG],
                             lnav_data.ld_log_source);

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

    lnav_data.ld_vtab_manager->register_vtab(new all_logs_vtab());
    lnav_data.ld_vtab_manager->register_vtab(new log_format_vtab_impl(
            *log_format::find_root_format("generic_log")));

    for (std::vector<log_format *>::iterator iter = log_format::get_root_formats().begin();
         iter != log_format::get_root_formats().end();
         ++iter) {
        log_vtab_impl *lvi = (*iter)->get_vtab_impl();

        if (lvi != NULL) {
            lnav_data.ld_vtab_manager->register_vtab(lvi);
        }
    }

    load_format_extra(lnav_data.ld_db.in(), lnav_data.ld_config_paths, loader_errors);
    if (!loader_errors.empty()) {
        print_errors(loader_errors);
        return EXIT_FAILURE;
    }

    if (!(lnav_data.ld_flags & LNF_CHECK_CONFIG)) {
        DEFAULT_FILES.insert(make_pair(LNF_SYSLOG, string("var/log/messages")));
        DEFAULT_FILES.insert(
                make_pair(LNF_SYSLOG, string("var/log/system.log")));
        DEFAULT_FILES.insert(make_pair(LNF_SYSLOG, string("var/log/syslog")));
        DEFAULT_FILES.insert(
                make_pair(LNF_SYSLOG, string("var/log/syslog.log")));
    }

    init_lnav_commands(lnav_commands);

    lnav_data.ld_views[LNV_HELP].
    set_sub_source(new plain_text_source(help_txt));
    lnav_data.ld_views[LNV_HELP].set_word_wrap(true);
    lnav_data.ld_views[LNV_LOG].
    set_sub_source(&lnav_data.ld_log_source);
    lnav_data.ld_views[LNV_LOG].
    set_delegate(new action_delegate(lnav_data.ld_log_source));
    lnav_data.ld_views[LNV_TEXT].
    set_sub_source(&lnav_data.ld_text_source);
    lnav_data.ld_views[LNV_HISTOGRAM].
    set_sub_source(&lnav_data.ld_hist_source2);
    lnav_data.ld_views[LNV_GRAPH].
    set_sub_source(&lnav_data.ld_graph_source);
    lnav_data.ld_views[LNV_DB].
    set_sub_source(&lnav_data.ld_db_row_source);
    lnav_data.ld_db_overlay.dos_labels = &lnav_data.ld_db_row_source;
    lnav_data.ld_views[LNV_DB].
    set_overlay_source(&lnav_data.ld_db_overlay);
    lnav_data.ld_views[LNV_LOG].
    set_overlay_source(new field_overlay_source(lnav_data.ld_log_source));
    lnav_data.ld_match_view.set_left(0);

    for (lpc = 0; lpc < LNV__MAX; lpc++) {
        lnav_data.ld_views[lpc].set_gutter_source(new log_gutter_source());
    }

    {
        setup_highlights(lnav_data.ld_views[LNV_LOG].get_highlights());
        setup_highlights(lnav_data.ld_views[LNV_TEXT].get_highlights());
        setup_highlights(lnav_data.ld_views[LNV_SCHEMA].get_highlights());
        setup_highlights(lnav_data.ld_views[LNV_PRETTY].get_highlights());
    }

    {
        hist_source2 &hs = lnav_data.ld_hist_source2;

        lnav_data.ld_log_source.set_index_delegate(
                new hist_index_delegate(lnav_data.ld_hist_source2,
                        lnav_data.ld_views[LNV_HISTOGRAM]));
        hs.init();
        lnav_data.ld_hist_zoom = 2;
        hs.set_time_slice(HIST_ZOOM_VALUES[lnav_data.ld_hist_zoom].hl_time_slice);
    }

    {
        hist_source &hs = lnav_data.ld_graph_source;

        hs.set_bucket_size(1);
        hs.set_group_size(100);
    }

    for (int lpc = 0; lpc < LNV__MAX; lpc++) {
        lnav_data.ld_views[lpc].set_title(view_titles[lpc]);
    }

    lnav_data.ld_looping        = true;
    lnav_data.ld_mode           = LNM_PAGING;

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

        if (startswith(argv[lpc], "pt:")) {
#ifdef HAVE_LIBCURL
            lnav_data.ld_pt_search = argv[lpc];
#else
            fprintf(stderr, "error: lnav is not compiled with libcurl\n");
            retval = EXIT_FAILURE;
#endif
        }
#ifdef HAVE_LIBCURL
        else if (is_url(argv[lpc])) {
            auto_ptr<url_loader> ul(new url_loader(argv[lpc]));

            lnav_data.ld_file_names.insert(make_pair(argv[lpc], ul->copy_fd().release()));
            lnav_data.ld_curl_looper.add_request(ul.release());
        }
#endif
        else if (is_glob(argv[lpc])) {
            lnav_data.ld_file_names.insert(make_pair(argv[lpc], -1));
        }
        else if (stat(argv[lpc], &st) == -1) {
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

    if (lnav_data.ld_flags & LNF_CHECK_CONFIG) {
        rescan_files(true);
        for (list<logfile *>::iterator file_iter = lnav_data.ld_files.begin();
                file_iter != lnav_data.ld_files.end();
                ++file_iter) {
            logfile *lf = (*file_iter);

            lf->rebuild_index();

            lf->rebuild_index();
            log_format *fmt = lf->get_format();
            if (fmt == NULL) {
                fprintf(stderr, "error:%s:no format found for file\n",
                        lf->get_filename().c_str());
                retval = EXIT_FAILURE;
                continue;
            }
            for (logfile::iterator line_iter = lf->begin();
                    line_iter != lf->end();
                    ++line_iter) {
                if (!line_iter->is_continued()) {
                    continue;
                }

                shared_buffer_ref sbr;
                size_t partial_len;

                lf->read_line(line_iter, sbr);
                if (fmt->scan_for_partial(sbr, partial_len)) {
                    long line_number = distance(lf->begin(), line_iter);
                    string full_line(sbr.get_data(), sbr.length());
                    string partial_line(sbr.get_data(), partial_len);

                    fprintf(stderr,
                            "error:%s:%ld:line did not match format %s\n",
                            lf->get_filename().c_str(), line_number,
                            fmt->get_pattern_name().c_str());
                    fprintf(stderr,
                            "error:%s:%ld:         line -- %s\n",
                            lf->get_filename().c_str(), line_number,
                            full_line.c_str());
                    if (partial_len > 0) {
                        fprintf(stderr,
                                "error:%s:%ld:partial match -- %s\n",
                                lf->get_filename().c_str(), line_number,
                                partial_line.c_str());
                    }
                    else {
                        fprintf(stderr,
                                "error:%s:%ld:no partial match found\n",
                                lf->get_filename().c_str(), line_number);
                    }
                    retval = EXIT_FAILURE;
                }
            }
        }
        return retval;
    }

    if (!(lnav_data.ld_flags & (LNF_HEADLESS|LNF_CHECK_CONFIG)) && !isatty(STDOUT_FILENO)) {
        fprintf(stderr, "error: stdout is not a tty.\n");
        retval = EXIT_FAILURE;
    }

    if (!isatty(STDIN_FILENO)) {
        stdin_reader =
            auto_ptr<piper_proc>(new piper_proc(STDIN_FILENO,
                                                lnav_data.ld_flags &
                                                LNF_TIMESTAMP, stdin_out));
        stdin_out_fd = stdin_reader->get_fd();
        lnav_data.ld_file_names.insert(make_pair("stdin", stdin_out_fd));
        if (dup2(STDOUT_FILENO, STDIN_FILENO) == -1) {
            perror("cannot dup stdout to stdin");
        }
        lnav_data.ld_pipers.push_back(stdin_reader.release());
    }

    if (lnav_data.ld_file_names.empty() &&
        lnav_data.ld_pt_search.empty() &&
        !(lnav_data.ld_flags & LNF_HELP)) {
        fprintf(stderr, "error: no log files given/found.\n");
        retval = EXIT_FAILURE;
    }

    if (retval != EXIT_SUCCESS) {
        usage();
    }
    else {
        try {
            rescan_files(true);

            log_info("startup: %s", VCS_PACKAGE_STRING);
            log_host_info();
            log_info("Libraries:");
#ifdef HAVE_BZLIB_H
            log_info("  bzip=%s", BZ2_bzlibVersion());
#endif
#ifdef HAVE_LIBCURL
            log_info("  curl=%s (%s)", LIBCURL_VERSION, LIBCURL_TIMESTAMP);
#endif
            log_info("  ncurses=%s", NCURSES_VERSION);
            log_info("  pcre=%s", pcre_version());
            log_info("  readline=%s", rl_library_version);
            log_info("  sqlite=%s", sqlite3_version);
            log_info("  zlib=%s", zlibVersion());
            log_info("lnav_data:");
            log_info("  flags=%x", lnav_data.ld_flags);
            log_info("  commands:");
            for (std::list<string>::iterator cmd_iter =
                 lnav_data.ld_commands.begin();
                 cmd_iter != lnav_data.ld_commands.end();
                 ++cmd_iter) {
                log_info("    %s", cmd_iter->c_str());
            }
            log_info("  files:");
            for (std::set<pair<string, int> >::iterator file_iter =
                 lnav_data.ld_file_names.begin();
                 file_iter != lnav_data.ld_file_names.end();
                 ++file_iter) {
                log_info("    %s", file_iter->first.c_str());
            }

            if (lnav_data.ld_flags & LNF_HEADLESS) {
                std::vector<pair<string, string> > msgs;
                std::vector<pair<string, string> >::iterator msg_iter;
                textview_curses *log_tc, *text_tc, *tc;
                attr_line_t al;
                const std::string &line = al.get_string();
                bool found_error = false;

                alerter::singleton().enabled(false);

                log_tc = &lnav_data.ld_views[LNV_LOG];
                log_tc->set_height(vis_line_t(24));
                lnav_data.ld_view_stack.push(log_tc);
                // Read all of stdin
                wait_for_pipers();
                rebuild_indexes(true);

                log_tc->set_top(vis_line_t(0));
                text_tc = &lnav_data.ld_views[LNV_TEXT];
                text_tc->set_top(vis_line_t(0));
                text_tc->set_height(vis_line_t(text_tc->get_inner_height()));
                if (lnav_data.ld_log_source.text_line_count() == 0 &&
                    lnav_data.ld_text_source.text_line_count() > 0) {
                    toggle_view(&lnav_data.ld_views[LNV_TEXT]);
                }

                log_info("Executing initial commands");
                execute_init_commands(msgs);
                wait_for_pipers();
                lnav_data.ld_curl_looper.process_all();
                rebuild_indexes(false);

                for (msg_iter = msgs.begin();
                     msg_iter != msgs.end();
                     ++msg_iter) {
                    if (strncmp("error:", msg_iter->first.c_str(), 6) != 0) {
                        continue;
                    }

                    fprintf(stderr, "%s\n", msg_iter->first.c_str());
                    found_error = true;
                }

                if (!found_error &&
                    !(lnav_data.ld_flags & LNF_QUIET) &&
                    !lnav_data.ld_view_stack.empty() &&
                    !lnav_data.ld_stdout_used) {
                    bool suppress_empty_lines = false;
                    list_overlay_source *los;
                    unsigned long view_index;
                    vis_line_t y;

                    tc = lnav_data.ld_view_stack.top();
                    view_index = tc - lnav_data.ld_views;
                    switch (view_index) {
                        case LNV_DB:
                        case LNV_HISTOGRAM:
                            suppress_empty_lines = true;
                            break;
                        default:
                            break;
                    }

                    los = tc->get_overlay_source();

                    for (vis_line_t vl = tc->get_top();
                         vl < tc->get_inner_height();
                         ++vl, ++y) {
                        while (los != NULL &&
                               los->list_value_for_overlay(*tc, y, al)) {
                            write(STDOUT_FILENO, line.c_str(), line.length());
                            write(STDOUT_FILENO, "\n", 1);
                            ++y;
                        }

                        tc->listview_value_for_row(*tc, vl, al);
                        if (suppress_empty_lines && line.empty()) {
                            continue;
                        }

                        struct line_range lr = find_string_attr_range(
                                al.get_attrs(), &textview_curses::SA_ORIGINAL_LINE);
                        write(STDOUT_FILENO, lr.substr(al.get_string()),
                              lr.sublen(al.get_string()));
                        write(STDOUT_FILENO, "\n", 1);
                    }
                }
            }
            else {
                lnav_data.ld_curl_looper.start();

                init_session();

                log_info("  session_id=%s", lnav_data.ld_session_id.c_str());

                scan_sessions();

                guard_termios gt(STDIN_FILENO);

                lnav_log_orig_termios = gt.get_termios();

                looper();

                save_session();
            }
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

        // When reading from stdin, dump out the last couple hundred lines so
        // the user can have the text in their terminal history.
        if (stdin_out_fd != -1 &&
                !(lnav_data.ld_flags & LNF_QUIET) &&
                !(lnav_data.ld_flags & LNF_HEADLESS)) {
            list<logfile *>::iterator file_iter;
            struct stat st;

            fstat(stdin_out_fd, &st);
            file_iter = find_if(lnav_data.ld_files.begin(),
                                lnav_data.ld_files.end(),
                                same_file(st));
            if (file_iter != lnav_data.ld_files.end()) {
                logfile::iterator line_iter;
                logfile *lf = *file_iter;
                string str;

                for (line_iter = lf->begin();
                     line_iter != lf->end();
                     ++line_iter) {
                    lf->read_line(line_iter, str);

                    write(STDOUT_FILENO, str.c_str(), str.size());
                    write(STDOUT_FILENO, "\n", 1);
                }
            }
        }
    }

    lnav_data.ld_curl_looper.stop();

    return retval;
}
