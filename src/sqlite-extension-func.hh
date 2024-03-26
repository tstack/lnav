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
 * @file sqlite-extension-func.h
 */

#ifndef lnav_sqlite_extension_func_h
#define lnav_sqlite_extension_func_h

#include <map>
#include <string>

#include <sqlite3.h>
#include <stdint.h>

#include "help_text.hh"

struct FuncDef {
    const char* zName{nullptr};
    signed char nArg{0};
    int eTextRep{0}; /* 1: UTF-16.  0: UTF-8 */
    uint8_t needCollSeq{0};
    void (*xFunc)(sqlite3_context*, int, sqlite3_value**){nullptr};
    help_text fd_help{};

    FuncDef& with_flags(int flags)
    {
        this->eTextRep = flags;
        return *this;
    }
};

struct FuncDefAgg {
    const char* zName{nullptr};
    signed char nArg{0};
    uint8_t needCollSeq{0};
    void (*xStep)(sqlite3_context*, int, sqlite3_value**){nullptr};
    void (*xFinalize)(sqlite3_context*){nullptr};
    help_text fda_help{};
};

typedef int (*sqlite_registration_func_t)(struct FuncDef** basic_funcs,
                                          struct FuncDefAgg** agg_funcs);

int common_extension_functions(struct FuncDef** basic_funcs,
                               struct FuncDefAgg** agg_funcs);

int state_extension_functions(struct FuncDef** basic_funcs,
                              struct FuncDefAgg** agg_funcs);

int string_extension_functions(struct FuncDef** basic_funcs,
                               struct FuncDefAgg** agg_funcs);

int network_extension_functions(struct FuncDef** basic_funcs,
                                struct FuncDefAgg** agg_funcs);

int fs_extension_functions(struct FuncDef** basic_funcs,
                           struct FuncDefAgg** agg_funcs);

int json_extension_functions(struct FuncDef** basic_funcs,
                             struct FuncDefAgg** agg_funcs);

int time_extension_functions(struct FuncDef** basic_funcs,
                             struct FuncDefAgg** agg_funcs);

int yaml_extension_functions(struct FuncDef** basic_funcs,
                             struct FuncDefAgg** agg_funcs);

extern sqlite_registration_func_t sqlite_registration_funcs[];

int register_sqlite_funcs(sqlite3* db, sqlite_registration_func_t* reg_funcs);

extern std::string sqlite_extension_prql;

extern "C"
{
int sqlite3_db_dump(
    sqlite3* db, /* The database connection */
    const char* zSchema, /* Which schema to dump.  Usually "main". */
    const char* zTable, /* Which table to dump.  NULL means everything. */
    int (*xCallback)(const char*, void*), /* Output sent to this callback */
    void* pArg /* Second argument of the callback */
);
}

#endif
