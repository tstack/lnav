/**
 * Copyright (c) 2014, Timothy Stack
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
 */

#include "environ_vtab.hh"

#include <stdlib.h>
#include <string.h>

#include "base/auto_mem.hh"
#include "base/lnav_log.hh"
#include "base/short_alloc.h"
#include "config.h"

extern char** environ;

const char* const ENVIRON_CREATE_STMT = R"(
-- Access lnav's environment variables through this table.
CREATE TABLE environ (
    name TEXT PRIMARY KEY,
    value TEXT
);
)";

namespace {

struct env_vtab {
    sqlite3_vtab base;
    sqlite3* db;
};

struct env_vtab_cursor {
    sqlite3_vtab_cursor base;
    char** env_cursor;
};

static int vt_destructor(sqlite3_vtab* p_svt);

static int
vt_create(sqlite3* db,
          void* pAux,
          int argc,
          const char* const* argv,
          sqlite3_vtab** pp_vt,
          char** pzErr)
{
    env_vtab* p_vt;

    /* Allocate the sqlite3_vtab/vtab structure itself */
    p_vt = (env_vtab*) sqlite3_malloc(sizeof(*p_vt));

    if (p_vt == nullptr) {
        return SQLITE_NOMEM;
    }

    memset(&p_vt->base, 0, sizeof(sqlite3_vtab));
    p_vt->db = db;

    *pp_vt = &p_vt->base;

    int rc = sqlite3_declare_vtab(db, ENVIRON_CREATE_STMT);

    return rc;
}

static int
vt_destructor(sqlite3_vtab* p_svt)
{
    env_vtab* p_vt = (env_vtab*) p_svt;

    /* Free the SQLite structure */
    sqlite3_free(p_vt);

    return SQLITE_OK;
}

static int
vt_connect(sqlite3* db,
           void* p_aux,
           int argc,
           const char* const* argv,
           sqlite3_vtab** pp_vt,
           char** pzErr)
{
    return vt_create(db, p_aux, argc, argv, pp_vt, pzErr);
}

static int
vt_disconnect(sqlite3_vtab* pVtab)
{
    return vt_destructor(pVtab);
}

static int
vt_destroy(sqlite3_vtab* p_vt)
{
    return vt_destructor(p_vt);
}

static int vt_next(sqlite3_vtab_cursor* cur);

static int
vt_open(sqlite3_vtab* p_svt, sqlite3_vtab_cursor** pp_cursor)
{
    env_vtab* p_vt = (env_vtab*) p_svt;

    p_vt->base.zErrMsg = nullptr;

    env_vtab_cursor* p_cur = (env_vtab_cursor*) new env_vtab_cursor();

    if (p_cur == nullptr) {
        return SQLITE_NOMEM;
    } else {
        *pp_cursor = (sqlite3_vtab_cursor*) p_cur;

        p_cur->base.pVtab = p_svt;
        p_cur->env_cursor = environ;
    }

    return SQLITE_OK;
}

static int
vt_close(sqlite3_vtab_cursor* cur)
{
    env_vtab_cursor* p_cur = (env_vtab_cursor*) cur;

    /* Free cursor struct. */
    delete p_cur;

    return SQLITE_OK;
}

static int
vt_eof(sqlite3_vtab_cursor* cur)
{
    env_vtab_cursor* vc = (env_vtab_cursor*) cur;

    return vc->env_cursor[0] == nullptr;
}

static int
vt_next(sqlite3_vtab_cursor* cur)
{
    env_vtab_cursor* vc = (env_vtab_cursor*) cur;

    if (vc->env_cursor[0] != nullptr) {
        vc->env_cursor += 1;
    }

    return SQLITE_OK;
}

static int
vt_column(sqlite3_vtab_cursor* cur, sqlite3_context* ctx, int col)
{
    env_vtab_cursor* vc = (env_vtab_cursor*) cur;
    const char* eq = strchr(vc->env_cursor[0], '=');

    switch (col) {
        case 0:
            sqlite3_result_text(ctx,
                                vc->env_cursor[0],
                                eq - vc->env_cursor[0],
                                SQLITE_TRANSIENT);
            break;
        case 1:
            sqlite3_result_text(ctx, eq + 1, -1, SQLITE_TRANSIENT);
            break;
    }

    return SQLITE_OK;
}

static int
vt_rowid(sqlite3_vtab_cursor* cur, sqlite_int64* p_rowid)
{
    env_vtab_cursor* p_cur = (env_vtab_cursor*) cur;

    *p_rowid = (int64_t) p_cur->env_cursor[0];

    return SQLITE_OK;
}

static int
vt_best_index(sqlite3_vtab* tab, sqlite3_index_info* p_info)
{
    return SQLITE_OK;
}

static int
vt_filter(sqlite3_vtab_cursor* p_vtc,
          int idxNum,
          const char* idxStr,
          int argc,
          sqlite3_value** argv)
{
    return SQLITE_OK;
}

static int
vt_update(sqlite3_vtab* tab,
          int argc,
          sqlite3_value** argv,
          sqlite_int64* rowid)
{
    const char* name
        = (argc > 2 ? (const char*) sqlite3_value_text(argv[2]) : nullptr);
    env_vtab* p_vt = (env_vtab*) tab;
    int retval = SQLITE_ERROR;

    if (argc != 1
        && (argc < 3 || sqlite3_value_type(argv[2]) == SQLITE_NULL
            || sqlite3_value_type(argv[3]) == SQLITE_NULL
            || sqlite3_value_text(argv[2])[0] == '\0'))
    {
        tab->zErrMsg = sqlite3_mprintf(
            "A non-empty name and value must be provided when inserting an "
            "environment variable");

        return SQLITE_ERROR;
    }
    if (name != nullptr && strchr(name, '=') != nullptr) {
        tab->zErrMsg = sqlite3_mprintf(
            "Environment variable names cannot contain an equals sign (=)");

        return SQLITE_ERROR;
    }

    if (sqlite3_value_type(argv[0]) != SQLITE_NULL) {
        int64_t index = sqlite3_value_int64(argv[0]);
        const char* var = (const char*) index;
        const char* eq = strchr(var, '=');
        size_t namelen = eq - var;
        stack_buf allocator;
        auto* name = allocator.allocate(namelen + 1);

        memcpy(name, var, namelen);
        name[namelen] = '\0';
        unsetenv(name);

        retval = SQLITE_OK;
    } else if (name != nullptr && getenv(name) != nullptr) {
#ifdef SQLITE_FAIL
        int rc;

        rc = sqlite3_vtab_on_conflict(p_vt->db);
        switch (rc) {
            case SQLITE_FAIL:
            case SQLITE_ABORT:
                tab->zErrMsg = sqlite3_mprintf(
                    "An environment variable with the name '%s' already exists",
                    name);
                return rc;
            case SQLITE_IGNORE:
                return SQLITE_OK;
            case SQLITE_REPLACE:
                break;
            default:
                return rc;
        }
#endif
    }

    if (name != nullptr && argc == 4) {
        const unsigned char* value = sqlite3_value_text(argv[3]);

        setenv((const char*) name, (const char*) value, 1);

        return SQLITE_OK;
    }

    return retval;
}

static sqlite3_module vtab_module = {
    0, /* iVersion */
    vt_create, /* xCreate       - create a vtable */
    vt_connect, /* xConnect      - associate a vtable with a connection */
    vt_best_index, /* xBestIndex    - best index */
    vt_disconnect, /* xDisconnect   - disassociate a vtable with a connection */
    vt_destroy, /* xDestroy      - destroy a vtable */
    vt_open, /* xOpen         - open a cursor */
    vt_close, /* xClose        - close a cursor */
    vt_filter, /* xFilter       - configure scan constraints */
    vt_next, /* xNext         - advance a cursor */
    vt_eof, /* xEof          - inidicate end of result set*/
    vt_column, /* xColumn       - read data */
    vt_rowid, /* xRowid        - read data */
    vt_update, /* xUpdate       - write data */
    nullptr, /* xBegin        - begin transaction */
    nullptr, /* xSync         - sync transaction */
    nullptr, /* xCommit       - commit transaction */
    nullptr, /* xRollback     - rollback transaction */
    nullptr, /* xFindFunction - function overloading */
};

}  // namespace

int
register_environ_vtab(sqlite3* db)
{
    auto_mem<char, sqlite3_free> errmsg;
    int rc;

    rc = sqlite3_create_module(db, "environ_vtab_impl", &vtab_module, nullptr);
    ensure(rc == SQLITE_OK);
    if ((rc = sqlite3_exec(
             db,
             "CREATE VIRTUAL TABLE environ USING environ_vtab_impl()",
             nullptr,
             nullptr,
             errmsg.out()))
        != SQLITE_OK)
    {
        fprintf(stderr, "unable to create environ table %s\n", errmsg.in());
    }
    return rc;
}
