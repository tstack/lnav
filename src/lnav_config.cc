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

#include "pcrecpp.h"

#include "auto_fd.hh"
#include "lnav_log.hh"
#include "auto_mem.hh"
#include "auto_pid.hh"
#include "lnav_config.hh"
#include "yajlpp.hh"
#include "yajlpp_def.hh"
#include "shlex.hh"

using namespace std;

static const int MAX_CRASH_LOG_COUNT = 16;

extern "C" {
extern const char default_config_json[];
extern const char keymap_default_json[];
}

struct _lnav_config lnav_config;
static struct _lnav_config lnav_default_config;

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

static struct json_path_handler keymap_def_handlers[] = {
    json_path_handler("(?<key_seq>(x[0-9a-f]{2})+)#")
        .with_synopsis("<command>")
        .with_description("The command to execute for the given key sequence")
        .with_pattern("[:|;].*")
        .with_path_provider<key_map>([](key_map *km, vector<string> &paths_out) {
            for (const auto &iter : km->km_seq_to_cmd) {
                paths_out.push_back(iter.first);
            }
        })
        .FOR_FIELD(key_map, km_seq_to_cmd),

    json_path_handler()
};

static struct json_path_handler keymap_defs_handlers[] = {
    json_path_handler("(?<key_map_name>[^/]+)/")
        .with_synopsis("<name>")
        .with_description("The command to execute for the given key sequence")
        .with_obj_provider<key_map, _lnav_config>([](const yajlpp_provider_context &ypc, _lnav_config *root) {
            key_map &retval = root->lc_ui_keymaps[ypc.ypc_extractor.get_substr("key_map_name")];
            return &retval;
        })
        .with_path_provider<_lnav_config>([](struct _lnav_config *cfg, vector<string> &paths_out) {
            for (const auto &iter : cfg->lc_ui_keymaps) {
                paths_out.push_back(iter.first);
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
                paths_out.push_back(iter.first);
            }
        })
        .FOR_FIELD(_lnav_config, lc_global_vars),

    json_path_handler()
};

static struct json_path_handler root_config_handlers[] = {
    json_path_handler("/keymap_def/")
        .with_children(keymap_defs_handlers),

    json_path_handler("/global/")
        .with_children(global_var_handlers),

    json_path_handler()
};

static struct json_path_handler ui_handlers[] = {
        json_path_handler("clock-format")
            .with_synopsis("<format>")
            .with_description(
                "The format for the clock displayed in "
                "the top-left corner using strftime(3) conversions")
            .FOR_FIELD(_lnav_config, lc_ui_clock_format),
        json_path_handler("dim-text")
            .with_synopsis("<bool>")
            .with_description("Reduce the brightness of text (useful for xterms)")
            .FOR_FIELD(_lnav_config, lc_ui_dim_text),
        json_path_handler("default-colors")
            .with_synopsis("<bool>")
            .with_description("Use default terminal fg/bg colors")
            .FOR_FIELD(_lnav_config, lc_ui_default_colors),
        json_path_handler("keymap")
            .with_synopsis("<name>")
            .with_description("The name of the keymap to use")
            .FOR_FIELD(_lnav_config, lc_ui_keymap),

        json_path_handler()
};

struct json_path_handler lnav_config_handlers[] = {
        json_path_handler("/ui/")
                .with_children(ui_handlers),

        json_path_handler()
};

static void load_config_from(const string &path, vector<string> &errors)
{
    yajlpp_parse_context ypc(path, lnav_config_handlers);
    struct userdata ud(errors);
    auto_fd fd;

    ypc.with_obj(lnav_config);
    ypc.ypc_userdata = &ud;
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
            if (yajl_parse(handle, (const unsigned char *)buffer, rc) != yajl_status_ok) {
                errors.push_back(path +
                                 ": invalid json -- " +
                                 string((char *)yajl_get_error(handle, 1, (unsigned char *)buffer, rc)));
                break;
            }
            offset += rc;
        }
        if (rc == 0) {
            if (yajl_complete_parse(handle) != yajl_status_ok) {
                errors.push_back(path +
                                 ": invalid json -- " +
                                 string((char *)yajl_get_error(handle, 0, NULL, 0)));
            }
        }
    }
}

