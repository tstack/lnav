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
 * @file lnav_config.cc
 */

#include <chrono>
#include <iostream>
#include <regex>
#include <stdexcept>

#include "lnav_config.hh"

#include <fcntl.h>
#include <fmt/format.h>
#include <glob.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base/auto_fd.hh"
#include "base/auto_mem.hh"
#include "base/auto_pid.hh"
#include "base/fs_util.hh"
#include "base/injector.bind.hh"
#include "base/injector.hh"
#include "base/lnav_log.hh"
#include "base/paths.hh"
#include "base/string_util.hh"
#include "bin2c.hh"
#include "command_executor.hh"
#include "config.h"
#include "default-config.h"
#include "scn/scn.h"
#include "styling.hh"
#include "view_curses.hh"
#include "yajlpp/yajlpp.hh"
#include "yajlpp/yajlpp_def.hh"

using namespace std::chrono_literals;

static const int MAX_CRASH_LOG_COUNT = 16;
static const auto STDIN_CAPTURE_RETENTION = 24h;

static auto intern_lifetime = intern_string::get_table_lifetime();

struct _lnav_config lnav_config;
struct _lnav_config rollback_lnav_config;
static struct _lnav_config lnav_default_config;

std::map<intern_string_t, source_location> lnav_config_locations;

lnav_config_listener* lnav_config_listener::LISTENER_LIST;

static auto a = injector::bind<archive_manager::config>::to_instance(
    +[]() { return &lnav_config.lc_archive_manager; });

static auto dtc = injector::bind<date_time_scanner_ns::config>::to_instance(
    +[]() { return &lnav_config.lc_log_date_time; });

static auto fvc = injector::bind<file_vtab::config>::to_instance(
    +[]() { return &lnav_config.lc_file_vtab; });

static auto lc = injector::bind<lnav::logfile::config>::to_instance(
    +[]() { return &lnav_config.lc_logfile; });

static auto p = injector::bind<lnav::piper::config>::to_instance(
    +[]() { return &lnav_config.lc_piper; });

static auto tc = injector::bind<tailer::config>::to_instance(
    +[]() { return &lnav_config.lc_tailer; });

static auto scc = injector::bind<sysclip::config>::to_instance(
    +[]() { return &lnav_config.lc_sysclip; });

static auto oc = injector::bind<lnav::external_opener::config>::to_instance(
    +[]() { return &lnav_config.lc_opener; });

static auto uh = injector::bind<lnav::url_handler::config>::to_instance(
    +[]() { return &lnav_config.lc_url_handlers; });

static auto lsc = injector::bind<logfile_sub_source_ns::config>::to_instance(
    +[]() { return &lnav_config.lc_log_source; });

static auto annoc = injector::bind<lnav::log::annotate::config>::to_instance(
    +[]() { return &lnav_config.lc_log_annotations; });

static auto tssc = injector::bind<top_status_source_cfg>::to_instance(
    +[]() { return &lnav_config.lc_top_status_cfg; });

static auto ltc = injector::bind<lnav::textfile::config>::to_instance(
    +[]() { return &lnav_config.lc_textfile; });

bool
check_experimental(const char* feature_name)
{
    const char* env_value = getenv("LNAV_EXP");

    require(feature_name != nullptr);

    if (env_value && strcasestr(env_value, feature_name)) {
        return true;
    }

    return false;
}

void
ensure_dotlnav()
{
    static const char* subdirs[] = {
        "",
        "configs",
        "configs/default",
        "configs/installed",
        "formats",
        "formats/default",
        "formats/installed",
        "staging",
        "stdin-captures",
        "crash",
    };

    auto path = lnav::paths::dotlnav();

    for (const auto* sub_path : subdirs) {
        auto full_path = path / sub_path;

        if (mkdir(full_path.c_str(), 0755) == -1 && errno != EEXIST) {
            log_error("unable to make directory: %s -- %s",
                      full_path.c_str(),
                      strerror(errno));
        }
    }

    auto crash_dir_path = path / "crash";
    lnav_log_crash_dir = strdup(crash_dir_path.c_str());

    {
        static_root_mem<glob_t, globfree> gl;
        auto crash_glob = path / "crash-*";

        if (glob(crash_glob.c_str(), GLOB_NOCHECK, nullptr, gl.inout()) == 0) {
            std::error_code ec;
            for (size_t lpc = 0; lpc < gl->gl_pathc; lpc++) {
                auto crash_file = std::filesystem::path(gl->gl_pathv[lpc]);

                std::filesystem::rename(
                    crash_file, crash_dir_path / crash_file.filename(), ec);
            }
        }
    }

    {
        static_root_mem<glob_t, globfree> gl;
        auto crash_glob = path / "crash/*";

        if (glob(crash_glob.c_str(), GLOB_NOCHECK, nullptr, gl.inout()) == 0) {
            for (int lpc = 0; lpc < ((int) gl->gl_pathc - MAX_CRASH_LOG_COUNT);
                 lpc++)
            {
                log_perror(remove(gl->gl_pathv[lpc]));
            }
        }
    }

    {
        static_root_mem<glob_t, globfree> gl;
        auto cap_glob = path / "stdin-captures/*";

        if (glob(cap_glob.c_str(), GLOB_NOCHECK, nullptr, gl.inout()) == 0) {
            auto old_time
                = std::chrono::system_clock::now() - STDIN_CAPTURE_RETENTION;

            for (size_t lpc = 0; lpc < gl->gl_pathc; lpc++) {
                struct stat st;

                if (stat(gl->gl_pathv[lpc], &st) == -1) {
                    continue;
                }

                if (std::chrono::system_clock::from_time_t(st.st_mtime)
                    > old_time)
                {
                    continue;
                }

                log_info("Removing old stdin capture: %s", gl->gl_pathv[lpc]);
                log_perror(remove(gl->gl_pathv[lpc]));
            }
        }
    }
}

bool
install_from_git(const std::string& repo)
{
    static const std::regex repo_name_converter("[^\\w]");

    auto formats_path = lnav::paths::dotlnav() / "formats";
    auto configs_path = lnav::paths::dotlnav() / "configs";
    auto staging_path = lnav::paths::dotlnav() / "staging";
    auto local_name = std::regex_replace(repo, repo_name_converter, "_");

    auto local_formats_path = formats_path / local_name;
    auto local_configs_path = configs_path / local_name;
    auto local_staging_path = staging_path / local_name;

    auto fork_res = lnav::pid::from_fork();
    if (fork_res.isErr()) {
        fprintf(stderr,
                "error: cannot fork() to run git: %s\n",
                fork_res.unwrapErr().c_str());
        _exit(1);
    }

    auto git_cmd = fork_res.unwrap();
    if (git_cmd.in_child()) {
        if (std::filesystem::is_directory(local_formats_path)) {
            fmt::print("Updating format repo: {}\n", repo);
            log_perror(chdir(local_formats_path.c_str()));
            execlp("git", "git", "pull", nullptr);
        } else if (std::filesystem::is_directory(local_configs_path)) {
            fmt::print("Updating config repo: {}\n", repo);
            log_perror(chdir(local_configs_path.c_str()));
            execlp("git", "git", "pull", nullptr);
        } else {
            execlp("git",
                   "git",
                   "clone",
                   repo.c_str(),
                   local_staging_path.c_str(),
                   nullptr);
        }
        _exit(1);
    }

    auto finished_child = std::move(git_cmd).wait_for_child();
    if (!finished_child.was_normal_exit() || finished_child.exit_status() != 0)
    {
        return false;
    }

    if (std::filesystem::is_directory(local_formats_path)
        || std::filesystem::is_directory(local_configs_path))
    {
        return false;
    }
    if (!std::filesystem::is_directory(local_staging_path)) {
        auto um
            = lnav::console::user_message::error(
                  attr_line_t("failed to install git repo: ")
                      .append(lnav::roles::file(repo)))
                  .with_reason(
                      attr_line_t("git failed to create the local directory ")
                          .append(
                              lnav::roles::file(local_staging_path.string())))
                  .move();
        lnav::console::print(stderr, um);
        return false;
    }

    auto config_path = local_staging_path / "*";
    static_root_mem<glob_t, globfree> gl;
    int found_config_file = 0;
    int found_format_file = 0;
    int found_sql_file = 0;
    int found_lnav_file = 0;

    if (glob(config_path.c_str(), 0, nullptr, gl.inout()) == 0) {
        for (size_t lpc = 0; lpc < gl->gl_pathc; lpc++) {
            auto file_path = std::filesystem::path{gl->gl_pathv[lpc]};

            if (file_path.extension() == ".lnav") {
                found_lnav_file += 1;
                continue;
            }
            if (file_path.extension() == ".sql") {
                found_sql_file += 1;
                continue;
            }
            if (file_path.extension() != ".json") {
                found_sql_file += 1;
                continue;
            }

            auto file_type_result = detect_config_file_type(file_path);

            if (file_type_result.isErr()) {
                fprintf(stderr,
                        "error: %s\n",
                        file_type_result.unwrapErr().c_str());
                return false;
            }
            if (file_type_result.unwrap() == config_file_type::CONFIG) {
                found_config_file += 1;
            } else {
                found_format_file += 1;
            }
        }
    }

    if (found_config_file == 0 && found_format_file == 0 && found_sql_file == 0
        && found_lnav_file == 0)
    {
        auto um = lnav::console::user_message::error(
                      attr_line_t("invalid lnav repo: ")
                          .append(lnav::roles::file(repo)))
                      .with_reason("no .json, .sql, or .lnav files were found")
                      .move();
        lnav::console::print(stderr, um);
        return false;
    }

    auto dest_path = local_formats_path;
    attr_line_t notes;
    if (found_format_file > 0) {
        notes.append("found ")
            .append(lnav::roles::number(fmt::to_string(found_format_file)))
            .append(" format file(s)\n");
    }
    if (found_config_file > 0) {
        if (found_format_file == 0) {
            dest_path = local_configs_path;
        }
        notes.append("found ")
            .append(lnav::roles::number(fmt::to_string(found_config_file)))
            .append(" configuration file(s)\n");
    }
    if (found_sql_file > 0) {
        notes.append("found ")
            .append(lnav::roles::number(fmt::to_string(found_sql_file)))
            .append(" SQL file(s)\n");
    }
    if (found_lnav_file > 0) {
        notes.append("found ")
            .append(lnav::roles::number(fmt::to_string(found_lnav_file)))
            .append(" lnav-script file(s)\n");
    }
    rename(local_staging_path.c_str(), dest_path.c_str());
    auto um = lnav::console::user_message::ok(
                  attr_line_t("installed lnav repo at: ")
                      .append(lnav::roles::file(dest_path.string())))
                  .with_note(notes)
                  .move();
    lnav::console::print(stdout, um);

    return true;
}

