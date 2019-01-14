/**
 * Copyright (c) 2007-2016, Timothy Stack
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

#if defined(__OpenBSD__) && defined(__clang__) && \
    !defined(_WCHAR_H_CPLUSPLUS_98_CONFORMANCE_)
#define _WCHAR_H_CPLUSPLUS_98_CONFORMANCE_
#endif
#include <map>
#include <memory>
#include <set>
#include <stack>
#include <vector>
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
#include "log_gutter_source.hh"
#include "session_data.hh"
#include "lnav_config.hh"
#include "sql_util.hh"
#include "sqlite-extension-func.hh"
#include "sysclip.hh"
#include "term_extra.hh"
#include "log_data_helper.hh"
#include "readline_highlighters.hh"
#include "environ_vtab.hh"
#include "views_vtab.hh"
#include "pretty_printer.hh"
#include "all_logs_vtab.hh"
#include "file_vtab.hh"
#include "regexp_vtab.hh"
#include "fstat_vtab.hh"
#include "textfile_highlighters.hh"

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
#include "shlex.hh"
#include "log_actions.hh"

using namespace std;

static multimap<lnav_flags_t, string> DEFAULT_FILES;

struct _lnav_data lnav_data;

const int ZOOM_LEVELS[] = {
    1,
    30,
    60,
    5 * 60,
    15 * 60,
    60 * 60,
    4 * 60 * 60,
    8 * 60 * 60,
    24 * 60 * 60,
    7 * 24 * 60 * 60,
};

const ssize_t ZOOM_COUNT = sizeof(ZOOM_LEVELS) / sizeof(int);

const char *lnav_view_strings[LNV__MAX + 1] = {
    "log",
    "text",
    "help",
    "histogram",
    "db",
    "schema",
    "pretty",
    "spectro",

    nullptr
};

const char *lnav_zoom_strings[] = {
    "1-second",
    "30-second",
    "1-minute",
    "5-minute",
    "15-minute",
    "1-hour",
    "4-hour",
    "8-hour",
    "1-day",
    "1-week",

    nullptr
};

static const char *view_titles[LNV__MAX] = {
    "LOG",
    "TEXT",
    "HELP",
    "HIST",
    "DB",
    "SCHEMA",
    "PRETTY",
    "SPECTRO",
};

static std::vector<std::string> DEFAULT_DB_KEY_NAMES = {
    "match_index",
    "capture_index",
    "capture_count",
    "range_start",
    "range_stop",
    "inode",
    "device",
    "inode",
    "rowid",
    "st_dev",
    "st_ino",
    "st_mode",
    "st_rdev",
    "st_uid",
    "st_gid",
};

static void regenerate_unique_file_names()
{
    unique_path_generator upg;

    for (auto lf : lnav_data.ld_files) {
        upg.add_source(shared_ptr<logfile>(lf));
    }

    upg.generate();
}

bool setup_logline_table(exec_context &ec)
{
    // Hidden columns don't show up in the table_info pragma.
    static const char *hidden_table_columns[] = {
        "log_path",
        "log_text",
        "log_body",

        nullptr
    };

    static const char *commands[] = {
        ".schema",
        ".msgformats",

        nullptr
    };

    textview_curses &log_view = lnav_data.ld_views[LNV_LOG];
    bool             retval   = false;
    bool update_possibilities = (
        lnav_data.ld_rl_view != NULL &&
        ec.ec_local_vars.size() == 1);

    if (update_possibilities) {
        lnav_data.ld_rl_view->clear_possibilities(LNM_SQL, "*");
        add_view_text_possibilities(lnav_data.ld_rl_view, LNM_SQL, "*", &log_view);
    }

    if (log_view.get_inner_height()) {
        static intern_string_t logline = intern_string::lookup("logline");
        vis_line_t     vl = log_view.get_top();
        content_line_t cl = lnav_data.ld_log_source.at_base(vl);

        lnav_data.ld_vtab_manager->unregister_vtab(logline);
        lnav_data.ld_vtab_manager->register_vtab(
            new log_data_table(lnav_data.ld_log_source,
                               *lnav_data.ld_vtab_manager,
                               cl,
                               logline));

        if (update_possibilities) {
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

    auto &db_key_names = lnav_data.ld_db_key_names;

    db_key_names = DEFAULT_DB_KEY_NAMES;

    if (update_possibilities) {
        add_env_possibilities(LNM_SQL);

        lnav_data.ld_rl_view->add_possibility(LNM_SQL, "*",
                                              std::begin(sql_keywords),
                                              std::end(sql_keywords));
        lnav_data.ld_rl_view->add_possibility(LNM_SQL, "*", sql_function_names);
        lnav_data.ld_rl_view->add_possibility(LNM_SQL, "*",
            hidden_table_columns);
        lnav_data.ld_rl_view->add_possibility(LNM_SQL, "*", commands);

        for (int lpc = 0; sqlite_registration_funcs[lpc]; lpc++) {
            struct FuncDef *basic_funcs;
            struct FuncDefAgg *agg_funcs;

            sqlite_registration_funcs[lpc](&basic_funcs, &agg_funcs);
            for (int lpc2 = 0; basic_funcs && basic_funcs[lpc2].zName; lpc2++) {
                const FuncDef &func_def = basic_funcs[lpc2];

                lnav_data.ld_rl_view->add_possibility(
                    LNM_SQL,
                    "*",
                    string(func_def.zName) + (func_def.nArg ? "(" : "()"));
            }
            for (int lpc2 = 0; agg_funcs && agg_funcs[lpc2].zName; lpc2++) {
                const FuncDefAgg &func_def = agg_funcs[lpc2];

                lnav_data.ld_rl_view->add_possibility(
                    LNM_SQL,
                    "*",
                    string(func_def.zName) + (func_def.nArg ? "(" : "()"));
            }
        }

        for (const auto &pair : sqlite_function_help) {
            lnav_data.ld_rl_view->add_possibility(
                LNM_SQL,
                "*",
                pair.first + (pair.second->ht_parameters.empty() ? "()" : ("(")));
        }
    }

    walk_sqlite_metadata(lnav_data.ld_db.in(), lnav_sql_meta_callbacks);

    for (const auto &iter : *lnav_data.ld_vtab_manager) {
        iter.second->get_foreign_keys(db_key_names);
    }

    stable_sort(db_key_names.begin(), db_key_names.end());

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
    void do_update()
    {
        lnav_data.ld_top_source.update_time();
        for (auto &sc : lnav_data.ld_status) {
            sc.do_update();
        }
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
        if (ll->is_continued() || ll->get_time() == 0) {
            return;
        }

        hist_source2::hist_type_t ht;

        switch (ll->get_msg_level()) {
            case LEVEL_FATAL:
            case LEVEL_CRITICAL:
            case LEVEL_ERROR:
                ht = hist_source2::HT_ERROR;
                break;
            case LEVEL_WARNING:
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

void rebuild_hist()
{
    logfile_sub_source &lss = lnav_data.ld_log_source;
    hist_source2 &hs = lnav_data.ld_hist_source2;
    int zoom = lnav_data.ld_zoom_level;

    hs.set_time_slice(ZOOM_LEVELS[zoom]);
    lss.reload_index_delegate();
}

class textfile_callback {
public:
    textfile_callback() : front_file(nullptr), front_top(-1) { };

    void closed_file(const shared_ptr<logfile> &lf) {
        log_info("closed text file: %s", lf->get_filename().c_str());
        if (!lf->is_valid_filename()) {
            lnav_data.ld_file_names.erase(lf->get_filename());
        }
        auto file_iter = find(lnav_data.ld_files.begin(),
                              lnav_data.ld_files.end(),
                              lf);
        lnav_data.ld_files.erase(file_iter);

        regenerate_unique_file_names();
    };

    void promote_file(const shared_ptr<logfile> &lf) {
        if (lnav_data.ld_log_source.insert_file(lf)) {
            log_info("promoting text file to log file: %s",
                     lf->get_filename().c_str());
            log_format *format = lf->get_format();
            if (format->lf_is_self_describing) {
                log_vtab_impl *vt = format->get_vtab_impl();

                if (vt) {
                    lnav_data.ld_vtab_manager->register_vtab(vt);
                }
            }
        }
        else {
            this->closed_file(lf);
        }
    };

    void scanned_file(const shared_ptr<logfile> &lf) {
        if (!lnav_data.ld_files_to_front.empty() &&
                lnav_data.ld_files_to_front.front().first ==
                        lf->get_filename()) {
            this->front_file = lf;
            this->front_top = lnav_data.ld_files_to_front.front().second;

            lnav_data.ld_files_to_front.pop_front();
        }
    };

    shared_ptr<logfile> front_file;
    int front_top;
};

void rebuild_indexes()
{
    logfile_sub_source &lss = lnav_data.ld_log_source;
    textview_curses &log_view  = lnav_data.ld_views[LNV_LOG];
    textview_curses &text_view = lnav_data.ld_views[LNV_TEXT];
    vis_line_t old_bottoms[LNV__MAX];

    bool scroll_downs[LNV__MAX];

    for (int lpc = 0; lpc < LNV__MAX; lpc++) {
        old_bottoms[lpc] = lnav_data.ld_views[lpc].get_top_for_last_row();
        scroll_downs[lpc] =
            (lnav_data.ld_views[lpc].get_top() >= old_bottoms[lpc]) &&
            !(lnav_data.ld_flags & LNF_HEADLESS);
    }

    {
        textfile_sub_source *tss = &lnav_data.ld_text_source;
        textfile_callback cb;

        tss->rescan_files(cb);

        if (cb.front_file != nullptr) {
            ensure_view(&text_view);

            if (tss->current_file() != cb.front_file) {
                tss->to_front(cb.front_file);
                old_bottoms[LNV_TEXT] = vis_line_t(-1);
            }

            if (cb.front_top < 0) {
                cb.front_top += text_view.get_inner_height();
            }
            if (cb.front_top < text_view.get_inner_height()) {
                text_view.set_top(vis_line_t(cb.front_top));
                scroll_downs[LNV_TEXT] = false;
            }
        }
    }

    for (auto file_iter = lnav_data.ld_files.begin();
         file_iter != lnav_data.ld_files.end(); ) {
        auto lf = *file_iter;

        if (!lf->exists() || lf->is_closed()) {
            log_info("closed log file: %s", lf->get_filename().c_str());
            if (!lf->is_valid_filename()) {
                lnav_data.ld_file_names.erase(lf->get_filename());
            }
            lnav_data.ld_text_source.remove(lf);
            lnav_data.ld_log_source.remove_file(lf);
            file_iter = lnav_data.ld_files.erase(file_iter);

            regenerate_unique_file_names();
        }
        else {
            ++file_iter;
        }
    }

    logfile_sub_source::rebuild_result result = lss.rebuild_index();
    if (result != logfile_sub_source::rebuild_result::rr_no_change) {
        size_t new_count = lss.text_line_count();
        bool force = result == logfile_sub_source::rebuild_result::rr_full_rebuild;

        if ((!scroll_downs[LNV_LOG] || log_view.get_top() > new_count) && force) {
            scroll_downs[LNV_LOG] = false;
        }

        log_view.reload_data();
    }

    for (int lpc = 0; lpc < LNV__MAX; lpc++) {
        textview_curses &scroll_view = lnav_data.ld_views[lpc];

        if (scroll_downs[lpc] && scroll_view.get_top_for_last_row() > scroll_view.get_top()) {
            scroll_view.set_top(scroll_view.get_top_for_last_row());
        }
    }

    lnav_data.ld_view_stack.top() | [] (auto tc) {
        lnav_data.ld_filter_status_source.update_filtered(tc->get_sub_source());
        lnav_data.ld_scroll_broadcaster.invoke(tc);
    };
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
                    logfile_open_options default_loo;

                    lnav_data.ld_file_names[abspath.in()] = default_loo;
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

vis_line_t next_cluster(
    vis_line_t(bookmark_vector<vis_line_t>::*f) (vis_line_t) const,
    bookmark_type_t *bt,
    const vis_line_t top)
{
    textview_curses *tc = *(lnav_data.ld_view_stack.top());
    vis_bookmarks &bm = tc->get_bookmarks();
    bookmark_vector<vis_line_t> &bv = bm[bt];
    bool top_is_marked = binary_search(bv.begin(), bv.end(), top);
    vis_line_t last_top(top), new_top(top), tc_height;
    unsigned long tc_width;
    int hit_count = 0;

    tc->get_dimensions(tc_height, tc_width);

    while ((new_top = (bv.*f)(new_top)) != -1) {
        int diff = new_top - last_top;

        hit_count += 1;
        if (!top_is_marked || diff > 1) {
            return new_top;
        }
        else if (hit_count > 1 && std::abs(new_top - top) >= tc_height) {
            return vis_line_t(new_top - diff);
        }
        else if (diff < -1) {
            last_top = new_top;
            while ((new_top = (bv.*f)(new_top)) != -1) {
                if ((std::abs(last_top - new_top) > 1) ||
                    (hit_count > 1 && (std::abs(top - new_top) >= tc_height))) {
                    break;
                }
                last_top = new_top;
            }
            return last_top;
        }
        last_top = new_top;
    }

    if (last_top != top) {
        return last_top;
    }

    return vis_line_t(-1);
}

bool moveto_cluster(vis_line_t(bookmark_vector<vis_line_t>::*f) (vis_line_t) const,
                    bookmark_type_t *bt,
                    vis_line_t top)
{
    textview_curses *tc = *(lnav_data.ld_view_stack.top());
    vis_line_t new_top;

    new_top = next_cluster(f, bt, top);
    if (new_top == -1) {
        new_top = next_cluster(f, bt, tc->get_top());
    }
    if (new_top != -1) {
        tc->get_sub_source()->get_location_history() | [new_top] (auto lh) {
            lh->loc_history_append(new_top);
        };

        tc->set_top(new_top);
        return true;
    }

    alerter::singleton().chime();

    return false;
}

void previous_cluster(bookmark_type_t *bt, textview_curses *tc)
{
    key_repeat_history &krh = lnav_data.ld_key_repeat_history;
    vis_line_t height, new_top, initial_top = tc->get_top();
    unsigned long width;

    new_top = next_cluster(&bookmark_vector<vis_line_t>::prev,
                           bt,
                           initial_top);

    tc->get_dimensions(height, width);
    if (krh.krh_count > 1 &&
        initial_top < (krh.krh_start_line - (1.5 * height)) &&
        (initial_top - new_top) < height) {
        bookmark_vector<vis_line_t> &bv = tc->get_bookmarks()[bt];
        new_top = bv.next(std::max(vis_line_t(0), initial_top - height));
    }

    if (new_top != -1) {
        tc->get_sub_source()->get_location_history() | [new_top] (auto lh) {
            lh->loc_history_append(new_top);
        };

        tc->set_top(new_top);
    }
    else {
        alerter::singleton().chime();
    }
}

vis_line_t search_forward_from(textview_curses *tc)
{
    vis_line_t height, retval = tc->get_top();
    key_repeat_history &krh = lnav_data.ld_key_repeat_history;
    unsigned long width;

    tc->get_dimensions(height, width);

    if (krh.krh_count > 1 &&
        retval > (krh.krh_start_line + (1.5 * height))) {
        retval += vis_line_t(0.90 * height);
    }

    return retval;
}

static void handle_rl_key(int ch)
{
    switch (ch) {
        case KEY_PPAGE:
        case KEY_NPAGE:
        case KEY_CTRL_P:
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

static void usage()
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

static void clear_last_user_mark(void *, listview_curses *lv)
{
    textview_curses *tc = (textview_curses *) lv;
    if (lnav_data.ld_select_start.find(tc) != lnav_data.ld_select_start.end() &&
        !tc->is_line_visible(vis_line_t(lnav_data.ld_last_user_mark[tc]))) {
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
    bool operator()(const shared_ptr<logfile> &lf) const
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
static bool watch_logfile(string filename, logfile_open_options &loo, bool required)
{
    static loading_observer obs;
    struct stat st;
    int         rc;
    bool retval = false;

    if (lnav_data.ld_closed_files.count(filename)) {
        return retval;
    }

    if (loo.loo_fd != -1) {
        rc = fstat(loo.loo_fd, &st);
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
                return retval;
            }
        }
    }
    if (rc == -1) {
        if (required) {
            throw logfile::error(filename, errno);
        }
        else{
            return retval;
        }
    }

    auto file_iter = find_if(lnav_data.ld_files.begin(),
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
                retval = true;
                break;

            default:
                /* It's a new file, load it in. */
                shared_ptr<logfile> lf = make_shared<logfile>(filename, loo);

                log_info("loading new file: filename=%s",
                         filename.c_str());
                lf->set_logfile_observer(&obs);
                lnav_data.ld_files.push_back(lf);
                lnav_data.ld_text_source.push_back(lf);

                regenerate_unique_file_names();

                retval = true;
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

    return retval;
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

            if ((abspath = realpath(gl->gl_pathv[lpc], nullptr)) == NULL) {
                if (required) {
                    fprintf(stderr, "Cannot find file: %s -- %s",
                        gl->gl_pathv[lpc], strerror(errno));
                }
            }
            else if (required || access(abspath.in(), R_OK) == 0) {
                logfile_open_options loo;

                watch_logfile(abspath.in(), loo, required);
            }
        }
    }
}

