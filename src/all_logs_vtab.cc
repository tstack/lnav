/**
 * Copyright (c) 2020, Timothy Stack
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

#include "all_logs_vtab.hh"

#include "base/attr_line.hh"
#include "base/intern_string.hh"
#include "config.h"
#include "data_parser.hh"
#include "elem_to_json.hh"
#include "hasher.hh"
#include "scn/scan.h"
#include "vtab_module.hh"
#include "yajlpp/yajlpp.hh"

#ifdef HAVE_RUST_DEPS
#    include "lnav_rs_ext.cxx.hh"
#endif

static auto intern_lifetime = intern_string::get_table_lifetime();

/**
 * log_msg_schema_kind's values, interned so vt_column() hands them to SQLite
 * without a copy.
 */
static const intern_string_t KIND_STATEMENT
    = intern_string::lookup("statement");
static const intern_string_t KIND_SRC_LOCATION
    = intern_string::lookup("src_location");
static const intern_string_t KIND_MESSAGE
    = intern_string::lookup("message");

all_logs_vtab::schema_lines_t all_logs_vtab::alv_schema_lines = nullptr;
all_logs_vtab::line_stmt_t all_logs_vtab::alv_line_stmt = nullptr;
all_logs_vtab::scan_started_t all_logs_vtab::alv_scan_started = nullptr;

void
all_logs_vtab::filter(log_cursor& lc, logfile_sub_source& lss)
{
    if (alv_scan_started != nullptr) {
        alv_scan_started();
    }
}

void
all_logs_vtab::lines_for_values(int col,
                                const std::vector<std::string>& values,
                                logfile_sub_source& lss,
                                std::vector<vis_line_t>& lines_out,
                                std::vector<std::string>& unknown_out)
{
    if (!this->knows_lines_for(col)) {
        unknown_out.insert(unknown_out.end(), values.begin(), values.end());
        return;
    }

    alv_schema_lines(values, lss, lines_out, unknown_out);
}

bool
all_logs_vtab::knows_lines_for(int col) const
{
    return alv_schema_lines != nullptr
        && col
        == VT_COL_MAX
            + (int) this->alv_schema_meta.lvm_column
                  .get<logline_value_meta::table_column>()
                  .value;
}

bool
all_logs_vtab::cached_column(int col,
                             logfile* lf,
                             uint64_t line_number,
                             sqlite3_context* ctx)
{
    if (alv_line_stmt == nullptr) {
        return false;
    }

    const auto table_col = [](const logline_value_meta& lvm) {
        return VT_COL_MAX
            + (int) lvm.lvm_column.get<logline_value_meta::table_column>()
                  .value;
    };
    const auto is_src = col == table_col(this->alv_src_meta);
    const auto is_kind = col == table_col(this->alv_schema_kind_meta);
    if (!is_src && !is_kind && col != table_col(this->alv_schema_meta)) {
        return false;
    }

    // What the statement map knows, as extract() takes it.
    auto stmt = alv_line_stmt(lf, line_number);
    if (is_kind) {
        const auto set_kind = [ctx](const intern_string_t& kind) {
            sqlite3_result_text(ctx, kind.get(), kind.size(), SQLITE_STATIC);
            return true;
        };
        if (stmt.is<line_stmt_mapped>()) {
            return set_kind(KIND_STATEMENT);
        }
        // Without source fields in the format, an unmatched message's schema
        // comes from its text; with them, it depends on whether this message
        // has both, which takes reading it.
        if (stmt.is<line_stmt_unmatched>()
            && !lf->get_format_ptr()->lf_has_src_location)
        {
            return set_kind(KIND_MESSAGE);
        }
        return false;
    }
    if (stmt.is<line_stmt_mapped>()) {
        // The statement map's strings are never freed or changed, so SQLite
        // can take them without a copy.
        const auto& mapped = stmt.get<line_stmt_mapped>();
        const auto& str = is_src ? *mapped.lsm_src : *mapped.lsm_schema;
        sqlite3_result_text(ctx, str.data(), str.size(), SQLITE_STATIC);
        if (is_src) {
            sqlite3_result_subtype(ctx, JSON_SUBTYPE);
        }
        return true;
    }
    if (is_src && stmt.is<line_stmt_unmatched>()) {
        // No statement.  The schema of such a message comes from its text.
        sqlite3_result_null(ctx);
        return true;
    }

    return false;
}

