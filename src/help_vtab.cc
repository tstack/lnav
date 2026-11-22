/**
 * Copyright (c) 2026, Timothy Stack
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
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS AND CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * lnav_help: the reference text behind `:help`, the prompt's help panel, and
 * the generated cmd-ref/sql-ref docs, as rows.  A table's row comes from its
 * CREATE TABLE: the comment lines before it are its summary, and a
 * "-- Tags: a, b" line among them gives its topics, like a command's
 * with_tags(), so a lookup by topic finds tables too; every table needs one. It
 * lets a script (or an agent driving lnav over the external-access API) look up
 * what commands and SQL functions the running build has, by name or by tag,
 * without the docs.
 */

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "base/attr_line.hh"
#include "base/injector.bind.hh"
#include "base/injector.hh"
#include "base/lnav_log.hh"
#include "base/string_util.hh"
#include "bound_tags.hh"
#include "environ_vtab.hh"
#include "fmt/format.h"
#include "help_text.hh"
#include "help_text_formatter.hh"
#include "lnav.hh"
#include "log_vtab_impl.hh"
#include "mapbox/variant.hpp"
#include "sql_help.hh"
#include "static_file_vtab.hh"
#include "vtab_module.hh"
#include "vtab_module_json.hh"
#include "yajlpp/yajlpp.hh"