bool
update_installs_from_git()
{
    static_root_mem<glob_t, globfree> gl;
    auto git_formats = lnav::paths::dotlnav() / "formats/*/.git";
    bool found = false, retval = true;

    if (glob(git_formats.c_str(), 0, nullptr, gl.inout()) == 0) {
        for (int lpc = 0; lpc < (int) gl->gl_pathc; lpc++) {
            auto git_dir
                = std::filesystem::path(gl->gl_pathv[lpc]).parent_path();

            printf("Updating formats in %s\n", git_dir.c_str());
            auto pull_cmd = fmt::format(FMT_STRING("cd '{}' && git pull"),
                                        git_dir.string());
            int ret = system(pull_cmd.c_str());
            if (ret == -1) {
                std::cerr << "Failed to spawn command "
                          << "\"" << pull_cmd << "\": " << strerror(errno)
                          << std::endl;
                retval = false;
            } else if (ret > 0) {
                std::cerr << "Command "
                          << "\"" << pull_cmd
                          << "\" failed: " << strerror(errno) << std::endl;
                retval = false;
            }
            found = true;
        }
    }

    if (!found) {
        printf(
            "No formats from git repositories found, "
            "use 'lnav -i extra' to install third-party formats\n");
    }

    return retval;
}

static int
read_repo_path(yajlpp_parse_context* ypc, const unsigned char* str, size_t len)
{
    auto path = std::string((const char*) str, len);

    install_from_git(path.c_str());

    return 1;
}

static const struct json_path_container format_handlers = {
    json_path_handler("format-repos#", read_repo_path),
};

void
install_extra_formats()
{
    auto config_root = lnav::paths::dotlnav() / "remote-config";
    auto_fd fd;

    if (access(config_root.c_str(), R_OK) == 0) {
        printf("Updating lnav remote config repo...\n");
        auto pull_cmd = fmt::format(FMT_STRING("cd '{}' && git pull"),
                                    config_root.string());
        log_perror(system(pull_cmd.c_str()));
    } else {
        printf("Cloning lnav remote config repo...\n");
        auto clone_cmd = fmt::format(
            FMT_STRING(
                "git clone https://github.com/tstack/lnav-config.git {}"),
            config_root.string());
        log_perror(system(clone_cmd.c_str()));
    }

    auto config_json = config_root / "remote-config.json";
    if ((fd = lnav::filesystem::openp(config_json, O_RDONLY)) == -1) {
        perror("Unable to open remote-config.json");
    } else {
        yajlpp_parse_context ypc_config(
            intern_string::lookup(config_root.string()), &format_handlers);
        auto_mem<yajl_handle_t> jhandle(yajl_free);
        unsigned char buffer[4096];
        ssize_t rc;

        jhandle = yajl_alloc(&ypc_config.ypc_callbacks, nullptr, &ypc_config);
        yajl_config(jhandle, yajl_allow_comments, 1);
        while ((rc = read(fd, buffer, sizeof(buffer))) > 0) {
            if (yajl_parse(jhandle, buffer, rc) != yajl_status_ok) {
                auto* msg = yajl_get_error(jhandle, 1, buffer, rc);
                fprintf(
                    stderr, "Unable to parse remote-config.json -- %s", msg);
                yajl_free_error(jhandle, msg);
                return;
            }
        }
        if (yajl_complete_parse(jhandle) != yajl_status_ok) {
            auto* msg = yajl_get_error(jhandle, 0, nullptr, 0);

            fprintf(stderr, "Unable to parse remote-config.json -- %s", msg);
            yajl_free_error(jhandle, msg);
        }
    }
}

struct config_userdata {
    explicit config_userdata(std::vector<lnav::console::user_message>& errors)
        : ud_errors(errors)
    {
    }

    std::vector<lnav::console::user_message>& ud_errors;
};

static void
config_error_reporter(const yajlpp_parse_context& ypc,
                      const lnav::console::user_message& msg)
{
    auto* ud = (config_userdata*) ypc.ypc_userdata;

    ud->ud_errors.emplace_back(msg);
}

static const struct json_path_container key_command_handlers = {
    yajlpp::property_handler("id")
        .with_synopsis("<id>")
        .with_description(
            "The identifier that can be used to refer to this key")
        .for_field(&key_command::kc_id),
    yajlpp::property_handler("command")
        .with_synopsis("<command>")
        .with_description(
            "The command to execute for the given key sequence.  Use a script "
            "to execute more complicated operations.")
        .with_pattern("^$|^[:|;].*")
        .with_example(":goto next hour")
        .for_field(&key_command::kc_cmd),
    yajlpp::property_handler("alt-msg")
        .with_synopsis("<msg>")
        .with_description(
            "The help message to display after the key is pressed.")
        .for_field<>(&key_command::kc_alt_msg),
};

static const struct json_path_container keymap_def_handlers = {
    yajlpp::pattern_property_handler(
        "(?<key_seq>(?:x[0-9a-f]{2}|f[0-9]{1,2})+)")
        .with_synopsis("<utf8-key-code-in-hex>")
        .with_description(
            "Map of key codes to commands to execute.  The field names are "
            "the keys to be mapped using as a hexadecimal representation of "
            "the UTF-8 encoding.  Each byte of the UTF-8 should start with "
            "an 'x' followed by the hexadecimal representation of the byte.")
        .with_obj_provider<key_command, key_map>(
            [](const yajlpp_provider_context& ypc, key_map* km) {
                auto& retval = km->km_seq_to_cmd[ypc.get_substr("key_seq")];

                if (ypc.ypc_parse_context != nullptr) {
                    retval.kc_cmd.pp_path
                        = ypc.ypc_parse_context->get_full_path();

                    retval.kc_cmd.pp_location.sl_source
                        = ypc.ypc_parse_context->ypc_source;
                    retval.kc_cmd.pp_location.sl_line_number
                        = ypc.ypc_parse_context->get_line_number();
                }
                return &retval;
            })
        .with_path_provider<key_map>(
            [](key_map* km, std::vector<std::string>& paths_out) {
                for (const auto& iter : km->km_seq_to_cmd) {
                    paths_out.emplace_back(iter.first);
                }
            })
        .with_children(key_command_handlers),
};

static const struct json_path_container keymap_defs_handlers = {
    yajlpp::pattern_property_handler("(?<keymap_name>[\\w\\-]+)")
        .with_description("The keymap definitions")
        .with_obj_provider<key_map, _lnav_config>(
            [](const yajlpp_provider_context& ypc, _lnav_config* root) {
                key_map& retval
                    = root->lc_ui_keymaps[ypc.get_substr("keymap_name")];
                return &retval;
            })
        .with_path_provider<_lnav_config>(
            [](struct _lnav_config* cfg, std::vector<std::string>& paths_out) {
                for (const auto& iter : cfg->lc_ui_keymaps) {
                    paths_out.emplace_back(iter.first);
                }
            })
        .with_children(keymap_def_handlers),
};

static const json_path_handler_base::enum_value_t _movement_values[] = {
    {"top", config_movement_mode::TOP},
    {"cursor", config_movement_mode::CURSOR},

    json_path_handler_base::ENUM_TERMINATOR,
};

static const struct json_path_container movement_handlers = {
    yajlpp::property_handler("mode")
        .with_synopsis("top|cursor")
        .with_enum_values(_movement_values)
        .with_example("top")
        .with_example("cursor")
        .with_description("The mode of cursor movement to use.")
        .for_field<>(&_lnav_config::lc_ui_movement, &movement_config::mode),
};

static const json_path_handler_base::enum_value_t _mouse_mode_values[] = {
    {"disabled", lnav_mouse_mode::disabled},
    {"enabled", lnav_mouse_mode::enabled},

    json_path_handler_base::ENUM_TERMINATOR,
};

static const struct json_path_container mouse_handlers = {
    yajlpp::property_handler("mode")
        .with_synopsis("enabled|disabled")
        .with_enum_values(_mouse_mode_values)
        .with_example("enabled")
        .with_example("disabled")
        .with_description("Overall control for mouse support")
        .for_field<>(&_lnav_config::lc_mouse_mode),
};

