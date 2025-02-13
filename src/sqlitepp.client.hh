/**
 * Copyright (c) 2022, Timothy Stack
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

#ifndef lnav_sqlitepp_client_hh
#define lnav_sqlitepp_client_hh

#include <sqlite3.h>

#include "base/auto_mem.hh"
#include "base/intern_string.hh"
#include "base/lnav_log.hh"
#include "sql_util.hh"
#include "vtab_module.hh"

inline int
bind_to_sqlite(sqlite3_stmt* stmt, int index, const struct timeval& tv)
{
    char timestamp[64];

    sql_strftime(timestamp, sizeof(timestamp), tv, 'T');

    return sqlite3_bind_text(stmt, index, timestamp, -1, SQLITE_TRANSIENT);
}

inline int
bind_to_sqlite(sqlite3_stmt* stmt, int index, const std::chrono::system_clock::time_point& tp)
{
    auto epoch_ts = tp.time_since_epoch().count();

    return sqlite3_bind_int64(stmt, index, epoch_ts);
}

inline int
bind_to_sqlite(sqlite3_stmt* stmt, int index, const char* str)
{
    return sqlite3_bind_text(stmt, index, str, -1, SQLITE_TRANSIENT);
}

inline int
bind_to_sqlite(sqlite3_stmt* stmt, int index, intern_string_t ist)
{
    return sqlite3_bind_text(
        stmt, index, ist.get(), ist.size(), SQLITE_TRANSIENT);
}

inline int
bind_to_sqlite(sqlite3_stmt* stmt, int index, string_fragment sf)
{
    return sqlite3_bind_text(
        stmt, index, sf.data(), sf.length(), SQLITE_TRANSIENT);
}

inline int
bind_to_sqlite(sqlite3_stmt* stmt, int index, const std::string& str)
{
    return sqlite3_bind_text(
        stmt, index, str.c_str(), str.size(), SQLITE_TRANSIENT);
}

inline int
bind_to_sqlite(sqlite3_stmt* stmt, int index, int64_t i)
{
    return sqlite3_bind_int64(stmt, index, i);
}

template<typename... Args, std::size_t... Idx>
int
bind_values_helper(sqlite3_stmt* stmt,
                   std::index_sequence<Idx...> idxs,
                   Args... args)
{
    int rcs[] = {bind_to_sqlite(stmt, Idx + 1, args)...};

    for (size_t lpc = 0; lpc < idxs.size(); lpc++) {
        if (rcs[lpc] != SQLITE_OK) {
            log_error("Failed to bind column %d in statement: %s -- %s",
                      lpc,
                      sqlite3_sql(stmt),
                      sqlite3_errstr(rcs[lpc]));
            return rcs[lpc];
        }
    }

    return SQLITE_OK;
}

template<typename... Args>
int
bind_values(sqlite3_stmt* stmt, Args... args)
{
    return bind_values_helper(
        stmt, std::make_index_sequence<sizeof...(Args)>(), args...);
}

struct prepared_stmt {
    explicit prepared_stmt(auto_mem<sqlite3_stmt> stmt)
        : ps_stmt(std::move(stmt))
    {
    }

    Result<void, std::string> execute()
    {
        auto rc = sqlite3_reset(this->ps_stmt.in());
        if (rc != SQLITE_OK) {
            return Err(std::string(
                sqlite3_errmsg(sqlite3_db_handle(this->ps_stmt.in()))));
        }

        rc = sqlite3_step(this->ps_stmt.in());
        if (rc == SQLITE_OK || rc == SQLITE_DONE) {
            return Ok();
        }

        auto msg = std::string(
            sqlite3_errmsg(sqlite3_db_handle(this->ps_stmt.in())));
        return Err(msg);
    }

    struct end_of_rows {};
    struct fetch_error {
        std::string fe_msg;
    };

    void reset() { sqlite3_reset(this->ps_stmt.in()); }

    template<typename T>
    using fetch_result = mapbox::util::variant<T, end_of_rows, fetch_error>;

    template<typename T>
    fetch_result<T> fetch_row()
    {
        auto rc = sqlite3_step(this->ps_stmt.in());
        if (rc == SQLITE_OK || rc == SQLITE_DONE) {
            return end_of_rows{};
        }

        if (rc == SQLITE_ROW) {
            const auto argc = sqlite3_column_count(this->ps_stmt.in());
            sqlite3_value* argv[argc];

            for (int lpc = 0; lpc < argc; lpc++) {
                argv[lpc] = sqlite3_column_value(this->ps_stmt.in(), lpc);
            }

            return from_sqlite<T>()(argc, argv, 0);
        }

        return fetch_error{
            sqlite3_errmsg(sqlite3_db_handle(this->ps_stmt.in())),
        };
    }

    template<typename T, typename F>
    Result<void, fetch_error> for_each_row(F func)
    {
        std::optional<fetch_error> err;
        auto done = false;

        while (!done) {
            done = this->template fetch_row<T>().match(
                func,
                [](end_of_rows) { return true; },
                [&err](const fetch_error& fe) {
                    err = fe;
                    return true;
                });
        }

        if (err) {
            return Err(err.value());
        }

        return Ok();
    }

    auto_mem<sqlite3_stmt> ps_stmt;
};

template<typename... Args>
static Result<prepared_stmt, std::string>
prepare_stmt(sqlite3* db, const char* sql, Args... args)
{
    auto_mem<sqlite3_stmt> retval(sqlite3_finalize);

    if (sqlite3_prepare_v2(db, sql, -1, retval.out(), nullptr) != SQLITE_OK) {
        return Err(
            fmt::format(FMT_STRING("unable to prepare SQL statement: {}"),
                        sqlite3_errmsg(db)));
    }

    if (sizeof...(args) > 0) {
        if (bind_values(retval.in(), args...) != SQLITE_OK) {
            return Err(
                fmt::format(FMT_STRING("unable to prepare SQL statement: {}"),
                            sqlite3_errmsg(db)));
        }
    }

    return Ok(prepared_stmt{
        std::move(retval),
    });
}

#endif
