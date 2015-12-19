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
 * @file lnav.hh
 */

#ifndef __lnav_hh
#define __lnav_hh

#include "config.h"

#include <signal.h>

#include <map>
#include <set>
#include <list>
#include <stack>
#include <memory>

#include "byte_array.hh"
#include "grapher.hh"
#include "logfile.hh"
#include "hist_source.hh"
#include "statusview_curses.hh"
#include "listview_curses.hh"
#include "top_status_source.hh"
#include "bottom_status_source.hh"
#include "grep_highlighter.hh"
#include "db_sub_source.hh"
#include "textfile_sub_source.hh"
#include "log_vtab_impl.hh"
#include "readline_curses.hh"
#include "xterm_mouse.hh"
#include "piper_proc.hh"
#include "term_extra.hh"
#include "ansi_scrubber.hh"
#include "curl_looper.hh"
#include "papertrail_proc.hh"
#include "relative_time.hh"

/** The command modes that are available while viewing a file. */
typedef enum {
    LNM_PAGING,
    LNM_COMMAND,
    LNM_SEARCH,
    LNM_CAPTURE,
    LNM_SQL,
    LNM_EXEC,
} ln_mode_t;

enum {
    LNB_SYSLOG,
    LNB__MAX,

    LNB_TIMESTAMP,
    LNB_HELP,
    LNB_HEADLESS,
    LNB_QUIET,
    LNB_ROTATED,
    LNB_CHECK_CONFIG,
    LNB_INSTALL,
    LNB_UPDATE_FORMATS,
};

/** Flags set on the lnav command-line. */
typedef enum {
    LNF_SYSLOG    = (1L << LNB_SYSLOG),

    LNF_ROTATED   = (1L << LNB_ROTATED),

    LNF_TIMESTAMP = (1L << LNB_TIMESTAMP),
    LNF_HELP      = (1L << LNB_HELP),
    LNF_HEADLESS  = (1L << LNB_HEADLESS),
    LNF_QUIET     = (1L << LNB_QUIET),
    LNF_CHECK_CONFIG = (1L << LNB_CHECK_CONFIG),
    LNF_INSTALL   = (1L << LNB_INSTALL),
    LNF_UPDATE_FORMATS = (1L << LNB_UPDATE_FORMATS),

    LNF__ALL      = (LNF_SYSLOG|LNF_HELP)
} lnav_flags_t;

/** The different views available. */
typedef enum {
    LNV_LOG,
    LNV_TEXT,
    LNV_HELP,
    LNV_HISTOGRAM,
    LNV_GRAPH,
    LNV_DB,
    LNV_EXAMPLE,
    LNV_SCHEMA,
    LNV_PRETTY,

    LNV__MAX
} lnav_view_t;

extern const char *lnav_view_strings[LNV__MAX + 1];

extern const char *lnav_zoom_strings[];

/** The status bars. */
typedef enum {
    LNS_TOP,
    LNS_BOTTOM,

    LNS__MAX
} lnav_status_t;

typedef enum {
    LG_GRAPH,
    LG_CAPTURE,

    LG__MAX
} lnav_grep_t;

void sqlite_close_wrapper(void *mem);

typedef std::pair<int, int>                      ppid_time_pair_t;
typedef std::pair<ppid_time_pair_t, std::string> session_pair_t;

class input_state_tracker : public log_state_dumper {
public:
    input_state_tracker() : ist_index(0) {
        memset(this->ist_recent_key_presses, 0, sizeof(this->ist_recent_key_presses));
    };

    void log_state() {
        log_info("recent_key_presses: index=%d", this->ist_index);
        for (int lpc = 0; lpc < COUNT; lpc++) {
            log_msg_extra(" 0x%x (%c)", this->ist_recent_key_presses[lpc],
                    this->ist_recent_key_presses[lpc]);
        }
        log_msg_extra_complete();
    };

    void push_back(int ch) {
        this->ist_recent_key_presses[this->ist_index % COUNT] = ch;
        this->ist_index = (this->ist_index + 1) % COUNT;
    };

private:
    static const int COUNT = 10;

    int ist_recent_key_presses[COUNT];
    size_t ist_index;
};

