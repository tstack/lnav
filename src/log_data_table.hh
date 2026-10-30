/**
 * Copyright (c) 2007-2012, Timothy Stack
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
 * @file log_data_table.hh
 */

#ifndef lnav_log_data_table_hh
#define lnav_log_data_table_hh

#include <memory>
#include <string>
#include <vector>

#include "data_parser.hh"
#include "log_vtab_impl.hh"
#include "logfile_sub_source.hh"

class log_data_table : public log_vtab_impl {
public:
    log_data_table(logfile_sub_source& lss,
                   log_vtab_manager& lvm,
                   content_line_t template_line,
                   intern_string_t table_name);

    void get_columns_int();

    void get_columns(std::vector<vtab_column>& cols) const override
    {
        cols = this->ldt_cols;
    }

    void get_foreign_keys(
        std::unordered_set<std::string>& keys_inout) const override
    {
        log_vtab_impl::get_foreign_keys(keys_inout);
    }

    bool next(log_cursor& lc, logfile_sub_source& lss) override;

    void extract(const log_cursor::extract_context& ec,
                 logfile* lf,
                 uint64_t line_number,
                 string_attrs_t& sa,
                 logline_value_vector& values) override;

    /** What the statement map knows about a message's data_parser schema. */
    enum class schema_check {
        /** Not mapped yet, or mapped to a schema that isn't its text's. */
        unknown,
        same,
        different,
    };

    /**
     * Checks the message at a line against a data_parser schema, given as
     * text, using lnav's per-line statement map (log_stmt_stats.cc), so
     * messages with another schema aren't read and parsed again.  Set by
     * the lnav binary; unset, every message is parsed.
     */
    using check_schema_t = schema_check (*)(const logfile* lf,
                                            uint64_t line_number,
                                            const std::string& schema);
    static check_schema_t ldt_check_schema;

    /**
     * Starts the statement map, which then maps the messages in the
     * background.  Set by the lnav binary.
     */
    using start_map_t = void (*)();
    static start_map_t ldt_start_map;

private:
    logfile_sub_source& ldt_log_source;
    const content_line_t ldt_template_line;
    data_parser::schema_id_t ldt_schema_id;
    /** ldt_schema_id as text, as the statement map keeps it. */
    std::string ldt_schema_str;
    data_parser::element_list_t ldt_pairs;
    std::shared_ptr<log_vtab_impl> ldt_format_impl;
    std::vector<vtab_column> ldt_cols;
    std::vector<logline_value_meta> ldt_value_metas;
};

#endif