bool rescan_files(bool required)
{
    map<string, logfile_open_options>::iterator iter;
    bool retval = false;

    for (iter = lnav_data.ld_file_names.begin();
         iter != lnav_data.ld_file_names.end();
         iter++) {
        if (iter->second.loo_fd == -1) {
            expand_filename(iter->first, required);
            if (lnav_data.ld_flags & LNF_ROTATED) {
                string path = iter->first + ".*";

                expand_filename(path, false);
            }
        }
        else {
            retval = retval || watch_logfile(iter->first, iter->second, required);
        }
    }

    for (auto file_iter = lnav_data.ld_files.begin();
         file_iter != lnav_data.ld_files.end(); ) {
        auto lf = *file_iter;

        if (!lf->exists() || lf->is_closed()) {
            log_info("Log file no longer exists or is closed: %s",
                     lf->get_filename().c_str());
            return true;
        }
        else {
            ++file_iter;
        }
    }

    return retval;
}

class lnav_behavior : public mouse_behavior {
public:
    lnav_behavior() {};

    int scroll_polarity(int button)
    {
        return button == xterm_mouse::XT_SCROLL_UP ? -1 : 1;
    };

    void mouse_event(int button, bool release, int x, int y)
    {
        textview_curses *tc = *(lnav_data.ld_view_stack.top());
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

        gettimeofday(&me.me_time, nullptr);
        me.me_x = x - 1;
        me.me_y = y - tc->get_y() - 1;

        tc->handle_mouse(me);
    };

private:
};

