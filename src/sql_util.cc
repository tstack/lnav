/**
 * Copyright (c) 2013, Timothy Stack
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
 *
 * @file sql_util.cc
 */

#include <algorithm>
#include <regex>
#include <vector>

#include "sql_util.hh"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "base/auto_mem.hh"
#include "base/injector.hh"
#include "base/lnav_log.hh"
#include "base/string_util.hh"
#include "base/time_util.hh"
#include "bound_tags.hh"
#include "config.h"
#include "lnav_util.hh"
#include "pcrepp/pcre2pp.hh"
#include "readline_context.hh"
#include "readline_highlighters.hh"
#include "shlex.resolver.hh"
#include "sql_help.hh"
#include "sqlite-extension-func.hh"

using namespace lnav::roles::literals;

/**
 * Copied from -- http://www.sqlite.org/lang_keywords.html
 */
const char* sql_keywords[] = {
    "ABORT",
    "ACTION",
    "ADD",
    "AFTER",
    "ALL",
    "ALTER",
    "ALWAYS",
    "ANALYZE",
    "AND",
    "AS",
    "ASC",
    "ATTACH",
    "AUTOINCREMENT",
    "BEFORE",
    "BEGIN",
    "BETWEEN",
    "BY",
    "CASCADE",
    "CASE",
    "CAST",
    "CHECK",
    "COLLATE",
    "COLUMN",
    "COMMIT",
    "CONFLICT",
    "CONSTRAINT",
    "CREATE",
    "CROSS",
    "CURRENT",
    "CURRENT_DATE",
    "CURRENT_TIME",
    "CURRENT_TIMESTAMP",
    "DATABASE",
    "DEFAULT",
    "DEFERRABLE",
    "DEFERRED",
    "DELETE",
    "DESC",
    "DETACH",
    "DISTINCT",
    "DO",
    "DROP",
    "EACH",
    "ELSE",
    "END",
    "ESCAPE",
    "EXCEPT",
    "EXCLUDE",
    "EXCLUSIVE",
    "EXISTS",
    "EXPLAIN",
    "FAIL",
    "FILTER",
    "FIRST",
    "FOLLOWING",
    "FOR",
    "FOREIGN",
    "FROM",
    "FULL",
    "GENERATED",
    "GLOB",
    "GROUP",
    "GROUPS",
    "HAVING",
    "IF",
    "IGNORE",
    "IMMEDIATE",
    "IN",
    "INDEX",
    "INDEXED",
    "INITIALLY",
    "INNER",
    "INSERT",
    "INSTEAD",
    "INTERSECT",
    "INTO",
    "IS",
    "ISNULL",
    "JOIN",
    "KEY",
    "LAST",
    "LEFT",
    "LIKE",
    "LIMIT",
    "MATCH",
    "NATURAL",
    "NO",
    "NOT",
    "NOTHING",
    "NOTNULL",
    "NULL",
    "NULLS",
    "OF",
    "OFFSET",
    "ON",
    "OR",
    "ORDER",
    "OTHERS",
    "OUTER",
    "OVER",
    "PARTITION",
    "PLAN",
    "PRAGMA",
    "PRECEDING",
    "PRIMARY",
    "QUERY",
    "RAISE",
    "RANGE",
    "RECURSIVE",
    "REFERENCES",
    "REGEXP",
    "REINDEX",
    "RELEASE",
    "RENAME",
    "REPLACE",
    "RESTRICT",
    "RIGHT",
    "ROLLBACK",
    "ROW",
    "ROWS",
    "SAVEPOINT",
    "SELECT",
    "SET",
    "TABLE",
    "TEMP",
    "TEMPORARY",
    "THEN",
    "TIES",
    "TO",
    "TRANSACTION",
    "TRIGGER",
    "UNBOUNDED",
    "UNION",
    "UNIQUE",
    "UPDATE",
    "USING",
    "VACUUM",
    "VALUES",
    "VIEW",
    "VIRTUAL",
    "WHEN",
    "WHERE",
    "WINDOW",
    "WITH",
    "WITHOUT",
};