all_logs_vtab::all_logs_vtab()
    : log_vtab_impl(intern_string::lookup("all_logs")),
      alv_msg_meta(intern_string::lookup("log_msg_format"),
                   value_kind_t::VALUE_TEXT,
                   logline_value_meta::table_column{0}),
      alv_schema_meta(intern_string::lookup("log_msg_schema"),
                      value_kind_t::VALUE_TEXT,
                      logline_value_meta::table_column{1}),
      alv_values_meta(intern_string::lookup("log_msg_values"),
                      value_kind_t::VALUE_JSON,
                      logline_value_meta::table_column{2}),
      alv_src_meta(intern_string::lookup("log_msg_src"),
                   value_kind_t::VALUE_JSON,
                   logline_value_meta::table_column{3}),
      alv_stacktrace_meta(intern_string::lookup("log_stack_trace"),
                          value_kind_t::VALUE_TEXT,
                          logline_value_meta::table_column{4}),
      alv_format_values_meta(intern_string::lookup("log_format_values"),
                             value_kind_t::VALUE_JSON,
                             logline_value_meta::table_column{5}),
      alv_schema_kind_meta(intern_string::lookup("log_msg_schema_kind"),
                           value_kind_t::VALUE_TEXT,
                           logline_value_meta::table_column{6})
{
    this->alv_msg_meta.lvm_identifier = true;
    this->alv_schema_meta.lvm_identifier = true;
}

void
all_logs_vtab::get_columns(std::vector<vtab_column>& cols) const
{
    cols.emplace_back(
        vtab_column(this->alv_msg_meta.lvm_name)
            .with_comment(
                "The message format with variables replaced by hash marks"_frag));
    cols.emplace_back(this->alv_schema_meta.lvm_name,
                      SQLITE3_TEXT,
                      intern_string_t{},
                      true,
                      "The ID for the message schema"_frag);
    cols.emplace_back(this->alv_values_meta.lvm_name,
                      SQLITE3_TEXT,
                      intern_string_t{},
                      false,
                      "The values extracted from the message"_frag);
    cols.emplace_back(this->alv_src_meta.lvm_name,
                      SQLITE3_TEXT,
                      intern_string_t{},
                      false,
                      "The source code that generated this message"_frag);
    cols.emplace_back(this->alv_stacktrace_meta.lvm_name,
                      SQLITE3_TEXT,
                      intern_string_t{},
                      false,
                      "If found, the stack trace details"_frag);
    // Hidden, so SELECT * isn't widened by a copy of every message's values.
    cols.emplace_back(
        this->alv_format_values_meta.lvm_name,
        SQLITE3_TEXT,
        intern_string_t{},
        true,
        "The non-null values the message's log format extracted, as a JSON object"_frag);
    // Hidden like log_msg_schema.
    cols.emplace_back(
        this->alv_schema_kind_meta.lvm_name,
        SQLITE3_TEXT,
        intern_string_t{},
        true,
        "Where log_msg_schema comes from: 'statement' (a log statement in the source), 'src_location' (the format's source file and line), or 'message' (the message's text)"_frag);
}

/**
 * The values the format extracted from a message, as a JSON object of the
 * non-null ones, the way :write-json-to writes them: numbers and booleans keep
 * their type, JSON values go in as JSON, and hidden values are left out.
 */
static std::string
format_values_json(const logline_value_vector& values)
{
    yajlpp_gen gen;
    yajl_gen_config(gen, yajl_gen_beautify, false);
    {
        yajlpp_map obj(gen);
        for (const auto& lv : values.lvv_values) {
            if (lv.lv_meta.lvm_hidden
                || lv.lv_meta.lvm_column
                       .is<logline_value_meta::internal_column>()
                || lv.lv_meta.lvm_kind == value_kind_t::VALUE_NULL)
            {
                continue;
            }

            obj.gen(lv.lv_meta.lvm_name.get());
            switch (lv.lv_meta.lvm_kind) {
                case value_kind_t::VALUE_BOOLEAN:
                    obj.gen(lv.lv_value.i ? true : false);
                    break;
                case value_kind_t::VALUE_INTEGER:
                    obj.gen(lv.lv_value.i);
                    break;
                case value_kind_t::VALUE_FLOAT:
                    obj.gen(lv.lv_value.d);
                    break;
                case value_kind_t::VALUE_JSON:
                    yajl_gen_number(gen, lv.text_value(), lv.text_length());
                    break;
                default:
                    obj.gen(lv.text_value(), lv.text_length());
                    break;
            }
        }
    }
    return gen.to_string_fragment().to_string();
}

