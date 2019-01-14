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

#include "config.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "logfile.hh"
#include "auto_mem.hh"
#include "lnav_log.hh"
#include "sql_util.hh"
#include "file_vtab.hh"
#include "vtab_module.hh"

using namespace std;

enum {
    RC_COL_MATCH_INDEX,
    RC_COL_INDEX,
    RC_COL_NAME,
    RC_COL_CAPTURE_COUNT,
    RC_COL_RANGE_START,
    RC_COL_RANGE_STOP,
    RC_COL_CONTENT,
    RC_COL_VALUE,
    RC_COL_PATTERN,
};

struct regexp_capture {
    using iterator = vector<logfile *>::iterator;

    static constexpr const char *CREATE_STMT = R"(
-- The regexp_capture() table-valued function allows you to execute a regular-
-- expression over a given string and get the captured data as rows in a table.
CREATE TABLE regexp_capture (
    match_index integer,
    capture_index integer,
    capture_name text,
    capture_count integer,
    range_start integer,
    range_stop integer,
    content text,
    value text HIDDEN,
    pattern text HIDDEN
);
)";

    struct vtab {
        sqlite3_vtab base;

        operator sqlite3_vtab *() {
            return &this->base;
        };
    };

    struct cursor {
        sqlite3_vtab_cursor base;
        unique_ptr<pcrepp> c_pattern;
        pcre_context_static<30> c_context;
        unique_ptr<pcre_input> c_input;
        string c_pattern_string;
        string c_content;
        int c_index;
        int c_start_index;
        bool c_matched;
        int c_match_index;
        sqlite3_int64 c_rowid;

        cursor(sqlite3_vtab *vt)
                : base({vt}),
                  c_index(0),
                  c_start_index(0),
                  c_match_index(0),
                  c_rowid(0) {
            this->c_context.set_count(0);
        };

        int next() {
            if (this->c_index >= (this->c_context.get_count() - 1)) {
                this->c_input->pi_offset = this->c_input->pi_next_offset;
                this->c_matched = this->c_pattern->match(this->c_context, *(this->c_input));
                this->c_index = -1;
                this->c_match_index += 1;
            }

            if (!this->c_pattern || !this->c_matched) {
                return SQLITE_OK;
            }

            this->c_index += 1;

            return SQLITE_OK;
        };

        int eof() {
            return !this->c_pattern || !this->c_matched;
        };

        int get_rowid(sqlite3_int64 &rowid_out) {
            rowid_out = this->c_rowid;

            return SQLITE_OK;
        };
    };

    int get_column(const cursor &vc, sqlite3_context *ctx, int col) {
        pcre_context::capture_t &cap = vc.c_context.all()[vc.c_index];

        switch (col) {
            case RC_COL_MATCH_INDEX:
                sqlite3_result_int64(ctx, vc.c_match_index);
                break;
            case RC_COL_INDEX:
                sqlite3_result_int64(ctx, vc.c_index);
                break;
            case RC_COL_NAME:
                if (vc.c_index == 0) {
                    sqlite3_result_null(ctx);
                } else {
                    sqlite3_result_text(ctx, vc.c_pattern->name_for_capture(
                        vc.c_index - 1), -1, SQLITE_TRANSIENT);
                }
                break;
            case RC_COL_CAPTURE_COUNT:
                sqlite3_result_int64(ctx, vc.c_context.get_count());
                break;
            case RC_COL_RANGE_START:
                sqlite3_result_int64(ctx, cap.c_begin);
                break;
            case RC_COL_RANGE_STOP:
                sqlite3_result_int64(ctx, cap.c_end);
                break;
            case RC_COL_CONTENT:
                if (cap.is_valid()) {
                    sqlite3_result_text(ctx,
                                        vc.c_input->get_substr_start(&cap),
                                        cap.length(),
                                        SQLITE_TRANSIENT);
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            case RC_COL_VALUE:
                sqlite3_result_text(ctx,
                                    vc.c_content.c_str(),
                                    vc.c_content.length(),
                                    SQLITE_TRANSIENT);
                break;
            case RC_COL_PATTERN:
                sqlite3_result_text(ctx,
                                    vc.c_pattern_string.c_str(),
                                    vc.c_pattern_string.length(),
                                    SQLITE_TRANSIENT);
                break;
        }

        return SQLITE_OK;
    }
};

