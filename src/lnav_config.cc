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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file lnav_config.cc
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glob.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <libgen.h>

#include <iostream>
#include <stdexcept>
#include <fmt/format.h>

#include "pcrecpp.h"

#include "auto_fd.hh"
#include "base/lnav_log.hh"
#include "lnav_util.hh"
#include "auto_mem.hh"
#include "auto_pid.hh"
#include "lnav_config.hh"
#include "yajlpp/yajlpp.hh"
#include "yajlpp/yajlpp_def.hh"
#include "shlex.hh"
#include "styling.hh"
#include "bin2c.h"
#include "default-config.h"

using namespace std;

static const int MAX_CRASH_LOG_COUNT = 16;

struct _lnav_config lnav_config;
struct _lnav_config rollback_lnav_config;
static struct _lnav_config lnav_default_config;

std::map<intern_string_t, source_location> lnav_config_locations;

lnav_config_listener *lnav_config_listener::LISTENER_LIST;

string dotlnav_path(const char *sub)
{
    string retval;
    char * home;

    home = getenv("HOME");
    if (home && access(home, W_OK|X_OK) == 0) {
        char hpath[2048];

        snprintf(hpath, sizeof(hpath), "%s/.lnav/%s", home, sub);
        retval = hpath;
    }
    else {
        retval = sub;
    }

    return retval;
}

bool check_experimental(const char *feature_name)
{
    const char *env_value = getenv("LNAV_EXP");

    require(feature_name != NULL);

    if (env_value && strcasestr(env_value, feature_name)) {
        return true;
    }

    return false;
}

void ensure_dotlnav(void)
{
    string path = dotlnav_path("");

    if (!path.empty()) {
        log_perror(mkdir(path.c_str(), 0755));
    }

    path = dotlnav_path("formats");
    if (!path.empty()) {
        log_perror(mkdir(path.c_str(), 0755));
    }

    path = dotlnav_path("formats/installed");
    if (!path.empty()) {
        log_perror(mkdir(path.c_str(), 0755));
    }

    path = dotlnav_path("crash");
    if (!path.empty()) {
        log_perror(mkdir(path.c_str(), 0755));
    }
    lnav_log_crash_dir = strdup(path.c_str());

    {
        static_root_mem<glob_t, globfree> gl;

        path += "/*";
        if (glob(path.c_str(), GLOB_NOCHECK, NULL, gl.inout()) == 0) {
            for (int lpc = 0;
                 lpc < ((int)gl->gl_pathc - MAX_CRASH_LOG_COUNT);
                 lpc++) {
                log_perror(remove(gl->gl_pathv[lpc]));
            }
        }
    }
    
    path = dotlnav_path("formats/default");
    if (!path.empty()) {
        log_perror(mkdir(path.c_str(), 0755));
    }
}

void install_git_format(const char *repo)
{
    static pcrecpp::RE repo_name_converter("[^\\w]");

    auto_pid git_cmd(fork());

    if (git_cmd.in_child()) {
        string formats_path = dotlnav_path("formats/");
        string local_name = repo;
        string local_path;

        repo_name_converter.GlobalReplace("_", &local_name);
        local_path = formats_path + local_name;
        if (access(local_path.c_str(), R_OK) == 0) {
            printf("Updating format repo: %s\n", repo);
            log_perror(chdir(local_path.c_str()));
            execlp("git", "git", "pull", NULL);
        }
        else {
            execlp("git", "git", "clone", repo, local_path.c_str(), NULL);
        }
        _exit(1);
    }

    git_cmd.wait_for_child();
}

bool update_git_formats()
{
    static_root_mem<glob_t, globfree> gl;
    string formats_path = dotlnav_path("formats/");
    string git_formats = formats_path + "*/.git";
    bool found = false, retval = true;

    if (glob(git_formats.c_str(), GLOB_NOCHECK, NULL, gl.inout()) == 0) {
        for (int lpc = 0; lpc < (int) gl->gl_pathc; lpc++) {
            char *git_dir = dirname(gl->gl_pathv[lpc]);
            char pull_cmd[1024];

            printf("Updating formats in %s\n", git_dir);
            snprintf(pull_cmd, sizeof(pull_cmd),
                     "cd %s && git pull",
                     git_dir);
            int ret = system(pull_cmd);
            if (ret == -1) {
                std::cerr << "Failed to spawn command "
                          << "\"" << pull_cmd << "\": "
                          << strerror(errno) << std::endl;
                retval = false;
            }
            else if (ret > 0) {
                std::cerr << "Command "
                          << "\"" << pull_cmd << "\" failed: "
                          << strerror(errno) << std::endl;
                retval = false;
            }
            found = true;
        }
    }

    if (!found) {
        printf("No formats from git repositories found, "
               "use 'lnav -i extra' to install third-party foramts\n");
    }

    return retval;
}

