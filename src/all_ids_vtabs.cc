/**
 * Copyright (c) 2025, Timothy Stack
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

#include <string>
#include <vector>

#include "all_ids_vtabs.hh"

#include "base/distributed_slice.hh"
#include "base/injector.bind.hh"
#include "base/injector.hh"
#include "base/itertools.hh"
#include "base/time_util.hh"
#include "file_collection.hh"
#include "log_format.hh"
#include "logfile.hh"
#include "robin_hood/robin_hood.h"
#include "vtab_module.hh"

namespace {
struct all_opids {
    static constexpr const char* NAME = "all_opids";
    static constexpr const char* CREATE_STMT = R"(
CREATE TABLE all_opids (
    opid TEXT PRIMARY KEY,  -- The operation ID
    earliest DATETIME,      -- The earliest time this ID was seen
    latest DATETIME,        -- The latest time this ID was seen
    errors INTEGER,         -- The number of error messages associated with this ID
    warnings INTEGER,       -- The number of warning messages associated with this ID
    total INTEGER,          -- The total number of messages associated with this ID
    description TEXT        -- A description of the operation
);
)";

    struct cursor {
        struct opid_time_pair {
            std::string otp_opid;
            opid_time_range otp_range;
            std::string otp_description;

            bool operator<(const opid_time_pair& rhs) const
            {
                return this->otp_range < rhs.otp_range;
            }
        };

        using pair_map = robin_hood::unordered_map<std::string, opid_time_pair>;

        sqlite3_vtab_cursor base{};
        std::vector<opid_time_pair> c_opids;
        std::vector<opid_time_pair>::const_iterator c_iter;

        explicit cursor(sqlite3_vtab* vt)
        {
            const auto& active_files = injector::get<file_collection&>();
            pair_map gather_map;

            for (const auto& lf : active_files.fc_files) {
                auto lf_opids = lf->get_opids().readAccess();
                for (const auto& [key, om] : lf_opids->los_opid_ranges) {
                    auto key_str = key.to_string();
                    auto gather_iter = gather_map.find(key_str);
                    if (gather_iter == gather_map.end()) {
                        auto emplace_res = gather_map.emplace(
                            key_str, opid_time_pair{key_str, om});
                        gather_iter = emplace_res.first;
                    } else {
                        gather_iter->second.otp_range |= om;
                    }
                    if (gather_iter->second.otp_description.empty()) {
                        auto format = lf->get_format();
                        if (om.otr_description.lod_id.has_value()) {
                            auto desc_iter
                                = format->lf_opid_description_def->find(
                                    om.otr_description.lod_id.value());
                            if (desc_iter
                                != format->lf_opid_description_def->end())
                            {
                                gather_iter->second.otp_description
                                    = desc_iter->second.to_string(
                                        om.otr_description.lod_elements);
                            }
                        } else if (!om.otr_description.lod_elements.empty()) {
                            gather_iter->second.otp_description
                                = om.otr_description.lod_elements.front()
                                      .second;
                        }
                    }
                }
            }
            for (const auto& [key, value] : gather_map) {
                this->c_opids.emplace_back(std::move(value));
            }
            std::stable_sort(this->c_opids.begin(), this->c_opids.end());

            this->base.pVtab = vt;
        }

        int reset()
        {
            this->c_iter = this->c_opids.begin();

            return SQLITE_OK;
        }

        int next()
        {
            if (this->c_iter != this->c_opids.end()) {
                ++this->c_iter;
            }

            return SQLITE_OK;
        }

        int eof() const { return this->c_iter == this->c_opids.end(); }

        int get_rowid(sqlite_int64& rowid_out) const
        {
            rowid_out = std::distance(this->c_opids.begin(), this->c_iter);

            return SQLITE_OK;
        }
    };

    int get_column(cursor& vc, sqlite3_context* ctx, int col)
    {
        switch (col) {
            case 0: {
                to_sqlite(ctx, vc.c_iter->otp_opid);
                break;
            }
            case 1: {
                to_sqlite(ctx, vc.c_iter->otp_range.otr_range.tr_begin);
                break;
            }
            case 2: {
                to_sqlite(ctx, vc.c_iter->otp_range.otr_range.tr_end);
                break;
            }
            case 3: {
                to_sqlite(ctx,
                          vc.c_iter->otp_range.otr_level_stats.lls_error_count);
                break;
            }
            case 4: {
                to_sqlite(
                    ctx,
                    vc.c_iter->otp_range.otr_level_stats.lls_warning_count);
                break;
            }
            case 5: {
                to_sqlite(ctx,
                          vc.c_iter->otp_range.otr_level_stats.lls_total_count);
                break;
            }
            case 6: {
                if (vc.c_iter->otp_description.empty()) {
                    sqlite3_result_null(ctx);
                } else {
                    to_sqlite(ctx, vc.c_iter->otp_description);
                }
                break;
            }
        }

        return SQLITE_OK;
    }

    int delete_row(sqlite3_vtab* tab, sqlite3_int64 rowid)
    {
        tab->zErrMsg = sqlite3_mprintf(
            "Rows cannot be deleted from the all_opids table");
        return SQLITE_ERROR;
    }

    int insert_row(sqlite3_vtab* tab, sqlite3_int64& rowid_out)
    {
        tab->zErrMsg = sqlite3_mprintf(
            "Rows cannot be inserted into the all_opids table");
        return SQLITE_ERROR;
    }

    int update_row(sqlite3_vtab* tab,
                   sqlite3_int64& index,
                   string_fragment opid,
                   string_fragment earliest,
                   string_fragment latest,
                   int64_t errors,
                   int64_t warnings,
                   int64_t total,
                   std::optional<string_fragment> description)
    {
        if (description) {
            const auto& active_files = injector::get<file_collection&>();

            for (const auto& lf : active_files.fc_files) {
                lf->set_opid_description(opid, description.value());
            }
        }
        return SQLITE_OK;
    }
};

struct all_thread_ids {
    static constexpr const char* NAME = "all_thread_ids";
    static constexpr const char* CREATE_STMT = R"(
CREATE TABLE all_thread_ids (
    thread_id TEXT PRIMARY KEY,  -- The thread ID
    earliest DATETIME,           -- The earliest time this ID was seen
    latest DATETIME,             -- The latest time this ID was seen
    errors INTEGER,              -- The number of error messages associated with this ID
    warnings INTEGER,            -- The number of warning messages associated with this ID
    total INTEGER                -- The total number of messages associated with this ID
);
)";

    struct cursor {
        struct thread_id_time_pair {
            std::string titp_thread_id;
            thread_id_time_range titp_range;

            bool operator<(const thread_id_time_pair& rhs) const
            {
                return this->titp_range < rhs.titp_range;
            }
        };

        using pair_map
            = robin_hood::unordered_map<std::string, thread_id_time_pair>;

        sqlite3_vtab_cursor base{};
        std::vector<thread_id_time_pair> c_thread_ids;
        std::vector<thread_id_time_pair>::const_iterator c_iter;

        explicit cursor(sqlite3_vtab* vt)
        {
            const auto& active_files = injector::get<file_collection&>();
            pair_map gather_map;

            for (const auto& lf : active_files.fc_files) {
                auto lf_thread_ids = lf->get_thread_ids().readAccess();
                for (const auto& [key, om] : lf_thread_ids->ltis_tid_ranges) {
                    auto key_str = key.to_string();
                    auto gather_iter = gather_map.find(key_str);
                    if (gather_iter == gather_map.end()) {
                        gather_map[key_str].titp_thread_id = key_str;
                        gather_map[key_str].titp_range = om;
                    } else {
                        gather_iter->second.titp_range |= om;
                    }
                }
            }
            for (const auto& [key, value] : gather_map) {
                this->c_thread_ids.emplace_back(std::move(value));
            }
            std::stable_sort(this->c_thread_ids.begin(),
                             this->c_thread_ids.end());

            this->base.pVtab = vt;
        }

        int reset()
        {
            this->c_iter = this->c_thread_ids.begin();

            return SQLITE_OK;
        }

        int next()
        {
            if (this->c_iter != this->c_thread_ids.end()) {
                ++this->c_iter;
            }

            return SQLITE_OK;
        }

        int eof() const { return this->c_iter == this->c_thread_ids.end(); }

        int get_rowid(sqlite_int64& rowid_out) const
        {
            rowid_out = std::distance(this->c_thread_ids.begin(), this->c_iter);

            return SQLITE_OK;
        }
    };

    int get_column(cursor& vc, sqlite3_context* ctx, int col)
    {
        switch (col) {
            case 0: {
                to_sqlite(ctx, vc.c_iter->titp_thread_id);
                break;
            }
            case 1: {
                to_sqlite(ctx, vc.c_iter->titp_range.titr_range.tr_begin);
                break;
            }
            case 2: {
                to_sqlite(ctx, vc.c_iter->titp_range.titr_range.tr_end);
                break;
            }
            case 3: {
                to_sqlite(
                    ctx,
                    vc.c_iter->titp_range.titr_level_stats.lls_error_count);
                break;
            }
            case 4: {
                to_sqlite(
                    ctx,
                    vc.c_iter->titp_range.titr_level_stats.lls_warning_count);
                break;
            }
            case 5: {
                to_sqlite(
                    ctx,
                    vc.c_iter->titp_range.titr_level_stats.lls_total_count);
                break;
            }
        }

        return SQLITE_OK;
    }
};

auto all_vtabs_binder = injector::bind_multiple<vtab_module_base>()
                            .add<vtab_module<all_opids>>()
                            .add<vtab_module<tvt_no_update<all_thread_ids>>>();

}  // namespace

DIST_SLICE(inject_bind) int lnav_all_ids = 3;
