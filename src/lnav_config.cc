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

#include "auto_fd.hh"
#include "auto_mem.hh"
#include "base/auto_pid.hh"
#include "base/fs_util.hh"
#include "base/injector.bind.hh"
#include "base/injector.hh"
#include "base/lnav_log.hh"
#include "base/paths.hh"
#include "base/string_util.hh"
#include "bin2c.hh"
#include "config.h"
#include "default-config.h"
#include "styling.hh"
#include "yajlpp/yajlpp.hh"
#include "yajlpp/yajlpp_def.hh"

using namespace std;

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

static auto fvc = injector::bind<file_vtab::config>::to_instance(
    +[]() { return &lnav_config.lc_file_vtab; });

static auto lc = injector::bind<lnav::logfile::config>::to_instance(
    +[]() { return &lnav_config.lc_logfile; });

static auto tc = injector::bind<tailer::config>::to_instance(
    +[]() { return &lnav_config.lc_tailer; });

static auto scc = injector::bind<sysclip::config>::to_instance(
    +[]() { return &lnav_config.lc_sysclip; });

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

    for (auto sub_path : subdirs) {
        auto full_path = path / sub_path;

        log_perror(mkdir(full_path.c_str(), 0755));
    }

    lnav_log_crash_dir = strdup(path.c_str());

    {
        static_root_mem<glob_t, globfree> gl;
        auto crash_glob = path / "crash/*";

        if (glob(crash_glob.c_str(), GLOB_NOCHECK, nullptr, gl.inout()) == 0) {
            for (int lpc = 0; lpc < ((int) gl->gl_pathc - MAX_CRASH_LOG_COUNT);
                 lpc++) {
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

                if (chrono::system_clock::from_time_t(st.st_mtime) > old_time) {
                    continue;
                }

                log_debug("Removing old stdin capture: %s", gl->gl_pathv[lpc]);
                log_perror(remove(gl->gl_pathv[lpc]));
            }
        }
    }
}