static const struct json_path_container global_var_handlers = {
    yajlpp::pattern_property_handler("(?<var_name>\\w+)")
        .with_synopsis("<name>")
        .with_description(
            "A global variable definition.  Global variables can be referenced "
            "in scripts, SQL statements, or commands.")
        .with_path_provider<_lnav_config>(
            [](struct _lnav_config* cfg, std::vector<std::string>& paths_out) {
                for (const auto& iter : cfg->lc_global_vars) {
                    paths_out.emplace_back(iter.first);
                }
            })
        .for_field(&_lnav_config::lc_global_vars),
};

static const struct json_path_container style_config_handlers =
    json_path_container{
        yajlpp::property_handler("color")
            .with_synopsis("#hex|color_name")
            .with_description(
                "The foreground color value for this style. The value can be "
                "the name of an xterm color, the hexadecimal value, or a theme "
                "variable reference.")
            .with_example("#fff")
            .with_example("Green")
            .with_example("$black")
            .for_field(&style_config::sc_color),
        yajlpp::property_handler("background-color")
            .with_synopsis("#hex|color_name")
            .with_description(
                "The background color value for this style. The value can be "
                "the name of an xterm color, the hexadecimal value, or a theme "
                "variable reference.")
            .with_example("#2d2a2e")
            .with_example("Green")
            .for_field(&style_config::sc_background_color),
        yajlpp::property_handler("underline")
            .with_description("Indicates that the text should be underlined.")
            .for_field(&style_config::sc_underline),
        yajlpp::property_handler("bold")
            .with_description("Indicates that the text should be bolded.")
            .for_field(&style_config::sc_bold),
    }
        .with_definition_id("style");

static const auto icon_config_handlers
    = json_path_container{
        yajlpp::property_handler("value")
            .with_description("The icon.")
            .for_field(&icon_config::ic_value),
    }.with_definition_id("icon");

static const json_path_container theme_icons_handlers = {
    yajlpp::property_handler("hidden")
        .with_description("Icon for hidden fields")
        .for_child(&lnav_theme::lt_icon_hidden)
        .with_children(icon_config_handlers),
};

static const struct json_path_container theme_styles_handlers = {
    yajlpp::property_handler("identifier")
        .with_description("Styling for identifiers in logs")
        .for_child(&lnav_theme::lt_style_identifier)
        .with_children(style_config_handlers),
    yajlpp::property_handler("text")
        .with_description("Styling for plain text")
        .for_child(&lnav_theme::lt_style_text)
        .with_children(style_config_handlers),
    yajlpp::property_handler("selected-text")
        .with_description("Styling for text selected in a view")
        .for_child(&lnav_theme::lt_style_selected_text)
        .with_children(style_config_handlers),
    yajlpp::property_handler("alt-text")
        .with_description("Styling for plain text when alternating")
        .for_child(&lnav_theme::lt_style_alt_text)
        .with_children(style_config_handlers),
    yajlpp::property_handler("error")
        .with_description("Styling for error messages")
        .for_child(&lnav_theme::lt_style_error)
        .with_children(style_config_handlers),
    yajlpp::property_handler("ok")
        .with_description("Styling for success messages")
        .for_child(&lnav_theme::lt_style_ok)
        .with_children(style_config_handlers),
    yajlpp::property_handler("info")
        .with_description("Styling for informational messages")
        .for_child(&lnav_theme::lt_style_info)
        .with_children(style_config_handlers),
    yajlpp::property_handler("warning")
        .with_description("Styling for warning messages")
        .for_child(&lnav_theme::lt_style_warning)
        .with_children(style_config_handlers),
    yajlpp::property_handler("hidden")
        .with_description("Styling for hidden fields in logs")
        .for_child(&lnav_theme::lt_style_hidden)
        .with_children(style_config_handlers),
    yajlpp::property_handler("cursor-line")
        .with_description("Styling for the cursor line in the main view")
        .for_child(&lnav_theme::lt_style_cursor_line)
        .with_children(style_config_handlers),
    yajlpp::property_handler("disabled-cursor-line")
        .with_description("Styling for the cursor line when it is disabled")
        .for_child(&lnav_theme::lt_style_disabled_cursor_line)
        .with_children(style_config_handlers),
    yajlpp::property_handler("adjusted-time")
        .with_description("Styling for timestamps that have been adjusted")
        .for_child(&lnav_theme::lt_style_adjusted_time)
        .with_children(style_config_handlers),
    yajlpp::property_handler("skewed-time")
        .with_description(
            "Styling for timestamps that are different from the received time")
        .for_child(&lnav_theme::lt_style_skewed_time)
        .with_children(style_config_handlers),
    yajlpp::property_handler("file-offset")
        .with_description("Styling for a file offset")
        .for_child(&lnav_theme::lt_style_file_offset)
        .with_children(style_config_handlers),
    yajlpp::property_handler("offset-time")
        .with_description("Styling for the elapsed time column")
        .for_child(&lnav_theme::lt_style_offset_time)
        .with_children(style_config_handlers),
    yajlpp::property_handler("invalid-msg")
        .with_description("Styling for invalid log messages")
        .for_child(&lnav_theme::lt_style_invalid_msg)
        .with_children(style_config_handlers),
    yajlpp::property_handler("popup")
        .with_description("Styling for popup windows")
        .for_child(&lnav_theme::lt_style_popup)
        .with_children(style_config_handlers),
    yajlpp::property_handler("focused")
        .with_description("Styling for a focused row in a list view")
        .for_child(&lnav_theme::lt_style_focused)
        .with_children(style_config_handlers),
    yajlpp::property_handler("disabled-focused")
        .with_description("Styling for a disabled focused row in a list view")
        .for_child(&lnav_theme::lt_style_disabled_focused)
        .with_children(style_config_handlers),
    yajlpp::property_handler("scrollbar")
        .with_description("Styling for scrollbars")
        .for_child(&lnav_theme::lt_style_scrollbar)
        .with_children(style_config_handlers),
    yajlpp::property_handler("h1")
        .with_description("Styling for top-level headers")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                if (ypc.ypc_parse_context != nullptr
                    && root->lt_style_header[0].pp_path.empty())
                {
                    root->lt_style_header[0].pp_path
                        = ypc.ypc_parse_context->get_full_path();
                }
                return &root->lt_style_header[0].pp_value;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("h2")
        .with_description("Styling for 2nd-level headers")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                if (ypc.ypc_parse_context != nullptr
                    && root->lt_style_header[1].pp_path.empty())
                {
                    root->lt_style_header[1].pp_path
                        = ypc.ypc_parse_context->get_full_path();
                }
                return &root->lt_style_header[1].pp_value;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("h3")
        .with_description("Styling for 3rd-level headers")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                if (ypc.ypc_parse_context != nullptr
                    && root->lt_style_header[2].pp_path.empty())
                {
                    root->lt_style_header[2].pp_path
                        = ypc.ypc_parse_context->get_full_path();
                }
                return &root->lt_style_header[2].pp_value;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("h4")
        .with_description("Styling for 4th-level headers")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                if (ypc.ypc_parse_context != nullptr
                    && root->lt_style_header[3].pp_path.empty())
                {
                    root->lt_style_header[3].pp_path
                        = ypc.ypc_parse_context->get_full_path();
                }
                return &root->lt_style_header[3].pp_value;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("h5")
        .with_description("Styling for 5th-level headers")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                if (ypc.ypc_parse_context != nullptr
                    && root->lt_style_header[4].pp_path.empty())
                {
                    root->lt_style_header[4].pp_path
                        = ypc.ypc_parse_context->get_full_path();
                }
                return &root->lt_style_header[4].pp_value;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("h6")
        .with_description("Styling for 6th-level headers")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                if (ypc.ypc_parse_context != nullptr
                    && root->lt_style_header[5].pp_path.empty())
                {
                    root->lt_style_header[5].pp_path
                        = ypc.ypc_parse_context->get_full_path();
                }
                return &root->lt_style_header[5].pp_value;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("hr")
        .with_description("Styling for horizontal rules")
        .for_child(&lnav_theme::lt_style_hr)
        .with_children(style_config_handlers),
    yajlpp::property_handler("hyperlink")
        .with_description("Styling for hyperlinks")
        .for_child(&lnav_theme::lt_style_hyperlink)
        .with_children(style_config_handlers),
    yajlpp::property_handler("list-glyph")
        .with_description("Styling for glyphs that prefix a list item")
        .for_child(&lnav_theme::lt_style_list_glyph)
        .with_children(style_config_handlers),
    yajlpp::property_handler("breadcrumb")
        .with_description("Styling for the separator between breadcrumbs")
        .for_child(&lnav_theme::lt_style_breadcrumb)
        .with_children(style_config_handlers),
    yajlpp::property_handler("table-border")
        .with_description("Styling for table borders")
        .for_child(&lnav_theme::lt_style_table_border)
        .with_children(style_config_handlers),
    yajlpp::property_handler("table-header")
        .with_description("Styling for table headers")
        .for_child(&lnav_theme::lt_style_table_header)
        .with_children(style_config_handlers),
    yajlpp::property_handler("quote-border")
        .with_description("Styling for quoted-block borders")
        .for_child(&lnav_theme::lt_style_quote_border)
        .with_children(style_config_handlers),
    yajlpp::property_handler("quoted-text")
        .with_description("Styling for quoted text blocks")
        .for_child(&lnav_theme::lt_style_quoted_text)
        .with_children(style_config_handlers),
    yajlpp::property_handler("footnote-border")
        .with_description("Styling for footnote borders")
        .for_child(&lnav_theme::lt_style_footnote_border)
        .with_children(style_config_handlers),
    yajlpp::property_handler("footnote-text")
        .with_description("Styling for footnote text")
        .for_child(&lnav_theme::lt_style_footnote_text)
        .with_children(style_config_handlers),
    yajlpp::property_handler("snippet-border")
        .with_description("Styling for snippet borders")
        .for_child(&lnav_theme::lt_style_snippet_border)
        .with_children(style_config_handlers),
    yajlpp::property_handler("indent-guide")
        .with_description("Styling for indent guide lines")
        .for_child(&lnav_theme::lt_style_indent_guide)
        .with_children(style_config_handlers),
};