namespace {

// A command, SQL function or keyword, or PRQL transform with help text.
struct help_entry_row {
    const help_text* her_help;
    // Other names the same entry answers to (command aliases).
    std::vector<std::string> her_aliases;
};

// One of lnav's own tables, whose row comes from its DDL, or one of the
// kinds of errors that a user_message can be marked with.
struct help_table_row {
    const char* htr_kind;
    std::string htr_name;
    std::string htr_summary;
    std::vector<std::string> htr_tags;
    std::string htr_ddl;
};

using help_row = mapbox::util::variant<help_entry_row, help_table_row>;

// The comment lines before the CREATE TABLE, joined, as the table's summary.
std::string
table_summary(const std::string& ddl)
{
    std::string retval;

    for (const auto& line : string_fragment::from_str(ddl).split_lines()) {
        auto trimmed = line.trim();
        if (trimmed.empty()) {
            continue;
        }
        if (!trimmed.startswith("--")) {
            break;
        }
        if (!retval.empty()) {
            retval += " ";
        }
        retval += trimmed.substr(2).trim().to_string();
    }
    return retval;
}

const char*
kind_for(const help_text& ht)
{
    switch (ht.ht_context) {
        case help_context_t::HC_COMMAND:
            return "command";
        case help_context_t::HC_SQL_COMMAND:
            return "sql_command";
        case help_context_t::HC_SQL_KEYWORD:
            return "sql_keyword";
        case help_context_t::HC_SQL_INFIX:
            return "sql_infix";
        case help_context_t::HC_SQL_FUNCTION:
            return ht.ht_function_type == help_function_type_t::HFT_AGGREGATE
                ? "sql_aggregate_function"
                : "sql_function";
        case help_context_t::HC_SQL_TABLE_VALUED_FUNCTION:
            return "sql_table_function";
        case help_context_t::HC_PRQL_TRANSFORM:
            return "prql_transform";
        case help_context_t::HC_PRQL_FUNCTION:
            return "prql_function";
        default:
            return "other";
    }
}

// The JSON-schema-style type of a parameter's value.  A parameter without a
// value is a flag, so it's either given or not.
const char*
type_for(help_parameter_format_t fmt)
{
    switch (fmt) {
        case help_parameter_format_t::HPF_NONE:
            return "boolean";
        case help_parameter_format_t::HPF_INTEGER:
            return "integer";
        case help_parameter_format_t::HPF_NUMBER:
            return "number";
        default:
            return "string";
    }
}

// The formats that tell a caller how to write a string value, or nullptr for
// the rest, including completion sources like HPF_LOADED_FILE.
const char*
format_for(help_parameter_format_t fmt)
{
    switch (fmt) {
        case help_parameter_format_t::HPF_REGEX:
            return "regex";
        case help_parameter_format_t::HPF_SQL:
            return "sql";
        case help_parameter_format_t::HPF_SQL_EXPR:
            return "sql_expr";
        case help_parameter_format_t::HPF_TEXT:
        case help_parameter_format_t::HPF_MULTILINE_TEXT:
            return "text";
        case help_parameter_format_t::HPF_ADJUSTED_TIME:
        case help_parameter_format_t::HPF_TIME_FILTER_POINT:
            return "time";
        case help_parameter_format_t::HPF_FILENAME:
        case help_parameter_format_t::HPF_LOCAL_FILENAME:
        case help_parameter_format_t::HPF_LOADED_FILE:
        case help_parameter_format_t::HPF_FILE_WITH_ZONE:
        case help_parameter_format_t::HPF_VISIBLE_FILES:
        case help_parameter_format_t::HPF_HIDDEN_FILES:
            return "filename";
        case help_parameter_format_t::HPF_DIRECTORY:
            return "directory";
        case help_parameter_format_t::HPF_CONFIG_PATH:
            return "config_path";
        case help_parameter_format_t::HPF_TIMEZONE:
            return "timezone";
        case help_parameter_format_t::HPF_LOG_STATEMENT:
            return "log_statement";
        default:
            return nullptr;
    }
}

// Help strings can carry ANSI styling; the table holds plain text.
std::string
plain(const char* str)
{
    if (str == nullptr) {
        return "";
    }
    return attr_line_t::from_ansi_str(str).get_string();
}

std::string
synopsis_for(const help_text& ht)
{
    attr_line_t al;

    // Wide enough that the formatter keeps the synopsis on one line where it
    // can; keywords like SELECT still break per clause.
    format_help_text_for_term(ht, 1000, al, help_text_content::synopsis);
    auto retval = al.get_string();
    while (!retval.empty() && isspace(retval.back())) {
        retval.pop_back();
    }
    return retval;
}

// Opposites name commands without the colon; prefix it so they match `name`.
template<typename S>
void
gen_strings(yajlpp_gen& gen,
            const std::vector<S>& strs,
            const char* prefix = "")
{
    yajlpp_array arr(gen);
    for (const auto& str : strs) {
        arr.gen(std::string(prefix) + str);
    }
}

// The type, format, and enum values of one of a parameter's values.
void
gen_value_schema(yajl_gen gen, yajlpp_map& obj, const help_text& param)
{
    obj.gen("type");
    obj.gen(type_for(param.ht_format));
    const auto* format = format_for(param.ht_format);
    if (format != nullptr) {
        obj.gen("format");
        obj.gen(format);
    }
    if (!param.ht_enum_values.empty()) {
        obj.gen("enum");
        yajlpp_array enums(gen);
        for (const auto& ev : param.ht_enum_values) {
            enums.gen(ev);
        }
    }
}

/**
 * The parameters, described along the lines of JSON schema: a parameter
 * that can be given more than once is an array of its values.  The "flag" is
 * the keyword before the value, and the nested "parameters" are the parts
 * of a compound parameter, in order.
 */
void
gen_parameters(yajl_gen gen, const std::vector<help_text>& params)
{
    yajlpp_array arr(gen);
    for (const auto& param : params) {
        yajlpp_map obj(gen);

        obj.gen("name");
        obj.gen(param.ht_name);
        if (param.ht_flag_name != nullptr) {
            obj.gen("flag");
            obj.gen(param.ht_flag_name);
        }
        obj.gen("description");
        obj.gen(plain(param.ht_summary));
        obj.gen("required");
        obj.gen(param.ht_nargs == help_nargs_t::HN_REQUIRED
                || param.ht_nargs == help_nargs_t::HN_ONE_OR_MORE);
        if (param.ht_nargs == help_nargs_t::HN_ZERO_OR_MORE
            || param.ht_nargs == help_nargs_t::HN_ONE_OR_MORE)
        {
            obj.gen("type");
            obj.gen("array");
            obj.gen("items");
            {
                yajlpp_map items(gen);
                gen_value_schema(gen, items, param);
            }
            if (param.ht_nargs == help_nargs_t::HN_ONE_OR_MORE) {
                obj.gen("minItems");
                obj.gen(1);
            }
        } else {
            gen_value_schema(gen, obj, param);
        }
        if (param.ht_default_value != nullptr) {
            obj.gen("default");
            obj.gen(param.ht_default_value);
        }
        if (!param.ht_parameters.empty()) {
            obj.gen("parameters");
            gen_parameters(gen, param.ht_parameters);
        }
    }
}

void
gen_examples(yajl_gen gen, const help_text& ht)
{
    yajlpp_array arr(gen);
    for (const auto& ex : ht.ht_example) {
        yajlpp_map obj(gen);

        obj.gen("description");
        obj.gen(plain(ex.he_description));
        obj.gen("code");
        // A command's examples hold only its arguments; give the whole
        // command so the code can be run as written.
        if (ht.ht_context == help_context_t::HC_COMMAND) {
            obj.gen(fmt::format(FMT_STRING(":{} {}"), ht.ht_name, ex.he_cmd));
        } else {
            obj.gen(ex.he_cmd);
        }
        obj.gen("language");
        obj.gen(ex.he_language == help_example::language::prql ? "prql"
                                                               : "default");
    }
}

struct lnav_help : tvt_iterator_cursor<lnav_help> {
    using iterator = std::vector<help_row>::iterator;