const char* sql_function_names[] = {
    /* http://www.sqlite.org/lang_aggfunc.html */
    "avg(",
    "count(",
    "group_concat(",
    "max(",
    "min(",
    "sum(",
    "total(",

    /* http://www.sqlite.org/lang_corefunc.html */
    "abs(",
    "changes()",
    "char(",
    "coalesce(",
    "glob(",
    "ifnull(",
    "instr(",
    "hex(",
    "last_insert_rowid()",
    "length(",
    "like(",
    "load_extension(",
    "lower(",
    "ltrim(",
    "nullif(",
    "printf(",
    "quote(",
    "random()",
    "randomblob(",
    "replace(",
    "round(",
    "rtrim(",
    "soundex(",
    "sqlite_compileoption_get(",
    "sqlite_compileoption_used(",
    "sqlite_source_id()",
    "sqlite_version()",
    "substr(",
    "total_changes()",
    "trim(",
    "typeof(",
    "unicode(",
    "upper(",
    "zeroblob(",

    /* http://www.sqlite.org/lang_datefunc.html */
    "date(",
    "time(",
    "datetime(",
    "julianday(",
    "strftime(",

    nullptr,
};

const std::unordered_map<unsigned char, const char*> sql_constraint_names = {
    {SQLITE_INDEX_CONSTRAINT_EQ, "="},
    {SQLITE_INDEX_CONSTRAINT_GT, ">"},
    {SQLITE_INDEX_CONSTRAINT_LE, "<="},
    {SQLITE_INDEX_CONSTRAINT_LT, "<"},
    {SQLITE_INDEX_CONSTRAINT_GE, ">="},
    {SQLITE_INDEX_CONSTRAINT_MATCH, "MATCH"},
    {SQLITE_INDEX_CONSTRAINT_LIKE, "LIKE"},
    {SQLITE_INDEX_CONSTRAINT_GLOB, "GLOB"},
    {SQLITE_INDEX_CONSTRAINT_REGEXP, "REGEXP"},
    {SQLITE_INDEX_CONSTRAINT_NE, "!="},
    {SQLITE_INDEX_CONSTRAINT_ISNOT, "IS NOT"},
    {SQLITE_INDEX_CONSTRAINT_ISNOTNULL, "IS NOT NULL"},
    {SQLITE_INDEX_CONSTRAINT_ISNULL, "IS NULL"},
    {SQLITE_INDEX_CONSTRAINT_IS, "IS"},
#if defined(SQLITE_INDEX_CONSTRAINT_LIMIT)
    {SQLITE_INDEX_CONSTRAINT_LIMIT, "LIMIT"},
    {SQLITE_INDEX_CONSTRAINT_OFFSET, "OFFSET"},
#endif
#if defined(SQLITE_INDEX_CONSTRAINT_FUNCTION)
    {SQLITE_INDEX_CONSTRAINT_FUNCTION, "function"},
#endif
};

std::multimap<std::string, help_text*> sqlite_function_help;

static int
handle_db_list(void* ptr, int ncols, char** colvalues, char** colnames)
{
    struct sqlite_metadata_callbacks* smc;

    smc = (struct sqlite_metadata_callbacks*) ptr;

    smc->smc_db_list[colvalues[1]] = std::vector<std::string>();
    if (!smc->smc_database_list) {
        return 0;
    }

    return smc->smc_database_list(ptr, ncols, colvalues, colnames);
}

struct table_list_data {
    struct sqlite_metadata_callbacks* tld_callbacks;
    db_table_map_t::iterator* tld_iter;
};

static int
handle_table_list(void* ptr, int ncols, char** colvalues, char** colnames)
{
    struct table_list_data* tld = (struct table_list_data*) ptr;

    (*tld->tld_iter)->second.emplace_back(colvalues[0]);
    if (!tld->tld_callbacks->smc_table_list) {
        return 0;
    }

    return tld->tld_callbacks->smc_table_list(
        tld->tld_callbacks, ncols, colvalues, colnames);
}