static int read_repo_path(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    string path = string((const char *)str, len);

    install_git_format(path.c_str());

    return 1;
}

static struct json_path_handler format_handlers[] = {
    json_path_handler("/format-repos#", read_repo_path),

    json_path_handler()
};

void install_extra_formats()
{
    string config_root = dotlnav_path("remote-config");
    auto_fd fd;

    if (access(config_root.c_str(), R_OK) == 0) {
        char pull_cmd[1024];

        printf("Updating lnav remote config repo...\n");
        snprintf(pull_cmd, sizeof(pull_cmd),
                 "cd '%s' && git pull",
                 config_root.c_str());
        log_perror(system(pull_cmd));
    }
    else {
        char clone_cmd[1024];

        printf("Cloning lnav remote config repo...\n");
        snprintf(clone_cmd, sizeof(clone_cmd),
                 "git clone https://github.com/tstack/lnav-config.git %s",
                 config_root.c_str());
        log_perror(system(clone_cmd));
    }

    string config_json = config_root + "/remote-config.json";
    if ((fd = open(config_json.c_str(), O_RDONLY)) == -1) {
        perror("Unable to open remote-config.json");
    }
    else {
        yajlpp_parse_context ypc_config(config_root, format_handlers);
        auto_mem<yajl_handle_t> jhandle(yajl_free);
        unsigned char buffer[4096];
        ssize_t rc;

        jhandle = yajl_alloc(&ypc_config.ypc_callbacks, NULL, &ypc_config);
        yajl_config(jhandle, yajl_allow_comments, 1);
        while ((rc = read(fd, buffer, sizeof(buffer))) > 0) {
            if (yajl_parse(jhandle,
                           buffer,
                           rc) != yajl_status_ok) {
                fprintf(stderr, "Unable to parse remote-config.json -- %s",
                        yajl_get_error(jhandle, 1, buffer, rc));
                return;
            }
        }
        if (yajl_complete_parse(jhandle) != yajl_status_ok) {
            fprintf(stderr, "Unable to parse remote-config.json -- %s",
                    yajl_get_error(jhandle, 1, buffer, rc));
        }
    }
}

struct userdata {
    userdata(vector<string> &errors) : ud_errors(errors) {};

    vector<string> &ud_errors;
};

static void config_error_reporter(const yajlpp_parse_context &ypc,
                                  lnav_log_level_t level,
                                  const char *msg)
{
    if (level >= lnav_log_level_t::ERROR) {
        struct userdata *ud = (userdata *) ypc.ypc_userdata;

        ud->ud_errors.emplace_back(msg);
    } else {
        fprintf(stderr, "warning:%s\n",  msg);
    }
}

static struct json_path_handler key_command_handlers[] = {
    json_path_handler("command")
        .with_synopsis("<command>")
        .with_description("The command to execute for the given key sequence")
        .with_pattern("[:|;].*")
        .FOR_FIELD(key_command, kc_cmd),
    json_path_handler("alt-msg")
        .with_synopsis("<msg>")
        .with_description("The help message to display after the key is pressed")
        .FOR_FIELD(key_command, kc_alt_msg),

    json_path_handler()
};

static struct json_path_handler keymap_def_handlers[] = {
    json_path_handler("(?<key_seq>(x[0-9a-f]{2})+)/")
        .with_obj_provider<key_command, key_map>([](const yajlpp_provider_context &ypc, key_map *km) {
            key_command &retval = km->km_seq_to_cmd[ypc.ypc_extractor.get_substr("key_seq")];

            return &retval;
        })
        .with_path_provider<key_map>([](key_map *km, vector<string> &paths_out) {
            for (const auto &iter : km->km_seq_to_cmd) {
                paths_out.emplace_back(iter.first);
            }
        })
        .with_children(key_command_handlers),