    static constexpr const char* NAME = "lnav_help";
    static constexpr const char* CREATE_STMT = R"(
-- The reference for lnav's commands, SQL functions and keywords, PRQL
-- transforms, and lnav's own tables, one row per entry.  Find entries by name
-- or by tag, for example:
--   SELECT name, synopsis, summary FROM lnav_help WHERE json_contains(tags, 'filtering')
-- Tags: scripting
CREATE TABLE lnav_db.lnav_help (
    kind TEXT,         -- The kind of entry: command, table, sql_function, sql_aggregate_function, sql_table_function, sql_command, sql_keyword, sql_infix, prql_transform, prql_function, or error_type.
    name TEXT,         -- The name to call it by; commands start with a colon (e.g. ':filter-out').
    effect TEXT,       -- For a command or SQL dot-command, what running it can change: read (nothing, though it may print to the script's output), output (writes to the file it's given, or with '-' to the script's output), view (lnav's state for this session, such as views, filters, marks, and the files shown), config (configuration or session files), external (runs programs, reads or writes other files, or opens connections), exit (quits lnav), or unknown (not declared; treat it like external).
    aliases TEXT,      -- A JSON array of other names for the same entry.
    summary TEXT,      -- A one-line summary.
    synopsis TEXT,     -- How to call it, as plain text; for a table, its CREATE TABLE statement with a comment on each column; for an error_type, the HTTP status of a request that fails with it.
    description TEXT,  -- Further detail, when there is more than the summary.
    parameters TEXT,   -- A JSON array of the parameters, each an object with name, summary, nargs (required, optional, zero_or_more, one_or_more), format, and optionally flag, default, enum, and nested parameters.
    examples TEXT,     -- A JSON array of examples, each an object with description, code, and language (default or prql).
    tags TEXT,         -- A JSON array of the topics this entry belongs to (e.g. filtering, navigation, datetime).
    opposites TEXT,    -- A JSON array of the names of entries that undo this one.
    prql_path TEXT     -- For PRQL functions, the dotted module path (e.g. 'text.contains').
);
)";

    // Built on the first query: commands are registered after the virtual
    // tables are created, and the set doesn't change once lnav is running.
    // Cursors hold iterators into it, so it's never rebuilt.
    static std::vector<help_row>& rows()
    {
        static auto retval = build_rows();
        return retval;
    }

