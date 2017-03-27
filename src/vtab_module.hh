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

#include "optional.hpp"
#include "lnav_log.hh"
#include "lnav_util.hh"
#include "yajlpp.hh"
#include "mapbox/variant.hpp"

#include "sqlite-extension-func.hh"

struct from_sqlite_conversion_error : std::exception {
    from_sqlite_conversion_error(const char *type, int argi)
        : e_type(type), e_argi(argi) {

    };

    const char *e_type;
    int e_argi;
};

template<typename T>
struct from_sqlite {
    inline T operator()(int argc, sqlite3_value **val, int argi) {
        return T();
    };
};

template<>
struct from_sqlite<int64_t> {
    inline int64_t operator()(int argc, sqlite3_value **val, int argi) {
        if (sqlite3_value_numeric_type(val[argi]) != SQLITE_INTEGER) {
            throw from_sqlite_conversion_error("integer", argi);
        }

        return sqlite3_value_int64(val[argi]);
    }
};

template<>
struct from_sqlite<int> {
    inline int operator()(int argc, sqlite3_value **val, int argi) {
        return sqlite3_value_int(val[argi]);
    }
};

template<>
struct from_sqlite<const char *> {
    inline const char *operator()(int argc, sqlite3_value **val, int argi) {
        return (const char *) sqlite3_value_text(val[argi]);
    }
};

template<>
struct from_sqlite<double> {
    inline double operator()(int argc, sqlite3_value **val, int argi) {
        return sqlite3_value_double(val[argi]);
    }
};

template<typename T>
struct from_sqlite<nonstd::optional<T>> {
    inline nonstd::optional<T> operator()(int argc, sqlite3_value **val, int argi) {
        if (argi >= argc || sqlite3_value_type(val[argi]) == SQLITE_NULL) {
            return nonstd::optional<T>();
        }

        return nonstd::optional<T>(from_sqlite<T>()(argc, val, argi));
    }
};

inline void to_sqlite(sqlite3_context *ctx, const char *str)
{
    if (str == nullptr) {
        sqlite3_result_null(ctx);
    } else {
        sqlite3_result_text(ctx, str, -1, SQLITE_STATIC);
    }
}

inline void to_sqlite(sqlite3_context *ctx, const std::string &str)
{
    sqlite3_result_text(ctx, str.c_str(), str.length(), SQLITE_TRANSIENT);
}

inline void to_sqlite(sqlite3_context *ctx, const string_fragment &sf)
{
    if (sf.is_valid()) {
        sqlite3_result_text(ctx,
                            &sf.sf_string[sf.sf_begin], sf.length(),
                            SQLITE_TRANSIENT);
    } else {
        sqlite3_result_null(ctx);
    }
}

inline void to_sqlite(sqlite3_context *ctx, bool val)
{
    sqlite3_result_int(ctx, val);
}

inline void to_sqlite(sqlite3_context *ctx, int64_t val)
{
    sqlite3_result_int64(ctx, val);
}

inline void to_sqlite(sqlite3_context *ctx, double val)
{
    sqlite3_result_double(ctx, val);
}

inline void to_sqlite(sqlite3_context *ctx, const json_string &val)
{
    sqlite3_result_text(ctx,
                        (const char *) val.js_content,
                        val.js_len,
                        free);
    sqlite3_result_subtype(ctx, JSON_SUBTYPE);
}

struct ToSqliteVisitor {
    ToSqliteVisitor(sqlite3_context *vctx) : tsv_context(vctx) {

    };

    template<typename T>
    void operator()(T t) const {
        to_sqlite(this->tsv_context, t);
    }

    sqlite3_context *tsv_context;
};

template<typename ... Types>
void to_sqlite(sqlite3_context *ctx, const mapbox::util::variant<Types...> &val)
{
    ToSqliteVisitor visitor(ctx);

    mapbox::util::apply_visitor(visitor, val);
}

template<typename ... Args>
struct optional_counter;

template<typename T>
struct optional_counter<nonstd::optional<T>> {
    constexpr static int value = 1;
};

template<typename T, typename ... Rest>
struct optional_counter<nonstd::optional<T>, Rest...> {
    constexpr static int value = 1 + sizeof...(Rest);
};

template<typename Arg>
struct optional_counter<Arg> {
    constexpr static int value = 0;
};

template<typename Arg1, typename ... Args>
struct optional_counter<Arg1, Args...> : optional_counter<Args...> {

};


template<typename F, F f> struct sqlite_func_adapter;

template<typename Return, typename ... Args, Return (*f)(Args...)>
struct sqlite_func_adapter<Return (*)(Args...), f> {
    constexpr static int OPT_COUNT = optional_counter<Args...>::value;
    constexpr static int REQ_COUNT = sizeof...(Args) - OPT_COUNT;

    template<size_t ... Idx>
    static void func2(sqlite3_context *context,
                      int argc, sqlite3_value **argv,
                      std::index_sequence<Idx...>) {
        try {
            Return retval = f(from_sqlite<Args>()(argc, argv, Idx)...);

            to_sqlite(context, retval);
        } catch (from_sqlite_conversion_error &e) {
            char buffer[64];

            snprintf(buffer, sizeof(buffer),
                     "Expecting an %s for argument number %d",
                     e.e_type,
                     e.e_argi);
            sqlite3_result_error(context, buffer, -1);
        } catch (const std::exception &e) {
            sqlite3_result_error(context, e.what(), -1);
        } catch (...) {
            sqlite3_result_error(context, "Function threw an unexpected exception", -1);
        }
    };

