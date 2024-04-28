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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file lnav.hh
 */

#ifndef lnav_hh
#define lnav_hh

#include <list>
#include <map>
#include <unordered_map>

#include <signal.h>
#include <sys/time.h>

#include "base/ansi_scrubber.hh"
#include "base/isc.hh"
#include "bottom_status_source.hh"
#include "command_executor.hh"
#include "config.h"
#include "db_sub_source.hh"
#include "doc_status_source.hh"
#include "file_collection.hh"
#include "files_sub_source.hh"
#include "filter_status_source.hh"
#include "gantt_status_source.hh"
#include "hist_source.hh"
#include "input_dispatcher.hh"
#include "log_vtab_impl.hh"
#include "plain_text_source.hh"
#include "preview_status_source.hh"
#include "readline_curses.hh"
#include "sqlitepp.hh"
#include "statusview_curses.hh"
#include "textfile_sub_source.hh"
#include "view_helpers.hh"

class spectrogram_source;
class spectro_status_source;

extern const std::vector<std::string> lnav_zoom_strings;

/** The status bars. */
typedef enum {
    LNS_TOP,
    LNS_BOTTOM,
    LNS_FILTER,
    LNS_FILTER_HELP,
    LNS_DOC,
    LNS_PREVIEW0,
    LNS_PREVIEW1,
    LNS_SPECTRO,
    LNS_GANTT,

    LNS__MAX
} lnav_status_t;

using ppid_time_pair_t = std::pair<int, int>;
using session_pair_t = std::pair<ppid_time_pair_t, ghc::filesystem::path>;

class input_state_tracker : public log_state_dumper {
public:
    input_state_tracker()
    {
        memset(this->ist_recent_key_presses,
               0,
               sizeof(this->ist_recent_key_presses));
    }

    void log_state() override
    {
        log_info("recent_key_presses: index=%d", this->ist_index);
        for (int lpc = 0; lpc < COUNT; lpc++) {
            log_msg_extra(" 0x%x (%c)",
                          this->ist_recent_key_presses[lpc],
                          this->ist_recent_key_presses[lpc]);
        }
        log_msg_extra_complete();
    }

    void push_back(int ch)
    {
        this->ist_recent_key_presses[this->ist_index % COUNT] = ch;
        this->ist_index = (this->ist_index + 1) % COUNT;
    }

private:
    static const int COUNT = 10;

    int ist_recent_key_presses[COUNT];
    size_t ist_index{0};
};

struct key_repeat_history {
    int krh_key{0};
    int krh_count{0};
    vis_line_t krh_start_line{0_vl};
    struct timeval krh_last_press_time {
        0, 0
    };

    void update(int ch, vis_line_t top)
    {
        struct timeval now, diff;

        gettimeofday(&now, nullptr);
        timersub(&now, &this->krh_last_press_time, &diff);
        if (diff.tv_sec >= 1 || diff.tv_usec > (750 * 1000)) {
            this->krh_key = 0;
            this->krh_count = 0;
        }
        this->krh_last_press_time = now;

        if (this->krh_key == ch) {
            this->krh_count += 1;
        } else {
            this->krh_key = ch;
            this->krh_count = 1;
            this->krh_start_line = top;
        }
    };
};

enum class lnav_exec_phase : int {
    INIT,
    PRELOAD,
    LOADING,
    INTERACTIVE,
};

struct lnav_data_t {
    std::map<std::string, std::list<session_pair_t>> ld_session_id;
    time_t ld_session_time;
    time_t ld_session_load_time;
    const char* ld_program_name;
    std::string ld_debug_log_name;

    std::list<std::string> ld_commands;
    bool ld_cmd_init_done;
    bool ld_session_loaded;
    std::vector<ghc::filesystem::path> ld_config_paths;
    file_collection ld_active_files;
    std::list<child_poller> ld_child_pollers;
    std::list<std::pair<std::string, file_location_t>> ld_files_to_front;
    bool ld_stdout_used;
    std::atomic_uint32_t ld_sigint_count{0};
    sig_atomic_t ld_looping{true};
    sig_atomic_t ld_winched;
    sig_atomic_t ld_child_terminated;
    unsigned long ld_flags;
    WINDOW* ld_window;
    ln_mode_t ld_mode;
    ln_mode_t ld_last_config_mode{ln_mode_t::FILTER};