static const struct json_path_container theme_syntax_styles_handlers = {
    yajlpp::property_handler("inline-code")
        .with_description("Styling for inline code blocks")
        .for_child(&lnav_theme::lt_style_inline_code)
        .with_children(style_config_handlers),
    yajlpp::property_handler("quoted-code")
        .with_description("Styling for quoted code blocks")
        .for_child(&lnav_theme::lt_style_quoted_code)
        .with_children(style_config_handlers),
    yajlpp::property_handler("code-border")
        .with_description("Styling for quoted-code borders")
        .for_child(&lnav_theme::lt_style_code_border)
        .with_children(style_config_handlers),
    yajlpp::property_handler("keyword")
        .with_description("Styling for keywords in source files")
        .for_child(&lnav_theme::lt_style_keyword)
        .with_children(style_config_handlers),
    yajlpp::property_handler("string")
        .with_description("Styling for single/double-quoted strings in text")
        .for_child(&lnav_theme::lt_style_string)
        .with_children(style_config_handlers),
    yajlpp::property_handler("comment")
        .with_description("Styling for comments in source files")
        .for_child(&lnav_theme::lt_style_comment)
        .with_children(style_config_handlers),
    yajlpp::property_handler("doc-directive")
        .with_description(
            "Styling for documentation directives in source files")
        .for_child(&lnav_theme::lt_style_doc_directive)
        .with_children(style_config_handlers),
    yajlpp::property_handler("variable")
        .with_description("Styling for variables in text")
        .for_child(&lnav_theme::lt_style_variable)
        .with_children(style_config_handlers),
    yajlpp::property_handler("symbol")
        .with_description("Styling for symbols in source files")
        .for_child(&lnav_theme::lt_style_symbol)
        .with_children(style_config_handlers),
    yajlpp::property_handler("null")
        .with_description("Styling for nulls in source files")
        .for_child(&lnav_theme::lt_style_null)
        .with_children(style_config_handlers),
    yajlpp::property_handler("ascii-control")
        .with_description(
            "Styling for ASCII control characters in source files")
        .for_child(&lnav_theme::lt_style_ascii_ctrl)
        .with_children(style_config_handlers),
    yajlpp::property_handler("non-ascii")
        .with_description("Styling for non-ASCII characters in source files")
        .for_child(&lnav_theme::lt_style_non_ascii)
        .with_children(style_config_handlers),
    yajlpp::property_handler("number")
        .with_description("Styling for numbers in source files")
        .for_child(&lnav_theme::lt_style_number)
        .with_children(style_config_handlers),
    yajlpp::property_handler("type")
        .with_description("Styling for types in source files")
        .for_child(&lnav_theme::lt_style_type)
        .with_children(style_config_handlers),
    yajlpp::property_handler("function")
        .with_description("Styling for functions in source files")
        .for_child(&lnav_theme::lt_style_function)
        .with_children(style_config_handlers),
    yajlpp::property_handler("separators-references-accessors")
        .with_description("Styling for sigils in source files")
        .for_child(&lnav_theme::lt_style_sep_ref_acc)
        .with_children(style_config_handlers),
    yajlpp::property_handler("re-special")
        .with_description(
            "Styling for special characters in regular expressions")
        .for_child(&lnav_theme::lt_style_re_special)
        .with_children(style_config_handlers),
    yajlpp::property_handler("re-repeat")
        .with_description("Styling for repeats in regular expressions")
        .for_child(&lnav_theme::lt_style_re_repeat)
        .with_children(style_config_handlers),

    yajlpp::property_handler("diff-delete")
        .with_description("Styling for deleted lines in diffs")
        .for_child(&lnav_theme::lt_style_diff_delete)
        .with_children(style_config_handlers),
    yajlpp::property_handler("diff-add")
        .with_description("Styling for added lines in diffs")
        .for_child(&lnav_theme::lt_style_diff_add)
        .with_children(style_config_handlers),
    yajlpp::property_handler("diff-section")
        .with_description("Styling for diffs")
        .for_child(&lnav_theme::lt_style_diff_section)
        .with_children(style_config_handlers),

    yajlpp::property_handler("spectrogram-low")
        .with_description(
            "Styling for the lower threshold values in the spectrogram view")
        .for_child(&lnav_theme::lt_style_low_threshold)
        .with_children(style_config_handlers),
    yajlpp::property_handler("spectrogram-medium")
        .with_description(
            "Styling for the medium threshold values in the spectrogram view")
        .for_child(&lnav_theme::lt_style_med_threshold)
        .with_children(style_config_handlers),
    yajlpp::property_handler("spectrogram-high")
        .with_description(
            "Styling for the high threshold values in the spectrogram view")
        .for_child(&lnav_theme::lt_style_high_threshold)
        .with_children(style_config_handlers),

    yajlpp::property_handler("file")
        .with_description("Styling for file names in source files")
        .for_child(&lnav_theme::lt_style_file)
        .with_children(style_config_handlers),
};

static const struct json_path_container theme_status_styles_handlers = {
    yajlpp::property_handler("text")
        .with_description("Styling for status bars")
        .for_child(&lnav_theme::lt_style_status)
        .with_children(style_config_handlers),
    yajlpp::property_handler("warn")
        .with_description("Styling for warnings in status bars")
        .for_child(&lnav_theme::lt_style_warn_status)
        .with_children(style_config_handlers),
    yajlpp::property_handler("alert")
        .with_description("Styling for alerts in status bars")
        .for_child(&lnav_theme::lt_style_alert_status)
        .with_children(style_config_handlers),
    yajlpp::property_handler("active")
        .with_description("Styling for activity in status bars")
        .for_child(&lnav_theme::lt_style_active_status)
        .with_children(style_config_handlers),
    yajlpp::property_handler("inactive-alert")
        .with_description("Styling for inactive alert status bars")
        .for_child(&lnav_theme::lt_style_inactive_alert_status)
        .with_children(style_config_handlers),
    yajlpp::property_handler("inactive")
        .with_description("Styling for inactive status bars")
        .for_child(&lnav_theme::lt_style_inactive_status)
        .with_children(style_config_handlers),
    yajlpp::property_handler("title-hotkey")
        .with_description("Styling for hotkey highlights in titles")
        .for_child(&lnav_theme::lt_style_status_title_hotkey)
        .with_children(style_config_handlers),
    yajlpp::property_handler("title")
        .with_description("Styling for title sections of status bars")
        .for_child(&lnav_theme::lt_style_status_title)
        .with_children(style_config_handlers),
    yajlpp::property_handler("disabled-title")
        .with_description("Styling for title sections of status bars")
        .for_child(&lnav_theme::lt_style_status_disabled_title)
        .with_children(style_config_handlers),
    yajlpp::property_handler("subtitle")
        .with_description("Styling for subtitle sections of status bars")
        .for_child(&lnav_theme::lt_style_status_subtitle)
        .with_children(style_config_handlers),
    yajlpp::property_handler("info")
        .with_description("Styling for informational messages in status bars")
        .for_child(&lnav_theme::lt_style_status_info)
        .with_children(style_config_handlers),
    yajlpp::property_handler("hotkey")
        .with_description("Styling for hotkey highlights of status bars")
        .for_child(&lnav_theme::lt_style_status_hotkey)
        .with_children(style_config_handlers),
    yajlpp::property_handler("suggestion")
        .with_description("Styling for suggested values")
        .for_child(&lnav_theme::lt_style_suggestion)
        .with_children(style_config_handlers),
};

static const struct json_path_container theme_log_level_styles_handlers = {
    yajlpp::pattern_property_handler(
        "(?<level>trace|debug5|debug4|debug3|debug2|debug|info|stats|notice|"
        "warning|error|critical|fatal|invalid)")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                auto& sc = root->lt_level_styles[string2level(
                    ypc.get_substr_i("level").get())];

                if (ypc.ypc_parse_context != nullptr && sc.pp_path.empty()) {
                    sc.pp_path = ypc.ypc_parse_context->get_full_path();
                }

                return &sc.pp_value;
            })
        .with_path_provider<lnav_theme>(
            [](struct lnav_theme* cfg, std::vector<std::string>& paths_out) {
                for (int lpc = LEVEL_TRACE; lpc < LEVEL__MAX; lpc++) {
                    paths_out.emplace_back(level_names[lpc]);
                }
            })
        .with_children(style_config_handlers),
};

static const struct json_path_container highlighter_handlers = {
    yajlpp::property_handler("pattern")
        .with_synopsis("regular expression")
        .with_description("The regular expression to highlight")
        .for_field(&highlighter_config::hc_regex),

    yajlpp::property_handler("style")
        .with_description(
            "The styling for the text that matches the associated pattern")
        .for_child(&highlighter_config::hc_style)
        .with_children(style_config_handlers),
};