bool
install_from_git(const char* repo)
{
    static const std::regex repo_name_converter("[^\\w]");

    auto formats_path = lnav::paths::dotlnav() / "formats";
    auto configs_path = lnav::paths::dotlnav() / "configs";
    auto staging_path = lnav::paths::dotlnav() / "staging";
    string local_name = std::regex_replace(repo, repo_name_converter, "_");

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
        if (ghc::filesystem::is_directory(local_formats_path)) {
            printf("Updating format repo: %s\n", repo);
            log_perror(chdir(local_formats_path.c_str()));
            execlp("git", "git", "pull", nullptr);
        } else if (ghc::filesystem::is_directory(local_configs_path)) {
            printf("Updating config repo: %s\n", repo);
            log_perror(chdir(local_configs_path.c_str()));
            execlp("git", "git", "pull", nullptr);
        } else {
            execlp("git",
                   "git",
                   "clone",
                   repo,
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

    if (ghc::filesystem::is_directory(local_staging_path)) {
        auto config_path = local_staging_path / "*.json";
        static_root_mem<glob_t, globfree> gl;
        bool found_config_file = false, found_format_file = false;

        if (glob(config_path.c_str(), 0, nullptr, gl.inout()) == 0) {
            for (size_t lpc = 0; lpc < gl->gl_pathc; lpc++) {
                auto json_file_path = gl->gl_pathv[lpc];
                auto file_type_result = detect_config_file_type(json_file_path);

                if (file_type_result.isErr()) {
                    fprintf(stderr,
                            "error: %s\n",
                            file_type_result.unwrapErr().c_str());
                    return false;
                }
                if (file_type_result.unwrap() == config_file_type::CONFIG) {
                    found_config_file = true;
                } else {
                    found_format_file = true;
                }
            }
        }

        if (found_config_file) {
            rename(local_staging_path.c_str(), local_configs_path.c_str());
            fprintf(stderr,
                    "info: installed configuration repo -- %s\n",
                    local_configs_path.c_str());
        } else if (found_format_file) {
            rename(local_staging_path.c_str(), local_formats_path.c_str());
            fprintf(stderr,
                    "info: installed format repo -- %s\n",
                    local_formats_path.c_str());
        } else {
            fprintf(stderr,
                    "error: cannot find a valid lnav configuration or format "
                    "file\n");
            return false;
        }
    }

    return true;
}

bool
update_installs_from_git()
{
    static_root_mem<glob_t, globfree> gl;
    auto git_formats = lnav::paths::dotlnav() / "formats/*/.git";
    bool found = false, retval = true;

    if (glob(git_formats.c_str(), GLOB_NOCHECK, nullptr, gl.inout()) == 0) {
        for (int lpc = 0; lpc < (int) gl->gl_pathc; lpc++) {
            char* git_dir = dirname(gl->gl_pathv[lpc]);
            char pull_cmd[1024];

            printf("Updating formats in %s\n", git_dir);
            snprintf(pull_cmd, sizeof(pull_cmd), "cd %s && git pull", git_dir);
            int ret = system(pull_cmd);
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
            "use 'lnav -i extra' to install third-party foramts\n");
    }

    return retval;
}

static int
read_repo_path(yajlpp_parse_context* ypc, const unsigned char* str, size_t len)
{
    string path = string((const char*) str, len);

    install_from_git(path.c_str());

    return 1;
}

static struct json_path_container format_handlers
    = {json_path_handler("format-repos#", read_repo_path)};

void
install_extra_formats()
{
    auto config_root = lnav::paths::dotlnav() / "remote-config";
    auto_fd fd;

    if (access(config_root.c_str(), R_OK) == 0) {
        char pull_cmd[1024];

        printf("Updating lnav remote config repo...\n");
        snprintf(pull_cmd,
                 sizeof(pull_cmd),
                 "cd '%s' && git pull",
                 config_root.c_str());
        log_perror(system(pull_cmd));
    } else {
        char clone_cmd[1024];

        printf("Cloning lnav remote config repo...\n");
        snprintf(clone_cmd,
                 sizeof(clone_cmd),
                 "git clone https://github.com/tstack/lnav-config.git %s",
                 config_root.c_str());
        log_perror(system(clone_cmd));
    }

    auto config_json = config_root / "remote-config.json";
    if ((fd = lnav::filesystem::openp(config_json, O_RDONLY)) == -1) {
        perror("Unable to open remote-config.json");
    } else {
        yajlpp_parse_context ypc_config(config_root.string(), &format_handlers);
        auto_mem<yajl_handle_t> jhandle(yajl_free);
        unsigned char buffer[4096];
        ssize_t rc;

        jhandle = yajl_alloc(&ypc_config.ypc_callbacks, nullptr, &ypc_config);
        yajl_config(jhandle, yajl_allow_comments, 1);
        while ((rc = read(fd, buffer, sizeof(buffer))) > 0) {
            if (yajl_parse(jhandle, buffer, rc) != yajl_status_ok) {
                auto msg = yajl_get_error(jhandle, 1, buffer, rc);
                fprintf(
                    stderr, "Unable to parse remote-config.json -- %s", msg);
                yajl_free_error(jhandle, msg);
                return;
            }
        }
        if (yajl_complete_parse(jhandle) != yajl_status_ok) {
            auto msg = yajl_get_error(jhandle, 1, buffer, rc);

            fprintf(stderr, "Unable to parse remote-config.json -- %s", msg);
            yajl_free_error(jhandle, msg);
        }
    }
}

struct userdata {
    userdata(vector<string>& errors) : ud_errors(errors){};

    vector<string>& ud_errors;
};

static void
config_error_reporter(const yajlpp_parse_context& ypc,
                      lnav_log_level_t level,
                      const char* msg)
{
    if (level >= lnav_log_level_t::ERROR) {
        struct userdata* ud = (userdata*) ypc.ypc_userdata;

        ud->ud_errors.emplace_back(msg);
    } else {
        fprintf(stderr, "warning:%s\n", msg);
    }
}

static struct json_path_container key_command_handlers = {
    yajlpp::property_handler("command")
        .with_synopsis("<command>")
        .with_description(
            "The command to execute for the given key sequence.  Use a script "
            "to execute more complicated operations.")
        .with_pattern("[:|;].*")
        .with_example(":goto next hour")
        .FOR_FIELD(key_command, kc_cmd),
    yajlpp::property_handler("alt-msg")
        .with_synopsis("<msg>")
        .with_description(
            "The help message to display after the key is pressed.")
        .FOR_FIELD(key_command, kc_alt_msg)};

static struct json_path_container keymap_def_handlers
    = {yajlpp::pattern_property_handler("(?<key_seq>(?:x[0-9a-f]{2})+)")
           .with_synopsis("<utf8-key-code-in-hex>")
           .with_description(
               "Map of key codes to commands to execute.  The field names are "
               "the keys to be mapped using as a hexadecimal representation of "
               "the UTF-8 encoding.  Each byte of the UTF-8 should start with "
               "an 'x' followed by the hexadecimal representation of the byte.")
           .with_obj_provider<key_command, key_map>(
               [](const yajlpp_provider_context& ypc, key_map* km) {
                   key_command& retval
                       = km->km_seq_to_cmd[ypc.ypc_extractor.get_substr(
                           "key_seq")];

                   return &retval;
               })
           .with_path_provider<key_map>(
               [](key_map* km, vector<string>& paths_out) {
                   for (const auto& iter : km->km_seq_to_cmd) {
                       paths_out.emplace_back(iter.first);
                   }
               })
           .with_children(key_command_handlers)};

static struct json_path_container keymap_defs_handlers
    = {yajlpp::pattern_property_handler("(?<keymap_name>[\\w\\-]+)")
           .with_description("The keymap definitions")
           .with_obj_provider<key_map, _lnav_config>(
               [](const yajlpp_provider_context& ypc, _lnav_config* root) {
                   key_map& retval
                       = root->lc_ui_keymaps[ypc.ypc_extractor.get_substr(
                           "keymap_name")];
                   return &retval;
               })
           .with_path_provider<_lnav_config>(
               [](struct _lnav_config* cfg, vector<string>& paths_out) {
                   for (const auto& iter : cfg->lc_ui_keymaps) {
                       paths_out.emplace_back(iter.first);
                   }
               })
           .with_children(keymap_def_handlers)};

static struct json_path_container global_var_handlers = {
    yajlpp::pattern_property_handler("(?<var_name>\\w+)")
        .with_synopsis("<name>")
        .with_description(
            "A global variable definition.  Global variables can be referenced "
            "in scripts, SQL statements, or commands.")
        .with_path_provider<_lnav_config>(
            [](struct _lnav_config* cfg, vector<string>& paths_out) {
                for (const auto& iter : cfg->lc_global_vars) {
                    paths_out.emplace_back(iter.first);
                }
            })
        .FOR_FIELD(_lnav_config, lc_global_vars)};

static struct json_path_container style_config_handlers =
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
            .FOR_FIELD(style_config, sc_color),
        yajlpp::property_handler("background-color")
            .with_synopsis("#hex|color_name")
            .with_description(
                "The background color value for this style. The value can be "
                "the name of an xterm color, the hexadecimal value, or a theme "
                "variable reference.")
            .with_example("#2d2a2e")
            .with_example("Green")
            .FOR_FIELD(style_config, sc_background_color),
        yajlpp::property_handler("underline")
            .with_description("Indicates that the text should be underlined.")
            .FOR_FIELD(style_config, sc_underline),
        yajlpp::property_handler("bold")
            .with_description("Indicates that the text should be bolded.")
            .FOR_FIELD(style_config, sc_bold),
    }
        .with_definition_id("style");

static struct json_path_container theme_styles_handlers = {
    yajlpp::property_handler("identifier")
        .with_description("Styling for identifiers in logs")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                return &root->lt_style_identifier;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("text")
        .with_description("Styling for plain text")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                return &root->lt_style_text;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("alt-text")
        .with_description("Styling for plain text when alternating")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                return &root->lt_style_alt_text;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("error")
        .with_description("Styling for error messages")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                return &root->lt_style_error;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("ok")
        .with_description("Styling for success messages")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                return &root->lt_style_ok;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("warning")
        .with_description("Styling for warning messages")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                return &root->lt_style_warning;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("hidden")
        .with_description("Styling for hidden fields in logs")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                return &root->lt_style_hidden;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("adjusted-time")
        .with_description("Styling for timestamps that have been adjusted")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                return &root->lt_style_adjusted_time;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("skewed-time")
        .with_description(
            "Styling for timestamps that are different from the received time")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                return &root->lt_style_skewed_time;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("offset-time")
        .with_description("Styling for hidden fields")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                return &root->lt_style_offset_time;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("invalid-msg")
        .with_description("Styling for invalid log messages")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                return &root->lt_style_invalid_msg;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("popup")
        .with_description("Styling for popup windows")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                return &root->lt_style_popup;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("focused")
        .with_description("Styling for a focused row in a list view")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                return &root->lt_style_focused;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("disabled-focused")
        .with_description("Styling for a disabled focused row in a list view")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                return &root->lt_style_disabled_focused;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("scrollbar")
        .with_description("Styling for scrollbars")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                return &root->lt_style_scrollbar;
            })
        .with_children(style_config_handlers)};

