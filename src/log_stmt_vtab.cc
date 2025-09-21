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

#include <string>

#include "log_stmt_vtab.hh"

#include "lnav-rs-ext/lnav_rs_ext.cxx.hh"
#include "sql_help.hh"
#include "vtab_module.hh"

namespace {

enum class log_stmt_col : uint8_t {
    begin_line,
    end_line,
    language,
    function_name,
    pattern,
    path,
};

struct log_stmt_table {
    static constexpr const char* NAME = "source_log_stmt";
    static constexpr const char* CREATE_STMT = R"(
-- The source_log_stmt() table-valued function allows you to query
-- the log statements that were extracted from source code added
-- by the :add-source-path command.
CREATE TABLE source_log_stmt (
    begin_line INTEGER,
    end_line INTEGER,
    language TEXT,
    function_name TEXT,
    pattern TEXT,
    path TEXT HIDDEN
);
)";

    struct cursor {
        sqlite3_vtab_cursor base;
        std::string c_path;
        ::rust::Vec<lnav_rs_ext::FindLogResult> c_stmts;
        size_t c_index{0};

        explicit cursor(sqlite3_vtab* vt) : base({vt}) {}

        int next()
        {
            if (this->c_index < this->c_stmts.size()) {
                this->c_index += 1;
            }

            return SQLITE_OK;
        }

        int reset()
        {
            this->c_index = 0;
            return SQLITE_OK;
        }

        int eof() { return this->c_index == this->c_stmts.size(); }

        int get_rowid(sqlite3_int64& rowid_out)
        {
            rowid_out = this->c_index;

            return SQLITE_OK;
        }
    };

    int get_column(const cursor& vc, sqlite3_context* ctx, int col)
    {
        const auto& stmt = vc.c_stmts[vc.c_index];
        switch (log_stmt_col(col)) {
            case log_stmt_col::begin_line:
                to_sqlite(ctx, stmt.src.begin_line);
                break;
            case log_stmt_col::end_line:
                to_sqlite(ctx, stmt.src.end_line);
                break;
            case log_stmt_col::language:
                sqlite3_result_text(ctx,
                                    stmt.src.language.data(),
                                    stmt.src.language.size(),
                                    SQLITE_STATIC);
                break;
            case log_stmt_col::function_name:
                to_sqlite(ctx, (std::string) stmt.src.name);
                break;
            case log_stmt_col::pattern:
                to_sqlite(ctx, (std::string) stmt.pattern);
                break;
            case log_stmt_col::path:
                to_sqlite(ctx, (std::string) stmt.src.file);
                break;
        }

        return SQLITE_OK;
    }
};

int
rcBestIndex(sqlite3_vtab* tab, sqlite3_index_info* pIdxInfo)
{
    vtab_index_constraints vic(pIdxInfo);
    vtab_index_usage viu(pIdxInfo);

    for (auto iter = vic.begin(); iter != vic.end(); ++iter) {
        if (iter->op != SQLITE_INDEX_CONSTRAINT_EQ) {
            continue;
        }

        switch (log_stmt_col(iter->iColumn)) {
            case log_stmt_col::path:
                viu.column_used(iter);
                break;
            default:
                break;
        }
    }

    viu.allocate_args(lnav::enums::to_underlying(log_stmt_col::path),
                      lnav::enums::to_underlying(log_stmt_col::path),
                      1);
    return SQLITE_OK;
}

int
rcFilter(sqlite3_vtab_cursor* pVtabCursor,
         int idxNum,
         const char* idxStr,
         int argc,
         sqlite3_value** argv)
{
    auto* pCur = (log_stmt_table::cursor*) pVtabCursor;

    if (argc != 1) {
        pCur->c_path.clear();
        return SQLITE_OK;
    }

    const char* path = (const char*) sqlite3_value_text(argv[0]);
    pCur->c_path = path;
    pCur->c_index = 0;
    pCur->c_stmts = lnav_rs_ext::get_log_statements_for(path);

    return SQLITE_OK;
}

}  // namespace

int
register_log_stmt_vtab(sqlite3* db)
{
    static vtab_module<tvt_no_update<log_stmt_table>> LOG_STMT_MODULE;
    static auto log_stmt_help
        = help_text("source_log_stmt",
                    "A table-valued function for getting information about log "
                    "statements "
                    "that were found in source code added by the "
                    ":add-source-path command.")
              .sql_table_valued_function()
              .with_parameter({"path", "The source file path"})
              .with_result(
                  {"begin_line", "The line number where the statement begins"})
              .with_result(
                  {"end_line", "The line number where the statement ends"})
              .with_result({"language", "The language of the source code"})
              .with_result(
                  {"function_name",
                   "The name of the function containing the log statement"})
              .with_result({"pattern",
                            "The pattern used to match log messages from this "
                            "log statement"})
              .with_result({"path", "The path to the source file"});

    LOG_STMT_MODULE.vm_module.xBestIndex = rcBestIndex;
    LOG_STMT_MODULE.vm_module.xFilter = rcFilter;

    auto rc = LOG_STMT_MODULE.create(db, "source_log_stmt");
    sqlite_function_help.emplace("source_log_stmt", &log_stmt_help);
    log_stmt_help.index_tags();

    ensure(rc == SQLITE_OK);

    return rc;
}
