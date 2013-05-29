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
 * @file fs-extension-functions.cc
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <stdint.h>

#include <string>

#include "sqlite3.h"

static void sql_basename(sqlite3_context *context,
                         int argc, sqlite3_value **argv)
{
    const char *path_in;
    int text_end = -1;

    if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
        sqlite3_result_null(context);
        return;
    }

    path_in = (const char *)sqlite3_value_text(argv[0]);

    if (path_in[0] == '\0') {
        sqlite3_result_text(context, ".", 1, SQLITE_STATIC);
        return;
    }

    for (int lpc = strlen(path_in) - 1; lpc >= 0; lpc--) {
        if (path_in[lpc] == '/' || path_in[lpc] == '\\') {
            if (text_end != -1) {
                sqlite3_result_text(context,
                                    &path_in[lpc + 1], text_end - lpc - 1,
                                    SQLITE_TRANSIENT);
                return;
            }
        }
        else if (text_end == -1) {
            text_end = lpc + 1;
        }
    }

    if (text_end == -1) {
        sqlite3_result_text(context, "/", 1, SQLITE_STATIC);
    }
    else {
        sqlite3_result_text(context,
                            path_in, text_end,
                            SQLITE_TRANSIENT);
    }
}

static void sql_dirname(sqlite3_context *context,
                        int argc, sqlite3_value **argv)
{
    const char *path_in;
    int text_end;

    if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
        sqlite3_result_null(context);
        return;
    }

    path_in = (const char *)sqlite3_value_text(argv[0]);

    text_end = strlen(path_in) - 1;
    while (text_end >= 0 &&
           (path_in[text_end] == '/' || path_in[text_end] == '\\')) {
        text_end -= 1;
    }

    while (text_end >= 0) {
        if (path_in[text_end] == '/' || path_in[text_end] == '\\') {
            sqlite3_result_text(context,
                                path_in, text_end + 1,
                                SQLITE_TRANSIENT);
            return;
        }

        text_end -= 1;
    }

    if (text_end == -1) {
        sqlite3_result_text(context,
                            path_in[0] == '/' ? "/" : ".", 1,
                            SQLITE_STATIC);
        return;
    }
}

static void sql_joinpath(sqlite3_context *context,
                         int argc, sqlite3_value **argv)
{
    std::string full_path;
    int lpc;

    for (lpc = 0; lpc < argc; lpc++) {
        if (sqlite3_value_type(argv[lpc]) == SQLITE_NULL) {
            sqlite3_result_null(context);
            return;
        }
    }

    for (lpc = 0; lpc < argc; lpc++) {
        const char *path_in;

        path_in = (const char *)sqlite3_value_text(argv[lpc]);

        if (path_in[0] == '/' || path_in[0] == '\\') {
            full_path.clear();
        }
        if (!full_path.empty() &&
            full_path[full_path.length() - 1] != '/' &&
            full_path[full_path.length() - 1] != '\\') {
            full_path += "/";
        }
        full_path += path_in;
    }

    sqlite3_result_text(context, full_path.c_str(), -1, SQLITE_TRANSIENT);
}

int register_fs_extension_functions(sqlite3 *db)
{
    static const struct {
        const char *name;
        char        narg;
        uint8_t     text_rep;
        void        (*func)(sqlite3_context *, int, sqlite3_value **);
    } plain_funcs[] = {
        { "basename", 1, SQLITE_UTF8, sql_basename },
        { "dirname", 1, SQLITE_UTF8, sql_dirname },
        { "joinpath", 2, SQLITE_UTF8, sql_joinpath },

        { NULL }
    };

    int retval;

    for (int lpc = 0; plain_funcs[lpc].name; lpc++) {
        retval = sqlite3_create_function(db,
                                         plain_funcs[lpc].name,
                                         plain_funcs[lpc].narg,
                                         plain_funcs[lpc].text_rep,
                                         NULL,
                                         plain_funcs[lpc].func,
                                         NULL,
                                         NULL);
        if (retval != SQLITE_OK) {
            return retval;
        }
    }

    return SQLITE_OK;
}
