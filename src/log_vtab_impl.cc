/**
 * Copyright (c) 2007-2012, Timothy Stack
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

#include "sql_util.hh"
#include "log_vtab_impl.hh"

#include "logfile_sub_source.hh"

using namespace std;

static struct log_cursor log_cursor_latest;

static sql_progress_callback_t vtab_progress_callback;

static const char *type_to_string(int type)
{
    switch (type) {
    case SQLITE_FLOAT:
        return "float";

    case SQLITE_INTEGER:
        return "integer";

    case SQLITE_TEXT:
        return "text";
    }

    assert("Invalid sqlite type");

    return NULL;
}

std::string log_vtab_impl::get_table_statement(void)
{
    std::vector<log_vtab_impl::vtab_column> cols;
    std::vector<log_vtab_impl::vtab_column>::const_iterator iter;
    std::ostringstream oss;

    oss << "CREATE TABLE " << this->get_name() << " (\n"
        << "  log_line integer PRIMARY KEY,\n"
        << "  log_part text collate naturalnocase,\n"
        << "  log_time datetime,\n"
        << "  log_idle_msecs int,\n"
        << "  log_level text collate loglevel,\n";
    this->get_columns(cols);
    this->vi_column_count = cols.size();
    for (iter = cols.begin(); iter != cols.end(); iter++) {
        auto_mem<char, sqlite3_free> coldecl;

        assert(iter->vc_name != NULL);

        coldecl = sqlite3_mprintf("  %Q %s %s collate %Q,\n",
                                  iter->vc_name,
                                  type_to_string(iter->vc_type),
                                  iter->vc_hidden ? "hidden" : "",
                                  (iter->vc_collator == NULL ||
                                   iter->vc_collator[0] == '\0') ?
                                  "BINARY" : iter->vc_collator);
        oss << coldecl;
    }
    oss << "  log_path text hidden collate naturalnocase,\n"
        << "  log_text text hidden\n"
        << ");\n";

    return oss.str();
}

struct vtab {
    sqlite3_vtab        base;
    sqlite3 *           db;
    textview_curses *tc;
    logfile_sub_source *lss;
    log_vtab_impl *     vi;
};

struct vtab_cursor {
    sqlite3_vtab_cursor        base;
    struct log_cursor          log_cursor;
    shared_buffer_ref          log_msg;
    std::vector<logline_value> line_values;
};

static int vt_destructor(sqlite3_vtab *p_svt);

static int vt_create(sqlite3 *db,
                     void *pAux,
                     int argc, const char *const *argv,
                     sqlite3_vtab **pp_vt,
                     char **pzErr)
{
    log_vtab_manager *vm = (log_vtab_manager *)pAux;
    int   rc             = SQLITE_OK;
    vtab *p_vt;

    /* Allocate the sqlite3_vtab/vtab structure itself */
    p_vt = (vtab *)sqlite3_malloc(sizeof(*p_vt));

    if (p_vt == NULL) {
        return SQLITE_NOMEM;
    }

    memset(&p_vt->base, 0, sizeof(sqlite3_vtab));
    p_vt->db = db;

    /* Declare the vtable's structure */
    p_vt->vi = vm->lookup_impl(argv[3]);
    if (p_vt->vi == NULL) {
        return SQLITE_ERROR;
    }
    p_vt->tc = vm->get_view();
    p_vt->lss = vm->get_source();
    rc        = sqlite3_declare_vtab(db, p_vt->vi->get_table_statement().c_str());

    /* Success. Set *pp_vt and return */
    *pp_vt = &p_vt->base;

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

    *pp_cursor = (sqlite3_vtab_cursor *)p_cur;

    p_cur->base.pVtab = p_svt;
    p_cur->log_cursor.lc_curr_line = vis_line_t(-1);
    p_cur->log_cursor.lc_sub_index = 0;
    vt_next((sqlite3_vtab_cursor *)p_cur);

    return p_cur ? SQLITE_OK : SQLITE_NOMEM;
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
    vtab *       vt = (vtab *)cur->pVtab;

    return vc->log_cursor.lc_curr_line == (int)vt->lss->text_line_count();
}

static int vt_next(sqlite3_vtab_cursor *cur)
{
    vtab_cursor *vc   = (vtab_cursor *)cur;
    vtab *       vt   = (vtab *)cur->pVtab;
    bool         done = false;

    vc->line_values.clear();
    do {
        log_cursor_latest = vc->log_cursor;
        if (((log_cursor_latest.lc_curr_line % 1024) == 0) &&
            vtab_progress_callback(log_cursor_latest)) {
            break;
        }
        done = vt->vi->next(vc->log_cursor, *vt->lss);
    } while (!done);

    return SQLITE_OK;
}

