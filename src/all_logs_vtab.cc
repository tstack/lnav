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
                      logline_value_meta::table_column{2})
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
}

void
all_logs_vtab::extract(logfile* lf,
                       uint64_t line_number,
                       logline_value_vector& values)
{
    auto& line = values.lvv_sbr;
    auto* format = lf->get_format_ptr();

    logline_value_vector sub_values;

    this->vi_attrs.clear();
    sub_values.lvv_sbr = line.clone();
    format->annotate(lf, line_number, this->vi_attrs, sub_values, false);

    auto body = find_string_attr_range(this->vi_attrs, &SA_BODY);
    if (body.lr_start == -1) {
        body.lr_start = 0;
        body.lr_end = line.length();
    }

    data_scanner ds(
        line.to_string_fragment().sub_range(body.lr_start, body.lr_end));
    data_parser dp(&ds);
    std::string str;

    dp.dp_msg_format = &str;
    dp.parse();

    yajlpp_gen gen;
    yajl_gen_config(gen, yajl_gen_beautify, false);

    elements_to_json(gen, dp, &dp.dp_pairs);

    values.lvv_values.emplace_back(this->alv_msg_meta, std::move(str));
    values.lvv_values.emplace_back(this->alv_schema_meta,
                                   dp.dp_schema_id.to_string());
    values.lvv_values.emplace_back(
        this->alv_values_meta,
        json_string(gen).to_string_fragment().to_string());
    values.lvv_opid_value = std::move(sub_values.lvv_opid_value);
    values.lvv_opid_provenance = sub_values.lvv_opid_provenance;
}

bool
all_logs_vtab::next(log_cursor& lc, logfile_sub_source& lss)
{
    return true;
}