int
walk_sqlite_metadata(sqlite3* db, struct sqlite_metadata_callbacks& smc)
{
    auto_mem<char, sqlite3_free> errmsg;
    int retval;

    if (smc.smc_collation_list) {
        retval = sqlite3_exec(db,
                              "pragma collation_list",
                              smc.smc_collation_list,
                              &smc,
                              errmsg.out());
        if (retval != SQLITE_OK) {
            log_error("could not get collation list -- %s", errmsg.in());
            return retval;
        }
    }

    retval = sqlite3_exec(
        db, "pragma database_list", handle_db_list, &smc, errmsg.out());
    if (retval != SQLITE_OK) {
        log_error("could not get DB list -- %s", errmsg.in());
        return retval;
    }

    for (auto iter = smc.smc_db_list.begin(); iter != smc.smc_db_list.end();
         ++iter)
    {
        struct table_list_data tld = {&smc, &iter};
        auto_mem<char, sqlite3_free> query;

        query = sqlite3_mprintf(
            "SELECT name,sql FROM %Q.sqlite_master "
            "WHERE type in ('table', 'view')",
            iter->first.c_str());

        retval = sqlite3_exec(db, query, handle_table_list, &tld, errmsg.out());
        if (retval != SQLITE_OK) {
            log_error("could not get table list -- %s", errmsg.in());
            return retval;
        }

        for (auto table_iter = iter->second.begin();
             table_iter != iter->second.end();
             ++table_iter)
        {
            auto_mem<char, sqlite3_free> table_query;
            std::string& table_name = *table_iter;

            table_query = sqlite3_mprintf("pragma %Q.table_xinfo(%Q)",
                                          iter->first.c_str(),
                                          table_name.c_str());
            if (table_query == nullptr) {
                return SQLITE_NOMEM;
            }

            if (smc.smc_table_info) {
                retval = sqlite3_exec(
                    db, table_query, smc.smc_table_info, &smc, errmsg.out());
                if (retval != SQLITE_OK) {
                    log_error("could not get table info -- %s", errmsg.in());
                    return retval;
                }
            }

            table_query = sqlite3_mprintf("pragma %Q.foreign_key_list(%Q)",
                                          iter->first.c_str(),
                                          table_name.c_str());
            if (table_query == nullptr) {
                return SQLITE_NOMEM;
            }

            if (smc.smc_foreign_key_list) {
                retval = sqlite3_exec(db,
                                      table_query,
                                      smc.smc_foreign_key_list,
                                      &smc,
                                      errmsg.out());
                if (retval != SQLITE_OK) {
                    log_error("could not get foreign key list -- %s",
                              errmsg.in());
                    return retval;
                }
            }
        }
    }

    return retval;
}

static int
schema_collation_list(void* ptr, int ncols, char** colvalues, char** colnames)
{
    return 0;
}

static int
schema_db_list(void* ptr, int ncols, char** colvalues, char** colnames)
{
    struct sqlite_metadata_callbacks* smc = (sqlite_metadata_callbacks*) ptr;
    std::string& schema_out = *((std::string*) smc->smc_userdata);
    auto_mem<char, sqlite3_free> attach_sql;

    attach_sql = sqlite3_mprintf(
        "ATTACH DATABASE %Q AS %Q;\n", colvalues[2], colvalues[1]);

    schema_out += attach_sql;

    return 0;
}

static int
schema_table_list(void* ptr, int ncols, char** colvalues, char** colnames)
{
    struct sqlite_metadata_callbacks* smc = (sqlite_metadata_callbacks*) ptr;
    std::string& schema_out = *((std::string*) smc->smc_userdata);
    auto_mem<char, sqlite3_free> create_sql;

    create_sql = sqlite3_mprintf("%s;\n", colvalues[1]);

    schema_out += create_sql;

    return 0;
}

static int
schema_table_info(void* ptr, int ncols, char** colvalues, char** colnames)
{
    return 0;
}

static int
schema_foreign_key_list(void* ptr, int ncols, char** colvalues, char** colnames)
{
    return 0;
}

void
dump_sqlite_schema(sqlite3* db, std::string& schema_out)
{
    struct sqlite_metadata_callbacks schema_sql_meta_callbacks
        = {schema_collation_list,
           schema_db_list,
           schema_table_list,
           schema_table_info,
           schema_foreign_key_list,
           &schema_out,
           {}};

    walk_sqlite_metadata(db, schema_sql_meta_callbacks);
}

