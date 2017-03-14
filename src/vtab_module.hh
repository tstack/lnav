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

#ifndef __vtab_module_hh
#define __vtab_module_hh

#include <sqlite3.h>

#include <string>
#include <utility>

#include "lnav_log.hh"

template<typename T>
inline T from_sqlite(sqlite3_value *val)
{
    return T();
}

template<>
inline int64_t from_sqlite<int64_t>(sqlite3_value *val)
{
    return sqlite3_value_int64(val);
}

template<>
inline const char *from_sqlite<const char *>(sqlite3_value *val)
{
    return (const char *) sqlite3_value_text(val);
}

template<>
inline double from_sqlite<double>(sqlite3_value *val)
{
    return sqlite3_value_double(val);
}

extern std::string vtab_module_schemas;

template<typename T>
struct vtab_module {
    static int tvt_create(sqlite3 *db,
                          void *pAux,
                          int argc, const char *const *argv,
                          sqlite3_vtab **pp_vt,
                          char **pzErr) {
        static typename T::vtab vt;

        *pp_vt = vt;

        return sqlite3_declare_vtab(db, T::CREATE_STMT);
    };

    template<typename ... Args, size_t... Idx>
    static int apply_impl(T &obj, int (T::*func)(sqlite3_vtab *, sqlite3_int64 &, Args...), sqlite3_vtab *tab, sqlite3_int64 &rowid, sqlite3_value **argv, std::index_sequence<Idx...>)
    {
        return (obj.*func)(tab, rowid, from_sqlite<Args>(argv[Idx])...);
    }

    template<typename ... Args>
    static int apply(T &obj,
                     int (T::*func)(sqlite3_vtab *, sqlite3_int64 &, Args...),
                     sqlite3_vtab *tab,
                     sqlite3_int64 &rowid,
                     int argc,
                     sqlite3_value **argv)
    {
        require(sizeof...(Args) == 0 || argc == sizeof...(Args));

        return apply_impl(obj,
                          func,
                          tab,
                          rowid,
                          argv,
                          std::make_index_sequence<sizeof...(Args)>{});
    }

    static int tvt_destructor(sqlite3_vtab *p_svt)
    {
        return SQLITE_OK;
    }

    static int tvt_open(sqlite3_vtab *p_svt, sqlite3_vtab_cursor **pp_cursor)
    {
        p_svt->zErrMsg = NULL;

        typename T::cursor *p_cur = new (typename T::cursor)(p_svt);

        if (p_cur == NULL) {
            return SQLITE_NOMEM;
        } else {
            *pp_cursor = (sqlite3_vtab_cursor *) p_cur;
        }

        return SQLITE_OK;
    }

    static int tvt_next(sqlite3_vtab_cursor *cur)
    {
        typename T::cursor *p_cur = (typename T::cursor *) cur;

        return p_cur->next();
    }

    static int tvt_eof(sqlite3_vtab_cursor *cur)
    {
        typename T::cursor *p_cur = (typename T::cursor *) cur;

        return p_cur->eof();
    }

    static int tvt_close(sqlite3_vtab_cursor *cur)
    {
        typename T::cursor *p_cur = (typename T::cursor *) cur;

        delete p_cur;

        return SQLITE_OK;
    }


    static int tvt_rowid(sqlite3_vtab_cursor *cur, sqlite_int64 *p_rowid) {
        typename T::cursor *p_cur = (typename T::cursor *) cur;

        return p_cur->get_rowid(*p_rowid);
    };

    static int tvt_column(sqlite3_vtab_cursor *cur, sqlite3_context *ctx, int col) {
        typename T::cursor *p_cur = (typename T::cursor *) cur;
        T handler;

        return handler.get_column(*p_cur, ctx, col);
    };

    static int vt_best_index(sqlite3_vtab *tab, sqlite3_index_info *p_info) {
        return SQLITE_OK;
    };