static struct json_path_container theme_syntax_styles_handlers
    = {yajlpp::property_handler("keyword")
           .with_description("Styling for keywords in source files")
           .with_obj_provider<style_config, lnav_theme>(
               [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                   return &root->lt_style_keyword;
               })
           .with_children(style_config_handlers),
       yajlpp::property_handler("string")
           .with_description("Styling for single/double-quoted strings in text")
           .with_obj_provider<style_config, lnav_theme>(
               [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                   return &root->lt_style_string;
               })
           .with_children(style_config_handlers),
       yajlpp::property_handler("comment")
           .with_description("Styling for comments in source files")
           .with_obj_provider<style_config, lnav_theme>(
               [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                   return &root->lt_style_comment;
               })
           .with_children(style_config_handlers),
       yajlpp::property_handler("doc-directive")
           .with_description(
               "Styling for documentation directives in source files")
           .with_obj_provider<style_config, lnav_theme>(
               [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                   return &root->lt_style_doc_directive;
               })
           .with_children(style_config_handlers),
       yajlpp::property_handler("variable")
           .with_description("Styling for variables in text")
           .with_obj_provider<style_config, lnav_theme>(
               [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                   return &root->lt_style_variable;
               })
           .with_children(style_config_handlers),
       yajlpp::property_handler("symbol")
           .with_description("Styling for symbols in source files")
           .with_obj_provider<style_config, lnav_theme>(
               [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                   return &root->lt_style_symbol;
               })
           .with_children(style_config_handlers),
       yajlpp::property_handler("number")
           .with_description("Styling for numbers in source files")
           .with_obj_provider<style_config, lnav_theme>(
               [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                   return &root->lt_style_number;
               })
           .with_children(style_config_handlers),
       yajlpp::property_handler("re-special")
           .with_description(
               "Styling for special characters in regular expressions")
           .with_obj_provider<style_config, lnav_theme>(
               [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                   return &root->lt_style_re_special;
               })
           .with_children(style_config_handlers),
       yajlpp::property_handler("re-repeat")
           .with_description("Styling for repeats in regular expressions")
           .with_obj_provider<style_config, lnav_theme>(
               [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                   return &root->lt_style_re_repeat;
               })
           .with_children(style_config_handlers),

       yajlpp::property_handler("diff-delete")
           .with_description("Styling for deleted lines in diffs")
           .with_obj_provider<style_config, lnav_theme>(
               [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                   return &root->lt_style_diff_delete;
               })
           .with_children(style_config_handlers),
       yajlpp::property_handler("diff-add")
           .with_description("Styling for added lines in diffs")
           .with_obj_provider<style_config, lnav_theme>(
               [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                   return &root->lt_style_diff_add;
               })
           .with_children(style_config_handlers),
       yajlpp::property_handler("diff-section")
           .with_description("Styling for diffs")
           .with_obj_provider<style_config, lnav_theme>(
               [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                   return &root->lt_style_diff_section;
               })
           .with_children(style_config_handlers),
       yajlpp::property_handler("file")
           .with_description("Styling for file names in source files")
           .with_obj_provider<style_config, lnav_theme>(
               [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                   return &root->lt_style_file;
               })
           .with_children(style_config_handlers)};

