/**
 * Copyright (c) 2015, Timothy Stack
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
#include "views_vtab.hh"
#include "view_curses.hh"

using namespace std;

const char *LNAV_VIEWS_CREATE_STMT = "\
-- Access lnav's views through this table.\n\
CREATE TABLE lnav_views (\n\
    name text PRIMARY KEY,\n\
    top integer,\n\
    left integer,\n\
    height integer,\n\
    inner_height integer\n\
);\n\
";

struct vtab {
    sqlite3_vtab        base;
    sqlite3 *           db;
};

struct vtab_cursor {
    sqlite3_vtab_cursor        base;
    lnav_view_t vc_cursor;
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

    int rc = sqlite3_declare_vtab(db, LNAV_VIEWS_CREATE_STMT);

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
        p_cur->vc_cursor = (lnav_view_t) -1;

        vt_next((sqlite3_vtab_cursor *)p_cur);
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

    return vc->vc_cursor == LNV__MAX;
}

static int vt_next(sqlite3_vtab_cursor *cur)
{
    vtab_cursor *vc   = (vtab_cursor *)cur;

    if (vc->vc_cursor < LNV__MAX) {
        vc->vc_cursor = (lnav_view_t) (vc->vc_cursor + 1);
    }

    return SQLITE_OK;
}

static int vt_column(sqlite3_vtab_cursor *cur, sqlite3_context *ctx, int col)
{
    vtab_cursor *vc = (vtab_cursor *)cur;
    textview_curses &tc = lnav_data.ld_views[vc->vc_cursor];
    unsigned long width;
    vis_line_t height;

    tc.get_dimensions(height, width);
    switch (col) {
        case 0:
            sqlite3_result_text(ctx,
                                lnav_view_strings[vc->vc_cursor], -1,
                                SQLITE_STATIC);
            break;
        case 1:
            sqlite3_result_int(ctx, (int) tc.get_top());
            break;
        case 2:
            sqlite3_result_int(ctx, tc.get_left());
            break;
        case 3:
            sqlite3_result_int(ctx, height);
            break;
        case 4:
            sqlite3_result_int(ctx, tc.get_inner_height());
            break;
    }

    return SQLITE_OK;
}

static int vt_rowid(sqlite3_vtab_cursor *cur, sqlite_int64 *p_rowid)
{
    vtab_cursor *p_cur = (vtab_cursor *)cur;

    *p_rowid = p_cur->vc_cursor;

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
    if (argc <= 1) {
        tab->zErrMsg = sqlite3_mprintf(
                "Rows cannot be deleted from the lnav_views table");
        return SQLITE_ERROR;
    }

    if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
        tab->zErrMsg = sqlite3_mprintf(
                "Rows cannot be inserted into the lnav_views table");
        return SQLITE_ERROR;
    }

    int64_t index = sqlite3_value_int64(argv[0]);

    if (index != sqlite3_value_int64(argv[1])) {
        tab->zErrMsg = sqlite3_mprintf(
                "The rowids in the lnav_views table cannot be changed");
        return SQLITE_ERROR;
    }

    textview_curses &tc = lnav_data.ld_views[index];
    int64_t top = sqlite3_value_int64(argv[3]);
    int64_t left = sqlite3_value_int64(argv[4]);

    tc.set_top(vis_line_t(top));
    tc.set_left(left);

    return SQLITE_OK;
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

int register_views_vtab(sqlite3 *db)
{
    auto_mem<char, sqlite3_free> errmsg;
    int rc;

    rc = sqlite3_create_module(db, "views_vtab_impl", &vtab_module, NULL);
    assert(rc == SQLITE_OK);
    if ((rc = sqlite3_exec(db,
             "CREATE VIRTUAL TABLE lnav_views USING views_vtab_impl()",
             NULL, NULL, errmsg.out())) != SQLITE_OK) {
        fprintf(stderr, "wtf %s\n", errmsg.in());
    }
    return rc;
}
