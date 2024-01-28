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

#ifdef __CYGWIN__
#    include <alloca.h>
#endif

#include "base/lnav.console.into.hh"
#include "base/lnav_log.hh"
#include "column_namer.hh"
#include "config.h"
#include "lnav_util.hh"
#include "pcrepp/pcre2pp.hh"
#include "scn/scn.h"
#include "sql_help.hh"
#include "sql_util.hh"
#include "sqlitepp.hh"
#include "vtab_module.hh"
#include "yajlpp/yajlpp.hh"
#include "yajlpp/yajlpp_def.hh"

namespace {

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
    static constexpr const char* NAME = "regexp_capture";
    static constexpr const char* CREATE_STMT = R"(
-- The regexp_capture() table-valued function allows you to execute a regular-
-- expression over a given string and get the captured data as rows in a table.
CREATE TABLE regexp_capture (
    match_index INTEGER,
    capture_index INTEGER,
    capture_name TEXT,
    capture_count INTEGER,
    range_start INTEGER,
    range_stop INTEGER,
    content TEXT,
    value TEXT HIDDEN,
    pattern TEXT HIDDEN
);
)";

    struct cursor {
        sqlite3_vtab_cursor base;
        std::shared_ptr<lnav::pcre2pp::code> c_pattern;
        lnav::pcre2pp::match_data c_match_data{
            lnav::pcre2pp::match_data::unitialized()};
        std::string c_content;
        string_fragment c_remaining;
        bool c_content_as_blob{false};
        int c_index{0};
        bool c_matched{false};
        int c_match_index{0};
        sqlite3_int64 c_rowid{0};

        cursor(sqlite3_vtab* vt) : base({vt}) {}

        int reset() { return SQLITE_OK; }

        int next()
        {
            if (this->c_index >= (this->c_match_data.get_count() - 1)) {
                auto match_res = this->c_pattern->capture_from(this->c_content)
                                     .at(this->c_remaining)
                                     .into(this->c_match_data)
                                     .matches(PCRE2_NO_UTF_CHECK)
                                     .ignore_error();
                if (match_res) {
                    this->c_remaining = match_res->f_remaining;
                }
                this->c_matched = match_res.has_value();
                this->c_index = -1;
                this->c_match_index += 1;
            }

            if (!this->c_matched) {
                return SQLITE_OK;
            }

            this->c_index += 1;

            return SQLITE_OK;
        }

        int eof() { return this->c_pattern == nullptr || !this->c_matched; }

        int get_rowid(sqlite3_int64& rowid_out)
        {
            rowid_out = this->c_rowid;

            return SQLITE_OK;
        }
    };

    int get_column(const cursor& vc, sqlite3_context* ctx, int col)
    {
        const auto cap = vc.c_match_data[vc.c_index];

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
                    to_sqlite(ctx,
                              vc.c_pattern->get_name_for_capture(vc.c_index));
                }
                break;
            case RC_COL_CAPTURE_COUNT:
                sqlite3_result_int64(ctx, vc.c_match_data.get_count());
                break;
            case RC_COL_RANGE_START:
                if (cap.has_value()) {
                    sqlite3_result_int64(ctx, cap->sf_begin + 1);
                } else {
                    sqlite3_result_int64(ctx, 0);
                }
                break;
            case RC_COL_RANGE_STOP:
                if (cap.has_value()) {
                    sqlite3_result_int64(ctx, cap->sf_end + 1);
                } else {
                    sqlite3_result_int64(ctx, 0);
                }
                break;
            case RC_COL_CONTENT:
                if (cap.has_value()) {
                    to_sqlite(ctx, cap.value());
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            case RC_COL_VALUE:
                if (vc.c_content_as_blob) {
                    sqlite3_result_blob64(ctx,
                                          vc.c_content.c_str(),
                                          vc.c_content.length(),
                                          SQLITE_STATIC);
                } else {
                    sqlite3_result_text(ctx,
                                        vc.c_content.c_str(),
                                        vc.c_content.length(),
                                        SQLITE_STATIC);
                }
                break;
            case RC_COL_PATTERN: {
                to_sqlite(ctx, vc.c_pattern->get_pattern());
                break;
            }
        }

        return SQLITE_OK;
    }
};