static int vt_column(sqlite3_vtab_cursor *cur, sqlite3_context *ctx, int col)
{
    vtab_cursor *vc = (vtab_cursor *)cur;
    vtab *       vt = (vtab *)cur->pVtab;

    content_line_t    cl(vt->lss->at(vc->log_cursor.lc_curr_line));
    logfile *         lf = vt->lss->find(cl);
    logfile::iterator ll = lf->begin() + cl;

    assert(col >= 0);

    /* Just return the ordinal of the column requested. */
    switch (col) {
    case VT_COL_LINE_NUMBER:
    {
        sqlite3_result_int64(ctx, vc->log_cursor.lc_curr_line);
    }
    break;

    case VT_COL_PARTITION:
    {
        vis_bookmarks &vb = vt->tc->get_bookmarks();
        bookmark_vector<vis_line_t> &bv = vb[&textview_curses::BM_USER];
        bookmark_vector<vis_line_t>::iterator iter;
        vis_line_t prev_line;
        char part_name[64];
        int index;

        prev_line = vis_line_t(vc->log_cursor.lc_curr_line);
        ++prev_line;
        iter = lower_bound(bv.begin(), bv.end(), prev_line);
        index = distance(bv.begin(), iter);
        snprintf(part_name, sizeof(part_name), "p.%d", index);

        if (iter == bv.begin()) {
            sqlite3_result_text(ctx, part_name, strlen(part_name), SQLITE_TRANSIENT);
        }
        else {
            --iter;

            content_line_t part_line = vt->lss->at(*iter);
            std::map<content_line_t, bookmark_metadata> &bm_meta = vt->lss->get_user_bookmark_metadata();
            std::map<content_line_t, bookmark_metadata>::iterator meta_iter;

            meta_iter = bm_meta.find(part_line);
            if (meta_iter != bm_meta.end()) {
                sqlite3_result_text(ctx,
                                    meta_iter->second.bm_name.c_str(),
                                    meta_iter->second.bm_name.size(),
                                    SQLITE_TRANSIENT);
            }
            else {
                sqlite3_result_text(ctx, part_name, strlen(part_name), SQLITE_TRANSIENT);
            }
        }
    }
    break;

    case VT_COL_LOG_TIME:
    {
        char   buffer[64];

        sql_strftime(buffer, sizeof(buffer), ll->get_time(), ll->get_millis());
        sqlite3_result_text(ctx, buffer, strlen(buffer), SQLITE_TRANSIENT);
    }
    break;

    case VT_COL_IDLE_MSECS:
        if (vc->log_cursor.lc_curr_line == 0) {
            sqlite3_result_int64(ctx, 0);
        }
        else {
            content_line_t prev_cl(vt->lss->at(vis_line_t(
                                                   vc->log_cursor.lc_curr_line -
                                                   1)));
            logfile *         prev_lf = vt->lss->find(prev_cl);
            logfile::iterator prev_ll = prev_lf->begin() + prev_cl;
            uint64_t          prev_time, curr_line_time;

            prev_time       = prev_ll->get_time() * 1000ULL;
            prev_time      += prev_ll->get_millis();
            curr_line_time  = ll->get_time() * 1000ULL;
            curr_line_time += ll->get_millis();
            // assert(curr_line_time >= prev_time);
            sqlite3_result_int64(ctx, curr_line_time - prev_time);
        }
        break;

    case VT_COL_LEVEL:
    {
        const char *level_name = ll->get_level_name();

        sqlite3_result_text(ctx,
                            level_name,
                            strlen(level_name),
                            SQLITE_STATIC);
    }
    break;

    default:
        if (col > (VT_COL_MAX + vt->vi->vi_column_count - 1)) {
            int post_col_number = col -
                                  (VT_COL_MAX + vt->vi->vi_column_count -
                                   1) - 1;

            if (post_col_number == 0) {
                const string &fn = lf->get_filename();

                sqlite3_result_text(ctx,
                                    fn.c_str(),
                                    fn.length(),
                                    SQLITE_STATIC);
            }
            else {
                shared_buffer_ref line;

                if (lf->read_line(ll, line)) {
                    sqlite3_result_text(ctx,
                                        line.get_data(),
                                        line.length(),
                                        SQLITE_TRANSIENT);
                }
            }
        }
        else {
            if (vc->line_values.empty()) {
                logfile::iterator line_iter;

                line_iter = lf->begin() + cl;
                lf->read_line(line_iter, vc->log_msg);
                vt->vi->extract(lf, vc->log_msg, vc->line_values);
            }

            size_t sub_col = col - VT_COL_MAX;
            std::vector<logline_value>::iterator lv_iter;

            lv_iter = find_if(vc->line_values.begin(), vc->line_values.end(),
                              logline_value_cmp(NULL, sub_col));

            if (lv_iter != vc->line_values.end()) {
                switch (lv_iter->lv_kind) {
                case logline_value::VALUE_NULL:
                    sqlite3_result_null(ctx);
                    break;
                case logline_value::VALUE_TEXT: {
                    const char *text_value = lv_iter->lv_sbr.get_data();

                    sqlite3_result_text(ctx,
                                        text_value,
                                        lv_iter->lv_sbr.length(),
                                        SQLITE_TRANSIENT);
                    break;
                }

                case logline_value::VALUE_BOOLEAN:
                case logline_value::VALUE_INTEGER:
                    sqlite3_result_int64(ctx, lv_iter->lv_number.i);
                    break;

                case logline_value::VALUE_FLOAT:
                    sqlite3_result_double(ctx, lv_iter->lv_number.d);
                    break;

                case logline_value::VALUE_UNKNOWN:
                case logline_value::VALUE__MAX:
                    assert(0);
                    break;
                }
            }
            else {
                sqlite3_result_null(ctx);
            }
        }
        break;
    }

    return SQLITE_OK;
}