static const struct json_path_container theme_highlights_handlers = {
    yajlpp::pattern_property_handler("(?<highlight_name>[\\w\\-]+)")
        .with_obj_provider<highlighter_config,
                           lnav_theme>([](const yajlpp_provider_context& ypc,
                                          lnav_theme* root) {
            highlighter_config& hc
                = root->lt_highlights[ypc.get_substr_i("highlight_name").get()];

            return &hc;
        })
        .with_path_provider<lnav_theme>(
            [](struct lnav_theme* cfg, std::vector<std::string>& paths_out) {
                for (const auto& pair : cfg->lt_highlights) {
                    paths_out.emplace_back(pair.first);
                }
            })
        .with_children(highlighter_handlers),
};

static const struct json_path_container theme_vars_handlers = {
    yajlpp::pattern_property_handler("(?<var_name>\\w+)")
        .with_synopsis("name")
        .with_description("A theme variable definition")
        .with_path_provider<lnav_theme>(
            [](struct lnav_theme* lt, std::vector<std::string>& paths_out) {
                for (const auto& iter : lt->lt_vars) {
                    paths_out.emplace_back(iter.first);
                }
            })
        .for_field(&lnav_theme::lt_vars),
};

static const struct json_path_container theme_def_handlers = {
    yajlpp::property_handler("vars")
        .with_description("Variables definitions that are used in this theme.")
        .with_children(theme_vars_handlers),

    yajlpp::property_handler("icons")
        .with_description("Icons for UI elements.")
        .with_children(theme_icons_handlers),

    yajlpp::property_handler("styles")
        .with_description("Styles for log messages.")
        .with_children(theme_styles_handlers),

    yajlpp::property_handler("syntax-styles")
        .with_description("Styles for syntax highlighting in text files.")
        .with_children(theme_syntax_styles_handlers),

    yajlpp::property_handler("status-styles")
        .with_description("Styles for the user-interface components.")
        .with_children(theme_status_styles_handlers),

    yajlpp::property_handler("log-level-styles")
        .with_description("Styles for each log message level.")
        .with_children(theme_log_level_styles_handlers),

    yajlpp::property_handler("highlights")
        .with_description("Styles for text highlights.")
        .with_children(theme_highlights_handlers),
};

static const struct json_path_container theme_defs_handlers = {
    yajlpp::pattern_property_handler("(?<theme_name>[\\w\\-]+)")
        .with_description("Theme definitions")
        .with_obj_provider<lnav_theme, _lnav_config>(
            [](const yajlpp_provider_context& ypc, _lnav_config* root) {
                lnav_theme& lt
                    = root->lc_ui_theme_defs[ypc.get_substr("theme_name")];

                return &lt;
            })
        .with_path_provider<_lnav_config>(
            [](struct _lnav_config* cfg, std::vector<std::string>& paths_out) {
                for (const auto& iter : cfg->lc_ui_theme_defs) {
                    paths_out.emplace_back(iter.first);
                }
            })
        .with_obj_deleter(
            +[](const yajlpp_provider_context& ypc, _lnav_config* root) {
                root->lc_ui_theme_defs.erase(ypc.get_substr("theme_name"));
            })
        .with_children(theme_def_handlers),
};

static const struct json_path_container ui_handlers = {
    yajlpp::property_handler("clock-format")
        .with_synopsis("format")
        .with_description("The format for the clock displayed in "
                          "the top-left corner using strftime(3) conversions")
        .with_example("%a %b %d %H:%M:%S %Z")
        .for_field(&_lnav_config::lc_top_status_cfg,
                   &top_status_source_cfg::tssc_clock_format),
    yajlpp::property_handler("dim-text")
        .with_synopsis("bool")
        .with_description("Reduce the brightness of text (useful for xterms). "
                          "This setting can be useful when running in an xterm "
                          "where the white color is very bright.")
        .for_field(&_lnav_config::lc_ui_dim_text),
    yajlpp::property_handler("default-colors")
        .with_synopsis("bool")
        .with_description(
            "Use default terminal background and foreground colors "
            "instead of black and white for all text coloring.  This setting "
            "can be useful when transparent background or alternate color "
            "theme terminal is used.")
        .for_field(&_lnav_config::lc_ui_default_colors),
    yajlpp::property_handler("keymap")
        .with_synopsis("keymap_name")
        .with_description("The name of the keymap to use.")
        .for_field(&_lnav_config::lc_ui_keymap),
    yajlpp::property_handler("theme")
        .with_synopsis("theme_name")
        .with_description("The name of the theme to use.")
        .for_field(&_lnav_config::lc_ui_theme),
    yajlpp::property_handler("theme-defs")
        .with_description("Theme definitions.")
        .with_children(theme_defs_handlers),
    yajlpp::property_handler("mouse")
        .with_description("Mouse-related settings")
        .with_children(mouse_handlers),
    yajlpp::property_handler("movement")
        .with_description("Log file cursor movement mode settings")
        .with_children(movement_handlers),
    yajlpp::property_handler("keymap-defs")
        .with_description("Keymap definitions.")
        .with_children(keymap_defs_handlers),
};

static const struct json_path_container archive_handlers = {
    yajlpp::property_handler("min-free-space")
        .with_synopsis("<bytes>")
        .with_description(
            "The minimum free space, in bytes, to maintain when unpacking "
            "archives")
        .with_min_value(0)
        .for_field(&_lnav_config::lc_archive_manager,
                   &archive_manager::config::amc_min_free_space),
    yajlpp::property_handler("cache-ttl")
        .with_synopsis("<duration>")
        .with_description(
            "The time-to-live for unpacked archives, expressed as a duration "
            "(e.g. '3d' for three days)")
        .with_example("3d")
        .with_example("12h")
        .for_field(&_lnav_config::lc_archive_manager,
                   &archive_manager::config::amc_cache_ttl),
};

static const struct typed_json_path_container<lnav::piper::demux_def>
    demux_def_handlers = {
    yajlpp::property_handler("enabled")
        .with_description(
            "Indicates whether this demuxer will be used at the demuxing stage "
            "(defaults to 'true')")
        .for_field(&lnav::piper::demux_def::dd_enabled),
        yajlpp::property_handler("pattern")
            .with_synopsis("<regex>")
            .with_description(
                "A regular expression to match a line in a multiplexed file")
            .for_field(&lnav::piper::demux_def::dd_pattern),
        yajlpp::property_handler("control-pattern")
            .with_synopsis("<regex>")
            .with_description(
                "A regular expression to match a control line in a multiplexed "
                "file")
            .for_field(&lnav::piper::demux_def::dd_control_pattern),
};

static const struct json_path_container demux_defs_handlers = {
    yajlpp::pattern_property_handler("(?<name>[\\w\\-\\.]+)")
        .with_description("The definition of a demultiplexer")
        .with_children(demux_def_handlers)
        .for_field(&_lnav_config::lc_piper,
                   &lnav::piper::config::c_demux_definitions),
};

static const struct json_path_container piper_handlers = {
    yajlpp::property_handler("max-size")
        .with_synopsis("<bytes>")
        .with_description("The maximum size of a capture file")
        .with_min_value(128)
        .for_field(&_lnav_config::lc_piper, &lnav::piper::config::c_max_size),
    yajlpp::property_handler("rotations")
        .with_synopsis("<count>")
        .with_min_value(2)
        .with_description("The number of rotated files to keep")
        .for_field(&_lnav_config::lc_piper, &lnav::piper::config::c_rotations),
    yajlpp::property_handler("ttl")
        .with_synopsis("<duration>")
        .with_description(
            "The time-to-live for captured data, expressed as a duration "
            "(e.g. '3d' for three days)")
        .with_example("3d")
        .with_example("12h")
        .for_field(&_lnav_config::lc_piper, &lnav::piper::config::c_ttl),
};

static const struct json_path_container file_vtab_handlers = {
    yajlpp::property_handler("max-content-size")
        .with_synopsis("<bytes>")
        .with_description(
            "The maximum allowed file size for the content column")
        .with_min_value(0)
        .for_field(&_lnav_config::lc_file_vtab,
                   &file_vtab::config::fvc_max_content_size),
};

static const struct json_path_container textfile_handlers = {
    yajlpp::property_handler("max-unformatted-line-length")
        .with_synopsis("<bytes>")
        .with_description("The maximum allowed length for a line in a text "
                          "file before formatting is automatically applied")
        .with_min_value(0)
        .for_field(&_lnav_config::lc_textfile,
                   &lnav::textfile::config::c_max_unformatted_line_length),
};

static const struct json_path_container logfile_handlers = {
    yajlpp::property_handler("max-unrecognized-lines")
        .with_synopsis("<lines>")
        .with_description("The maximum number of lines in a file to use when "
                          "detecting the format")
        .with_min_value(1)
        .for_field(&_lnav_config::lc_logfile,
                   &lnav::logfile::config::lc_max_unrecognized_lines),
};

static const struct json_path_container ssh_config_handlers = {
    yajlpp::pattern_property_handler("(?<config_name>\\w+)")
        .with_synopsis("name")
        .with_description("Set an SSH configuration value")
        .with_path_provider<_lnav_config>(
            [](auto* m, std::vector<std::string>& paths_out) {
                for (const auto& pair : m->lc_tailer.c_ssh_config) {
                    paths_out.emplace_back(pair.first);
                }
            })
        .for_field(&_lnav_config::lc_tailer, &tailer::config::c_ssh_config),
};

static const struct json_path_container ssh_option_handlers = {
    yajlpp::pattern_property_handler("(?<option_name>\\w+)")
        .with_synopsis("name")
        .with_description("Set an option to be passed to the SSH command")
        .for_field(&_lnav_config::lc_tailer, &tailer::config::c_ssh_options),
};

