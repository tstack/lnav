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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include "sql_util.hh"
#include "column_namer.hh"
#include "log_search_table.hh"

const static std::string LOG_MSG_INSTANCE = "log_msg_instance";
static auto instance_name = intern_string::lookup("log_msg_instance");
static auto instance_meta = logline_value_meta(
    instance_name, value_kind_t::VALUE_INTEGER, 0);
static auto empty = intern_string::lookup("", 0);

log_search_table::log_search_table(pcrepp pattern,
                                   intern_string_t table_name)
    : log_vtab_impl(table_name),
      lst_regex(std::move(pattern)),
      lst_instance(-1)
{
    this->vi_supports_indexes = false;
    this->get_columns_int(this->lst_cols);
}

void log_search_table::get_columns_int(std::vector<vtab_column> &cols)
{
    column_namer cn;

    cols.emplace_back(LOG_MSG_INSTANCE, SQLITE_INTEGER);
    for (int lpc = 0; lpc < this->lst_regex.get_capture_count(); lpc++) {
        std::string collator;
        std::string colname;
        int sqlite_type = SQLITE3_TEXT;

        colname = cn.add_column(this->lst_regex.name_for_capture(lpc));
        if (this->lst_regex.captures().size() ==
            (size_t) this->lst_regex.get_capture_count()) {
            auto iter = this->lst_regex.cap_begin() + lpc;
            auto cap_re = this->lst_regex.get_pattern()
                .substr(iter->c_begin, iter->length());
            sqlite_type = guess_type_from_pcre(cap_re, collator);
            switch (sqlite_type) {
                case SQLITE_FLOAT:
                    this->lst_column_metas.emplace_back(
                        intern_string::lookup(colname),
                        value_kind_t::VALUE_FLOAT,
                        cols.size());
                    break;
                case SQLITE_INTEGER:
                    this->lst_column_metas.emplace_back(
                        intern_string::lookup(colname),
                        value_kind_t::VALUE_INTEGER,
                        cols.size());
                    break;
                default:
                    this->lst_column_metas.emplace_back(
                        intern_string::lookup(colname),
                        value_kind_t::VALUE_TEXT,
                        cols.size());
                    break;
            }
        }
        cols.emplace_back(colname, sqlite_type, collator);
    }
}

void
log_search_table::get_foreign_keys(std::vector<std::string> &keys_inout) const
{
    log_vtab_impl::get_foreign_keys(keys_inout);
    keys_inout.emplace_back("log_msg_instance");
}

bool log_search_table::next(log_cursor &lc, logfile_sub_source &lss)
{
    if (lc.lc_curr_line == -1_vl) {
        this->lst_instance = -1;
    }

    lc.lc_curr_line = lc.lc_curr_line + 1_vl;
    lc.lc_sub_index = 0;

    if (lc.lc_curr_line == (int) lss.text_line_count()) {
        return true;
    }

    auto cl = lss.at(lc.lc_curr_line);
    auto lf = lss.find(cl);
    auto lf_iter = lf->begin() + cl;

    if (!lf_iter->is_message()) {
        return false;
    }

    string_attrs_t sa;
    std::vector<logline_value> line_values;

    lf->read_full_message(lf_iter, this->lst_current_line);
    lf->get_format()->annotate(cl, this->lst_current_line, sa, line_values,
                               false);
    pcre_input pi(this->lst_current_line.get_data(),
                  0,
                  this->lst_current_line.length());

    if (!this->lst_regex.match(this->lst_match_context, pi)) {
        return false;
    }

    this->lst_instance += 1;

    return true;
}

void
log_search_table::extract(std::shared_ptr<logfile> lf, uint64_t line_number,
                          shared_buffer_ref &line,
                          std::vector<logline_value> &values)
{
    values.emplace_back(instance_meta, this->lst_instance);
    for (int lpc = 0; lpc < this->lst_regex.get_capture_count(); lpc++) {
        auto cap = this->lst_match_context[lpc];
        values.emplace_back(this->lst_column_metas[lpc], line,
                            line_range{cap->c_begin, cap->c_end});
    }
}
