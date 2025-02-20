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

#include "base/ansi_scrubber.hh"
#include "column_namer.hh"
#include "config.h"
#include "sql_util.hh"

const static std::string MATCH_INDEX = "match_index";
static auto match_index_name = intern_string::lookup("match_index");

log_search_table::log_search_table(std::shared_ptr<lnav::pcre2pp::code> code,
                                   intern_string_t table_name)
    : log_vtab_impl(table_name), lst_regex(code),
      lst_match_data(this->lst_regex->create_match_data())
{
}

void
log_search_table::get_columns_int(std::vector<vtab_column>& cols) const
{
    if (!this->lst_cols.empty()) {
        return;
    }

    column_namer cn{column_namer::language::SQL};

    if (this->lst_format != nullptr) {
        this->lst_column_metas = this->lst_format->get_value_metadata();
        this->lst_format_column_count = this->lst_column_metas.size();
        cols.resize(this->lst_column_metas.size());
        for (const auto& meta : this->lst_column_metas) {
            if (!meta.lvm_column.is<logline_value_meta::table_column>()) {
                cols.pop_back();
                continue;
            }
            auto col
                = meta.lvm_column.get<logline_value_meta::table_column>().value;
            auto type_pair = logline_value_to_sqlite_type(meta.lvm_kind);
            cols[col].vc_name = meta.lvm_name.to_string();
            cols[col].vc_type = type_pair.first;
            cols[col].vc_subtype = type_pair.second;

            ensure(!cols[col].vc_name.empty());
        }
    }

    this->lst_column_metas.emplace_back(
        match_index_name,
        value_kind_t::VALUE_INTEGER,
        logline_value_meta::table_column{cols.size()});
    cols.emplace_back(MATCH_INDEX, SQLITE_INTEGER);
    cn.add_column("__all__"_frag);
    auto captures = this->lst_regex->get_captures();
    for (size_t lpc = 0; lpc < this->lst_regex->get_capture_count(); lpc++) {
        std::string collator;
        int sqlite_type = SQLITE3_TEXT;

        auto colname
            = cn.add_column(string_fragment::from_c_str(
                                this->lst_regex->get_name_for_capture(lpc + 1)))
                  .to_string();
        if (captures.size() == (size_t) this->lst_regex->get_capture_count()) {
            auto cap_re = captures[lpc].to_string();
            sqlite_type = guess_type_from_pcre(cap_re, collator);
            switch (sqlite_type) {
                case SQLITE_FLOAT:
                    this->lst_column_metas.emplace_back(
                        intern_string::lookup(colname),
                        value_kind_t::VALUE_FLOAT,
                        logline_value_meta::table_column{cols.size()});
                    break;
                case SQLITE_INTEGER:
                    this->lst_column_metas.emplace_back(
                        intern_string::lookup(colname),
                        value_kind_t::VALUE_INTEGER,
                        logline_value_meta::table_column{cols.size()});
                    break;
                default:
                    this->lst_column_metas.emplace_back(
                        intern_string::lookup(colname),
                        value_kind_t::VALUE_TEXT,
                        logline_value_meta::table_column{cols.size()});
                    break;
            }
        }
        cols.emplace_back(colname, sqlite_type, collator);
    }
}

void
log_search_table::get_foreign_keys(std::unordered_set<std::string>& keys_inout) const
{
    log_vtab_impl::get_foreign_keys(keys_inout);
    keys_inout.emplace(MATCH_INDEX);
}

bool
log_search_table::next(log_cursor& lc, logfile_sub_source& lss)
{
    this->vi_attrs.clear();
    this->lst_line_values_cache.lvv_values.clear();

    if (this->lst_match_index >= 0) {
        auto match_res = this->lst_regex->capture_from(this->lst_content)
                             .at(this->lst_remaining)
                             .into(this->lst_match_data)
                             .matches(PCRE2_NO_UTF_CHECK)
                             .ignore_error();

        if (match_res) {
#if 0
            log_debug("matched within line: %d",
                      this->lst_match_context.get_count());
#endif
            this->lst_remaining = match_res->f_remaining;
            this->lst_match_index += 1;
            return true;
        }

        // log_debug("done matching message");
        this->lst_remaining.clear();
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
    auto& sbr = this->lst_line_values_cache.lvv_sbr;
    lf->read_full_message(lf_iter, sbr);
    sbr.erase_ansi();
    lf->get_format()->annotate(
        lf, cl, this->vi_attrs, this->lst_line_values_cache, false);
    this->lst_content
        = this->lst_line_values_cache.lvv_sbr.to_string_fragment();

    auto match_res = this->lst_regex->capture_from(this->lst_content)
                         .into(this->lst_match_data)
                         .matches(PCRE2_NO_UTF_CHECK)
                         .ignore_error();

    if (!match_res) {
        this->lst_mismatch_bitmap.set_bit(lc.lc_curr_line);
        return false;
    }

    this->lst_remaining = match_res->f_remaining;
    this->lst_match_index = 0;

    return true;
}

void
log_search_table::extract(logfile* lf,
                          uint64_t line_number,
                          logline_value_vector& values)
{
    auto& line = values.lvv_sbr;
    if (this->lst_format != nullptr) {
        values = this->lst_line_values_cache;
    }
    values.lvv_values.emplace_back(
        this->lst_column_metas[this->lst_format_column_count],
        this->lst_match_index);
    for (size_t lpc = 0; lpc < this->lst_regex->get_capture_count(); lpc++) {
        const auto cap = this->lst_match_data[lpc + 1];
        if (cap) {
            values.lvv_values.emplace_back(
                this->lst_column_metas[this->lst_format_column_count + 1 + lpc],
                line,
                to_line_range(cap.value()));
        } else {
            values.lvv_values.emplace_back(
                this->lst_column_metas[this->lst_format_column_count + 1
                                       + lpc]);
        }
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
    if (!lc.lc_indexed_lines.empty()
        && lc.lc_indexed_lines_range.contains(lc.lc_curr_line))
    {
        lc.lc_curr_line = lc.lc_indexed_lines.back();
        lc.lc_indexed_lines.pop_back();
    }
}
