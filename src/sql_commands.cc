/**
 * Copyright (c) 2021, Timothy Stack
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

#include "base/auto_mem.hh"
#include "base/fs_util.hh"
#include "base/injector.bind.hh"
#include "base/itertools.hh"
#include "base/lnav_log.hh"
#include "bound_tags.hh"
#include "command_executor.hh"
#include "config.h"
#include "readline_context.hh"
#include "shlex.hh"
#include "sqlite-extension-func.hh"
#include "sqlitepp.hh"
#include "view_helpers.hh"

static Result<std::string, lnav::console::user_message>
sql_cmd_dump(exec_context& ec,
             std::string cmdline,
             std::vector<std::string>& args)
{
    static auto& lnav_db = injector::get<auto_sqlite3&>();
    static auto& lnav_flags = injector::get<unsigned long&, lnav_flags_tag>();

    std::string retval;

    if (args.empty()) {
        args.emplace_back("filename");
        args.emplace_back("tables");
        return Ok(retval);
    }

    if (args.size() < 2) {
        return ec.make_error("expecting a file name to write to");
    }

    if (lnav_flags & LNF_SECURE_MODE) {
        return ec.make_error("{} -- unavailable in secure mode", args[0]);
    }

    auto_mem<FILE> file(fclose);

    if ((file = fopen(args[1].c_str(), "w+")) == nullptr) {
        return ec.make_error(
            "unable to open '{}' for writing: {}", args[1], strerror(errno));
    }

    for (size_t lpc = 2; lpc < args.size(); lpc++) {
        sqlite3_db_dump(lnav_db.in(),
                        "main",
                        args[lpc].c_str(),
                        (int (*)(const char*, void*)) fputs,
                        file.in());
    }

    retval = "generated";
    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
sql_cmd_read(exec_context& ec,
             std::string cmdline,
             std::vector<std::string>& args)
{
    static const intern_string_t SRC = intern_string::lookup("cmdline");
    static auto& lnav_db = injector::get<auto_sqlite3&>();
    static auto& lnav_flags = injector::get<unsigned long&, lnav_flags_tag>();

    std::string retval;

    if (args.empty()) {
        args.emplace_back("filename");
        return Ok(retval);
    }

    if (lnav_flags & LNF_SECURE_MODE) {
        return ec.make_error("{} -- unavailable in secure mode", args[0]);
    }

    shlex lexer(cmdline);

    auto split_args_res = lexer.split(ec.create_resolver());
    if (split_args_res.isErr()) {
        auto split_err = split_args_res.unwrapErr();
        auto um
            = lnav::console::user_message::error("unable to parse file name")
                  .with_reason(split_err.te_msg)
                  .with_snippet(lnav::console::snippet::from(
                      SRC, lexer.to_attr_line(split_err)));

        return Err(um);
    }

    auto split_args = split_args_res.unwrap()
        | lnav::itertools::map([](const auto& elem) { return elem.se_value; });

    for (size_t lpc = 1; lpc < split_args.size(); lpc++) {
        auto read_res = lnav::filesystem::read_file(split_args[lpc]);

        if (read_res.isErr()) {
            return ec.make_error("unable to read script file: {} -- {}",
                                 split_args[lpc],
                                 read_res.unwrapErr());
        }

        auto script = read_res.unwrap();
        auto_mem<sqlite3_stmt> stmt(sqlite3_finalize);
        const char* start = script.c_str();

        do {
            const char* tail;
            auto rc = sqlite3_prepare_v2(
                lnav_db.in(), start, -1, stmt.out(), &tail);

            if (rc != SQLITE_OK) {
                const char* errmsg = sqlite3_errmsg(lnav_db.in());

                return ec.make_error("{}", errmsg);
            }

            if (stmt.in() != nullptr) {
                std::string alt_msg;
                auto exec_res = execute_sql(
                    ec, std::string(start, tail - start), alt_msg);
                if (exec_res.isErr()) {
                    return exec_res;
                }
            }

            start = tail;
        } while (start[0]);
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
sql_cmd_schema(exec_context& ec,
               std::string cmdline,
               std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        return Ok(retval);
    }

    ensure_view(LNV_SCHEMA);

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
sql_cmd_msgformats(exec_context& ec,
                   std::string cmdline,
                   std::vector<std::string>& args)
{
    static const std::string MSG_FORMAT_STMT = R"(
SELECT count(*) AS total,
       min(log_line) AS log_line,
       min(log_time) AS log_time,
       humanize_duration(timediff(max(log_time), min(log_time))) AS duration,
       group_concat(DISTINCT log_format) AS log_formats,
       log_msg_format
    FROM all_logs
    WHERE log_msg_format != ''
    GROUP BY log_msg_format
    HAVING total > 1
    ORDER BY total DESC, log_line ASC
)";

    std::string retval;

    if (args.empty()) {
        return Ok(retval);
    }

    std::string alt;

    return execute_sql(ec, MSG_FORMAT_STMT, alt);
}

static Result<std::string, lnav::console::user_message>
sql_cmd_generic(exec_context& ec,
                std::string cmdline,
                std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("*");
        return Ok(retval);
    }

    return Ok(retval);
}

static readline_context::command_t sql_commands[] = {
    {
        ".dump",
        sql_cmd_dump,
        help_text(".dump", "Dump the contents of the database")
            .sql_command()
            .with_parameter({"path", "The path to the file to write"})
            .with_tags({
                "io",
            }),
    },
    {
        ".msgformats",
        sql_cmd_msgformats,
        help_text(".msgformats",
                  "Executes a query that will summarize the different message "
                  "formats found in the logs")
            .sql_command(),
    },
    {
        ".read",
        sql_cmd_read,
        help_text(".read", "Execute the SQLite statements in the given file")
            .sql_command()
            .with_parameter({"path", "The path to the file to write"})
            .with_tags({
                "io",
            }),
    },
    {
        ".schema",
        sql_cmd_schema,
        help_text(".schema",
                  "Switch to the SCHEMA view that contains a dump of the "
                  "current database schema")
            .sql_command(),
    },
    {
        "ATTACH",
        sql_cmd_generic,
    },
    {
        "CREATE",
        sql_cmd_generic,
    },
    {
        "DELETE",
        sql_cmd_generic,
    },
    {
        "DETACH",
        sql_cmd_generic,
    },
    {
        "DROP",
        sql_cmd_generic,
    },
    {
        "INSERT",
        sql_cmd_generic,
    },
    {
        "SELECT",
        sql_cmd_generic,
    },
    {
        "UPDATE",
        sql_cmd_generic,
    },
    {
        "WITH",
        sql_cmd_generic,
    },
};

static readline_context::command_map_t sql_cmd_map;

static auto bound_sql_cmd_map
    = injector::bind<readline_context::command_map_t,
                     sql_cmd_map_tag>::to_instance(+[]() {
          for (auto& cmd : sql_commands) {
              sql_cmd_map[cmd.c_name] = &cmd;
          }

          return &sql_cmd_map;
      });

namespace injector {
template<>
void
force_linking(sql_cmd_map_tag anno)
{
}
}  // namespace injector