void
all_logs_vtab::extract(const log_cursor::extract_context& ec,
                       logfile* lf,
                       uint64_t line_number,
                       string_attrs_t& sa,
                       logline_value_vector& values)
{
    auto& line = values.lvv_sbr;
    auto* format = lf->get_format_ptr();

    logline_value_vector sub_values;

    sa.clear();
    sub_values.lvv_sbr = line.clone();
    format->annotate(lf, line_number, sa, sub_values);

    values.lvv_thread_id_value
        = to_owned(sub_values.lvv_thread_id_value, values.lvv_allocator);
    values.lvv_opid_value = std::move(sub_values.lvv_opid_value);
    values.lvv_opid_provenance = sub_values.lvv_opid_provenance;
    values.lvv_src_file_value
        = to_owned(sub_values.lvv_src_file_value, values.lvv_allocator);
    values.lvv_src_line_value
        = to_owned(sub_values.lvv_src_line_value, values.lvv_allocator);
    values.lvv_duration_value = sub_values.lvv_duration_value;

    if (ec.is_column_used(this->alv_format_values_meta)) {
        values.lvv_values.emplace_back(this->alv_format_values_meta,
                                       format_values_json(sub_values));
    }

    if (!ec.are_columns_used(this->alv_msg_meta,
                             this->alv_schema_meta,
                             this->alv_values_meta,
                             this->alv_src_meta,
                             this->alv_stacktrace_meta,
                             this->alv_schema_kind_meta))
    {
        return;
    }
    // Without a statement, the schema comes from the format's source file
    // and line, when the message has both, else from its text.
    const auto unmatched_kind = sub_values.lvv_src_file_value
            && sub_values.lvv_src_line_value
        ? KIND_SRC_LOCATION
        : KIND_MESSAGE;

    // The statement map already matched most messages, so take what it knows
    // instead of matching the message again.  The pattern, the variables, and
    // the stack trace aren't kept there, so they still need log2src.
    auto stmt = alv_line_stmt != nullptr ? alv_line_stmt(lf, line_number)
                                         : line_stmt{line_stmt_unmapped{}};
    if (stmt.is<line_stmt_mapped>()
        && !ec.are_columns_used(this->alv_msg_meta,
                                this->alv_values_meta,
                                this->alv_stacktrace_meta))
    {
        const auto& mapped = stmt.get<line_stmt_mapped>();
        // The statement map's strings are never freed or changed.
        values.lvv_values.emplace_back(
            this->alv_schema_meta,
            permanent_fragment{string_fragment::from(*mapped.lsm_schema)});
        values.lvv_values.emplace_back(
            this->alv_src_meta,
            permanent_fragment{string_fragment::from(*mapped.lsm_src)});
        values.lvv_values.emplace_back(this->alv_schema_kind_meta,
                                       KIND_STATEMENT);
        return;
    }
    const auto unmatched = stmt.is<line_stmt_unmatched>();
    if (unmatched
        && !ec.are_columns_used(
            this->alv_msg_meta, this->alv_schema_meta, this->alv_values_meta))
    {
        // No statement, so no log_msg_src or stack trace.
        values.lvv_values.emplace_back(this->alv_schema_kind_meta,
                                       unmatched_kind);
        return;
    }

    auto body = find_string_attr_range(sa, &SA_BODY);
    if (!body.is_valid()) {
        body.lr_start = 0;
        body.lr_end = line.length();
    }
    auto body_sf = line.to_string_fragment(body);
    auto src_file_sf = sub_values.lvv_src_file_value;
    auto src_line_sf = sub_values.lvv_src_line_value;
    auto h = hasher();
#ifdef HAVE_RUST_DEPS
    auto find_res = unmatched
        ? nullptr
        : find_statement(line,
                         sa,
                         sub_values,
                         lf->begin()[line_number].get_msg_level(),
                         ec.is_column_used(this->alv_values_meta));
    if (find_res != nullptr) {
        char schema_buf[hasher::STRING_SIZE];
        statement_schema(string_fragment::from(find_res->src),
                         string_fragment::from(find_res->pattern),
                         schema_buf);
        values.lvv_values.emplace_back(this->alv_msg_meta,
                                       (std::string) find_res->pattern);
        values.lvv_values.emplace_back(
            this->alv_schema_meta,
            string_fragment::from(schema_buf).to_string());
        values.lvv_values.emplace_back(this->alv_values_meta,
                                       (std::string) find_res->variables);
        values.lvv_values.emplace_back(this->alv_src_meta,
                                       (std::string) find_res->src);
        values.lvv_values.emplace_back(this->alv_schema_kind_meta,
                                       KIND_STATEMENT);
        if (!find_res->stack_trace.empty()) {
            values.lvv_values.emplace_back(this->alv_stacktrace_meta,
                                           (std::string) find_res->stack_trace);
        }
    } else
#endif
    {
        data_scanner ds(body_sf);
        data_parser dp(&ds);
        std::string str;

        dp.dp_msg_format = &str;
        dp.parse();

        yajlpp_gen gen;
        yajl_gen_config(gen, yajl_gen_beautify, false);

        elements_to_json(gen, dp, &dp.dp_pairs);

        if (src_file_sf && src_line_sf) {
            h.update(format->get_name().c_str());
            h.update(src_file_sf.value());
            h.update(src_line_sf.value());
        }
        auto schema_id = (src_file_sf && src_line_sf)
            ? h.to_string()
            : dp.dp_schema_id.to_string();
        values.lvv_values.emplace_back(this->alv_msg_meta, std::move(str));
        values.lvv_values.emplace_back(this->alv_schema_meta, schema_id);
        values.lvv_values.emplace_back(this->alv_schema_kind_meta,
                                       unmatched_kind);
        values.lvv_values.emplace_back(
            this->alv_values_meta,
            json_string(gen).to_string_fragment().to_string());
    }
}

