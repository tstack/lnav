/**
 * Copyright (c) 2015, Timothy Stack
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
 * @file log_search_table.hh
 */

#ifndef lnav_log_search_table_hh
#define lnav_log_search_table_hh

#include <string>
#include <vector>

#include "log_vtab_impl.hh"
#include "pcrepp/pcre2pp.hh"
#include "shared_buffer.hh"

class log_search_table : public log_vtab_impl {
public:
    log_search_table(std::shared_ptr<lnav::pcre2pp::code> code,
                     intern_string_t table_name);

    void get_primary_keys(std::vector<std::string>& keys_out) const override;

    void get_columns_int(std::vector<vtab_column>& cols) const;

    void get_columns(std::vector<vtab_column>& cols) const override
    {
        this->get_columns_int(this->lst_cols);
        cols = this->lst_cols;
    }

    void filter(log_cursor& lc, logfile_sub_source& lss) override;

    void get_foreign_keys(std::vector<std::string>& keys_inout) const override;

    bool next(log_cursor& lc, logfile_sub_source& lss) override;

    void extract(logfile* lf,
                 uint64_t line_number,
                 logline_value_vector& values) override;

    std::shared_ptr<lnav::pcre2pp::code> lst_regex;
    lnav::pcre2pp::match_data lst_match_data;
    string_fragment lst_content;
    string_fragment lst_remaining;
    log_format* lst_format{nullptr};
    mutable size_t lst_format_column_count{0};
    std::string lst_log_path_glob;
    std::optional<log_level_t> lst_log_level;
    mutable std::vector<logline_value_meta> lst_column_metas;
    int64_t lst_match_index{-1};
    mutable std::vector<vtab_column> lst_cols;
    logline_value_vector lst_line_values_cache;
    auto_buffer lst_mismatch_bitmap{auto_buffer::alloc_bitmap(0)};
    uint32_t lst_index_generation{0};
};

#endif
