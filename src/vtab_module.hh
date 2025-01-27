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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef vtab_module_hh
#define vtab_module_hh

#include <exception>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <sqlite3.h>

#include "base/auto_mem.hh"
#include "base/intern_string.hh"
#include "base/lnav.console.hh"
#include "base/lnav_log.hh"
#include "base/string_util.hh"
#include "base/types.hh"
#include "fmt/format.h"
#include "help_text_formatter.hh"
#include "mapbox/variant.hpp"
#include "sqlite-extension-func.hh"

lnav::console::user_message sqlite3_error_to_user_message(sqlite3*);

struct from_sqlite_conversion_error : std::exception {
    from_sqlite_conversion_error(const char* type, int argi)
        : e_type(type), e_argi(argi)
    {
    }

    const char* e_type;
    int e_argi;
};

struct sqlite_func_error : std::exception {
    template<typename... Args>
    explicit sqlite_func_error(fmt::string_view format_str, const Args&... args)
        : e_what(fmt::vformat(format_str, fmt::make_format_args(args...)))
    {
    }

    const char* what() const noexcept override { return this->e_what.c_str(); }

    const std::string e_what;
};

namespace vtab_types {

template<typename T>
struct nullable {
    T* n_value{nullptr};
};

template<typename>
struct is_nullable : std::false_type {};

template<typename T>
struct is_nullable<nullable<T>> : std::true_type {};

}  // namespace vtab_types

template<typename T>
struct from_sqlite {
    using U = std::remove_reference_t<T>;

    U operator()(int argc, sqlite3_value** val, int argi) const { return U(); }
};

template<>
struct from_sqlite<bool> {
    bool operator()(int argc, sqlite3_value** val, int argi) const
    {
        if (sqlite3_value_numeric_type(val[argi]) != SQLITE_INTEGER) {
            throw from_sqlite_conversion_error("integer", argi);
        }

        return sqlite3_value_int64(val[argi]);
    }
};

template<>
struct from_sqlite<int64_t> {
    int64_t operator()(int argc, sqlite3_value** val, int argi) const
    {
        if (sqlite3_value_numeric_type(val[argi]) != SQLITE_INTEGER) {
            throw from_sqlite_conversion_error("integer", argi);
        }

        return sqlite3_value_int64(val[argi]);
    }
};

template<>
struct from_sqlite<sqlite3_value*> {
    sqlite3_value* operator()(int argc, sqlite3_value** val, int argi) const
    {
        return val[argi];
    }
};

template<>
struct from_sqlite<int> {
    int operator()(int argc, sqlite3_value** val, int argi) const
    {
        if (sqlite3_value_numeric_type(val[argi]) != SQLITE_INTEGER) {
            throw from_sqlite_conversion_error("integer", argi);
        }

        return sqlite3_value_int(val[argi]);
    }
};

template<>
struct from_sqlite<const char*> {
    const char* operator()(int argc, sqlite3_value** val, int argi) const
    {
        return (const char*) sqlite3_value_text(val[argi]);
    }
};

template<>
struct from_sqlite<string_fragment> {
    string_fragment operator()(int argc, sqlite3_value** val, int argi) const
    {
        const auto ptr = (const char*) sqlite3_value_blob(val[argi]);

        if (ptr == nullptr) {
            return string_fragment::invalid();
        }
        return string_fragment::from_bytes(ptr, sqlite3_value_bytes(val[argi]));
    }
};

template<>
struct from_sqlite<std::string> {
    std::string operator()(int argc, sqlite3_value** val, int argi) const
    {
        return {
            (const char*) sqlite3_value_blob(val[argi]),
            (size_t) sqlite3_value_bytes(val[argi]),
        };
    }
};

template<>
struct from_sqlite<double> {
    double operator()(int argc, sqlite3_value** val, int argi) const
    {
        return sqlite3_value_double(val[argi]);
    }
};

template<typename T>
struct from_sqlite<std::optional<T>> {
    std::optional<T> operator()(int argc, sqlite3_value** val, int argi) const
    {
        if (argi >= argc || sqlite3_value_type(val[argi]) == SQLITE_NULL) {
            return std::nullopt;
        }

        return std::optional<T>(from_sqlite<T>()(argc, val, argi));
    }
};