    json_path_handler()
};

static struct json_path_handler keymap_defs_handlers[] = {
    json_path_handler("(?<keymap_name>[^/]+)/")
        .with_obj_provider<key_map, _lnav_config>([](const yajlpp_provider_context &ypc, _lnav_config *root) {
            key_map &retval = root->lc_ui_keymaps[ypc.ypc_extractor.get_substr("keymap_name")];
            return &retval;
        })
        .with_path_provider<_lnav_config>([](struct _lnav_config *cfg, vector<string> &paths_out) {
            for (const auto &iter : cfg->lc_ui_keymaps) {
                paths_out.emplace_back(iter.first);
            }
        })
        .with_children(keymap_def_handlers),

    json_path_handler()
};

static struct json_path_handler global_var_handlers[] = {
    json_path_handler("(?<var_name>\\w+)")
        .with_synopsis("<name>")
        .with_description("A global variable definition")
        .with_path_provider<_lnav_config>([](struct _lnav_config *cfg, vector<string> &paths_out) {
            for (const auto &iter : cfg->lc_global_vars) {
                paths_out.emplace_back(iter.first);
            }
        })
        .FOR_FIELD(_lnav_config, lc_global_vars),

    json_path_handler()
};

static struct json_path_handler style_config_handlers[] = {
    json_path_handler("color")
        .with_synopsis("#hex|color_name")
        .with_description("Foreground color")
        .FOR_FIELD(style_config, sc_color),
    json_path_handler("background-color")
        .with_synopsis("#hex|color_name")
        .with_description("Background color")
        .FOR_FIELD(style_config, sc_background_color),
    json_path_handler("selected-color")
        .with_synopsis("#hex|color_name")
        .with_description("Background color when selected")
        .FOR_FIELD(style_config, sc_selected_color),
    json_path_handler("underline")
        .with_description("Underline")
        .FOR_FIELD(style_config, sc_underline),
    json_path_handler("bold")
        .with_description("Bold")
        .FOR_FIELD(style_config, sc_bold),

    json_path_handler()
};

static struct json_path_handler theme_styles_handlers[] = {
    json_path_handler("identifier/")
        .with_description("Styling for identifiers in logs")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_identifier;
        })
        .with_children(style_config_handlers),
    json_path_handler("text/")
        .with_description("Styling for plain text")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_text;
        })
        .with_children(style_config_handlers),
    json_path_handler("alt-text/")
        .with_description("Styling for plain text when alternating")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_alt_text;
        })
        .with_children(style_config_handlers),
    json_path_handler("error/")
        .with_description("Styling for error messages")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_error;
        })
        .with_children(style_config_handlers),
    json_path_handler("ok/")
        .with_description("Styling for success messages")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_ok;
        })
        .with_children(style_config_handlers),
    json_path_handler("warning/")
        .with_description("Styling for warning messages")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_warning;
        })
        .with_children(style_config_handlers),
    json_path_handler("hidden/")
        .with_description("Styling for hidden fields in logs")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_hidden;
        })
        .with_children(style_config_handlers),
    json_path_handler("adjusted-time/")
        .with_description("Styling for timestamps that have been adjusted")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_adjusted_time;
        })
        .with_children(style_config_handlers),
    json_path_handler("skewed-time/")
        .with_description("Styling for timestamps ")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_skewed_time;
        })
        .with_children(style_config_handlers),
    json_path_handler("offset-time/")
        .with_description("Styling for hidden fields")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_offset_time;
        })
        .with_children(style_config_handlers),
    json_path_handler("popup/")
        .with_description("Styling for popup windows")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_popup;
        })
        .with_children(style_config_handlers),
    json_path_handler("scrollbar/")
        .with_description("Styling for scrollbars")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_scrollbar;
        })
        .with_children(style_config_handlers),

    json_path_handler()
};

