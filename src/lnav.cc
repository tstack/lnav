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
#include <algorithm>
#include <functional>

#include <sqlite3.h>

#ifdef HAVE_BZLIB_H
#include <bzlib.h>
#endif

#include "lnav.hh"
#include "help-txt.h"
#include "init-sql.h"
#include "logfile.hh"
#include "base/func_util.hh"
#include "base/humanize.network.hh"
#include "base/humanize.time.hh"
#include "base/injector.bind.hh"
#include "base/isc.hh"
#include "base/string_util.hh"
#include "base/lnav_log.hh"
#include "base/paths.hh"
#include "bound_tags.hh"
#include "lnav_util.hh"
#include "ansi_scrubber.hh"
#include "listview_curses.hh"
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
#include "termios_guard.hh"
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
#include "term_extra.hh"
#include "log_data_helper.hh"
#include "readline_highlighters.hh"
#include "environ_vtab.hh"
#include "views_vtab.hh"
#include "all_logs_vtab.hh"
#include "regexp_vtab.hh"
#include "fstat_vtab.hh"
#include "xpath_vtab.hh"
#include "textfile_highlighters.hh"
#include "base/future_util.hh"
#include "tailer/tailer.looper.hh"
#include "service_tags.hh"

#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#endif

#include "curl_looper.hh"

#if HAVE_ARCHIVE_H
#include <archive.h>
#endif

#include "yajlpp/yajlpp.hh"
#include "readline_callbacks.hh"
#include "command_executor.hh"
#include "hotkeys.hh"
#include "readline_possibilities.hh"
#include "field_overlay_source.hh"
#include "url_loader.hh"
#include "shlex.hh"
#include "log_actions.hh"
#include "archive_manager.hh"

#ifndef SYSCONFDIR
#define SYSCONFDIR "/usr/etc"
#endif

using namespace std;
using namespace std::literals::chrono_literals;

static bool initial_build = false;
static multimap<lnav_flags_t, string> DEFAULT_FILES;

struct lnav_data_t lnav_data;

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

const static size_t MAX_STDIN_CAPTURE_SIZE = 10 * 1024 * 1024;

static auto bound_active_files =
    injector::bind<file_collection>::to_instance(+[]() {
        return &lnav_data.ld_active_files;
    });

static auto bound_last_rel_time =
    injector::bind<relative_time, last_relative_time_tag>::to_singleton();

static auto bound_term_extra =
    injector::bind<term_extra>::to_singleton();

static auto bound_xterm_mouse =
    injector::bind<xterm_mouse>::to_singleton();

static auto bound_scripts =
    injector::bind<available_scripts>::to_singleton();

static auto bound_curl =
    injector::bind_multiple<isc::service_base>()
        .add_singleton<curl_looper, services::curl_streamer_t>();

static auto bound_tailer =
    injector::bind_multiple<isc::service_base>()
        .add_singleton<tailer::looper, services::remote_tailer_t>();

static auto bound_main =
    injector::bind_multiple<static_service>()
        .add_singleton<main_looper, services::main_t>();

namespace injector {
template<>
void force_linking(last_relative_time_tag anno)
{
}

template<>
void force_linking(services::curl_streamer_t anno)
{
}

template<>
void force_linking(services::remote_tailer_t anno)
{
}

template<>
void force_linking(services::main_t anno)
{
}
}

void add_recent_netlocs_possibilities()
{
    readline_curses *rc = lnav_data.ld_rl_view;

    rc->clear_possibilities(LNM_COMMAND, "recent-netlocs");
    std::set<std::string> netlocs;

    isc::to<tailer::looper&, services::remote_tailer_t>()
        .send_and_wait([&netlocs](auto& tlooper) {
            netlocs = tlooper.active_netlocs();
        });
    netlocs.insert(session_data.sd_recent_netlocs.begin(),
                   session_data.sd_recent_netlocs.end());
    rc->add_possibility(LNM_COMMAND, "recent-netlocs", netlocs);
}