    static std::vector<help_row> build_rows()
    {
        std::vector<help_row> retval;
        std::map<const help_text*, size_t> index;

        auto add = [&](const help_text* ht, const std::string& key) {
            if (ht == nullptr || ht->ht_name == nullptr
                || ht->ht_name[0] == '\0')
            {
                return;
            }
            auto iter = index.find(ht);
            if (iter == index.end()) {
                index.emplace(ht, retval.size());
                retval.emplace_back(help_entry_row{ht, {}});
                return;
            }
            // The SQL help map also keys an entry by its keywords and enum
            // values (e.g. SELECT under FROM); only command aliases are
            // names the entry can be called by.
            if (ht->ht_context == help_context_t::HC_COMMAND
                && key != ht->ht_name)
            {
                retval[iter->second]
                    .get<help_entry_row>()
                    .her_aliases.emplace_back(":" + key);
            }
        };

        for (const auto& [name, cmd] : lnav_commands) {
            add(&cmd->c_help, name.to_string());
        }
        for (const auto& [name, ht] : sqlite_function_help) {
            add(ht, name);
        }
        const auto* sql_cmd_map
            = injector::get<lnav::commands::command_map_t*, sql_cmd_map_tag>();
        for (const auto& [name, cmd] : *sql_cmd_map) {
            add(&cmd->c_help, name.to_string());
        }

        // lnav's own tables.  Their entries in sqlite_master only say
        // "CREATE VIRTUAL TABLE ... USING ...", without the column comments.
        for (const auto& [name, ddl] : vtab_module_ddls) {
            auto tags_iter = vtab_module_tags.find(name);
            retval.emplace_back(help_table_row{
                "table",
                name.to_string(),
                table_summary(ddl),
                tags_iter != vtab_module_tags.end()
                    ? tags_iter->second
                    : std::vector<std::string>{},
                ddl,
            });
        }

        // The tables that aren't made by vtab_module, whose DDL comes from
        // elsewhere, as in the SCHEMA view.  The tables of the log formats
        // are left out: there are dozens of them, and lnav_format_values
        // describes their columns.  The logline table's columns change with
        // the focused message, so it is left out too.
        auto add_table = [&retval](const char* name,
                                   const std::string& summary,
                                   const std::string& ddl,
                                   std::vector<std::string> tags) {
            const auto trimmed = trim(ddl);
            retval.emplace_back(help_table_row{
                "table",
                name,
                summary.empty() ? table_summary(trimmed) : summary,
                std::move(tags),
                trimmed,
            });
        };
        add_table("environ", "", ENVIRON_CREATE_STMT, {"scripting"});
        add_table(
            "lnav_app_files", "", STATIC_FILE_CREATE_STMT, {"scripting"});
        auto* vtab_manager = injector::get<log_vtab_manager*>();
        auto all_logs
            = vtab_manager->lookup_impl(intern_string::lookup("all_logs"));
        if (all_logs != nullptr) {
            add_table("all_logs",
                      "The messages from every file in the LOG view, with the "
                      "columns that every log format has",
                      all_logs->get_table_statement(),
                      {"vtables"});
        }

        // The kinds of errors, whose names are the "type" of an error from
        // the external-access server.  The slice is in link order, so they
        // are sorted for a stable listing.
        std::vector<const lnav::error_type_t*> error_types;
        for (const auto& et : lnav::error_type_t::get_all_types()) {
            error_types.emplace_back(&et);
        }
        std::sort(error_types.begin(),
                  error_types.end(),
                  [](const auto* lhs, const auto* rhs) {
                      return lhs->get_name() < rhs->get_name();
                  });
        for (const auto* et : error_types) {
            retval.emplace_back(help_table_row{
                "error_type",
                et->get_name().to_string(),
                et->get_description().to_string(),
                {"scripting"},
                fmt::format(FMT_STRING("HTTP {}"), et->get_http_status()),
            });
        }

        log_debug("lnav_help: %zu entries", retval.size());
        return retval;
    }

