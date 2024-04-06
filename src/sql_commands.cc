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
#include "lnav.hh"
#include "readline_context.hh"
#include "shlex.hh"
#include "sql_help.hh"
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

static Result<std::string, lnav::console::user_message>
prql_cmd_from(exec_context& ec,
              std::string cmdline,
              std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("prql-table");
        return Ok(retval);
    }

    return Ok(retval);
}

static readline_context::prompt_result_t
prql_cmd_from_prompt(exec_context& ec, const std::string& cmdline)
{
    if (!endswith(cmdline, "from ")) {
        return {};
    }

    auto* tc = *lnav_data.ld_view_stack.top();
    auto* lss = dynamic_cast<logfile_sub_source*>(tc->get_sub_source());

    if (lss == nullptr || lss->text_line_count() == 0) {
        return {};
    }

    auto line_pair = lss->find_line_with_file(lss->at(tc->get_selection()));
    if (!line_pair) {
        return {};
    }

    auto format_name
        = line_pair->first->get_format_ptr()->get_name().to_string();
    return {
        "",
        lnav::prql::quote_ident(format_name),
    };
}

static Result<std::string, lnav::console::user_message>
prql_cmd_aggregate(exec_context& ec,
                   std::string cmdline,
                   std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("prql-expr");
        return Ok(retval);
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
prql_cmd_append(exec_context& ec,
                std::string cmdline,
                std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("prql-table");
        return Ok(retval);
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
prql_cmd_derive(exec_context& ec,
                std::string cmdline,
                std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("prql-expr");
        return Ok(retval);
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
prql_cmd_filter(exec_context& ec,
                std::string cmdline,
                std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("prql-expr");
        return Ok(retval);
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
prql_cmd_group(exec_context& ec,
               std::string cmdline,
               std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("prql-expr");
        args.emplace_back("prql-source");
        return Ok(retval);
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
prql_cmd_join(exec_context& ec,
              std::string cmdline,
              std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("prql-table");
        args.emplace_back("prql-expr");
        return Ok(retval);
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
prql_cmd_select(exec_context& ec,
                std::string cmdline,
                std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("prql-expr");
        return Ok(retval);
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
prql_cmd_sort(exec_context& ec,
              std::string cmdline,
              std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("prql-expr");
        return Ok(retval);
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
prql_cmd_take(exec_context& ec,
              std::string cmdline,
              std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
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
    {
        "from",
        prql_cmd_from,
        help_text("from")
            .prql_transform()
            .with_tags({"prql"})
            .with_summary("PRQL command to specify a data source")
            .with_parameter({"table", "The table to use as a source"})
            .with_example({
                "To pull data from the 'http_status_codes' database table",
                "from http_status_codes | take 3",
                help_example::language::prql,
            })
            .with_example({
                "To use an array literal as a source",
                "from [{ col1=1, col2='abc' }, { col1=2, col2='def' }]",
                help_example::language::prql,
            }),
        prql_cmd_from_prompt,
        "prql-source",
    },
    {
        "aggregate",
        prql_cmd_aggregate,
        help_text("aggregate")
            .prql_transform()
            .with_tags({"prql"})
            .with_summary("PRQL transform to summarize many rows into one")
            .with_parameter(
                help_text{"expr", "The aggregate expression(s)"}.with_grouping(
                    "{", "}"))
            .with_example({
                "To group values into a JSON array",
                "from [{a=1}, {a=2}] | aggregate { arr = json.group_array a }",
                help_example::language::prql,
            }),
        nullptr,
        "prql-source",
        {"prql-source"},
    },
    {
        "append",
        prql_cmd_append,
        help_text("append")
            .prql_transform()
            .with_tags({"prql"})
            .with_summary("PRQL transform to concatenate tables together")
            .with_parameter({"table", "The table to use as a source"}),
        nullptr,
        "prql-source",
        {"prql-source"},
    },
    {
        "derive",
        prql_cmd_derive,
        help_text("derive")
            .prql_transform()
            .with_tags({"prql"})
            .with_summary("PRQL transform to derive one or more columns")
            .with_parameter(
                help_text{"column", "The new column"}.with_grouping("{", "}"))
            .with_example({
                "To add a column that is a multiplication of another",
                "from [{a=1}, {a=2}] | derive b = a * 2",
                help_example::language::prql,
            }),
        nullptr,
        "prql-source",
        {"prql-source"},
    },
    {
        "filter",
        prql_cmd_filter,
        help_text("filter")
            .prql_transform()
            .with_tags({"prql"})
            .with_summary("PRQL transform to pick rows based on their values")
            .with_parameter(
                {"expr", "The expression to evaluate over each row"})
            .with_example({
                "To pick rows where 'a' is greater than one",
                "from [{a=1}, {a=2}] | filter a > 1",
                help_example::language::prql,
            }),
        nullptr,
        "prql-source",
        {"prql-source"},
    },
    {
        "group",
        prql_cmd_group,
        help_text("group")
            .prql_transform()
            .with_tags({"prql"})
            .with_summary("PRQL transform to partition rows into groups")
            .with_parameter(
                help_text{"key_columns", "The columns that define the group"}
                    .with_grouping("{", "}"))
            .with_parameter(
                help_text{"pipeline", "The pipeline to execute over a group"}
                    .with_grouping("(", ")"))
            .with_example({
                "To group by log_level and count the rows in each partition",
                "from lnav_example_log | group { log_level } (aggregate { "
                "count this })",
                help_example::language::prql,
            }),
        nullptr,
        "prql-source",
        {"prql-source"},
    },
    {
        "join",
        prql_cmd_join,
        help_text("join")
            .prql_transform()
            .with_tags({"prql"})
            .with_summary("PRQL transform to add columns from another table")
            .with_parameter(
                help_text{"side", "Specifies which rows to include"}
                    .with_enum_values({"inner", "left", "right", "full"})
                    .with_default_value("inner")
                    .optional())
            .with_parameter(
                {"table", "The other table to join with the current rows"})
            .with_parameter(
                help_text{"condition", "The condition used to join rows"}
                    .with_grouping("(", ")")),
        nullptr,
        "prql-source",
        {"prql-source"},
    },
    {
        "select",
        prql_cmd_select,
        help_text("select")
            .prql_transform()
            .with_tags({"prql"})
            .with_summary("PRQL transform to pick and compute columns")
            .with_parameter(
                help_text{"expr", "The columns to include in the result set"}
                    .with_grouping("{", "}"))
            .with_example({
                "To pick the 'b' column from the rows",
                "from [{a=1, b='abc'}, {a=2, b='def'}] | select b",
                help_example::language::prql,
            })
            .with_example({
                "To compute a new column from an input",
                "from [{a=1}, {a=2}] | select b = a * 2",
                help_example::language::prql,
            }),
        nullptr,
        "prql-source",
        {"prql-source"},
    },
    {
        "stats.average_of",
        prql_cmd_sort,
        help_text("stats.average_of", "Compute the average of col")
            .prql_function()
            .with_tags({"prql"})
            .with_parameter(help_text{"col", "The column to average"})
            .with_example({
                "To get the average of a",
                "from [{a=1}, {a=1}, {a=2}] | stats.average_of a",
                help_example::language::prql,
            }),
        nullptr,
        "prql-source",
        {"prql-source"},
    },
    {
        "stats.count_by",
        prql_cmd_sort,
        help_text(
            "stats.count_by",
            "Partition rows and count the number of rows in each partition")
            .prql_function()
            .with_tags({"prql"})
            .with_parameter(help_text{"column", "The columns to group by"}
                                .one_or_more()
                                .with_grouping("{", "}"))
            .with_example({
                "To count rows for a particular value of column 'a'",
                "from [{a=1}, {a=1}, {a=2}] | stats.count_by a",
                help_example::language::prql,
            }),
        nullptr,
        "prql-source",
        {"prql-source"},
    },
    {
        "stats.hist",
        prql_cmd_sort,
        help_text("stats.hist", "Count values per bucket of time")
            .prql_function()
            .with_tags({"prql"})
            .with_parameter(help_text{"col", "The column to count"})
            .with_parameter(help_text{"slice", "The time slice"}
                                .optional()
                                .with_default_value("'5m'"))
            .with_example({
                "To chart the values of ex_procname over time",
                "from lnav_example_log | stats.hist ex_procname",
                help_example::language::prql,
            }),
        nullptr,
        "prql-source",
        {"prql-source"},
    },
    {
        "stats.sum_of",
        prql_cmd_sort,
        help_text("stats.sum_of", "Compute the sum of col")
            .prql_function()
            .with_tags({"prql"})
            .with_parameter(help_text{"col", "The column to sum"})
            .with_example({
                "To get the sum of a",
                "from [{a=1}, {a=1}, {a=2}] | stats.sum_of a",
                help_example::language::prql,
            }),
        nullptr,
        "prql-source",
        {"prql-source"},
    },
    {
        "stats.by",
        prql_cmd_sort,
        help_text("stats.by", "A shorthand for grouping and aggregating")
            .prql_function()
            .with_tags({"prql"})
            .with_parameter(help_text{"col", "The column to sum"})
            .with_parameter(help_text{"values", "The aggregations to perform"})
            .with_example({
                "To partition by a and get the sum of b",
                "from [{a=1, b=1}, {a=1, b=1}, {a=2, b=1}] | stats.by a "
                "{sum b}",
                help_example::language::prql,
            }),
        nullptr,
        "prql-source",
        {"prql-source"},
    },
    {
        "sort",
        prql_cmd_sort,
        help_text("sort")
            .prql_transform()
            .with_tags({"prql"})
            .with_summary("PRQL transform to sort rows")
            .with_parameter(help_text{
                "expr", "The values to use when ordering the result set"}
                                .with_grouping("{", "}"))
            .with_example({
                "To sort the rows in descending order",
                "from [{a=1}, {a=2}] | sort {-a}",
                help_example::language::prql,
            }),
        nullptr,
        "prql-source",
        {"prql-source"},
    },
    {
        "take",
        prql_cmd_take,
        help_text("take")
            .prql_transform()
            .with_tags({"prql"})
            .with_summary("PRQL command to pick rows based on their position")
            .with_parameter({"n_or_range", "The number of rows or range"})
            .with_example({
                "To pick the first row",
                "from [{a=1}, {a=2}, {a=3}] | take 1",
                help_example::language::prql,
            })
            .with_example({
                "To pick the second and third rows",
                "from [{a=1}, {a=2}, {a=3}] | take 2..3",
                help_example::language::prql,
            }),
        nullptr,
        "prql-source",
        {"prql-source"},
    },
    {
        "utils.distinct",
        prql_cmd_sort,
        help_text("utils.distinct",
                  "A shorthand for getting distinct values of col")
            .prql_function()
            .with_tags({"prql"})
            .with_parameter(help_text{"col", "The column to sum"})
            .with_example({
                "To get the distinct values of a",
                "from [{a=1}, {a=1}, {a=2}] | utils.distinct a",
                help_example::language::prql,
            }),
        nullptr,
        "prql-source",
        {"prql-source"},
    },
};

static readline_context::command_map_t sql_cmd_map;

static auto bound_sql_cmd_map
    = injector::bind<readline_context::command_map_t,
                     sql_cmd_map_tag>::to_instance(+[]() {
          for (auto& cmd : sql_commands) {
              sql_cmd_map[cmd.c_name] = &cmd;
              if (cmd.c_help.ht_name) {
                  cmd.c_help.index_tags();
              }
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
