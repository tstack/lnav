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

#include "log_search_table.hh"

#include "column_namer.hh"
#include "config.h"
#include "sql_util.hh"

const static std::string MATCH_INDEX = "match_index";
static auto match_index_name = intern_string::lookup("match_index");

log_search_table::log_search_table(pcrepp pattern, intern_string_t table_name)
    : log_vtab_impl(table_name), lst_regex(std::move(pattern))
{
}

void
log_search_table::get_columns_int(std::vector<vtab_column>& cols) const
{
    column_namer cn{column_namer::language::SQL};

    if (this->lst_format != nullptr) {
        this->lst_column_metas = this->lst_format->get_value_metadata();
        this->lst_format_column_count = this->lst_column_metas.size();
        cols.resize(this->lst_column_metas.size());
        for (const auto& meta : this->lst_column_metas) {
            if (meta.lvm_column == -1) {
                continue;
            }
            auto type_pair
                = log_vtab_impl::logline_value_to_sqlite_type(meta.lvm_kind);
            cols[meta.lvm_column].vc_name = meta.lvm_name.to_string();
            cols[meta.lvm_column].vc_type = type_pair.first;
            cols[meta.lvm_column].vc_subtype = type_pair.second;
        }
    }

    this->lst_column_metas.emplace_back(
        match_index_name, value_kind_t::VALUE_INTEGER, cols.size());
    cols.emplace_back(MATCH_INDEX, SQLITE_INTEGER);
    for (int lpc = 0; lpc < this->lst_regex.get_capture_count(); lpc++) {
        std::string collator;
        std::string colname;
        int sqlite_type = SQLITE3_TEXT;

        colname = cn.add_column(
                        string_fragment{this->lst_regex.name_for_capture(lpc)})
                      .to_string();
        if (this->lst_regex.captures().size()
            == (size_t) this->lst_regex.get_capture_count())
        {
            auto iter = this->lst_regex.cap_begin() + lpc;
            auto cap_re = this->lst_regex.get_pattern().substr(iter->c_begin,
                                                               iter->length());
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
log_search_table::get_foreign_keys(std::vector<std::string>& keys_inout) const
{
    log_vtab_impl::get_foreign_keys(keys_inout);
    keys_inout.emplace_back(MATCH_INDEX);
}

bool
log_search_table::next(log_cursor& lc, logfile_sub_source& lss)
{
    this->vi_attrs.clear();
    this->lst_line_values_cache.clear();

    if (this->lst_match_index >= 0) {
        this->lst_input.pi_offset = this->lst_input.pi_next_offset;
        if (this->lst_regex.match(
                this->lst_match_context, this->lst_input, PCRE_NO_UTF8_CHECK))
        {
            this->lst_match_index += 1;
            return true;
        }

        // log_debug("done matching message");
        this->lst_match_index = -1;
        return false;
    }

    this->lst_match_index = -1;

    if (lc.is_eof()) {
        return true;
    }

    if (!this->is_valid(lc, lss)) {
        return false;
    }

    auto cl = lss.at(lc.lc_curr_line);
    auto* lf = lss.find_file_ptr(cl);

    auto lf_iter = lf->begin() + cl;

    if (!lf_iter->is_message()) {
        return false;
    }

    if (this->lst_mismatch_bitmap.is_bit_set(lc.lc_curr_line)) {
        // log_debug("%d: mismatch, aborting", (int) lc.lc_curr_line);
        return false;
    }

    // log_debug("%d: doing message", (int) lc.lc_curr_line);
    lf->read_full_message(lf_iter, this->lst_current_line);
    lf->get_format()->annotate(cl,
                               this->lst_current_line,
                               this->vi_attrs,
                               this->lst_line_values_cache,
                               false);
    this->lst_input.reset(
        this->lst_current_line.get_data(), 0, this->lst_current_line.length());

    if (!this->lst_regex.match(
            this->lst_match_context, this->lst_input, PCRE_NO_UTF8_CHECK))
    {
        this->lst_mismatch_bitmap.set_bit(lc.lc_curr_line);
        return false;
    }

    this->lst_match_index = 0;

    return true;
}

void
log_search_table::extract(logfile* lf,
                          uint64_t line_number,
                          shared_buffer_ref& line,
                          std::vector<logline_value>& values)
{
    if (this->lst_format != nullptr) {
        values = this->lst_line_values_cache;
    }
    values.emplace_back(this->lst_column_metas[this->lst_format_column_count],
                        this->lst_match_index);
    for (int lpc = 0; lpc < this->lst_regex.get_capture_count(); lpc++) {
        const auto* cap = this->lst_match_context[lpc];
        values.emplace_back(
            this->lst_column_metas[this->lst_format_column_count + 1 + lpc],
            line,
            line_range{cap->c_begin, cap->c_end});
    }
}

void
log_search_table::get_primary_keys(std::vector<std::string>& keys_out) const
{
    keys_out.emplace_back("log_line");
    keys_out.emplace_back("match_index");
}

void
log_search_table::filter(log_cursor& lc, logfile_sub_source& lss)
{
    if (this->lst_format != nullptr) {
        lc.lc_format_name = this->lst_format->get_name();
    }
    if (!this->lst_log_path_glob.empty()) {
        lc.lc_log_path.emplace_back(SQLITE_INDEX_CONSTRAINT_GLOB,
                                    this->lst_log_path_glob);
    }
    if (this->lst_log_level) {
        lc.lc_level_constraint = log_cursor::level_constraint{
            SQLITE_INDEX_CONSTRAINT_EQ,
            this->lst_log_level.value(),
        };
    }
    this->lst_match_index = -1;

    if (lss.lss_index_generation != this->lst_index_generation) {
        log_debug("%s:index generation changed from %d to %d, resetting...",
                  this->vi_name.c_str(),
                  this->lst_index_generation,
                  lss.lss_index_generation);
        this->lst_mismatch_bitmap
            = auto_buffer::alloc_bitmap(lss.text_line_count());
        this->lst_index_generation = lss.lss_index_generation;
    }

    if (this->lst_mismatch_bitmap.bitmap_size() < lss.text_line_count()) {
        this->lst_mismatch_bitmap.expand_bitmap_to(lss.text_line_count());
        this->lst_mismatch_bitmap.resize_bitmap(lss.text_line_count());
#if 1
        log_debug("%s:bitmap resize %d:%d",
                  this->vi_name.c_str(),
                  this->lst_mismatch_bitmap.size(),
                  this->lst_mismatch_bitmap.capacity());
#endif
    }
    if (!lc.lc_indexed_lines.empty()) {
        lc.lc_curr_line = lc.lc_indexed_lines.back();
        lc.lc_indexed_lines.pop_back();
    }
}