struct _lnav_data {
    std::string                             ld_session_id;
    time_t                                  ld_session_time;
    time_t                                  ld_session_load_time;
    time_t                                  ld_session_save_time;
    std::list<session_pair_t>               ld_session_file_names;
    int                                     ld_session_file_index;
    const char *                            ld_program_name;
    const char *                            ld_debug_log_name;

    std::list<std::string>                  ld_commands;
    bool                                    ld_cmd_init_done;
    std::vector<std::string>                ld_config_paths;
    std::set<std::pair<std::string, int> >  ld_file_names;
    std::list<logfile *>                    ld_files;
    std::list<std::string>                  ld_other_files;
    std::set<std::string>                   ld_closed_files;
    std::list<std::pair<std::string, int> > ld_files_to_front;
    std::string                             ld_pt_search;
    time_t                                  ld_pt_min_time;
    time_t                                  ld_pt_max_time;
    bool                                    ld_stdout_used;
    sig_atomic_t                            ld_looping;
    sig_atomic_t                            ld_winched;
    sig_atomic_t                            ld_child_terminated;
    unsigned long                           ld_flags;
    WINDOW *                                ld_window;
    ln_mode_t                               ld_mode;

    statusview_curses                       ld_status[LNS__MAX];
    top_status_source                       ld_top_source;
    bottom_status_source                    ld_bottom_source;
    listview_curses::action::broadcaster    ld_scroll_broadcaster;

    time_t                                  ld_top_time;
    int                                     ld_top_time_millis;
    time_t                                  ld_bottom_time;
    int                                     ld_bottom_time_millis;

    textview_curses                         ld_match_view;

    std::stack<textview_curses *>           ld_view_stack;
    textview_curses                         ld_views[LNV__MAX];
    std::auto_ptr<grep_highlighter>         ld_search_child[LNV__MAX];
    vis_line_t                              ld_search_start_line;
    readline_curses *                       ld_rl_view;

    logfile_sub_source                      ld_log_source;
    hist_source                             ld_hist_source;
    hist_source2                            ld_hist_source2;
    int                                     ld_hist_zoom;

    textfile_sub_source                     ld_text_source;

    std::map<textview_curses *, int>        ld_last_user_mark;
    std::map<textview_curses *, int>        ld_select_start;

    grapher                                 ld_graph_source;

    db_label_source                         ld_db_row_source;
    db_overlay_source                       ld_db_overlay;
    std::vector<std::string>                ld_db_key_names;

    std::auto_ptr<grep_highlighter>         ld_grep_child[LG__MAX];
    std::string                             ld_previous_search;
    std::string                             ld_last_search[LNV__MAX];

    vis_line_t                              ld_last_pretty_print_top;

    log_vtab_manager *                      ld_vtab_manager;
    auto_mem<sqlite3, sqlite_close_wrapper> ld_db;

    std::list<pid_t>                        ld_children;
    std::list<piper_proc *>                 ld_pipers;
    xterm_mouse ld_mouse;
    term_extra ld_term_extra;

    input_state_tracker ld_input_state;

    curl_looper ld_curl_looper;

    relative_time ld_last_relative_time;

    std::stack<std::map<std::string, std::string> > ld_local_vars;
    std::stack<std::string> ld_path_stack;
};

extern struct _lnav_data lnav_data;

extern readline_context::command_map_t lnav_commands;
extern bookmark_type_t BM_QUERY;
extern const int HIST_ZOOM_LEVELS;

#define HELP_MSG_1(x, msg) \
    "Press '" ANSI_BOLD(#x) "' " msg

#define HELP_MSG_2(x, y, msg) \
    "Press " ANSI_BOLD(#x) "/" ANSI_BOLD(#y) " " msg

void rebuild_hist(size_t old_count, bool force);
void rebuild_indexes(bool force);

bool ensure_view(textview_curses *expected_tc);
bool toggle_view(textview_curses *toggle_tc);

bool setup_logline_table();

void execute_search(lnav_view_t view, const std::string &regex);

void redo_search(lnav_view_t view_index);

bool rescan_files(bool required = false);

vis_line_t next_cluster(
        vis_line_t(bookmark_vector<vis_line_t>::*f) (vis_line_t),
        bookmark_type_t *bt,
        vis_line_t top);
bool moveto_cluster(vis_line_t(bookmark_vector<vis_line_t>::*f) (vis_line_t),
        bookmark_type_t *bt,
        vis_line_t top);

#endif
