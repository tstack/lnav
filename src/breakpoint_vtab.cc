/**
 * Copyright (c) 2026, Timothy Stack
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

#include <functional>
#include <string>

#include "base/injector.bind.hh"
#include "base/lnav_log.hh"
#include "lnav.hh"
#include "pcrepp/pcre2pp.hh"
#include "vtab_module.hh"

static const char*
source_type_to_string(breakpoint_info::source_type st)
{
    switch (st) {
        case breakpoint_info::source_type::src_location:
            return "src_location";
        case breakpoint_info::source_type::message_schema:
            return "message_schema";
    }
    return "src_location";
}

static breakpoint_info::source_type
string_to_source_type(const std::string& s)
{
    if (s == "src_location") {
        return breakpoint_info::source_type::src_location;
    }
    if (s == "message_schema") {
        return breakpoint_info::source_type::message_schema;
    }
    throw sqlite_func_error(
        "Invalid breakpoint type '{}', expecting 'src_location' or "
        "'message_schema'",
        s);
}

static void
validate_schema_id(const std::string& schema_id)
{
    static const auto SCHEMA_RE
        = lnav::pcre2pp::code::from_const(R"(^[0-9a-f]{32}$)");

    if (!SCHEMA_RE.find_in(schema_id).ignore_error()) {
        throw sqlite_func_error(
            "Invalid schema_id '{}', expecting a 32-character hex string",
            schema_id);
    }
}

struct lnav_log_breakpoints : tvt_iterator_cursor<lnav_log_breakpoints> {
    using iterator = std::map<std::string, breakpoint_info>::iterator;

    static constexpr const char* NAME = "lnav_log_breakpoints";
    static constexpr const char* CREATE_STMT = R"(
-- Access lnav's log breakpoints through this table.
CREATE TABLE lnav_db.lnav_log_breakpoints (
    schema_id TEXT NOT NULL,                    -- The schema identifier for the breakpoint.  Matches the log_msg_schema in the all_logs table.
    description TEXT NOT NULL,                  -- A human-readable description of the breakpoint (e.g. format:file:line).
    type TEXT NOT NULL DEFAULT 'src_location',  -- The source of the schema ID, either 'src_location' or 'message_schema'.
    enabled INTEGER NOT NULL DEFAULT 1          -- Indicates whether the breakpoint is active.
);
)";

    iterator begin()
    {
        return lnav_data.ld_log_source.get_breakpoints().begin();
    }

    iterator end()
    {
        return lnav_data.ld_log_source.get_breakpoints().end();
    }

    sqlite3_int64 get_rowid(iterator iter)
    {
        return std::distance(
            lnav_data.ld_log_source.get_breakpoints().begin(), iter);
    }

    int get_column(const cursor& vc, sqlite3_context* ctx, int col)
    {
        switch (col) {
            case 0:
                to_sqlite(ctx, vc.iter->first);
                break;
            case 1:
                to_sqlite(ctx, vc.iter->second.bp_description);
                break;
            case 2:
                to_sqlite(ctx,
                          source_type_to_string(vc.iter->second.bp_source));
                break;
            case 3:
                to_sqlite(ctx, vc.iter->second.bp_enabled ? 1 : 0);
                break;
        }
        return SQLITE_OK;
    }

    int delete_row(sqlite3_vtab* tab, sqlite3_int64 rowid)
    {
        auto& bps = lnav_data.ld_log_source.get_breakpoints();

        auto iter = bps.begin();
        std::advance(iter, rowid);
        if (iter != bps.end()) {
            bps.erase(iter);
        }

        return SQLITE_OK;
    }

    int insert_row(sqlite3_vtab* tab,
                   sqlite3_int64& rowid_out,
                   std::string schema_id,
                   std::string description,
                   std::string type,
                   std::optional<bool> enabled)
    {
        validate_schema_id(schema_id);

        auto& bps = lnav_data.ld_log_source.get_breakpoints();
        auto existing = bps.find(schema_id);

        if (existing != bps.end()) {
            auto* mod_vt
                = (vtab_module<lnav_log_breakpoints>::vtab*) tab;
            auto conflict_mode = sqlite3_vtab_on_conflict(mod_vt->v_db);
            switch (conflict_mode) {
                case SQLITE_FAIL:
                case SQLITE_ABORT:
                    tab->zErrMsg = sqlite3_mprintf(
                        "A breakpoint with schema_id '%s' already exists",
                        schema_id.c_str());
                    return conflict_mode;
                case SQLITE_IGNORE:
                    return SQLITE_OK;
                case SQLITE_REPLACE:
                    break;
                default:
                    break;
            }
        }

        breakpoint_info bp;
        bp.bp_description = std::move(description);
        bp.bp_source = string_to_source_type(type);
        bp.bp_enabled = enabled.value_or(true);
        auto [it, _] = bps.insert_or_assign(schema_id, std::move(bp));
        rowid_out = std::distance(bps.begin(), it);

        return SQLITE_OK;
    }

    int update_row(sqlite3_vtab* tab,
                   sqlite3_int64& rowid,
                   std::string schema_id,
                   std::string description,
                   std::string type,
                   std::optional<bool> enabled)
    {
        validate_schema_id(schema_id);

        auto& bps = lnav_data.ld_log_source.get_breakpoints();
        auto it = bps.find(schema_id);
        if (it == bps.end()) {
            return SQLITE_OK;
        }

        it->second.bp_description = std::move(description);
        it->second.bp_source = string_to_source_type(type);
        if (enabled) {
            it->second.bp_enabled = enabled.value();
        }

        return SQLITE_OK;
    }
};

static auto bp_binder
    = injector::bind_multiple<vtab_module_base>()
          .add<vtab_module<lnav_log_breakpoints>>();