void
all_logs_vtab::statement_schema(const string_fragment& src_json,
                                const string_fragment& pattern,
                                char buf[hasher::STRING_SIZE])
{
    hasher h;
    h.update(src_json);
    h.update(pattern);
    h.to_string(buf);
}

#ifdef HAVE_RUST_DEPS
std::unique_ptr<lnav_rs_ext::FindLogResultJson>
all_logs_vtab::find_statement(const shared_buffer_ref& sbr,
                              const string_attrs_t& sa,
                              const logline_value_vector& values,
                              log_level_t level,
                              bool extract_variables)
{
    if (!sbr.get_metadata().m_valid_utf) {
        return nullptr;
    }

    auto body = find_string_attr_range(sa, &SA_BODY);
    if (!body.is_valid()) {
        body.lr_start = 0;
        body.lr_end = sbr.length();
    }
    auto body_sf = sbr.to_string_fragment(body);

    auto file_rust_str = rust::Str();
    auto lineno = 0UL;
    if (values.lvv_src_file_value) {
        file_rust_str = rust::Str(values.lvv_src_file_value->data(),
                                  values.lvv_src_file_value->length());
    }
    if (values.lvv_src_line_value) {
        auto scan_res = scn::scan_int<decltype(lineno)>(
            values.lvv_src_line_value->to_string_view());
        if (scan_res) {
            lineno = scan_res->value();
        }
    }

    auto find_options = lnav_rs_ext::FindLogOptions{
        .extract_variables = extract_variables,
    };
    const auto& level_name = level_names[level];
    return lnav_rs_ext::find_log_statement_json(
        file_rust_str,
        lineno,
        rust::Str(level_name.data(), level_name.length()),
        rust::Str(body_sf.data(), body_sf.length()),
        find_options);
}
#endif

bool
all_logs_vtab::next(log_cursor& lc, logfile_sub_source& lss)
{
    return true;
}
