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

using namespace std;

static const int MAX_CRASH_LOG_COUNT = 16;

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
            chdir(local_path.c_str());
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
    json_path_handler("^/format-repos#$", read_repo_path),

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
        system(pull_cmd);
    }
    else {
        char clone_cmd[1024];

        printf("Cloning lnav remote config repo...\n");
        snprintf(clone_cmd, sizeof(clone_cmd),
                 "git clone https://github.com/tstack/lnav-config.git %s",
                 config_root.c_str());
        system(clone_cmd);
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
        yajl_complete_parse(jhandle);
    }
}