template<typename T>
struct from_sqlite<const std::vector<T>&> {
    std::vector<T> operator()(int argc, sqlite3_value** val, int argi) const
    {
        std::vector<T> retval;

        for (int lpc = argi; lpc < argc; lpc++) {
            retval.emplace_back(from_sqlite<T>()(argc, val, lpc));
        }

        return retval;
    }
};

template<typename T>
struct from_sqlite<vtab_types::nullable<T>> {
    vtab_types::nullable<T> operator()(int argc,
                                       sqlite3_value** val,
                                       int argi) const
    {
        return {from_sqlite<T*>()(argc, val, argi)};
    }
};

void to_sqlite(sqlite3_context* ctx, const lnav::console::user_message& um);

void set_vtable_errmsg(sqlite3_vtab* vtab,
                       const lnav::console::user_message& um);

inline void
to_sqlite(sqlite3_context* ctx, null_value_t)
{
    sqlite3_result_null(ctx);
}

inline void
to_sqlite(sqlite3_context* ctx, const char* str)
{
    if (str == nullptr) {
        sqlite3_result_null(ctx);
    } else {
        sqlite3_result_text(ctx, str, -1, SQLITE_STATIC);
    }
}

inline void
to_sqlite(sqlite3_context* ctx, text_auto_buffer buf)
{
    auto pair = buf.inner.release();
    sqlite3_result_text(ctx, pair.first, pair.second, free);
}

inline void
to_sqlite(sqlite3_context* ctx, blob_auto_buffer buf)
{
    auto pair = buf.inner.release();
    sqlite3_result_blob(ctx, pair.first, pair.second, free);
}

inline void
to_sqlite(sqlite3_context* ctx, const std::string& str)
{
    sqlite3_result_text(ctx, str.c_str(), str.length(), SQLITE_TRANSIENT);
}

inline void
to_sqlite(sqlite3_context* ctx, const string_fragment& sf)
{
    if (sf.is_valid()) {
        sqlite3_result_text(
            ctx, &sf.sf_string[sf.sf_begin], sf.length(), SQLITE_TRANSIENT);
    } else {
        sqlite3_result_null(ctx);
    }
}

inline void
to_sqlite(sqlite3_context* ctx, bool val)
{
    sqlite3_result_int(ctx, val);
}

template<typename T>
void
to_sqlite(
    sqlite3_context* ctx,
    T val,
    std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>* dummy
    = 0)
{
    sqlite3_result_int64(ctx, val);
}

inline void
to_sqlite(sqlite3_context* ctx, double val)
{
    sqlite3_result_double(ctx, val);
}

inline void
to_sqlite(sqlite3_context* ctx, auto_mem<char> str)
{
    const auto free_func = str.get_free_func<void (*)(void*)>();
    sqlite3_result_text(ctx, str.release(), -1, free_func);
}

#define JSON_SUBTYPE    74 /* Ascii for "J" */
#define FLATTEN_SUBTYPE 0x5f

template<typename T>
void
to_sqlite(sqlite3_context* ctx, std::optional<T>& val)
{
    if (val.has_value()) {
        to_sqlite(ctx, val.value());
    } else {
        sqlite3_result_null(ctx);
    }
}

template<typename T>
void
to_sqlite(sqlite3_context* ctx, std::optional<T> val)
{
    if (val.has_value()) {
        to_sqlite(ctx, std::move(val.value()));
    } else {
        sqlite3_result_null(ctx);
    }
}

struct ToSqliteVisitor {
    ToSqliteVisitor(sqlite3_context* vctx) : tsv_context(vctx) {}

    template<typename T>
    void operator()(T&& t) const
    {
        to_sqlite(this->tsv_context, std::move(t));
    }

    sqlite3_context* tsv_context;
};

template<typename... Types>
void
to_sqlite(sqlite3_context* ctx, mapbox::util::variant<Types...>&& val)
{
    ToSqliteVisitor visitor(ctx);

    mapbox::util::apply_visitor(visitor, val);
}

