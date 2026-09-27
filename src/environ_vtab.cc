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

#include <optional>

#include "environ_vtab.hh"

#include <stdlib.h>
#include <string.h>

#include "base/auto_mem.hh"
#include "base/intern_string.hh"
#include "base/lnav_log.hh"
#include "config.h"
#include "vtab_module.hh"

extern char** environ;

const char* const ENVIRON_CREATE_STMT = R"(
-- Access lnav's environment variables through this table.
CREATE TABLE lnav_db.environ (
    name TEXT PRIMARY KEY,
    value TEXT
) WITHOUT ROWID;
)";

namespace {

struct env_vtab {
    sqlite3_vtab base;
    sqlite3* db;
};

struct env_vtab_cursor {
    sqlite3_vtab_cursor base;
    char** env_cursor;

    /**
     * Move past any entries without an "=".  A process can be started with
     * one, but getenv() cannot see it and setenv() cannot change it, so it
     * is left out of the table like most programs do.
     */
    void skip_nameless()
    {
        while (this->env_cursor[0] != nullptr
               && strchr(this->env_cursor[0], '=') == nullptr)
        {
            this->env_cursor += 1;
        }
    }
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
        p_cur->skip_nameless();
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
        vc->skip_nameless();
    }

    return SQLITE_OK;
}

static int
vt_column(sqlite3_vtab_cursor* cur, sqlite3_context* ctx, int col)
{
    env_vtab_cursor* vc = (env_vtab_cursor*) cur;
    const auto [name, value]
        = string_fragment::from_c_str(vc->env_cursor[0])
              .split_when(string_fragment::tag1{'='});

    switch (col) {
        case 0:
            to_sqlite(ctx, name);
            break;
        case 1:
            to_sqlite(ctx, value);
            break;
    }

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
    using opt_text = from_sqlite<std::optional<string_fragment>>;

    // The table is WITHOUT ROWID, so argv[0] is the name of the variable
    // being deleted or updated, or NULL for an insert.  The fragments are
    // not guaranteed to be NUL-terminated, so they are copied into strings
    // before being handed to the libc environment functions.
    const auto old_name = opt_text()(argc, argv, 0);
    env_vtab* p_vt = (env_vtab*) tab;

    if (argc == 1) {
        if (old_name) {
            unsetenv(old_name->to_string().c_str());
        }
        return SQLITE_OK;
    }

    const auto name = opt_text()(argc, argv, 2);
    const auto value = opt_text()(argc, argv, 3);

    if (!name || name->empty() || !value) {
        tab->zErrMsg = sqlite3_mprintf(
            "A non-empty name and value must be provided when inserting an "
            "environment variable");

        return SQLITE_ERROR;
    }
    if (name->find('=')) {
        tab->zErrMsg = sqlite3_mprintf(
            "Environment variable names cannot contain an equals sign (=)");

        return SQLITE_ERROR;
    }

    const auto name_str = name->to_string();

    // Both an insert and a rename can collide with another variable.
    const auto is_rename = old_name && old_name.value() != name.value();
    if ((!old_name || is_rename) && getenv(name_str.c_str()) != nullptr) {
        auto rc = sqlite3_vtab_on_conflict(p_vt->db);

        switch (rc) {
            case SQLITE_IGNORE:
                return SQLITE_OK;
            case SQLITE_REPLACE:
                break;
            default:
                tab->zErrMsg = sqlite3_mprintf(
                    "An environment variable with the name '%.*s' already "
                    "exists",
                    name->length(),
                    name->data());
                return rc == SQLITE_FAIL || rc == SQLITE_ABORT
                    ? rc
                    : SQLITE_CONSTRAINT;
        }
    }

    if (is_rename) {
        unsetenv(old_name->to_string().c_str());
    }
    setenv(name_str.c_str(), value->to_string().c_str(), 1);

    return SQLITE_OK;
}

static sqlite3_module environ_module = {
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
    nullptr, /* xRowid        - read data */
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

    rc = sqlite3_create_module(
        db, "environ_vtab_impl", &environ_module, nullptr);
    ensure(rc == SQLITE_OK);
    if ((rc = sqlite3_exec(
             db,
             "CREATE VIRTUAL TABLE lnav_db.environ USING environ_vtab_impl()",
             nullptr,
             nullptr,
             errmsg.out()))
        != SQLITE_OK)
    {
        fprintf(stderr, "unable to create environ table %s\n", errmsg.in());
    }
    return rc;
}