static int
rcBestIndex(sqlite3_vtab* tab, sqlite3_index_info* pIdxInfo)
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

    viu.allocate_args(RC_COL_VALUE, RC_COL_PATTERN, 2);
    return SQLITE_OK;
}

static int
rcFilter(sqlite3_vtab_cursor* pVtabCursor,
         int idxNum,
         const char* idxStr,
         int argc,
         sqlite3_value** argv)
{
    regexp_capture::cursor* pCur = (regexp_capture::cursor*) pVtabCursor;

    if (argc != 2) {
        pCur->c_content.clear();
        pCur->c_pattern.reset();
        return SQLITE_OK;
    }

    auto byte_count = sqlite3_value_bytes(argv[0]);
    auto blob = (const char*) sqlite3_value_blob(argv[0]);

    pCur->c_content_as_blob = (sqlite3_value_type(argv[0]) == SQLITE_BLOB);
    pCur->c_content.assign(blob, byte_count);

    auto pattern = from_sqlite<string_fragment>()(argc, argv, 1);
    auto compile_res = lnav::pcre2pp::code::from(pattern);
    if (compile_res.isErr()) {
        static const intern_string_t PATTERN_SRC
            = intern_string::lookup("pattern");

        set_vtable_errmsg(pVtabCursor->pVtab,
                          lnav::console::to_user_message(
                              PATTERN_SRC, compile_res.unwrapErr()));
        return SQLITE_ERROR;
    }

    pCur->c_pattern = compile_res.unwrap().to_shared();

    pCur->c_index = 0;
    pCur->c_match_data = pCur->c_pattern->create_match_data();

    pCur->c_remaining.clear();
    auto match_res = pCur->c_pattern->capture_from(pCur->c_content)
                         .into(pCur->c_match_data)
                         .matches(PCRE2_NO_UTF_CHECK)
                         .ignore_error();
    if (match_res) {
        pCur->c_remaining = match_res->f_remaining;
    }
    pCur->c_matched = match_res.has_value();
    pCur->c_match_index = 0;

    return SQLITE_OK;
}

enum {
    RCJ_COL_MATCH_INDEX,
    RCJ_COL_CONTENT,
    RCJ_COL_VALUE,
    RCJ_COL_PATTERN,
    RCJ_COL_FLAGS,
};

struct regexp_capture_flags {
    bool convert_numbers{true};
};

const typed_json_path_container<regexp_capture_flags>
    regexp_capture_flags_handlers
    = typed_json_path_container<regexp_capture_flags>{
        yajlpp::property_handler("convert-numbers")
            .for_field(&regexp_capture_flags::convert_numbers),
    };