template<typename... Args>
struct optional_counter {
    constexpr static int value = 0;
};

template<typename T>
struct optional_counter<std::optional<T>> {
    constexpr static int value = 1;
};

template<typename T, typename U>
struct optional_counter<std::optional<T>, const std::vector<U>&> {
    constexpr static int value = 1;
};

template<typename T, typename... Rest>
struct optional_counter<std::optional<T>, Rest...> {
    constexpr static int value = 1 + sizeof...(Rest);
};

template<typename Arg>
struct optional_counter<Arg> {
    constexpr static int value = 0;
};

template<typename Arg1, typename... Args>
struct optional_counter<Arg1, Args...> : optional_counter<Args...> {};

template<typename... Args>
struct variadic_counter {
    constexpr static int value = 0;
};

template<typename T>
struct variadic_counter<const std::vector<T>&> {
    constexpr static int value = 1;
};

template<typename Arg>
struct variadic_counter<Arg> {
    constexpr static int value = 0;
};

template<typename Arg1, typename... Args>
struct variadic_counter<Arg1, Args...> : variadic_counter<Args...> {};

template<typename F, F f>
struct sqlite_func_adapter;

template<typename Return, typename... Args, Return (*f)(Args...)>
struct sqlite_func_adapter<Return (*)(Args...), f> {
    constexpr static size_t OPT_COUNT = optional_counter<Args...>::value;
    constexpr static size_t VAR_COUNT = variadic_counter<Args...>::value;
    constexpr static size_t REQ_COUNT = sizeof...(Args) - OPT_COUNT - VAR_COUNT;

    template<size_t... Idx>
    static void func2(sqlite3_context* context,
                      int argc,
                      sqlite3_value** argv,
                      std::index_sequence<Idx...>)
    {
        try {
            Return retval = f(from_sqlite<Args>()(argc, argv, Idx)...);

            to_sqlite(context, std::move(retval));
        } catch (const lnav::console::user_message& um) {
            to_sqlite(context, um);
        } catch (from_sqlite_conversion_error& e) {
            char buffer[256];

            snprintf(buffer,
                     sizeof(buffer),
                     "Expecting an %s for argument number %d",
                     e.e_type,
                     e.e_argi);
            sqlite3_result_error(context, buffer, -1);
        } catch (const std::exception& e) {
            const auto* fd = (const FuncDef*) sqlite3_user_data(context);
            attr_line_t error_al;
            error_al.append("call to ");
            format_help_text_for_term(
                fd->fd_help, 40, error_al, help_text_content::synopsis);
            error_al.append(" failed");
            auto um = lnav::console::user_message::error(error_al).with_reason(
                e.what());

            to_sqlite(context, um);
        } catch (...) {
            sqlite3_result_error(
                context, "Function threw an unexpected exception", -1);
        }
    }

    static void func1(sqlite3_context* context, int argc, sqlite3_value** argv)
    {
        if ((size_t) argc < REQ_COUNT && VAR_COUNT == 0) {
            const auto* fd = (const FuncDef*) sqlite3_user_data(context);
            char buffer[128];

            if (OPT_COUNT == 0) {
                snprintf(buffer,
                         sizeof(buffer),
                         "%s() expects exactly %ld argument%s",
                         fd->fd_help.ht_name,
                         REQ_COUNT,
                         REQ_COUNT == 1 ? "s" : "");
            } else {
                snprintf(buffer,
                         sizeof(buffer),
                         "%s() expects between %ld and %ld arguments",
                         fd->fd_help.ht_name,
                         REQ_COUNT,
                         REQ_COUNT + OPT_COUNT);
            }
            sqlite3_result_error(context, buffer, -1);
            return;
        }

        if constexpr (REQ_COUNT > 0) {
            const static bool IS_NULLABLE[]
                = {vtab_types::is_nullable<Args>::value...};
            const static bool IS_SQLITE3_VALUE[]
                = {std::is_same_v<Args, sqlite3_value*>...};

            for (size_t lpc = 0; lpc < REQ_COUNT; lpc++) {
                if (!IS_NULLABLE[lpc] && !IS_SQLITE3_VALUE[lpc]
                    && sqlite3_value_type(argv[lpc]) == SQLITE_NULL)
                {
                    sqlite3_result_null(context);
                    return;
                }
            }
        }

        func2(context, argc, argv, std::make_index_sequence<sizeof...(Args)>{});
    }