void
attach_sqlite_db(sqlite3* db, const std::string& filename)
{
    static const std::regex db_name_converter("[^\\w]");

    auto_mem<sqlite3_stmt> stmt(sqlite3_finalize);

    if (sqlite3_prepare_v2(db, "ATTACH DATABASE ? as ?", -1, stmt.out(), NULL)
        != SQLITE_OK)
    {
        log_error("could not prepare DB attach statement -- %s",
                  sqlite3_errmsg(db));
        return;
    }

    if (sqlite3_bind_text(
            stmt.in(), 1, filename.c_str(), filename.length(), SQLITE_TRANSIENT)
        != SQLITE_OK)
    {
        log_error("could not bind DB attach statement -- %s",
                  sqlite3_errmsg(db));
        return;
    }

    size_t base_start = filename.find_last_of("/\\");
    std::string db_name;

    if (base_start == std::string::npos) {
        db_name = filename;
    } else {
        db_name = filename.substr(base_start + 1);
    }

    db_name = std::regex_replace(db_name, db_name_converter, "_");

    if (sqlite3_bind_text(
            stmt.in(), 2, db_name.c_str(), db_name.length(), SQLITE_TRANSIENT)
        != SQLITE_OK)
    {
        log_error("could not bind DB attach statement -- %s",
                  sqlite3_errmsg(db));
        return;
    }

    if (sqlite3_step(stmt.in()) != SQLITE_DONE) {
        log_error("could not execute DB attach statement -- %s",
                  sqlite3_errmsg(db));
        return;
    }
}

static void
sqlite_logger(void* dummy, int code, const char* msg)
{
    lnav_log_level_t level;

    switch (code) {
        case SQLITE_OK:
            level = lnav_log_level_t::DEBUG;
            break;
#ifdef SQLITE_NOTICE
        case SQLITE_NOTICE:
            level = lnav_log_level_t::INFO;
            break;
#endif
#ifdef SQLITE_WARNING
        case SQLITE_WARNING:
            level = lnav_log_level_t::WARNING;
            break;
#endif
        default:
            level = lnav_log_level_t::ERROR;
            break;
    }

    log_msg(level, __FILE__, __LINE__, "(%d) %s", code, msg);

    ensure(code != 21);
}

void
sql_install_logger()
{
#ifdef SQLITE_CONFIG_LOG
    sqlite3_config(SQLITE_CONFIG_LOG, sqlite_logger, NULL);
#endif
}

bool
sql_ident_needs_quote(const char* ident)
{
    for (int lpc = 0; ident[lpc]; lpc++) {
        if (!isalnum(ident[lpc]) && ident[lpc] != '_') {
            return true;
        }
    }

    return false;
}

char*
sql_quote_ident(const char* ident)
{
    bool needs_quote = false;
    size_t quote_count = 0, alloc_size;
    char* retval;

    for (int lpc = 0; ident[lpc]; lpc++) {
        if ((lpc == 0 && isdigit(ident[lpc]))
            || (!isalnum(ident[lpc]) && ident[lpc] != '_'))
        {
            needs_quote = true;
        }
        if (ident[lpc] == '"') {
            quote_count += 1;
        }
    }

    alloc_size = strlen(ident) + quote_count * 2 + (needs_quote ? 2 : 0) + 1;
    if ((retval = (char*) sqlite3_malloc(alloc_size)) == NULL) {
        retval = NULL;
    } else {
        char* curr = retval;

        if (needs_quote) {
            curr[0] = '"';
            curr += 1;
        }
        for (size_t lpc = 0; ident[lpc] != '\0'; lpc++) {
            switch (ident[lpc]) {
                case '"':
                    curr[0] = '"';
                    curr += 1;
                default:
                    curr[0] = ident[lpc];
                    break;
            }
            curr += 1;
        }
        if (needs_quote) {
            curr[0] = '"';
            curr += 1;
        }

        *curr = '\0';
    }

    return retval;
}

std::string
sql_safe_ident(const string_fragment& ident)
{
    std::string retval = std::to_string(ident);

    for (size_t lpc = 0; lpc < retval.size(); lpc++) {
        char ch = retval[lpc];

        if (isalnum(ch) || ch == '_') {
            retval[lpc] = tolower(ch);
        } else {
            retval[lpc] = '_';
        }
    }

    return retval;
}

