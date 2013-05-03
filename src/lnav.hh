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

#include "grapher.hh"
#include "logfile.hh"
#include "hist_source.hh"
#include "readline_curses.hh"
#include "statusview_curses.hh"
#include "listview_curses.hh"
#include "top_status_source.hh"
#include "bottom_status_source.hh"
#include "grep_highlighter.hh"
#include "db_sub_source.hh"
#include "textfile_sub_source.hh"
#include "log_vtab_impl.hh"

/** The command modes that are available while viewing a file. */
typedef enum {
    LNM_PAGING,
    LNM_COMMAND,
    LNM_SEARCH,
    LNM_CAPTURE,
    LNM_SQL,
} ln_mode_t;

enum {
    LNB_SYSLOG,
    LNB_TIMESTAMP,

    LNB__MAX,

    LNB_ROTATED
};

/** Flags set on the lnav command-line. */
typedef enum {
    LNF_SYSLOG        = (1L << LNB_SYSLOG),

    LNF_ROTATED       = (1L << LNB_ROTATED),

    LNF_TIMESTAMP     = (1L << LNB_TIMESTAMP),

    LNF__ALL          = (LNF_SYSLOG)
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

    LNV__MAX
} lnav_view_t;

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

struct _lnav_data {
    const char *				ld_program_name;
    const char *				ld_debug_log_name;

    std::set< std::pair<std::string, int> >	ld_file_names;
    std::list<logfile *>			ld_files;
    sig_atomic_t				ld_looping;
    sig_atomic_t				ld_winched;
    unsigned long				ld_flags;
    WINDOW *					ld_window;
    ln_mode_t					ld_mode;

    statusview_curses				ld_status[LNS__MAX];
    top_status_source				ld_top_source;
    bottom_status_source			ld_bottom_source;
    listview_curses::action::broadcaster	ld_scroll_broadcaster;

    time_t					ld_top_time;
    time_t					ld_bottom_time;

    std::stack<textview_curses *>		ld_view_stack;
    textview_curses				ld_views[LNV__MAX];
    std::auto_ptr<grep_highlighter>		ld_search_child[LNV__MAX];
    vis_line_t					ld_search_start_line;
    readline_curses *				ld_rl_view;

    logfile_sub_source				ld_log_source;
    hist_source					ld_hist_source;
    int						ld_hist_zoom;

    textfile_sub_source				ld_text_source;

    std::map<textview_curses *, int>		ld_last_user_mark;

    grapher					ld_graph_source;

    hist_source					ld_db_source;
    db_label_source				ld_db_rows;

    int						ld_max_fd;
    fd_set					ld_read_fds;

    std::auto_ptr<grep_highlighter>		ld_grep_child[LG__MAX];

    log_vtab_manager *				ld_vtab_manager;
    auto_mem<sqlite3, sqlite_close_wrapper>	ld_db;
};

extern struct _lnav_data lnav_data;

void rebuild_indexes(bool force);

std::string dotlnav_path(const char *sub);

bool toggle_view(textview_curses *toggle_tc);

#endif
