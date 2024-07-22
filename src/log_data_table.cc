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

#include "log_data_table.hh"

#include "column_namer.hh"
#include "config.h"
#include "scn/scn.h"

log_data_table::log_data_table(logfile_sub_source& lss,
                               log_vtab_manager& lvm,
                               content_line_t template_line,
                               intern_string_t table_name)
    : log_vtab_impl(table_name), ldt_log_source(lss),
      ldt_template_line(template_line)
{
    auto lf = lss.find(template_line);
    auto format = lf->get_format();

    this->vi_supports_indexes = false;
    this->ldt_format_impl = lvm.lookup_impl(format->get_name());
    this->get_columns_int();
}

void
log_data_table::get_columns_int()
{
    auto& cols = this->ldt_cols;
    auto& metas = this->ldt_value_metas;
    content_line_t cl_copy = this->ldt_template_line;
    auto lf = this->ldt_log_source.find(cl_copy);
    struct line_range body;
    string_attrs_t sa;
    logline_value_vector line_values;
    auto format = lf->get_format();

    if (this->ldt_format_impl != nullptr) {
        this->ldt_format_impl->get_columns(cols);
    }
    lf->read_full_message(lf->begin() + cl_copy, line_values.lvv_sbr);
    line_values.lvv_sbr.erase_ansi();
    format->annotate(lf.get(), cl_copy, sa, line_values, false);
    body = find_string_attr_range(sa, &SA_BODY);
    if (body.lr_end == -1) {
        this->ldt_schema_id.clear();
        return;
    }

    data_scanner ds(line_values.lvv_sbr, body.lr_start, body.lr_end);
    data_parser dp(&ds);
    column_namer cn{column_namer::language::SQL};

    dp.parse();

    for (auto pair_iter = dp.dp_pairs.begin(); pair_iter != dp.dp_pairs.end();
         ++pair_iter)
    {
        std::string key_str
            = dp.get_element_string(pair_iter->e_sub_elements->front());
        auto colname = cn.add_column(key_str).to_string();
        int sql_type = SQLITE3_TEXT;
        value_kind_t kind = value_kind_t::VALUE_TEXT;
        std::string collator;

        switch (pair_iter->e_sub_elements->back().value_token()) {
            case DT_IPV4_ADDRESS:
            case DT_IPV6_ADDRESS:
                collator = "ipaddress";
                break;

            case DT_NUMBER:
                sql_type = SQLITE_FLOAT;
                kind = value_kind_t::VALUE_FLOAT;
                break;

            default:
                collator = "naturalnocase";
                break;
        }
        metas.emplace_back(intern_string::lookup(colname),
                           kind,
                           logline_value_meta::table_column{cols.size()},
                           format.get());
        cols.emplace_back(colname, sql_type, collator);
    }
    this->ldt_schema_id = dp.dp_schema_id;
}

bool
log_data_table::next(log_cursor& lc, logfile_sub_source& lss)
{
    if (lc.is_eof()) {
        return true;
    }

    content_line_t cl;

    cl = lss.at(lc.lc_curr_line);
    auto* lf = lss.find_file_ptr(cl);
    if (lf->get_format()->get_name() != this->ldt_format_impl->get_name()) {
        return false;
    }
    auto lf_iter = lf->begin() + cl;

    if (!lf_iter->is_message()) {
        return false;
    }

    if (lf_iter->has_schema() && !lf_iter->match_schema(this->ldt_schema_id)) {
        return false;
    }

    string_attrs_t sa;
    struct line_range body;
    logline_value_vector line_values;

    lf->read_full_message(lf_iter, line_values.lvv_sbr);
    line_values.lvv_sbr.erase_ansi();
    lf->get_format()->annotate(lf, cl, sa, line_values, false);
    body = find_string_attr_range(sa, &SA_BODY);
    if (body.lr_end == -1) {
        return false;
    }

    data_scanner ds(line_values.lvv_sbr, body.lr_start, body.lr_end);
    data_parser dp(&ds);
    dp.parse();

    lf_iter->set_schema(dp.dp_schema_id);

    /* The cached schema ID in the log line is not complete, so we still */
    /* need to check for a full match. */
    if (dp.dp_schema_id != this->ldt_schema_id) {
        return false;
    }

    this->ldt_pairs.clear();
    this->ldt_pairs.swap(dp.dp_pairs, __FILE__, __LINE__);

    return true;
}

void
log_data_table::extract(logfile* lf,
                        uint64_t line_number,
                        logline_value_vector& values)
{
    auto& line = values.lvv_sbr;
    auto meta_iter = this->ldt_value_metas.begin();

    this->ldt_format_impl->extract(lf, line_number, values);
    for (const auto& ldt_pair : this->ldt_pairs) {
        const auto& pvalue = ldt_pair.get_pair_value();
        auto lr = line_range{
            pvalue.e_capture.c_begin,
            pvalue.e_capture.c_end,
        };

        switch (pvalue.value_token()) {
            case DT_NUMBER: {
                auto num_view = line.to_string_view(lr);
                auto num_scan_res = scn::scan_value<double>(num_view);
                auto num = num_scan_res ? num_scan_res.value() : 0.0;

                values.lvv_values.emplace_back(*meta_iter, num);
                break;
            }

            default: {
                values.lvv_values.emplace_back(*meta_iter, line, lr);
                break;
            }
        }
        ++meta_iter;
    }
}
