/**
 * Copyright (c) 2025, Timothy Stack
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
#include <vector>

#include "textinput.history.hh"

#include "base/fs_util.hh"
#include "base/lnav_log.hh"
#include "base/paths.hh"
#include "lnav.hh"
#include "sql_execute.hh"
#include "sql_util.hh"
#include "sqlite-extension-func.hh"
#include "sqlitepp.client.hh"

int register_collation_functions(sqlite3* db);

template<>
struct from_sqlite<lnav::textinput::history::timestamp_t> {
    lnav::textinput::history::timestamp_t operator()(int argc,
                                                     sqlite3_value** val,
                                                     int argi) const
    {
        if (sqlite3_value_numeric_type(val[argi]) != SQLITE_INTEGER) {
            throw from_sqlite_conversion_error("integer", argi);
        }

        auto us = sqlite3_value_int64(val[argi]);
        auto duration = std::chrono::microseconds{us};
        return lnav::textinput::history::timestamp_t{duration};
    }
};

template<>
struct from_sqlite<log_level_t> {
    log_level_t operator()(int argc, sqlite3_value** val, int argi) const
    {
        const auto* level_text = (const char*) sqlite3_value_text(val[argi]);

        return string2level(level_text);
    }
};

template<>
struct from_sqlite<lnav::textinput::history::entry> {
    lnav::textinput::history::entry operator()(int argc,
                                               sqlite3_value** argv,
                                               int argi)
    {
        return {
            from_sqlite<std::string>()(argc, argv, argi + 0),
            from_sqlite<lnav::textinput::history::timestamp_t>()(
                argc, argv, argi + 1),
            from_sqlite<std::optional<lnav::textinput::history::timestamp_t>>()(
                argc, argv, argi + 2),
            from_sqlite<std::string>()(argc, argv, argi + 3),
            from_sqlite<log_level_t>()(argc, argv, argi + 4),
        };
    }
};

namespace lnav::textinput {

constexpr auto HISTORY_FILE_NAME = "textinput-history.db";

constexpr auto HISTORY_DDL = R"(

CREATE TABLE IF NOT EXISTS lnav_history (
    context TEXT NOT NULL,
    session_id TEXT NOT NULL,
    create_time_us INTEGER NOT NULL,
    end_time_us INTEGER DEFAULT NULL,
    content TEXT NOT NULL,
    status TEXT COLLATE loglevel NOT NULL DEFAULT 'info',

    CHECK(
        context <> '' AND
        session_id <> '' AND
        create_time_us > 0 AND
        end_time_us > 0 AND
        content <> '' AND
        status IN ('info', 'warning', 'error')
    )
);

CREATE INDEX IF NOT EXISTS idx_lnav_history_create_time ON lnav_history(create_time_us);
CREATE INDEX IF NOT EXISTS idx_lnav_history_content ON lnav_history(content);

DROP TRIGGER IF EXISTS lnav_history_cleanup;
CREATE TRIGGER lnav_history_cleanup AFTER INSERT ON lnav_history
BEGIN
    DELETE FROM lnav_history WHERE rowid <= NEW.rowid - 1000;
END;

)";

static auto_sqlite3
create_db()
{
    auto_sqlite3 retval;
    auto db_path = lnav::paths::dotlnav() / HISTORY_FILE_NAME;
    auto rc = sqlite3_open(db_path.c_str(), retval.out());
    if (rc != SQLITE_OK) {
        if (retval.in() != nullptr) {
            log_error("unable to open history DB: %s -- %s",
                      db_path.c_str(),
                      sqlite3_errmsg(retval.in()));
        }
        sqlite3_open(":memory:", retval.out());
    }

    register_sqlite_funcs(retval.in(), sqlite_registration_funcs);
    register_collation_functions(retval.in());

    std::vector<lnav::console::user_message> errors;
    sql_execute_script(retval.in(), {}, "internal", HISTORY_DDL, errors);
    ensure(errors.empty());

    return retval;
}

static auto_sqlite3&
get_db()
{
    thread_local auto retval = create_db();

    return retval;
}

constexpr auto INSERT_OP = R"(
INSERT INTO lnav_history
      (context, session_id, create_time_us, end_time_us, content, status)
    VALUES (?, ?, ?, ?, ?, ?)
)";

static const auto DEFAULT_SESSION_ID = std::string("-");

history::op_guard::~op_guard()
{
    if (!this->og_guard_helper || this->og_context.empty()) {
        return;
    }

    const auto& session_id = lnav_data.ld_session_id.empty()
        ? DEFAULT_SESSION_ID
        : lnav_data.ld_session_id.begin()->first;

    auto now = std::chrono::system_clock::now();
    auto prep_res = prepare_stmt(get_db().in(),
                                 INSERT_OP,
                                 this->og_context,
                                 session_id,
                                 this->og_start_time,
                                 now,
                                 this->og_content,
                                 level_names[this->og_status]);
    if (prep_res.isErr()) {
        log_error("unable to prepare INSERT_OP: %s",
                  prep_res.unwrapErr().c_str());
        return;
    }

    auto stmt = prep_res.unwrap();
    auto exec_res = stmt.execute();
    if (exec_res.isErr()) {
        log_error("failed to execute INSERT_OP: %s",
                  exec_res.unwrapErr().c_str());
    }
}

history
history::for_context(string_fragment name)
{
    history retval;

    retval.h_context = name;

    return retval;
}

constexpr auto INSERT_PLAIN = R"(
INSERT INTO lnav_history (context, session_id, create_time_us, content)
    VALUES (?, ?, ?, ?)
)";

void
history::insert_plain_content(string_fragment content)
{
    const auto& session_id = lnav_data.ld_session_id.empty()
        ? DEFAULT_SESSION_ID
        : lnav_data.ld_session_id.begin()->first;

    auto now = std::chrono::system_clock::now();
    auto stmt_res = prepare_stmt(
        get_db().in(), INSERT_PLAIN, this->h_context, session_id, now, content);
    if (stmt_res.isErr()) {
        log_error("unable to prepare plain history content insert: %s",
                  stmt_res.unwrapErr().c_str());
        return;
    }

    auto stmt = stmt_res.unwrap();
    auto exec_res = stmt.execute();
    if (exec_res.isErr()) {
        log_error("unable to insert plain history content: %s",
                  exec_res.unwrapErr().c_str());
    }
}

constexpr auto FUZZY_QUERY = R"(
SELECT * FROM (
  SELECT
      session_id,
      max(create_time_us) as max_create_time,
      NULL,
      content,
      status
    FROM lnav_history
    WHERE
      context = ?1 AND fuzzy_match(?2, content) > 0
    GROUP BY content
    ORDER BY fuzzy_match(?2, content) DESC, max_create_time DESC
    LIMIT 50
)
ORDER BY max_create_time DESC
)";

void
history::query_entries(string_fragment str, entry_handler_t handler)
{
    auto stmt_res
        = prepare_stmt(get_db().in(), FUZZY_QUERY, this->h_context, str);

    if (stmt_res.isErr()) {
        log_error("failed to query history: %s", stmt_res.unwrapErr().c_str());
        return;
    }
    auto stmt = stmt_res.unwrap();
    stmt.for_each_row<entry>([&](const entry& row) {
        handler(row);
        return false;
    });
}

}  // namespace lnav::textinput