    statusview_curses ld_status[LNS__MAX];
    bottom_status_source ld_bottom_source;
    filter_status_source ld_filter_status_source;
    filter_help_status_source ld_filter_help_status_source;
    doc_status_source ld_doc_status_source;
    preview_status_source ld_preview_status_source[2];
    std::unique_ptr<spectro_status_source> ld_spectro_status_source;
    gantt_status_source ld_gantt_status_source;
    bool ld_preview_hidden;
    int64_t ld_preview_generation{0};
    action_broadcaster<listview_curses> ld_scroll_broadcaster;
    action_broadcaster<listview_curses> ld_view_stack_broadcaster;

    plain_text_source ld_help_source;

    plain_text_source ld_doc_source;
    textview_curses ld_doc_view;
    textview_curses ld_filter_view;
    files_sub_source ld_files_source;
    files_overlay_source ld_files_overlay;
    textview_curses ld_files_view;
    plain_text_source ld_example_source;
    textview_curses ld_example_view;
    plain_text_source ld_match_source;
    textview_curses ld_match_view;
    plain_text_source ld_preview_source[2];
    textview_curses ld_preview_view[2];
    plain_text_source ld_user_message_source;
    textview_curses ld_user_message_view;
    std::chrono::time_point<std::chrono::steady_clock>
        ld_user_message_expiration;
    textview_curses ld_spectro_details_view;
    plain_text_source ld_spectro_no_details_source;
    textview_curses ld_gantt_details_view;
    plain_text_source ld_gantt_details_source;

    view_stack<textview_curses> ld_view_stack;
    textview_curses* ld_last_view;
    textview_curses ld_views[LNV__MAX];
    vis_line_t ld_search_start_line;
    readline_curses* ld_rl_view;

    logfile_sub_source ld_log_source;
    hist_source2 ld_hist_source2;
    int ld_zoom_level;
    std::unique_ptr<spectrogram_source> ld_spectro_source;

    textfile_sub_source ld_text_source;

    std::map<textview_curses*, int> ld_last_user_mark;
    std::map<textview_curses*, int> ld_select_start;

    db_label_source ld_db_row_source;
    db_overlay_source ld_db_overlay;
    db_label_source ld_db_preview_source[2];
    db_overlay_source ld_db_preview_overlay_source[2];
    std::vector<std::string> ld_db_key_names;

    vis_line_t ld_last_pretty_print_top;

    std::unique_ptr<log_vtab_manager> ld_vtab_manager;
    auto_sqlite3 ld_db;

    std::unordered_map<std::string, std::string> ld_table_ddl;

    std::list<pid_t> ld_children;

    input_state_tracker ld_input_state;
    input_dispatcher ld_input_dispatcher;

    exec_context ld_exec_context;

    int ld_fifo_counter;

    struct key_repeat_history ld_key_repeat_history;

    bool ld_initial_build{false};
    bool ld_show_help_view{false};
    bool ld_treat_stdin_as_log{false};
    lnav_exec_phase ld_exec_phase{lnav_exec_phase::INIT};

    lnav::func::scoped_cb ld_status_refresher;

    ghc::filesystem::file_time_type ld_last_dot_lnav_time;
};

struct static_service {};

class main_looper
    : public isc::service<main_looper>
    , public static_service {
public:
};

enum class verbosity_t : int {
    quiet,
    standard,
    verbose,
};

extern struct lnav_data_t lnav_data;
extern verbosity_t verbosity;

extern readline_context::command_map_t lnav_commands;
extern const int ZOOM_LEVELS[];
extern const ssize_t ZOOM_COUNT;

#define HELP_MSG_CTRL(x, msg) "Press '" ANSI_BOLD("CTRL-" #x) "' " msg

#define HELP_MSG_1(x, msg) "Press '" ANSI_BOLD(#x) "' " msg

#define HELP_MSG_2(x, y, msg) "Press " ANSI_BOLD(#x) "/" ANSI_BOLD(#y) " " msg

bool setup_logline_table(exec_context& ec);
void wait_for_children();
void wait_for_pipers(std::optional<timeval> deadline = std::nullopt);

#endif
