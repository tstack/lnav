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
 * @file lnav_util.cc
 *
 * Dumping ground for useful functions with no other home.
 */

#include "lnav_util.hh"

#include <stdio.h>
#include <sys/stat.h>

#include "ansi_scrubber.hh"
#include "base/result.h"
#include "config.h"
#include "fmt/format.h"
#include "view_curses.hh"

bool
change_to_parent_dir()
{
    bool retval = false;
    char cwd[3] = "";

    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        /* perror("getcwd"); */
    }
    if (strcmp(cwd, "/") != 0) {
        if (chdir("..") == -1) {
            perror("chdir('..')");
        } else {
            retval = true;
        }
    }

    return retval;
}

bool
is_dev_null(const struct stat& st)
{
    struct stat null_stat;

    stat("/dev/null", &null_stat);

    return st.st_dev == null_stat.st_dev && st.st_ino == null_stat.st_ino;
}

bool
is_dev_null(int fd)
{
    struct stat fd_stat;

    fstat(fd, &fd_stat);
    return is_dev_null(fd_stat);
}

std::string
ok_prefix(std::string msg)
{
    if (msg.empty()) {
        return msg;
    }

    return std::string(ANSI_COLOR(COLOR_GREEN) "\u2714" ANSI_NORM " ") + msg;
}

std::string
err_prefix(const std::string msg)
{
    if (msg.empty()) {
        return msg;
    }

    return std::string(ANSI_COLOR(COLOR_RED) "\u2718" ANSI_NORM " ") + msg;
}

Result<std::string, std::string>
err_to_ok(const std::string msg)
{
    return Ok(err_prefix(msg));
}