static struct json_path_handler theme_syntax_styles_handlers[] = {
    json_path_handler("keyword/")
        .with_description("Styling for keywords in source files")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_keyword;
        })
        .with_children(style_config_handlers),
    json_path_handler("string/")
        .with_description("Styling for single/double-quoted strings in text")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_string;
        })
        .with_children(style_config_handlers),
    json_path_handler("comment/")
        .with_description("Styling for comments in source files")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_comment;
        })
        .with_children(style_config_handlers),
    json_path_handler("variable/")
        .with_description("Styling for variables in text")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_variable;
        })
        .with_children(style_config_handlers),
    json_path_handler("symbol/")
        .with_description("Styling for symbols in source files")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_symbol;
        })
        .with_children(style_config_handlers),
    json_path_handler("number/")
        .with_description("Styling for numbers in source files")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_number;
        })
        .with_children(style_config_handlers),
    json_path_handler("re-special/")
        .with_description("Styling for special characters in regular expressions")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_re_special;
        })
        .with_children(style_config_handlers),
    json_path_handler("re-repeat/")
        .with_description("Styling for repeats in regular expressions")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_re_repeat;
        })
        .with_children(style_config_handlers),

    json_path_handler("diff-delete/")
        .with_description("Styling for deleted lines in diffs")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_diff_delete;
        })
        .with_children(style_config_handlers),
    json_path_handler("diff-add/")
        .with_description("Styling for added lines in diffs")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_diff_add;
        })
        .with_children(style_config_handlers),
    json_path_handler("diff-section/")
        .with_description("Styling for diffs")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_diff_section;
        })
        .with_children(style_config_handlers),
    json_path_handler("file/")
        .with_description("Styling for file names in source files")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_file;
        })
        .with_children(style_config_handlers),

    json_path_handler()
};

static struct json_path_handler theme_status_styles_handlers[] = {
    json_path_handler("text/")
        .with_description("Styling for status bars")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_status;
        })
        .with_children(style_config_handlers),
    json_path_handler("warn/")
        .with_description("Styling for warnings in status bars")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_warn_status;
        })
        .with_children(style_config_handlers),
    json_path_handler("alert/")
        .with_description("Styling for alerts in status bars")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_alert_status;
        })
        .with_children(style_config_handlers),
    json_path_handler("active/")
        .with_description("Styling for activity in status bars")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_active_status;
        })
        .with_children(style_config_handlers),
    json_path_handler("inactive/")
        .with_description("Styling for inactive status bars")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_inactive_status;
        })
        .with_children(style_config_handlers),
    json_path_handler("title/")
        .with_description("Styling for title sections of status bars")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_status_title;
        })
        .with_children(style_config_handlers),
    json_path_handler("subtitle/")
        .with_description("Styling for subtitle sections of status bars")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            return &root->lt_style_status_subtitle;
        })
        .with_children(style_config_handlers),

    json_path_handler()
};

static struct json_path_handler theme_log_level_styles_handlers[] = {
    json_path_handler("(?<level>trace|debug5|debug4|debug3|debug2|debug|info|stats|notice|warning|error|critical|fatal)"
                      "/")
        .with_obj_provider<style_config, lnav_theme>([](const yajlpp_provider_context &ypc, lnav_theme *root) {
            style_config &sc = root->lt_level_styles[
                string2level(ypc.ypc_extractor.get_substr_i("level").get())];

            return &sc;
        })
        .with_path_provider<lnav_theme>([](struct lnav_theme *cfg, vector<string> &paths_out) {
            for (int lpc = LEVEL_TRACE; lpc < LEVEL__MAX; lpc++) {
                paths_out.emplace_back(level_names[lpc]);
            }
        })
        .with_children(style_config_handlers),

    json_path_handler()
};

static struct json_path_handler theme_vars_handlers[] = {
    json_path_handler("(?<var_name>\\w+)")
        .with_synopsis("name")
        .with_description("A theme variable definition")
        .with_path_provider<lnav_theme>([](struct lnav_theme *lt, vector<string> &paths_out) {
            for (const auto &iter : lt->lt_vars) {
                paths_out.emplace_back(iter.first);
            }
        })
        .FOR_FIELD(lnav_theme, lt_vars),

    json_path_handler()
};

static struct json_path_handler theme_def_handlers[] = {
    json_path_handler("vars/")
        .with_description("Variables definitions that are used in this theme")
        .with_children(theme_vars_handlers),

    json_path_handler("styles/")
        .with_children(theme_styles_handlers),