static struct json_path_container theme_status_styles_handlers = {
    yajlpp::property_handler("text")
        .with_description("Styling for status bars")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                return &root->lt_style_status;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("warn")
        .with_description("Styling for warnings in status bars")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                return &root->lt_style_warn_status;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("alert")
        .with_description("Styling for alerts in status bars")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                return &root->lt_style_alert_status;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("active")
        .with_description("Styling for activity in status bars")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                return &root->lt_style_active_status;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("inactive-alert")
        .with_description("Styling for inactive alert status bars")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                return &root->lt_style_inactive_alert_status;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("inactive")
        .with_description("Styling for inactive status bars")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                return &root->lt_style_inactive_status;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("title-hotkey")
        .with_description("Styling for hotkey highlights in titles")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                return &root->lt_style_status_title_hotkey;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("title")
        .with_description("Styling for title sections of status bars")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                return &root->lt_style_status_title;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("disabled-title")
        .with_description("Styling for title sections of status bars")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                return &root->lt_style_status_disabled_title;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("subtitle")
        .with_description("Styling for subtitle sections of status bars")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                return &root->lt_style_status_subtitle;
            })
        .with_children(style_config_handlers),
    yajlpp::property_handler("hotkey")
        .with_description("Styling for hotkey highlights of status bars")
        .with_obj_provider<style_config, lnav_theme>(
            [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                return &root->lt_style_status_hotkey;
            })
        .with_children(style_config_handlers),
};

