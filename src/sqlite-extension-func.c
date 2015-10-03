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
 * @file sqlite-extension-func.c
 */

#include "config.h"

#include <stdio.h>
#include <assert.h>

#include "sqlite-extension-func.h"

sqlite_registration_func_t sqlite_registration_funcs[] = {
    common_extension_functions,
    state_extension_functions,
    string_extension_functions,
    network_extension_functions,
    fs_extension_functions,
    json_extension_functions,
    time_extension_functions,

    NULL
};

int register_sqlite_funcs(sqlite3 *db, sqlite_registration_func_t *reg_funcs)
{
    int lpc;

    assert(db != NULL);
    assert(reg_funcs != NULL);
    
    for (lpc = 0; reg_funcs[lpc]; lpc++) {
        const struct FuncDef *basic_funcs = NULL;
        const struct FuncDefAgg *agg_funcs = NULL;
        int i;

        reg_funcs[lpc](&basic_funcs, &agg_funcs);

        for (i = 0; basic_funcs && basic_funcs[i].zName; i++) {
            void *pArg = 0;

            switch (basic_funcs[i].argType) {
                case 1: pArg = db; break;
                case 2: pArg = (void *)(-1); break;
            }
            //sqlite3CreateFunc
            /* LMH no error checking */
            sqlite3_create_function(db,
                                    basic_funcs[i].zName,
                                    basic_funcs[i].nArg,
                                    basic_funcs[i].eTextRep,
                                    pArg,
                                    basic_funcs[i].xFunc,
                                    0,
                                    0);
        }

        for (i = 0; agg_funcs && agg_funcs[i].zName; i++) {
            void *pArg = 0;

            switch (agg_funcs[i].argType) {
                case 1: pArg = db; break;
                case 2: pArg = (void *)(-1); break;
            }
            //sqlite3CreateFunc
            sqlite3_create_function(db,
                                    agg_funcs[i].zName,
                                    agg_funcs[i].nArg,
                                    SQLITE_UTF8,
                                    pArg,
                                    0,
                                    agg_funcs[i].xStep,
                                    agg_funcs[i].xFinalize);
        }
    }

    return 0;
}