static void handle_key(int ch) {
    lnav_data.ld_input_state.push_back(ch);

    if (lnav_data.ld_mode == LNM_PAGING) {
        auto top_tc = lnav_data.ld_view_stack.top();

    }

    switch (ch) {
        case CTRL('d'):
        case KEY_RESIZE:
            break;
        default: {
            switch (lnav_data.ld_mode) {
                case LNM_PAGING:
                    handle_paging_key(ch);
                    break;

                case LNM_FILTER:
                    if (!lnav_data.ld_filter_view.handle_key(ch)) {
                        handle_paging_key(ch);
                    }
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
}

static input_dispatcher::escape_match_t match_escape_seq(const char *escape_buffer)
{
    if (lnav_data.ld_mode != LNM_PAGING) {
        return input_dispatcher::escape_match_t::NONE;
    }

    char keyseq[32] = "";

    for (size_t lpc = 0; escape_buffer[lpc]; lpc++) {
        snprintf(keyseq + strlen(keyseq), sizeof(keyseq) - strlen(keyseq),
                 "x%02x",
                 escape_buffer[lpc]);
    }

    key_map &km = lnav_config.lc_ui_keymaps[lnav_config.lc_ui_keymap];
    auto iter = km.km_seq_to_cmd.find(keyseq);
    if (iter != km.km_seq_to_cmd.end()) {
        return input_dispatcher::escape_match_t::FULL;
    }

    auto lb = km.km_seq_to_cmd.lower_bound(keyseq);

    if (lb == km.km_seq_to_cmd.end()) {
        return input_dispatcher::escape_match_t::NONE;
    }

    auto ub = km.km_seq_to_cmd.upper_bound(keyseq);
    auto longest = max_element(lb, ub, [] (auto l, auto r) {
        return l.first.size() < r.first.size();
    });

    if (strlen(escape_buffer) < longest->first.size()) {
        return input_dispatcher::escape_match_t::PARTIAL;
    }

    return input_dispatcher::escape_match_t::NONE;
}

static void handle_escape_seq(const char *escape_buffer)
{
    char keyseq[32] = "";

    for (size_t lpc = 0; escape_buffer[lpc]; lpc++) {
        snprintf(keyseq + strlen(keyseq), sizeof(keyseq) - strlen(keyseq),
                 "x%02x",
                 escape_buffer[lpc]);
    }

    handle_keyseq(keyseq);
}

void update_hits(void *dummy, textview_curses *tc)
{
    auto top_tc = lnav_data.ld_view_stack.top();

    if (top_tc && tc == *top_tc) {
        lnav_data.ld_bottom_source.update_hits(tc);

        if (lnav_data.ld_mode == LNM_SEARCH) {
            const int MAX_MATCH_COUNT = 10;
            const vis_line_t PREVIEW_SIZE = vis_line_t(MAX_MATCH_COUNT + 1);

            int preview_count = 0;

            vis_bookmarks &bm = tc->get_bookmarks();
            const auto &bv = bm[&textview_curses::BM_SEARCH];
            vis_line_t vl = tc->get_top();
            unsigned long width;
            vis_line_t height;
            attr_line_t all_matches;
            char linebuf[32];
            int last_line = tc->get_inner_height();
            int max_line_width;

            snprintf(linebuf, sizeof(linebuf), "%d", last_line);
            max_line_width = strlen(linebuf);

            tc->get_dimensions(height, width);
            vl += height;
            if (vl > PREVIEW_SIZE) {
                vl -= PREVIEW_SIZE;
            }

            while ((vl = bv.next(vl)) != -1_vl &&
                preview_count < MAX_MATCH_COUNT) {
                attr_line_t al;

                tc->textview_value_for_row(vl, al);
                if (preview_count > 0) {
                    all_matches.append("\n");
                }
                snprintf(linebuf, sizeof(linebuf),
                         "L%*d: ",
                         max_line_width, (int) vl);
                all_matches
                    .append(linebuf)
                    .append(al);
                preview_count += 1;
            }

            if (preview_count > 0) {
                lnav_data.ld_preview_status_source.get_description()
                         .set_value("Matching lines for search");
                lnav_data.ld_preview_source
                         .replace_with(all_matches)
                         .set_text_format(TF_UNKNOWN);
            }
        }
    }
}

static void gather_pipers()
{
    for (auto iter = lnav_data.ld_pipers.begin();
         iter != lnav_data.ld_pipers.end(); ) {
        pid_t child_pid = (*iter)->get_child_pid();
        if ((*iter)->has_exited()) {
            log_info("child piper has exited -- %d", child_pid);
            iter = lnav_data.ld_pipers.erase(iter);
        } else {
            ++iter;
        }
    }
}

static void wait_for_pipers()
{
    for (;;) {
        gather_pipers();
        if (lnav_data.ld_pipers.empty()) {
            log_debug("all pipers finished");
            break;
        }
        else {
            usleep(10000);
            rebuild_indexes();
        }
        log_debug("%d pipers still active",
                lnav_data.ld_pipers.size());
    }
}

static void looper()
{
    try {
        exec_context &ec = lnav_data.ld_exec_context;

        readline_context command_context("cmd", &lnav_commands);

        readline_context search_context("search", NULL, false);
        readline_context index_context("capture");
        readline_context sql_context("sql", NULL, false);
        readline_context exec_context("exec");
        readline_curses  rlc;
        sig_atomic_t overlay_counter = 0;
        int lpc;

        command_context.set_highlighter(readline_command_highlighter);
        search_context
                .set_append_character(0)
                .set_highlighter(readline_regex_highlighter);
        sql_context
                .set_highlighter(readline_sqlite_highlighter)
                .set_quote_chars("\"")
                .with_readline_var((char **)&rl_completer_word_break_characters,
                                   " \t\n(),");
        exec_context.set_highlighter(readline_shlex_highlighter);

        listview_curses::action::broadcaster &sb =
            lnav_data.ld_scroll_broadcaster;
        listview_curses::action::broadcaster &vsb =
            lnav_data.ld_view_stack_broadcaster;

        rlc.add_context(LNM_COMMAND, command_context);
        rlc.add_context(LNM_SEARCH, search_context);
        rlc.add_context(LNM_CAPTURE, index_context);
        rlc.add_context(LNM_SQL, sql_context);
        rlc.add_context(LNM_EXEC, exec_context);
        rlc.start();

        lnav_data.ld_filter_source.fss_editor.start();

        lnav_data.ld_rl_view = &rlc;

        lnav_data.ld_rl_view->add_possibility(
            LNM_COMMAND, "viewname", lnav_view_strings);

        lnav_data.ld_rl_view->add_possibility(
            LNM_COMMAND, "zoomlevel", lnav_zoom_strings);

        lnav_data.ld_rl_view->add_possibility(
            LNM_COMMAND, "levelname", level_names);

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

#ifdef VDSUSP
        {
            struct termios tio;

            tcgetattr(STDIN_FILENO, &tio);
            tio.c_cc[VDSUSP] = 0;
            tcsetattr(STDIN_FILENO, TCSANOW, &tio);
        }
#endif

        define_key("\033Od", KEY_BEG);
        define_key("\033Oc", KEY_END);

        view_colors &vc = view_colors::singleton();
        vc.init();

        {
            setup_highlights(lnav_data.ld_views[LNV_LOG].get_highlights());
            setup_highlights(lnav_data.ld_views[LNV_TEXT].get_highlights());
            setup_highlights(lnav_data.ld_views[LNV_SCHEMA].get_highlights());
            setup_highlights(lnav_data.ld_views[LNV_PRETTY].get_highlights());
            setup_highlights(lnav_data.ld_preview_view.get_highlights());

            for (auto format : log_format::get_root_formats()) {
                for (auto &hl : format->lf_highlighters) {
                    if (hl.h_fg.empty()) {
                        hl.with_attrs(hl.h_attrs | vc.attrs_for_ident(hl.h_pattern));
                    } else {
                        hl.with_attrs(hl.h_attrs | vc.ensure_color_pair(hl.h_fg, hl.h_bg));
                    }

                    lnav_data.ld_views[LNV_LOG].get_highlights()[
                        "$" + format->get_name().to_string() + "-" + hl.h_pattern] = hl;
                }
            }
        }

        execute_examples();

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

        lnav_data.ld_view_stack.vs_views.push_back(&lnav_data.ld_views[LNV_LOG]);

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

        lnav_data.ld_doc_view.set_window(lnav_data.ld_window);
        lnav_data.ld_doc_view.set_show_scrollbar(false);

        lnav_data.ld_example_view.set_window(lnav_data.ld_window);
        lnav_data.ld_example_view.set_show_scrollbar(false);

        lnav_data.ld_match_view.set_window(lnav_data.ld_window);

        lnav_data.ld_preview_view.set_window(lnav_data.ld_window);
        lnav_data.ld_preview_view.set_show_scrollbar(false);

        lnav_data.ld_filter_view.set_selectable(true);
        lnav_data.ld_filter_view.set_window(lnav_data.ld_window);
        lnav_data.ld_filter_view.set_show_scrollbar(true);

        lnav_data.ld_status[LNS_TOP].set_top(0);
        lnav_data.ld_status[LNS_BOTTOM].set_top(-(rlc.get_height() + 1));
        for (auto &sc : lnav_data.ld_status) {
            sc.set_window(lnav_data.ld_window);
        }
        lnav_data.ld_status[LNS_TOP].set_data_source(
            &lnav_data.ld_top_source);
        lnav_data.ld_status[LNS_BOTTOM].set_data_source(
            &lnav_data.ld_bottom_source);
        lnav_data.ld_status[LNS_FILTER].set_data_source(
            &lnav_data.ld_filter_status_source);
        lnav_data.ld_status[LNS_DOC].set_data_source(
            &lnav_data.ld_doc_status_source);
        lnav_data.ld_status[LNS_PREVIEW].set_data_source(
            &lnav_data.ld_preview_status_source);

        vsb.push_back(sb.get_functor());

        sb.push_back(view_action<listview_curses>(clear_last_user_mark));
        sb.push_back(&lnav_data.ld_top_source.filename_wire);
        vsb.push_back(&lnav_data.ld_top_source.view_name_wire);
        sb.push_back(&lnav_data.ld_bottom_source.line_number_wire);
        sb.push_back(&lnav_data.ld_bottom_source.percent_wire);
        sb.push_back(&lnav_data.ld_bottom_source.marks_wire);
        sb.push_back(&lnav_data.ld_term_extra.filename_wire);

        lnav_data.ld_match_view.set_show_bottom_border(true);

        for (auto &sc : lnav_data.ld_status) {
            sc.window_change();
        }

        execute_file(ec, dotlnav_path("session"));

        sb.invoke(*lnav_data.ld_view_stack.top());
        vsb.invoke(*lnav_data.ld_view_stack.top());

        {
            input_dispatcher &id = lnav_data.ld_input_dispatcher;

            id.id_escape_matcher = match_escape_seq;
            id.id_escape_handler = handle_escape_seq;
            id.id_key_handler = handle_key;
            id.id_mouse_handler = bind(&xterm_mouse::handle_mouse, &lnav_data.ld_mouse);
        }

        bool session_loaded = false;
        ui_periodic_timer &timer = ui_periodic_timer::singleton();
        struct timeval current_time;

        static sig_atomic_t index_counter;


        timer.start_fade(index_counter, 1);
        while (lnav_data.ld_looping) {
            vector<struct pollfd> pollfds;
            struct timeval to = { 0, 333000 };
            int            rc;
            size_t starting_view_stack_size = lnav_data.ld_view_stack.vs_views.size();

            gettimeofday(&current_time, nullptr);

            lnav_data.ld_top_source.update_time(current_time);

            layout_views();

            rescan_files();
            rebuild_indexes();

            lnav_data.ld_view_stack.do_update();
            lnav_data.ld_doc_view.do_update();
            lnav_data.ld_example_view.do_update();
            lnav_data.ld_match_view.do_update();
            lnav_data.ld_preview_view.do_update();
            for (auto &sc : lnav_data.ld_status) {
                sc.do_update();
            }
            rlc.do_update();
            if (lnav_data.ld_filter_source.fss_editing) {
                lnav_data.ld_filter_source.fss_match_view.set_needs_update();
            }
            lnav_data.ld_filter_view.do_update();
            refresh();

            if (session_loaded) {
                // Only take input from the user after everything has loaded.
                pollfds.push_back((struct pollfd) {
                    STDIN_FILENO,
                    POLLIN,
                    0
                });
            }
            rlc.update_poll_set(pollfds);
            lnav_data.ld_filter_source.fss_editor.update_poll_set(pollfds);

            for (auto &tc : lnav_data.ld_views) {
                tc.update_poll_set(pollfds);
            }

            if (lnav_data.ld_input_dispatcher.in_escape()) {
                to.tv_usec = 15000;
            }
            rc = poll(&pollfds[0], pollfds.size(), to.tv_usec / 1000);

            gettimeofday(&current_time, nullptr);
            lnav_data.ld_input_dispatcher.poll(current_time);

            if (rc < 0) {
                switch (errno) {
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
                    int ch;

                    while ((ch = getch()) != ERR) {
                        alerter::singleton().new_input(ch);

                        lnav_data.ld_input_dispatcher.new_input(current_time, ch);

                        lnav_data.ld_view_stack.top() | [ch] (auto tc) {
                            lnav_data.ld_key_repeat_history.update(ch, tc->get_top());
                        };

                        if (!lnav_data.ld_looping) {
                            // No reason to keep processing input after the
                            // user has quit.  The view stack will also be
                            // empty, which will cause issues.
                            break;
                        }
                    }
                }

                for (auto &tc : lnav_data.ld_views) {
                    tc.check_poll_set(pollfds);
                }

                lnav_data.ld_view_stack.top() | [] (auto tc) {
                    lnav_data.ld_bottom_source.update_hits(tc);
                };

                rlc.check_poll_set(pollfds);
                lnav_data.ld_filter_source.fss_editor.check_poll_set(pollfds);
            }

            if (timer.time_to_update(overlay_counter)) {
                lnav_data.ld_view_stack.top() | [] (auto tc) {
                    tc->set_overlay_needs_update();
                };
            }

            static bool initial_build = false;
            if (!initial_build || timer.fade_diff(index_counter) == 0) {

                if (lnav_data.ld_mode == LNM_PAGING) {
                    timer.start_fade(index_counter, 1);
                }
                else {
                    timer.start_fade(index_counter, 3);
                }
                rebuild_indexes();
                if (!initial_build &&
                        lnav_data.ld_log_source.text_line_count() == 0 &&
                        lnav_data.ld_text_source.text_line_count() > 0) {
                    ensure_view(&lnav_data.ld_views[LNV_TEXT]);
                    lnav_data.ld_views[LNV_TEXT].set_top(vis_line_t(0));
                    lnav_data.ld_rl_view->set_alt_value(
                            HELP_MSG_2(f, F,
                                    "to switch to the next/previous file"));
                }
                if (lnav_data.ld_view_stack.top().value_or(nullptr) ==
                    &lnav_data.ld_views[LNV_TEXT] &&
                    lnav_data.ld_text_source.empty() &&
                    lnav_data.ld_log_source.text_line_count() > 0) {
                    textview_curses *tc_log = &lnav_data.ld_views[LNV_LOG];
                    lnav_data.ld_view_stack.vs_views.pop_back();
                    lnav_data.ld_views[LNV_LOG].set_top(tc_log->get_top_for_last_row());
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

                    execute_init_commands(ec, msgs);

                    if (!msgs.empty()) {
                        pair<string, string> last_msg = msgs.back();

                        lnav_data.ld_rl_view->set_value(last_msg.first);
                        lnav_data.ld_rl_view->set_alt_value(last_msg.second);
                    }

                    session_loaded = true;
                }
            }

            if (lnav_data.ld_winched) {
                struct winsize size;

                lnav_data.ld_winched = false;

                if (ioctl(fileno(stdout), TIOCGWINSZ, &size) == 0) {
                    resizeterm(size.ws_row, size.ws_col);
                }
                rlc.do_update();
                rlc.window_change();
                lnav_data.ld_filter_source.fss_editor.window_change();
                for (auto &sc : lnav_data.ld_status) {
                    sc.window_change();
                }
                lnav_data.ld_view_stack.set_needs_update();
                lnav_data.ld_doc_view.set_needs_update();
                lnav_data.ld_example_view.set_needs_update();
                lnav_data.ld_match_view.set_needs_update();
                lnav_data.ld_filter_view.set_needs_update();
            }

            if (lnav_data.ld_child_terminated) {
                lnav_data.ld_child_terminated = false;

                for (auto iter = lnav_data.ld_children.begin();
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

            if (lnav_data.ld_meta_search) {
                lnav_data.ld_meta_search->start();
            }

            if (lnav_data.ld_view_stack.vs_views.empty() ||
                (lnav_data.ld_view_stack.vs_views.size() == 1 &&
                 starting_view_stack_size == 2 &&
                 lnav_data.ld_file_names.size() ==
                 lnav_data.ld_text_source.size())) {
                lnav_data.ld_looping = false;
            }
        }
    }
    catch (readline_curses::error & e) {
        log_error("error: %s", strerror(e.e_err));
    }
}

void wait_for_children()
{
    vector<struct pollfd> pollfds;
    struct timeval to = { 0, 333000 };

    if (lnav_data.ld_meta_search) {
        lnav_data.ld_meta_search->start();
    }

    do {
        pollfds.clear();

        for (auto &tc : lnav_data.ld_views) {
            tc.update_poll_set(pollfds);
        }

        if (pollfds.empty()) {
            return;
        }

        int rc = poll(&pollfds[0], pollfds.size(), to.tv_usec / 1000);

        if (rc < 0) {
            switch (errno) {
                case 0:
                case EINTR:
                    break;
                default:
                    return;
            }
        }

        for (auto &tc : lnav_data.ld_views) {
            tc.check_poll_set(pollfds);

            lnav_data.ld_view_stack.top() | [] (auto tc) {
                lnav_data.ld_bottom_source.update_hits(tc);
            };
        }
    } while (true);
}

static void print_errors(vector<string> error_list)
{
    for (auto &iter : error_list) {
        fprintf(stderr, "%s%s", iter.c_str(),
                iter[iter.size() - 1] == '\n' ? "" : "\n");
    }
}

int main(int argc, char *argv[])
{
    std::vector<std::string> config_errors, loader_errors;
    exec_context &ec = lnav_data.ld_exec_context;
    int lpc, c, retval = EXIT_SUCCESS;

    shared_ptr<piper_proc> stdin_reader;
    const char *         stdin_out = NULL;
    int                  stdin_out_fd = -1;
    bool exec_stdin = false;
    const char *LANG = getenv("LANG");

    if (LANG == nullptr || strcmp(LANG, "C") == 0) {
        setenv("LANG", "en_US.utf-8", 1);
    }

    (void)signal(SIGPIPE, SIG_IGN);
    setlocale(LC_ALL, "");
    umask(077);

    /* Disable Lnav from being able to execute external commands if
     * "LNAVSECURE" environment variable is set by the user.
     */
    if (getenv("LNAVSECURE") != nullptr) {
        lnav_data.ld_flags |= LNF_SECURE_MODE;
    }

    lnav_data.ld_exec_context.ec_sql_callback = sql_callback;
    lnav_data.ld_exec_context.ec_pipe_callback = pipe_callback;

    lnav_data.ld_program_name = argv[0];
    add_ansi_vars(ec.ec_global_vars);

    rl_readline_name = "lnav";
    lnav_data.ld_db_key_names = DEFAULT_DB_KEY_NAMES;

    stable_sort(lnav_data.ld_db_key_names.begin(),
                lnav_data.ld_db_key_names.end());

    ensure_dotlnav();

    log_install_handlers();
    sql_install_logger();

#ifdef HAVE_LIBCURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
#endif

    lnav_data.ld_debug_log_name = "/dev/null";
    while ((c = getopt(argc, argv, "hHarCc:I:iuf:d:nqtw:vVW")) != -1) {
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
                break;
            case '|':
                if (strcmp("|-", optarg) == 0 ||
                    strcmp("|/dev/stdin", optarg) == 0) {
                    exec_stdin = true;
                }
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
            lnav_data.ld_commands.emplace_back(optarg);
            break;

        case 'f':
            // XXX Not the best way to check for stdin.
            if (strcmp("-", optarg) == 0 ||
                strcmp("/dev/stdin", optarg) == 0) {
                exec_stdin = true;
            }
            lnav_data.ld_commands.push_back("|" + string(optarg));
            break;

        case 'I':
            if (access(optarg, X_OK) != 0) {
                perror("invalid config path");
                exit(EXIT_FAILURE);
            }
            lnav_data.ld_config_paths.emplace_back(optarg);
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

        case 't':
            lnav_data.ld_flags |= LNF_TIMESTAMP;
            break;

        case 'w':
            stdin_out = optarg;
            break;

        case 'W':
        {
            char b;
            if (isatty(STDIN_FILENO) && read(STDIN_FILENO, &b, 1) == -1) {
                perror("Read key from STDIN");
            }
        }
            break;

        case 'v':
            lnav_data.ld_flags |= LNF_VERBOSE;
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

    load_config(lnav_data.ld_config_paths, config_errors);
    if (!config_errors.empty()) {
        print_errors(config_errors);
        return EXIT_FAILURE;
    }
    add_global_vars(ec);

    if (lnav_data.ld_flags & LNF_UPDATE_FORMATS) {
        if (!update_git_formats()) {
            return EXIT_FAILURE;
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
                    O_WRONLY | O_CREAT | O_TRUNC, 0644)) == -1) {
                fprintf(stderr, "error: unable to open destination: %s -- %s\n",
                        dst_path.c_str(), strerror(errno));
            }
            else {
                char buffer[2048];
                ssize_t rc;

                while ((rc = read(in_fd, buffer, sizeof(buffer))) > 0) {
                    ssize_t remaining = rc, written;

                    while (remaining > 0) {
                        written = write(out_fd, buffer, rc);
                        if (written == -1) {
                            fprintf(stderr,
                                    "error: unable to install format file -- %s\n",
                                    strerror(errno));
                            exit(EXIT_FAILURE);
                        }

                        remaining -= written;
                    }
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

    if (lnav_data.ld_flags & LNF_SECURE_MODE) {
        if ((sqlite3_set_authorizer(lnav_data.ld_db.in(),
                                    sqlite_authorizer, NULL)) != SQLITE_OK) {
            fprintf(stderr, "error: unable to attach sqlite authorizer\n");
            exit(EXIT_FAILURE);
        }
    }

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
    register_file_vtab(lnav_data.ld_db.in());
    register_regexp_vtab(lnav_data.ld_db.in());
    register_fstat_vtab(lnav_data.ld_db.in());

    lnav_data.ld_vtab_manager =
        new log_vtab_manager(lnav_data.ld_db,
                             lnav_data.ld_views[LNV_LOG],
                             lnav_data.ld_log_source);

    load_formats(lnav_data.ld_config_paths, loader_errors);

    {
        auto_mem<char, sqlite3_free> errmsg;

        if (sqlite3_exec(lnav_data.ld_db.in(),
                         init_sql,
                         nullptr,
                         nullptr,
                         errmsg.out()) != SQLITE_OK) {
            fprintf(stderr,
                    "error: unable to execute DB init -- %s\n",
                    errmsg.in());
        }
    }

    lnav_data.ld_vtab_manager->register_vtab(new all_logs_vtab());
    lnav_data.ld_vtab_manager->register_vtab(new log_format_vtab_impl(
            *log_format::find_root_format("generic_log")));

    for (auto &iter : log_format::get_root_formats()) {
        log_vtab_impl *lvi = iter->get_vtab_impl();

        if (lvi != NULL) {
            lnav_data.ld_vtab_manager->register_vtab(lvi);
        }
    }

    load_format_extra(lnav_data.ld_db.in(), lnav_data.ld_config_paths, loader_errors);
    load_format_vtabs(lnav_data.ld_vtab_manager, loader_errors);
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

    lnav_data.ld_views[LNV_HELP]
        .set_sub_source(&lnav_data.ld_help_source)
        .set_word_wrap(true);
    lnav_data.ld_views[LNV_LOG]
        .set_sub_source(&lnav_data.ld_log_source)
        .set_delegate(new action_delegate(lnav_data.ld_log_source))
        .add_input_delegate(lnav_data.ld_log_source)
        .set_tail_space(vis_line_t(2))
        .set_overlay_source(new field_overlay_source(lnav_data.ld_log_source));
    lnav_data.ld_views[LNV_TEXT]
        .set_sub_source(&lnav_data.ld_text_source);
    lnav_data.ld_views[LNV_HISTOGRAM]
        .set_sub_source(&lnav_data.ld_hist_source2);
    lnav_data.ld_views[LNV_DB]
        .set_sub_source(&lnav_data.ld_db_row_source);
    lnav_data.ld_db_overlay.dos_labels = &lnav_data.ld_db_row_source;
    lnav_data.ld_views[LNV_DB]
        .set_overlay_source(&lnav_data.ld_db_overlay);
    lnav_data.ld_views[LNV_SPECTRO]
        .set_sub_source(&lnav_data.ld_spectro_source)
        .set_overlay_source(&lnav_data.ld_spectro_source)
        .add_input_delegate(lnav_data.ld_spectro_source)
        .set_tail_space(vis_line_t(2));

    lnav_data.ld_doc_view.set_sub_source(&lnav_data.ld_doc_source);
    lnav_data.ld_example_view.set_sub_source(&lnav_data.ld_example_source);
    lnav_data.ld_match_view.set_sub_source(&lnav_data.ld_match_source);
    lnav_data.ld_preview_view.set_sub_source(&lnav_data.ld_preview_source);
    lnav_data.ld_filter_view
             .set_sub_source(&lnav_data.ld_filter_source)
             .add_input_delegate(lnav_data.ld_filter_source)
             .add_child_view(&lnav_data.ld_filter_source.fss_match_view)
             .add_child_view(&lnav_data.ld_filter_source.fss_editor);

    for (lpc = 0; lpc < LNV__MAX; lpc++) {
        lnav_data.ld_views[lpc].set_gutter_source(new log_gutter_source());
    }

    {
        hist_source2 &hs = lnav_data.ld_hist_source2;

        lnav_data.ld_log_source.set_index_delegate(
                new hist_index_delegate(lnav_data.ld_hist_source2,
                        lnav_data.ld_views[LNV_HISTOGRAM]));
        hs.init();
        lnav_data.ld_zoom_level = 3;
        hs.set_time_slice(ZOOM_LEVELS[lnav_data.ld_zoom_level]);
    }

    for (lpc = 0; lpc < LNV__MAX; lpc++) {
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
        logfile_open_options default_loo;
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
            unique_ptr<url_loader> ul(new url_loader(argv[lpc]));

            lnav_data.ld_file_names[argv[lpc]]
                .with_fd(ul->copy_fd());
            lnav_data.ld_curl_looper.add_request(ul.release());
        }
#endif
        else if (is_glob(argv[lpc])) {
            lnav_data.ld_file_names[argv[lpc]] = default_loo;
        }
        else if (stat(argv[lpc], &st) == -1) {
            fprintf(stderr,
                    "Cannot stat file: %s -- %s\n",
                    argv[lpc],
                    strerror(errno));
            retval = EXIT_FAILURE;
        }
        else if (S_ISFIFO(st.st_mode)) {
            auto_fd fifo_fd;

            if ((fifo_fd = open(argv[lpc], O_RDONLY)) == -1) {
                fprintf(stderr,
                        "Cannot open fifo: %s -- %s\n",
                        argv[lpc],
                        strerror(errno));
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
        else if ((abspath = realpath(argv[lpc], NULL)) == NULL) {
            perror("Cannot find file");
            retval = EXIT_FAILURE;
        }
        else if (S_ISDIR(st.st_mode)) {
            string dir_wild(abspath.in());

            if (dir_wild[dir_wild.size() - 1] == '/') {
                dir_wild.resize(dir_wild.size() - 1);
            }
            lnav_data.ld_file_names[dir_wild + "/*"] = default_loo;
        }
        else {
            lnav_data.ld_file_names[abspath.in()] = default_loo;
        }
    }

    if (lnav_data.ld_flags & LNF_CHECK_CONFIG) {
        rescan_files(true);
        for (auto &ld_file : lnav_data.ld_files) {
            auto lf = ld_file;

            lf->rebuild_index();

            lf->rebuild_index();
            log_format *fmt = lf->get_format();
            if (fmt == NULL) {
                fprintf(stderr, "error:%s:no format found for file\n",
                        lf->get_filename().c_str());
                retval = EXIT_FAILURE;
                continue;
            }
            for (auto line_iter = lf->begin();
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

    if (!isatty(STDIN_FILENO) && !exec_stdin) {
        stdin_reader = make_shared<piper_proc>(
            STDIN_FILENO, lnav_data.ld_flags & LNF_TIMESTAMP, stdin_out);
        stdin_out_fd = stdin_reader->get_fd();
        lnav_data.ld_file_names["stdin"]
            .with_fd(stdin_out_fd);
        if (dup2(STDOUT_FILENO, STDIN_FILENO) == -1) {
            perror("cannot dup stdout to stdin");
        }
        lnav_data.ld_pipers.push_back(stdin_reader);
    }

    if (lnav_data.ld_file_names.empty() &&
        lnav_data.ld_commands.empty() &&
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
            for (auto cmd_iter =
                 lnav_data.ld_commands.begin();
                 cmd_iter != lnav_data.ld_commands.end();
                 ++cmd_iter) {
                log_info("    %s", cmd_iter->c_str());
            }
            log_info("  files:");
            for (auto file_iter =
                 lnav_data.ld_file_names.begin();
                 file_iter != lnav_data.ld_file_names.end();
                 ++file_iter) {
                log_info("    %s", file_iter->first.c_str());
            }

            if (lnav_data.ld_flags & LNF_HEADLESS) {
                std::vector<pair<string, string> > msgs;
                std::vector<pair<string, string> >::iterator msg_iter;
                textview_curses *log_tc, *text_tc, *tc;
                bool found_error = false;

                init_session();
                lnav_data.ld_exec_context.ec_output_stack.back() = stdout;
                alerter::singleton().enabled(false);

                log_tc = &lnav_data.ld_views[LNV_LOG];
                log_tc->set_height(vis_line_t(24));
                lnav_data.ld_view_stack.vs_views.push_back(log_tc);
                // Read all of stdin
                wait_for_pipers();
                rebuild_indexes();

                log_tc->set_top(vis_line_t(0));
                text_tc = &lnav_data.ld_views[LNV_TEXT];
                text_tc->set_top(vis_line_t(0));
                text_tc->set_height(vis_line_t(text_tc->get_inner_height()));
                if (lnav_data.ld_log_source.text_line_count() == 0 &&
                    lnav_data.ld_text_source.text_line_count() > 0) {
                    toggle_view(&lnav_data.ld_views[LNV_TEXT]);
                }

                log_info("Executing initial commands");
                execute_init_commands(lnav_data.ld_exec_context, msgs);
                wait_for_pipers();
                lnav_data.ld_curl_looper.process_all();
                rebuild_indexes();

                for (msg_iter = msgs.begin();
                     msg_iter != msgs.end();
                     ++msg_iter) {
                    if (strncmp("error:", msg_iter->first.c_str(), 6) == 0) {
                        fprintf(stderr, "%s\n", msg_iter->first.c_str());
                        found_error = true;
                    }
                    else if (startswith(msg_iter->first, "info:") &&
                             lnav_data.ld_flags & LNF_VERBOSE) {
                        printf("%s\n", msg_iter->first.c_str());
                    }
                }

                if (!found_error &&
                    !(lnav_data.ld_flags & LNF_QUIET) &&
                    !lnav_data.ld_view_stack.vs_views.empty() &&
                    !lnav_data.ld_stdout_used) {
                    bool suppress_empty_lines = false;
                    list_overlay_source *los;
                    unsigned long view_index;
                    vis_line_t y;

                    tc = *lnav_data.ld_view_stack.top();
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

                    vis_line_t vl;
                    for (vl = tc->get_top();
                         vl < tc->get_inner_height();
                         ++vl, ++y) {
                        attr_line_t al;
                        string &line = al.get_string();
                        while (los != nullptr &&
                               los->list_value_for_overlay(*tc, y, tc->get_inner_height(), vl, al)) {
                            if (write(STDOUT_FILENO, line.c_str(),
                                      line.length()) == -1 ||
                                write(STDOUT_FILENO, "\n", 1) == -1) {
                                perror("1 write to STDOUT");
                            }
                            ++y;
                        }

                        vector<attr_line_t> rows(1);
                        tc->listview_value_for_rows(*tc, vl, rows);
                        if (suppress_empty_lines && rows[0].empty()) {
                            continue;
                        }

                        struct line_range lr = find_string_attr_range(
                                rows[0].get_attrs(), &textview_curses::SA_ORIGINAL_LINE);
                        if (write(STDOUT_FILENO, lr.substr(rows[0].get_string()),
                                  lr.sublen(rows[0].get_string())) == -1 ||
                            write(STDOUT_FILENO, "\n", 1) == -1) {
                            perror("2 write to STDOUT");
                        }

                    }
                    {
                        attr_line_t al;
                        string &line = al.get_string();

                        while (los != nullptr &&
                               los->list_value_for_overlay(*tc, y, tc->get_inner_height(), vl, al) &&
                               !al.empty()) {
                            if (write(STDOUT_FILENO, line.c_str(),
                                      line.length()) == -1 ||
                                write(STDOUT_FILENO, "\n", 1) == -1) {
                                perror("1 write to STDOUT");
                            }
                            ++y;
                        }
                    }
                }
            }
            else {
                auto_fd errpipe[2];

                lnav_data.ld_curl_looper.start();

                init_session();

                log_info("  session_id=%s", lnav_data.ld_session_id.c_str());

                scan_sessions();

                guard_termios gt(STDIN_FILENO);
                auto_fd::pipe(errpipe);

                dup2(errpipe[1], STDERR_FILENO);
                errpipe[1].reset();
                log_pipe_err(errpipe[0]);

                lnav_log_orig_termios = gt.get_termios();

                looper();

                dup2(STDOUT_FILENO, STDERR_FILENO);

                signal(SIGINT, SIG_DFL);

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
            struct stat st;

            fstat(stdin_out_fd, &st);
            auto file_iter = find_if(lnav_data.ld_files.begin(),
                                     lnav_data.ld_files.end(),
                                     same_file(st));
            if (file_iter != lnav_data.ld_files.end()) {
                logfile::iterator line_iter;
                auto lf = *file_iter;
                string str;

                for (line_iter = lf->begin();
                     line_iter != lf->end();
                     ++line_iter) {
                    lf->read_line(line_iter, str);

                    if (write(STDOUT_FILENO, str.c_str(), str.size()) == -1 ||
                        write(STDOUT_FILENO, "\n", 1) == -1) {
                        perror("3 write to STDOUT");
                    }
                }
            }
        }
    }

    lnav_data.ld_curl_looper.stop();

    return retval;
}
