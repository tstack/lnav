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
 * @file fs-extension-functions.cc
 */

#include <string>

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config.h"
#include "sqlite-extension-func.hh"
#include "sqlite3.h"
#include "vtab_module.hh"

using namespace mapbox;

static util::variant<const char*, string_fragment>
sql_basename(const char* path_in)
{
    int text_end = -1;

    if (path_in[0] == '\0') {
        return ".";
    }

    for (ssize_t lpc = strlen(path_in) - 1; lpc >= 0; lpc--) {
        if (path_in[lpc] == '/' || path_in[lpc] == '\\') {
            if (text_end != -1) {
                return string_fragment(path_in, lpc + 1, text_end);
            }
        } else if (text_end == -1) {
            text_end = (int) (lpc + 1);
        }
    }

    if (text_end == -1) {
        return "/";
    } else {
        return string_fragment(path_in, 0, text_end);
    }
}

static util::variant<const char*, string_fragment>
sql_dirname(const char* path_in)
{
    ssize_t text_end;

    text_end = strlen(path_in) - 1;
    while (text_end >= 0
           && (path_in[text_end] == '/' || path_in[text_end] == '\\'))
    {
        text_end -= 1;
    }

    while (text_end >= 0) {
        if (path_in[text_end] == '/' || path_in[text_end] == '\\') {
            return string_fragment(path_in, 0, text_end == 0 ? 1 : text_end);
        }

        text_end -= 1;
    }

    return path_in[0] == '/' ? "/" : ".";
}

static nonstd::optional<std::string>
sql_joinpath(const std::vector<const char*>& paths)
{
    std::string full_path;

    if (paths.empty()) {
        return nonstd::nullopt;
    }

    for (auto& path_in : paths) {
        if (path_in == nullptr) {
            return nonstd::nullopt;
        }

        if (path_in[0] == '/' || path_in[0] == '\\') {
            full_path.clear();
        }
        if (!full_path.empty() && full_path[full_path.length() - 1] != '/'
            && full_path[full_path.length() - 1] != '\\')
        {
            full_path += "/";
        }
        full_path += path_in;
    }

    return full_path;
}

static std::string
sql_readlink(const char* path)
{
    struct stat st;

    if (lstat(path, &st) == -1) {
        throw sqlite_func_error(
            "unable to stat path: {} -- {}", path, strerror(errno));
    }

    char buf[st.st_size];
    ssize_t rc;

    rc = readlink(path, buf, sizeof(buf));
    if (rc < 0) {
        if (errno == EINVAL) {
            return path;
        }
        throw sqlite_func_error(
            "unable to read link: {} -- {}", path, strerror(errno));
    }

    return std::string(buf, rc);
}

static std::string
sql_realpath(const char* path)
{
    char resolved_path[PATH_MAX];

    if (realpath(path, resolved_path) == nullptr) {
        throw sqlite_func_error(
            "Could not get real path for {} -- {}", path, strerror(errno));
    }

    return resolved_path;
}

int
fs_extension_functions(struct FuncDef** basic_funcs,
                       struct FuncDefAgg** agg_funcs)
{
    static struct FuncDef fs_funcs[] = {

        sqlite_func_adapter<decltype(&sql_basename), sql_basename>::builder(
            help_text("basename", "Extract the base portion of a pathname.")
                .sql_function()
                .with_parameter({"path", "The path"})
                .with_tags({"filename"})
                .with_example({"To get the base of a plain file name",
                               "SELECT basename('foobar')"})
                .with_example(
                    {"To get the base of a path", "SELECT basename('foo/bar')"})
                .with_example({"To get the base of a directory",
                               "SELECT basename('foo/bar/')"})
                .with_example({"To get the base of an empty string",
                               "SELECT basename('')"})
                .with_example({"To get the base of a Windows path",
                               "SELECT basename('foo\\bar')"})
                .with_example({"To get the base of the root directory",
                               "SELECT basename('/')"})),

        sqlite_func_adapter<decltype(&sql_dirname), sql_dirname>::builder(
            help_text("dirname", "Extract the directory portion of a pathname.")
                .sql_function()
                .with_parameter({"path", "The path"})
                .with_tags({"filename"})
                .with_example({"To get the directory of a relative file path",
                               "SELECT dirname('foo/bar')"})
                .with_example({"To get the directory of an absolute file path",
                               "SELECT dirname('/foo/bar')"})
                .with_example(
                    {"To get the directory of a file in the root directory",
                     "SELECT dirname('/bar')"})
                .with_example({"To get the directory of a Windows path",
                               "SELECT dirname('foo\\bar')"})
                .with_example({"To get the directory of an empty path",
                               "SELECT dirname('')"})),

        sqlite_func_adapter<decltype(&sql_joinpath), sql_joinpath>::builder(
            help_text("joinpath", "Join components of a path together.")
                .sql_function()
                .with_parameter(
                    help_text(
                        "path",
                        "One or more path components to join together.  "
                        "If an argument starts with a forward or backward "
                        "slash, it will be considered "
                        "an absolute path and any preceding elements will "
                        "be ignored.")
                        .one_or_more())
                .with_tags({"filename"})
                .with_example(
                    {"To join a directory and file name into a relative path",
                     "SELECT joinpath('foo', 'bar')"})
                .with_example(
                    {"To join an empty component with other names into "
                     "a relative path",
                     "SELECT joinpath('', 'foo', 'bar')"})
                .with_example(
                    {"To create an absolute path with two path components",
                     "SELECT joinpath('/', 'foo', 'bar')"})
                .with_example(
                    {"To create an absolute path from a path component "
                     "that starts with a forward slash",
                     "SELECT joinpath('/', 'foo', '/bar')"})),

        sqlite_func_adapter<decltype(&sql_readlink), sql_readlink>::builder(
            help_text("readlink", "Read the target of a symbolic link.")
                .sql_function()
                .with_parameter({"path", "The path to the symbolic link."})
                .with_tags({"filename"})),

        sqlite_func_adapter<decltype(&sql_realpath), sql_realpath>::builder(
            help_text(
                "realpath",
                "Returns the resolved version of the given path, expanding "
                "symbolic links and "
                "resolving '.' and '..' references.")
                .sql_function()
                .with_parameter({"path", "The path to resolve."})
                .with_tags({"filename"})),

        /*
         * TODO: add other functions like normpath, ...
         */

        {nullptr},
    };

    *basic_funcs = fs_funcs;
    *agg_funcs = nullptr;

    return SQLITE_OK;
}