    json_path_handler("syntax-styles/")
        .with_children(theme_syntax_styles_handlers),

    json_path_handler("status-styles/")
        .with_children(theme_status_styles_handlers),

    json_path_handler("log-level-styles/")
        .with_children(theme_log_level_styles_handlers),

    json_path_handler()
};

static struct json_path_handler theme_defs_handlers[] = {
    json_path_handler("(?<theme_name>[^/]+)/")
        .with_obj_provider<lnav_theme, _lnav_config>([](const yajlpp_provider_context &ypc, _lnav_config *root) {
            lnav_theme &lt = root->lc_ui_theme_defs[ypc.ypc_extractor.get_substr("theme_name")];

            return &lt;
        })
        .with_path_provider<_lnav_config>([](struct _lnav_config *cfg, vector<string> &paths_out) {
            for (const auto &iter : cfg->lc_ui_theme_defs) {
                paths_out.emplace_back(iter.first);
            }
        })
        .with_children(theme_def_handlers),

    json_path_handler()
};

static struct json_path_handler ui_handlers[] = {
        json_path_handler("clock-format")
            .with_synopsis("format")
            .with_description(
                "The format for the clock displayed in "
                "the top-left corner using strftime(3) conversions")
            .FOR_FIELD(_lnav_config, lc_ui_clock_format),
        json_path_handler("dim-text")
            .with_synopsis("bool")
            .with_description("Reduce the brightness of text (useful for xterms)")
            .FOR_FIELD(_lnav_config, lc_ui_dim_text),
        json_path_handler("default-colors")
            .with_synopsis("bool")
            .with_description("Use default terminal fg/bg colors")
            .FOR_FIELD(_lnav_config, lc_ui_default_colors),
        json_path_handler("keymap")
            .with_synopsis("keymap_name")
            .with_description("The name of the keymap to use")
            .FOR_FIELD(_lnav_config, lc_ui_keymap),
        json_path_handler("theme")
            .with_synopsis("theme_name")
            .with_description("The name of the theme to use")
            .FOR_FIELD(_lnav_config, lc_ui_theme),
        json_path_handler("theme-defs/")
            .with_description("Theme definitions")
            .with_children(theme_defs_handlers),
        json_path_handler("keymap_def/")
            .with_description("Keymap definitions")
            .with_children(keymap_defs_handlers),

        json_path_handler()
};

struct json_path_handler lnav_config_handlers[] = {
        json_path_handler("/ui/")
            .with_description("User-interface settings")
            .with_children(ui_handlers),

        json_path_handler("/global/")
            .with_description("Global variable definitions")
            .with_children(global_var_handlers),

        json_path_handler()
};

static void load_config_from(const string &path, vector<string> &errors)
{
    yajlpp_parse_context ypc(path, lnav_config_handlers);
    struct userdata ud(errors);
    auto_fd fd;

    ypc.ypc_locations = &lnav_config_locations;
    ypc.with_obj(lnav_config);
    ypc.ypc_userdata = &ud;
    ypc.with_error_reporter(config_error_reporter);
    if ((fd = open(path.c_str(), O_RDONLY)) == -1) {
        if (errno != ENOENT) {
            char errmsg[1024];

            snprintf(errmsg, sizeof(errmsg),
                     "error: unable to open format file -- %s",
                     path.c_str());
            errors.emplace_back(errmsg);
        }
    }
    else {
        auto_mem<yajl_handle_t> handle(yajl_free);
        char buffer[2048];
        off_t offset = 0;
        ssize_t rc = -1;

        handle = yajl_alloc(&ypc.ypc_callbacks, NULL, &ypc);
        yajl_config(handle, yajl_allow_comments, 1);
        yajl_config(handle, yajl_allow_multiple_values, 1);
        ypc.ypc_handle = handle;
        while (true) {
            rc = read(fd, buffer, sizeof(buffer));
            if (rc == 0) {
                break;
            }
            else if (rc == -1) {
                errors.push_back(path +
                                 ":unable to read file -- " +
                                 string(strerror(errno)));
                break;
            }
            if (ypc.parse((const unsigned char *)buffer, rc) != yajl_status_ok) {
                break;
            }
            offset += rc;
        }
        if (rc == 0) {
            ypc.complete_parse();
        }
    }
}