bool setup_logline_table(exec_context &ec)
{
    // Hidden columns don't show up in the table_info pragma.
    static const char *hidden_table_columns[] = {
        "log_time_msecs",
        "log_path",
        "log_text",
        "log_body",

        nullptr
    };

    textview_curses &log_view = lnav_data.ld_views[LNV_LOG];
    bool             retval   = false;
    bool update_possibilities = (
        lnav_data.ld_rl_view != nullptr &&
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
            std::make_shared<log_data_table>(lnav_data.ld_log_source,
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
            switch (pair.second->ht_context) {
                case help_context_t::HC_SQL_FUNCTION:
                case help_context_t::HC_SQL_TABLE_VALUED_FUNCTION: {
                    string poss = pair.first +
                        (pair.second->ht_parameters.empty() ? "()" : ("("));

                    lnav_data.ld_rl_view->add_possibility(LNM_SQL, "*", poss);
                    break;
                }
                default:
                    break;
            }
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

    indexing_result logfile_indexing(const shared_ptr<logfile>& lf,
                                     file_off_t off,
                                     file_size_t total) override
    {
        static sig_atomic_t index_counter = 0;

        if (lnav_data.ld_flags & (LNF_HEADLESS|LNF_CHECK_CONFIG)) {
            return indexing_result::CONTINUE;
        }

        /* XXX require(off <= total); */
        if (off > (off_t)total) {
            off = total;
        }

        if ((((size_t)off == total) && (this->lo_last_offset != off)) ||
            ui_periodic_timer::singleton().time_to_update(index_counter)) {
            lnav_data.ld_bottom_source.update_loading(off, total);
            this->do_update(lf);
            this->lo_last_offset = off;
        }

        if (!lnav_data.ld_looping) {
            return indexing_result::BREAK;
        }
        return indexing_result::CONTINUE;
    };

private:
    void do_update(const shared_ptr<logfile>& lf)
    {
        lnav_data.ld_top_source.update_time();
        for (auto &sc : lnav_data.ld_status) {
            sc.do_update();
        }
        if (lnav_data.ld_mode == LNM_FILES && !initial_build) {
            auto &fc = lnav_data.ld_active_files;
            auto iter = std::find(fc.fc_files.begin(),
                                  fc.fc_files.end(), lf);

            if (iter != fc.fc_files.end()) {
                auto index = std::distance(fc.fc_files.begin(), iter);
                lnav_data.ld_files_view.set_selection(
                    vis_line_t(fc.fc_other_files.size() + index));
                lnav_data.ld_files_view.reload_data();
                lnav_data.ld_files_view.do_update();
            }
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

    void index_start(logfile_sub_source &lss) override {
        this->hid_source.clear();
    };

    void index_line(logfile_sub_source &lss, logfile *lf, logfile::iterator ll) override {
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
        if (ll->is_marked() || ll->is_expr_marked()) {
            this->hid_source.add_value(ll->get_time(), hist_source2::HT_MARK);
        }
    };

    void index_complete(logfile_sub_source &lss) override {
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

    void closed_files(const std::vector<shared_ptr<logfile>> &files) {
        for (const auto& lf : files) {
            log_info("closed text files: %s", lf->get_filename().c_str());
        }
        lnav_data.ld_active_files.close_files(files);
    };

    void promote_file(const shared_ptr<logfile> &lf) {
        if (lnav_data.ld_log_source.insert_file(lf)) {
            this->did_promotion = true;
            log_info("promoting text file to log file: %s (%s)",
                     lf->get_filename().c_str(),
                     lf->get_content_id().c_str());
            auto format = lf->get_format();
            if (format->lf_is_self_describing) {
                auto vt = format->get_vtab_impl();

                if (vt != nullptr) {
                    lnav_data.ld_vtab_manager->register_vtab(vt);
                }
            }

            auto iter = session_data.sd_file_states.find(lf->get_filename());
            if (iter != session_data.sd_file_states.end()) {
                log_debug("found state for log file %d",
                          iter->second.fs_is_visible);

                lnav_data.ld_log_source.find_data(lf) | [&iter](auto ld) {
                    ld->set_visibility(iter->second.fs_is_visible);
                };
            }
        }
        else {
            this->closed_files({lf});
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
    bool did_promotion{false};
};

void rebuild_indexes(nonstd::optional<ui_clock::time_point> deadline)
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

        if (tss->rescan_files(cb, deadline)) {
            text_view.reload_data();
        }

        if (cb.front_file != nullptr) {
            ensure_view(&text_view);

            if (tss->current_file() != cb.front_file) {
                tss->to_front(cb.front_file);
                old_bottoms[LNV_TEXT] = -1_vl;
            }

            if (cb.front_top < 0) {
                cb.front_top += text_view.get_inner_height();
            }
            if (cb.front_top < text_view.get_inner_height()) {
                text_view.set_top(vis_line_t(cb.front_top));
                scroll_downs[LNV_TEXT] = false;
            }
        }
        if (cb.did_promotion && deadline) {
            // If there's a new log file, extend the deadline so it can be
            // indexed quickly.
            deadline = deadline.value() + 500ms;
        }
    }

    std::vector<std::shared_ptr<logfile>> closed_files;
    for (auto& lf : lnav_data.ld_active_files.fc_files) {
        if ((!lf->exists() || lf->is_closed())) {
            log_info("closed log file: %s", lf->get_filename().c_str());
            lnav_data.ld_text_source.remove(lf);
            lnav_data.ld_log_source.remove_file(lf);
            closed_files.emplace_back(lf);
        }
    }
    if (!closed_files.empty()) {
        lnav_data.ld_active_files.close_files(closed_files);
    }

    auto result = lss.rebuild_index(deadline);
    if (result != logfile_sub_source::rebuild_result::rr_no_change) {
        size_t new_count = lss.text_line_count();
        bool force =
            result == logfile_sub_source::rebuild_result::rr_full_rebuild;

        if ((!scroll_downs[LNV_LOG] ||
             log_view.get_top() > vis_line_t(new_count)) &&
            force) {
            scroll_downs[LNV_LOG] = false;
        }

        log_view.reload_data();

        {
            unordered_map<string, list<shared_ptr<logfile>>> id_to_files;
            bool reload = false;

            for (const auto &lf : lnav_data.ld_active_files.fc_files) {
                id_to_files[lf->get_content_id()].push_back(lf);
            }

            for (auto &pair : id_to_files) {
                if (pair.second.size() == 1) {
                    continue;
                }

                pair.second.sort([](const auto& left, const auto& right) {
                    return right->get_stat().st_size <
                           left->get_stat().st_size;
                });

                auto dupe_name = pair.second.front()->get_unique_path();
                pair.second.pop_front();
                for_each(pair.second.begin(),
                         pair.second.end(),
                         [&dupe_name](auto& lf) {
                    log_info("Hiding duplicate file: %s",
                             lf->get_filename().c_str());
                    lf->mark_as_duplicate(dupe_name);
                    lnav_data.ld_log_source.find_data(lf) | [](auto ld) {
                        ld->set_visibility(false);
                    };
                });
                reload = true;
            }

            if (reload) {
                lss.text_filters_changed();
            }
        }
    }

    for (int lpc = 0; lpc < LNV__MAX; lpc++) {
        textview_curses &scroll_view = lnav_data.ld_views[lpc];

        if (scroll_downs[lpc] && scroll_view.get_top_for_last_row() > scroll_view.get_top()) {
            scroll_view.set_top(scroll_view.get_top_for_last_row());
        }
    }

    lnav_data.ld_view_stack.top() | [] (auto tc) {
        lnav_data.ld_filter_status_source.update_filtered(tc->get_sub_source());
        lnav_data.ld_scroll_broadcaster(tc);
    };
}

static bool append_default_files(lnav_flags_t flag)
{
    bool retval = true;

    if (lnav_data.ld_flags & flag) {
        auto cwd = ghc::filesystem::current_path();

        pair<multimap<lnav_flags_t, string>::iterator,
             multimap<lnav_flags_t, string>::iterator> range;
        for (range = DEFAULT_FILES.equal_range(flag);
             range.first != range.second;
             range.first++) {
            string path = range.first->second;
            struct stat st;

            if (access(path.c_str(), R_OK) == 0) {
                auto_mem<char> abspath;

                path = cwd / range.first->second;
                if ((abspath = realpath(path.c_str(), nullptr)) == nullptr) {
                    perror("Unable to resolve path");
                }
                else {
                    lnav_data.ld_active_files.fc_file_names[abspath.in()];
                }
            }
            else if (stat(path.c_str(), &st) == 0) {
                fprintf(stderr,
                        "error: cannot read -- %s%s\n",
                        cwd.c_str(),
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

void rl_focus(readline_curses *rc)
{
    auto fos = (field_overlay_source *)lnav_data.ld_views[LNV_LOG]
                        .get_overlay_source();

    fos->fos_contexts.emplace("", false, true);
}

void rl_blur(readline_curses *rc)
{
    auto fos = (field_overlay_source *)lnav_data.ld_views[LNV_LOG]
        .get_overlay_source();

    fos->fos_contexts.pop();
    lnav_data.ld_preview_generation += 1;
}

readline_context::command_map_t lnav_commands;

static void usage()
{
    const char *usage_msg = R"(usage: %s [options] [logfile1 logfile2 ...]

A curses-based log file viewer that indexes log messages by type
and time to make it easier to navigate through files quickly.

Key bindings:
  ?     View/leave the online help text.
  q     Quit the program.

Options:
  -h         Print this message, then exit.
  -H         Display the internal help text.
  -I path    An additional configuration directory.
  -i         Install the given format files and exit.  Pass 'extra'
             to install the default set of third-party formats.
  -u         Update formats installed from git repositories.
  -C         Check configuration and then exit.
  -d path    Write debug messages to the given file.
  -V         Print version information.

  -a         Load all of the most recent log file types.
  -r         Recursively load files from the given directory hierarchies.
  -R         Load older rotated log files as well.
  -t         Prepend timestamps to the lines of data being read in
             on the standard input.
  -w file    Write the contents of the standard input to this file.

  -c cmd     Execute a command after the files have been loaded.
  -f path    Execute the commands in the given file.
  -n         Run without the curses UI. (headless mode)
  -N         Do not open the default syslog file if no files are given.
  -q         Do not print the log messages after executing all
             of the commands.

Optional arguments:
  logfileN          The log files, directories, or remote paths to view.
                    If a directory is given, all of the files in the
                    directory will be loaded.

Examples:
  To load and follow the syslog file:
    $ lnav

  To load all of the files in /var/log:
    $ lnav /var/log

  To watch the output of make with timestamps prepended:
    $ make 2>&1 | lnav -t

Paths:
  Configuration, session, and format files are stored in:
    %s

  Local copies of remote files and files extracted from
  archives are stored in:
    %s

Documentation: https://docs.lnav.org
Contact:
  https://github.com/tstack/lnav/discussions
  %s
Version: %s
)";

    fprintf(stderr,
            usage_msg,
            lnav_data.ld_program_name,
            lnav::paths::dotlnav().c_str(),
            lnav::paths::workdir().c_str(),
            PACKAGE_BUGREPORT,
            VCS_PACKAGE_STRING);
}

static void clear_last_user_mark(listview_curses *lv)
{
    textview_curses *tc = (textview_curses *) lv;
    if (lnav_data.ld_select_start.find(tc) != lnav_data.ld_select_start.end() &&
        !tc->is_line_visible(vis_line_t(lnav_data.ld_last_user_mark[tc]))) {
        lnav_data.ld_select_start.erase(tc);
        lnav_data.ld_last_user_mark.erase(tc);
    }
}

bool update_active_files(const file_collection& new_files)
{
    static loading_observer obs;

    for (const auto& lf : new_files.fc_files) {
        lf->set_logfile_observer(&obs);
        lnav_data.ld_text_source.push_back(lf);
    }
    for (const auto& other_pair : new_files.fc_other_files) {
        switch (other_pair.second.ofd_format) {
            case file_format_t::FF_SQLITE_DB:
                attach_sqlite_db(lnav_data.ld_db.in(), other_pair.first);
                break;
            default:
                break;
        }
    }
    lnav_data.ld_active_files.merge(new_files);
    if (!new_files.fc_files.empty() ||
        !new_files.fc_other_files.empty() ||
        !new_files.fc_name_to_errors.empty()) {
        lnav_data.ld_active_files.regenerate_unique_file_names();
    }

    return true;
}

bool rescan_files(bool req)
{
    auto& mlooper = injector::get<main_looper&, services::main_t>();
    bool done = false;
    auto delay = 0ms;

    do {
        auto fc = lnav_data.ld_active_files.rescan_files(req);
        bool all_synced = true;

        update_active_files(fc);
        mlooper.get_port().process_for(delay);
        if (lnav_data.ld_flags & LNF_HEADLESS) {
            for (const auto& pair : lnav_data.ld_active_files.fc_other_files) {
                if (pair.second.ofd_format != file_format_t::FF_REMOTE) {
                    continue;
                }

                if (lnav_data.ld_active_files.fc_synced_files
                        .count(pair.first) == 0) {
                    all_synced = false;
                }
            }
            if (!all_synced) {
                delay = 30ms;
            }
        }
        done = fc.fc_file_names.empty() && all_synced;
    } while (!done);
    return true;
}

class lnav_behavior : public mouse_behavior {
public:
    void mouse_event(int button, bool release, int x, int y) override
    {
        textview_curses *tc = *(lnav_data.ld_view_stack.top());
        struct mouse_event me;

        switch (button & xterm_mouse::XT_BUTTON__MASK) {
        case xterm_mouse::XT_BUTTON1:
            me.me_button = mouse_button_t::BUTTON_LEFT;
            break;
        case xterm_mouse::XT_BUTTON2:
            me.me_button = mouse_button_t::BUTTON_MIDDLE;
            break;
        case xterm_mouse::XT_BUTTON3:
            me.me_button = mouse_button_t::BUTTON_RIGHT;
            break;
        case xterm_mouse::XT_SCROLL_UP:
            me.me_button = mouse_button_t::BUTTON_SCROLL_UP;
            break;
        case xterm_mouse::XT_SCROLL_DOWN:
            me.me_button = mouse_button_t::BUTTON_SCROLL_DOWN;
            break;
        }

        if (button & xterm_mouse::XT_DRAG_FLAG) {
            me.me_state = mouse_button_state_t::BUTTON_STATE_DRAGGED;
        }
        else if (release) {
            me.me_state = mouse_button_state_t::BUTTON_STATE_RELEASED;
        }
        else {
            me.me_state = mouse_button_state_t::BUTTON_STATE_PRESSED;
        }

        gettimeofday(&me.me_time, nullptr);
        me.me_x = x - 1;
        me.me_y = y - tc->get_y() - 1;

        tc->handle_mouse(me);
    };

private:
};

static bool handle_config_ui_key(int ch)
{
    nonstd::optional<ln_mode_t> new_mode;

    lnav_data.ld_filter_help_status_source.fss_error.clear();
    if (ch == 'F') {
        new_mode = LNM_FILES;
    } else if (ch == 'T') {
        new_mode = LNM_FILTER;
    }
    if (!lnav_data.ld_filter_source.fss_editing &&
        (ch == '\t' || ch == KEY_BTAB)) {
        if (lnav_data.ld_mode == LNM_FILES) {
            new_mode = LNM_FILTER;
        } else {
            new_mode = LNM_FILES;
        }
    }

    if (ch == 'q') {
        lnav_data.ld_mode = LNM_PAGING;
    } else if (new_mode) {
        lnav_data.ld_last_config_mode = new_mode.value();
        lnav_data.ld_mode = new_mode.value();
        lnav_data.ld_files_view.reload_data();
        lnav_data.ld_filter_view.reload_data();
        lnav_data.ld_status[LNS_FILTER].set_needs_update();
    } else {
        switch (lnav_data.ld_mode) {
            case LNM_FILES:
                if (!lnav_data.ld_files_view.handle_key(ch)) {
                    return handle_paging_key(ch);
                }
                break;
            case LNM_FILTER:
                if (!lnav_data.ld_filter_view.handle_key(ch)) {
                    return handle_paging_key(ch);
                }
                break;
            default:
                ensure(0);
        }
    }
    return true;
}

static bool handle_key(int ch) {
    lnav_data.ld_input_state.push_back(ch);

    switch (ch) {
        case CTRL('d'):
        case KEY_RESIZE:
            break;
        default: {
            switch (lnav_data.ld_mode) {
                case LNM_PAGING:
                    return handle_paging_key(ch);

                case LNM_FILTER:
                case LNM_FILES:
                    return handle_config_ui_key(ch);

                case LNM_COMMAND:
                case LNM_SEARCH:
                case LNM_SEARCH_FILTERS:
                case LNM_SEARCH_FILES:
                case LNM_CAPTURE:
                case LNM_SQL:
                case LNM_EXEC:
                case LNM_USER:
                    handle_rl_key(ch);
                    break;

                default:
                    require(0);
                    break;
            }
        }
    }

    return true;
}

static input_dispatcher::escape_match_t match_escape_seq(const char *keyseq)
{
    if (lnav_data.ld_mode != LNM_PAGING) {
        return input_dispatcher::escape_match_t::NONE;
    }

    auto &km = lnav_config.lc_active_keymap;
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

    if (strlen(keyseq) < longest->first.size()) {
        return input_dispatcher::escape_match_t::PARTIAL;
    }

    return input_dispatcher::escape_match_t::NONE;
}

void update_hits(textview_curses *tc)
{
    auto top_tc = lnav_data.ld_view_stack.top();

    if (top_tc && tc == *top_tc) {
        lnav_data.ld_bottom_source.update_hits(tc);

        if (lnav_data.ld_mode == LNM_SEARCH) {
            const auto MAX_MATCH_COUNT = 10_vl;
            const auto PREVIEW_SIZE = MAX_MATCH_COUNT + 1_vl;

            int preview_count = 0;

            vis_bookmarks &bm = tc->get_bookmarks();
            const auto &bv = bm[&textview_curses::BM_SEARCH];
            auto vl = tc->get_top();
            unsigned long width;
            vis_line_t height;
            attr_line_t all_matches;
            char linebuf[64];
            int last_line = tc->get_inner_height();
            int max_line_width;

            snprintf(linebuf, sizeof(linebuf), "%d", last_line);
            max_line_width = strlen(linebuf);

            tc->get_dimensions(height, width);
            vl += height;
            if (vl > PREVIEW_SIZE) {
                vl -= PREVIEW_SIZE;
            }

            auto prev_vl = bv.prev(tc->get_top());

            if (prev_vl != -1_vl) {
                attr_line_t al;

                tc->textview_value_for_row(prev_vl, al);
                if (preview_count > 0) {
                    all_matches.append("\n");
                }
                snprintf(linebuf, sizeof(linebuf),
                         "L%*d: ",
                         max_line_width, (int) prev_vl);
                all_matches
                    .append(linebuf)
                    .append(al);
                preview_count += 1;
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
                         .set_text_format(text_format_t::TF_UNKNOWN);
                lnav_data.ld_preview_view.set_needs_update();
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
        auto sql_cmd_map = injector::get<
            readline_context::command_map_t*, sql_cmd_map_tag>();
        auto& ec = lnav_data.ld_exec_context;

        readline_context command_context("cmd", &lnav_commands);

        readline_context search_context("search", nullptr, false);
        readline_context search_filters_context("search-filters", nullptr, false);
        readline_context search_files_context("search-files", nullptr, false);
        readline_context index_context("capture");
        readline_context sql_context("sql", sql_cmd_map, false);
        readline_context exec_context("exec");
        readline_context user_context("user");
        readline_curses  rlc;
        sig_atomic_t overlay_counter = 0;
        int lpc;

        command_context.set_highlighter(readline_command_highlighter);
        search_context
                .set_append_character(0)
                .set_highlighter(readline_regex_highlighter);
        search_filters_context
            .set_append_character(0)
            .set_highlighter(readline_regex_highlighter);
        search_files_context
            .set_append_character(0)
            .set_highlighter(readline_regex_highlighter);
        sql_context
                .set_highlighter(readline_sqlite_highlighter)
                .set_quote_chars("\"")
                .with_readline_var((char **)&rl_completer_word_break_characters,
                                   " \t\n(),");
        exec_context.set_highlighter(readline_shlex_highlighter);

        auto &sb = lnav_data.ld_scroll_broadcaster;
        auto &vsb = lnav_data.ld_view_stack_broadcaster;

        rlc.add_context(LNM_COMMAND, command_context);
        rlc.add_context(LNM_SEARCH, search_context);
        rlc.add_context(LNM_SEARCH_FILTERS, search_filters_context);
        rlc.add_context(LNM_SEARCH_FILES, search_files_context);
        rlc.add_context(LNM_CAPTURE, index_context);
        rlc.add_context(LNM_SQL, sql_context);
        rlc.add_context(LNM_EXEC, exec_context);
        rlc.add_context(LNM_USER, user_context);
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

        auto_fd errpipe[2];
        auto_fd::pipe(errpipe);

        dup2(errpipe[1], STDERR_FILENO);
        errpipe[1].reset();
        log_pipe_err(errpipe[0]);

        ui_periodic_timer::singleton();

        auto mouse_i = injector::get<xterm_mouse&>();

        mouse_i.set_behavior(&lb);
        mouse_i.set_enabled(check_experimental("mouse"));

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
        view_colors::init();

        {
            setup_highlights(lnav_data.ld_views[LNV_LOG].get_highlights());
            setup_highlights(lnav_data.ld_views[LNV_TEXT].get_highlights());
            setup_highlights(lnav_data.ld_views[LNV_SCHEMA].get_highlights());
            setup_highlights(lnav_data.ld_views[LNV_PRETTY].get_highlights());
            setup_highlights(lnav_data.ld_preview_view.get_highlights());

            for (const auto& format : log_format::get_root_formats()) {
                for (auto &hl : format->lf_highlighters) {
                    if (hl.h_fg.empty()) {
                        hl.with_attrs(hl.h_attrs | vc.attrs_for_ident(hl.h_pattern));
                    }

                    lnav_data.ld_views[LNV_LOG].get_highlights()[{
                        highlight_source_t::CONFIGURATION,
                        format->get_name().to_string() + "-" + hl.h_pattern
                    }] = hl;
                }
            }
        }

        execute_examples();

        rlc.set_window(lnav_data.ld_window);
        rlc.set_y(-1);
        rlc.set_focus_action(rl_focus);
        rlc.set_change_action(rl_change);
        rlc.set_perform_action(rl_callback);
        rlc.set_alt_perform_action(rl_alt_callback);
        rlc.set_timeout_action(rl_search);
        rlc.set_abort_action(lnav_rl_abort);
        rlc.set_display_match_action(rl_display_matches);
        rlc.set_display_next_action(rl_display_next);
        rlc.set_blur_action(rl_blur);
        rlc.set_completion_request_action(rl_completion_request);
        rlc.set_alt_value(HELP_MSG_2(
            e, E, "to move forward/backward through error messages"));

        (void)curs_set(0);

        lnav_data.ld_view_stack.vs_views.push_back(&lnav_data.ld_views[LNV_LOG]);

        sb.push_back(clear_last_user_mark);
        sb.push_back(bind_mem(&top_status_source::update_filename, &lnav_data.ld_top_source));
        vsb.push_back(bind_mem(&top_status_source::update_view_name, &lnav_data.ld_top_source));
        sb.push_back(bind_mem(&bottom_status_source::update_line_number, &lnav_data.ld_bottom_source));
        sb.push_back(bind_mem(&bottom_status_source::update_percent, &lnav_data.ld_bottom_source));
        sb.push_back(bind_mem(&bottom_status_source::update_marks, &lnav_data.ld_bottom_source));
        sb.push_back(bind_mem(&term_extra::update_title, injector::get<term_extra*>()));

        vsb.push_back(sb);

        for (lpc = 0; lpc < LNV__MAX; lpc++) {
            lnav_data.ld_views[lpc].set_window(lnav_data.ld_window);
            lnav_data.ld_views[lpc].set_y(1);
            lnav_data.ld_views[lpc].
            set_height(vis_line_t(-(rlc.get_height() + 1)));
            lnav_data.ld_views[lpc].set_scroll_action(sb);
            lnav_data.ld_views[lpc].set_search_action(update_hits);
            lnav_data.ld_views[lpc].tc_state_event_handler = [](auto &&tc) {
                lnav_data.ld_bottom_source.update_search_term(tc);
            };
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

        lnav_data.ld_files_view.set_selectable(true);
        lnav_data.ld_files_view.set_window(lnav_data.ld_window);
        lnav_data.ld_files_view.set_show_scrollbar(true);
        lnav_data.ld_files_view.get_disabled_highlights()
            .insert(highlight_source_t::THEME);
        lnav_data.ld_files_view.set_overlay_source(&lnav_data.ld_files_overlay);

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
        lnav_data.ld_status[LNS_FILTER_HELP].set_data_source(
            &lnav_data.ld_filter_help_status_source);
        lnav_data.ld_status[LNS_DOC].set_data_source(
            &lnav_data.ld_doc_status_source);
        lnav_data.ld_status[LNS_PREVIEW].set_data_source(
            &lnav_data.ld_preview_status_source);

        lnav_data.ld_match_view.set_show_bottom_border(true);

        for (auto &sc : lnav_data.ld_status) {
            sc.window_change();
        }

        auto session_path = lnav::paths::dotlnav() / "session";
        execute_file(ec, session_path.string());

        sb(*lnav_data.ld_view_stack.top());
        vsb(*lnav_data.ld_view_stack.top());

        {
            input_dispatcher &id = lnav_data.ld_input_dispatcher;

            id.id_escape_matcher = match_escape_seq;
            id.id_escape_handler = handle_keyseq;
            id.id_key_handler = handle_key;
            id.id_mouse_handler = bind(&xterm_mouse::handle_mouse, &mouse_i);
            id.id_unhandled_handler = [](const char *keyseq) {
                auto enc_len = lnav_config.lc_ui_keymap.size() * 2;
                auto encoded_name = (char *) alloca(enc_len);

                log_info("unbound keyseq: %s", keyseq);
                json_ptr::encode(encoded_name, enc_len,
                                 lnav_config.lc_ui_keymap.c_str());
                // XXX we should have a hotkey for opening a prompt that is
                // pre-filled with a suggestion that the user can complete.
                // This quick-fix key could be used for other stuff as well
                lnav_data.ld_rl_view->set_value(fmt::format(
                    ANSI_CSI ANSI_COLOR_PARAM(COLOR_YELLOW)
                    ";" ANSI_BOLD_PARAM ANSI_CHAR_ATTR
                    "Unrecognized key"
                    ANSI_NORM
                    ", bind to a command using \u2014 "
                    ANSI_BOLD(":config")
                    " /ui/keymap-defs/{}/{}/command <cmd>",
                    encoded_name, keyseq));
                alerter::singleton().chime();
            };
        }

        ui_periodic_timer &timer = ui_periodic_timer::singleton();
        struct timeval current_time;

        static sig_atomic_t index_counter;

        lnav_data.ld_mode = LNM_FILES;

        timer.start_fade(index_counter, 1);

        file_collection active_copy;
        log_debug("rescan started %p", &active_copy);
        active_copy.merge(lnav_data.ld_active_files);
        active_copy.fc_progress = lnav_data.ld_active_files.fc_progress;
        future<file_collection> rescan_future =
            std::async(std::launch::async,
                       &file_collection::rescan_files,
                       active_copy,
                       false);
        bool initial_rescan_completed = false;
        int session_stage = 0;

        // rlc.do_update();

        auto next_rebuild_time = ui_clock::now();
        auto next_status_update_time = next_rebuild_time;
        auto next_rescan_time = next_rebuild_time;

        while (lnav_data.ld_looping) {
            auto loop_deadline = ui_clock::now() +
                (session_stage == 0 ? 3s : 50ms);

            vector<struct pollfd> pollfds;
            size_t starting_view_stack_size = lnav_data.ld_view_stack.vs_views.size();
            int rc;

            gettimeofday(&current_time, nullptr);

            lnav_data.ld_top_source.update_time(current_time);
            lnav_data.ld_preview_view.set_needs_update();

            layout_views();

            auto scan_timeout = initial_rescan_completed ? 0s : 10ms;
            if (rescan_future.valid() &&
                rescan_future.wait_for(scan_timeout) ==
                std::future_status::ready) {
                auto new_files = rescan_future.get();
                if (!initial_rescan_completed &&
                    new_files.fc_file_names.empty() &&
                    new_files.fc_files.empty() &&
                    lnav_data.ld_active_files.fc_progress->readAccess()->
                    sp_tailers.empty()) {
                    initial_rescan_completed = true;

                    log_debug("initial rescan rebuild");
                    rebuild_indexes(loop_deadline);
                    load_session();
                    if (session_data.sd_save_time) {
                        std::string ago;

                        ago = humanize::time::point::from_tv({
                            (time_t) session_data.sd_save_time, 0})
                            .as_time_ago();
                        lnav_data.ld_rl_view->set_value(
                            ("restored session from " ANSI_BOLD_START) +
                            ago +
                            (ANSI_NORM "; press Ctrl-R to reset session"));
                    }

                    lnav_data.ld_session_loaded = true;
                    session_stage += 1;
                    loop_deadline = ui_clock::now();
                    log_debug("file count %d",
                              lnav_data.ld_active_files.fc_files.size())
                }
                update_active_files(new_files);
                if (!initial_rescan_completed) {
                    auto &fview = lnav_data.ld_files_view;
                    auto height = fview.get_inner_height();

                    if (height > 0_vl) {
                        fview.set_selection(height - 1_vl);
                    }
                }

                active_copy.clear();
                active_copy.merge(lnav_data.ld_active_files);
                rescan_future = std::future<file_collection>{};
                next_rescan_time = ui_clock::now() + 333ms;
            }

            if (!rescan_future.valid() &&
                (session_stage < 2 || ui_clock::now() >= next_rescan_time)) {
                rescan_future = std::async(std::launch::async,
                                           &file_collection::rescan_files,
                                           active_copy,
                                           false);
            }

            {
                auto& mlooper = injector::get<main_looper&, services::main_t>();

                mlooper.get_port().process_for(0s);
            }

            auto ui_now = ui_clock::now();
            if (initial_rescan_completed) {
                if (ui_now >= next_rebuild_time) {
                    rebuild_indexes(loop_deadline);
                    if (ui_clock::now() < loop_deadline) {
                        next_rebuild_time = ui_clock::now() + 333ms;
                    }
                }
            } else {
                lnav_data.ld_files_view.set_overlay_needs_update();
            }

            lnav_data.ld_view_stack.do_update();
            lnav_data.ld_doc_view.do_update();
            lnav_data.ld_example_view.do_update();
            lnav_data.ld_match_view.do_update();
            lnav_data.ld_preview_view.do_update();
            if (ui_clock::now() >= next_status_update_time) {
                for (auto &sc : lnav_data.ld_status) {
                    sc.do_update();
                }
                next_status_update_time = ui_clock::now() + 100ms;
            }
            if (lnav_data.ld_filter_source.fss_editing) {
                lnav_data.ld_filter_source.fss_match_view.set_needs_update();
            }
            switch (lnav_data.ld_mode) {
                case LNM_FILTER:
                case LNM_SEARCH_FILTERS:
                    lnav_data.ld_filter_view.set_needs_update();
                    lnav_data.ld_filter_view.do_update();
                    break;
                case LNM_SEARCH_FILES:
                case LNM_FILES:
                    lnav_data.ld_files_view.set_needs_update();
                    lnav_data.ld_files_view.do_update();
                    break;
                default:
                    break;
            }
            if (lnav_data.ld_mode != LNM_FILTER &&
                lnav_data.ld_mode != LNM_FILES) {
                rlc.do_update();
            }
            refresh();

            if (lnav_data.ld_session_loaded) {
                // Only take input from the user after everything has loaded.
                pollfds.push_back((struct pollfd) {
                    STDIN_FILENO,
                    POLLIN,
                    0
                });
                if (initial_build) {
                    switch (lnav_data.ld_mode) {
                        case LNM_COMMAND:
                        case LNM_SEARCH:
                        case LNM_SEARCH_FILTERS:
                        case LNM_SEARCH_FILES:
                        case LNM_SQL:
                        case LNM_EXEC:
                        case LNM_USER:
                            if (rlc.consume_ready_for_input()) {
                                // log_debug("waiting for readline input")
                                view_curses::awaiting_user_input();
                            }
                            break;
                        default:
                            // log_debug("waiting for paging input");
                            view_curses::awaiting_user_input();
                            break;
                    }
                }
            }
            rlc.update_poll_set(pollfds);
            lnav_data.ld_filter_source.fss_editor.update_poll_set(pollfds);

            for (auto &tc : lnav_data.ld_views) {
                tc.update_poll_set(pollfds);
            }
            lnav_data.ld_filter_view.update_poll_set(pollfds);
            lnav_data.ld_files_view.update_poll_set(pollfds);

            ui_now = ui_clock::now();
            auto poll_to =
                (ui_now < loop_deadline && session_stage >= 1) ?
                std::chrono::duration_cast<std::chrono::milliseconds>(loop_deadline - ui_now) :
                0ms;

            if (initial_rescan_completed &&
                lnav_data.ld_input_dispatcher.in_escape() &&
                poll_to > 15ms) {
                poll_to = 15ms;
            }
            // log_debug("poll %d", poll_to.count());
            rc = poll(&pollfds[0], pollfds.size(), poll_to.count());

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
                bool got_user_input = false;
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

                    got_user_input = true;
                    next_status_update_time = ui_clock::now();
                    switch (lnav_data.ld_mode) {
                        case LNM_PAGING:
                        case LNM_FILTER:
                        case LNM_FILES:
                            next_rescan_time = next_status_update_time + 1s;
                            break;
                        case LNM_COMMAND:
                        case LNM_SEARCH:
                        case LNM_SEARCH_FILTERS:
                        case LNM_SEARCH_FILES:
                        case LNM_CAPTURE:
                        case LNM_SQL:
                        case LNM_EXEC:
                        case LNM_USER:
                            next_rescan_time = next_status_update_time + 1min;
                            break;
                    }
                    next_rebuild_time = next_rescan_time;
                }

                for (auto &tc : lnav_data.ld_views) {
                    tc.check_poll_set(pollfds);
                }

                lnav_data.ld_view_stack.top() | [] (auto tc) {
                    lnav_data.ld_bottom_source.update_hits(tc);
                };

                auto old_mode = lnav_data.ld_mode;
                rlc.check_poll_set(pollfds);
                lnav_data.ld_filter_source.fss_editor.check_poll_set(pollfds);
                lnav_data.ld_filter_view.check_poll_set(pollfds);
                lnav_data.ld_files_view.check_poll_set(pollfds);

                if (lnav_data.ld_mode != old_mode) {
                    switch (lnav_data.ld_mode) {
                        case LNM_PAGING:
                        case LNM_FILTER:
                        case LNM_FILES:
                            next_rescan_time = next_status_update_time + 1s;
                            next_rebuild_time = next_rescan_time;
                            break;
                        default:
                            break;
                    }
                }
            }

            if (timer.time_to_update(overlay_counter)) {
                lnav_data.ld_view_stack.top() | [] (auto tc) {
                    tc->set_overlay_needs_update();
                };
            }

            if (initial_rescan_completed && session_stage < 2 &&
                (!initial_build || timer.fade_diff(index_counter) == 0)) {
                if (lnav_data.ld_mode == LNM_PAGING) {
                    timer.start_fade(index_counter, 1);
                }
                else {
                    timer.start_fade(index_counter, 3);
                }
                log_debug("initial build rebuild");
                rebuild_indexes(loop_deadline);
                if (!initial_build &&
                    lnav_data.ld_log_source.text_line_count() == 0 &&
                    lnav_data.ld_text_source.text_line_count() > 0) {
                    ensure_view(&lnav_data.ld_views[LNV_TEXT]);
                    lnav_data.ld_views[LNV_TEXT].set_top(0_vl);
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
                    !lnav_data.ld_active_files.fc_other_files.empty() &&
                    std::any_of(lnav_data.ld_active_files.fc_other_files.begin(),
                                lnav_data.ld_active_files.fc_other_files.end(),
                                [](const auto& pair) {
                                    return pair.second.ofd_format ==
                                           file_format_t::FF_SQLITE_DB;
                                })) {
                    ensure_view(&lnav_data.ld_views[LNV_SCHEMA]);
                }

                if (!initial_build && lnav_data.ld_flags & LNF_HELP) {
                    toggle_view(&lnav_data.ld_views[LNV_HELP]);
                    initial_build = true;
                }
                if (!initial_build && lnav_data.ld_flags & LNF_NO_DEFAULT) {
                    initial_build = true;
                }
                if (lnav_data.ld_log_source.text_line_count() > 0 ||
                    lnav_data.ld_text_source.text_line_count() > 0 ||
                    !lnav_data.ld_active_files.fc_other_files.empty()) {
                    initial_build = true;
                }

                if (initial_build) {
                    static bool ran_cleanup = false;
                    vector<pair<Result<string, string>, string>> cmd_results;

                    execute_init_commands(ec, cmd_results);

                    if (!cmd_results.empty()) {
                        auto last_cmd_result = cmd_results.back();

                        lnav_data.ld_rl_view->set_value(
                            last_cmd_result.first.orElse(err_to_ok).unwrap());
                        lnav_data.ld_rl_view->set_alt_value(
                            last_cmd_result.second);
                    }

                    if (!ran_cleanup) {
                        archive_manager::cleanup_cache();
                        ran_cleanup = true;
                    }
                }

                if (session_stage == 1) {
                    for (size_t view_index = 0;
                         view_index < LNV__MAX;
                         view_index++) {
                        const auto &vs = session_data.sd_view_states[view_index];

                        if (vs.vs_top > 0) {
                            lnav_data.ld_views[view_index]
                                .set_top(vis_line_t(vs.vs_top));
                        }
                    }
                    if (lnav_data.ld_mode == LNM_FILES) {
                        if (lnav_data.ld_active_files.fc_name_to_errors.empty()) {
                            log_debug("switching to paging!");
                            lnav_data.ld_mode = LNM_PAGING;
                        } else {
                            lnav_data.ld_files_view.set_selection(0_vl);
                        }
                    }
                    session_stage += 1;
                    load_time_bookmarks();
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
                lnav_data.ld_files_view.set_needs_update();
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
                 lnav_data.ld_active_files.fc_file_names.size() ==
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
        lnav_data.ld_filter_view.update_poll_set(pollfds);
        lnav_data.ld_files_view.update_poll_set(pollfds);

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
        lnav_data.ld_filter_view.check_poll_set(pollfds);
        lnav_data.ld_files_view.check_poll_set(pollfds);
    } while (true);
}

textview_curses *get_textview_for_mode(ln_mode_t mode)
{
    switch (mode) {
        case LNM_SEARCH_FILTERS:
        case LNM_FILTER:
            return &lnav_data.ld_filter_view;
        case LNM_SEARCH_FILES:
        case LNM_FILES:
            return &lnav_data.ld_files_view;
        default:
            return *lnav_data.ld_view_stack.top();
    }
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
    const char *stdin_out = nullptr;
    int stdin_out_fd = -1;
    bool exec_stdin = false, load_stdin = false;
    const char *LANG = getenv("LANG");
    ghc::filesystem::path stdin_tmp_path;

    if (LANG == nullptr || strcmp(LANG, "C") == 0) {
        setenv("LANG", "en_US.utf-8", 1);
    }

    (void)signal(SIGPIPE, SIG_IGN);
    setlocale(LC_ALL, "");
    umask(027);

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
    lnav_data.ld_config_paths.emplace_back("/etc/lnav");
    lnav_data.ld_config_paths.emplace_back(SYSCONFDIR "/lnav");
    lnav_data.ld_config_paths.emplace_back(lnav::paths::dotlnav());
    while ((c = getopt(argc, argv, "hHarRCc:I:iuf:d:nNqtw:vVW")) != -1) {
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
            lnav_log_level = lnav_log_level_t::TRACE;
            break;

        case 'a':
            lnav_data.ld_flags |= LNF__ALL;
            break;

        case 'n':
            lnav_data.ld_flags |= LNF_HEADLESS;
            break;

        case 'N':
            lnav_data.ld_flags |= LNF_NO_DEFAULT;
            break;

        case 'q':
            lnav_data.ld_flags |= LNF_QUIET;
            break;

        case 'R':
            lnav_data.ld_active_files.fc_rotated = true;
            break;

        case 'r':
            lnav_data.ld_active_files.fc_recursive = true;
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
        if (!update_installs_from_git()) {
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    if (lnav_data.ld_flags & LNF_INSTALL) {
        auto formats_installed_path = lnav::paths::dotlnav() / "formats/installed";
        auto configs_installed_path = lnav::paths::dotlnav() / "configs/installed";

        if (argc == 0) {
            fprintf(stderr, "error: expecting file format paths\n");
            return EXIT_FAILURE;
        }

        for (lpc = 0; lpc < argc; lpc++) {
            if (endswith(argv[lpc], ".git")) {
                if (!install_from_git(argv[lpc])) {
                    return EXIT_FAILURE;
                }
                continue;
            }

            if (strcmp(argv[lpc], "extra") == 0) {
                install_extra_formats();
                continue;
            }

            auto file_type_result = detect_config_file_type(argv[lpc]);
            if (file_type_result.isErr()) {
                fprintf(stderr, "error: %s\n", file_type_result.unwrapErr().c_str());
                return EXIT_FAILURE;
            }
            auto file_type = file_type_result.unwrap();

            string dst_name;
            if (file_type == config_file_type::CONFIG) {
                dst_name = basename(argv[lpc]);
            } else {
                vector<intern_string_t> format_list = load_format_file(
                    argv[lpc], loader_errors);

                if (!loader_errors.empty()) {
                    print_errors(loader_errors);
                    return EXIT_FAILURE;
                }
                if (format_list.empty()) {
                    fprintf(stderr, "error: format file is empty: %s\n",
                            argv[lpc]);
                    return EXIT_FAILURE;
                }

                dst_name = format_list[0].to_string() + ".json";
            }
            auto dst_path = (file_type == config_file_type::CONFIG ?
                             configs_installed_path :
                             formats_installed_path) /
                            dst_name;
            auto_fd in_fd, out_fd;

            if ((in_fd = open(argv[lpc], O_RDONLY)) == -1) {
                perror("unable to open file to install");
            }
            else if ((out_fd = openp(dst_path,
                                     O_WRONLY | O_CREAT | O_TRUNC,
                                     0644)) == -1) {
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
                                    "error: unable to install file -- %s\n",
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
    {
        static auto vtab_modules =
            injector::get<std::vector<std::shared_ptr<vtab_module_base>>>();

        for (const auto& mod : vtab_modules) {
            mod->create(lnav_data.ld_db.in());
        }
    }

    register_views_vtab(lnav_data.ld_db.in());
    register_regexp_vtab(lnav_data.ld_db.in());
    register_xpath_vtab(lnav_data.ld_db.in());
    register_fstat_vtab(lnav_data.ld_db.in());

    lnav_data.ld_vtab_manager = std::make_unique<log_vtab_manager>(
        lnav_data.ld_db, lnav_data.ld_views[LNV_LOG], lnav_data.ld_log_source);

    lnav_data.ld_views[LNV_HELP]
        .set_sub_source(&lnav_data.ld_help_source)
        .set_word_wrap(true);
    auto log_fos = new field_overlay_source(lnav_data.ld_log_source,
                                            lnav_data.ld_text_source);
    if (lnav_data.ld_flags & LNF_HEADLESS) {
        log_fos->fos_show_status = false;
    }
    log_fos->fos_contexts.emplace("", false, true);
    lnav_data.ld_views[LNV_LOG]
        .set_sub_source(&lnav_data.ld_log_source)
        .set_delegate(new action_delegate(lnav_data.ld_log_source))
        .add_input_delegate(lnav_data.ld_log_source)
        .set_tail_space(2_vl)
        .set_overlay_source(log_fos);
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
        .set_tail_space(2_vl);

    lnav_data.ld_doc_view.set_sub_source(&lnav_data.ld_doc_source);
    lnav_data.ld_example_view.set_sub_source(&lnav_data.ld_example_source);
    lnav_data.ld_match_view.set_sub_source(&lnav_data.ld_match_source);
    lnav_data.ld_preview_view.set_sub_source(&lnav_data.ld_preview_source);
    lnav_data.ld_filter_view
        .set_sub_source(&lnav_data.ld_filter_source)
        .add_input_delegate(lnav_data.ld_filter_source)
        .add_child_view(&lnav_data.ld_filter_source.fss_match_view)
        .add_child_view(&lnav_data.ld_filter_source.fss_editor);
    lnav_data.ld_files_view
        .set_sub_source(&lnav_data.ld_files_source)
        .add_input_delegate(lnav_data.ld_files_source);

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

    load_formats(lnav_data.ld_config_paths, loader_errors);

    {
        auto_mem<char, sqlite3_free> errmsg;

        if (sqlite3_exec(lnav_data.ld_db.in(),
                         init_sql.to_string_fragment().data(),
                         nullptr,
                         nullptr,
                         errmsg.out()) != SQLITE_OK) {
            fprintf(stderr,
                    "error: unable to execute DB init -- %s\n",
                    errmsg.in());
        }
    }

    lnav_data.ld_vtab_manager->register_vtab(std::make_shared<all_logs_vtab>());
    lnav_data.ld_vtab_manager->register_vtab(std::make_shared<log_format_vtab_impl>(
            *log_format::find_root_format("generic_log")));

    for (auto &iter : log_format::get_root_formats()) {
        auto lvi = iter->get_vtab_impl();

        if (lvi != nullptr) {
            lnav_data.ld_vtab_manager->register_vtab(lvi);
        }
    }

    load_format_extra(lnav_data.ld_db.in(), lnav_data.ld_config_paths, loader_errors);
    load_format_vtabs(lnav_data.ld_vtab_manager.get(), loader_errors);
    if (!loader_errors.empty()) {
        print_errors(loader_errors);
        return EXIT_FAILURE;
    }

    auto _vtab_cleanup = finally([] {
        static const char *VIRT_TABLES = R"(
SELECT tbl_name FROM sqlite_master WHERE sql LIKE 'CREATE VIRTUAL TABLE%'
)";

        for (auto& lf : lnav_data.ld_active_files.fc_files) {
            lf->close();
        }
        rebuild_indexes();

        lnav_data.ld_vtab_manager = nullptr;

        auto_mem<sqlite3_stmt> stmt(sqlite3_finalize);
        std::vector<std::string> tables_to_drop;
        bool done = false;

        sqlite3_prepare_v2(lnav_data.ld_db.in(),
                           VIRT_TABLES,
                           -1,
                           stmt.out(),
                           nullptr);
        do {
            auto ret = sqlite3_step(stmt.in());

            switch (ret) {
                case SQLITE_OK:
                case SQLITE_DONE:
                    done = true;
                    break;
                case SQLITE_ROW:
                    tables_to_drop.emplace_back(fmt::format(
                        "DROP TABLE {}", sqlite3_column_text(stmt.in(), 0)));
                    break;
            }
        } while (!done);

        for (auto &drop_stmt : tables_to_drop) {
            sqlite3_exec(lnav_data.ld_db.in(),
                         drop_stmt.c_str(),
                         nullptr,
                         nullptr,
                         nullptr);
        }
    });

    if (!(lnav_data.ld_flags & LNF_CHECK_CONFIG)) {
        DEFAULT_FILES.insert(make_pair(LNF_SYSLOG, string("var/log/messages")));
        DEFAULT_FILES.insert(
                make_pair(LNF_SYSLOG, string("var/log/system.log")));
        DEFAULT_FILES.insert(make_pair(LNF_SYSLOG, string("var/log/syslog")));
        DEFAULT_FILES.insert(
                make_pair(LNF_SYSLOG, string("var/log/syslog.log")));
    }

    init_lnav_commands(lnav_commands);

    lnav_data.ld_looping        = true;
    lnav_data.ld_mode           = LNM_PAGING;

    if ((isatty(STDIN_FILENO) || is_dev_null(STDIN_FILENO)) && argc == 0 &&
        !(lnav_data.ld_flags & LNF__ALL) &&
        !(lnav_data.ld_flags & LNF_NO_DEFAULT)) {
        lnav_data.ld_flags |= LNF_SYSLOG;
    }
    if (lnav_data.ld_flags != 0) {
        char start_dir[FILENAME_MAX];

        if (getcwd(start_dir, sizeof(start_dir)) == nullptr) {
            perror("getcwd");
        }
        else {
            do {
                for (lpc = 0; lpc < LNB__MAX; lpc++) {
                    if (!append_default_files((lnav_flags_t)(1L << lpc))) {
                        retval = EXIT_FAILURE;
                    }
                }
            } while (lnav_data.ld_active_files.fc_file_names.empty() &&
                     change_to_parent_dir());

            if (chdir(start_dir) == -1) {
                perror("chdir(start_dir)");
            }
        }
    }

    {
        const auto internals_dir = getenv("DUMP_INTERNALS_DIR");

        if (internals_dir) {
            dump_schema_to(lnav_config_handlers, internals_dir, "config-v1.schema.json");
            dump_schema_to(root_format_handler, internals_dir, "format-v1.schema.json");

            execute_examples();

            auto cmd_ref_path = ghc::filesystem::path(internals_dir) / "cmd-ref.rst";
            auto cmd_file = unique_ptr<FILE, decltype(&fclose)>(fopen(cmd_ref_path.c_str(), "w+"), fclose);

            if (cmd_file.get()) {
                set<readline_context::command_t *> unique_cmds;

                for (auto &cmd : lnav_commands) {
                    if (unique_cmds.find(cmd.second) != unique_cmds.end()) {
                        continue;
                    }
                    unique_cmds.insert(cmd.second);
                    format_help_text_for_rst(cmd.second->c_help, eval_example,
                                             cmd_file.get());
                }
            }

            auto sql_ref_path = ghc::filesystem::path(internals_dir) / "sql-ref.rst";
            auto sql_file = unique_ptr<FILE, decltype(&fclose)>(fopen(sql_ref_path.c_str(), "w+"), fclose);
            set<help_text *> unique_sql_help;

            if (sql_file.get()) {
                for (auto &sql : sqlite_function_help) {
                    if (unique_sql_help.find(sql.second) !=
                        unique_sql_help.end()) {
                        continue;
                    }
                    unique_sql_help.insert(sql.second);
                    format_help_text_for_rst(*sql.second, eval_example,
                                             sql_file.get());
                }
            }

            return EXIT_SUCCESS;
        }
    }

    if (argc == 0) {
        load_stdin = true;
    }

    for (lpc = 0; lpc < argc; lpc++) {
        auto_mem<char> abspath;
        struct stat    st;

        if (strcmp(argv[lpc], "-") == 0) {
            load_stdin = true;
        }
        else if (startswith(argv[lpc], "pt:")) {
#ifdef HAVE_LIBCURL
            lnav_data.ld_pt_search = argv[lpc];
#else
            fprintf(stderr, "error: lnav is not compiled with libcurl\n");
            retval = EXIT_FAILURE;
#endif
        }
#ifdef HAVE_LIBCURL
        else if (is_url(argv[lpc])) {
            auto ul = std::make_shared<url_loader>(argv[lpc]);

            lnav_data.ld_active_files.fc_file_names[argv[lpc]]
                .with_fd(ul->copy_fd());
            isc::to<curl_looper&, services::curl_streamer_t>()
                .send([ul](auto& clooper) {
                    clooper.add_request(ul);
                });
        }
#endif
        else if (is_glob(argv[lpc])) {
            lnav_data.ld_active_files.fc_file_names[argv[lpc]]
                .with_tail(!(lnav_data.ld_flags & LNF_HEADLESS));
        }
        else if (stat(argv[lpc], &st) == -1) {
            if (strchr(argv[lpc], ':') != nullptr) {
                lnav_data.ld_active_files.fc_file_names[argv[lpc]]
                    .with_tail(!(lnav_data.ld_flags & LNF_HEADLESS));
            } else {
                fprintf(stderr,
                        "Cannot stat file: %s -- %s\n",
                        argv[lpc],
                        strerror(errno));
                retval = EXIT_FAILURE;
            }
        }
        else if (access(argv[lpc], R_OK) == -1) {
            fprintf(stderr,
                    "Cannot read file: %s -- %s\n",
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
                retval = EXIT_FAILURE;
            } else {
                auto fifo_piper = make_shared<piper_proc>(
                    fifo_fd.release(),
                    false,
                    open_temp_file(ghc::filesystem::temp_directory_path() /
                                   "lnav.fifo.XXXXXX")
                        .map([](auto pair) {
                            ghc::filesystem::remove(pair.first);

                            return pair;
                        })
                        .expect("Cannot create temporary file for FIFO")
                        .second);
                auto fifo_out_fd = fifo_piper->get_fd();
                char desc[128];

                snprintf(desc, sizeof(desc),
                         "FIFO [%d]",
                         lnav_data.ld_fifo_counter++);
                lnav_data.ld_active_files.fc_file_names[desc]
                    .with_fd(fifo_out_fd);
                lnav_data.ld_pipers.push_back(fifo_piper);
            }
        }
        else if ((abspath = realpath(argv[lpc], nullptr)) == nullptr) {
            perror("Cannot find file");
            retval = EXIT_FAILURE;
        }
        else if (S_ISDIR(st.st_mode)) {
            string dir_wild(abspath.in());

            if (dir_wild[dir_wild.size() - 1] == '/') {
                dir_wild.resize(dir_wild.size() - 1);
            }
            lnav_data.ld_active_files.fc_file_names
                .emplace(dir_wild + "/*", logfile_open_options());
        }
        else {
            lnav_data.ld_active_files.fc_file_names
                .emplace(abspath.in(), logfile_open_options());
        }
    }

    if (lnav_data.ld_flags & LNF_CHECK_CONFIG) {
        rescan_files(true);
        for (auto &lf : lnav_data.ld_active_files.fc_files) {
            logfile::rebuild_result_t rebuild_result;

            do {
                rebuild_result = lf->rebuild_index();
            } while (rebuild_result == logfile::rebuild_result_t::NEW_LINES ||
                     rebuild_result == logfile::rebuild_result_t::NEW_ORDER);
            auto fmt = lf->get_format();
            if (fmt == nullptr) {
                fprintf(stderr, "error:%s:no format found for file\n",
                        lf->get_filename().c_str());
                retval = EXIT_FAILURE;
                continue;
            }
            for (auto line_iter = lf->begin(); line_iter != lf->end(); ++line_iter) {
                if (line_iter->get_msg_level() != log_level_t::LEVEL_INVALID) {
                    continue;
                }

                size_t partial_len;

                auto read_result = lf->read_line(line_iter);
                if (read_result.isErr()) {
                    continue;
                }
                shared_buffer_ref sbr = read_result.unwrap();
                if (fmt->scan_for_partial(sbr, partial_len)) {
                    long line_number = distance(lf->begin(), line_iter);
                    string full_line(sbr.get_data(), sbr.length());
                    string partial_line(sbr.get_data(), partial_len);

                    fprintf(stderr,
                            "error:%s:%ld:line did not match format %s\n",
                            lf->get_filename().c_str(), line_number,
                            fmt->get_pattern_name(line_number).c_str());
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

    if (load_stdin && !isatty(STDIN_FILENO) && !is_dev_null(STDIN_FILENO) && !exec_stdin) {
        if (stdin_out == nullptr) {
            auto pattern = lnav::paths::dotlnav() / "stdin-captures/stdin.XXXXXX";

            auto open_result = open_temp_file(pattern);
            if (open_result.isErr()) {
                fprintf(stderr,
                        "Unable to open temporary file for stdin: %s",
                        open_result.unwrapErr().c_str());
                return EXIT_FAILURE;
            }

            auto temp_pair = open_result.unwrap();
            stdin_tmp_path = temp_pair.first;
            stdin_out_fd = temp_pair.second;
        } else {
            if ((stdin_out_fd = open(stdin_out, O_RDWR | O_CREAT | O_TRUNC, 0600)) == -1) {
                perror("Unable to open output file for stdin");
                return EXIT_FAILURE;
            }
        }

        stdin_reader = make_shared<piper_proc>(
            STDIN_FILENO, lnav_data.ld_flags & LNF_TIMESTAMP, stdin_out_fd);
        lnav_data.ld_active_files.fc_file_names["stdin"]
            .with_fd(auto_fd(stdin_out_fd))
            .with_include_in_session(false);
        lnav_data.ld_pipers.push_back(stdin_reader);
    }

    if (!isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
        if (dup2(STDOUT_FILENO, STDIN_FILENO) == -1) {
            perror("cannot dup stdout to stdin");
        }
    }

    if (lnav_data.ld_active_files.fc_file_names.empty() &&
        lnav_data.ld_commands.empty() &&
        lnav_data.ld_pt_search.empty() &&
        !(lnav_data.ld_flags & (LNF_HELP|LNF_NO_DEFAULT))) {
        fprintf(stderr, "error: no log files given/found.\n");
        retval = EXIT_FAILURE;
    }

    if (retval != EXIT_SUCCESS) {
        usage();
    }
    else {
        isc::supervisor root_superv(injector::get<isc::service_list>());

        try {
            log_info("startup: %s", VCS_PACKAGE_STRING);
            log_host_info();
            log_info("Libraries:");
#ifdef HAVE_BZLIB_H
            log_info("  bzip=%s", BZ2_bzlibVersion());
#endif
#ifdef HAVE_LIBCURL
            log_info("  curl=%s (%s)", LIBCURL_VERSION, LIBCURL_TIMESTAMP);
#endif
#ifdef HAVE_ARCHIVE_H
            log_info("  libarchive=%d", ARCHIVE_VERSION_NUMBER);
#endif
            log_info("  ncurses=%s", NCURSES_VERSION);
            log_info("  pcre=%s", pcre_version());
            log_info("  readline=%s", rl_library_version);
            log_info("  sqlite=%s", sqlite3_version);
            log_info("  zlib=%s", zlibVersion());
            log_info("lnav_data:");
            log_info("  flags=%x", lnav_data.ld_flags);
            log_info("  commands:");
            for (auto cmd_iter = lnav_data.ld_commands.begin();
                 cmd_iter != lnav_data.ld_commands.end();
                 ++cmd_iter) {
                log_info("    %s", cmd_iter->c_str());
            }
            log_info("  files:");
            for (auto file_iter = lnav_data.ld_active_files.fc_file_names.begin();
                 file_iter != lnav_data.ld_active_files.fc_file_names.end();
                 ++file_iter) {
                log_info("    %s", file_iter->first.c_str());
            }

            if (lnav_data.ld_flags & LNF_HEADLESS) {
                std::vector<pair<Result<string, string>, string>> cmd_results;
                textview_curses *log_tc, *text_tc, *tc;
                bool output_view = true;

                view_colors::init();
                rescan_files(true);
                if (!lnav_data.ld_active_files.fc_name_to_errors.empty()) {
                    for (const auto& pair : lnav_data.ld_active_files.fc_name_to_errors) {
                        fprintf(stderr,
                                "error: unable to open file: %s -- %s\n",
                                pair.first.c_str(),
                                pair.second.c_str());
                    }

                    return EXIT_FAILURE;
                }
                init_session();
                lnav_data.ld_exec_context.set_output("stdout", stdout);
                alerter::singleton().enabled(false);

                log_tc = &lnav_data.ld_views[LNV_LOG];
                log_tc->set_height(24_vl);
                lnav_data.ld_view_stack.vs_views.push_back(log_tc);
                // Read all of stdin
                wait_for_pipers();
                rebuild_indexes();

                log_tc->set_top(0_vl);
                text_tc = &lnav_data.ld_views[LNV_TEXT];
                text_tc->set_top(0_vl);
                text_tc->set_height(vis_line_t(text_tc->get_inner_height()));
                if (lnav_data.ld_log_source.text_line_count() == 0 &&
                    lnav_data.ld_text_source.text_line_count() > 0) {
                    toggle_view(&lnav_data.ld_views[LNV_TEXT]);
                }

                log_info("Executing initial commands");
                execute_init_commands(lnav_data.ld_exec_context, cmd_results);
                archive_manager::cleanup_cache();
                wait_for_pipers();
                isc::to<curl_looper&, services::curl_streamer_t>()
                    .send_and_wait([](auto& clooper) {
                        clooper.process_all();
                    });
                rebuild_indexes();

                for (auto &pair : cmd_results) {
                    if (pair.first.isErr()) {
                        fprintf(stderr, "%s\n", pair.first.unwrapErr().c_str());
                        output_view = false;
                    }
                    else {
                        auto msg = pair.first.unwrap();

                        if (startswith(msg, "info:")) {
                            if (lnav_data.ld_flags & LNF_VERBOSE) {
                                printf("%s\n", msg.c_str());
                            }
                        } else if (!msg.empty()) {
                            printf("%s\n", msg.c_str());
                            output_view = false;
                        }
                    }
                }

                if (output_view &&
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
                                rows[0].get_attrs(), &SA_ORIGINAL_LINE);
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
                init_session();

                guard_termios gt(STDIN_FILENO);
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

        // When reading from stdin, tell the user where the capture file is
        // stored so they can look at it later.
        if (stdin_out_fd != -1 &&
            stdin_out == nullptr &&
            !(lnav_data.ld_flags & LNF_QUIET) &&
            !(lnav_data.ld_flags & LNF_HEADLESS)) {
            if (ghc::filesystem::file_size(stdin_tmp_path) > MAX_STDIN_CAPTURE_SIZE) {
                log_info("not saving large stdin capture -- %s",
                         stdin_tmp_path.c_str());
                ghc::filesystem::remove(stdin_tmp_path);
            } else {
                auto home = getenv("HOME");
                auto path_str = stdin_tmp_path.string();

                if (home != nullptr && startswith(path_str, home)) {
                    path_str = path_str.substr(strlen(home));
                    if (path_str[0] != '/') {
                        path_str.insert(0, 1, '/');
                    }
                    path_str.insert(0, 1, '~');
                }

                fprintf(stderr,
                        "info: stdin was captured, you can reopen it using -- "
                        "lnav %s\n",
                        path_str.c_str());
            }
        }
    }

    return retval;
}
