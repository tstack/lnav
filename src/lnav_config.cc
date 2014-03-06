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

#include "lnav_log.hh"
#include "auto_mem.hh"
#include "lnav_config.hh"

using namespace std;

static const int MAX_CRASH_LOG_COUNT = 16;

string dotlnav_path(const char *sub)
{
    string retval;
    char * home;

    home = getenv("HOME");
    if (home) {
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
        mkdir(path.c_str(), 0755);
    }

    path = dotlnav_path("formats");
    if (!path.empty()) {
        mkdir(path.c_str(), 0755);
    }
    
    path = dotlnav_path("crash");
    if (!path.empty()) {
        mkdir(path.c_str(), 0755);
    }
    lnav_log_crash_dir = strdup(path.c_str());

    {
        static_root_mem<glob_t, globfree> gl;

        path += "/*";
        if (glob(path.c_str(), GLOB_NOCHECK, NULL, gl.inout()) == 0) {
            for (int lpc = 0;
                 lpc < ((int)gl->gl_pathc - MAX_CRASH_LOG_COUNT);
                 lpc++) {
                remove(gl->gl_pathv[lpc]);
            }
        }
    }
    
    path = dotlnav_path("formats/default");
    if (!path.empty()) {
        mkdir(path.c_str(), 0755);
    }
}