static void load_default_config(struct _lnav_config &config_obj,
                                const std::string &path,
                                struct bin_src_file &bsf,
                                vector<string> &errors)
{
    yajlpp_parse_context ypc_builtin(bsf.bsf_name, lnav_config_handlers);
    auto_mem<yajl_handle_t> handle(yajl_free);
    struct userdata ud(errors);

    handle = yajl_alloc(&ypc_builtin.ypc_callbacks, NULL, &ypc_builtin);
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
    if (ypc_builtin.parse(bsf.bsf_data, bsf.bsf_size) == yajl_status_ok) {
        ypc_builtin.complete_parse();
    }
}

static void load_default_configs(struct _lnav_config &config_obj,
                                 const std::string &path,
                                 struct bin_src_file bsf[],
                                 vector<string> &errors)
{
    for (int lpc = 0; bsf[lpc].bsf_name; lpc++) {
        load_default_config(config_obj, path, bsf[lpc], errors);
    }
}

void load_config(const vector<string> &extra_paths, vector<string> &errors)
{
    string user_config = dotlnav_path("config.json");

    {
        load_default_configs(lnav_default_config, "*", lnav_config_json, errors);
        load_default_configs(lnav_config, "*", lnav_config_json, errors);

        for (const auto &extra_path : extra_paths) {
            string format_path = extra_path + "/formats/*/*.json";
            static_root_mem<glob_t, globfree> gl;

            if (glob(format_path.c_str(), 0, NULL, gl.inout()) == 0) {
                for (int lpc = 0; lpc < (int)gl->gl_pathc; lpc++) {
                    const char *base = basename(gl->gl_pathv[lpc]);

                    if (!startswith(base, "config.")) {
                        continue;
                    }

                    string filename(gl->gl_pathv[lpc]);

                    load_config_from(filename, errors);
                }
            }
        }

        load_config_from(user_config, errors);
    }

    reload_config(errors);

    rollback_lnav_config = lnav_config;
}

void reset_config(const std::string &path)
{
    vector<string> errors;

    load_default_configs(lnav_config, path, lnav_config_json, errors);

    reload_config(errors);
}

string save_config()
{
    yajlpp_gen gen;
    string filename = fmt::format("config.json.{}.tmp", getpid());
    string user_config_tmp = dotlnav_path(filename.c_str());
    string user_config = dotlnav_path("config.json");

    yajl_gen_config(gen, yajl_gen_beautify, true);
    yajlpp_gen_context ygc(gen, lnav_config_handlers);
    vector<string> errors;

    ygc.with_default_obj(lnav_default_config)
       .with_obj(lnav_config);
    ygc.gen();

    {
        auto_fd fd;

        if ((fd = open(user_config_tmp.c_str(),
                       O_WRONLY | O_CREAT | O_TRUNC, 0600)) == -1) {
            return "error: unable to save configuration -- " +
                   string(strerror(errno));
        } else {
            string_fragment bits = gen.to_string_fragment();

            log_perror(write(fd, bits.data(), bits.length()));
        }
    }

    rename(user_config_tmp.c_str(), user_config.c_str());

    return "info: configuration saved";
}

void reload_config(vector<string> &errors)
{
    lnav_config_listener *curr = lnav_config_listener::LISTENER_LIST;

    while (curr != NULL) {
        auto reporter = [&errors](const void *cfg_value, const std::string &errmsg) {
            auto cb = [&cfg_value, &errors, &errmsg](
                const json_path_handler_base &jph,
                const string &path,
                void *mem) {
                if (mem != cfg_value) {
                    return;
                }

                auto loc_iter = lnav_config_locations.find(intern_string::lookup(path));
                if (loc_iter == lnav_config_locations.end()) {
                    return;
                }

                char msg[1024];

                snprintf(msg, sizeof(msg),
                         "%s:%d:%s",
                         loc_iter->second.sl_source.get(),
                         loc_iter->second.sl_line_number,
                         errmsg.c_str());

                errors.emplace_back(msg);
            };

            for (int lpc = 0; lnav_config_handlers[lpc].jph_path[0]; lpc++) {
                lnav_config_handlers[lpc].walk(cb, &lnav_config);
            }
        };

        curr->reload_config(reporter);
        curr = curr->lcl_next;
    }
}