struct regexp_capture_into_json {
    static constexpr const char* NAME = "regexp_capture_into_json";
    static constexpr const char* CREATE_STMT = R"(
-- The regexp_capture_into_json() table-valued function allows you to execute a
-- regular-expression over a given string and get the captured data as rows in
-- a table.
CREATE TABLE regexp_capture_into_json (
    match_index INTEGER,
    content TEXT,
    value TEXT HIDDEN,
    pattern TEXT HIDDEN,
    flags TEXT HIDDEN
);
)";

    struct cursor {
        sqlite3_vtab_cursor base;
        std::shared_ptr<lnav::pcre2pp::code> c_pattern;
        lnav::pcre2pp::match_data c_match_data{
            lnav::pcre2pp::match_data::unitialized()};
        std::unique_ptr<column_namer> c_namer;
        std::string c_content;
        string_fragment c_remaining;
        bool c_content_as_blob{false};
        bool c_matched{false};
        size_t c_match_index{0};
        sqlite3_int64 c_rowid{0};
        std::string c_flags_string;
        nonstd::optional<regexp_capture_flags> c_flags;

        cursor(sqlite3_vtab* vt) : base({vt}) {}

        int reset() { return SQLITE_OK; }

        int next()
        {
            auto match_res = this->c_pattern->capture_from(this->c_content)
                                 .at(this->c_remaining)
                                 .into(this->c_match_data)
                                 .matches(PCRE2_NO_UTF_CHECK)
                                 .ignore_error();
            if (match_res) {
                this->c_remaining = match_res->f_remaining;
            }
            this->c_matched = match_res.has_value();
            this->c_match_index += 1;

            if (!this->c_matched) {
                return SQLITE_OK;
            }

            return SQLITE_OK;
        }

        int eof() { return this->c_pattern == nullptr || !this->c_matched; }

        int get_rowid(sqlite3_int64& rowid_out)
        {
            rowid_out = this->c_rowid;

            return SQLITE_OK;
        }
    };

    int get_column(const cursor& vc, sqlite3_context* ctx, int col)
    {
        switch (col) {
            case RCJ_COL_MATCH_INDEX:
                sqlite3_result_int64(ctx, vc.c_match_index);
                break;
            case RCJ_COL_CONTENT: {
                yajlpp_gen gen;
                yajl_gen_config(gen, yajl_gen_beautify, false);

                {
                    yajlpp_map root_map(gen);

                    for (size_t lpc = 1; lpc < vc.c_match_data.get_count();
                         lpc++)
                    {
                        const auto& colname = vc.c_namer->cn_names[lpc];
                        const auto cap = vc.c_match_data[lpc];

                        if (!cap) {
                            continue;
                        }

                        yajl_gen_pstring(gen, colname.data(), colname.length());

                        if (!vc.c_flags || vc.c_flags->convert_numbers) {
                            auto cap_view = cap->to_string_view();
                            auto scan_int_res
                                = scn::scan_value<int64_t>(cap_view);

                            if (scan_int_res && scan_int_res.range().empty()) {
                                yajl_gen_integer(gen, scan_int_res.value());
                                continue;
                            }

                            auto scan_float_res
                                = scn::scan_value<double>(cap_view);
                            if (scan_float_res
                                && scan_float_res.range().empty())
                            {
                                yajl_gen_number(
                                    gen, cap_view.data(), cap_view.length());
                                continue;
                            }

                            yajl_gen_pstring(
                                gen, cap_view.data(), cap_view.length());
                        } else {
                            yajl_gen_pstring(gen, cap->data(), cap->length());
                        }
                    }
                }

                auto sf = gen.to_string_fragment();
                sqlite3_result_text(
                    ctx, sf.data(), sf.length(), SQLITE_TRANSIENT);
                sqlite3_result_subtype(ctx, JSON_SUBTYPE);
                break;
            }
            case RCJ_COL_VALUE:
                if (vc.c_content_as_blob) {
                    sqlite3_result_blob64(ctx,
                                          vc.c_content.c_str(),
                                          vc.c_content.length(),
                                          SQLITE_STATIC);
                } else {
                    sqlite3_result_text(ctx,
                                        vc.c_content.c_str(),
                                        vc.c_content.length(),
                                        SQLITE_STATIC);
                }
                break;
            case RCJ_COL_PATTERN: {
                to_sqlite(ctx, vc.c_pattern->get_pattern());
                break;
            }
            case RCJ_COL_FLAGS: {
                if (!vc.c_flags) {
                    sqlite3_result_null(ctx);
                } else {
                    to_sqlite(ctx, vc.c_flags_string);
                }
                break;
            }
        }

        return SQLITE_OK;
    }
};