attr_line_t
annotate_sql_with_error(sqlite3* db, const char* sql, const char* tail)
{
    const auto* errmsg = sqlite3_errmsg(db);
    attr_line_t retval;
    int erroff = -1;

#if defined(HAVE_SQLITE3_ERROR_OFFSET)
    erroff = sqlite3_error_offset(db);
#endif
    if (tail != nullptr) {
        const auto* tail_lf = strchr(tail, '\n');
        if (tail_lf == nullptr) {
            tail = tail + strlen(tail);
        } else {
            tail = tail_lf;
        }
        retval.append(string_fragment::from_bytes(sql, tail - sql));
    } else {
        retval.append(sql);
    }
    if (erroff >= retval.length()) {
        erroff -= 1;
    }
    if (erroff != -1 && !endswith(retval.get_string(), "\n")) {
        retval.append("\n");
    }
    retval.with_attr_for_all(VC_ROLE.value(role_t::VCR_QUOTED_CODE));
    readline_sqlite_highlighter(retval, retval.length());

    if (erroff != -1) {
        auto line_with_error
            = string_fragment(retval.get_string())
                  .find_boundaries_around(erroff, string_fragment::tag1{'\n'});
        auto erroff_in_line = erroff - line_with_error.sf_begin;

        attr_line_t pointer;

        pointer.append(erroff_in_line, ' ')
            .append("^ "_snippet_border)
            .append(lnav::roles::error(errmsg))
            .append("\n");

        retval.insert(line_with_error.sf_end + 1, pointer).rtrim();
    }

    return retval;
}

static void
sql_execute_script(sqlite3* db,
                   const std::map<std::string, scoped_value_t>& global_vars,
                   const char* src_name,
                   sqlite3_stmt* stmt,
                   std::vector<lnav::console::user_message>& errors)
{
    std::map<std::string, scoped_value_t> lvars;
    bool done = false;
    int param_count;

    sqlite3_clear_bindings(stmt);

    param_count = sqlite3_bind_parameter_count(stmt);
    for (int lpc = 0; lpc < param_count; lpc++) {
        const char* name;

        name = sqlite3_bind_parameter_name(stmt, lpc + 1);
        if (name[0] == '$') {
            const char* env_value;
            auto iter = lvars.find(&name[1]);
            if (iter != lvars.end()) {
                mapbox::util::apply_visitor(
                    sqlitepp::bind_visitor(stmt, lpc + 1), iter->second);
            } else {
                auto giter = global_vars.find(&name[1]);
                if (giter != global_vars.end()) {
                    mapbox::util::apply_visitor(
                        sqlitepp::bind_visitor(stmt, lpc + 1), giter->second);
                } else if ((env_value = getenv(&name[1])) != nullptr) {
                    sqlite3_bind_text(
                        stmt, lpc + 1, env_value, -1, SQLITE_TRANSIENT);
                } else {
                    sqlite3_bind_null(stmt, lpc + 1);
                }
            }
        } else {
            sqlite3_bind_null(stmt, lpc + 1);
        }
    }
    while (!done) {
        int retcode = sqlite3_step(stmt);
        switch (retcode) {
            case SQLITE_OK:
            case SQLITE_DONE:
                done = true;
                break;

            case SQLITE_ROW: {
                int ncols = sqlite3_column_count(stmt);

                for (int lpc = 0; lpc < ncols; lpc++) {
                    const char* name = sqlite3_column_name(stmt, lpc);
                    auto* raw_value = sqlite3_column_value(stmt, lpc);
                    auto value_type = sqlite3_value_type(raw_value);
                    scoped_value_t value;

                    switch (value_type) {
                        case SQLITE_INTEGER:
                            value = (int64_t) sqlite3_value_int64(raw_value);
                            break;
                        case SQLITE_FLOAT:
                            value = sqlite3_value_double(raw_value);
                            break;
                        case SQLITE_NULL:
                            value = null_value_t{};
                            break;
                        default:
                            value = string_fragment::from_bytes(
                                sqlite3_value_text(raw_value),
                                sqlite3_value_bytes(raw_value));
                            break;
                    }
                    lvars[name] = value;
                }
                break;
            }

            default: {
                const auto* sql_str = sqlite3_sql(stmt);
                auto sql_content
                    = annotate_sql_with_error(db, sql_str, nullptr);

                errors.emplace_back(
                    lnav::console::user_message::error(
                        "failed to execute SQL statement")
                        .with_reason(sqlite3_errmsg_to_attr_line(db))
                        .with_snippet(lnav::console::snippet::from(
                            intern_string::lookup(src_name), sql_content)));
                done = true;
                break;
            }
        }
    }

    sqlite3_reset(stmt);
}

