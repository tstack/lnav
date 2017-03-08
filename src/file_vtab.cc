/**
 * Copyright (c) 2017, Timothy Stack
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
 */

#include "config.h"

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "lnav.hh"
#include "auto_mem.hh"
#include "lnav_log.hh"
#include "sql_util.hh"
#include "file_vtab.hh"

using namespace std;

const char *LNAV_FILE_CREATE_STMT = "\
-- Access lnav's open file list through this table.\n\
CREATE TABLE lnav_file (\n\
    device integer,\n\
    inode integer,\n\
    filepath text,\n\
    format text,\n\
    lines integer\n\
);\n\
";

struct vtab {
    sqlite3_vtab        base;
    sqlite3 *           db;
};

struct vtab_cursor {
    sqlite3_vtab_cursor        base;
    int vc_index;
};

static int vt_destructor(sqlite3_vtab *p_svt);

static int vt_create(sqlite3 *db,
                     void *pAux,
                     int argc, const char *const *argv,
                     sqlite3_vtab **pp_vt,
                     char **pzErr)
{
    vtab *p_vt;

    /* Allocate the sqlite3_vtab/vtab structure itself */
    p_vt = (vtab *)sqlite3_malloc(sizeof(*p_vt));

    if (p_vt == NULL) {
        return SQLITE_NOMEM;
    }

    memset(&p_vt->base, 0, sizeof(sqlite3_vtab));
    p_vt->db = db;

    *pp_vt = &p_vt->base;

    int rc = sqlite3_declare_vtab(db, LNAV_FILE_CREATE_STMT);

    return rc;
}


static int vt_destructor(sqlite3_vtab *p_svt)
{
    vtab *p_vt = (vtab *)p_svt;

    /* Free the SQLite structure */
    sqlite3_free(p_vt);

    return SQLITE_OK;
}

static int vt_connect(sqlite3 *db, void *p_aux,
                      int argc, const char *const *argv,
                      sqlite3_vtab **pp_vt, char **pzErr)
{
    return vt_create(db, p_aux, argc, argv, pp_vt, pzErr);
}

static int vt_disconnect(sqlite3_vtab *pVtab)
{
    return vt_destructor(pVtab);
}

static int vt_destroy(sqlite3_vtab *p_vt)
{
    return vt_destructor(p_vt);
}

static int vt_next(sqlite3_vtab_cursor *cur);

static int vt_open(sqlite3_vtab *p_svt, sqlite3_vtab_cursor **pp_cursor)
{
    vtab *p_vt = (vtab *)p_svt;

    p_vt->base.zErrMsg = NULL;

    vtab_cursor *p_cur = (vtab_cursor *)new vtab_cursor();

    if (p_cur == NULL) {
        return SQLITE_NOMEM;
    } else {
        *pp_cursor = (sqlite3_vtab_cursor *)p_cur;

        p_cur->base.pVtab = p_svt;
        p_cur->vc_index = 0;
    }

    return SQLITE_OK;
}

static int vt_close(sqlite3_vtab_cursor *cur)
{
    vtab_cursor *p_cur = (vtab_cursor *)cur;

    /* Free cursor struct. */
    delete p_cur;

    return SQLITE_OK;
}

static int vt_eof(sqlite3_vtab_cursor *cur)
{
    vtab_cursor *vc = (vtab_cursor *)cur;

    return vc->vc_index >= lnav_data.ld_files.size();
}

static int vt_next(sqlite3_vtab_cursor *cur)
{
    vtab_cursor *vc   = (vtab_cursor *)cur;

    if (vc->vc_index < lnav_data.ld_files.size()) {
        vc->vc_index += 1;
    }

    return SQLITE_OK;
}

static int vt_column(sqlite3_vtab_cursor *cur, sqlite3_context *ctx, int col)
{
    vtab_cursor *vc = (vtab_cursor *)cur;
    logfile *lf = lnav_data.ld_files[vc->vc_index];
    const struct stat &st = lf->get_stat();
    const string &name = lf->get_filename();
    log_format *format = lf->get_format();
    const char *format_name = format != NULL ? format->get_name().get() : NULL;

    switch (col) {
        case 0:
            sqlite3_result_int(ctx, st.st_dev);
            break;
        case 1:
            sqlite3_result_int(ctx, st.st_ino);
            break;
        case 2:
            sqlite3_result_text(ctx, name.c_str(), name.size(), SQLITE_TRANSIENT);
            break;
        case 3:
            if (format_name != NULL) {
                sqlite3_result_text(ctx, format_name, -1, SQLITE_STATIC);
            } else {
                sqlite3_result_null(ctx);
            }
            break;
        case 4:
            sqlite3_result_int(ctx, lf->size());
            break;
    }

    return SQLITE_OK;
}

static int vt_rowid(sqlite3_vtab_cursor *cur, sqlite_int64 *p_rowid)
{
    vtab_cursor *p_cur = (vtab_cursor *)cur;

    *p_rowid = (int64_t) p_cur->vc_index;

    return SQLITE_OK;
}

static int vt_best_index(sqlite3_vtab *tab, sqlite3_index_info *p_info)
{
    return SQLITE_OK;
}

static int vt_filter(sqlite3_vtab_cursor *p_vtc,
                     int idxNum, const char *idxStr,
                     int argc, sqlite3_value **argv)
{
    return SQLITE_OK;
}

static int vt_update(sqlite3_vtab *tab,
                     int argc,
                     sqlite3_value **argv,
                     sqlite_int64 *rowid)
{
    return SQLITE_ERROR;
}

static sqlite3_module vtab_module = {
    0,              /* iVersion */
    vt_create,      /* xCreate       - create a vtable */
    vt_connect,     /* xConnect      - associate a vtable with a connection */
    vt_best_index,  /* xBestIndex    - best index */
    vt_disconnect,  /* xDisconnect   - disassociate a vtable with a connection */
    vt_destroy,     /* xDestroy      - destroy a vtable */
    vt_open,        /* xOpen         - open a cursor */
    vt_close,       /* xClose        - close a cursor */
    vt_filter,      /* xFilter       - configure scan constraints */
    vt_next,        /* xNext         - advance a cursor */
    vt_eof,         /* xEof          - inidicate end of result set*/
    vt_column,      /* xColumn       - read data */
    vt_rowid,       /* xRowid        - read data */
    vt_update,      /* xUpdate       - write data */
    NULL,           /* xBegin        - begin transaction */
    NULL,           /* xSync         - sync transaction */
    NULL,           /* xCommit       - commit transaction */
    NULL,           /* xRollback     - rollback transaction */
    NULL,           /* xFindFunction - function overloading */
};

int register_file_vtab(sqlite3 *db)
{
    auto_mem<char, sqlite3_free> errmsg;
    int rc;

    rc = sqlite3_create_module(db, "lnav_file_vtab_impl", &vtab_module, NULL);
    assert(rc == SQLITE_OK);
    if ((rc = sqlite3_exec(db,
             "CREATE VIRTUAL TABLE lnav_file USING lnav_file_vtab_impl()",
             NULL, NULL, errmsg.out())) != SQLITE_OK) {
        fprintf(stderr, "unable to create lnav_file table %s\n", errmsg.in());
    }
    return rc;
}