    iterator begin() { return rows().begin(); }

    iterator end() { return rows().end(); }

    int get_column(const cursor& vc, sqlite3_context* ctx, int col)
    {
        return vc.iter->match(
            [ctx, col](const help_entry_row& row) {
                return get_entry_column(row, ctx, col);
            },
            [ctx, col](const help_table_row& row) {
                return get_table_column(row, ctx, col);
            });
    }

    static int get_entry_column(const help_entry_row& row,
                                sqlite3_context* ctx,
                                int col)
    {
        const auto& ht = *row.her_help;
        const auto* cmd_prefix
            = ht.ht_context == help_context_t::HC_COMMAND ? ":" : "";

        switch (col) {
            case 0:
                to_sqlite(ctx, kind_for(ht));
                break;
            case 1:
                to_sqlite(ctx, std::string(cmd_prefix) + ht.ht_name);
                break;
            case 2:
                if (ht.ht_context == help_context_t::HC_COMMAND
                    || ht.ht_context == help_context_t::HC_SQL_COMMAND)
                {
                    to_sqlite(ctx, help_effect_name(ht.ht_effect));
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            case 3: {
                yajlpp_gen gen;
                {
                    yajlpp_array arr(gen);
                    for (const auto& alias : row.her_aliases) {
                        arr.gen(alias);
                    }
                }
                to_sqlite(ctx, gen);
                break;
            }
            case 4:
                to_sqlite(ctx, plain(ht.ht_summary));
                break;
            case 5:
                to_sqlite(ctx, synopsis_for(ht));
                break;
            case 6:
                if (ht.ht_description == nullptr
                    || ht.ht_description[0] == '\0')
                {
                    sqlite3_result_null(ctx);
                } else {
                    to_sqlite(ctx, plain(ht.ht_description));
                }
                break;
            case 7: {
                yajlpp_gen gen;
                gen_parameters(gen, ht.ht_parameters);
                to_sqlite(ctx, gen);
                break;
            }
            case 8: {
                yajlpp_gen gen;
                gen_examples(gen, ht);
                to_sqlite(ctx, gen);
                break;
            }
            case 9: {
                yajlpp_gen gen;
                gen_strings(gen, ht.ht_tags);
                to_sqlite(ctx, gen);
                break;
            }
            case 10: {
                yajlpp_gen gen;
                gen_strings(gen, ht.ht_opposites, cmd_prefix);
                to_sqlite(ctx, gen);
                break;
            }
            case 11:
                if (ht.ht_prql_path.empty()) {
                    sqlite3_result_null(ctx);
                } else {
                    std::string path;
                    for (const auto* part : ht.ht_prql_path) {
                        if (!path.empty()) {
                            path += ".";
                        }
                        path += part;
                    }
                    to_sqlite(ctx, path);
                }
                break;
            default:
                sqlite3_result_null(ctx);
                break;
        }
        return SQLITE_OK;
    }

    static int get_table_column(const help_table_row& row,
                                sqlite3_context* ctx,
                                int col)
    {
        switch (col) {
            case 0:
                to_sqlite(ctx, row.htr_kind);
                break;
            case 1:
                to_sqlite(ctx, row.htr_name);
                break;
            case 4:
                to_sqlite(ctx, row.htr_summary);
                break;
            case 5:
                to_sqlite(ctx, row.htr_ddl);
                break;
            case 9: {
                yajlpp_gen gen;
                gen_strings(gen, row.htr_tags);
                to_sqlite(ctx, gen);
                break;
            }
            case 3:
            case 7:
            case 8:
            case 10: {
                yajlpp_gen gen;
                {
                    yajlpp_array arr(gen);
                }
                to_sqlite(ctx, gen);
                break;
            }
            default:
                sqlite3_result_null(ctx);
                break;
        }
        return SQLITE_OK;
    }
};

}  // namespace

static auto help_binder = injector::bind_multiple<vtab_module_base>()
                              .add<vtab_module<tvt_no_update<lnav_help>>>();