    static FuncDef builder(help_text ht)
    {
        require(ht.ht_parameters.size() == sizeof...(Args));

        return {
            ht.ht_name,
            (OPT_COUNT > 0 || VAR_COUNT > 0) ? -1 : (int) REQ_COUNT,
            SQLITE_UTF8 | SQLITE_DETERMINISTIC,
            0,
            func1,
            ht,
        };
    }
};

extern std::string vtab_module_schemas;
extern std::map<intern_string_t, std::string> vtab_module_ddls;

class vtab_index_constraints {
public:
    vtab_index_constraints(const sqlite3_index_info* index_info)
        : vic_index_info(*index_info)
    {
    }

    struct const_iterator {
        const_iterator(vtab_index_constraints* parent, int index = 0)
            : i_parent(parent), i_index(index)
        {
            while (this->i_index < this->i_parent->vic_index_info.nConstraint
                   && !this->i_parent->vic_index_info.aConstraint[this->i_index]
                           .usable)
            {
                this->i_index += 1;
            }
        }

        const_iterator& operator++()
        {
            do {
                this->i_index += 1;
            } while (
                this->i_index < this->i_parent->vic_index_info.nConstraint
                && !this->i_parent->vic_index_info.aConstraint[this->i_index]
                        .usable);

            return *this;
        }

        const sqlite3_index_info::sqlite3_index_constraint& operator*() const
        {
            return this->i_parent->vic_index_info.aConstraint[this->i_index];
        }

        const sqlite3_index_info::sqlite3_index_constraint* operator->() const
        {
            return &this->i_parent->vic_index_info.aConstraint[this->i_index];
        }

        bool operator!=(const const_iterator& rhs) const
        {
            return this->i_parent != rhs.i_parent
                || this->i_index != rhs.i_index;
        }

        const vtab_index_constraints* i_parent;
        int i_index;
    };

    const_iterator begin() { return {this}; }

    const_iterator end() { return {this, this->vic_index_info.nConstraint}; }

private:
    const sqlite3_index_info& vic_index_info;
};

class vtab_index_usage {
public:
    vtab_index_usage(sqlite3_index_info* index_info)
        : viu_index_info(*index_info)
    {
    }

    void column_used(const vtab_index_constraints::const_iterator& iter);

    void allocate_args(int low, int high, int required);

private:
    sqlite3_index_info& viu_index_info;
    int viu_used_column_count{0};
    int viu_min_column{INT_MAX};
    int viu_max_column{0};
};

struct vtab_module_base {
    virtual int create(sqlite3* db) = 0;

    virtual ~vtab_module_base() = default;
};

template<typename T>
struct vtab_module : public vtab_module_base {
    struct vtab {
        explicit vtab(sqlite3* db, T& impl) : v_db(db), v_impl(impl) {}

        explicit operator sqlite3_vtab*() { return &this->v_base; }

        sqlite3_vtab v_base{};
        sqlite3* v_db;
        T& v_impl;
    };

    static int tvt_create(sqlite3* db,
                          void* pAux,
                          int argc,
                          const char* const* argv,
                          sqlite3_vtab** pp_vt,
                          char** pzErr)
    {
        auto* mod = static_cast<vtab_module<T>*>(pAux);
        auto vt = new vtab(db, mod->vm_impl);

        *pp_vt = (sqlite3_vtab*) &vt->v_base;

        return sqlite3_declare_vtab(db, T::CREATE_STMT);
    }

    template<typename... Args, size_t... Idx>
    static int apply_impl(T& obj,
                          int (T::*func)(sqlite3_vtab*,
                                         sqlite3_int64&,
                                         Args...),
                          sqlite3_vtab* tab,
                          sqlite3_int64& rowid,
                          sqlite3_value** argv,
                          std::index_sequence<Idx...>)
    {
        return (obj.*func)(
            tab, rowid, from_sqlite<Args>()(sizeof...(Args), argv, Idx)...);
    }