    static void func1(sqlite3_context *context,
                      int argc, sqlite3_value **argv) {
        if (argc < REQ_COUNT) {
            char buffer[128];

            if (OPT_COUNT == 0) {
                snprintf(buffer, sizeof(buffer),
                         "%s expects exactly %d argument%s",
                         "foo",
                         REQ_COUNT,
                         REQ_COUNT == 1 ? "s" : "");
            } else {
                snprintf(buffer, sizeof(buffer),
                         "%s expects between %d and %d arguments",
                         "bar",
                         REQ_COUNT,
                         OPT_COUNT);
            }
            sqlite3_result_error(context, buffer, -1);
            return;
        }

        for (int lpc = 0; lpc < REQ_COUNT; lpc++) {
            if (sqlite3_value_type(argv[lpc]) == SQLITE_NULL) {
                sqlite3_result_null(context);
                return;
            }
        }

        func2(context, argc, argv, std::make_index_sequence<sizeof...(Args)>{});
    };


    static FuncDef builder(const char name[],
                           const char description[],
                           std::initializer_list<FuncDef::ParamDoc> pdocs) {
        static FuncDef::ParamDoc PARAM_DOCS[sizeof...(Args) + 1];

        require(pdocs.size() == sizeof...(Args));

        std::copy(std::begin(pdocs), std::end(pdocs), std::begin(PARAM_DOCS));
        return {
            name,
            OPT_COUNT > 0 ? -1 : REQ_COUNT,
            0,
            SQLITE_UTF8 | SQLITE_DETERMINISTIC,
            0,
            func1,
            PARAM_DOCS,
            description,
        };
    };
};

extern std::string vtab_module_schemas;

class vtab_index_constraints {
public:
    vtab_index_constraints(const sqlite3_index_info *index_info)
        : vic_index_info(*index_info) {
    };

    struct const_iterator {
        const_iterator(vtab_index_constraints *parent, int index = 0)
            : i_parent(parent), i_index(index) {
            while (this->i_index < this->i_parent->vic_index_info.nConstraint &&
                   !this->i_parent->vic_index_info.aConstraint[this->i_index].usable) {
                this->i_index += 1;
            }
        };

        const_iterator& operator++() {
            do {
                this->i_index += 1;
            } while (
                this->i_index < this->i_parent->vic_index_info.nConstraint &&
                !this->i_parent->vic_index_info.aConstraint[this->i_index].usable);

            return *this;
        };

        const sqlite3_index_info::sqlite3_index_constraint &operator*() const {
            return this->i_parent->vic_index_info.aConstraint[this->i_index];
        };

        const sqlite3_index_info::sqlite3_index_constraint *operator->() const {
            return &this->i_parent->vic_index_info.aConstraint[this->i_index];
        };

        bool operator!=(const const_iterator &rhs) const {
            return this->i_parent != rhs.i_parent || this->i_index != rhs.i_index;
        };

        const vtab_index_constraints *i_parent;
        int i_index;
    };

    const_iterator begin() {
        return const_iterator(this);
    };

    const_iterator end() {
        return const_iterator(this, this->vic_index_info.nConstraint);
    };

private:
    const sqlite3_index_info &vic_index_info;
};

class vtab_index_usage {
public:
    vtab_index_usage(sqlite3_index_info *index_info)
        : viu_index_info(*index_info),
          viu_used_column_count(0),
          viu_max_column(0) {

    };

    void column_used(const vtab_index_constraints::const_iterator &iter) {
        this->viu_max_column = std::max(iter->iColumn, this->viu_max_column);
        this->viu_index_info.idxNum |= (1L << iter.i_index);
        this->viu_used_column_count += 1;
    };

    void allocate_args(int expected) {
        int n_arg = 0;

        if (this->viu_used_column_count != expected) {
            this->viu_index_info.estimatedCost = 2147483647;
            this->viu_index_info.estimatedRows = 2147483647;
            return;
        }

        for (int lpc = 0; lpc <= this->viu_max_column; lpc++) {
            for (int cons_index = 0;
                 cons_index < this->viu_index_info.nConstraint;
                 cons_index++) {
                if (this->viu_index_info.aConstraint[cons_index].iColumn != lpc) {
                    continue;
                }
                if (!(this->viu_index_info.idxNum & (1L << cons_index))) {
                    continue;
                }

                this->viu_index_info.aConstraintUsage[cons_index].argvIndex = ++n_arg;
            }
        }
        this->viu_index_info.estimatedCost = 1.0;
        this->viu_index_info.estimatedRows = 1;
    };

private:
    sqlite3_index_info &viu_index_info;
    int viu_used_column_count;
    int viu_max_column;
};

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
        return (obj.*func)(tab, rowid, from_sqlite<Args>()(sizeof...(Args), argv, Idx)...);
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

    template<typename U>
    auto addUpdate(U u) -> decltype(&U::delete_row, void()) {
        log_debug("has updates!");
        this->vm_module.xUpdate = tvt_update;
    };

    template<typename U>
    void addUpdate(...) {
        log_debug("no has updates!");
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
        this->addUpdate<T>(T());
    };

    int create(sqlite3 *db, const char *name) {
        std::string impl_name = name;
        vtab_module_schemas += T::CREATE_STMT;

        // XXX Eponymous tables don't seem to work in older sqlite versions
        impl_name += "_impl";
        int rc = sqlite3_create_module(db, impl_name.c_str(), &this->vm_module, NULL);
        ensure(rc == SQLITE_OK);
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
