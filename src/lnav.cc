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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file lnav.cc
 *
 * XXX This file has become a dumping ground for code and needs to be broken up
 * a bit.
 */

#ifdef __CYGWIN__
#    include <alloca.h>
#endif

#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include "config.h"

#if defined(__OpenBSD__) && defined(__clang__) \
    && !defined(_WCHAR_H_CPLUSPLUS_98_CONFORMANCE_)
#    define _WCHAR_H_CPLUSPLUS_98_CONFORMANCE_
#endif
#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include <sqlite3.h>

#ifdef HAVE_BZLIB_H
#    include <bzlib.h>
#endif

#include "all_logs_vtab.hh"
#include "base/ansi_scrubber.hh"
#include "base/ansi_vars.hh"
#include "base/fs_util.hh"
#include "base/func_util.hh"
#include "base/humanize.hh"
#include "base/humanize.time.hh"
#include "base/injector.bind.hh"
#include "base/isc.hh"
#include "base/itertools.hh"
#include "base/lnav.console.hh"
#include "base/lnav_log.hh"
#include "base/paths.hh"
#include "base/string_util.hh"
#include "bottom_status_source.hh"
#include "bound_tags.hh"
#include "breadcrumb_curses.hh"
#include "CLI/CLI.hpp"
#include "date/tz.h"
#include "dump_internals.hh"
#include "environ_vtab.hh"
#include "file_converter_manager.hh"
#include "file_options.hh"
#include "filter_sub_source.hh"
#include "fstat_vtab.hh"
#include "gantt_source.hh"
#include "hist_source.hh"
#include "init-sql.h"
#include "listview_curses.hh"
#include "lnav.events.hh"
#include "lnav.hh"
#include "lnav.indexing.hh"
#include "lnav.management_cli.hh"
#include "lnav_commands.hh"
#include "lnav_config.hh"
#include "lnav_util.hh"
#include "log_data_helper.hh"
#include "log_data_table.hh"
#include "log_format_loader.hh"
#include "log_gutter_source.hh"
#include "log_vtab_impl.hh"
#include "logfile.hh"
#include "logfile_sub_source.hh"
#include "md4cpp.hh"
#include "piper.looper.hh"
#include "readline_curses.hh"
#include "readline_highlighters.hh"
#include "regexp_vtab.hh"
#include "scn/scn.h"
#include "service_tags.hh"
#include "session_data.hh"
#include "spectro_source.hh"
#include "sql_help.hh"
#include "sql_util.hh"
#include "sqlite-extension-func.hh"
#include "sqlitepp.client.hh"
#include "static_file_vtab.hh"
#include "tailer/tailer.looper.hh"
#include "term_extra.hh"
#include "termios_guard.hh"
#include "textfile_highlighters.hh"
#include "textview_curses.hh"
#include "top_status_source.hh"
#include "view_helpers.crumbs.hh"
#include "view_helpers.examples.hh"
#include "view_helpers.hist.hh"
#include "views_vtab.hh"
#include "xpath_vtab.hh"
#include "xterm_mouse.hh"

#ifdef HAVE_LIBCURL
#    include <curl/curl.h>
#endif

#include "curl_looper.hh"

#if HAVE_ARCHIVE_H
#    include <archive.h>
#endif

#include "archive_manager.hh"
#include "command_executor.hh"
#include "field_overlay_source.hh"
#include "hotkeys.hh"
#include "readline_callbacks.hh"
#include "readline_possibilities.hh"
#include "url_loader.hh"
#include "yajlpp/json_ptr.hh"

#ifndef SYSCONFDIR
#    define SYSCONFDIR "/usr/etc"
#endif

using namespace std::literals::chrono_literals;
using namespace lnav::roles::literals;
using namespace md4cpp::literals;

static std::vector<std::string> DEFAULT_FILES;
static auto intern_lifetime = intern_string::get_table_lifetime();

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

const std::vector<std::string> lnav_zoom_strings = {
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
};

static const std::vector<std::string> DEFAULT_DB_KEY_NAMES = {
    "$id",         "capture_count", "capture_index",
    "device",      "enabled",       "filter_id",
    "id",          "inode",         "key",
    "match_index", "parent",        "range_start",
    "range_stop",  "rowid",         "st_dev",
    "st_gid",      "st_ino",        "st_mode",
    "st_rdev",     "st_uid",
};

static auto bound_pollable_supervisor
    = injector::bind<pollable_supervisor>::to_singleton();

static auto bound_filter_sub_source
    = injector::bind<filter_sub_source>::to_singleton();

static auto bound_active_files = injector::bind<file_collection>::to_instance(
    +[]() { return &lnav_data.ld_active_files; });

static auto bound_sqlite_db
    = injector::bind<auto_sqlite3>::to_instance(&lnav_data.ld_db);

static auto bound_lnav_flags
    = injector::bind<unsigned long, lnav_flags_tag>::to_instance(
        &lnav_data.ld_flags);

static auto bound_lnav_exec_context
    = injector::bind<exec_context>::to_instance(&lnav_data.ld_exec_context);

static auto bound_last_rel_time
    = injector::bind<relative_time, last_relative_time_tag>::to_singleton();

static auto bound_term_extra = injector::bind<term_extra>::to_singleton();

static auto bound_xterm_mouse = injector::bind<xterm_mouse>::to_singleton();

static auto bound_scripts = injector::bind<available_scripts>::to_singleton();

static auto bound_crumbs = injector::bind<breadcrumb_curses>::to_singleton();

static auto bound_curl
    = injector::bind_multiple<isc::service_base>()
          .add_singleton<curl_looper, services::curl_streamer_t>();

static auto bound_tailer
    = injector::bind_multiple<isc::service_base>()
          .add_singleton<tailer::looper, services::remote_tailer_t>();

static auto bound_main = injector::bind_multiple<static_service>()
                             .add_singleton<main_looper, services::main_t>();

static auto bound_file_options_hier
    = injector::bind<lnav::safe_file_options_hier>::to_singleton();

namespace injector {
template<>
void
force_linking(last_relative_time_tag anno)
{
}

template<>
void
force_linking(lnav_flags_tag anno)
{
}

template<>
void
force_linking(services::curl_streamer_t anno)
{
}

template<>
void
force_linking(services::remote_tailer_t anno)
{
}

template<>
void
force_linking(services::main_t anno)
{
}
}  // namespace injector

struct lnav_data_t lnav_data;

bool
setup_logline_table(exec_context& ec)
{
    auto& log_view = lnav_data.ld_views[LNV_LOG];
    bool retval = false;

    if (log_view.get_inner_height()) {
        static intern_string_t logline = intern_string::lookup("logline");
        auto vl = log_view.get_selection();
        auto cl = lnav_data.ld_log_source.at_base(vl);

        lnav_data.ld_vtab_manager->unregister_vtab(logline);
        lnav_data.ld_vtab_manager->register_vtab(
            std::make_shared<log_data_table>(lnav_data.ld_log_source,
                                             *lnav_data.ld_vtab_manager,
                                             cl,
                                             logline));

        retval = true;
    }

    auto& db_key_names = lnav_data.ld_db_key_names;

    db_key_names = DEFAULT_DB_KEY_NAMES;

    for (const auto& iter : *lnav_data.ld_vtab_manager) {
        iter.second->get_foreign_keys(db_key_names);
    }

    stable_sort(db_key_names.begin(), db_key_names.end());

    return retval;
}

static bool
append_default_files()
{
    bool retval = true;
    auto cwd = ghc::filesystem::current_path();

    for (const auto& path : DEFAULT_FILES) {
        if (access(path.c_str(), R_OK) == 0) {
            auto_mem<char> abspath;

            auto full_path = cwd / path;
            if ((abspath = realpath(full_path.c_str(), nullptr)) == nullptr) {
                perror("Unable to resolve path");
            } else {
                lnav_data.ld_active_files.fc_file_names[abspath.in()];
            }
        } else if (lnav::filesystem::stat_file(path).isOk()) {
            lnav::console::print(
                stderr,
                lnav::console::user_message::error(
                    attr_line_t("default syslog file is not readable -- ")
                        .append(lnav::roles::file(cwd))
                        .append(lnav::roles::file(path))));
            retval = false;
        }
    }

    return retval;
}

static void
sigint(int sig)
{
    auto counter = lnav_data.ld_sigint_count.fetch_add(1);
    if (counter >= 3) {
        abort();
    }
}

static void
sigwinch(int sig)
{
    lnav_data.ld_winched = true;
}

static void
sigchld(int sig)
{
    lnav_data.ld_child_terminated = true;
}

static void
handle_rl_key(int ch, const char* keyseq)
{
    switch (ch) {
        case KEY_F(2):
            if (xterm_mouse::is_available()) {
                auto& mouse_i = injector::get<xterm_mouse&>();
                mouse_i.set_enabled(!mouse_i.is_enabled());
            }
            break;
        case KEY_PPAGE:
        case KEY_NPAGE:
        case KEY_CTRL('p'):
            handle_paging_key(ch, keyseq);
            break;

        case KEY_CTRL(']'):
            lnav_data.ld_rl_view->abort();
            break;

        default:
            lnav_data.ld_rl_view->handle_key(ch);
            break;
    }
}

readline_context::command_map_t lnav_commands;

static attr_line_t
command_arg_help()
{
    return attr_line_t()
        .append(
            "command arguments must start with one of the following symbols "
            "to denote the type of command:\n")
        .append("   ")
        .append(":"_symbol)
        .append(" - ")
        .append("an lnav command   (e.g. :goto 42)\n")
        .append("   ")
        .append(";"_symbol)
        .append(" - ")
        .append("an SQL statement  (e.g. ;SELECT * FROM syslog_log)\n")
        .append("   ")
        .append("|"_symbol)
        .append(" - ")
        .append("an lnav script    (e.g. |rename-stdin foo)\n");
}

static void
usage()
{
    attr_line_t ex1_term;

    ex1_term.append(lnav::roles::ok("$"))
        .append(" ")
        .append(lnav::roles::file("lnav"))
        .pad_to(40)
        .with_attr_for_all(VC_ROLE.value(role_t::VCR_QUOTED_CODE));

    attr_line_t ex2_term;

    ex2_term.append(lnav::roles::ok("$"))
        .append(" ")
        .append(lnav::roles::file("lnav"))
        .append(" ")
        .append(lnav::roles::file("/var/log"))
        .pad_to(40)
        .with_attr_for_all(VC_ROLE.value(role_t::VCR_QUOTED_CODE));

    attr_line_t ex3_term;

    ex3_term.append(lnav::roles::ok("$"))
        .append(" ")
        .append(lnav::roles::file("lnav"))
        .append(" ")
        .append("-e"_symbol)
        .append(" '")
        .append(lnav::roles::file("make"))
        .append(" ")
        .append("-j4"_symbol)
        .append("' ")
        .pad_to(40)
        .with_attr_for_all(VC_ROLE.value(role_t::VCR_QUOTED_CODE));

    attr_line_t usage_al;

    usage_al.append("usage"_h1)
        .append(": ")
        .append(lnav::roles::file(lnav_data.ld_program_name))
        .append(" [")
        .append("options"_variable)
        .append("] [")
        .append("logfile1"_variable)
        .append(" ")
        .append("logfile2"_variable)
        .append(" ")
        .append("..."_variable)
        .append("]\n")
        .append(R"(
A log file viewer for the terminal that indexes log messages to
make it easier to navigate through files quickly.

)")
        .append("Key Bindings"_h2)
        .append("\n")
        .append("  ?"_symbol)
        .append("    View/leave the online help text.\n")
        .append("  q"_symbol)
        .append("    Quit the program.\n")
        .append("\n")
        .append("Options"_h2)
        .append("\n")
        .append("  ")
        .append("-h"_symbol)
        .append("         ")
        .append("Print this message, then exit.\n")
        .append("  ")
        .append("-H"_symbol)
        .append("         ")
        .append("Display the internal help text.\n")
        .append("\n")
        .append("  ")
        .append("-I"_symbol)
        .append(" ")
        .append("dir"_variable)
        .append("     ")
        .append("An additional configuration directory.\n")
        .append("  ")
        .append("-W"_symbol)
        .append("         ")
        .append("Print warnings related to lnav's configuration.\n")
        .append("  ")
        .append("-u"_symbol)
        .append("         ")
        .append("Update formats installed from git repositories.\n")
        .append("  ")
        .append("-d"_symbol)
        .append(" ")
        .append("file"_variable)
        .append("    ")
        .append("Write debug messages to the given file.\n")
        .append("  ")
        .append("-V"_symbol)
        .append("         ")
        .append("Print version information.\n")
        .append("\n")
        .append("  ")
        .append("-r"_symbol)
        .append("         ")
        .append(
            "Recursively load files from the given directory hierarchies.\n")
        .append("  ")
        .append("-R"_symbol)
        .append("         ")
        .append("Load older rotated log files as well.\n")
        .append("  ")
        .append("-c"_symbol)
        .append(" ")
        .append("cmd"_variable)
        .append("     ")
        .append("Execute a command after the files have been loaded.\n")
        .append("  ")
        .append("-f"_symbol)
        .append(" ")
        .append("file"_variable)
        .append("    ")
        .append("Execute the commands in the given file.\n")
        .append("  ")
        .append("-e"_symbol)
        .append(" ")
        .append("cmd"_variable)
        .append("     ")
        .append("Execute a shell command-line.\n")
        .append("  ")
        .append("-t"_symbol)
        .append("         ")
        .append("Treat data piped into standard in as a log file.\n")
        .append("  ")
        .append("-n"_symbol)
        .append("         ")
        .append("Run without the curses UI. (headless mode)\n")
        .append("  ")
        .append("-N"_symbol)
        .append("         ")
        .append("Do not open the default syslog file if no files are given.\n")
        .append("  ")
        .append("-q"_symbol)
        .append("         ")
        .append("Do not print informational messages.\n")
        .append("\n")
        .append("Optional arguments"_h2)
        .append("\n")
        .append("  ")
        .append("logfileN"_variable)
        .append(R"(   The log files, directories, or remote paths to view.
             If a directory is given, all of the files in the
             directory will be loaded.
)")
        .append("\n")
        .append("Management-Mode Options"_h2)
        .append("\n")
        .append("  ")
        .append("-i"_symbol)
        .append("         ")
        .append(R"(Install the given format files and exit.  Pass 'extra'
             to install the default set of third-party formats.
)")
        .append("  ")
        .append("-m"_symbol)
        .append("         ")
        .append(R"(Switch to the management command-line mode.  This mode
             is used to work with lnav's configuration.
)")
        .append("\n")
        .append("Examples"_h2)
        .append("\n ")
        .append("\u2022"_list_glyph)
        .append(" To load and follow the syslog file:\n")
        .append("     ")
        .append(ex1_term)
        .append("\n\n ")
        .append("\u2022"_list_glyph)
        .append(" To load all of the files in ")
        .append(lnav::roles::file("/var/log"))
        .append(":\n")
        .append("     ")
        .append(ex2_term)
        .append("\n\n ")
        .append("\u2022"_list_glyph)
        .append(" To watch the output of ")
        .append(lnav::roles::file("make"))
        .append(":\n")
        .append("     ")
        .append(ex3_term)
        .append("\n\n")
        .append("Paths"_h2)
        .append("\n ")
        .append("\u2022"_list_glyph)
        .append(" Format files are read from:")
        .append("\n    ")
        .append(":open_file_folder:"_emoji)
        .append(" ")
        .append(lnav::roles::file("/etc/lnav"))
        .append("\n    ")
        .append(":open_file_folder:"_emoji)
        .append(" ")
        .append(lnav::roles::file(SYSCONFDIR "/lnav"))
        .append("\n ")
        .append("\u2022"_list_glyph)
        .append(" Configuration, session, and format files are stored in:\n")
        .append("    ")
        .append(":open_file_folder:"_emoji)
        .append(" ")
        .append(lnav::roles::file(lnav::paths::dotlnav().string()))
        .append("\n\n ")
        .append("\u2022"_list_glyph)
        .append(" Local copies of remote files, files extracted from\n")
        .append("   archives, execution output, and so on are stored in:\n")
        .append("    ")
        .append(":open_file_folder:"_emoji)
        .append(" ")
        .append(lnav::roles::file(lnav::paths::workdir().string()))
        .append("\n\n")
        .append("Documentation"_h1)
        .append(": ")
        .append("https://docs.lnav.org"_hyperlink)
        .append("\n")
        .append("Contact"_h1)
        .append("\n")
        .append("  ")
        .append(":speech_balloon:"_emoji)
        .append(" https://github.com/tstack/lnav/discussions\n")
        .append("  ")
        .append(":mailbox:"_emoji)
        .appendf(FMT_STRING(" {}\n"), PACKAGE_BUGREPORT)
        .append("Version"_h1)
        .appendf(FMT_STRING(": {}"), VCS_PACKAGE_STRING);

    lnav::console::println(stderr, usage_al);
}

