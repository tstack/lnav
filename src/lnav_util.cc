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
#include <ctype.h>

#include "lnav_util.hh"
#include "base/opt_util.hh"
#include "base/result.h"
#include "ansi_scrubber.hh"
#include "fmt/format.h"
#include "view_curses.hh"

using namespace std;

bool change_to_parent_dir()
{
    bool retval = false;
    char cwd[3] = "";

    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
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

template<typename T>
size_t strtonum(T &num_out, const char *string, size_t len)
{
    size_t retval = 0;
    T sign = 1;

    num_out = 0;
    
    for (; retval < len && isspace(string[retval]); retval++);
    for (; retval < len && string[retval] == '-'; retval++) {
        sign *= -1;
    }
    for (; retval < len && string[retval] == '+'; retval++);
    for (; retval < len && isdigit(string[retval]); retval++) {
        num_out *= 10;
        num_out += string[retval] - '0';
    }
    num_out *= sign;

    return retval;
}

template
size_t strtonum<long long>(long long &num_out, const char *string, size_t len);

template
size_t strtonum<long>(long &num_out, const char *string, size_t len);

template
size_t strtonum<int>(int &num_out, const char *string, size_t len);

string build_path(const vector<ghc::filesystem::path> &paths)
{
    string retval;

    for (const auto &path : paths) {
        if (path.empty()) {
            continue;
        }
        if (!retval.empty()) {
            retval += ":";
        }
        retval += path.string();
    }
    auto env_path = getenv_opt("PATH");
    if (env_path) {
        retval += ":" + string(*env_path);
    }
    return retval;
}

Result<std::string, std::string> read_file(const ghc::filesystem::path &path)
{
    try {
        ghc::filesystem::ifstream file_stream(path);

        if (!file_stream) {
            return Err(std::string(strerror(errno)));
        }

        std::string retval;
        retval.assign((std::istreambuf_iterator<char>(file_stream)),
                      std::istreambuf_iterator<char>());
        return Ok(retval);
    } catch (const std::exception& e) {
        return Err(std::string(e.what()));
    }
}

Result<std::pair<ghc::filesystem::path, int>, std::string>
open_temp_file(const ghc::filesystem::path &pattern)
{
    auto pattern_str = pattern.string();
    char pattern_copy[pattern_str.size() + 1];
    int fd;

    strcpy(pattern_copy, pattern_str.c_str());
    if ((fd = mkstemp(pattern_copy)) == -1) {
        return Err(fmt::format("unable to create temporary file: {} -- {}",
                               pattern.string(), strerror(errno)));
    }

    return Ok(make_pair(ghc::filesystem::path(pattern_copy), fd));
}

bool is_dev_null(const struct stat &st)
{
    struct stat null_stat;

    stat("/dev/null", &null_stat);

    return st.st_dev == null_stat.st_dev &&
           st.st_ino == null_stat.st_ino;
}

bool is_dev_null(int fd)
{
    struct stat fd_stat;

    fstat(fd, &fd_stat);
    return is_dev_null(fd_stat);
}

std::string ok_prefix(std::string msg)
{
    if (msg.empty()) {
        return msg;
    }

    return std::string(ANSI_COLOR(COLOR_GREEN) "\u2714" ANSI_NORM " ") + msg;
}

std::string err_prefix(const std::string msg)
{
    if (msg.empty()) {
        return msg;
    }

    return std::string(ANSI_COLOR(COLOR_RED) "\u2718" ANSI_NORM " ") + msg;
}

Result<std::string, std::string> err_to_ok(const std::string msg)
{
    return Ok(err_prefix(msg));
}