static void load_default_config(yajlpp_parse_context &ypc_builtin,
                                struct _lnav_config &config_obj,
                                const char *config_json,
                                vector<string> &errors)
{
    auto_mem<yajl_handle_t> handle(yajl_free);
    struct userdata ud(errors);

    handle = yajl_alloc(&ypc_builtin.ypc_callbacks, NULL, &ypc_builtin);
    ypc_builtin.with_obj(config_obj);
    ypc_builtin.ypc_userdata = &ud;
    yajl_config(handle, yajl_allow_comments, 1);
    if (yajl_parse(handle,
                   (const unsigned char *) config_json,
                   strlen(config_json)) != yajl_status_ok) {
        errors.push_back("builtin: invalid json -- " +
                         string((char *)yajl_get_error(handle, 1, (unsigned char *) config_json, strlen(config_json))));
    }
    yajl_complete_parse(handle);
}

void load_config(const vector<string> &extra_paths, vector<string> &errors)
{
    string user_config = dotlnav_path("config.json");

    {
        yajlpp_parse_context ypc_builtin("keymap", root_config_handlers);
        load_default_config(ypc_builtin, lnav_config, keymap_default_json,
                            errors);
    }

    for (auto pair : lnav_config.lc_ui_keymaps) {
        for (auto pair2 : pair.second.km_seq_to_cmd) {
            log_debug("foo %s %d", pair2.first.c_str(), pair2.second.size());
        }
    }

    {
        yajlpp_parse_context ypc_builtin("builtin", lnav_config_handlers);
        ypc_builtin.reset(lnav_config_handlers);
        load_default_config(ypc_builtin, lnav_default_config,
                            default_config_json, errors);
        ypc_builtin.reset(lnav_config_handlers);
        load_default_config(ypc_builtin, lnav_config, default_config_json,
                            errors);
        load_config_from(user_config, errors);
    }

    reload_config();
}

void reset_config(const std::string &path)
{
    yajlpp_parse_context ypc_builtin("builtin", lnav_config_handlers);
    vector<string> errors;

    if (path != "*") {
        ypc_builtin.ypc_ignore_unused = true;
        ypc_builtin.ypc_active_paths.insert(path);
    }

    load_default_config(ypc_builtin, lnav_config, default_config_json, errors);
}

string save_config()
{
    auto_mem<yajl_gen_t> handle(yajl_gen_free);

    if ((handle = yajl_gen_alloc(NULL)) == NULL) {
        return "error: Unable to create yajl_gen_object";
    }
    else {
        char filename[128];

        snprintf(filename, sizeof(filename), "config.json.%d.tmp", getpid());

        string user_config_tmp = dotlnav_path(filename);
        string user_config = dotlnav_path("config.json");

        yajl_gen_config(handle, yajl_gen_beautify, true);

        yajlpp_gen_context ygc(handle, lnav_config_handlers);
        vector<string> errors;

        ygc.with_default_obj(lnav_default_config)
           .with_obj(lnav_config);
        ygc.gen();

        const unsigned char *buffer;
        size_t len;

        yajl_gen_get_buf(handle, &buffer, &len);

        {
            auto_fd fd;

            if ((fd = open(user_config_tmp.c_str(),
                           O_WRONLY | O_CREAT | O_TRUNC, 0600)) == -1) {
                return "error: unable to save configuration -- " +
                       string(strerror(errno));
            }
            else {
                log_perror(write(fd, buffer, len));
            }
        }

        rename(user_config_tmp.c_str(), user_config.c_str());
    }

    return "info: configuration saved";
}

void reload_config()
{
    lnav_config_listener *curr = lnav_config_listener::LISTENER_LIST;

    while (curr != NULL) {
        curr->reload_config();
        curr = curr->lcl_next;
    }
}