static void
clear_last_user_mark(listview_curses* lv)
{
    auto* tc = (textview_curses*) lv;
    if (lnav_data.ld_select_start.find(tc) != lnav_data.ld_select_start.end()
        && !tc->is_line_visible(vis_line_t(lnav_data.ld_last_user_mark[tc])))
    {
        lnav_data.ld_select_start.erase(tc);
        lnav_data.ld_last_user_mark.erase(tc);
    }
}

static void
update_view_position(listview_curses* lv)
{
    lnav_data.ld_view_stack.top() | [lv](auto* top_lv) {
        if (lv != top_lv) {
            return;
        }

        lnav_data.ld_bottom_source.update_line_number(lv);
        lnav_data.ld_bottom_source.update_percent(lv);
        lnav_data.ld_bottom_source.update_marks(lv);
    };
}

static bool
handle_config_ui_key(int ch, const char* keyseq)
{
    bool retval = false;

    if (ch == KEY_F(2)) {
        if (xterm_mouse::is_available()) {
            auto& mouse_i = injector::get<xterm_mouse&>();
            mouse_i.set_enabled(!mouse_i.is_enabled());
        }
        return retval;
    }

    switch (lnav_data.ld_mode) {
        case ln_mode_t::FILES:
            retval = lnav_data.ld_files_view.handle_key(ch);
            break;
        case ln_mode_t::FILTER:
            retval = lnav_data.ld_filter_view.handle_key(ch);
            break;
        default:
            ensure(0);
    }

    if (retval) {
        return retval;
    }

    std::optional<ln_mode_t> new_mode;

    lnav_data.ld_filter_help_status_source.fss_error.clear();
    if (ch == 'F') {
        new_mode = ln_mode_t::FILES;
    } else if (ch == 'T') {
        new_mode = ln_mode_t::FILTER;
    } else if (ch == '\t' || ch == KEY_BTAB) {
        if (lnav_data.ld_mode == ln_mode_t::FILES) {
            new_mode = ln_mode_t::FILTER;
        } else {
            new_mode = ln_mode_t::FILES;
        }
    } else if (ch == 'q' || ch == KEY_ESCAPE) {
        new_mode = ln_mode_t::PAGING;
    }

    if (new_mode) {
        if (new_mode.value() == ln_mode_t::FILES
            || new_mode.value() == ln_mode_t::FILTER)
        {
            lnav_data.ld_last_config_mode = new_mode.value();
        }
        lnav_data.ld_mode = new_mode.value();
        lnav_data.ld_files_view.reload_data();
        lnav_data.ld_filter_view.reload_data();
        lnav_data.ld_status[LNS_FILTER].set_needs_update();
    } else {
        return handle_paging_key(ch, keyseq);
    }

    return true;
}

static bool
handle_key(int ch, const char* keyseq)
{
    static auto* breadcrumb_view = injector::get<breadcrumb_curses*>();

    lnav_data.ld_input_state.push_back(ch);

    switch (ch) {
        case KEY_RESIZE:
            break;
        default: {
            switch (lnav_data.ld_mode) {
                case ln_mode_t::PAGING:
                    return handle_paging_key(ch, keyseq);

                case ln_mode_t::BREADCRUMBS:
                    if (ch == '`' || !breadcrumb_view->handle_key(ch)) {
                        lnav_data.ld_mode = ln_mode_t::PAGING;
                        lnav_data.ld_view_stack.set_needs_update();
                        return true;
                    }
                    return true;

                case ln_mode_t::FILTER:
                case ln_mode_t::FILES:
                    return handle_config_ui_key(ch, keyseq);

                case ln_mode_t::SPECTRO_DETAILS: {
                    if (ch == '\t' || ch == 'q') {
                        lnav_data.ld_mode = ln_mode_t::PAGING;
                        return true;
                    }
                    if (lnav_data.ld_spectro_details_view.handle_key(ch)) {
                        return true;
                    }
                    switch (ch) {
                        case 'n': {
                            execute_command(lnav_data.ld_exec_context,
                                            "next-mark search");
                            return true;
                        }
                        case 'N': {
                            execute_command(lnav_data.ld_exec_context,
                                            "prev-mark search");
                            return true;
                        }
                        case '/': {
                            execute_command(lnav_data.ld_exec_context,
                                            "prompt search-spectro-details");
                            return true;
                        }
                    }
                    return false;
                }

                case ln_mode_t::COMMAND:
                case ln_mode_t::SEARCH:
                case ln_mode_t::SEARCH_FILTERS:
                case ln_mode_t::SEARCH_FILES:
                case ln_mode_t::SEARCH_SPECTRO_DETAILS:
                case ln_mode_t::CAPTURE:
                case ln_mode_t::SQL:
                case ln_mode_t::EXEC:
                case ln_mode_t::USER:
                    handle_rl_key(ch, keyseq);
                    break;

                case ln_mode_t::BUSY:
                    switch (ch) {
                        case KEY_ESCAPE:
                        case KEY_CTRL(']'):
                            log_vtab_data.lvd_looping = false;
                            break;
                    }
                    break;

                default:
                    require(0);
                    break;
            }
        }
    }

    return true;
}

static input_dispatcher::escape_match_t
match_escape_seq(const char* keyseq)
{
    if (lnav_data.ld_mode != ln_mode_t::PAGING) {
        return input_dispatcher::escape_match_t::NONE;
    }

    auto& km = lnav_config.lc_active_keymap;
    auto iter = km.km_seq_to_cmd.find(keyseq);
    if (iter != km.km_seq_to_cmd.end()) {
        return input_dispatcher::escape_match_t::FULL;
    }

    auto lb = km.km_seq_to_cmd.lower_bound(keyseq);
    if (lb == km.km_seq_to_cmd.end()) {
        return input_dispatcher::escape_match_t::NONE;
    }

    auto ub = km.km_seq_to_cmd.upper_bound(keyseq);
    auto longest = max_element(
        lb, ub, [](auto l, auto r) { return l.first.size() < r.first.size(); });

    if (strlen(keyseq) < longest->first.size()) {
        return input_dispatcher::escape_match_t::PARTIAL;
    }

    return input_dispatcher::escape_match_t::NONE;
}

static void
gather_pipers()
{
    for (auto iter = lnav_data.ld_child_pollers.begin();
         iter != lnav_data.ld_child_pollers.end();)
    {
        if (iter->poll(lnav_data.ld_active_files)
            == child_poll_result_t::FINISHED)
        {
            iter = lnav_data.ld_child_pollers.erase(iter);
        } else {
            ++iter;
        }
    }
}

void
wait_for_pipers(std::optional<timeval> deadline)
{
    static const auto MAX_SLEEP_TIME = std::chrono::milliseconds(300);
    auto sleep_time = std::chrono::milliseconds(10);

    for (;;) {
        gather_pipers();
        auto piper_count = lnav_data.ld_active_files.active_pipers();
        if (piper_count == 0 && lnav_data.ld_child_pollers.empty()) {
            log_debug("all pipers finished");
            break;
        }
        if (deadline && (deadline.value() < current_timeval())) {
            break;
        }
        // Use usleep() since it is defined to be interruptable by a signal.
        auto urc = usleep(
            std::chrono::duration_cast<std::chrono::microseconds>(sleep_time)
                .count());
        if (urc == -1 && errno == EINTR) {
            log_trace("wait_for_pipers(): sleep interrupted");
        }
        rebuild_indexes();

        log_debug("%d pipers and %d children are still active",
                  piper_count,
                  lnav_data.ld_child_pollers.size());
        if (sleep_time < MAX_SLEEP_TIME) {
            sleep_time = sleep_time * 2;
        }
    }
}

struct refresh_status_bars {
    refresh_status_bars(std::shared_ptr<top_status_source> top_source)
        : rsb_top_source(std::move(top_source))
    {
    }

    using injectable
        = refresh_status_bars(std::shared_ptr<top_status_source> top_source);

    void doit() const
    {
        struct timeval current_time {};
        int ch;

        gettimeofday(&current_time, nullptr);
        while ((ch = getch()) != ERR) {
            lnav_data.ld_user_message_source.clear();

            alerter::singleton().new_input(ch);

            lnav_data.ld_input_dispatcher.new_input(current_time, ch);

            lnav_data.ld_view_stack.top() | [ch](auto tc) {
                lnav_data.ld_key_repeat_history.update(ch, tc->get_top());
            };

            if (!lnav_data.ld_looping) {
                // No reason to keep processing input after the
                // user has quit.  The view stack will also be
                // empty, which will cause issues.
                break;
            }
        }

        this->rsb_top_source->update_time(current_time);
        for (auto& sc : lnav_data.ld_status) {
            sc.do_update();
        }
        lnav_data.ld_rl_view->do_update();
        if (handle_winch()) {
            layout_views();
            lnav_data.ld_view_stack.do_update();
        }
        refresh();
    }

    std::shared_ptr<top_status_source> rsb_top_source;
};

static void
check_for_file_zones()
{
    auto with_tz_count = 0;
    std::vector<std::string> without_tz_files;

    for (const auto& lf : lnav_data.ld_active_files.fc_files) {
        auto format = lf->get_format_ptr();
        if (format == nullptr) {
            continue;
        }

        if (format->lf_timestamp_flags & ETF_ZONE_SET
            || format->lf_date_time.dts_default_zone != nullptr)
        {
            with_tz_count += 1;
        } else {
            without_tz_files.emplace_back(lf->get_unique_path());
        }
    }
    if (with_tz_count > 0 && !without_tz_files.empty()) {
        auto note
            = attr_line_t("The file(s) without a zone: ")
                  .join(
                      without_tz_files, VC_ROLE.value(role_t::VCR_FILE), ", ");
        auto um
            = lnav::console::user_message::warning(
                  "Some messages may not be sorted by time correctly")
                  .with_reason(
                      "There are one or more files whose messages do not have "
                      "a timezone in their timestamps mixed in with files that "
                      "do have timezones")
                  .with_note(note)
                  .with_help(
                      attr_line_t("Use the ")
                          .append(":set-file-timezone"_symbol)
                          .append(
                              " command to set the zone for messages in files "
                              "that do not include a zone in the timestamp"));

        lnav_data.ld_exec_context.ec_error_callback_stack.back()(um);
    }
}