static int rcBestIndex(sqlite3_vtab *tab, sqlite3_index_info *pIdxInfo)
{
    vtab_index_constraints vic(pIdxInfo);
    vtab_index_usage viu(pIdxInfo);

    for (auto iter = vic.begin(); iter != vic.end(); ++iter) {
        if (iter->op != SQLITE_INDEX_CONSTRAINT_EQ) {
            continue;
        }

        switch (iter->iColumn) {
            case RC_COL_VALUE:
            case RC_COL_PATTERN:
                viu.column_used(iter);
                break;
        }
    }

    viu.allocate_args(2);
    return SQLITE_OK;
}

static int rcFilter(sqlite3_vtab_cursor *pVtabCursor,
                    int idxNum, const char *idxStr,
                    int argc, sqlite3_value **argv)
{
    regexp_capture::cursor *pCur = (regexp_capture::cursor *)pVtabCursor;

    if (argc != 2) {
        pCur->c_content.clear();
        pCur->c_pattern.reset();
        return SQLITE_OK;
    }

    const char *value = (const char *) sqlite3_value_text(argv[0]);
    const char *pattern = (const char *) sqlite3_value_text(argv[1]);

    pCur->c_content = value;

    try {
        pCur->c_pattern = make_unique<pcrepp>(pattern);
        pCur->c_pattern_string = pattern;
    } catch (const pcrepp::error &e) {
        pVtabCursor->pVtab->zErrMsg = sqlite3_mprintf(
            "Invalid regular expression: %s", e.e_msg.c_str());
        return SQLITE_ERROR;
    }

    pCur->c_index = 0;
    pCur->c_context.set_count(0);

    pCur->c_input = make_unique<pcre_input>(pCur->c_content);
    pCur->c_matched = pCur->c_pattern->match(pCur->c_context, *(pCur->c_input));

    return SQLITE_OK;
}

int register_regexp_vtab(sqlite3 *db)
{
    static vtab_module<tvt_no_update<regexp_capture>> REGEXP_CAPTURE_MODULE;
    static help_text regexp_capture_help = help_text("regexp_capture",
        "A table-valued function that executes a regular-expression over a "
        "string and returns the captured values.  If the regex only matches a "
        "subset of the input string, it will be rerun on the remaining parts "
        "of the string until no more matches are found.")
        .sql_table_valued_function()
        .with_parameter({"string",
                         "The string to match against the given pattern."})
        .with_parameter({"pattern",
                         "The regular expression to match."})
        .with_result({"match_index",
                      "The match iteration.  This value will increase "
                      "each time a new match is found in the input string."})
        .with_result({"capture_index",
                      "The index of the capture in the regex."})
        .with_result({"capture_name",
                      "The name of the capture in the regex."})
        .with_result({"capture_count",
                      "The total number of captures in the regex."})
        .with_result({"range_start",
                      "The start of the capture in the input string."})
        .with_result({"range_stop",
                      "The stop of the capture in the input string."})
        .with_result({"content",
                      "The captured value from the string."})
        .with_tags({"string"})
        .with_example({
            "SELECT * FROM regexp_capture('a=1; b=2', '(\\w+)=(\\d+)')"});

    int rc;

    REGEXP_CAPTURE_MODULE.vm_module.xBestIndex = rcBestIndex;
    REGEXP_CAPTURE_MODULE.vm_module.xFilter = rcFilter;

    rc = REGEXP_CAPTURE_MODULE.create(db, "regexp_capture");
    sqlite_function_help.insert(make_pair("regexp_capture", &regexp_capture_help));
    regexp_capture_help.index_tags();

    ensure(rc == SQLITE_OK);

    return rc;
}