static void
sql_compile_script(sqlite3* db,
                   const std::map<std::string, scoped_value_t>& global_vars,
                   const char* src_name,
                   const char* script_orig,
                   std::vector<lnav::console::user_message>& errors)
{
    const char* script = script_orig;

    while (script != nullptr && script[0]) {
        auto_mem<sqlite3_stmt> stmt(sqlite3_finalize);
        int line_number = 1;
        const char* tail;
        int retcode;

        while (isspace(*script) && script[0]) {
            script += 1;
        }
        for (const char* ch = script_orig; ch < script && ch[0]; ch++) {
            if (*ch == '\n') {
                line_number += 1;
            }
        }

        retcode = sqlite3_prepare_v2(db, script, -1, stmt.out(), &tail);
        log_debug("retcode %d  %p %p", retcode, script, tail);
        if (retcode != SQLITE_OK) {
            const auto* errmsg = sqlite3_errmsg(db);
            auto sql_content = annotate_sql_with_error(db, script, tail);

            errors.emplace_back(
                lnav::console::user_message::error(
                    "failed to compile SQL statement")
                    .with_reason(errmsg)
                    .with_snippet(
                        lnav::console::snippet::from(
                            intern_string::lookup(src_name), sql_content)
                            .with_line(line_number)));
            break;
        }
        if (script == tail) {
            break;
        }
        if (stmt == nullptr) {
        } else {
            sql_execute_script(db, global_vars, src_name, stmt.in(), errors);
        }

        script = tail;
    }
}

void
sql_execute_script(sqlite3* db,
                   const std::map<std::string, scoped_value_t>& global_vars,
                   const char* src_name,
                   const char* script,
                   std::vector<lnav::console::user_message>& errors)
{
    sql_compile_script(db, global_vars, src_name, script, errors);
}

static struct {
    int sqlite_type;
    const char* collator;
    const char* sample;
} TYPE_TEST_VALUE[] = {
    {SQLITE3_TEXT, "", "foobar"},
    {SQLITE_INTEGER, "", "123"},
    {SQLITE_FLOAT, "", "123.0"},
    {SQLITE_TEXT, "ipaddress", "127.0.0.1"},
};

int
guess_type_from_pcre(const std::string& pattern, std::string& collator)
{
    static const std::vector<int> number_matches = {1, 2};

    auto compile_res = lnav::pcre2pp::code::from(pattern);
    if (compile_res.isErr()) {
        return SQLITE3_TEXT;
    }

    auto re = compile_res.unwrap();
    std::vector<int> matches;
    int retval = SQLITE3_TEXT;
    int index = 0;

    collator.clear();
    for (const auto& test_value : TYPE_TEST_VALUE) {
        auto find_res
            = re.find_in(string_fragment::from_c_str(test_value.sample),
                         PCRE2_ANCHORED)
                  .ignore_error();
        if (find_res && find_res->f_all.sf_begin == 0
            && find_res->f_remaining.empty())
        {
            matches.push_back(index);
        }

        index += 1;
    }

    if (matches.size() == 1) {
        retval = TYPE_TEST_VALUE[matches.front()].sqlite_type;
        collator = TYPE_TEST_VALUE[matches.front()].collator;
    } else if (matches == number_matches) {
        retval = SQLITE_FLOAT;
        collator = "";
    }

    return retval;
}

const char*
sqlite3_type_to_string(int type)
{
    switch (type) {
        case SQLITE_FLOAT:
            return "FLOAT";
        case SQLITE_INTEGER:
            return "INTEGER";
        case SQLITE_TEXT:
            return "TEXT";
        case SQLITE_NULL:
            return "NULL";
        case SQLITE_BLOB:
            return "BLOB";
    }

    ensure("Invalid sqlite type");

    return nullptr;
}

/* XXX figure out how to do this with the template */
void
sqlite_close_wrapper(void* mem)
{
    sqlite3_close_v2((sqlite3*) mem);
}

int
sqlite_authorizer(void* pUserData,
                  int action_code,
                  const char* detail1,
                  const char* detail2,
                  const char* detail3,
                  const char* detail4)
{
    if (action_code == SQLITE_ATTACH) {
        return SQLITE_DENY;
    }
    return SQLITE_OK;
}

attr_line_t
sqlite3_errmsg_to_attr_line(sqlite3* db)
{
    const auto* errmsg = sqlite3_errmsg(db);
    if (startswith(errmsg, sqlitepp::ERROR_PREFIX)) {
        auto from_res = lnav::from_json<lnav::console::user_message>(
            &errmsg[strlen(sqlitepp::ERROR_PREFIX)]);

        if (from_res.isOk()) {
            return from_res.unwrap().to_attr_line();
        }

        return from_res.unwrapErr()[0].um_message.get_string();
    }

    return attr_line_t(errmsg);
}

