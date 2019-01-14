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
#include <vector>
#include <utility>

#include "optional.hpp"
#include "lnav_log.hh"
#include "lnav_util.hh"
#include "auto_mem.hh"
#include "yajl/api/yajl_gen.h"
#include "mapbox/variant.hpp"

#include "sqlite-extension-func.hh"

struct from_sqlite_conversion_error : std::exception {
    from_sqlite_conversion_error(const char *type, int argi)
        : e_type(type), e_argi(argi) {

    };

    const char *e_type;
    int e_argi;
};

struct sqlite_func_error : std::exception {
    sqlite_func_error(const char *fmt, ...) {
        char buffer[1024];
        va_list args;

        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);

        this->e_what = buffer;
    };

    const char *what() const noexcept {
        return this->e_what.c_str();
    }

    std::string e_what;
};

template<typename T>
struct from_sqlite {
    inline T operator()(int argc, sqlite3_value **val, int argi) {
        return T();
    };
};

template<>
struct from_sqlite<bool> {
    inline bool operator()(int argc, sqlite3_value **val, int argi) {
        if (sqlite3_value_numeric_type(val[argi]) != SQLITE_INTEGER) {
            throw from_sqlite_conversion_error("integer", argi);
        }

        return sqlite3_value_int64(val[argi]);
    }
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
struct from_sqlite<sqlite3_value *> {
    inline sqlite3_value *operator()(int argc, sqlite3_value **val, int argi) {
        return val[argi];
    }
};

template<>
struct from_sqlite<int> {
    inline int operator()(int argc, sqlite3_value **val, int argi) {
        if (sqlite3_value_numeric_type(val[argi]) != SQLITE_INTEGER) {
            throw from_sqlite_conversion_error("integer", argi);
        }

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
struct from_sqlite<const std::string &> {
    inline const std::string operator()(int argc, sqlite3_value **val, int argi) {
        return std::string((const char *) sqlite3_value_text(val[argi]));
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
            return nonstd::nullopt;
        }

        return nonstd::optional<T>(from_sqlite<T>()(argc, val, argi));
    }
};

template<typename T>
struct from_sqlite<const std::vector<T> &> {
    inline std::vector<T> operator()(int argc, sqlite3_value **val, int argi) {
        std::vector<T> retval;

        for (int lpc = argi; lpc < argc; lpc++) {
            retval.emplace_back(from_sqlite<T>()(argc, val, lpc));
        }

        return retval;
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

inline void to_sqlite(sqlite3_context *ctx, int val)
{
    sqlite3_result_int64(ctx, val);
}

inline void to_sqlite(sqlite3_context *ctx, double val)
{
    sqlite3_result_double(ctx, val);
}

#define JSON_SUBTYPE  74    /* Ascii for "J" */

struct json_string {
    json_string(yajl_gen_t *gen) {
        const unsigned char *buf;

        yajl_gen_get_buf(gen, &buf, &this->js_len);

        this->js_content = (const unsigned char *) malloc(this->js_len);
        memcpy((void *) this->js_content.in(), buf, this->js_len);
    };

    auto_mem<const unsigned char> js_content;
    size_t js_len;
};

inline void to_sqlite(sqlite3_context *ctx, json_string &val)
{
    sqlite3_result_text(ctx,
                        (const char *) val.js_content.release(),
                        val.js_len,
                        free);
    sqlite3_result_subtype(ctx, JSON_SUBTYPE);
}

template<typename T>
inline void to_sqlite(sqlite3_context *ctx, const nonstd::optional<T> &val)
{
    if (val.has_value()) {
        to_sqlite(ctx, val.value());
    } else {
        sqlite3_result_null(ctx);
    }
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
void to_sqlite(sqlite3_context *ctx, mapbox::util::variant<Types...> &val)
{
    ToSqliteVisitor visitor(ctx);

    mapbox::util::apply_visitor(visitor, val);
}

template<typename ... Args>
struct optional_counter {
    constexpr static int value = 0;
};

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


template<typename ... Args>
struct variadic_counter {
    constexpr static int value = 0;
};

template<typename T>
struct variadic_counter<const std::vector<T> &> {
    constexpr static int value = 1;
};

template<typename Arg>
struct variadic_counter<Arg> {
    constexpr static int value = 0;
};

template<typename Arg1, typename ... Args>
struct variadic_counter<Arg1, Args...> : variadic_counter<Args...> {

};


template<typename F, F f> struct sqlite_func_adapter;

template<typename Return, typename ... Args, Return (*f)(Args...)>
struct sqlite_func_adapter<Return (*)(Args...), f> {
    constexpr static int OPT_COUNT = optional_counter<Args...>::value;
    constexpr static int VAR_COUNT = variadic_counter<Args...>::value;
    constexpr static int REQ_COUNT = sizeof...(Args) - OPT_COUNT - VAR_COUNT;

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
        if (argc < REQ_COUNT && VAR_COUNT == 0) {
            const struct FuncDef *fd = (const FuncDef *) sqlite3_user_data(context);
            char buffer[128];

            if (OPT_COUNT == 0) {
                snprintf(buffer, sizeof(buffer),
                         "%s() expects exactly %d argument%s",
                         fd->fd_help.ht_name,
                         REQ_COUNT,
                         REQ_COUNT == 1 ? "s" : "");
            } else {
                snprintf(buffer, sizeof(buffer),
                         "%s() expects between %d and %d arguments",
                         fd->fd_help.ht_name,
                         REQ_COUNT,
                         REQ_COUNT + OPT_COUNT);
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

    static FuncDef builder(help_text ht) {
        require(ht.ht_parameters.size() == sizeof...(Args));

        return {
            ht.ht_name,
            (OPT_COUNT > 0 || VAR_COUNT > 0) ? -1 : REQ_COUNT,
            SQLITE_UTF8 | SQLITE_DETERMINISTIC,
            0,
            func1,
            ht,
        };
    };
};

extern std::string vtab_module_schemas;
extern std::map<intern_string_t, std::string> vtab_module_ddls;

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

        *pp_vt = (sqlite3_vtab *) vt;

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

        try {
            return apply_impl(obj,
                              func,
                              tab,
                              rowid,
                              argv,
                              std::make_index_sequence<sizeof...(Args)>{});
        } catch (from_sqlite_conversion_error &e) {
            tab->zErrMsg = sqlite3_mprintf(
                "Expecting an %s for column number %d",
                e.e_type,
                e.e_argi);
            return SQLITE_ERROR;
        } catch (const std::exception &e) {
            tab->zErrMsg = sqlite3_mprintf("%s", e.what());
            return SQLITE_ERROR;
        } catch (...) {
            tab->zErrMsg = sqlite3_mprintf("Encountered an unexpected exception");
            return SQLITE_ERROR;
        }
    }

    static int tvt_destructor(sqlite3_vtab *p_svt)
    {
        return SQLITE_OK;
    }

    static int tvt_open(sqlite3_vtab *p_svt, sqlite3_vtab_cursor **pp_cursor)
    {
        p_svt->zErrMsg = nullptr;

        auto *p_cur = new (typename T::cursor)(p_svt);

        if (p_cur == NULL) {
            return SQLITE_NOMEM;
        } else {
            *pp_cursor = (sqlite3_vtab_cursor *) p_cur;
        }

        return SQLITE_OK;
    }

    static int tvt_next(sqlite3_vtab_cursor *cur)
    {
        auto *p_cur = (typename T::cursor *) cur;

        return p_cur->next();
    }

    static int tvt_eof(sqlite3_vtab_cursor *cur)
    {
        auto *p_cur = (typename T::cursor *) cur;

        return p_cur->eof();
    }

    static int tvt_close(sqlite3_vtab_cursor *cur)
    {
        auto *p_cur = (typename T::cursor *) cur;

        delete p_cur;

        return SQLITE_OK;
    }


    static int tvt_rowid(sqlite3_vtab_cursor *cur, sqlite_int64 *p_rowid) {
        auto *p_cur = (typename T::cursor *) cur;

        return p_cur->get_rowid(*p_rowid);
    };

    static int tvt_column(sqlite3_vtab_cursor *cur, sqlite3_context *ctx, int col) {
        auto *p_cur = (typename T::cursor *) cur;
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
        this->vm_module.xUpdate = tvt_update;
    };

    template<typename U>
    void addUpdate(...) {
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
        vtab_module_ddls[intern_string::lookup(name)] = trim(T::CREATE_STMT);

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

        cursor(sqlite3_vtab *vt)
        {
            T handler;

            this->base.pVtab = vt;
            this->iter = handler.begin();
        };

        int next()
        {
            T handler;

            if (this->iter != handler.end()) {
                ++this->iter;
            }

            return SQLITE_OK;
        };

        int eof()
        {
            T handler;

            return this->iter == handler.end();
        };

        template< bool cond, typename U >
        using resolvedType  = typename std::enable_if< cond, U >::type;

        template< typename U = int >
        resolvedType< std::is_same<std::random_access_iterator_tag,
            typename std::iterator_traits<typename T::iterator>::iterator_category>::value, U >
        get_rowid(sqlite_int64 &rowid_out) {
            T handler;

            rowid_out = std::distance(handler.begin(), this->iter);

            return SQLITE_OK;
        }

        template< typename U = int >
        resolvedType< !std::is_same<std::random_access_iterator_tag,
            typename std::iterator_traits<typename T::iterator>::iterator_category>::value, U >
        get_rowid(sqlite_int64 &rowid_out) {
            T handler;

            rowid_out = handler.get_rowid(this->iter);

            return SQLITE_OK;
        }
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