static int
rcjBestIndex(sqlite3_vtab* tab, sqlite3_index_info* pIdxInfo)
{
    vtab_index_constraints vic(pIdxInfo);
    vtab_index_usage viu(pIdxInfo);

    for (auto iter = vic.begin(); iter != vic.end(); ++iter) {
        if (iter->op != SQLITE_INDEX_CONSTRAINT_EQ) {
            continue;
        }

        switch (iter->iColumn) {
            case RCJ_COL_VALUE:
            case RCJ_COL_PATTERN:
            case RCJ_COL_FLAGS:
                viu.column_used(iter);
                break;
        }
    }

    viu.allocate_args(RCJ_COL_VALUE, RCJ_COL_FLAGS, 2);
    return SQLITE_OK;
}

static int
rcjFilter(sqlite3_vtab_cursor* pVtabCursor,
          int idxNum,
          const char* idxStr,
          int argc,
          sqlite3_value** argv)
{
    auto* pCur = (regexp_capture_into_json::cursor*) pVtabCursor;

    if (argc < 2 || argc > 3) {
        pCur->c_content.clear();
        pCur->c_pattern.reset();
        pCur->c_flags_string.clear();
        pCur->c_flags = nonstd::nullopt;
        return SQLITE_OK;
    }

    auto byte_count = sqlite3_value_bytes(argv[0]);
    const auto* blob = (const char*) sqlite3_value_blob(argv[0]);

    pCur->c_content_as_blob = (sqlite3_value_type(argv[0]) == SQLITE_BLOB);
    pCur->c_content.assign(blob, byte_count);

    auto pattern = from_sqlite<string_fragment>()(argc, argv, 1);
    auto compile_res = lnav::pcre2pp::code::from(pattern);
    if (compile_res.isErr()) {
        static const intern_string_t PATTERN_SRC
            = intern_string::lookup("pattern");

        set_vtable_errmsg(pVtabCursor->pVtab,
                          lnav::console::to_user_message(
                              PATTERN_SRC, compile_res.unwrapErr()));
        return SQLITE_ERROR;
    }

    pCur->c_flags_string.clear();
    pCur->c_flags = nonstd::nullopt;
    if (argc == 3) {
        static const intern_string_t FLAGS_SRC = intern_string::lookup("flags");
        const auto flags_json = from_sqlite<string_fragment>()(argc, argv, 2);

        if (!flags_json.empty()) {
            const auto parse_res
                = regexp_capture_flags_handlers.parser_for(FLAGS_SRC).of(
                    flags_json);

            if (parse_res.isErr()) {
                auto um = lnav::console::user_message::error(
                              "unable to parse flags")
                              .with_reason(parse_res.unwrapErr()[0]);

                set_vtable_errmsg(pVtabCursor->pVtab, um);
                return SQLITE_ERROR;
            }

            pCur->c_flags_string = flags_json.to_string();
            pCur->c_flags = parse_res.unwrap();
        }
    }

    pCur->c_pattern = compile_res.unwrap().to_shared();
    pCur->c_namer
        = std::make_unique<column_namer>(column_namer::language::JSON);
    pCur->c_namer->add_column(string_fragment::from_const("__all__"));
    for (size_t lpc = 1; lpc <= pCur->c_pattern->get_capture_count(); lpc++) {
        pCur->c_namer->add_column(string_fragment::from_c_str(
            pCur->c_pattern->get_name_for_capture(lpc)));
    }

    pCur->c_match_data = pCur->c_pattern->create_match_data();
    pCur->c_remaining.clear();
    auto match_res = pCur->c_pattern->capture_from(pCur->c_content)
                         .into(pCur->c_match_data)
                         .matches(PCRE2_NO_UTF_CHECK)
                         .ignore_error();
    if (match_res) {
        pCur->c_remaining = match_res->f_remaining;
    }
    pCur->c_matched = match_res.has_value();
    pCur->c_match_index = 0;

    return SQLITE_OK;
}

}  // namespace

