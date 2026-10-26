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
enum class StmtLevel : std::uint8_t;
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

    void lines_for_values(int col,
                          const std::vector<std::string>& values,
                          logfile_sub_source& lss,
                          std::vector<vis_line_t>& lines_out,
                          std::vector<std::string>& unknown_out) override;

    bool knows_lines_for(int col) const override;

    /** Has the statement map check its statements for the scan (alv_scan_started). */
    void filter(log_cursor& lc, logfile_sub_source& lss) override;

    bool cached_column(int col,
                       logfile* lf,
                       uint64_t line_number,
                       sqlite3_context* ctx) override;

    /**
     * Appends the rows logged by the statements with the log_msg_schemas,
     * from lnav's per-line statement map (log_stmt_stats.cc), and appends
     * the schemas the map can't answer for to unknown_out.  Set by the lnav
     * binary; unset, schema constraints use the column index.
     */
    using schema_lines_t = void (*)(const std::vector<std::string>& schemas,
                                    logfile_sub_source& lss,
                                    std::vector<vis_line_t>& lines_out,
                                    std::vector<std::string>& unknown_out);
    static schema_lines_t alv_schema_lines;

    /**
     * Called as a scan starts (each cursor's xFilter, after any lookup in
     * the statement map), so the map checks once per scan, not once per row,
     * whether its statements are still current.  Set by the lnav binary.
     */
    using scan_started_t = void (*)();
    static scan_started_t alv_scan_started;

    /** The statement map hasn't matched the line yet, or its answer is stale. */
    struct line_stmt_unmapped {};
    /** The statement map matched the line and log2src found no statement. */
    struct line_stmt_unmatched {};
    /** The statement map's answer for a line: the statement's details. */
    struct line_stmt_mapped {
        /**
         * log2src's description of the statement, like log_msg_src.  The
         * statement map keeps both strings for good, in place, so all_logs
         * passes them on as fragments, without copying.
         */
        const std::string* lsm_src;
        /** Like log_msg_schema. */
        const std::string* lsm_schema;
    };
    using line_stmt = mapbox::util::
        variant<line_stmt_unmapped, line_stmt_unmatched, line_stmt_mapped>;

    /**
     * Gives what the statement map knows about the message at a line, so
     * log_msg_src and log_msg_schema don't need log2src to match the message
     * again.  Set by the lnav binary; unset, every message is matched.
     */
    using line_stmt_t = line_stmt (*)(const logfile* lf, uint64_t line_number);
    static line_stmt_t alv_line_stmt;

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
     * gave for it, and level is the message's level, which breaks ties
     * between statements that match equally well.
     */
    static std::unique_ptr<lnav_rs_ext::FindLogResultJson> find_statement(
        const shared_buffer_ref& sbr,
        const string_attrs_t& sa,
        const logline_value_vector& values,
        log_level_t level,
        bool extract_variables);

    /**
     * The level of a message as log2src tells them apart.  The levels it
     * doesn't have are folded into the nearest one it does.
     */
    static lnav_rs_ext::StmtLevel to_stmt_level(log_level_t level);

    /** The level of a log statement found by log2src. */
    static log_level_t from_stmt_level(lnav_rs_ext::StmtLevel level);
#endif

private:
    logline_value_meta alv_msg_meta;
    logline_value_meta alv_schema_meta;
    logline_value_meta alv_values_meta;
    logline_value_meta alv_src_meta;
    logline_value_meta alv_stacktrace_meta;
    logline_value_meta alv_format_values_meta;
    logline_value_meta alv_schema_kind_meta;
};

#endif  // LNAV_ALL_LOGS_VTAB_HH
