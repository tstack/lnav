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

#ifdef HAVE_RUST_DEPS
#    include "lnav_rs_ext.cxx.hh"
#endif

static auto intern_lifetime = intern_string::get_table_lifetime();

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
      alv_thread_meta(intern_string::lookup("log_thread_id"),
                      value_kind_t::VALUE_TEXT,
                      logline_value_meta::table_column{4})
{
    this->alv_msg_meta.lvm_identifier = true;
    this->alv_schema_meta.lvm_identifier = true;
}

void
all_logs_vtab::get_columns(std::vector<vtab_column>& cols) const
{
    cols.emplace_back(
        vtab_column(this->alv_msg_meta.lvm_name.get())
            .with_comment(
                "The message format with variables replaced by hash marks"));
    cols.emplace_back(this->alv_schema_meta.lvm_name.get(),
                      SQLITE3_TEXT,
                      "",
                      true,
                      "The ID for the message schema");
    cols.emplace_back(this->alv_values_meta.lvm_name.get(),
                      SQLITE3_TEXT,
                      "",
                      false,
                      "The values extracted from the message");
    cols.emplace_back(this->alv_src_meta.lvm_name.get(),
                      SQLITE3_TEXT,
                      "",
                      false,
                      "The source code that generated this message");
    cols.emplace_back(this->alv_thread_meta.lvm_name.get(),
                      SQLITE3_TEXT,
                      "",
                      false,
                      "The ID of the thread that generated this message");
}

void
all_logs_vtab::extract(logfile* lf,
                       uint64_t line_number,
                       string_attrs_t& sa,
                       logline_value_vector& values)
{
    auto& line = values.lvv_sbr;
    auto* format = lf->get_format_ptr();

    logline_value_vector sub_values;

    sa.clear();
    sub_values.lvv_sbr = line.clone();
    format->annotate(lf, line_number, sa, sub_values, false);

    auto body = find_string_attr_range(sa, &SA_BODY);
    if (!body.is_valid()) {
        body.lr_start = 0;
        body.lr_end = line.length();
    }
    auto body_sf = line.to_string_fragment(body);
    auto src_file = find_string_attr_range(sa, &SA_SRC_FILE);
    auto src_line = find_string_attr_range(sa, &SA_SRC_LINE);
    auto thread_id = find_string_attr_range(sa, &SA_THREAD_ID);
    auto src_file_sf = line.to_string_fragment(src_file);
    auto src_line_sf = line.to_string_fragment(src_line);
    auto thread_id_sf = line.to_string_fragment(thread_id);
    auto h = hasher();
    if (src_file_sf.is_valid() && src_line_sf.is_valid()) {
        h.update(format->get_name().c_str());
        h.update(src_file_sf);
        h.update(src_line_sf);
    }
#ifdef HAVE_RUST_DEPS
    auto file_rust_str = rust::Str();
    auto lineno = 0UL;
    if (src_file_sf.is_valid()) {
        file_rust_str = rust::Str(src_file_sf.data(), src_file_sf.length());
    }
    if (src_line_sf.is_valid()) {
        auto scan_res
            = scn::scan_int<decltype(lineno)>(src_line_sf.to_string_view());
        if (scan_res) {
            lineno = scan_res->value();
        }
    }
    auto body_rust_str = rust::Str(body_sf.data(), body_sf.length());
    auto find_res = lnav_rs_ext::find_log_statement_json(
        file_rust_str, lineno, body_rust_str);
    if (find_res != nullptr) {
        if (!src_file_sf.is_valid() || !src_line_sf.is_valid()) {
            h.update(find_res->src.c_str());
            h.update(find_res->pattern.c_str());
        }
        auto line_iter = lf->begin() + line_number;
        if (!line_iter->has_schema()) {
            line_iter->set_schema(h.to_array());
        }
        values.lvv_values.emplace_back(this->alv_msg_meta,
                                       (std::string) find_res->pattern);
        values.lvv_values.emplace_back(this->alv_schema_meta, h.to_string());
        values.lvv_values.emplace_back(this->alv_values_meta,
                                       (std::string) find_res->variables);
        values.lvv_values.emplace_back(this->alv_src_meta,
                                       (std::string) find_res->src);
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

        auto schema_id = (src_file_sf.is_valid() && src_line_sf.is_valid())
            ? h.to_string()
            : dp.dp_schema_id.to_string();
        values.lvv_values.emplace_back(this->alv_msg_meta, std::move(str));
        values.lvv_values.emplace_back(this->alv_schema_meta, schema_id);
        values.lvv_values.emplace_back(
            this->alv_values_meta,
            json_string(gen).to_string_fragment().to_string());
    }
    if (thread_id_sf.empty()) {
        values.lvv_values.emplace_back(this->alv_thread_meta);
    } else {
        values.lvv_values.emplace_back(this->alv_thread_meta, thread_id_sf);
    }
    values.lvv_opid_value = std::move(sub_values.lvv_opid_value);
    values.lvv_opid_provenance = sub_values.lvv_opid_provenance;
}

bool
all_logs_vtab::next(log_cursor& lc, logfile_sub_source& lss)
{
    return true;
}