static void
looper()
{
    static auto* ps = injector::get<pollable_supervisor*>();
    static auto* filter_source = injector::get<filter_sub_source*>();
    static auto* breadcrumb_view = injector::get<breadcrumb_curses*>();

    try {
        auto* sql_cmd_map = injector::get<readline_context::command_map_t*,
                                          sql_cmd_map_tag>();
        auto& ec = lnav_data.ld_exec_context;

        readline_context command_context("cmd", &lnav_commands);

        readline_context search_context("search", nullptr, false);
        readline_context search_filters_context(
            "search-filters", nullptr, false);
        readline_context search_files_context("search-files", nullptr, false);
        readline_context search_spectro_details_context(
            "search-spectro-details", nullptr, false);
        readline_context index_context("capture");
        readline_context sql_context("sql", sql_cmd_map, false);
        readline_context exec_context("exec");
        readline_context user_context("user");
        auto rlc = injector::get<std::shared_ptr<readline_curses>>();
        sig_atomic_t overlay_counter = 0;
        int lpc;

        command_context.set_highlighter(readline_command_highlighter);
        search_context.set_append_character(0).set_highlighter(
            readline_regex_highlighter);
        search_filters_context.set_append_character(0).set_highlighter(
            readline_regex_highlighter);
        search_files_context.set_append_character(0).set_highlighter(
            readline_regex_highlighter);
        search_spectro_details_context.set_append_character(0).set_highlighter(
            readline_regex_highlighter);
        sql_context.set_highlighter(readline_sqlite_highlighter)
            .set_quote_chars("\"")
            .with_readline_var((char**) &rl_completer_word_break_characters,
                               " \t\n(),")
            .with_splitter(prql_splitter);
        exec_context.set_highlighter(readline_shlex_highlighter);

        lnav_data.ld_log_source.lss_sorting_observer
            = [](auto& lss, auto off, auto size) {
                  if (off == size) {
                      lnav_data.ld_bottom_source.update_loading(0, 0);
                  } else {
                      lnav_data.ld_bottom_source.update_loading(off, size);
                  }
                  do_observer_update(nullptr);
              };

        auto& sb = lnav_data.ld_scroll_broadcaster;
        auto& vsb = lnav_data.ld_view_stack_broadcaster;

        rlc->add_context(ln_mode_t::COMMAND, command_context);
        rlc->add_context(ln_mode_t::SEARCH, search_context);
        rlc->add_context(ln_mode_t::SEARCH_FILTERS, search_filters_context);
        rlc->add_context(ln_mode_t::SEARCH_FILES, search_files_context);
        rlc->add_context(ln_mode_t::SEARCH_SPECTRO_DETAILS,
                         search_spectro_details_context);
        rlc->add_context(ln_mode_t::CAPTURE, index_context);
        rlc->add_context(ln_mode_t::SQL, sql_context);
        rlc->add_context(ln_mode_t::EXEC, exec_context);
        rlc->add_context(ln_mode_t::USER, user_context);
        rlc->set_save_history(!(lnav_data.ld_flags & LNF_SECURE_MODE));
        rlc->start();

        filter_source->fss_editor->start();

        lnav_data.ld_rl_view = rlc.get();

        lnav_data.ld_rl_view->add_possibility(
            ln_mode_t::COMMAND, "viewname", lnav_view_strings);

        lnav_data.ld_rl_view->add_possibility(
            ln_mode_t::COMMAND, "zoomlevel", lnav_zoom_strings);

        lnav_data.ld_rl_view->add_possibility(
            ln_mode_t::COMMAND, "levelname", level_names);

        auto echo_views_stmt_res = prepare_stmt(lnav_data.ld_db,
#if SQLITE_VERSION_NUMBER < 3033000
                                                R"(
        UPDATE lnav_views_echo
          SET top = (SELECT top FROM lnav_views WHERE lnav_views.name = lnav_views_echo.name),
              left = (SELECT left FROM lnav_views WHERE lnav_views.name = lnav_views_echo.name),
              height = (SELECT height FROM lnav_views WHERE lnav_views.name = lnav_views_echo.name),
              inner_height = (SELECT inner_height FROM lnav_views WHERE lnav_views.name = lnav_views_echo.name),
              top_time = (SELECT top_time FROM lnav_views WHERE lnav_views.name = lnav_views_echo.name),
              search = (SELECT search FROM lnav_views WHERE lnav_views.name = lnav_views_echo.name)
          WHERE EXISTS (SELECT * FROM lnav_views WHERE name = lnav_views_echo.name AND
                    (
                        lnav_views.top != lnav_views_echo.top OR
                        lnav_views.left != lnav_views_echo.left OR
                        lnav_views.height != lnav_views_echo.height OR
                        lnav_views.inner_height != lnav_views_echo.inner_height OR
                        lnav_views.top_time != lnav_views_echo.top_time OR
                        lnav_views.search != lnav_views_echo.search
                    ))
        )"
#else
                                                R"(
        UPDATE lnav_views_echo
          SET top = orig.top,
              left = orig.left,
              height = orig.height,
              inner_height = orig.inner_height,
              top_time = orig.top_time,
              search = orig.search
          FROM (SELECT * FROM lnav_views) AS orig
          WHERE orig.name = lnav_views_echo.name AND
                (
                    orig.top != lnav_views_echo.top OR
                    orig.left != lnav_views_echo.left OR
                    orig.height != lnav_views_echo.height OR
                    orig.inner_height != lnav_views_echo.inner_height OR
                    orig.top_time != lnav_views_echo.top_time OR
                    orig.search != lnav_views_echo.search
                )
        )"
#endif
        );

        if (echo_views_stmt_res.isErr()) {
            lnav::console::print(
                stderr,
                lnav::console::user_message::error(
                    "unable to prepare UPDATE statement for lnav_views_echo "
                    "table")
                    .with_reason(echo_views_stmt_res.unwrapErr()));
            return;
        }
        auto echo_views_stmt = echo_views_stmt_res.unwrap();

        if (xterm_mouse::is_available()
            && lnav_config.lc_mouse_mode == lnav_mouse_mode::disabled)
        {
            auto mouse_note = prepare_stmt(lnav_data.ld_db, R"(
INSERT INTO lnav_user_notifications (id, priority, expiration, message)
VALUES ('org.lnav.mouse-support', -1, DATETIME('now', '+1 minute'),
        'Press <span class="-lnav_status-styles_hotkey">F2</span> to enable mouse support');
)");
            if (mouse_note.isErr()) {
                lnav::console::print(
                    stderr,
                    lnav::console::user_message::error(
                        "unable to prepare INSERT statement for "
                        "lnav_user_notifications table")
                        .with_reason(mouse_note.unwrapErr()));
                return;
            }

            mouse_note.unwrap().execute();
        }

        (void) signal(SIGINT, sigint);
        (void) signal(SIGTERM, sigint);
        (void) signal(SIGWINCH, sigwinch);

        auto create_screen_res = screen_curses::create();

        if (create_screen_res.isErr()) {
            log_error("create screen failed with: %s",
                      create_screen_res.unwrapErr().c_str());
            lnav::console::print(
                stderr,
                lnav::console::user_message::error("unable to open TUI")
                    .with_reason(create_screen_res.unwrapErr()));
            return;
        }

        auto sc = create_screen_res.unwrap();

        auto_fd errpipe[2];
        auto_fd::pipe(errpipe);

        errpipe[0].close_on_exec();
        errpipe[1].close_on_exec();
        dup2(errpipe[1], STDERR_FILENO);
        errpipe[1].reset();
        log_pipe_err(errpipe[0]);
        lnav_behavior lb;

        ui_periodic_timer::singleton();

        auto& mouse_i = injector::get<xterm_mouse&>();

        mouse_i.set_behavior(&lb);
        mouse_i.set_enabled(check_experimental("mouse")
                            || lnav_config.lc_mouse_mode
                                == lnav_mouse_mode::enabled);

        lnav_data.ld_window = sc.get_window();
        keypad(stdscr, TRUE);
        (void) nonl();
        (void) cbreak();
        (void) noecho();
        (void) nodelay(lnav_data.ld_window, 1);

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

        view_colors& vc = view_colors::singleton();
        view_colors::init(false);

        auto ecb_guard
            = lnav_data.ld_exec_context.add_error_callback([](const auto& um) {
                  auto al = um.to_attr_line().rtrim();

                  if (al.get_string().find('\n') == std::string::npos) {
                      if (lnav_data.ld_rl_view) {
                          lnav_data.ld_rl_view->set_attr_value(al);
                      }
                  } else {
                      lnav_data.ld_user_message_source.replace_with(al);
                      lnav_data.ld_user_message_view.reload_data();
                      lnav_data.ld_user_message_expiration
                          = std::chrono::steady_clock::now() + 20s;
                  }
              });

        {
            setup_highlights(lnav_data.ld_views[LNV_LOG].get_highlights());
            setup_highlights(lnav_data.ld_views[LNV_TEXT].get_highlights());
            setup_highlights(lnav_data.ld_views[LNV_SCHEMA].get_highlights());
            setup_highlights(lnav_data.ld_views[LNV_PRETTY].get_highlights());
            setup_highlights(lnav_data.ld_preview_view[0].get_highlights());
            setup_highlights(lnav_data.ld_preview_view[1].get_highlights());

            for (const auto& format : log_format::get_root_formats()) {
                for (auto& hl : format->lf_highlighters) {
                    if (hl.h_fg.empty() && hl.h_bg.empty()
                        && hl.h_attrs.empty())
                    {
                        hl.with_attrs(hl.h_attrs
                                      | vc.attrs_for_ident(hl.h_name));
                    }

                    lnav_data.ld_views[LNV_LOG].get_highlights()[{
                        highlight_source_t::CONFIGURATION,
                        format->get_name().to_string() + "-" + hl.h_name}]
                        = hl;
                }
            }
        }

        execute_examples();

        rlc->set_window(lnav_data.ld_window);
        rlc->set_focus_action(rl_focus);
        rlc->set_change_action(rl_change);
        rlc->set_perform_action(rl_callback);
        rlc->set_alt_perform_action(rl_alt_callback);
        rlc->set_timeout_action(rl_search);
        rlc->set_abort_action(lnav_rl_abort);
        rlc->set_display_match_action(rl_display_matches);
        rlc->set_display_next_action(rl_display_next);
        rlc->set_blur_action(rl_blur);
        rlc->set_completion_request_action(rl_completion_request);
        rlc->set_alt_value(
            HELP_MSG_2(e,
                       E,
                       "to move forward/backward through " ANSI_COLOR(
                           COLOR_RED) "error" ANSI_NORM " messages"));

        (void) curs_set(0);

        lnav_data.ld_view_stack.push_back(&lnav_data.ld_views[LNV_LOG]);

        sb.push_back(clear_last_user_mark);
        sb.push_back(update_view_position);
        vsb.push_back(
            bind_mem(&term_extra::update_title, injector::get<term_extra*>()));
        vsb.push_back([](listview_curses* lv) {
            auto* tc = dynamic_cast<textview_curses*>(lv);

            tc->tc_state_event_handler(*tc);
        });

        vsb.push_back(sb);

        breadcrumb_view->on_focus
            = [](breadcrumb_curses&) { set_view_mode(ln_mode_t::BREADCRUMBS); };
        breadcrumb_view->on_blur = [](breadcrumb_curses&) {
            set_view_mode(ln_mode_t::PAGING);
            lnav_data.ld_view_stack.set_needs_update();
        };
        breadcrumb_view->set_y(1);
        breadcrumb_view->set_window(lnav_data.ld_window);
        breadcrumb_view->set_line_source(lnav_crumb_source);
        auto event_handler = [](auto&& tc) {
            auto top_view = lnav_data.ld_view_stack.top();

            if (top_view && *top_view == &tc) {
                lnav_data.ld_bottom_source.update_search_term(tc);
            }
        };
        for (lpc = 0; lpc < LNV__MAX; lpc++) {
            lnav_data.ld_views[lpc].set_window(lnav_data.ld_window);
            lnav_data.ld_views[lpc].set_y(2);
            lnav_data.ld_views[lpc].set_height(
                vis_line_t(-(rlc->get_height() + 3)));
            lnav_data.ld_views[lpc].set_scroll_action(sb);
            lnav_data.ld_views[lpc].set_search_action(update_hits);
            lnav_data.ld_views[lpc].tc_cursor_role = role_t::VCR_CURSOR_LINE;
            lnav_data.ld_views[lpc].tc_disabled_cursor_role
                = role_t::VCR_DISABLED_CURSOR_LINE;
            lnav_data.ld_views[lpc].tc_state_event_handler = event_handler;
        }
        lnav_data.ld_views[LNV_DB].set_supports_marks(true);
        lnav_data.ld_views[LNV_HELP].set_supports_marks(true);
        lnav_data.ld_views[LNV_HISTOGRAM].set_supports_marks(true);
        lnav_data.ld_views[LNV_LOG].set_supports_marks(true);
        lnav_data.ld_views[LNV_TEXT].set_supports_marks(true);
        lnav_data.ld_views[LNV_SCHEMA].set_supports_marks(true);
        lnav_data.ld_views[LNV_PRETTY].set_supports_marks(true);

        lnav_data.ld_doc_view.set_window(lnav_data.ld_window);
        lnav_data.ld_doc_view.set_show_scrollbar(false);

        lnav_data.ld_example_view.set_window(lnav_data.ld_window);
        lnav_data.ld_example_view.set_show_scrollbar(false);

        lnav_data.ld_match_view.set_window(lnav_data.ld_window);

        lnav_data.ld_preview_view[0].set_window(lnav_data.ld_window);
        lnav_data.ld_preview_view[0].set_show_scrollbar(false);
        lnav_data.ld_preview_view[1].set_window(lnav_data.ld_window);
        lnav_data.ld_preview_view[1].set_show_scrollbar(false);

        lnav_data.ld_filter_view.set_title("Text Filters");
        lnav_data.ld_filter_view.set_selectable(true);
        lnav_data.ld_filter_view.set_window(lnav_data.ld_window);
        lnav_data.ld_filter_view.set_show_scrollbar(true);

        lnav_data.ld_files_view.set_title("Files");
        lnav_data.ld_files_view.set_selectable(true);
        lnav_data.ld_files_view.set_window(lnav_data.ld_window);
        lnav_data.ld_files_view.set_show_scrollbar(true);
        lnav_data.ld_files_view.get_disabled_highlights().insert(
            highlight_source_t::THEME);
        lnav_data.ld_files_view.set_overlay_source(&lnav_data.ld_files_overlay);

        lnav_data.ld_user_message_view.set_window(lnav_data.ld_window);

        lnav_data.ld_spectro_details_view.set_title("spectro-details");
        lnav_data.ld_spectro_details_view.set_window(lnav_data.ld_window);
        lnav_data.ld_spectro_details_view.set_show_scrollbar(true);
        lnav_data.ld_spectro_details_view.set_height(5_vl);
        lnav_data.ld_spectro_details_view.set_sub_source(
            &lnav_data.ld_spectro_no_details_source);
        lnav_data.ld_spectro_details_view.tc_state_event_handler
            = event_handler;
        lnav_data.ld_spectro_details_view.set_scroll_action(sb);
        lnav_data.ld_spectro_no_details_source.replace_with(
            attr_line_t().append(
                lnav::roles::comment(" No details available")));
        lnav_data.ld_spectro_source->ss_details_view
            = &lnav_data.ld_spectro_details_view;
        lnav_data.ld_spectro_source->ss_no_details_source
            = &lnav_data.ld_spectro_no_details_source;
        lnav_data.ld_spectro_source->ss_exec_context
            = &lnav_data.ld_exec_context;

        lnav_data.ld_gantt_details_view.set_title("gantt-details");
        lnav_data.ld_gantt_details_view.set_window(lnav_data.ld_window);
        lnav_data.ld_gantt_details_view.set_show_scrollbar(false);
        lnav_data.ld_gantt_details_view.set_height(5_vl);
        lnav_data.ld_gantt_details_view.set_sub_source(
            &lnav_data.ld_gantt_details_source);

        auto top_status_lifetime
            = injector::bind<top_status_source>::to_scoped_singleton();

        auto top_source = injector::get<std::shared_ptr<top_status_source>>();

        lnav_data.ld_bottom_source.get_field(bottom_status_source::BSF_HELP)
            .on_click
            = [](status_field&) { ensure_view(&lnav_data.ld_views[LNV_HELP]); };
        lnav_data.ld_bottom_source
            .get_field(bottom_status_source::BSF_LINE_NUMBER)
            .on_click
            = [](status_field&) {
                  auto cmd = fmt::format(
                      FMT_STRING("prompt command : 'goto {}'"),
                      (int) lnav_data.ld_view_stack.top().value()->get_top());

                  execute_command(lnav_data.ld_exec_context, cmd);
              };
        lnav_data.ld_bottom_source
            .get_field(bottom_status_source::BSF_SEARCH_TERM)
            .on_click
            = [](status_field&) {
                  auto term = lnav_data.ld_view_stack.top()
                                  .value()
                                  ->get_current_search();
                  auto cmd
                      = fmt::format(FMT_STRING("prompt search / '{}'"), term);

                  execute_command(lnav_data.ld_exec_context, cmd);
              };

        lnav_data.ld_status[LNS_TOP].set_y(0);
        lnav_data.ld_status[LNS_TOP].set_default_role(
            role_t::VCR_INACTIVE_STATUS);
        lnav_data.ld_status[LNS_TOP].set_data_source(top_source.get());
        lnav_data.ld_status[LNS_BOTTOM].set_y(-(rlc->get_height() + 1));
        for (auto& stat_bar : lnav_data.ld_status) {
            stat_bar.set_window(lnav_data.ld_window);
        }
        lnav_data.ld_status[LNS_BOTTOM].set_data_source(
            &lnav_data.ld_bottom_source);
        lnav_data.ld_status[LNS_FILTER].set_data_source(
            &lnav_data.ld_filter_status_source);
        lnav_data.ld_status[LNS_FILTER_HELP].set_data_source(
            &lnav_data.ld_filter_help_status_source);
        lnav_data.ld_status[LNS_DOC].set_data_source(
            &lnav_data.ld_doc_status_source);
        lnav_data.ld_status[LNS_PREVIEW0].set_data_source(
            &lnav_data.ld_preview_status_source[0]);
        lnav_data.ld_status[LNS_PREVIEW1].set_data_source(
            &lnav_data.ld_preview_status_source[1]);
        lnav_data.ld_spectro_status_source
            = std::make_unique<spectro_status_source>();
        lnav_data.ld_status[LNS_SPECTRO].set_data_source(
            lnav_data.ld_spectro_status_source.get());
        lnav_data.ld_status[LNS_GANTT].set_enabled(false);
        lnav_data.ld_status[LNS_GANTT].set_data_source(
            &lnav_data.ld_gantt_status_source);

        lnav_data.ld_match_view.set_show_bottom_border(true);
        lnav_data.ld_user_message_view.set_show_bottom_border(true);

        for (auto& sc : lnav_data.ld_status) {
            sc.window_change();
        }

        auto session_path = lnav::paths::dotlnav() / "session";
        execute_file(ec, session_path.string());

        sb(*lnav_data.ld_view_stack.top());
        vsb(*lnav_data.ld_view_stack.top());

        lnav_data.ld_view_stack.vs_change_handler = [](textview_curses* tc) {
            lnav_data.ld_view_stack_broadcaster(tc);
        };

        {
            auto& id = lnav_data.ld_input_dispatcher;

            id.id_escape_matcher = match_escape_seq;
            id.id_escape_handler = handle_keyseq;
            id.id_key_handler = handle_key;
            id.id_mouse_handler
                = std::bind(&xterm_mouse::handle_mouse, &mouse_i);
            id.id_unhandled_handler = [](const char* keyseq) {
                auto enc_len = lnav_config.lc_ui_keymap.size() * 2;
                auto encoded_name = (char*) alloca(enc_len);

                log_info("unbound keyseq: %s", keyseq);
                json_ptr::encode(
                    encoded_name, enc_len, lnav_config.lc_ui_keymap.c_str());
                // XXX we should have a hotkey for opening a prompt that is
                // pre-filled with a suggestion that the user can complete.
                // This quick-fix key could be used for other stuff as well
                lnav_data.ld_rl_view->set_value(fmt::format(
                    FMT_STRING(ANSI_CSI ANSI_COLOR_PARAM(
                        COLOR_YELLOW) ";" ANSI_BOLD_PARAM ANSI_CHAR_ATTR
                                      "Unrecognized key" ANSI_NORM
                                      ", bind to a command using "
                                      "\u2014 " ANSI_BOLD(
                                          ":config") " /ui/keymap-defs/{}/{}/"
                                                     "command <cmd>"),
                    encoded_name,
                    keyseq));
                alerter::singleton().chime("unrecognized key");
            };
        }

        auto refresher_lifetime
            = injector::bind<refresh_status_bars>::to_scoped_singleton();

        auto refresher = injector::get<std::shared_ptr<refresh_status_bars>>();

        auto refresh_guard = lnav_data.ld_status_refresher.install(
            [refresher]() { refresher->doit(); });

        auto& timer = ui_periodic_timer::singleton();
        struct timeval current_time;

        static sig_atomic_t index_counter;

        lnav_data.ld_mode = ln_mode_t::FILES;

        timer.start_fade(index_counter, 1);

        std::future<file_collection> rescan_future;

        log_debug("rescan started");
        rescan_future = std::async(std::launch::async,
                                   &file_collection::rescan_files,
                                   lnav_data.ld_active_files.copy(),
                                   false);
        bool initial_rescan_completed = false;
        int session_stage = 0;

        // rlc.do_update();

        auto next_rebuild_time = ui_clock::now();
        auto next_status_update_time = next_rebuild_time;
        auto next_rescan_time = next_rebuild_time;

        while (lnav_data.ld_looping) {
            auto loop_deadline
                = ui_clock::now() + (session_stage == 0 ? 3s : 50ms);

            std::vector<struct pollfd> pollfds;
            size_t starting_view_stack_size = lnav_data.ld_view_stack.size();
            size_t changes = 0;
            int rc;

            gettimeofday(&current_time, nullptr);

            top_source->update_time(current_time);
            lnav_data.ld_preview_view[0].set_needs_update();
            lnav_data.ld_preview_view[1].set_needs_update();

            layout_views();

            auto scan_timeout = initial_rescan_completed ? 0s : 10ms;
            if (rescan_future.valid()
                && rescan_future.wait_for(scan_timeout)
                    == std::future_status::ready)
            {
                auto ui_now = ui_clock::now();
                auto new_files = rescan_future.get();
                if (!initial_rescan_completed && new_files.empty()) {
                    initial_rescan_completed = true;

                    log_debug("initial rescan rebuild");
                    auto rebuild_res = rebuild_indexes(loop_deadline);
                    changes += rebuild_res.rir_changes;
                    load_session();
                    if (session_data.sd_save_time) {
                        std::string ago;

                        ago = humanize::time::point::from_tv(
                                  {(time_t) session_data.sd_save_time, 0})
                                  .as_time_ago();
                        auto um = lnav::console::user_message::ok(
                            attr_line_t("restored session from ")
                                .append(lnav::roles::number(ago))
                                .append("; press ")
                                .append("CTRL-R"_hotkey)
                                .append(" to reset session"));
                        lnav_data.ld_rl_view->set_attr_value(um.to_attr_line());
                    }

                    lnav_data.ld_session_loaded = true;
                    session_stage += 1;
                    loop_deadline = ui_now;
                    log_debug("file count %d",
                              lnav_data.ld_active_files.fc_files.size());
                }
                update_active_files(new_files);
                if (!initial_rescan_completed) {
                    auto& fview = lnav_data.ld_files_view;
                    auto height = fview.get_inner_height();

                    if (height > 0_vl) {
                        fview.set_selection(height - 1_vl);
                    }
                }

                rescan_future = std::future<file_collection>{};
                next_rescan_time = ui_now + 333ms;
            }

            if (!rescan_future.valid()
                && (session_stage < 2
                    || (lnav_data.ld_active_files.is_below_open_file_limit()
                        && ui_clock::now() >= next_rescan_time)))
            {
                rescan_future = std::async(std::launch::async,
                                           &file_collection::rescan_files,
                                           lnav_data.ld_active_files.copy(),
                                           false);
                loop_deadline = ui_clock::now() + 10ms;
            }

            {
                auto& mlooper = injector::get<main_looper&, services::main_t>();

                mlooper.get_port().process_for(0s);
            }

            auto ui_now = ui_clock::now();
            if (initial_rescan_completed) {
                if (ui_now >= next_rebuild_time) {
                    auto text_file_count = lnav_data.ld_text_source.size();
                    auto rebuild_res = rebuild_indexes(loop_deadline);
                    changes += rebuild_res.rir_changes;
                    if (!changes && ui_clock::now() < loop_deadline) {
                        next_rebuild_time = ui_clock::now() + 333ms;
                    }
                    if (changes && text_file_count
                        && lnav_data.ld_text_source.empty()
                        && lnav_data.ld_view_stack.top().value_or(nullptr)
                            == &lnav_data.ld_views[LNV_TEXT])
                    {
                        do {
                            lnav_data.ld_view_stack.pop_back();
                        } while (lnav_data.ld_view_stack.top().value_or(nullptr)
                                 != &lnav_data.ld_views[LNV_LOG]);
                    }
                }
            } else {
                lnav_data.ld_files_view.set_overlay_needs_update();
            }

            if (lnav_data.ld_mode == ln_mode_t::BREADCRUMBS
                && breadcrumb_view->get_needs_update())
            {
                lnav_data.ld_view_stack.set_needs_update();
            }
            if (lnav_data.ld_view_stack.do_update()) {
                breadcrumb_view->set_needs_update();
            }
            lnav_data.ld_doc_view.do_update();
            lnav_data.ld_example_view.do_update();
            lnav_data.ld_match_view.do_update();
            lnav_data.ld_preview_view[0].do_update();
            lnav_data.ld_preview_view[1].do_update();
            lnav_data.ld_spectro_details_view.do_update();
            lnav_data.ld_gantt_details_view.do_update();
            lnav_data.ld_user_message_view.do_update();
            if (ui_clock::now() >= next_status_update_time) {
                echo_views_stmt.execute();
                top_source->update_user_msg();
                for (auto& sc : lnav_data.ld_status) {
                    sc.do_update();
                }
                next_status_update_time = ui_clock::now() + 100ms;
            }
            if (filter_source->fss_editing) {
                filter_source->fss_match_view.set_needs_update();
            }
            breadcrumb_view->do_update();
            // These updates need to be done last so their readline views can
            // put the cursor in the right place.
            switch (lnav_data.ld_mode) {
                case ln_mode_t::FILTER:
                case ln_mode_t::SEARCH_FILTERS:
                    lnav_data.ld_filter_view.set_needs_update();
                    lnav_data.ld_filter_view.do_update();
                    break;
                case ln_mode_t::SEARCH_FILES:
                case ln_mode_t::FILES:
                    lnav_data.ld_files_view.set_needs_update();
                    lnav_data.ld_files_view.do_update();
                    break;
                default:
                    break;
            }
            if (lnav_data.ld_mode != ln_mode_t::FILTER
                && lnav_data.ld_mode != ln_mode_t::FILES)
            {
                rlc->do_update();
            }
            refresh();

            if (lnav_data.ld_session_loaded) {
                // Only take input from the user after everything has loaded.
                pollfds.push_back((struct pollfd){STDIN_FILENO, POLLIN, 0});
                if (lnav_data.ld_initial_build) {
                    switch (lnav_data.ld_mode) {
                        case ln_mode_t::COMMAND:
                        case ln_mode_t::SEARCH:
                        case ln_mode_t::SEARCH_FILTERS:
                        case ln_mode_t::SEARCH_FILES:
                        case ln_mode_t::SEARCH_SPECTRO_DETAILS:
                        case ln_mode_t::SQL:
                        case ln_mode_t::EXEC:
                        case ln_mode_t::USER:
                            if (rlc->consume_ready_for_input()) {
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

            ps->update_poll_set(pollfds);
            ui_now = ui_clock::now();
            auto poll_to
                = (!changes && ui_now < loop_deadline && session_stage >= 1)
                ? std::chrono::duration_cast<std::chrono::milliseconds>(
                      loop_deadline - ui_now)
                : 0ms;

            if (initial_rescan_completed
                && lnav_data.ld_input_dispatcher.in_escape() && poll_to > 15ms)
            {
                poll_to = 15ms;
            }
            // log_debug("poll %d %d", changes, poll_to.count());
            rc = poll(&pollfds[0], pollfds.size(), poll_to.count());

            gettimeofday(&current_time, nullptr);
            lnav_data.ld_input_dispatcher.poll(current_time);

            if (lb.lb_last_view != nullptr) {
                lb.lb_last_event.me_time = current_time;
                lb.lb_last_view->handle_mouse(lb.lb_last_event);
            }

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
            } else {
                auto in_revents = pollfd_revents(pollfds, STDIN_FILENO);

                if (in_revents & (POLLHUP | POLLNVAL)) {
                    log_info("stdin has been closed, exiting...");
                    lnav_data.ld_looping = false;
                } else if (in_revents & POLLIN) {
                    int ch;

                    auto old_gen
                        = lnav_data.ld_active_files.fc_files_generation;
                    while ((ch = getch()) != ERR) {
                        lnav_data.ld_user_message_source.clear();

                        alerter::singleton().new_input(ch);

                        lnav_data.ld_input_dispatcher.new_input(current_time,
                                                                ch);

                        lnav_data.ld_view_stack.top() | [ch](auto tc) {
                            lnav_data.ld_key_repeat_history.update(
                                ch, tc->get_top());
                        };

                        if (!lnav_data.ld_looping) {
                            // No reason to keep processing input after the
                            // user has quit.  The view stack will also be
                            // empty, which will cause issues.
                            break;
                        }
                    }

                    next_status_update_time = ui_clock::now();
                    switch (lnav_data.ld_mode) {
                        case ln_mode_t::PAGING:
                        case ln_mode_t::FILTER:
                        case ln_mode_t::FILES:
                        case ln_mode_t::SPECTRO_DETAILS:
                        case ln_mode_t::BUSY:
                            if (old_gen
                                == lnav_data.ld_active_files
                                       .fc_files_generation)
                            {
                                next_rescan_time = next_status_update_time + 1s;
                            } else {
                                next_rescan_time = next_status_update_time;
                            }
                            break;
                        case ln_mode_t::BREADCRUMBS:
                        case ln_mode_t::COMMAND:
                        case ln_mode_t::SEARCH:
                        case ln_mode_t::SEARCH_FILTERS:
                        case ln_mode_t::SEARCH_FILES:
                        case ln_mode_t::SEARCH_SPECTRO_DETAILS:
                        case ln_mode_t::CAPTURE:
                        case ln_mode_t::SQL:
                        case ln_mode_t::EXEC:
                        case ln_mode_t::USER:
                            next_rescan_time = next_status_update_time + 1min;
                            break;
                    }
                    next_rebuild_time = next_rescan_time;
                }

                auto old_mode = lnav_data.ld_mode;
                auto old_file_names_size
                    = lnav_data.ld_active_files.fc_file_names.size();

                ps->check_poll_set(pollfds);
                lnav_data.ld_view_stack.top() |
                    [](auto tc) { lnav_data.ld_bottom_source.update_hits(tc); };

                if (lnav_data.ld_mode != old_mode) {
                    switch (lnav_data.ld_mode) {
                        case ln_mode_t::PAGING:
                        case ln_mode_t::FILTER:
                        case ln_mode_t::FILES:
                            next_rescan_time = next_status_update_time;
                            next_rebuild_time = next_rescan_time;
                            break;
                        default:
                            break;
                    }
                }
                if (old_file_names_size
                        != lnav_data.ld_active_files.fc_file_names.size()
                    || lnav_data.ld_active_files.finished_pipers() > 0)
                {
                    next_rescan_time = ui_clock::now();
                    next_rebuild_time = next_rescan_time;
                    next_status_update_time = next_rescan_time;
                }
            }

            if (timer.time_to_update(overlay_counter)) {
                lnav_data.ld_view_stack.top() |
                    [](auto tc) { tc->set_overlay_needs_update(); };
            }

            if (initial_rescan_completed && session_stage < 2
                && (!lnav_data.ld_initial_build
                    || timer.fade_diff(index_counter) == 0))
            {
                if (lnav_data.ld_mode == ln_mode_t::PAGING) {
                    timer.start_fade(index_counter, 1);
                } else {
                    timer.start_fade(index_counter, 3);
                }
                // log_debug("initial build rebuild");
                auto rebuild_res = rebuild_indexes(loop_deadline);
                changes += rebuild_res.rir_changes;
                if (lnav_data.ld_view_stack.top().value_or(nullptr)
                        == &lnav_data.ld_views[LNV_TEXT]
                    && lnav_data.ld_text_source.empty()
                    && lnav_data.ld_log_source.text_line_count() > 0)
                {
                    auto* tc_log = &lnav_data.ld_views[LNV_LOG];
                    lnav_data.ld_view_stack.pop_back();

                    lnav_data.ld_views[LNV_LOG].set_top(
                        tc_log->get_top_for_last_row());
                }
                if (!lnav_data.ld_initial_build
                    && lnav_data.ld_log_source.text_line_count() == 0
                    && !lnav_data.ld_active_files.fc_other_files.empty()
                    && std::any_of(
                        lnav_data.ld_active_files.fc_other_files.begin(),
                        lnav_data.ld_active_files.fc_other_files.end(),
                        [](const auto& pair) {
                            return pair.second.ofd_format
                                == file_format_t::SQLITE_DB;
                        }))
                {
                    ensure_view(&lnav_data.ld_views[LNV_SCHEMA]);
                }

                if (!lnav_data.ld_initial_build && lnav_data.ld_show_help_view)
                {
                    toggle_view(&lnav_data.ld_views[LNV_HELP]);
                    lnav_data.ld_initial_build = true;
                }
                if (!lnav_data.ld_initial_build
                    && lnav_data.ld_active_files.fc_file_names.empty())
                {
                    lnav_data.ld_initial_build = true;
                }
                if (rebuild_res.rir_completed
                    && (lnav_data.ld_log_source.text_line_count() > 0
                        || lnav_data.ld_text_source.text_line_count() > 0
                        || lnav_data.ld_active_files.other_file_format_count(
                               file_format_t::SQLITE_DB)
                            > 0))
                {
                    log_debug("initial build completed");
                    lnav_data.ld_initial_build = true;
                }

                if (lnav_data.ld_initial_build) {
                    static bool ran_cleanup = false;
                    std::vector<std::pair<
                        Result<std::string, lnav::console::user_message>,
                        std::string>>
                        cmd_results;

                    execute_init_commands(ec, cmd_results);

                    if (!cmd_results.empty()) {
                        auto last_cmd_result = cmd_results.back();

                        if (last_cmd_result.first.isOk()) {
                            lnav_data.ld_rl_view->set_value(
                                last_cmd_result.first.unwrap());
                        } else {
                            ec.ec_error_callback_stack.back()(
                                last_cmd_result.first.unwrapErr());
                        }
                        lnav_data.ld_rl_view->set_alt_value(
                            last_cmd_result.second);
                    }

                    if (!ran_cleanup) {
                        line_buffer::cleanup_cache();
                        archive_manager::cleanup_cache();
                        tailer::cleanup_cache();
                        lnav::piper::cleanup();
                        file_converter_manager::cleanup();
                        ran_cleanup = true;
                    }
                }

                if (session_stage == 1 && lnav_data.ld_initial_build
                    && (lnav_data.ld_active_files.fc_file_names.empty()
                        || lnav_data.ld_log_source.text_line_count() > 0
                        || lnav_data.ld_text_source.text_line_count() > 0
                        || !lnav_data.ld_active_files.fc_other_files.empty()))
                {
                    lnav::session::restore_view_states();
                    if (lnav_data.ld_mode == ln_mode_t::FILES) {
                        if (lnav_data.ld_log_source.text_line_count() == 0
                            && lnav_data.ld_text_source.text_line_count() > 0
                            && lnav_data.ld_view_stack.size() == 1)
                        {
                            log_debug("no logs, just text...");
                            ensure_view(&lnav_data.ld_views[LNV_TEXT]);
                            lnav_data.ld_rl_view->set_alt_value(HELP_MSG_2(
                                f, F, "to switch to the next/previous file"));
                        }
                        if (lnav_data.ld_active_files.fc_name_to_errors
                                ->readAccess()
                                ->empty())
                        {
                            log_info("switching to paging!");
                            lnav_data.ld_mode = ln_mode_t::PAGING;
                            lnav_data.ld_active_files.fc_files
                                | lnav::itertools::for_each(
                                    &logfile::dump_stats);

                            check_for_file_zones();
                        } else {
                            lnav_data.ld_files_view.set_selection(0_vl);
                        }
                    }
                    session_stage += 1;
                    lnav_data.ld_exec_phase = lnav_exec_phase::INTERACTIVE;
                    load_time_bookmarks();
                }
            }

            handle_winch();

            if (lnav_data.ld_child_terminated) {
                lnav_data.ld_child_terminated = false;

                log_info("checking for terminated child processes");
                for (auto iter = lnav_data.ld_children.begin();
                     iter != lnav_data.ld_children.end();
                     ++iter)
                {
                    int rc, child_stat;

                    rc = waitpid(*iter, &child_stat, WNOHANG);
                    if (rc == -1 || rc == 0) {
                        continue;
                    }

                    iter = lnav_data.ld_children.erase(iter);
                }

                gather_pipers();

                next_rescan_time = ui_clock::now();
                next_rebuild_time = next_rescan_time;
                next_status_update_time = next_rescan_time;
            }

            if (lnav_data.ld_view_stack.empty()
                || (lnav_data.ld_view_stack.size() == 1
                    && starting_view_stack_size == 2
                    && lnav_data.ld_active_files.fc_file_names.size()
                        == lnav_data.ld_text_source.size()))
            {
                lnav_data.ld_looping = false;
            }

            if (lnav_data.ld_sigint_count > 0) {
                bool found_piper = false;

                lnav_data.ld_sigint_count = 0;
                if (!lnav_data.ld_view_stack.empty()) {
                    auto* tc = *lnav_data.ld_view_stack.top();

                    if (tc->get_inner_height() > 0_vl) {
                        std::vector<attr_line_t> rows(1);

                        tc->get_data_source()->listview_value_for_rows(
                            *tc, tc->get_selection(), rows);
                        auto& sa = rows[0].get_attrs();
                        auto line_attr_opt
                            = get_string_attr(sa, logline::L_FILE);
                        if (line_attr_opt) {
                            auto lf = line_attr_opt.value().get();

                            log_debug("file name when SIGINT: %s",
                                      lf->get_filename().c_str());
                            for (auto& cp : lnav_data.ld_child_pollers) {
                                auto cp_name = cp.get_filename();

                                if (!cp_name) {
                                    log_debug("no child_poller");
                                    continue;
                                }

                                if (lf->get_filename() == cp_name.value()) {
                                    log_debug("found it, sending signal!");
                                    cp.send_sigint();
                                    found_piper = true;
                                }
                            }
                        }
                    }
                }
                if (!found_piper) {
                    lnav_data.ld_looping = false;
                }
            }
        }
    } catch (readline_curses::error& e) {
        log_error("error: %s", strerror(e.e_err));
    }
}

void
wait_for_children()
{
    std::vector<struct pollfd> pollfds;
    struct timeval to = {0, 333000};
    static auto* ps = injector::get<pollable_supervisor*>();

    do {
        pollfds.clear();

        auto update_res = ps->update_poll_set(pollfds);

        if (update_res.ur_background == 0) {
            break;
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

        ps->check_poll_set(pollfds);
        lnav_data.ld_view_stack.top() |
            [](auto tc) { lnav_data.ld_bottom_source.update_hits(tc); };
    } while (lnav_data.ld_looping);
}

struct mode_flags_t {
    bool mf_check_configs{false};
    bool mf_install{false};
    bool mf_update_formats{false};
    bool mf_no_default{false};
    bool mf_print_warnings{false};
};

static int
print_user_msgs(std::vector<lnav::console::user_message> error_list,
                mode_flags_t mf)
{
    size_t warning_count = 0;
    int retval = EXIT_SUCCESS;

    for (auto& iter : error_list) {
        FILE* out_file;

        switch (iter.um_level) {
            case lnav::console::user_message::level::raw:
            case lnav::console::user_message::level::ok:
                out_file = stdout;
                break;
            case lnav::console::user_message::level::warning:
                warning_count += 1;
                if (!mf.mf_print_warnings) {
                    continue;
                }
                out_file = stderr;
                break;
            default:
                out_file = stderr;
                break;
        }

        lnav::console::print(out_file, iter);
        if (iter.um_level == lnav::console::user_message::level::error) {
            retval = EXIT_FAILURE;
        }
    }

    if (warning_count > 0 && !mf.mf_print_warnings
        && !(lnav_data.ld_flags & LNF_HEADLESS)
        && (std::chrono::system_clock::now() - lnav_data.ld_last_dot_lnav_time
            > 24h))
    {
        lnav::console::print(
            stderr,
            lnav::console::user_message::warning(
                attr_line_t()
                    .append(lnav::roles::number(fmt::to_string(warning_count)))
                    .append(" issues were detected when checking lnav's "
                            "configuration"))
                .with_help(
                    attr_line_t("pass ")
                        .append(lnav::roles::symbol("-W"))
                        .append(" on the command line to display the issues\n")
                        .append("(this message will only be displayed once a "
                                "day)")));
    }

    return retval;
}

verbosity_t verbosity = verbosity_t::standard;

int
main(int argc, char* argv[])
{
    std::vector<lnav::console::user_message> config_errors;
    std::vector<lnav::console::user_message> loader_errors;
    auto& ec = lnav_data.ld_exec_context;
    int retval = EXIT_SUCCESS;

    bool exec_stdin = false, load_stdin = false;
    mode_flags_t mode_flags;
    const char* LANG = getenv("LANG");

    if (LANG == nullptr || strcmp(LANG, "C") == 0) {
        setenv("LANG", "en_US.UTF-8", 1);
    }

    ec.ec_label_source_stack.push_back(&lnav_data.ld_db_row_source);

    (void) signal(SIGPIPE, SIG_IGN);
    (void) signal(SIGCHLD, sigchld);
    setlocale(LC_ALL, "");
    try {
        std::locale::global(std::locale(""));
    } catch (const std::runtime_error& e) {
        log_error("unable to set locale to ''");
    }
    umask(027);

    /* Disable Lnav from being able to execute external commands if
     * "LNAVSECURE" environment variable is set by the user.
     */
    if (getenv("LNAVSECURE") != nullptr) {
        lnav_data.ld_flags |= LNF_SECURE_MODE;
    }

    // Set PAGER so that stuff run from `:sh` will just dump their
    // output for lnav to display.  One example would be `man`, as
    // in `:sh man ls`.
    setenv("PAGER", "cat", 1);
    setenv("LNAV_HOME_DIR", lnav::paths::dotlnav().c_str(), 1);
    setenv("LNAV_WORK_DIR", lnav::paths::workdir().c_str(), 1);

    try {
        auto& safe_options_hier
            = injector::get<lnav::safe_file_options_hier&>();

        auto opt_path = lnav::paths::dotlnav() / "file-options.json";
        auto read_res = lnav::filesystem::read_file(opt_path);
        auto curr_tz = date::get_tzdb().current_zone();
        auto options_coll = lnav::file_options_collection{};

        if (read_res.isOk()) {
            intern_string_t opt_path_src = intern_string::lookup(opt_path);
            auto parse_res = lnav::file_options_collection::from_json(
                opt_path_src, read_res.unwrap());
            if (parse_res.isErr()) {
                for (const auto& um : parse_res.unwrapErr()) {
                    lnav::console::print(stderr, um);
                }
                return EXIT_FAILURE;
            }

            options_coll = parse_res.unwrap();
        }

        safe::WriteAccess<lnav::safe_file_options_hier> options_hier(
            safe_options_hier);

        options_hier->foh_generation += 1;
        auto_mem<char> var_path;

        var_path = realpath("/var/log", nullptr);
        options_coll.foc_pattern_to_options[fmt::format(FMT_STRING("{}/*"),
                                                        var_path.in())]
            = lnav::file_options{
                {
                    intern_string_t{},
                    source_location{},
                    curr_tz,
                },
            };
        options_hier->foh_path_to_collection.emplace(ghc::filesystem::path("/"),
                                                     options_coll);
    } catch (const std::runtime_error& e) {
        log_error("failed to setup tz: %s", e.what());
    }

    lnav_data.ld_exec_context.ec_sql_callback = sql_callback;
    lnav_data.ld_exec_context.ec_pipe_callback = pipe_callback;

    lnav_data.ld_program_name = argv[0];
    add_ansi_vars(ec.ec_global_vars);

    rl_readline_name = "lnav";
    lnav_data.ld_db_key_names = DEFAULT_DB_KEY_NAMES;

    stable_sort(lnav_data.ld_db_key_names.begin(),
                lnav_data.ld_db_key_names.end());

    auto dot_lnav_path = lnav::paths::dotlnav();
    std::error_code last_write_ec;
    lnav_data.ld_last_dot_lnav_time
        = ghc::filesystem::last_write_time(dot_lnav_path, last_write_ec);

    ensure_dotlnav();

    log_install_handlers();
    sql_install_logger();

    if (sqlite3_open(":memory:", lnav_data.ld_db.out()) != SQLITE_OK) {
        fprintf(stderr, "error: unable to create sqlite memory database\n");
        exit(EXIT_FAILURE);
    }

    {
        int register_collation_functions(sqlite3 * db);

        register_sqlite_funcs(lnav_data.ld_db.in(), sqlite_registration_funcs);
        register_collation_functions(lnav_data.ld_db.in());
    }

    register_environ_vtab(lnav_data.ld_db.in());
    register_static_file_vtab(lnav_data.ld_db.in());
    {
        static auto vtab_modules
            = injector::get<std::vector<std::shared_ptr<vtab_module_base>>>();

        for (const auto& mod : vtab_modules) {
            mod->create(lnav_data.ld_db.in());
        }
    }

    register_views_vtab(lnav_data.ld_db.in());
    register_regexp_vtab(lnav_data.ld_db.in());
    register_xpath_vtab(lnav_data.ld_db.in());
    register_fstat_vtab(lnav_data.ld_db.in());
    lnav::events::register_events_tab(lnav_data.ld_db.in());

    auto _vtab_cleanup = finally([] {
        static const char* VIRT_TABLES = R"(
SELECT tbl_name FROM sqlite_master WHERE sql LIKE 'CREATE VIRTUAL TABLE%'
)";

        lnav_data.ld_child_pollers.clear();

        for (auto& lf : lnav_data.ld_active_files.fc_files) {
            lf->close();
        }
        rebuild_indexes(ui_clock::now());

        lnav_data.ld_vtab_manager = nullptr;

        std::vector<std::string> tables_to_drop;
        {
            auto_mem<sqlite3_stmt> stmt(sqlite3_finalize);
            bool done = false;

            sqlite3_prepare_v2(
                lnav_data.ld_db.in(), VIRT_TABLES, -1, stmt.out(), nullptr);
            do {
                auto ret = sqlite3_step(stmt.in());

                switch (ret) {
                    case SQLITE_OK:
                    case SQLITE_DONE:
                        done = true;
                        break;
                    case SQLITE_ROW:
                        tables_to_drop.emplace_back(fmt::format(
                            FMT_STRING("DROP TABLE {}"),
                            reinterpret_cast<const char*>(
                                sqlite3_column_text(stmt.in(), 0))));
                        break;
                }
            } while (!done);
        }

        // XXX
        lnav_data.ld_log_source.set_preview_sql_filter(nullptr);
        lnav_data.ld_log_source.set_sql_filter("", nullptr);
        lnav_data.ld_log_source.set_sql_marker("", nullptr);
        lnav_config_listener::unload_all();

        {
            sqlite3_stmt* stmt_iter = nullptr;

            do {
                stmt_iter = sqlite3_next_stmt(lnav_data.ld_db.in(), stmt_iter);
                if (stmt_iter != nullptr) {
                    const auto* stmt_sql = sqlite3_sql(stmt_iter);

                    log_warning("unfinalized SQL statement: %s", stmt_sql);
                    ensure(false);
                }
            } while (stmt_iter != nullptr);
        }

        for (auto& drop_stmt : tables_to_drop) {
            sqlite3_exec(lnav_data.ld_db.in(),
                         drop_stmt.c_str(),
                         nullptr,
                         nullptr,
                         nullptr);
        }
#if defined(HAVE_SQLITE3_DROP_MODULES)
        sqlite3_drop_modules(lnav_data.ld_db.in(), nullptr);
#endif

        lnav_data.ld_db.reset();
    });

#ifdef HAVE_LIBCURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
#endif

    static const std::string DEFAULT_DEBUG_LOG = "/dev/null";

    lnav_data.ld_debug_log_name = DEFAULT_DEBUG_LOG;

    std::vector<std::string> file_args;
    std::vector<lnav::console::user_message> arg_errors;

    CLI::App app{"The Logfile Navigator"};

    app.add_option("-d",
                   lnav_data.ld_debug_log_name,
                   "Write debug messages to the given file.")
        ->type_name("FILE");
    app.add_option("-I", lnav_data.ld_config_paths, "include paths")
        ->check(CLI::ExistingDirectory)
        ->check([&arg_errors](std::string inc_path) -> std::string {
            if (access(inc_path.c_str(), X_OK) != 0) {
                arg_errors.emplace_back(
                    lnav::console::user_message::error(
                        attr_line_t("invalid configuration directory: ")
                            .append(lnav::roles::file(inc_path)))
                        .with_errno_reason());
                return "unreadable";
            }

            return std::string();
        })
        ->allow_extra_args(false);
    app.add_flag("-q{0},-v{2}", verbosity, "Control the verbosity");
    app.set_version_flag("-V,--version");
    app.footer(fmt::format(FMT_STRING("Version: {}"), VCS_PACKAGE_STRING));

    std::shared_ptr<lnav::management::operations> mmode_ops;

    if (argc < 2 || strcmp(argv[1], "-m") != 0) {
        app.add_flag("-H", lnav_data.ld_show_help_view, "show help");
        app.add_flag("-C", mode_flags.mf_check_configs, "check");
        auto* install_flag
            = app.add_flag("-i", mode_flags.mf_install, "install");
        app.add_flag("-u", mode_flags.mf_update_formats, "update");
        auto* no_default_flag
            = app.add_flag("-N", mode_flags.mf_no_default, "no def");
        auto* rotated_flag = app.add_flag(
            "-R", lnav_data.ld_active_files.fc_rotated, "rotated");
        auto* recurse_flag = app.add_flag(
            "-r", lnav_data.ld_active_files.fc_recursive, "recurse");
        auto* as_log_flag
            = app.add_flag("-t", lnav_data.ld_treat_stdin_as_log, "as-log");
        app.add_flag("-W", mode_flags.mf_print_warnings);
        auto* headless_flag = app.add_flag(
            "-n",
            [](size_t count) { lnav_data.ld_flags |= LNF_HEADLESS; },
            "headless");
        auto* file_opt = app.add_option("file", file_args, "files");

        auto wait_cb = [](size_t count) {
            char b;
            if (isatty(STDIN_FILENO) && read(STDIN_FILENO, &b, 1) == -1) {
                perror("Read key from STDIN");
            }
        };
        app.add_flag("-S", wait_cb);

        auto cmd_appender
            = [](std::string cmd) { lnav_data.ld_commands.emplace_back(cmd); };
        auto cmd_validator = [&arg_errors](std::string cmd) -> std::string {
            static const auto ARG_SRC
                = intern_string::lookup("command-line argument");

            if (cmd.empty()) {
                return "empty commands are not allowed";
            }

            switch (cmd[0]) {
                case ':':
                case '/':
                case ';':
                case '|':
                    break;
                default:
                    cmd.push_back(' ');
                    arg_errors.emplace_back(
                        lnav::console::user_message::error(
                            attr_line_t("invalid value for ")
                                .append_quoted("-c"_symbol)
                                .append(" option"))
                            .with_snippet(lnav::console::snippet::from(
                                ARG_SRC,
                                attr_line_t()
                                    .append(" -c "_quoted_code)
                                    .append(lnav::roles::quoted_code(cmd))
                                    .append("\n")
                                    .append(4, ' ')
                                    .append(lnav::roles::error(
                                        "^ command type prefix "
                                        "is missing"))))
                            .with_help(command_arg_help()));
                    return "invalid prefix";
            }
            return std::string();
        };
        auto* cmd_opt = app.add_option("-c")
                            ->check(cmd_validator)
                            ->each(cmd_appender)
                            ->allow_extra_args(false)
                            ->trigger_on_parse(true);

        auto file_appender = [](std::string file_path) {
            lnav_data.ld_commands.emplace_back(
                fmt::format(FMT_STRING("|{}"), file_path));
        };
        auto* exec_file_opt = app.add_option("-f")
                                  ->trigger_on_parse(true)
                                  ->allow_extra_args(false)
                                  ->each(file_appender);

        auto shexec_appender = [&mode_flags](std::string cmd) {
            mode_flags.mf_no_default = true;
            lnav_data.ld_commands.emplace_back(
                fmt::format(FMT_STRING(":sh {}"), cmd));
        };
        auto* cmdline_opt = app.add_option("-e")
                                ->each(shexec_appender)
                                ->allow_extra_args(false)
                                ->trigger_on_parse(true);

        install_flag->needs(file_opt);
        install_flag->excludes(no_default_flag,
                               as_log_flag,
                               rotated_flag,
                               recurse_flag,
                               headless_flag,
                               cmd_opt,
                               exec_file_opt,
                               cmdline_opt);
    }

    auto is_mmode = argc >= 2 && strcmp(argv[1], "-m") == 0;
    try {
        if (is_mmode) {
            mmode_ops = lnav::management::describe_cli(app, argc, argv);
        } else {
            app.parse(argc, argv);
        }
    } catch (const CLI::CallForHelp& e) {
        if (is_mmode) {
            fmt::print(FMT_STRING("{}\n"), app.help());
        } else {
            usage();
        }
        return EXIT_SUCCESS;
    } catch (const CLI::CallForVersion& e) {
        fmt::print(FMT_STRING("{}\n"), VCS_PACKAGE_STRING);
        return EXIT_SUCCESS;
    } catch (const CLI::ParseError& e) {
        if (!arg_errors.empty()) {
            print_user_msgs(arg_errors, mode_flags);
            return e.get_exit_code();
        }

        lnav::console::print(
            stderr,
            lnav::console::user_message::error("invalid command-line arguments")
                .with_reason(e.what()));
        return e.get_exit_code();
    }

    lnav_data.ld_config_paths.insert(lnav_data.ld_config_paths.begin(),
                                     lnav::paths::dotlnav());
    lnav_data.ld_config_paths.insert(lnav_data.ld_config_paths.begin(),
                                     SYSCONFDIR "/lnav");
    lnav_data.ld_config_paths.insert(lnav_data.ld_config_paths.begin(),
                                     "/etc/lnav");

    if (lnav_data.ld_debug_log_name != DEFAULT_DEBUG_LOG) {
        lnav_log_level = lnav_log_level_t::TRACE;
    }

    lnav_log_file = make_optional_from_nullable(
        fopen(lnav_data.ld_debug_log_name.c_str(), "ae"));
    lnav_log_file |
        [](auto* file) { fcntl(fileno(file), F_SETFD, FD_CLOEXEC); };
    log_info("lnav started");

    {
        static auto builtin_formats
            = injector::get<std::vector<std::shared_ptr<log_format>>>();
        auto& root_formats = log_format::get_root_formats();

        log_format::get_root_formats().insert(root_formats.begin(),
                                              builtin_formats.begin(),
                                              builtin_formats.end());
        builtin_formats.clear();
    }

    load_config(lnav_data.ld_config_paths, config_errors);

    if (!config_errors.empty()) {
        if (print_user_msgs(config_errors, mode_flags) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }
    }
    add_global_vars(ec);

    if (mode_flags.mf_update_formats) {
        if (!update_installs_from_git()) {
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    if (mode_flags.mf_install) {
        auto formats_installed_path
            = lnav::paths::dotlnav() / "formats/installed";
        auto configs_installed_path
            = lnav::paths::dotlnav() / "configs/installed";

        if (argc == 0) {
            const auto install_reason
                = attr_line_t("the ")
                      .append("-i"_symbol)
                      .append(
                          " option expects one or more log format "
                          "definition "
                          "files to install in your lnav "
                          "configuration "
                          "directory");
            const auto install_help
                = attr_line_t(
                      "log format definitions are JSON files that "
                      "tell lnav "
                      "how to understand log files\n")
                      .append(
                          "See: "
                          "https://docs.lnav.org/en/latest/"
                          "formats.html");

            lnav::console::print(stderr,
                                 lnav::console::user_message::error(
                                     "missing format files to install")
                                     .with_reason(install_reason)
                                     .with_help(install_help));
            return EXIT_FAILURE;
        }

        for (const auto& file_path : file_args) {
            if (endswith(file_path, ".git")) {
                if (!install_from_git(file_path)) {
                    return EXIT_FAILURE;
                }
                continue;
            }

            if (endswith(file_path, ".sql")) {
                auto sql_path = ghc::filesystem::path(file_path);
                auto read_res = lnav::filesystem::read_file(sql_path);
                if (read_res.isErr()) {
                    lnav::console::print(
                        stderr,
                        lnav::console::user_message::error(
                            attr_line_t("unable to read SQL file: ")
                                .append(lnav::roles::file(file_path)))
                            .with_reason(read_res.unwrapErr()));
                    return EXIT_FAILURE;
                }

                auto dst_path = formats_installed_path / sql_path.filename();
                auto write_res
                    = lnav::filesystem::write_file(dst_path, read_res.unwrap());
                if (write_res.isErr()) {
                    lnav::console::print(
                        stderr,
                        lnav::console::user_message::error(
                            attr_line_t("unable to write SQL file: ")
                                .append(lnav::roles::file(file_path)))
                            .with_reason(write_res.unwrapErr()));
                    return EXIT_FAILURE;
                }

                lnav::console::print(
                    stderr,
                    lnav::console::user_message::ok(
                        attr_line_t("installed -- ")
                            .append(lnav::roles::file(dst_path))));
                continue;
            }

            if (file_path == "extra") {
                install_extra_formats();
                continue;
            }

            auto file_type_result = detect_config_file_type(file_path);
            if (file_type_result.isErr()) {
                lnav::console::print(
                    stderr,
                    lnav::console::user_message::error(
                        attr_line_t("unable to open configuration file: ")
                            .append(lnav::roles::file(file_path)))
                        .with_reason(file_type_result.unwrapErr()));
                return EXIT_FAILURE;
            }
            auto file_type = file_type_result.unwrap();

            auto src_path = ghc::filesystem::path(file_path);
            ghc::filesystem::path dst_name;
            if (file_type == config_file_type::CONFIG) {
                dst_name = src_path.filename();
            } else {
                auto format_list = load_format_file(src_path, loader_errors);

                if (!loader_errors.empty()) {
                    if (print_user_msgs(loader_errors, mode_flags)
                        != EXIT_SUCCESS)
                    {
                        return EXIT_FAILURE;
                    }
                }
                if (format_list.empty()) {
                    lnav::console::print(
                        stderr,
                        lnav::console::user_message::error(
                            attr_line_t("invalid format file: ")
                                .append(lnav::roles::file(src_path.string())))
                            .with_reason("there must be at least one format "
                                         "definition in the file"));
                    return EXIT_FAILURE;
                }

                dst_name = format_list[0].to_string() + ".json";
            }
            auto dst_path = (file_type == config_file_type::CONFIG
                                 ? configs_installed_path
                                 : formats_installed_path)
                / dst_name;

            auto read_res = lnav::filesystem::read_file(file_path);
            if (read_res.isErr()) {
                auto um = lnav::console::user_message::error(
                              attr_line_t("cannot read file to install -- ")
                                  .append(lnav::roles::file(file_path)))
                              .with_reason(read_res.unwrap());

                lnav::console::print(stderr, um);
                return EXIT_FAILURE;
            }

            auto file_content = read_res.unwrap();

            auto read_dst_res = lnav::filesystem::read_file(dst_path);
            if (read_dst_res.isOk()) {
                auto dst_content = read_dst_res.unwrap();

                if (dst_content == file_content) {
                    auto um = lnav::console::user_message::info(
                        attr_line_t("file is already installed at -- ")
                            .append(lnav::roles::file(dst_path)));

                    lnav::console::print(stdout, um);

                    return EXIT_SUCCESS;
                }
            }

            auto write_res = lnav::filesystem::write_file(
                dst_path,
                file_content,
                {lnav::filesystem::write_file_options::backup_existing});
            if (write_res.isErr()) {
                auto um = lnav::console::user_message::error(
                              attr_line_t("failed to install file to -- ")
                                  .append(lnav::roles::file(dst_path)))
                              .with_reason(write_res.unwrapErr());

                lnav::console::print(stderr, um);
                return EXIT_FAILURE;
            }

            auto write_file_res = write_res.unwrap();
            auto um = lnav::console::user_message::ok(
                attr_line_t("installed -- ")
                    .append(lnav::roles::file(dst_path)));
            if (write_file_res.wfr_backup_path) {
                um.with_note(
                    attr_line_t("the previously installed ")
                        .append_quoted(
                            lnav::roles::file(dst_path.filename().string()))
                        .append(" was backed up to -- ")
                        .append(lnav::roles::file(
                            write_file_res.wfr_backup_path.value().string())));
            }

            lnav::console::print(stdout, um);
        }
        return EXIT_SUCCESS;
    }

    if (lnav_data.ld_flags & LNF_SECURE_MODE) {
        if ((sqlite3_set_authorizer(
                lnav_data.ld_db.in(), sqlite_authorizer, nullptr))
            != SQLITE_OK)
        {
            fprintf(stderr, "error: unable to attach sqlite authorizer\n");
            exit(EXIT_FAILURE);
        }
    }

    /* If we statically linked against an ncurses library that had a
     * non-standard path to the terminfo database, we need to set this
     * variable so that it will try the default path.
     */
    setenv("TERMINFO_DIRS",
           "/usr/share/terminfo:/lib/terminfo:/usr/share/lib/terminfo",
           0);

    auto* filter_source = injector::get<filter_sub_source*>();
    lnav_data.ld_vtab_manager = std::make_unique<log_vtab_manager>(
        lnav_data.ld_db, lnav_data.ld_views[LNV_LOG], lnav_data.ld_log_source);

    lnav_data.ld_log_source.set_exec_context(&lnav_data.ld_exec_context);
    lnav_data.ld_views[LNV_HELP]
        .set_sub_source(&lnav_data.ld_help_source)
        .set_word_wrap(false);
    auto log_fos = new field_overlay_source(lnav_data.ld_log_source,
                                            lnav_data.ld_text_source);
    log_fos->fos_contexts.emplace("", false, true, true);
    lnav_data.ld_views[LNV_LOG]
        .set_sub_source(&lnav_data.ld_log_source)
#if 0
        .set_delegate(std::make_shared<action_delegate>(
            lnav_data.ld_log_source,
            [](auto child_pid) { lnav_data.ld_children.push_back(child_pid); },
            [](const auto& desc, auto pp) {
                lnav_data.ld_pipers.push_back(pp);
                lnav_data.ld_active_files.fc_file_names[desc].with_fd(
                    pp->get_fd());
                lnav_data.ld_files_to_front.template emplace_back(desc, 0_vl);
            }))
#endif
        .add_input_delegate(lnav_data.ld_log_source)
        .set_tail_space(2_vl)
        .set_overlay_source(log_fos);
    auto sel_reload_delegate = [](textview_curses& tc) {
        if (!(lnav_data.ld_flags & LNF_HEADLESS)
            && lnav_config.lc_ui_movement.mode == config_movement_mode::CURSOR)
        {
            tc.set_selectable(true);
        }
    };
    lnav_data.ld_views[LNV_LOG].set_reload_config_delegate(sel_reload_delegate);
    lnav_data.ld_views[LNV_PRETTY].set_reload_config_delegate(
        sel_reload_delegate);
    auto text_header_source
        = std::make_shared<textfile_header_overlay>(&lnav_data.ld_text_source);
    lnav_data.ld_views[LNV_TEXT].set_overlay_source(text_header_source.get());
    lnav_data.ld_views[LNV_TEXT].set_sub_source(&lnav_data.ld_text_source);
    lnav_data.ld_views[LNV_TEXT].set_reload_config_delegate(
        sel_reload_delegate);
    lnav_data.ld_views[LNV_HISTOGRAM]
        .set_reload_config_delegate(sel_reload_delegate)
        .set_sub_source(&lnav_data.ld_hist_source2);
    lnav_data.ld_views[LNV_DB].set_sub_source(&lnav_data.ld_db_row_source);
    lnav_data.ld_db_overlay.dos_labels = &lnav_data.ld_db_row_source;
    lnav_data.ld_db_preview_overlay_source[0].dos_labels
        = &lnav_data.ld_db_preview_source[0];
    lnav_data.ld_db_preview_overlay_source[1].dos_labels
        = &lnav_data.ld_db_preview_source[1];
    lnav_data.ld_views[LNV_DB]
        .set_reload_config_delegate(sel_reload_delegate)
        .set_overlay_source(&lnav_data.ld_db_overlay);
    lnav_data.ld_spectro_source = std::make_unique<spectrogram_source>();
    lnav_data.ld_views[LNV_SPECTRO]
        .set_reload_config_delegate(sel_reload_delegate)
        .set_sub_source(lnav_data.ld_spectro_source.get())
        .set_overlay_source(lnav_data.ld_spectro_source.get())
        .add_input_delegate(*lnav_data.ld_spectro_source)
        .set_tail_space(4_vl);
    lnav_data.ld_views[LNV_SPECTRO].set_selectable(true);
    auto gantt_view_source
        = std::make_shared<gantt_source>(lnav_data.ld_views[LNV_LOG],
                                         lnav_data.ld_log_source,
                                         lnav_data.ld_gantt_details_source,
                                         lnav_data.ld_gantt_status_source);
    gantt_view_source->gs_exec_context = &lnav_data.ld_exec_context;
    auto gantt_header_source
        = std::make_shared<gantt_header_overlay>(gantt_view_source);
    lnav_data.ld_views[LNV_GANTT]
        .set_sub_source(gantt_view_source.get())
        .set_overlay_source(gantt_header_source.get())
        .set_tail_space(4_vl);
    lnav_data.ld_views[LNV_GANTT].set_selectable(true);

    auto _gantt_cleanup = finally([] {
        lnav_data.ld_views[LNV_GANTT].set_sub_source(nullptr);
        lnav_data.ld_views[LNV_GANTT].set_overlay_source(nullptr);
    });

    lnav_data.ld_doc_view.set_sub_source(&lnav_data.ld_doc_source);
    lnav_data.ld_example_view.set_sub_source(&lnav_data.ld_example_source);
    lnav_data.ld_match_view.set_sub_source(&lnav_data.ld_match_source);
    lnav_data.ld_preview_view[0].set_sub_source(
        &lnav_data.ld_preview_source[0]);
    lnav_data.ld_filter_view.set_sub_source(filter_source)
        .add_input_delegate(*filter_source);
    lnav_data.ld_files_view.set_sub_source(&lnav_data.ld_files_source)
        .add_input_delegate(lnav_data.ld_files_source);
    lnav_data.ld_user_message_view.set_sub_source(
        &lnav_data.ld_user_message_source);

    for (int lpc = 0; lpc < LNV__MAX; lpc++) {
        lnav_data.ld_views[lpc].set_gutter_source(new log_gutter_source());
    }

    {
        hist_source2& hs = lnav_data.ld_hist_source2;

        lnav_data.ld_log_source.set_index_delegate(new hist_index_delegate(
            lnav_data.ld_hist_source2, lnav_data.ld_views[LNV_HISTOGRAM]));
        hs.init();
        lnav_data.ld_zoom_level = 3;
        hs.set_time_slice(ZOOM_LEVELS[lnav_data.ld_zoom_level]);
    }

    for (int lpc = 0; lpc < LNV__MAX; lpc++) {
        lnav_data.ld_views[lpc].set_title(lnav_view_titles[lpc]);
    }

    load_formats(lnav_data.ld_config_paths, loader_errors);

    {
        auto_mem<char, sqlite3_free> errmsg;

        if (sqlite3_exec(lnav_data.ld_db.in(),
                         init_sql.to_string_fragment().data(),
                         nullptr,
                         nullptr,
                         errmsg.out())
            != SQLITE_OK)
        {
            fprintf(stderr,
                    "error: unable to execute DB init -- %s\n",
                    errmsg.in());
        }
    }

    lnav_data.ld_vtab_manager->register_vtab(std::make_shared<all_logs_vtab>());
    lnav_data.ld_vtab_manager->register_vtab(
        std::make_shared<log_format_vtab_impl>(
            *log_format::find_root_format("generic_log")));
    lnav_data.ld_vtab_manager->register_vtab(
        std::make_shared<log_format_vtab_impl>(
            *log_format::find_root_format("lnav_piper_log")));

    for (auto& iter : log_format::get_root_formats()) {
        auto lvi = iter->get_vtab_impl();

        if (lvi != nullptr) {
            lnav_data.ld_vtab_manager->register_vtab(lvi);
        }
    }

    load_format_extra(lnav_data.ld_db.in(),
                      ec.ec_global_vars,
                      lnav_data.ld_config_paths,
                      loader_errors);
    load_format_vtabs(lnav_data.ld_vtab_manager.get(), loader_errors);

    if (!loader_errors.empty()) {
        if (print_user_msgs(loader_errors, mode_flags) != EXIT_SUCCESS) {
            if (mmode_ops == nullptr) {
                return EXIT_FAILURE;
            }
        }
    }

    if (mmode_ops) {
        auto perform_res = lnav::management::perform(mmode_ops);

        return print_user_msgs(perform_res, mode_flags);
    }

    if (!mode_flags.mf_check_configs && !lnav_data.ld_show_help_view) {
        DEFAULT_FILES.emplace_back("var/log/messages");
        DEFAULT_FILES.emplace_back("var/log/system.log");
        DEFAULT_FILES.emplace_back("var/log/syslog");
        DEFAULT_FILES.emplace_back("var/log/syslog.log");
    }

    init_lnav_commands(lnav_commands);

    lnav_data.ld_looping = true;
    lnav_data.ld_mode = ln_mode_t::PAGING;

    if ((isatty(STDIN_FILENO) || is_dev_null(STDIN_FILENO)) && file_args.empty()
        && lnav_data.ld_active_files.fc_file_names.empty()
        && !mode_flags.mf_no_default)
    {
        char start_dir[FILENAME_MAX];

        if (getcwd(start_dir, sizeof(start_dir)) == nullptr) {
            perror("getcwd");
        } else {
            do {
                if (!append_default_files()) {
                    retval = EXIT_FAILURE;
                }
            } while (lnav_data.ld_active_files.fc_file_names.empty()
                     && change_to_parent_dir());

            if (chdir(start_dir) == -1) {
                perror("chdir(start_dir)");
            }
        }
    }

    {
        const auto internals_dir_opt = getenv_opt("DUMP_INTERNALS_DIR");

        if (internals_dir_opt) {
            lnav::dump_internals(internals_dir_opt.value());

            return EXIT_SUCCESS;
        }
    }

    if (file_args.empty()) {
        load_stdin = true;
    }

    for (const auto& file_path_str : file_args) {
        auto file_path_without_trailer = file_path_str;
        auto file_loc = file_location_t{mapbox::util::no_init{}};
        auto_mem<char> abspath;
        struct stat st;

        auto colon_index = file_path_str.rfind(':');
        if (colon_index != std::string::npos) {
            auto top_range = scn::string_view{&file_path_str[colon_index + 1],
                                              &(*file_path_str.cend())};
            auto scan_res = scn::scan_value<int>(top_range);

            if (scan_res) {
                file_path_without_trailer
                    = file_path_str.substr(0, colon_index);
                file_loc = vis_line_t(scan_res.value());
            } else {
                log_warning(
                    "failed to parse line number from file path "
                    "with colon: %s",
                    file_path_str.c_str());
            }
        }
        auto hash_index = file_path_str.rfind('#');
        if (hash_index != std::string::npos) {
            file_loc = file_path_str.substr(hash_index);
            file_path_without_trailer = file_path_str.substr(0, hash_index);
        }
        auto file_path = ghc::filesystem::path(
            stat(file_path_without_trailer.c_str(), &st) == 0
                ? file_path_without_trailer
                : file_path_str);

        if (file_path_str == "-") {
            load_stdin = true;
        }
#ifdef HAVE_LIBCURL
        else if (is_url(file_path_str))
        {
            auto ul = std::make_shared<url_loader>(file_path_str);

            lnav_data.ld_active_files.fc_file_names[ul->get_path()]
                .with_filename(file_path);
            isc::to<curl_looper&, services::curl_streamer_t>().send(
                [ul](auto& clooper) { clooper.add_request(ul); });
        } else if (file_path_str.find("://") != std::string::npos) {
            lnav_data.ld_commands.insert(
                lnav_data.ld_commands.begin(),
                fmt::format(FMT_STRING(":open {}"), file_path_str));
        }
#endif
        else if (lnav::filesystem::is_glob(file_path))
        {
            lnav_data.ld_active_files.fc_file_names[file_path].with_tail(
                !(lnav_data.ld_flags & LNF_HEADLESS));
        } else if (lnav::filesystem::statp(file_path, &st) == -1) {
            if (file_path_str.find(':') != std::string::npos) {
                lnav_data.ld_active_files.fc_file_names[file_path].with_tail(
                    !(lnav_data.ld_flags & LNF_HEADLESS));
            } else {
                lnav::console::print(
                    stderr,
                    lnav::console::user_message::error(
                        attr_line_t("unable to open file: ")
                            .append(lnav::roles::file(file_path)))
                        .with_errno_reason());
                retval = EXIT_FAILURE;
            }
        } else if (access(file_path.c_str(), R_OK) == -1) {
            lnav::console::print(
                stderr,
                lnav::console::user_message::error(
                    attr_line_t("file exists, but is not readable: ")
                        .append(lnav::roles::file(file_path)))
                    .with_errno_reason());
            retval = EXIT_FAILURE;
        } else if (S_ISFIFO(st.st_mode)) {
            auto_fd fifo_fd;

            if ((fifo_fd = lnav::filesystem::openp(file_path, O_RDONLY)) == -1)
            {
                lnav::console::print(
                    stderr,
                    lnav::console::user_message::error(
                        attr_line_t("cannot open fifo: ")
                            .append(lnav::roles::file(file_path)))
                        .with_errno_reason());
                retval = EXIT_FAILURE;
            } else {
                auto desc = fmt::format(FMT_STRING("FIFO [{}]"),
                                        lnav_data.ld_fifo_counter++);
                auto create_piper_res = lnav::piper::create_looper(
                    desc, std::move(fifo_fd), auto_fd{});

                if (create_piper_res.isOk()) {
                    lnav_data.ld_active_files.fc_file_names[desc].with_piper(
                        create_piper_res.unwrap());
                }
            }
        } else if ((abspath = realpath(file_path.c_str(), nullptr)) == nullptr)
        {
            perror("Cannot find file");
            retval = EXIT_FAILURE;
        } else if (S_ISDIR(st.st_mode)) {
            std::string dir_wild(abspath.in());

            if (dir_wild[dir_wild.size() - 1] == '/') {
                dir_wild.resize(dir_wild.size() - 1);
            }
            lnav_data.ld_active_files.fc_file_names.emplace(
                dir_wild + "/*", logfile_open_options());
        } else {
            lnav_data.ld_active_files.fc_file_names.emplace(
                abspath.in(),
                logfile_open_options().with_init_location(file_loc).with_tail(
                    !(lnav_data.ld_flags & LNF_HEADLESS)));
            if (file_loc.valid()) {
                lnav_data.ld_files_to_front.emplace_back(abspath.in(),
                                                         file_loc);
            }
        }
    }

    if (mode_flags.mf_check_configs) {
        rescan_files(true);
        for (auto& lf : lnav_data.ld_active_files.fc_files) {
            logfile::rebuild_result_t rebuild_result;

            do {
                rebuild_result = lf->rebuild_index();
            } while (rebuild_result == logfile::rebuild_result_t::NEW_LINES
                     || rebuild_result == logfile::rebuild_result_t::NEW_ORDER);
            auto fmt = lf->get_format();
            if (fmt == nullptr) {
                fprintf(stderr,
                        "error:%s:no format found for file\n",
                        lf->get_filename().c_str());
                retval = EXIT_FAILURE;
                continue;
            }
            for (auto line_iter = lf->begin(); line_iter != lf->end();
                 ++line_iter)
            {
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
                    std::string full_line(sbr.get_data(), sbr.length());
                    std::string partial_line(sbr.get_data(), partial_len);

                    fprintf(stderr,
                            "error:%s:%ld:line did not match format "
                            "%s\n",
                            lf->get_filename().c_str(),
                            line_number,
                            fmt->get_pattern_path(line_number).c_str());
                    fprintf(stderr,
                            "error:%s:%ld:         line -- %s\n",
                            lf->get_filename().c_str(),
                            line_number,
                            full_line.c_str());
                    if (partial_len > 0) {
                        fprintf(stderr,
                                "error:%s:%ld:partial match -- %s\n",
                                lf->get_filename().c_str(),
                                line_number,
                                partial_line.c_str());
                    } else {
                        fprintf(stderr,
                                "error:%s:%ld:no partial match found\n",
                                lf->get_filename().c_str(),
                                line_number);
                    }
                    retval = EXIT_FAILURE;
                }
            }
        }
        return retval;
    }

    if (lnav_data.ld_flags & LNF_HEADLESS || mode_flags.mf_check_configs) {
    } else if (!isatty(STDOUT_FILENO)) {
        lnav::console::print(
            stderr,
            lnav::console::user_message::error(
                "unable to display interactive text UI")
                .with_reason("stdout is not a TTY")
                .with_help(attr_line_t("pass the ")
                               .append("-n"_symbol)
                               .append(" option to run lnav in headless mode "
                                       "or don't redirect stdout")));
        retval = EXIT_FAILURE;
    }

    std::optional<std::string> stdin_url;
    ghc::filesystem::path stdin_dir;
    if (load_stdin && !isatty(STDIN_FILENO) && !is_dev_null(STDIN_FILENO)
        && !exec_stdin)
    {
        static const std::string STDIN_NAME = "stdin";
        struct stat stdin_st;

        if (fstat(STDIN_FILENO, &stdin_st) == -1) {
            lnav::console::print(
                stderr,
                lnav::console::user_message::error("unable to stat() stdin")
                    .with_errno_reason());
            retval = EXIT_FAILURE;
        } else if (S_ISFIFO(stdin_st.st_mode)) {
            struct pollfd pfd[1];

            pfd[0].fd = STDIN_FILENO;
            pfd[0].events = POLLIN;
            pfd[0].revents = 0;
            auto prc = poll(pfd, 1, 0);

            if (prc == 0 || (pfd[0].revents & POLLIN)) {
                auto stdin_piper_res = lnav::piper::create_looper(
                    STDIN_NAME, auto_fd::dup_of(STDIN_FILENO), auto_fd{});
                if (stdin_piper_res.isOk()) {
                    auto stdin_piper = stdin_piper_res.unwrap();
                    stdin_url = stdin_piper.get_url();
                    stdin_dir = stdin_piper.get_out_dir();
                    auto& loo = lnav_data.ld_active_files
                                    .fc_file_names[stdin_piper.get_name()];
                    loo.with_piper(stdin_piper).with_include_in_session(false);
                    if (lnav_data.ld_treat_stdin_as_log) {
                        loo.with_text_format(text_format_t::TF_LOG);
                    }
                }
            }
        } else if (S_ISREG(stdin_st.st_mode)) {
            // The shell connected a file directly, just open it up
            // and add it in here.
            auto loo = logfile_open_options{}
                           .with_filename(STDIN_NAME)
                           .with_include_in_session(false);

            auto open_res
                = logfile::open(STDIN_NAME, loo, auto_fd::dup_of(STDIN_FILENO));

            if (open_res.isErr()) {
                lnav::console::print(
                    stderr,
                    lnav::console::user_message::error("unable to open stdin")
                        .with_reason(open_res.unwrapErr()));
                retval = EXIT_FAILURE;
            } else {
                file_collection fc;

                fc.fc_files.emplace_back(open_res.unwrap());
                update_active_files(fc);
            }
        }
    }

    if (!isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
        if (dup2(STDOUT_FILENO, STDIN_FILENO) == -1) {
            perror("cannot dup stdout to stdin");
        }
    }

    if (retval == EXIT_SUCCESS && lnav_data.ld_active_files.fc_files.empty()
        && lnav_data.ld_active_files.fc_file_names.empty()
        && lnav_data.ld_commands.empty()
        && !(lnav_data.ld_show_help_view || mode_flags.mf_no_default))
    {
        lnav::console::print(
            stderr,
            lnav::console::user_message::error("nothing to do")
                .with_reason("no files given or default files found")
                .with_help(attr_line_t("use the ")
                               .append_quoted(lnav::roles::keyword("-N"))
                               .append(" option to open lnav without "
                                       "loading any files")));
        retval = EXIT_FAILURE;
    }

    if (retval == EXIT_SUCCESS) {
        isc::supervisor root_superv(injector::get<isc::service_list>());

        try {
            char pcre2_version[128];

            pcre2_config(PCRE2_CONFIG_VERSION, pcre2_version);
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
            log_info("    details=%s", archive_version_details());
#endif
            log_info("  ncurses=%s", NCURSES_VERSION);
            log_info("  pcre2=%s", pcre2_version);
            log_info("  readline=%s", rl_library_version);
            log_info("  sqlite=%s", sqlite3_version);
            log_info("  zlib=%s", zlibVersion());
            log_info("lnav_data:");
            log_info("  flags=%x", lnav_data.ld_flags);
            log_info("  commands:");
            for (auto cmd_iter = lnav_data.ld_commands.begin();
                 cmd_iter != lnav_data.ld_commands.end();
                 ++cmd_iter)
            {
                log_info("    %s", cmd_iter->c_str());
            }
            log_info("  files:");
            for (auto file_iter
                 = lnav_data.ld_active_files.fc_file_names.begin();
                 file_iter != lnav_data.ld_active_files.fc_file_names.end();
                 ++file_iter)
            {
                log_info("    %s", file_iter->first.c_str());
            }

            if (!(lnav_data.ld_flags & LNF_HEADLESS)
                && verbosity == verbosity_t::quiet && load_stdin
                && lnav_data.ld_active_files.fc_file_names.size() == 1)
            {
                rescan_files(true);
                gather_pipers();
                auto rebuild_res = rebuild_indexes(ui_clock::now() + 15ms);
                if (rebuild_res.rir_completed
                    && lnav_data.ld_child_pollers.empty())
                {
                    rebuild_indexes_repeatedly();
                    if (lnav_data.ld_active_files.fc_files.empty()
                        || lnav_data.ld_active_files.fc_files[0]->size() < 24)
                    {
                        lnav_data.ld_flags |= LNF_HEADLESS;
                        verbosity = verbosity_t::standard;
                    }
                }
            }

            if (lnav_data.ld_flags & LNF_HEADLESS) {
                std::vector<
                    std::pair<Result<std::string, lnav::console::user_message>,
                              std::string>>
                    cmd_results;
                textview_curses *log_tc, *text_tc, *tc;
                bool output_view = true;

                log_fos->fos_contexts.top().c_show_applicable_annotations
                    = false;

                view_colors::init(true);
                rescan_files(true);
                wait_for_pipers();
                rescan_files(true);
                rebuild_indexes_repeatedly();
                {
                    safe::WriteAccess<safe_name_to_errors> errs(
                        *lnav_data.ld_active_files.fc_name_to_errors);
                    if (!errs->empty()) {
                        for (const auto& pair : *errs) {
                            lnav::console::print(
                                stderr,
                                lnav::console::user_message::error(
                                    attr_line_t("unable to open file: ")
                                        .append(lnav::roles::file(pair.first)))
                                    .with_reason(pair.second.fei_description));
                        }

                        return EXIT_FAILURE;
                    }
                }
                init_session();
                lnav_data.ld_exec_context.set_output("stdout", stdout, nullptr);
                alerter::singleton().enabled(false);

                log_tc = &lnav_data.ld_views[LNV_LOG];
                log_tc->set_height(24_vl);
                lnav_data.ld_view_stack.push_back(log_tc);
                // Read all of stdin
                wait_for_pipers();
                rebuild_indexes_repeatedly();
                wait_for_children();

                log_tc->set_top(0_vl);
                text_tc = &lnav_data.ld_views[LNV_TEXT];
                text_tc->set_height(vis_line_t(text_tc->get_inner_height()
                                               - text_tc->get_top()));
                setup_highlights(lnav_data.ld_views[LNV_TEXT].get_highlights());
                if (lnav_data.ld_log_source.text_line_count() == 0
                    && lnav_data.ld_text_source.text_line_count() > 0)
                {
                    ensure_view(&lnav_data.ld_views[LNV_TEXT]);
                }

                log_info("Executing initial commands");
                execute_init_commands(lnav_data.ld_exec_context, cmd_results);
                archive_manager::cleanup_cache();
                tailer::cleanup_cache();
                line_buffer::cleanup_cache();
                lnav::piper::cleanup();
                file_converter_manager::cleanup();
                wait_for_pipers();
                rescan_files(true);
                isc::to<curl_looper&, services::curl_streamer_t>()
                    .send_and_wait(
                        [](auto& clooper) { clooper.process_all(); });
                rebuild_indexes_repeatedly();
                wait_for_children();
                {
                    safe::WriteAccess<safe_name_to_errors> errs(
                        *lnav_data.ld_active_files.fc_name_to_errors);
                    if (!errs->empty()) {
                        for (const auto& pair : *errs) {
                            lnav::console::print(
                                stderr,
                                lnav::console::user_message::error(
                                    attr_line_t("unable to open file: ")
                                        .append(lnav::roles::file(pair.first)))
                                    .with_reason(pair.second.fei_description));
                        }

                        return EXIT_FAILURE;
                    }
                }

                for (const auto& lf : lnav_data.ld_active_files.fc_files) {
                    for (const auto& note : lf->get_notes()) {
                        switch (note.first) {
                            case logfile::note_type::not_utf: {
                                auto um = lnav::console::user_message::error(
                                    note.second);
                                lnav::console::print(stderr, um);
                                break;
                            }

                            default:
                                break;
                        }
                    }
                }

                for (auto& pair : cmd_results) {
                    if (pair.first.isErr()) {
                        lnav::console::print(stderr, pair.first.unwrapErr());
                        output_view = false;
                    } else {
                        auto msg = pair.first.unwrap();

                        if (startswith(msg, "info:")) {
                            if (verbosity == verbosity_t::verbose) {
                                printf("%s\n", msg.c_str());
                            }
                        } else if (!msg.empty()) {
                            printf("%s\n", msg.c_str());
                            output_view = false;
                        }
                    }
                }

                if (output_view && verbosity != verbosity_t::quiet
                    && !lnav_data.ld_view_stack.empty()
                    && !lnav_data.ld_stdout_used)
                {
                    bool suppress_empty_lines = false;
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

                    auto* los = tc->get_overlay_source();
                    attr_line_t ov_al;
                    while (los != nullptr && tc->get_inner_height() > 0_vl
                           && los->list_static_overlay(
                               *tc, y, tc->get_inner_height(), ov_al))
                    {
                        write_line_to(stdout, ov_al);
                        ov_al.clear();
                        ++y;
                    }

                    vis_line_t vl;
                    for (vl = tc->get_top(); vl < tc->get_inner_height(); ++vl)
                    {
                        std::vector<attr_line_t> rows(1);
                        tc->listview_value_for_rows(*tc, vl, rows);
                        if (suppress_empty_lines && rows[0].empty()) {
                            continue;
                        }

                        write_line_to(stdout, rows[0]);

                        std::vector<attr_line_t> row_overlay_content;
                        if (los != nullptr) {
                            los->list_value_for_overlay(
                                *tc, vl, row_overlay_content);
                            for (const auto& ov_row : row_overlay_content) {
                                write_line_to(stdout, ov_row);
                            }
                        }
                    }
                }
            } else {
                init_session();

                guard_termios gt(STDIN_FILENO);
                lnav_log_orig_termios = gt.get_termios();

                looper();

                dup2(STDOUT_FILENO, STDERR_FILENO);

                signal(SIGINT, SIG_DFL);

                save_session();
            }
        } catch (const std::system_error& e) {
            if (e.code().value() != EPIPE) {
                fprintf(stderr, "error: %s\n", e.what());
            }
        } catch (const line_buffer::error& e) {
            fprintf(stderr, "error: %s\n", strerror(e.e_err));
        } catch (const std::exception& e) {
            fprintf(stderr, "error: %s\n", e.what());
        }

        // When reading from stdin, tell the user where the capture
        // file is stored so they can look at it later.
        if (stdin_url && !(lnav_data.ld_flags & LNF_HEADLESS)
            && verbosity != verbosity_t::quiet)
        {
            file_size_t stdin_size = 0;
            for (const auto& ent :
                 ghc::filesystem::directory_iterator(stdin_dir))
            {
                stdin_size += ent.file_size();
            }

            lnav::console::print(
                stderr,
                lnav::console::user_message::info(
                    attr_line_t()
                        .append(lnav::roles::number(humanize::file_size(
                            stdin_size, humanize::alignment::none)))
                        .append(" of data from stdin was captured and "
                                "will be saved for one day.  You can "
                                "reopen it by running:\n")
                        .appendf(FMT_STRING("   {} "),
                                 lnav_data.ld_program_name)
                        .append(lnav::roles::file(stdin_url.value()))));
        }
    }

    return retval;
}