    static int vt_filter(sqlite3_vtab_cursor *p_vtc,
                         int idxNum, const char *idxStr,
                         int argc, sqlite3_value **argv) {
        return SQLITE_OK;
    }

    static int tvt_update(sqlite3_vtab *tab,
                          int argc,
                          sqlite3_value **argv,
                          sqlite_int64 *rowid) {
        T handler;

        if (argc <= 1) {
            sqlite3_int64 rowid = sqlite3_value_int64(argv[0]);

            return handler.delete_row(tab, rowid);
        }

        if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
            sqlite3_int64 *rowid2 = rowid;
            return vtab_module<T>::apply(handler, &T::insert_row, tab, *rowid2, argc - 2, argv + 2);
        }

        sqlite3_int64 index = sqlite3_value_int64(argv[0]);

        if (index != sqlite3_value_int64(argv[1])) {
            tab->zErrMsg = sqlite3_mprintf(
                "The rowids in the lnav_views table cannot be changed");
            return SQLITE_ERROR;
        }

        return vtab_module<T>::apply(handler, &T::update_row, tab, index, argc - 2, argv + 2);
    };

    vtab_module() noexcept {
        memset(&this->vm_module, 0, sizeof(this->vm_module));
        this->vm_module.iVersion = 0;
        this->vm_module.xCreate = tvt_create;
        this->vm_module.xConnect = tvt_create;
        this->vm_module.xOpen = tvt_open;
        this->vm_module.xNext = tvt_next;
        this->vm_module.xEof = tvt_eof;
        this->vm_module.xClose = tvt_close;
        this->vm_module.xDestroy = tvt_destructor;
        this->vm_module.xRowid = tvt_rowid;
        this->vm_module.xDisconnect = tvt_destructor;
        this->vm_module.xBestIndex = vt_best_index;
        this->vm_module.xFilter = vt_filter;
        this->vm_module.xColumn = tvt_column;
        this->vm_module.xUpdate = tvt_update;
    };

    int create(sqlite3 *db, const char *name) {
        std::string impl_name = name;
        vtab_module_schemas += T::CREATE_STMT;

        // XXX Eponymous tables don't seem to work in older sqlite versions
        impl_name += "_impl";
        int rc = sqlite3_create_module(db, impl_name.c_str(), &this->vm_module, NULL);
        std::string create_stmt = std::string("CREATE VIRTUAL TABLE ") + name + " USING " + impl_name + "()";
        return sqlite3_exec(db, create_stmt.c_str(), NULL, NULL, NULL);
    };

    sqlite3_module vm_module;
};

template<typename T>
struct tvt_iterator_cursor {
    struct cursor {
        sqlite3_vtab_cursor base;
        typename T::iterator iter;

        cursor(sqlite3_vtab *vt) {
            T handler;

            this->base.pVtab = vt;
            this->iter = handler.begin();
        };

        int next() {
            T handler;

            if (this->iter != handler.end()) {
                ++this->iter;
            }

            return SQLITE_OK;
        };

        int eof() {
            T handler;

            return this->iter == handler.end();
        };

        int get_rowid(sqlite_int64 &rowid_out) {
            T handler;

            rowid_out = std::distance(handler.begin(), this->iter);

            return SQLITE_OK;
        };
    };
};

template<typename T>
struct tvt_no_update : public T {
    int delete_row(sqlite3_vtab *vt, sqlite3_int64 rowid) {
        vt->zErrMsg = sqlite3_mprintf(
            "Rows cannot be deleted from this table");
        return SQLITE_ERROR;
    };

    int insert_row(sqlite3_vtab *tab, sqlite3_int64 &rowid_out) {
        tab->zErrMsg = sqlite3_mprintf(
            "Rows cannot be inserted into this table");
        return SQLITE_ERROR;
    };

    int update_row(sqlite3_vtab *tab, sqlite3_int64 &rowid_out) {
        tab->zErrMsg = sqlite3_mprintf(
            "Rows cannot be update in this table");
        return SQLITE_ERROR;
    };

};

#endif