std::string
sql_keyword_re()
{
    std::string retval = "(?:";
    bool first = true;

    for (const char* kw : sql_keywords) {
        if (!first) {
            retval.append("|");
        } else {
            first = false;
        }
        retval.append("\\b");
        retval.append(kw);
        retval.append("\\b");
    }
    retval += ")";

    return retval;
}

string_attr_type<void> SQL_COMMAND_ATTR("sql_command");
string_attr_type<void> SQL_KEYWORD_ATTR("sql_keyword");
string_attr_type<void> SQL_IDENTIFIER_ATTR("sql_ident");
string_attr_type<void> SQL_FUNCTION_ATTR("sql_func");
string_attr_type<void> SQL_STRING_ATTR("sql_string");
string_attr_type<void> SQL_NUMBER_ATTR("sql_number");
string_attr_type<void> SQL_UNTERMINATED_STRING_ATTR("sql_unstring");
string_attr_type<void> SQL_OPERATOR_ATTR("sql_oper");
string_attr_type<void> SQL_PAREN_ATTR("sql_paren");
string_attr_type<void> SQL_COMMA_ATTR("sql_comma");
string_attr_type<void> SQL_GARBAGE_ATTR("sql_garbage");
string_attr_type<void> SQL_COMMENT_ATTR("sql_comment");

void
annotate_sql_statement(attr_line_t& al)
{
    static const std::string keyword_re_str = R"(\A)" + sql_keyword_re();

    static const struct {
        lnav::pcre2pp::code re;
        string_attr_type<void>* type;
    } PATTERNS[] = {
        {
            lnav::pcre2pp::code::from_const(R"(\A,)"),
            &SQL_COMMA_ATTR,
        },
        {
            lnav::pcre2pp::code::from_const(R"(\A\(|\A\))"),
            &SQL_PAREN_ATTR,
        },
        {
            lnav::pcre2pp::code::from(keyword_re_str, PCRE2_CASELESS).unwrap(),
            &SQL_KEYWORD_ATTR,
        },
        {
            lnav::pcre2pp::code::from_const(R"(\A'[^']*('(?:'[^']*')*|$))"),
            &SQL_STRING_ATTR,
        },
        {
            lnav::pcre2pp::code::from_const(
                R"(\A-?\d+(?:\.\d*(?:[eE][\-\+]?\d+)?)?|0x[0-9a-fA-F]+$)"),
            &SQL_NUMBER_ATTR,
        },
        {
            lnav::pcre2pp::code::from_const(
                R"(\A(((\$|:|@)?\b[a-z_]\w*)|\"([^\"]+)\"|\[([^\]]+)]))",
                PCRE2_CASELESS),
            &SQL_IDENTIFIER_ATTR,
        },
        {
            lnav::pcre2pp::code::from_const(R"(\A--.*)"),
            &SQL_COMMENT_ATTR,
        },
        {
            lnav::pcre2pp::code::from_const(R"(\A(\*|<|>|=|!|\-|\+|\|\|))"),
            &SQL_OPERATOR_ATTR,
        },
        {
            lnav::pcre2pp::code::from_const(R"(\A.)"),
            &SQL_GARBAGE_ATTR,
        },
    };

    static const auto cmd_pattern
        = lnav::pcre2pp::code::from_const(R"(^(\.\w+))");
    static const auto ws_pattern = lnav::pcre2pp::code::from_const(R"(\A\s+)");

    auto& line = al.get_string();
    auto& sa = al.get_attrs();

    auto cmd_find_res
        = cmd_pattern.find_in(line, PCRE2_ANCHORED).ignore_error();
    if (cmd_find_res) {
        auto cap = cmd_find_res->f_all;
        sa.emplace_back(line_range(cap.sf_begin, cap.sf_end),
                        SQL_COMMAND_ATTR.value());
        return;
    }

    auto remaining = string_fragment::from_str(line);
    while (!remaining.empty()) {
        auto ws_find_res = ws_pattern.find_in(remaining).ignore_error();
        if (ws_find_res) {
            remaining = ws_find_res->f_remaining;
            continue;
        }
        for (const auto& pat : PATTERNS) {
            auto pat_find_res = pat.re.find_in(remaining).ignore_error();
            if (pat_find_res) {
                sa.emplace_back(to_line_range(pat_find_res->f_all),
                                pat.type->value());
                remaining = pat_find_res->f_remaining;
                break;
            }
        }
    }

    string_attrs_t::const_iterator iter;
    int start = 0;

    while ((iter = find_string_attr(sa, &SQL_IDENTIFIER_ATTR, start))
           != sa.end())
    {
        string_attrs_t::const_iterator piter;
        bool found_open = false;
        ssize_t lpc;

        start = iter->sa_range.lr_end;
        for (lpc = iter->sa_range.lr_end; lpc < (int) line.length(); lpc++) {
            if (line[lpc] == '(') {
                found_open = true;
                break;
            }
            if (!isspace(line[lpc])) {
                break;
            }
        }

        if (found_open) {
            ssize_t pstart = lpc + 1;
            int depth = 1;

            while (depth > 0
                   && (piter = find_string_attr(sa, &SQL_PAREN_ATTR, pstart))
                       != sa.end())
            {
                if (line[piter->sa_range.lr_start] == '(') {
                    depth += 1;
                } else {
                    depth -= 1;
                }
                pstart = piter->sa_range.lr_end;
            }

            line_range func_range{iter->sa_range.lr_start};
            if (piter == sa.end()) {
                func_range.lr_end = line.length();
            } else {
                func_range.lr_end = piter->sa_range.lr_end - 1;
            }
            sa.emplace_back(func_range, SQL_FUNCTION_ATTR.value());
        }
    }

    remove_string_attr(sa, &SQL_PAREN_ATTR);
    stable_sort(sa.begin(), sa.end());
}