int
register_regexp_vtab(sqlite3* db)
{
    static vtab_module<tvt_no_update<regexp_capture>> REGEXP_CAPTURE_MODULE;
    static help_text regexp_capture_help
        = help_text("regexp_capture",
                    "A table-valued function that executes a "
                    "regular-expression over a "
                    "string and returns the captured values.  If the regex "
                    "only matches a "
                    "subset of the input string, it will be rerun on the "
                    "remaining parts "
                    "of the string until no more matches are found.")
              .sql_table_valued_function()
              .with_parameter(
                  {"string", "The string to match against the given pattern."})
              .with_parameter({"pattern", "The regular expression to match."})
              .with_result({
                  "match_index",
                  "The match iteration.  This value will increase "
                  "each time a new match is found in the input string.",
              })
              .with_result(
                  {"capture_index", "The index of the capture in the regex."})
              .with_result(
                  {"capture_name", "The name of the capture in the regex."})
              .with_result({"capture_count",
                            "The total number of captures in the regex."})
              .with_result({"range_start",
                            "The start of the capture in the input string."})
              .with_result({"range_stop",
                            "The stop of the capture in the input string."})
              .with_result({"content", "The captured value from the string."})
              .with_tags({"string"})
              .with_example({
                  "To extract the key/value pairs 'a'/1 and 'b'/2 "
                  "from the string 'a=1; b=2'",
                  "SELECT * FROM regexp_capture('a=1; b=2', "
                  "'(\\w+)=(\\d+)')",
              });

    int rc;

    REGEXP_CAPTURE_MODULE.vm_module.xBestIndex = rcBestIndex;
    REGEXP_CAPTURE_MODULE.vm_module.xFilter = rcFilter;

    rc = REGEXP_CAPTURE_MODULE.create(db, "regexp_capture");
    sqlite_function_help.insert(
        std::make_pair("regexp_capture", &regexp_capture_help));
    regexp_capture_help.index_tags();

    ensure(rc == SQLITE_OK);

    static vtab_module<tvt_no_update<regexp_capture_into_json>>
        REGEXP_CAPTURE_INTO_JSON_MODULE;
    static help_text regexp_capture_into_json_help
        = help_text(
              "regexp_capture_into_json",
              "A table-valued function that executes a "
              "regular-expression over a string and returns the captured "
              "values as a JSON object.  If the regex only matches a "
              "subset of the input string, it will be rerun on the "
              "remaining parts of the string until no more matches are found.")
              .sql_table_valued_function()
              .with_parameter(
                  {"string", "The string to match against the given pattern."})
              .with_parameter({"pattern", "The regular expression to match."})
              .with_parameter(help_text{
                  "options",
                  "A JSON object with the following option: "
                  "convert-numbers - True (default) if text that looks like "
                  "numeric data should be converted to JSON numbers, "
                  "false if they should be captured as strings."}
                                  .optional())
              .with_result({
                  "match_index",
                  "The match iteration.  This value will increase "
                  "each time a new match is found in the input string.",
              })
              .with_result({"content", "The captured values from the string."})
              .with_tags({"string"})
              .with_example({
                  "To extract the key/value pairs 'a'/1 and 'b'/2 "
                  "from the string 'a=1; b=2'",
                  "SELECT * FROM regexp_capture_into_json('a=1; b=2', "
                  "'(\\w+)=(\\d+)')",
              });

    REGEXP_CAPTURE_INTO_JSON_MODULE.vm_module.xBestIndex = rcjBestIndex;
    REGEXP_CAPTURE_INTO_JSON_MODULE.vm_module.xFilter = rcjFilter;

    rc = REGEXP_CAPTURE_INTO_JSON_MODULE.create(db, "regexp_capture_into_json");
    sqlite_function_help.insert(std::make_pair("regexp_capture_into_json",
                                               &regexp_capture_into_json_help));
    regexp_capture_into_json_help.index_tags();

    ensure(rc == SQLITE_OK);

    return rc;
}