static int vt_rowid(sqlite3_vtab_cursor *cur, sqlite_int64 *p_rowid)
{
    vtab_cursor *p_cur = (vtab_cursor *)cur;

    *p_rowid = (((uint64_t)p_cur->log_cursor.lc_curr_line) << 8) |
               (p_cur->log_cursor.lc_sub_index & 0xff);

    return SQLITE_OK;
}

static int vt_filter(sqlite3_vtab_cursor *p_vtc,
                     int idxNum, const char *idxStr,
                     int argc, sqlite3_value **argv)
{
    return SQLITE_OK;
}

static int vt_best_index(sqlite3_vtab *tab, sqlite3_index_info *p_info)
{
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
    NULL,           /* xUpdate       - write data */
    NULL,           /* xBegin        - begin transaction */
    NULL,           /* xSync         - sync transaction */
    NULL,           /* xCommit       - commit transaction */
    NULL,           /* xRollback     - rollback transaction */
    NULL,           /* xFindFunction - function overloading */
};

static int progress_callback(void *ptr)
{
    int retval = 0;

    if (vtab_progress_callback != NULL) {
        retval = vtab_progress_callback(log_cursor_latest);
    }

    return retval;
}

log_vtab_manager::log_vtab_manager(sqlite3 *memdb,
                                   textview_curses &tc,
                                   logfile_sub_source &lss,
                                   sql_progress_callback_t pc)
    : vm_db(memdb), vm_textview(tc), vm_source(lss)
{
    sqlite3_create_module(this->vm_db, "log_vtab_impl", &vtab_module, this);
    vtab_progress_callback = pc;
    sqlite3_progress_handler(memdb, 32, progress_callback, NULL);
}

string log_vtab_manager::register_vtab(log_vtab_impl *vi)
{
    string retval;

    if (this->vm_impls.find(vi->get_name()) == this->vm_impls.end()) {
        auto_mem<char> errmsg(sqlite3_free);
        char *         sql;
        int            rc;

        this->vm_impls[vi->get_name()] = vi;

        sql = sqlite3_mprintf("CREATE VIRTUAL TABLE %s "
                              "USING log_vtab_impl(%s)",
                              vi->get_name().c_str(),
                              vi->get_name().c_str());
        rc = sqlite3_exec(this->vm_db,
                          sql,
                          NULL,
                          NULL,
                          errmsg.out());
        if (rc != SQLITE_OK) {
            retval = errmsg;
        }

        sqlite3_free(sql);
    }
    else {
        retval = "a table with the given name already exists";
    }

    return retval;
}

string log_vtab_manager::unregister_vtab(std::string name)
{
    string retval = "";

    if (this->vm_impls.find(name) == this->vm_impls.end()) {
        retval = "unknown log line table -- " + name;
    }
    else {
        char *sql;
        int   rc;

        sql = sqlite3_mprintf("DROP TABLE %s ", name.c_str());
        rc  = sqlite3_exec(this->vm_db,
                           sql,
                           NULL,
                           NULL,
                           NULL);
        assert(rc == SQLITE_OK);

        sqlite3_free(sql);

        this->vm_impls.erase(name);
    }

    return retval;
}