static const struct json_path_container ssh_handlers = {
    yajlpp::property_handler("command")
        .with_synopsis("ssh-command")
        .with_description("The SSH command to execute")
        .for_field(&_lnav_config::lc_tailer, &tailer::config::c_ssh_cmd),
    yajlpp::property_handler("transfer-command")
        .with_synopsis("command")
        .with_description(
            "Command executed on the remote host when transferring the file")
        .for_field(&_lnav_config::lc_tailer, &tailer::config::c_transfer_cmd),
    yajlpp::property_handler("start-command")
        .with_synopsis("command")
        .with_description(
            "Command executed on the remote host to start the tailer")
        .for_field(&_lnav_config::lc_tailer, &tailer::config::c_start_cmd),
    yajlpp::property_handler("flags")
        .with_description("The flags to pass to the SSH command")
        .for_field(&_lnav_config::lc_tailer, &tailer::config::c_ssh_flags),
    yajlpp::property_handler("options")
        .with_description("The options to pass to the SSH command")
        .with_children(ssh_option_handlers),
    yajlpp::property_handler("config")
        .with_description(
            "The ssh_config options to pass to SSH with the -o option")
        .with_children(ssh_config_handlers),
};

static const struct json_path_container remote_handlers = {
    yajlpp::property_handler("cache-ttl")
        .with_synopsis("<duration>")
        .with_description("The time-to-live for files copied from remote "
                          "hosts, expressed as a duration "
                          "(e.g. '3d' for three days)")
        .with_example("3d")
        .with_example("12h")
        .for_field(&_lnav_config::lc_tailer, &tailer::config::c_cache_ttl),
    yajlpp::property_handler("ssh")
        .with_description(
            "Settings related to the ssh command used to contact remote "
            "machines")
        .with_children(ssh_handlers),
};

static const struct json_path_container sysclip_impl_cmd_handlers = json_path_container{
    yajlpp::property_handler("write")
        .with_synopsis("<command>")
        .with_description("The command used to write to the clipboard")
        .with_example("pbcopy")
        .for_field(&sysclip::clip_commands::cc_write),
    yajlpp::property_handler("read")
        .with_synopsis("<command>")
        .with_description("The command used to read from the clipboard")
        .with_example("pbpaste")
        .for_field(&sysclip::clip_commands::cc_read),
}
    .with_description("Container for the commands used to read from and write to the system clipboard")
    .with_definition_id("clip-commands");

static const struct json_path_container sysclip_impl_handlers = {
    yajlpp::property_handler("test")
        .with_synopsis("<command>")
        .with_description(
            "The command that checks if a clipboard command is available")
        .with_example("command -v pbcopy")
        .for_field(&sysclip::clipboard::c_test_command),
    yajlpp::property_handler("general")
        .with_description("Commands to work with the general clipboard")
        .for_child(&sysclip::clipboard::c_general)
        .with_children(sysclip_impl_cmd_handlers),
    yajlpp::property_handler("find")
        .with_description("Commands to work with the find clipboard")
        .for_child(&sysclip::clipboard::c_find)
        .with_children(sysclip_impl_cmd_handlers),
};

static const struct json_path_container sysclip_impls_handlers = {
    yajlpp::pattern_property_handler("(?<clipboard_impl_name>[\\w\\-]+)")
        .with_synopsis("<name>")
        .with_description("Clipboard implementation")
        .with_obj_provider<sysclip::clipboard, _lnav_config>(
            [](const yajlpp_provider_context& ypc, _lnav_config* root) {
                auto& retval
                    = root->lc_sysclip.c_clipboard_impls[ypc.get_substr(
                        "clipboard_impl_name")];
                return &retval;
            })
        .with_path_provider<_lnav_config>(
            [](struct _lnav_config* cfg, std::vector<std::string>& paths_out) {
                for (const auto& iter : cfg->lc_sysclip.c_clipboard_impls) {
                    paths_out.emplace_back(iter.first);
                }
            })
        .with_children(sysclip_impl_handlers),
};

static const struct json_path_container sysclip_handlers = {
    yajlpp::property_handler("impls")
        .with_description("Clipboard implementations")
        .with_children(sysclip_impls_handlers),
};

static const json_path_container opener_impl_handlers = {
    yajlpp::property_handler("test")
        .with_synopsis("<command>")
        .with_description(
            "The command that checks if an external opener is available")
        .with_example("command -v open")
        .for_field(&lnav::external_opener::impl::i_test_command),
    yajlpp::property_handler("command")
        .with_description("The command used to open a file or URL")
        .with_example("open")
        .for_field(&lnav::external_opener::impl::i_command),
};

static const json_path_container opener_impls_handlers = {
    yajlpp::pattern_property_handler("(?<opener_impl_name>[\\w\\-]+)")
        .with_synopsis("<name>")
        .with_description("External opener implementation")
        .with_obj_provider<lnav::external_opener::impl, _lnav_config>(
            [](const yajlpp_provider_context& ypc, _lnav_config* root) {
                auto& retval = root->lc_opener
                                   .c_impls[ypc.get_substr("opener_impl_name")];
                return &retval;
            })
        .with_path_provider<_lnav_config>(
            [](struct _lnav_config* cfg, std::vector<std::string>& paths_out) {
                for (const auto& iter : cfg->lc_opener.c_impls) {
                    paths_out.emplace_back(iter.first);
                }
            })
        .with_children(opener_impl_handlers),
};

static const struct json_path_container opener_handlers = {
    yajlpp::property_handler("impls")
        .with_description("External opener implementations")
        .with_children(opener_impls_handlers),
};

static const struct json_path_container log_source_watch_expr_handlers = {
    yajlpp::property_handler("expr")
        .with_synopsis("<SQL-expression>")
        .with_description("The SQL expression to execute for each input line. "
                          "If expression evaluates to true, a 'log message "
                          "detected' event will be published.")
        .for_field(&logfile_sub_source_ns::watch_expression::we_expr),
    yajlpp::property_handler("enabled")
        .with_description("Indicates whether or not this expression should be "
                          "evaluated during log processing.")
        .for_field(&logfile_sub_source_ns::watch_expression::we_enabled),
};

static const struct json_path_container log_source_watch_handlers = {
    yajlpp::pattern_property_handler("(?<watch_name>[\\w\\.\\-]+)")
        .with_synopsis("<name>")
        .with_description("A log message watch expression")
        .with_obj_provider<logfile_sub_source_ns::watch_expression,
                           _lnav_config>(
            [](const yajlpp_provider_context& ypc, _lnav_config* root) {
                auto& retval = root->lc_log_source
                                   .c_watch_exprs[ypc.get_substr("watch_name")];
                return &retval;
            })
        .with_path_provider<_lnav_config>(
            [](struct _lnav_config* cfg, std::vector<std::string>& paths_out) {
                for (const auto& iter : cfg->lc_log_source.c_watch_exprs) {
                    paths_out.emplace_back(iter.first);
                }
            })
        .with_obj_deleter(
            +[](const yajlpp_provider_context& ypc, _lnav_config* root) {
                root->lc_log_source.c_watch_exprs.erase(
                    ypc.get_substr("watch_name"));
            })
        .with_children(log_source_watch_expr_handlers),
};

static const struct json_path_container annotation_handlers = {
    yajlpp::property_handler("description")
        .with_synopsis("<text>")
        .with_description("A description of this annotation")
        .for_field(&lnav::log::annotate::annotation_def::a_description),
    yajlpp::property_handler("condition")
        .with_synopsis("<SQL-expression>")
        .with_description(
            "The SQLite expression to execute for a log message that "
            "determines whether or not this annotation is applicable.  The "
            "expression is evaluated the same way as a filter expression")
        .with_min_length(1)
        .for_field(&lnav::log::annotate::annotation_def::a_condition),
    yajlpp::property_handler("handler")
        .with_synopsis("<script>")
        .with_description("The script to execute to generate the annotation "
                          "content. A JSON object with the log message content "
                          "will be sent to the script on the standard input")
        .with_min_length(1)
        .for_field(&lnav::log::annotate::annotation_def::a_handler),
};

static const struct json_path_container annotations_handlers = {
    yajlpp::pattern_property_handler(R"((?<annotation_name>[\w\.\-]+))")
        .with_obj_provider<lnav::log::annotate::annotation_def, _lnav_config>(
            [](const yajlpp_provider_context& ypc, _lnav_config* root) {
                auto* retval = &(root->lc_log_annotations
                                     .a_definitions[ypc.get_substr_i(0)]);

                return retval;
            })
        .with_path_provider<_lnav_config>(
            [](struct _lnav_config* cfg, std::vector<std::string>& paths_out) {
                for (const auto& iter : cfg->lc_log_annotations.a_definitions) {
                    paths_out.emplace_back(iter.first.to_string());
                }
            })
        .with_children(annotation_handlers),
};

static const struct json_path_container log_date_time_handlers = {
    yajlpp::property_handler("convert-zoned-to-local")
        .with_description("Convert timestamps with ")
        .with_pattern(R"(^[\w\-]+(?!\.lnav)$)")
        .for_field(&_lnav_config::lc_log_date_time,
                   &date_time_scanner_ns::config::c_zoned_to_local),
};

static const struct json_path_container log_source_handlers = {
    yajlpp::property_handler("date-time")
        .with_description("Settings related to log message dates and times")
        .with_children(log_date_time_handlers),
    yajlpp::property_handler("watch-expressions")
        .with_description("Log message watch expressions")
        .with_children(log_source_watch_handlers),
    yajlpp::property_handler("annotations").with_children(annotations_handlers),
    yajlpp::property_handler("demux")
        .with_description("Demultiplexer definitions")
        .with_children(demux_defs_handlers),
};

