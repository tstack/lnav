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
 * @file lnav_util.cc
 *
 * Dumping ground for useful functions with no other home.
 */

#include "config.h"

#include <stdio.h>
#include <fcntl.h>

#include <sqlite3.h>

#include "auto_fd.hh"
#include "lnav_util.hh"

std::string hash_string(const std::string &str)
{
    byte_array<SHA_DIGEST_LENGTH> hash;
    SHA_CTX context;

    SHA_Init(&context);
    SHA_Update(&context, str.c_str(), str.length());
    SHA_Final(hash.out(), &context);

    return hash.to_string();
}

std::string time_ago(time_t last_time)
{
    time_t      delta, current_time = time(NULL);
    const char *fmt;
    char        buffer[64];
    int         amount;

    delta = current_time - last_time;
    if (delta < 0) {
        return "in the future";
    }
    else if (delta < 60) {
        return "just now";
    }
    else if (delta < (60 * 2)) {
        return "one minute ago";
    }
    else if (delta < (60 * 60)) {
        fmt    = "%d minutes ago";
        amount = delta / 60;
    }
    else if (delta < (2 * 60 * 60)) {
        return "one hour ago";
    }
    else if (delta < (24 * 60 * 60)) {
        fmt    = "%d hours ago";
        amount = delta / (60 * 60);
    }
    else if (delta < (2 * 24 * 60 * 60)) {
        return "one day ago";
    }
    else if (delta < (365 * 24 * 60 * 60)) {
        fmt    = "%d days ago";
        amount = delta / (24 * 60 * 60);
    }
    else {
        return "over a year ago";
    }

    snprintf(buffer, sizeof(buffer), fmt, amount);

    return std::string(buffer);
}

/* XXX figure out how to do this with the template */
void sqlite_close_wrapper(void *mem)
{
    sqlite3_close((sqlite3 *)mem);
}

std::string get_current_dir(void)
{
    char        cwd[FILENAME_MAX];
    std::string retval = ".";

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd");
    }
    else {
        retval = std::string(cwd);
    }

    if (retval != "/") {
        retval += "/";
    }

    return retval;
}

bool change_to_parent_dir(void)
{
    bool retval = false;
    char cwd[3] = "";

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        /* perror("getcwd"); */
    }
    if (strcmp(cwd, "/") != 0) {
        if (chdir("..") == -1) {
            perror("chdir('..')");
        }
        else {
            retval = true;
        }
    }

    return retval;
}

file_format_t detect_file_format(const std::string &filename)
{
    file_format_t retval = FF_UNKNOWN;
    auto_fd       fd;

    if ((fd = open(filename.c_str(), O_RDONLY)) != -1) {
        char buffer[32];
        int  rc;

        if ((rc = read(fd, buffer, sizeof(buffer))) > 0) {
            if (rc > 16 &&
                strncmp(buffer, "SQLite format 3", 16) == 0) {
                retval = FF_SQLITE_DB;
            }
        }
    }

    return retval;
}