    template<typename... Args>
    static int apply(T& obj,
                     int (T::*func)(sqlite3_vtab*, sqlite3_int64&, Args...),
                     sqlite3_vtab* tab,
                     sqlite3_int64& rowid,
                     int argc,
                     sqlite3_value** argv)
    {
        require(sizeof...(Args) == 0 || argc == sizeof...(Args));

        try {
            return apply_impl(obj,
                              func,
                              tab,
                              rowid,
                              argv,
                              std::make_index_sequence<sizeof...(Args)>{});
        } catch (const from_sqlite_conversion_error& e) {
            tab->zErrMsg = sqlite3_mprintf(
                "Expecting an %s for column number %d", e.e_type, e.e_argi);
            return SQLITE_ERROR;
        } catch (const std::exception& e) {
            tab->zErrMsg = sqlite3_mprintf("%s", e.what());
            return SQLITE_ERROR;
        } catch (...) {
            tab->zErrMsg
                = sqlite3_mprintf("Encountered an unexpected exception");
            return SQLITE_ERROR;
        }
    }

    static int tvt_destructor(sqlite3_vtab* p_svt)
    {
        vtab* vt = (vtab*) p_svt;

        delete vt;

        return SQLITE_OK;
    }

    static int tvt_open(sqlite3_vtab* p_svt, sqlite3_vtab_cursor** pp_cursor)
    {
        p_svt->zErrMsg = nullptr;

        auto* p_cur = new (typename T::cursor)(p_svt);
        if (p_cur == nullptr) {
            return SQLITE_NOMEM;
        }
        *pp_cursor = (sqlite3_vtab_cursor*) p_cur;

        return SQLITE_OK;
    }

    static int tvt_next(sqlite3_vtab_cursor* cur)
    {
        auto* p_cur = (typename T::cursor*) cur;

        return p_cur->next();
    }

    static int tvt_eof(sqlite3_vtab_cursor* cur)
    {
        auto* p_cur = (typename T::cursor*) cur;

        return p_cur->eof();
    }

    static int tvt_close(sqlite3_vtab_cursor* cur)
    {
        auto* p_cur = (typename T::cursor*) cur;

        delete p_cur;

        return SQLITE_OK;
    }

    static int tvt_rowid(sqlite3_vtab_cursor* cur, sqlite_int64* p_rowid)
    {
        auto* p_cur = (typename T::cursor*) cur;

        return p_cur->get_rowid(*p_rowid);
    }

    static int tvt_column(sqlite3_vtab_cursor* cur,
                          sqlite3_context* ctx,
                          int col)
    {
        auto* mod_vt = (typename vtab_module<T>::vtab*) cur->pVtab;
        auto* p_cur = (typename T::cursor*) cur;

        return mod_vt->v_impl.get_column(*p_cur, ctx, col);
    }

    static int vt_best_index(sqlite3_vtab* tab, sqlite3_index_info* p_info)
    {
        return SQLITE_OK;
    }

    static int vt_filter(sqlite3_vtab_cursor* p_vtc,
                         int idxNum,
                         const char* idxStr,
                         int argc,
                         sqlite3_value** argv)
    {
        auto* p_cur = (typename T::cursor*) p_vtc;

        return p_cur->reset();
    }

    static int tvt_update(sqlite3_vtab* tab,
                          int argc,
                          sqlite3_value** argv,
                          sqlite_int64* rowid)
    {
        auto* mod_vt = (typename vtab_module<T>::vtab*) tab;

        if (argc <= 1) {
            sqlite3_int64 rowid = sqlite3_value_int64(argv[0]);

            return mod_vt->v_impl.delete_row(tab, rowid);
        }

        if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
            sqlite3_int64* rowid2 = rowid;
            return vtab_module<T>::apply(mod_vt->v_impl,
                                         &T::insert_row,
                                         tab,
                                         *rowid2,
                                         argc - 2,
                                         argv + 2);
        }

        sqlite3_int64 index = sqlite3_value_int64(argv[0]);