static struct json_path_container theme_log_level_styles_handlers
    = {yajlpp::pattern_property_handler(
           "(?<level>trace|debug5|debug4|debug3|debug2|debug|info|stats|notice|"
           "warning|error|critical|fatal|invalid)")
           .with_obj_provider<style_config, lnav_theme>(
               [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                   style_config& sc = root->lt_level_styles[string2level(
                       ypc.ypc_extractor.get_substr_i("level").get())];

                   return &sc;
               })
           .with_path_provider<lnav_theme>(
               [](struct lnav_theme* cfg, vector<string>& paths_out) {
                   for (int lpc = LEVEL_TRACE; lpc < LEVEL__MAX; lpc++) {
                       paths_out.emplace_back(level_names[lpc]);
                   }
               })
           .with_children(style_config_handlers)};

static struct json_path_container highlighter_handlers = {
    yajlpp::property_handler("pattern")
        .with_synopsis("regular expression")
        .with_description("The regular expression to highlight")
        .FOR_FIELD(highlighter_config, hc_regex),

    yajlpp::property_handler("style")
        .with_description(
            "The styling for the text that matches the associated pattern")
        .with_obj_provider<style_config, highlighter_config>(
            [](const yajlpp_provider_context& ypc, highlighter_config* root) {
                return &root->hc_style;
            })
        .with_children(style_config_handlers),
};

static struct json_path_container theme_highlights_handlers
    = {yajlpp::pattern_property_handler("(?<highlight_name>\\w+)")
           .with_obj_provider<highlighter_config, lnav_theme>(
               [](const yajlpp_provider_context& ypc, lnav_theme* root) {
                   highlighter_config& hc
                       = root->lt_highlights[ypc.ypc_extractor
                                                 .get_substr_i("highlight_name")
                                                 .get()];

                   return &hc;
               })
           .with_path_provider<lnav_theme>(
               [](struct lnav_theme* cfg, vector<string>& paths_out) {
                   for (const auto& pair : cfg->lt_highlights) {
                       paths_out.emplace_back(pair.first);
                   }
               })
           .with_children(highlighter_handlers)};

static struct json_path_container theme_vars_handlers
    = {yajlpp::pattern_property_handler("(?<var_name>\\w+)")
           .with_synopsis("name")
           .with_description("A theme variable definition")
           .with_path_provider<lnav_theme>(
               [](struct lnav_theme* lt, vector<string>& paths_out) {
                   for (const auto& iter : lt->lt_vars) {
                       paths_out.emplace_back(iter.first);
                   }
               })
           .FOR_FIELD(lnav_theme, lt_vars)};

