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
 */

#ifndef lnav_all_logs_vtab_hh
#define lnav_all_logs_vtab_hh

#include <cstdint>
#include <memory>
#include <vector>

#include "config.h"
#include "hasher.hh"
#include "log_format.hh"
#include "log_vtab_impl.hh"
#include "logfile.hh"

#ifdef HAVE_RUST_DEPS
namespace lnav_rs_ext {
struct FindLogResultJson;
}
#endif

/**
 * A virtual table that provides access to all log messages from all formats.
 *
 * @feature f0:sql.tables.all_logs
 */
class all_logs_vtab : public log_vtab_impl {
public:
    all_logs_vtab();

    void get_columns(std::vector<vtab_column>& cols) const override;

    void extract(const log_cursor::extract_context& ec,
                 logfile* lf,
                 uint64_t line_number,
                 string_attrs_t& sa,
                 logline_value_vector& values) override;

    bool next(log_cursor& lc, logfile_sub_source& lss) override;

    const std::vector<vis_line_t>* lines_for_value(
        int col, const std::string& value, logfile_sub_source& lss) override;

    /**
     * Gives the rows logged by the statement with a log_msg_schema, from
     * lnav's per-line statement map (log_stmt_stats.cc), or nullptr when the
     * map can't answer.  Set by the lnav binary; unset, schema constraints
     * use the column index.
     */
    using schema_lines_t = const std::vector<vis_line_t>* (*) (
        const std::string& schema, logfile_sub_source& lss);
    static schema_lines_t alv_schema_lines;

    /**
     * Gives the log_msg_schema of the message at a line from the statement
     * map, or nullptr when the map doesn't know it (not mapped yet, no
     * statement, or the statements changed since).
     */
    using line_schema_t = const std::string* (*) (const logfile* lf,
                                                   uint64_t line_number);
    static line_schema_t alv_line_schema;

    /**
     * Writes the log_msg_schema of messages logged by a statement, given the
     * statement's JSON description and message pattern, to buf.
     */
    static void statement_schema(const string_fragment& src_json,
                                 const string_fragment& pattern,
                                 char buf[hasher::STRING_SIZE]);

#ifdef HAVE_RUST_DEPS
    /**
     * Finds the source statement that logged a message, or nullptr.  The
     * sbr is the message, sa and values are what the format's annotate()
     * gave for it.
     */
    static std::unique_ptr<lnav_rs_ext::FindLogResultJson> find_statement(
        const shared_buffer_ref& sbr,
        const string_attrs_t& sa,
        const logline_value_vector& values,
        bool extract_variables);
#endif

private:
    logline_value_meta alv_msg_meta;
    logline_value_meta alv_schema_meta;
    logline_value_meta alv_values_meta;
    logline_value_meta alv_src_meta;
    logline_value_meta alv_stacktrace_meta;
};

#endif  // LNAV_ALL_LOGS_VTAB_HH