std::vector<const help_text*>
find_sql_help_for_line(const attr_line_t& al, size_t x)
{
    std::vector<const help_text*> retval;
    const auto& sa = al.get_attrs();
    std::string name;

    x = al.nearest_text(x);

    {
        auto sa_opt = get_string_attr(al.get_attrs(), &SQL_COMMAND_ATTR);

        if (sa_opt) {
            auto* sql_cmd_map = injector::get<readline_context::command_map_t*,
                                              sql_cmd_map_tag>();
            auto cmd_name = al.get_substring((*sa_opt)->sa_range);
            auto cmd_iter = sql_cmd_map->find(cmd_name);

            if (cmd_iter != sql_cmd_map->end()) {
                return {&cmd_iter->second->c_help};
            }
        }
    }

    std::vector<std::string> kw;
    auto iter = rfind_string_attr_if(sa, x, [&al, &name, &kw, x](auto sa) {
        if (sa.sa_type != &SQL_FUNCTION_ATTR && sa.sa_type != &SQL_KEYWORD_ATTR)
        {
            return false;
        }

        const std::string& str = al.get_string();
        const line_range& lr = sa.sa_range;
        int lpc;

        if (sa.sa_type == &SQL_FUNCTION_ATTR) {
            if (!sa.sa_range.contains(x)) {
                return false;
            }
        }

        for (lpc = lr.lr_start; lpc < lr.lr_end; lpc++) {
            if (!isalnum(str[lpc]) && str[lpc] != '_') {
                break;
            }
        }

        auto tmp_name = str.substr(lr.lr_start, lpc - lr.lr_start);
        if (sa.sa_type == &SQL_KEYWORD_ATTR) {
            tmp_name = toupper(tmp_name);
        }
        bool retval = sqlite_function_help.count(tmp_name) > 0;

        if (retval) {
            kw.push_back(tmp_name);
            name = tmp_name;
        }
        return retval;
    });

    if (iter != sa.end()) {
        auto func_pair = sqlite_function_help.equal_range(name);
        size_t help_count = std::distance(func_pair.first, func_pair.second);

        if (help_count > 1 && name != func_pair.first->second->ht_name) {
            while (func_pair.first != func_pair.second) {
                if (find(kw.begin(), kw.end(), func_pair.first->second->ht_name)
                    == kw.end())
                {
                    ++func_pair.first;
                } else {
                    func_pair.second = next(func_pair.first);
                    break;
                }
            }
        }
        for (auto func_iter = func_pair.first; func_iter != func_pair.second;
             ++func_iter)
        {
            retval.emplace_back(func_iter->second);
        }
    }

    return retval;
}