static const struct json_path_container url_scheme_handlers = {
    yajlpp::property_handler("handler")
        .with_description(
            "The name of the lnav script that can handle URLs "
            "with of this scheme.  This should not include the '.lnav' suffix.")
        .with_pattern(R"(^[\w\-]+(?!\.lnav)$)")
        .for_field(&lnav::url_handler::scheme::p_handler),
};

static const struct json_path_container url_handlers = {
    yajlpp::pattern_property_handler(R"((?<url_scheme>[a-z][\w\-\+\.]+))")
        .with_description("Definition of a custom URL scheme")
        .with_obj_provider<lnav::url_handler::scheme, _lnav_config>(
            [](const yajlpp_provider_context& ypc, _lnav_config* root) {
                auto& retval = root->lc_url_handlers
                                   .c_schemes[ypc.get_substr("url_scheme")];
                return &retval;
            })
        .with_path_provider<_lnav_config>(
            [](struct _lnav_config* cfg, std::vector<std::string>& paths_out) {
                for (const auto& iter : cfg->lc_url_handlers.c_schemes) {
                    paths_out.emplace_back(iter.first);
                }
            })
        .with_children(url_scheme_handlers),
};

static const struct json_path_container tuning_handlers = {
    yajlpp::property_handler("archive-manager")
        .with_description("Settings related to opening archive files")
        .with_children(archive_handlers),
    yajlpp::property_handler("piper")
        .with_description("Settings related to capturing piped data")
        .with_children(piper_handlers),
    yajlpp::property_handler("file-vtab")
        .with_description("Settings related to the lnav_file virtual-table")
        .with_children(file_vtab_handlers),
    yajlpp::property_handler("logfile")
        .with_description("Settings related to log files")
        .with_children(logfile_handlers),
    yajlpp::property_handler("remote")
        .with_description("Settings related to remote file support")
        .with_children(remote_handlers),
    yajlpp::property_handler("clipboard")
        .with_description("Settings related to the clipboard")
        .with_children(sysclip_handlers),
    yajlpp::property_handler("external-opener")
        .with_description("Settings related to opening external files/URLs")
        .with_children(opener_handlers),
    yajlpp::property_handler("textfile")
        .with_description("Settings related to text file handling")
        .with_children(textfile_handlers),
    yajlpp::property_handler("url-scheme")
        .with_description("Settings related to custom URL handling")
        .with_children(url_handlers),
};

const char* DEFAULT_CONFIG_SCHEMA
    = "https://lnav.org/schemas/config-v1.schema.json";

static const std::set<std::string> SUPPORTED_CONFIG_SCHEMAS = {
    DEFAULT_CONFIG_SCHEMA,
};

const char* DEFAULT_FORMAT_SCHEMA
    = "https://lnav.org/schemas/format-v1.schema.json";

const std::set<std::string> SUPPORTED_FORMAT_SCHEMAS = {
    DEFAULT_FORMAT_SCHEMA,
};

static int
read_id(yajlpp_parse_context* ypc, const unsigned char* str, size_t len)
{
    auto file_id = std::string((const char*) str, len);

    if (SUPPORTED_CONFIG_SCHEMAS.count(file_id) == 0) {
        const auto* handler = ypc->ypc_current_handler;
        attr_line_t notes{"expecting one of the following $schema values:"};

        for (const auto& schema : SUPPORTED_CONFIG_SCHEMAS) {
            notes.append("\n").append(
                lnav::roles::symbol(fmt::format(FMT_STRING("  {}"), schema)));
        }
        ypc->report_error(
            lnav::console::user_message::error(
                attr_line_t()
                    .append_quoted(lnav::roles::symbol(file_id))
                    .append(
                        " is not a supported configuration $schema version"))
                .with_snippet(ypc->get_snippet())
                .with_note(notes)
                .with_help(handler->get_help_text(ypc)));
    }

    return 1;
}

const json_path_container lnav_config_handlers = json_path_container {
    json_path_handler("$schema", read_id)
        .with_synopsis("<schema-uri>")
        .with_description("The URI that specifies the schema that describes this type of file")
        .with_example(DEFAULT_CONFIG_SCHEMA),

    yajlpp::property_handler("tuning")
        .with_description("Internal settings")
        .with_children(tuning_handlers),

    yajlpp::property_handler("ui")
        .with_description("User-interface settings")
        .with_children(ui_handlers),

    yajlpp::property_handler("log")
        .with_description("Log message settings")
        .with_children(log_source_handlers),

    yajlpp::property_handler("global")
        .with_description("Global variable definitions")
        .with_children(global_var_handlers),
}
    .with_schema_id(*SUPPORTED_CONFIG_SCHEMAS.cbegin());

class active_key_map_listener : public lnav_config_listener {
public:
    active_key_map_listener() : lnav_config_listener(__FILE__) {}

    void reload_config(error_reporter& reporter) override
    {
        lnav_config.lc_active_keymap = lnav_config.lc_ui_keymaps["default"];
        for (const auto& pair :
             lnav_config.lc_ui_keymaps[lnav_config.lc_ui_keymap].km_seq_to_cmd)
        {
            if (pair.second.kc_cmd.pp_value.empty()) {
                lnav_config.lc_active_keymap.km_seq_to_cmd.erase(pair.first);
            } else {
                lnav_config.lc_active_keymap.km_seq_to_cmd[pair.first]
                    = pair.second;
            }
        }

        auto& ec = injector::get<exec_context&>();
        for (const auto& pair : lnav_config.lc_active_keymap.km_seq_to_cmd) {
            if (pair.second.kc_id.empty()) {
                continue;
            }

            auto keyseq_sf = string_fragment::from_str(pair.first);
            std::string keystr;
            if (keyseq_sf.startswith("f")) {
                auto sv = keyseq_sf.to_string_view();
                int32_t value;
                auto scan_res = scn::scan(sv, "f{}", value);
                if (!scan_res) {
                    log_error("invalid function key sequence: %s", keyseq_sf);
                    continue;
                }
                if (value < 0 || value > 64) {
                    log_error("invalid function key number: %s", keyseq_sf);
                    continue;
                }

                keystr = toupper(pair.first);
            } else {
                auto sv
                    = string_fragment::from_str(pair.first).to_string_view();
                while (!sv.empty()) {
                    int32_t value;
                    auto scan_res = scn::scan(sv, "x{:2x}", value);
                    if (!scan_res) {
                        log_error("invalid key sequence: %s",
                                  pair.first.c_str());
                        break;
                    }
                    auto ch = (char) (value & 0xff);
                    switch (ch) {
                        case '\t':
                            keystr.append("TAB");
                            break;
                        case '\r':
                            keystr.append("ENTER");
                            break;
                        default:
                            keystr.push_back(ch);
                            break;
                    }
                    sv = scan_res.range_as_string_view();
                }
            }

            if (!keystr.empty()) {
                ec.ec_global_vars[pair.second.kc_id] = keystr;
            }
        }
    }
};

static active_key_map_listener KEYMAP_LISTENER;

Result<config_file_type, std::string>
detect_config_file_type(const std::filesystem::path& path)
{
    static const char* id_path[] = {"$schema", nullptr};

    auto content = TRY(lnav::filesystem::read_file(path));
    if (startswith(content, "#")) {
        content.insert(0, "//");
    }

    char error_buffer[1024];
    auto content_tree = std::unique_ptr<yajl_val_s, decltype(&yajl_tree_free)>(
        yajl_tree_parse(content.c_str(), error_buffer, sizeof(error_buffer)),
        yajl_tree_free);
    if (content_tree == nullptr) {
        return Err(
            fmt::format(FMT_STRING("JSON parsing failed -- {}"), error_buffer));
    }

    auto* id_val = yajl_tree_get(content_tree.get(), id_path, yajl_t_string);
    if (id_val != nullptr) {
        if (SUPPORTED_CONFIG_SCHEMAS.count(id_val->u.string)) {
            return Ok(config_file_type::CONFIG);
        }
        if (SUPPORTED_FORMAT_SCHEMAS.count(id_val->u.string)) {
            return Ok(config_file_type::FORMAT);
        }
        return Err(fmt::format(
            FMT_STRING("unsupported configuration version in file -- {}"),
            id_val->u.string));
    }
    return Ok(config_file_type::FORMAT);
}

static void
load_config_from(_lnav_config& lconfig,
                 const std::filesystem::path& path,
                 std::vector<lnav::console::user_message>& errors)
{
    yajlpp_parse_context ypc(intern_string::lookup(path.string()),
                             &lnav_config_handlers);
    struct config_userdata ud(errors);
    auto_fd fd;

    log_info("loading configuration from %s", path.c_str());
    ypc.ypc_locations = &lnav_config_locations;
    ypc.with_obj(lconfig);
    ypc.ypc_userdata = &ud;
    ypc.with_error_reporter(config_error_reporter);
    if ((fd = lnav::filesystem::openp(path, O_RDONLY)) == -1) {
        if (errno != ENOENT) {
            errors.emplace_back(
                lnav::console::user_message::error(
                    attr_line_t("unable to open configuration file: ")
                        .append(lnav::roles::file(path)))
                    .with_errno_reason());
        }
    } else {
        char buffer[2048];
        ssize_t rc = -1;

        auto handle = yajlpp::alloc_handle(&ypc.ypc_callbacks, &ypc);
        yajl_config(handle, yajl_allow_comments, 1);
        yajl_config(handle, yajl_allow_multiple_values, 1);
        ypc.ypc_handle = handle;
        while (true) {
            rc = read(fd, buffer, sizeof(buffer));
            if (rc == 0) {
                break;
            }
            if (rc == -1) {
                errors.emplace_back(
                    lnav::console::user_message::error(
                        attr_line_t("unable to read format file: ")
                            .append(lnav::roles::file(path)))
                        .with_errno_reason());
                break;
            }
            if (ypc.parse((const unsigned char*) buffer, rc) != yajl_status_ok)
            {
                break;
            }
        }
        if (rc == 0) {
            ypc.complete_parse();
        }
    }
}

