/**
 * Copyright (c) 2013, Timothy Stack
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
 * @file lnav_config.hh
 */

#ifndef lnav_config_hh
#define lnav_config_hh

#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "archive_manager.cfg.hh"
#include "base/date_time_scanner.cfg.hh"
#include "base/file_range.hh"
#include "base/lnav.console.hh"
#include "base/result.h"
#include "external_opener.cfg.hh"
#include "external_editor.cfg.hh"
#include "file_vtab.cfg.hh"
#include "lnav_config_fwd.hh"
#include "log.annotate.cfg.hh"
#include "log_level.hh"
#include "logfile.cfg.hh"
#include "logfile_sub_source.cfg.hh"
#include "piper.looper.cfg.hh"
#include "styling.hh"
#include "sysclip.cfg.hh"
#include "tailer/tailer.looper.cfg.hh"
#include "textfile_sub_source.cfg.hh"
#include "top_status_source.cfg.hh"
#include "url_handler.cfg.hh"

/**
 * Check if an experimental feature should be enabled by
 * examining the LNAV_EXP environment variable.
 *
 * @param feature_name The feature name to check for in
 *   the LNAV_EXP environment variable.
 *
 * @return True if the feature was mentioned in the env
 *   var and should be enabled.
 */
bool check_experimental(const char* feature_name);

/**
 * Ensure that the '.lnav' directory exists.
 */
void ensure_dotlnav();

bool install_from_git(const std::string& repo);
bool update_installs_from_git();

void install_extra_formats();

struct key_command {
    std::string kc_id;
    positioned_property<std::string> kc_cmd;
    std::string kc_alt_msg;
};

struct key_map {
    std::map<std::string, key_command> km_seq_to_cmd;
};

enum class config_movement_mode : unsigned int {
    TOP,
    CURSOR,
};

struct movement_config {
    config_movement_mode mode{config_movement_mode::TOP};
};

enum class lnav_mouse_mode {
    disabled,
    enabled,
};

struct _lnav_config {
    top_status_source_cfg lc_top_status_cfg;
    bool lc_ui_dim_text;
    bool lc_ui_default_colors{true};
    std::string lc_ui_keymap;
    std::string lc_ui_theme;
    movement_config lc_ui_movement;
    lnav_mouse_mode lc_mouse_mode;
    std::map<std::string, key_map> lc_ui_keymaps;
    std::map<std::string, std::string> lc_ui_key_overrides;
    std::map<std::string, std::string> lc_global_vars;
    std::map<std::string, lnav_theme> lc_ui_theme_defs;

    key_map lc_active_keymap;

    archive_manager::config lc_archive_manager;
    date_time_scanner_ns::config lc_log_date_time;
    lnav::piper::config lc_piper;
    file_vtab::config lc_file_vtab;
    lnav::logfile::config lc_logfile;
    tailer::config lc_tailer;
    sysclip::config lc_sysclip;
    lnav::url_handler::config lc_url_handlers;
    logfile_sub_source_ns::config lc_log_source;
    lnav::log::annotate::config lc_log_annotations;
    lnav::external_opener::config lc_opener;
    lnav::external_editor::config lc_external_editor;
    lnav::textfile::config lc_textfile;
};

extern struct _lnav_config lnav_config;
extern struct _lnav_config rollback_lnav_config;
extern std::map<intern_string_t, source_location> lnav_config_locations;

extern const struct json_path_container lnav_config_handlers;

enum class config_file_type {
    FORMAT,
    CONFIG,
};

Result<config_file_type, std::string> detect_config_file_type(
    const std::filesystem::path& path);

void load_config(const std::vector<std::filesystem::path>& extra_paths,
                 std::vector<lnav::console::user_message>& errors);

void reset_config(const std::string& path);

void reload_config(std::vector<lnav::console::user_message>& errors);

std::string save_config();

std::string dump_config();

extern const char* DEFAULT_FORMAT_SCHEMA;
extern const std::set<std::string> SUPPORTED_FORMAT_SCHEMAS;

#endif