        if (index != sqlite3_value_int64(argv[1])) {
            tab->zErrMsg = sqlite3_mprintf(
                "The rowids in the lnav_views table cannot be changed");
            return SQLITE_ERROR;
        }

        return vtab_module<T>::apply(
            mod_vt->v_impl, &T::update_row, tab, index, argc - 2, argv + 2);
    }

    template<typename U>
    auto addUpdate(U u) -> decltype(&U::delete_row, void())
    {
        this->vm_module.xUpdate = tvt_update;
    }

    template<typename U>
    void addUpdate(...)
    {
    }

    template<typename... Args>
    vtab_module(Args&... args) noexcept : vm_impl(args...)
    {
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
        this->addUpdate<T>(this->vm_impl);
    }

    ~vtab_module() override = default;

    int create(sqlite3* db, const char* name)
    {
        auto impl_name = std::string(name);
        vtab_module_schemas += T::CREATE_STMT;
        vtab_module_ddls[intern_string::lookup(name)] = trim(T::CREATE_STMT);

        // XXX Eponymous tables don't seem to work in older sqlite versions
        impl_name += "_impl";
        int rc = sqlite3_create_module(
            db, impl_name.c_str(), &this->vm_module, this);
        ensure(rc == SQLITE_OK);
        auto create_stmt = fmt::format(
            FMT_STRING("CREATE VIRTUAL TABLE {} USING {}()"), name, impl_name);
        return sqlite3_exec(db, create_stmt.c_str(), nullptr, nullptr, nullptr);
    }

    int create(sqlite3* db) override { return this->create(db, T::NAME); }

    sqlite3_module vm_module;
    T vm_impl;
};

template<typename T>
struct tvt_iterator_cursor {
    struct cursor {
        sqlite3_vtab_cursor base{};

        typename T::iterator iter;

        explicit cursor(sqlite3_vtab* vt)
        {
            auto* mod_vt = (typename vtab_module<T>::vtab*) vt;

            this->base.pVtab = vt;
            this->iter = mod_vt->v_impl.begin();
        }

        int reset()
        {
            this->iter = get_handler().begin();

            return SQLITE_OK;
        }

        int next()
        {
            if (this->iter != get_handler().end()) {
                ++this->iter;
            }

            return SQLITE_OK;
        }

        int eof() { return this->iter == get_handler().end(); }

        template<bool cond, typename U>
        using resolvedType = typename std::enable_if<cond, U>::type;

        template<typename U = int>
        resolvedType<
            std::is_same<std::random_access_iterator_tag,
                         typename std::iterator_traits<
                             typename T::iterator>::iterator_category>::value,
            U>
        get_rowid(sqlite_int64& rowid_out)
        {
            rowid_out = std::distance(get_handler().begin(), this->iter);

            return SQLITE_OK;
        }

        template<typename U = int>
        resolvedType<
            !std::is_same<std::random_access_iterator_tag,
                          typename std::iterator_traits<
                              typename T::iterator>::iterator_category>::value,
            U>
        get_rowid(sqlite_int64& rowid_out)
        {
            rowid_out = get_handler().get_rowid(this->iter);

            return SQLITE_OK;
        }

    protected:
        T& get_handler()
        {
            auto* mod_vt = (typename vtab_module<T>::vtab*) this->base.pVtab;

            return mod_vt->v_impl;
        }
    };
};

template<typename T>
struct tvt_no_update : public T {
    using T::T;

    int delete_row(sqlite3_vtab* vt, sqlite3_int64 rowid)
    {
        vt->zErrMsg = sqlite3_mprintf("Rows cannot be deleted from this table");
        return SQLITE_ERROR;
    }

    int insert_row(sqlite3_vtab* tab, sqlite3_int64& rowid_out)
    {
        tab->zErrMsg
            = sqlite3_mprintf("Rows cannot be inserted into this table");
        return SQLITE_ERROR;
    }

    int update_row(sqlite3_vtab* tab, sqlite3_int64& rowid_out)
    {
        tab->zErrMsg = sqlite3_mprintf("Rows cannot be updated in this table");
        return SQLITE_ERROR;
    }
};

#endif