static struct json_path_container theme_def_handlers = {
    yajlpp::property_handler("vars")
        .with_description("Variables definitions that are used in this theme.")
        .with_children(theme_vars_handlers),

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

static struct json_path_container theme_defs_handlers
    = {yajlpp::pattern_property_handler("(?<theme_name>[\\w\\-]+)")
           .with_description("Theme definitions")
           .with_obj_provider<lnav_theme, _lnav_config>(
               [](const yajlpp_provider_context& ypc, _lnav_config* root) {
                   lnav_theme& lt
                       = root->lc_ui_theme_defs[ypc.ypc_extractor.get_substr(
                           "theme_name")];

                   return &lt;
               })
           .with_path_provider<_lnav_config>(
               [](struct _lnav_config* cfg, vector<string>& paths_out) {
                   for (const auto& iter : cfg->lc_ui_theme_defs) {
                       paths_out.emplace_back(iter.first);
                   }
               })
           .with_children(theme_def_handlers)};

static struct json_path_container ui_handlers = {
    yajlpp::property_handler("clock-format")
        .with_synopsis("format")
        .with_description("The format for the clock displayed in "
                          "the top-left corner using strftime(3) conversions")
        .with_example("%a %b %d %H:%M:%S %Z")
        .FOR_FIELD(_lnav_config, lc_ui_clock_format),
    yajlpp::property_handler("dim-text")
        .with_synopsis("bool")
        .with_description("Reduce the brightness of text (useful for xterms). "
                          "This setting can be useful when running in an xterm "
                          "where the white color is very bright.")
        .FOR_FIELD(_lnav_config, lc_ui_dim_text),
    yajlpp::property_handler("default-colors")
        .with_synopsis("bool")
        .with_description(
            "Use default terminal background and foreground colors "
            "instead of black and white for all text coloring.  This setting "
            "can be useful when transparent background or alternate color "
            "theme terminal is used.")
        .FOR_FIELD(_lnav_config, lc_ui_default_colors),
    yajlpp::property_handler("keymap")
        .with_synopsis("keymap_name")
        .with_description("The name of the keymap to use.")
        .FOR_FIELD(_lnav_config, lc_ui_keymap),
    yajlpp::property_handler("theme")
        .with_synopsis("theme_name")
        .with_description("The name of the theme to use.")
        .FOR_FIELD(_lnav_config, lc_ui_theme),
    yajlpp::property_handler("theme-defs")
        .with_description("Theme definitions.")
        .with_children(theme_defs_handlers),
    yajlpp::property_handler("keymap-defs")
        .with_description("Keymap definitions.")
        .with_children(keymap_defs_handlers),
};

static struct json_path_container archive_handlers = {
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

static struct json_path_container file_vtab_handlers = {
    yajlpp::property_handler("max-content-size")
        .with_synopsis("<bytes>")
        .with_description(
            "The maximum allowed file size for the content column")
        .with_min_value(0)
        .for_field(&_lnav_config::lc_file_vtab,
                   &file_vtab::config::fvc_max_content_size),
};

static struct json_path_container logfile_handlers = {
    yajlpp::property_handler("max-unrecognized-lines")
        .with_synopsis("<lines>")
        .with_description("The maximum number of lines in a file to use when "
                          "detecting the format")
        .with_min_value(1)
        .for_field(&_lnav_config::lc_logfile,
                   &lnav::logfile::config::lc_max_unrecognized_lines),
};

static struct json_path_container ssh_config_handlers = {
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

static struct json_path_container ssh_option_handlers = {
    yajlpp::pattern_property_handler("(?<option_name>\\w+)")
        .with_synopsis("name")
        .with_description("Set an option to be passed to the SSH command")
        .for_field(&_lnav_config::lc_tailer, &tailer::config::c_ssh_options),
};

static struct json_path_container ssh_handlers = {
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

static struct json_path_container remote_handlers = {
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

static struct json_path_container sysclip_impl_cmd_handlers = json_path_container{
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

static struct json_path_container sysclip_impl_handlers = {
    yajlpp::property_handler("test")
        .with_synopsis("<command>")
        .with_description("The command that checks")
        .with_example("command -v pbcopy")
        .for_field(&sysclip::clipboard::c_test_command),
    yajlpp::property_handler("general")
        .with_description("Commands to work with the general clipboard")
        .with_obj_provider<sysclip::clip_commands, sysclip::clipboard>(
            [](const yajlpp_provider_context& ypc, sysclip::clipboard* root) {
                return &root->c_general;
            })
        .with_children(sysclip_impl_cmd_handlers),
    yajlpp::property_handler("find")
        .with_description("Commands to work with the find clipboard")
        .with_obj_provider<sysclip::clip_commands, sysclip::clipboard>(
            [](const yajlpp_provider_context& ypc, sysclip::clipboard* root) {
                return &root->c_find;
            })
        .with_children(sysclip_impl_cmd_handlers),
};

static struct json_path_container sysclip_impls_handlers = {
    yajlpp::pattern_property_handler("(?<clipboard_impl_name>[\\w\\-]+)")
        .with_synopsis("<name>")
        .with_description("Clipboard implementation")
        .with_obj_provider<sysclip::clipboard, _lnav_config>(
            [](const yajlpp_provider_context& ypc, _lnav_config* root) {
                auto& retval
                    = root->lc_sysclip
                          .c_clipboard_impls[ypc.ypc_extractor.get_substr(
                              "clipboard_impl_name")];
                return &retval;
            })
        .with_path_provider<_lnav_config>(
            [](struct _lnav_config* cfg, vector<string>& paths_out) {
                for (const auto& iter : cfg->lc_sysclip.c_clipboard_impls) {
                    paths_out.emplace_back(iter.first);
                }
            })
        .with_children(sysclip_impl_handlers),
};

static struct json_path_container sysclip_handlers = {
    yajlpp::property_handler("impls")
        .with_description("Clipboard implementations")
        .with_children(sysclip_impls_handlers),
};

static struct json_path_container tuning_handlers = {
    yajlpp::property_handler("archive-manager")
        .with_description("Settings related to opening archive files")
        .with_children(archive_handlers),
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
};

static set<string> SUPPORTED_CONFIG_SCHEMAS = {
    "https://lnav.org/schemas/config-v1.schema.json",
};

const char* DEFAULT_FORMAT_SCHEMA
    = "https://lnav.org/schemas/format-v1.schema.json";

set<string> SUPPORTED_FORMAT_SCHEMAS = {
    DEFAULT_FORMAT_SCHEMA,
};

static int
read_id(yajlpp_parse_context* ypc, const unsigned char* str, size_t len)
{
    auto file_id = string((const char*) str, len);

    if (SUPPORTED_CONFIG_SCHEMAS.count(file_id) == 0) {
        ypc->report_error(
            lnav_log_level_t::ERROR,
            "%s:%d: error: unsupported configuration $schema -- %s\n",
            ypc->ypc_source.c_str(),
            ypc->get_line_number(),
            file_id.c_str());
        return 0;
    }

    return 1;
}

struct json_path_container lnav_config_handlers
    = json_path_container{json_path_handler("$schema", read_id)
                              .with_synopsis(
                                  "The URI of the schema for this file")
                              .with_description(
                                  "Specifies the type of this file"),

                          yajlpp::property_handler("tuning")
                              .with_description("Internal settings")
                              .with_children(tuning_handlers),

                          yajlpp::property_handler("ui")
                              .with_description("User-interface settings")
                              .with_children(ui_handlers),

                          yajlpp::property_handler("global")
                              .with_description("Global variable definitions")
                              .with_children(global_var_handlers)}
          .with_schema_id(*SUPPORTED_CONFIG_SCHEMAS.cbegin());

class active_key_map_listener : public lnav_config_listener {
public:
    void reload_config(error_reporter& reporter) override
    {
        lnav_config.lc_active_keymap = lnav_config.lc_ui_keymaps["default"];
        for (const auto& pair :
             lnav_config.lc_ui_keymaps[lnav_config.lc_ui_keymap].km_seq_to_cmd)
        {
            lnav_config.lc_active_keymap.km_seq_to_cmd[pair.first]
                = pair.second;
        }
    }
};

static active_key_map_listener KEYMAP_LISTENER;

Result<config_file_type, std::string>
detect_config_file_type(const ghc::filesystem::path& path)
{
    static const char* id_path[] = {"$schema", nullptr};

    auto read_res = lnav::filesystem::read_file(path);

    if (read_res.isErr()) {
        return Err(fmt::format("unable to open file: {} -- {}",
                               path.string(),
                               read_res.unwrapErr()));
    }

    auto content = read_res.unwrap();
    if (startswith(content, "#")) {
        content.insert(0, "//");
    }

    char error_buffer[1024];
    auto content_tree = unique_ptr<yajl_val_s, decltype(&yajl_tree_free)>(
        yajl_tree_parse(content.c_str(), error_buffer, sizeof(error_buffer)),
        yajl_tree_free);
    if (content_tree == nullptr) {
        return Err(fmt::format(
            "unable to parse file: {} -- {}", path.string(), error_buffer));
    }

    auto id_val = yajl_tree_get(content_tree.get(), id_path, yajl_t_string);
    if (id_val != nullptr) {
        if (SUPPORTED_CONFIG_SCHEMAS.count(id_val->u.string)) {
            return Ok(config_file_type::CONFIG);
        } else if (SUPPORTED_FORMAT_SCHEMAS.count(id_val->u.string)) {
            return Ok(config_file_type::FORMAT);
        } else {
            return Err(fmt::format(
                "unsupported configuration version in file: {} -- {}",
                path.string(),
                id_val->u.string));
        }
    } else {
        return Ok(config_file_type::FORMAT);
    }
}

static void
load_config_from(_lnav_config& lconfig,
                 const ghc::filesystem::path& path,
                 vector<string>& errors)
{
    yajlpp_parse_context ypc(path.string(), &lnav_config_handlers);
    struct userdata ud(errors);
    auto_fd fd;

    ypc.ypc_locations = &lnav_config_locations;
    ypc.with_obj(lconfig);
    ypc.ypc_userdata = &ud;
    ypc.with_error_reporter(config_error_reporter);
    if ((fd = lnav::filesystem::openp(path, O_RDONLY)) == -1) {
        if (errno != ENOENT) {
            char errmsg[1024];

            snprintf(errmsg,
                     sizeof(errmsg),
                     "error: unable to open format file -- %s",
                     path.c_str());
            errors.emplace_back(errmsg);
        }
    } else {
        auto_mem<yajl_handle_t> handle(yajl_free);
        char buffer[2048];
        off_t offset = 0;
        ssize_t rc = -1;

        handle = yajl_alloc(&ypc.ypc_callbacks, nullptr, &ypc);
        yajl_config(handle, yajl_allow_comments, 1);
        yajl_config(handle, yajl_allow_multiple_values, 1);
        ypc.ypc_handle = handle;
        while (true) {
            rc = read(fd, buffer, sizeof(buffer));
            if (rc == 0) {
                break;
            } else if (rc == -1) {
                errors.push_back(path.string() + ":unable to read file -- "
                                 + string(strerror(errno)));
                break;
            }
            if (ypc.parse((const unsigned char*) buffer, rc) != yajl_status_ok)
            {
                break;
            }
            offset += rc;
        }
        if (rc == 0) {
            ypc.complete_parse();
        }
    }
}

static void
load_default_config(struct _lnav_config& config_obj,
                    const std::string& path,
                    const bin_src_file& bsf,
                    vector<string>& errors)
{
    yajlpp_parse_context ypc_builtin(bsf.get_name(), &lnav_config_handlers);
    auto_mem<yajl_handle_t> handle(yajl_free);
    struct userdata ud(errors);

    handle = yajl_alloc(&ypc_builtin.ypc_callbacks, nullptr, &ypc_builtin);
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
    if (ypc_builtin.parse(bsf.to_string_fragment()) == yajl_status_ok) {
        ypc_builtin.complete_parse();
    }
}

static void
load_default_configs(struct _lnav_config& config_obj,
                     const std::string& path,
                     vector<string>& errors)
{
    for (auto& bsf : lnav_config_json) {
        load_default_config(config_obj, path, bsf, errors);
    }
}

void
load_config(const vector<ghc::filesystem::path>& extra_paths,
            vector<string>& errors)
{
    auto user_config = lnav::paths::dotlnav() / "config.json";

    for (auto& bsf : lnav_config_json) {
        auto sample_path = lnav::paths::dotlnav() / "configs" / "default"
            / fmt::format("{}.sample", bsf.get_name());

        auto fd = auto_fd(lnav::filesystem::openp(
            sample_path, O_WRONLY | O_TRUNC | O_CREAT, 0644));
        auto sf = bsf.to_string_fragment();
        if (fd == -1 || write(fd.get(), sf.data(), sf.length()) == -1) {
            fprintf(stderr,
                    "error:unable to write default config file: %s -- %s\n",
                    sample_path.c_str(),
                    strerror(errno));
        }
    }

    {
        load_default_configs(lnav_default_config, "*", errors);
        load_default_configs(lnav_config, "*", errors);

        for (const auto& extra_path : extra_paths) {
            auto config_path = extra_path / "configs/*/*.json";
            static_root_mem<glob_t, globfree> gl;

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
            auto config_path = extra_path / "formats/*/config.*.json";
            static_root_mem<glob_t, globfree> gl;

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

        load_config_from(lnav_config, user_config, errors);
    }

    reload_config(errors);

    rollback_lnav_config = lnav_config;
}

void
reset_config(const std::string& path)
{
    vector<string> errors;

    load_default_configs(lnav_config, path, errors);

    reload_config(errors);

    for (auto& err : errors) {
        log_debug("reset %s", err.c_str());
    }
}

string
save_config()
{
    yajlpp_gen gen;
    auto filename = fmt::format("config.json.{}.tmp", getpid());
    auto user_config_tmp = lnav::paths::dotlnav() / filename;
    auto user_config = lnav::paths::dotlnav() / "config.json";

    yajl_gen_config(gen, yajl_gen_beautify, true);
    yajlpp_gen_context ygc(gen, lnav_config_handlers);
    vector<string> errors;

    ygc.with_default_obj(lnav_default_config).with_obj(lnav_config);
    ygc.gen();

    {
        auto_fd fd;

        if ((fd = lnav::filesystem::openp(
                 user_config_tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600))
            == -1)
        {
            return "error: unable to save configuration -- "
                + string(strerror(errno));
        } else {
            string_fragment bits = gen.to_string_fragment();

            log_perror(write(fd, bits.data(), bits.length()));
        }
    }

    rename(user_config_tmp.c_str(), user_config.c_str());

    return "info: configuration saved";
}

void
reload_config(vector<string>& errors)
{
    lnav_config_listener* curr = lnav_config_listener::LISTENER_LIST;

    while (curr != nullptr) {
        auto reporter = [&errors](const void* cfg_value,
                                  const std::string& errmsg) {
            auto cb = [&cfg_value, &errors, &errmsg](
                          const json_path_handler_base& jph,
                          const string& path,
                          void* mem) {
                if (mem != cfg_value) {
                    return;
                }

                auto loc_iter
                    = lnav_config_locations.find(intern_string::lookup(path));
                if (loc_iter == lnav_config_locations.end()) {
                    return;
                }

                char msg[1024];

                snprintf(msg,
                         sizeof(msg),
                         "%s:%d:%s",
                         loc_iter->second.sl_source.get(),
                         loc_iter->second.sl_line_number,
                         errmsg.c_str());

                errors.emplace_back(msg);
            };

            for (auto& jph : lnav_config_handlers.jpc_children) {
                jph.walk(cb, &lnav_config);
            }
        };

        curr->reload_config(reporter);
        curr = curr->lcl_next;
    }
}