static bool
load_default_config(struct _lnav_config& config_obj,
                    const std::string& path,
                    const bin_src_file& bsf,
                    std::vector<lnav::console::user_message>& errors)
{
    yajlpp_parse_context ypc_builtin(intern_string::lookup(bsf.get_name()),
                                     &lnav_config_handlers);
    struct config_userdata ud(errors);

    auto handle
        = yajlpp::alloc_handle(&ypc_builtin.ypc_callbacks, &ypc_builtin);
    ypc_builtin.ypc_locations = &lnav_config_locations;
    ypc_builtin.with_handle(handle);
    ypc_builtin.with_obj(config_obj);
    ypc_builtin.with_error_reporter(config_error_reporter);
    ypc_builtin.ypc_userdata = &ud;

    if (path != "*") {
        ypc_builtin.ypc_ignore_unused = true;
        ypc_builtin.ypc_active_paths.insert(path);
    }

    yajl_config(handle, yajl_allow_comments, 1);
    yajl_config(handle, yajl_allow_multiple_values, 1);
    ypc_builtin.parse_doc(bsf.to_string_fragment());

    return path == "*" || ypc_builtin.ypc_active_paths.empty();
}

static bool
load_default_configs(struct _lnav_config& config_obj,
                     const std::string& path,
                     std::vector<lnav::console::user_message>& errors)
{
    auto retval = false;

    for (auto& bsf : lnav_config_json) {
        retval = load_default_config(config_obj, path, bsf, errors) || retval;
    }

    return retval;
}

void
load_config(const std::vector<std::filesystem::path>& extra_paths,
            std::vector<lnav::console::user_message>& errors)
{
    auto user_config = lnav::paths::dotlnav() / "config.json";

    for (auto& bsf : lnav_config_json) {
        auto sample_path = lnav::paths::dotlnav() / "configs" / "default"
            / fmt::format(FMT_STRING("{}.sample"), bsf.get_name());

        auto write_res = lnav::filesystem::write_file(sample_path,
                                                      bsf.to_string_fragment());
        if (write_res.isErr()) {
            fprintf(stderr,
                    "error:unable to write default config file: %s -- %s\n",
                    sample_path.c_str(),
                    write_res.unwrapErr().c_str());
        }
    }

    {
        log_info("loading builtin configuration into default");
        load_default_configs(lnav_default_config, "*", errors);
        log_info("loading builtin configuration into base");
        load_default_configs(lnav_config, "*", errors);

        log_info("loading installed configuration files");
        for (const auto& extra_path : extra_paths) {
            auto config_path = extra_path / "configs/*/*.json";
            static_root_mem<glob_t, globfree> gl;

            log_info("loading configuration files in configs directories: %s",
                     config_path.c_str());
            if (glob(config_path.c_str(), 0, nullptr, gl.inout()) == 0) {
                for (size_t lpc = 0; lpc < gl->gl_pathc; lpc++) {
                    load_config_from(lnav_config, gl->gl_pathv[lpc], errors);
                    if (errors.empty()) {
                        load_config_from(
                            lnav_default_config, gl->gl_pathv[lpc], errors);
                    }
                }
            }
        }
        for (const auto& extra_path : extra_paths) {
            for (const auto& pat :
                 {"formats/*/config.json", "formats/*/config.*.json"})
            {
                auto config_path = extra_path / pat;
                static_root_mem<glob_t, globfree> gl;

                log_info(
                    "loading configuration files in format directories: %s",
                    config_path.c_str());
                if (glob(config_path.c_str(), 0, nullptr, gl.inout()) == 0) {
                    for (size_t lpc = 0; lpc < gl->gl_pathc; lpc++) {
                        load_config_from(
                            lnav_config, gl->gl_pathv[lpc], errors);
                        if (errors.empty()) {
                            load_config_from(
                                lnav_default_config, gl->gl_pathv[lpc], errors);
                        }
                    }
                }
            }
        }

        log_info("loading user configuration");
        load_config_from(lnav_config, user_config, errors);
    }

    reload_config(errors);

    rollback_lnav_config = lnav_config;
}

std::string
dump_config()
{
    yajlpp_gen gen;
    yajlpp_gen_context ygc(gen, lnav_config_handlers);

    yajl_gen_config(gen, yajl_gen_beautify, true);
    ygc.with_obj(lnav_config);
    ygc.gen();

    return gen.to_string_fragment().to_string();
}

void
reset_config(const std::string& path)
{
    std::vector<lnav::console::user_message> errors;

    load_default_configs(lnav_config, path, errors);
    if (path != "*") {
        static const auto INPUT_SRC = intern_string::lookup("input");

        yajlpp_parse_context ypc(INPUT_SRC, &lnav_config_handlers);
        ypc.set_path(path)
            .with_obj(lnav_config)
            .with_error_reporter([&errors](const auto& ypc, auto msg) {
                errors.push_back(msg);
            });
        ypc.ypc_active_paths.insert(path);
        ypc.update_callbacks();
        const json_path_handler_base* jph = ypc.ypc_current_handler;

        if (!ypc.ypc_handler_stack.empty()) {
            jph = ypc.ypc_handler_stack.back();
        }

        if (jph != nullptr && jph->jph_children && jph->jph_obj_deleter) {
            auto key_start = ypc.ypc_path_index_stack.back();
            auto path_frag = string_fragment::from_byte_range(
                ypc.ypc_path.data(), key_start + 1, ypc.ypc_path.size());
            auto md = jph->jph_regex->create_match_data();
            yajlpp_provider_context provider_ctx{&md, static_cast<size_t>(-1)};
            jph->jph_regex->capture_from(path_frag).into(md).matches();

            ypc.ypc_obj_stack.pop();
            jph->jph_obj_deleter(provider_ctx, ypc.ypc_obj_stack.top());
        }
    }

    reload_config(errors);

    for (const auto& err : errors) {
        log_debug("reset %s", err.um_message.get_string().c_str());
    }
}

std::string
save_config()
{
    auto user_config = lnav::paths::dotlnav() / "config.json";

    yajlpp_gen gen;
    yajlpp_gen_context ygc(gen, lnav_config_handlers);

    ygc.with_default_obj(lnav_default_config).with_obj(lnav_config);
    ygc.gen();

    auto config_str = gen.to_string_fragment().to_string();
    char errbuf[1024];
    auto_mem<yajl_val_s> tree(yajl_tree_free);

    tree = yajl_tree_parse(config_str.c_str(), errbuf, sizeof(errbuf));

    if (tree == nullptr) {
        return fmt::format(
            FMT_STRING("error: unable to save configuration -- {}"), errbuf);
    }

    yajl_cleanup_tree(tree);

    yajlpp_gen clean_gen;

    yajl_gen_config(clean_gen, yajl_gen_beautify, true);
    yajl_gen_tree(clean_gen, tree);

    auto write_res = lnav::filesystem::write_file(
        user_config, clean_gen.to_string_fragment());
    if (write_res.isErr()) {
        return fmt::format(
            FMT_STRING("error: unable to write configuration file: {} -- {}"),
            user_config.string(),
            write_res.unwrapErr());
    }

    return "info: configuration saved";
}

void
reload_config(std::vector<lnav::console::user_message>& errors)
{
    auto* curr = lnav_config_listener::LISTENER_LIST;

    while (curr != nullptr) {
        auto reporter = [&errors](const void* cfg_value,
                                  const lnav::console::user_message& errmsg) {
            log_error("configuration error: %s",
                      errmsg.to_attr_line().get_string().c_str());
            auto cb = [&cfg_value, &errors, &errmsg](
                          const json_path_handler_base& jph,
                          const std::string& path,
                          const void* mem) {
                if (mem != cfg_value) {
                    return;
                }

                auto loc_iter
                    = lnav_config_locations.find(intern_string::lookup(path));
                auto has_loc = loc_iter != lnav_config_locations.end();
                auto um = has_loc
                    ? lnav::console::user_message::error(
                          attr_line_t()
                              .append("invalid value for property ")
                              .append_quoted(lnav::roles::symbol(path)))
                          .with_reason(errmsg)
                    : errmsg;
                um.with_help(jph.get_help_text(path));

                if (has_loc) {
                    um.with_snippet(
                        lnav::console::snippet::from(loc_iter->second.sl_source,
                                                     "")
                            .with_line(loc_iter->second.sl_line_number));
                } else {
                    um.um_message
                        = attr_line_t()
                              .append("missing value for property ")
                              .append_quoted(lnav::roles::symbol(path))
                              .move();
                }

                errors.emplace_back(um);
            };

            for (const auto& jph : lnav_config_handlers.jpc_children) {
                jph.walk(cb, &lnav_config);
            }
        };

        curr->reload_config(reporter);
        curr = curr->lcl_next;
    }
}
