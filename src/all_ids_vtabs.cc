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

#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "all_ids_vtabs.hh"

#include "ArenaAlloc/arenaalloc.h"
#include "base/distributed_slice.hh"
#include "base/injector.bind.hh"
#include "base/injector.hh"
#include "base/itertools.hh"
#include "base/time_util.hh"
#include "file_collection.hh"
#include "log_format.hh"
#include "logfile.hh"
#include "logfile_sub_source.hh"
#include "robin_hood/robin_hood.h"
#include "vtab_module.hh"

namespace {
/**
 * What a table's merged rows were built from: the LOG view's unfiltered index
 * (its generation, bumped by full and partial rebuilds and filter changes, and
 * its size, which grows with every indexed line, filtered or not, in hidden
 * files too), the file set, and the edits made to the id maps outside of
 * indexing.  While it's unchanged, so are the files' thread and opid maps.
 */
struct merge_key {
    uint32_t mk_generation{0};
    size_t mk_lines{0};
    int mk_files_generation{0};
    uint64_t mk_edits{0};

    static merge_key current()
    {
        auto& lss = injector::get<logfile_sub_source&>();
        const auto& fc = injector::get<file_collection&>();
        return {
            lss.lss_index_generation,
            lss.index_size(),
            fc.fc_files_generation,
            logfile::ID_EDITS.load(),
        };
    }

    bool operator==(const merge_key& rhs) const
    {
        return std::tie(this->mk_generation,
                        this->mk_lines,
                        this->mk_files_generation,
                        this->mk_edits)
            == std::tie(rhs.mk_generation,
                        rhs.mk_lines,
                        rhs.mk_files_generation,
                        rhs.mk_edits);
    }
};

/**
 * A table's rows merged across every file, kept between queries until the
 * files change (merge_key).  Merging each file's map on every open was most of
 * a query's time: the thread overview opens all_thread_ids twice, and with 77
 * files and ~900K threads each open took ~0.5s.  The rows are shared, never
 * changed in place: a cursor still open on older rows keeps them.
 */
template<typename Row>
struct merged_rows {
    using rows_t = std::vector<Row>;

    std::optional<merge_key> mr_key;
    std::shared_ptr<const rows_t> mr_rows;
    bool mr_sorted{false};

    /**
     * The rows, merged by [build] if the files changed, and in time order
     * when [sorted].  Sorting makes a sorted copy, so a cursor reading the
     * unsorted ones isn't disturbed; later readers that don't care about the
     * order get the sorted ones too.
     */
    template<typename F>
    std::shared_ptr<const rows_t> get(bool sorted, F build)
    {
        auto key = merge_key::current();
        if (!this->mr_rows || !this->mr_key || !(this->mr_key.value() == key)) {
            auto rows = std::make_shared<rows_t>(build());
            if (sorted) {
                std::stable_sort(rows->begin(), rows->end());
            }
            this->mr_rows = std::move(rows);
            this->mr_key = key;
            this->mr_sorted = sorted;
        } else if (sorted && !this->mr_sorted) {
            auto rows = std::make_shared<rows_t>(*this->mr_rows);
            std::stable_sort(rows->begin(), rows->end());
            this->mr_rows = std::move(rows);
            this->mr_sorted = true;
        }
        return this->mr_rows;
    }

    /** Drops the rows, e.g. after an UPDATE changed what they hold. */
    void invalidate() { this->mr_rows.reset(); }
};

struct all_opids {
    static constexpr const char* NAME = "all_opids";
    static constexpr const char* CREATE_STMT = R"(
CREATE TABLE lnav_db.all_opids (
    opid TEXT PRIMARY KEY,         -- The operation ID
    earliest DATETIME,             -- The earliest time this ID was seen
    latest DATETIME,               -- The latest time this ID was seen
    duration INTEGER,              -- The amount of time between earliest and latest in microseconds
    errors INTEGER,                -- The number of error messages associated with this ID
    warnings INTEGER,              -- The number of warning messages associated with this ID
    total INTEGER,                 -- The total number of messages associated with this ID
    definition TEXT,               -- The name of the opid description from the log format, if available
    description TEXT,              -- A description of the operation
    earliest_usecs INTEGER HIDDEN, -- The earliest column, in microseconds since the epoch
    latest_usecs INTEGER HIDDEN    -- The latest column, in microseconds since the epoch
);
)";

    struct cursor {
        struct opid_time_pair {
            string_fragment otp_opid;
            /**
             * The opid's range and level counts, merged across the files.  Not
             * the whole opid_time_range: its description elements (strings)
             * and sub-operations were copied into the map and the vector, and
             * merged, for every opid, though no column shows them.
             */
            time_range otp_time;
            log_level_stats otp_stats;
            intern_string_t otp_name;
            /**
             * The file whose description of the opid is shown, looked up by
             * otp_opid and made into text only when the column is read:
             * building every opid's description cost an allocation and a
             * format call each, for queries that never read it.  The file, not
             * a pointer into its opid map, since setting a line's opid can add
             * to that map, and a rehash would move its entries.
             */
            logfile* otp_desc_file{nullptr};
            /** The opid's range in otp_desc_file. */
            time_range otp_desc_time;

            bool operator<(const opid_time_pair& rhs) const
            {
                return this->otp_time < rhs.otp_time;
            }

            std::string description() const
            {
                if (this->otp_desc_file == nullptr) {
                    return {};
                }
                auto opids = this->otp_desc_file->get_opids().readAccess();
                auto iter = opids->los_opid_ranges.find(this->otp_opid);
                if (iter == opids->los_opid_ranges.end()) {
                    return {};
                }
                return describe(this->otp_desc_file->get_format_ptr(),
                                &iter->second.otr_description);
            }
        };

        /** The text of a file's description of an opid, as the column shows it.
         */
        static std::string describe(const log_format* format,
                                    const log_op_description* desc)
        {
            if (desc == nullptr) {
                return {};
            }
            if (desc->lod_index.has_value()) {
                return format->lf_opid_description_def_vec
                    ->at(desc->lod_index.value())
                    ->to_string(desc->lod_elements);
            }
            if (!desc->lod_elements.empty()) {
                return desc->lod_elements.entries().front().second;
            }
            return {};
        }

        using pair_map = robin_hood::unordered_map<string_fragment,
                                                   opid_time_pair,
                                                   frag_hasher,
                                                   frag_equal>;

        sqlite3_vtab_cursor base{};
        /** The rows: the shared merged ones, or a lookup's own. */
        std::shared_ptr<const std::vector<opid_time_pair>> c_opids;
        std::vector<opid_time_pair>::const_iterator c_iter;

        /** Every opid in every file, merged; see merged_rows. */
        static merged_rows<opid_time_pair>& merged()
        {
            static merged_rows<opid_time_pair> retval;
            return retval;
        }

        explicit cursor(sqlite3_vtab* vt) { this->base.pVtab = vt; }

        /**
         * Merges a file's range for the opid [key] into [gather_map], with
         * the description from the earliest file that has one.  The key
         * points into the file's opid map, which outlives the query.
         */
        static void gather(pair_map& gather_map,
                           const std::shared_ptr<logfile>& lf,
                           const string_fragment& key,
                           const opid_time_range& om)
        {
            auto [gather_iter, inserted] = gather_map.try_emplace(
                key, opid_time_pair{key, om.otr_range, om.otr_level_stats});
            auto& pair = gather_iter->second;
            if (!inserted) {
                pair.otp_time |= om.otr_range;
                pair.otp_stats |= om.otr_level_stats;
            }
            choose_description(pair, lf, om);
        }

        /**
         * Makes [lf]'s description of the opid [pair]'s if it has one and
         * either starts earlier than the one chosen so far or that one's text
         * is empty, so the earliest file with a description wins.
         */
        static void choose_description(opid_time_pair& pair,
                                       const std::shared_ptr<logfile>& lf,
                                       const opid_time_range& om)
        {
            const auto& desc = om.otr_description;
            if (!desc.lod_index.has_value() && desc.lod_elements.empty()) {
                return;
            }
            // Only an opid in several files needs the text of the description
            // it has so far, to see whether it's empty.  That description is
            // another file's, whose opid map isn't the one locked here.
            if (pair.otp_desc_file != nullptr
                && !(om.otr_range < pair.otp_desc_time)
                && !pair.description().empty())
            {
                return;
            }
            pair.otp_desc_file = lf.get();
            pair.otp_desc_time = om.otr_range;
            pair.otp_name = desc.lod_index.has_value()
                ? lf->get_format_ptr()
                      ->lf_opid_description_def_vec->at(desc.lod_index.value())
                      ->od_name
                : intern_string_t{};
        }

        /**
         * Collects the rows: every opid in every file, or with an `opid = ?`
         * (OPID_EQ) or `opid IN (...)` (OPID_IN) constraint, just those,
         * looked up in each file's opid map instead of copying all of them.
         */
        int filter(int idxNum,
                   const char* idxStr,
                   int argc,
                   sqlite3_value** argv)
        {
            const auto& active_files = injector::get<file_collection&>();
            pair_map gather_map;
            const auto sorted = (idxNum & OPID_UNSORTED) == 0;
            idxNum &= ~OPID_UNSORTED;

            if (idxNum == OPID_ALL || argc < 1) {
                this->c_opids = merged().get(sorted, [&active_files]() {
                    pair_map all_map;
                    for (const auto& lf : active_files.fc_files) {
                        auto lf_opids = lf->get_opids().readAccess();
                        for (const auto& [key, om] : lf_opids->los_opid_ranges)
                        {
                            gather(all_map, lf, key, om);
                        }
                    }
                    std::vector<opid_time_pair> rows;
                    rows.reserve(all_map.size());
                    for (const auto& [key, value] : all_map) {
                        rows.emplace_back(value);
                    }
                    return rows;
                });
                return this->reset();
            }
            {
                std::vector<std::string> keys;
                auto add_key = [&keys](sqlite3_value* val) {
                    if (val == nullptr
                        || sqlite3_value_type(val) == SQLITE_NULL)
                    {
                        return;
                    }
                    const auto* text = sqlite3_value_text(val);
                    keys.emplace_back((const char*) text,
                                      sqlite3_value_bytes(val));
                };
                if (idxNum == OPID_IN) {
                    sqlite3_value* val = nullptr;
                    for (auto rc = sqlite3_vtab_in_first(argv[0], &val);
                         rc == SQLITE_OK && val != nullptr;
                         rc = sqlite3_vtab_in_next(argv[0], &val))
                    {
                        add_key(val);
                    }
                } else {
                    add_key(argv[0]);
                }
                for (const auto& lf : active_files.fc_files) {
                    auto lf_opids = lf->get_opids().readAccess();
                    for (const auto& key : keys) {
                        auto key_sf = string_fragment::from_str(key);
                        auto iter = lf_opids->los_opid_ranges.find(key_sf);
                        if (iter != lf_opids->los_opid_ranges.end()) {
                            gather(gather_map, lf, iter->first, iter->second);
                        }
                    }
                }
            }
            auto rows = std::make_shared<std::vector<opid_time_pair>>();
            rows->reserve(gather_map.size());
            for (const auto& [key, value] : gather_map) {
                rows->emplace_back(value);
            }
            if (sorted) {
                std::stable_sort(rows->begin(), rows->end());
            }
            this->c_opids = std::move(rows);

            return this->reset();
        }

        int reset()
        {
            this->c_iter = this->c_opids->begin();

            return SQLITE_OK;
        }

        int next()
        {
            if (this->c_iter != this->c_opids->end()) {
                ++this->c_iter;
            }

            return SQLITE_OK;
        }

        int eof() const { return this->c_iter == this->c_opids->end(); }

        int get_rowid(sqlite_int64& rowid_out) const
        {
            rowid_out = std::distance(this->c_opids->begin(), this->c_iter);

            return SQLITE_OK;
        }
    };

    static constexpr int OPID_ALL = 0;
    static constexpr int OPID_EQ = 1;
    static constexpr int OPID_IN = 2;
    /**
     * Or'd in when the query has an ORDER BY, which SQLite sorts itself: the
     * rows needn't be put in time order first.
     */
    static constexpr int OPID_UNSORTED = 0x10;

    /**
     * Uses an `opid = ?` or `opid IN (...)` constraint (the whole IN list at
     * once) to look the opids up instead of listing them all.
     */
    int best_index(sqlite3_vtab* tab, sqlite3_index_info* p_info)
    {
        p_info->idxNum = OPID_ALL;
        p_info->estimatedCost = 1000000.0;
        for (int lpc = 0; lpc < p_info->nConstraint; lpc++) {
            const auto& cons = p_info->aConstraint[lpc];
            if (!cons.usable || cons.iColumn != 0
                || cons.op != SQLITE_INDEX_CONSTRAINT_EQ)
            {
                continue;
            }
            p_info->aConstraintUsage[lpc].argvIndex = 1;
            p_info->aConstraintUsage[lpc].omit = 1;
            if (sqlite3_vtab_in(p_info, lpc, -1)) {
                sqlite3_vtab_in(p_info, lpc, 1);
                p_info->idxNum = OPID_IN;
                p_info->estimatedCost = 100.0;
            } else {
                p_info->idxNum = OPID_EQ;
                p_info->estimatedCost = 10.0;
                p_info->estimatedRows = 1;
                p_info->idxFlags = SQLITE_INDEX_SCAN_UNIQUE;
            }
            break;
        }
        // The ORDER BY isn't consumed, so SQLite sorts the rows anyway.
        if (p_info->nOrderBy > 0) {
            p_info->idxNum |= OPID_UNSORTED;
        }

        return SQLITE_OK;
    }

    int get_column(cursor& vc, sqlite3_context* ctx, int col)
    {
        switch (col) {
            case 0: {
                to_sqlite(ctx, vc.c_iter->otp_opid);
                break;
            }
            case 1: {
                to_sqlite(ctx, vc.c_iter->otp_time.tr_begin);
                break;
            }
            case 2: {
                to_sqlite(ctx, vc.c_iter->otp_time.tr_end);
                break;
            }
            case 3: {
                to_sqlite(ctx, vc.c_iter->otp_time.duration().count());
                break;
            }
            case 4: {
                to_sqlite(ctx, vc.c_iter->otp_stats.lls_error_count);
                break;
            }
            case 5: {
                to_sqlite(ctx, vc.c_iter->otp_stats.lls_warning_count);
                break;
            }
            case 6: {
                to_sqlite(ctx, vc.c_iter->otp_stats.lls_total_count);
                break;
            }
            case 7: {
                to_sqlite(ctx, vc.c_iter->otp_name);
                break;
            }
            case 8: {
                auto desc = vc.c_iter->description();
                if (desc.empty()) {
                    sqlite3_result_null(ctx);
                } else {
                    to_sqlite(ctx, desc);
                }
                break;
            }
            case 9: {
                to_sqlite(ctx, (int64_t) vc.c_iter->otp_time.tr_begin.count());
                break;
            }
            case 10: {
                to_sqlite(ctx, (int64_t) vc.c_iter->otp_time.tr_end.count());
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
                   int64_t duration,
                   int64_t errors,
                   int64_t warnings,
                   int64_t total,
                   std::optional<string_fragment> definition,
                   std::optional<string_fragment> description,
                   std::optional<int64_t> earliest_usecs,
                   std::optional<int64_t> latest_usecs)
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
CREATE TABLE lnav_db.all_thread_ids (
    thread_id TEXT PRIMARY KEY,    -- The thread ID
    earliest DATETIME,             -- The earliest time this ID was seen
    latest DATETIME,               -- The latest time this ID was seen
    duration INTEGER,              -- The amount of time between earliest and latest in microseconds
    errors INTEGER,                -- The number of error messages associated with this ID
    warnings INTEGER,              -- The number of warning messages associated with this ID
    total INTEGER,                 -- The total number of messages associated with this ID
    earliest_usecs INTEGER HIDDEN, -- The earliest column, in microseconds since the epoch
    latest_usecs INTEGER HIDDEN    -- The latest column, in microseconds since the epoch
);
)";

    struct cursor {
        struct thread_id_time_pair {
            string_fragment titp_thread_id;
            thread_id_time_range titp_range;

            bool operator<(const thread_id_time_pair& rhs) const
            {
                return this->titp_range < rhs.titp_range;
            }
        };

        using pair_map = robin_hood::unordered_map<string_fragment,
                                                   thread_id_time_pair,
                                                   frag_hasher,
                                                   frag_equal>;

        sqlite3_vtab_cursor base{};
        /**
         * The rows, in time order unless the query sorts them itself (see
         * best_index()): the shared merged ones, or a lookup's own.  Copies,
         * not pointers into the gathering map: sorting
         * pointers scattered through it was slower (a cache miss per compare).
         */
        std::shared_ptr<const std::vector<thread_id_time_pair>> c_thread_ids;
        std::vector<thread_id_time_pair>::const_iterator c_iter;

        /** Every thread in every file, merged; see merged_rows. */
        static merged_rows<thread_id_time_pair>& merged()
        {
            static merged_rows<thread_id_time_pair> retval;
            return retval;
        }

        explicit cursor(sqlite3_vtab* vt) { this->base.pVtab = vt; }

        /**
         * Merges a file's range for the thread [key] into [gather_map].  The
         * key points into the file's thread map, which outlives the query.
         */
        static void gather(pair_map& gather_map,
                           const string_fragment& key,
                           const thread_id_time_range& om)
        {
            auto [gather_iter, inserted]
                = gather_map.try_emplace(key, thread_id_time_pair{key, om});
            if (!inserted) {
                gather_iter->second.titp_range |= om;
            }
        }

        /**
         * Collects the rows: every thread in every file, or with a
         * `thread_id = ?` (TID_EQ) or `thread_id IN (...)` (TID_IN)
         * constraint, just those, looked up in each file's thread map.
         */
        int filter(int idxNum,
                   const char* idxStr,
                   int argc,
                   sqlite3_value** argv)
        {
            const auto& active_files = injector::get<file_collection&>();
            pair_map gather_map;
            const auto sorted = (idxNum & TID_UNSORTED) == 0;
            idxNum &= ~TID_UNSORTED;

            if (idxNum == TID_ALL || argc < 1) {
                this->c_thread_ids = merged().get(sorted, [&active_files]() {
                    pair_map all_map;
                    for (const auto& lf : active_files.fc_files) {
                        auto lf_thread_ids = lf->get_thread_ids().readAccess();
                        for (const auto& [key, om] :
                             lf_thread_ids->ltis_tid_ranges)
                        {
                            gather(all_map, key, om);
                        }
                    }
                    std::vector<thread_id_time_pair> rows;
                    rows.reserve(all_map.size());
                    for (const auto& [key, value] : all_map) {
                        rows.emplace_back(value);
                    }
                    return rows;
                });
                return this->reset();
            }
            {
                std::vector<std::string> keys;
                auto add_key = [&keys](sqlite3_value* val) {
                    if (val == nullptr
                        || sqlite3_value_type(val) == SQLITE_NULL)
                    {
                        return;
                    }
                    const auto* text = sqlite3_value_text(val);
                    keys.emplace_back((const char*) text,
                                      sqlite3_value_bytes(val));
                };
                if (idxNum == TID_IN) {
                    sqlite3_value* val = nullptr;
                    for (auto rc = sqlite3_vtab_in_first(argv[0], &val);
                         rc == SQLITE_OK && val != nullptr;
                         rc = sqlite3_vtab_in_next(argv[0], &val))
                    {
                        add_key(val);
                    }
                } else {
                    add_key(argv[0]);
                }
                for (const auto& lf : active_files.fc_files) {
                    auto lf_thread_ids = lf->get_thread_ids().readAccess();
                    for (const auto& key : keys) {
                        auto key_sf = string_fragment::from_str(key);
                        auto iter = lf_thread_ids->ltis_tid_ranges.find(key_sf);
                        if (iter != lf_thread_ids->ltis_tid_ranges.end()) {
                            gather(gather_map, iter->first, iter->second);
                        }
                    }
                }
            }
            auto rows = std::make_shared<std::vector<thread_id_time_pair>>();
            rows->reserve(gather_map.size());
            for (const auto& [key, value] : gather_map) {
                rows->emplace_back(value);
            }
            if (sorted) {
                std::stable_sort(rows->begin(), rows->end());
            }
            this->c_thread_ids = std::move(rows);

            return this->reset();
        }

        int reset()
        {
            this->c_iter = this->c_thread_ids->begin();

            return SQLITE_OK;
        }

        int next()
        {
            if (this->c_iter != this->c_thread_ids->end()) {
                ++this->c_iter;
            }

            return SQLITE_OK;
        }

        int eof() const { return this->c_iter == this->c_thread_ids->end(); }

        int get_rowid(sqlite_int64& rowid_out) const
        {
            rowid_out
                = std::distance(this->c_thread_ids->begin(), this->c_iter);

            return SQLITE_OK;
        }
    };

    static constexpr int TID_ALL = 0;
    static constexpr int TID_EQ = 1;
    static constexpr int TID_IN = 2;
    /**
     * Or'd in when the query has an ORDER BY, which SQLite sorts itself: the
     * rows needn't be put in time order first.
     */
    static constexpr int TID_UNSORTED = 0x10;

    /**
     * Uses a `thread_id = ?` or `thread_id IN (...)` constraint (the whole
     * IN list at once) to look the threads up instead of listing them all.
     */
    int best_index(sqlite3_vtab* tab, sqlite3_index_info* p_info)
    {
        p_info->idxNum = TID_ALL;
        p_info->estimatedCost = 1000000.0;
        for (int lpc = 0; lpc < p_info->nConstraint; lpc++) {
            const auto& cons = p_info->aConstraint[lpc];
            if (!cons.usable || cons.iColumn != 0
                || cons.op != SQLITE_INDEX_CONSTRAINT_EQ)
            {
                continue;
            }
            p_info->aConstraintUsage[lpc].argvIndex = 1;
            p_info->aConstraintUsage[lpc].omit = 1;
            if (sqlite3_vtab_in(p_info, lpc, -1)) {
                sqlite3_vtab_in(p_info, lpc, 1);
                p_info->idxNum = TID_IN;
                p_info->estimatedCost = 100.0;
            } else {
                p_info->idxNum = TID_EQ;
                p_info->estimatedCost = 10.0;
                p_info->estimatedRows = 1;
                p_info->idxFlags = SQLITE_INDEX_SCAN_UNIQUE;
            }
            break;
        }
        // The ORDER BY isn't consumed, so SQLite sorts the rows anyway.
        if (p_info->nOrderBy > 0) {
            p_info->idxNum |= TID_UNSORTED;
        }

        return SQLITE_OK;
    }

    int get_column(cursor& vc, sqlite3_context* ctx, int col)
    {
        const auto& row = *vc.c_iter;
        switch (col) {
            case 0: {
                to_sqlite(ctx, row.titp_thread_id);
                break;
            }
            case 1: {
                to_sqlite(ctx, row.titp_range.titr_range.tr_begin);
                break;
            }
            case 2: {
                to_sqlite(ctx, row.titp_range.titr_range.tr_end);
                break;
            }
            case 3: {
                to_sqlite(ctx, row.titp_range.titr_range.duration().count());
                break;
            }
            case 4: {
                to_sqlite(ctx, row.titp_range.titr_level_stats.lls_error_count);
                break;
            }
            case 5: {
                to_sqlite(ctx,
                          row.titp_range.titr_level_stats.lls_warning_count);
                break;
            }
            case 6: {
                to_sqlite(ctx, row.titp_range.titr_level_stats.lls_total_count);
                break;
            }
            case 7: {
                to_sqlite(ctx,
                          (int64_t) row.titp_range.titr_range.tr_begin.count());
                break;
            }
            case 8: {
                to_sqlite(ctx,
                          (int64_t) row.titp_range.titr_range.tr_end.count());
                break;
            }
        }

        return SQLITE_OK;
    }
};

/**
 * One row per thread and opid: the operations each thread logged in, across
 * every file, with their times and level counts.  Built from the thread and
 * opid the scan recorded for each line (logfile::line_ids), so no message is
 * read except the few the scan didn't record (a file's first lines, read while
 * its format was detected).  Like all_thread_ids and all_opids, it reads the
 * files, not the LOG view, so it doesn't follow lnav's filters.
 */
struct all_thread_opids {
    static constexpr const char* NAME = "all_thread_opids";
    static constexpr const char* CREATE_STMT = R"(
CREATE TABLE lnav_db.all_thread_opids (
    thread_id TEXT,                -- The thread ID, or NULL for messages without one
    opid TEXT,                     -- The operation ID, or NULL for the thread's messages without one
    earliest DATETIME,             -- The earliest time the thread logged in this operation
    latest DATETIME,               -- The latest time the thread logged in this operation
    duration INTEGER,              -- The amount of time between earliest and latest in microseconds
    errors INTEGER,                -- The number of error messages the thread logged in this operation
    warnings INTEGER,              -- The number of warning messages the thread logged in this operation
    total INTEGER,                 -- The total number of messages the thread logged in this operation
    description TEXT,              -- The operation's description, if the format gives one
    earliest_usecs INTEGER HIDDEN, -- The earliest column, in microseconds since the epoch
    latest_usecs INTEGER HIDDEN    -- The latest column, in microseconds since the epoch
);
)";

    struct row {
        /** Invalid fragments for "none". */
        string_fragment r_thread;
        string_fragment r_opid;
        time_range r_time;
        log_level_stats r_stats;

        bool operator<(const row& rhs) const
        {
            return this->r_time < rhs.r_time;
        }
    };

    /** The rows, in time order, and an index of them by thread. */
    struct row_set {
        std::vector<row> rs_rows;
        /** Holds the rows' ids, so they don't depend on the files. */
        ArenaAlloc::Alloc<char> rs_allocator{4096};
        robin_hood::unordered_map<string_fragment,
                                  std::vector<uint32_t>,
                                  frag_hasher,
                                  frag_equal>
            rs_by_thread;
    };

    struct pair_hasher {
        size_t operator()(
            const std::pair<string_fragment, string_fragment>& p) const
        {
            auto h1 = p.first.is_valid() ? frag_hasher{}(p.first) : 0x9e37;
            auto h2 = p.second.is_valid() ? frag_hasher{}(p.second) : 0x7f4a;
            return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
        }
    };

    static std::shared_ptr<const row_set> build()
    {
        const auto& active_files = injector::get<file_collection&>();
        auto retval = std::make_shared<row_set>();
        robin_hood::unordered_map<std::pair<string_fragment, string_fragment>,
                                  uint32_t,
                                  pair_hasher>
            pair_rows;

        for (const auto& lf : active_files.fc_files) {
            const auto* format = lf->get_format_ptr();
            if (format == nullptr) {
                continue;
            }
            const auto& ids = lf->get_line_ids();
            const auto& bm = lf->get_bookmark_metadata();
            logline_value_vector values;
            string_attrs_t sa;
            // The row of the last line, since a thread's lines mostly come
            // in runs of the same operation.
            std::optional<uint32_t> last_row;
            string_fragment last_tid = string_fragment::invalid();
            string_fragment last_opid = string_fragment::invalid();

            const auto line_count = lf->size();
            for (size_t line = 0; line < line_count; line++) {
                const auto ll = lf->begin() + line;
                if (ll->is_continued() || ll->is_ignored()) {
                    continue;
                }

                const auto* tid_p = ids.li_tids.get(line);
                const auto* opid_p = ids.li_opids.get(line);
                string_fragment tid;
                string_fragment opid;
                if (tid_p == nullptr || opid_p == nullptr) {
                    // Not recorded by the scan: read the message.
                    values.clear();
                    sa.clear();
                    lf->read_full_message(ll, values.lvv_sbr);
                    values.lvv_sbr.erase_ansi();
                    format->annotate(lf.get(), line, sa, values);
                    tid = values.lvv_thread_id_value.value_or(
                        string_fragment::invalid());
                    opid = values.lvv_opid_value
                        ? string_fragment::from_str(
                              values.lvv_opid_value.value())
                        : string_fragment::invalid();
                } else {
                    tid = *tid_p;
                    opid = *opid_p;
                    if (!bm.empty()) {
                        auto bm_iter = bm.find(line);
                        if (bm_iter != bm.end()
                            && !bm_iter->second.bm_opid.empty())
                        {
                            // The user's opid, which the scan didn't see.
                            opid = string_fragment::from_str(
                                bm_iter->second.bm_opid);
                        }
                    }
                }

                uint32_t row_index;
                if (last_row && tid == last_tid && opid == last_opid) {
                    row_index = last_row.value();
                } else {
                    auto iter = pair_rows.find(std::make_pair(tid, opid));
                    if (iter == pair_rows.end()) {
                        // tid and opid may point into the message buffer,
                        // so the key and the row get copies.
                        row r{tid.to_owned(retval->rs_allocator),
                              opid.to_owned(retval->rs_allocator),
                              {},
                              {}};
                        r.r_time.invalidate();
                        iter
                            = pair_rows
                                  .emplace(std::make_pair(r.r_thread, r.r_opid),
                                           retval->rs_rows.size())
                                  .first;
                        retval->rs_rows.emplace_back(r);
                    }
                    row_index = iter->second;
                    last_row = row_index;
                    last_tid = iter->first.first;
                    last_opid = iter->first.second;
                }

                auto& r = retval->rs_rows[row_index];
                r.r_time.extend_to(ll->get_time<std::chrono::microseconds>());
                r.r_stats.update_msg_count(ll->get_msg_level());
            }
        }

        std::stable_sort(retval->rs_rows.begin(), retval->rs_rows.end());
        for (uint32_t index = 0; index < retval->rs_rows.size(); index++) {
            const auto& r = retval->rs_rows[index];
            if (r.r_thread.is_valid()) {
                retval->rs_by_thread[r.r_thread].emplace_back(index);
            }
        }
        return retval;
    }

    /** The rows, built again only when the files changed (merge_key). */
    static std::shared_ptr<const row_set> rows()
    {
        static std::optional<merge_key> key;
        static std::shared_ptr<const row_set> cached;

        auto current = merge_key::current();
        if (!cached || !key || !(key.value() == current)) {
            cached = build();
            key = current;
        }
        return cached;
    }

    struct cursor {
        sqlite3_vtab_cursor base{};
        std::shared_ptr<const row_set> c_set;
        /** The rows a thread_id constraint picked, or all of them. */
        std::optional<std::vector<uint32_t>> c_picked;
        size_t c_pos{0};

        explicit cursor(sqlite3_vtab* vt) { this->base.pVtab = vt; }

        int filter(int idxNum,
                   const char* idxStr,
                   int argc,
                   sqlite3_value** argv)
        {
            this->c_set = rows();
            this->c_picked.reset();
            if (idxNum != TOP_ALL && argc >= 1) {
                std::vector<uint32_t> picked;
                const auto add_thread = [this, &picked](sqlite3_value* val) {
                    if (val == nullptr
                        || sqlite3_value_type(val) == SQLITE_NULL)
                    {
                        return;
                    }
                    const auto* text = (const char*) sqlite3_value_text(val);
                    auto key = string_fragment::from_bytes(
                        text, sqlite3_value_bytes(val));
                    auto iter = this->c_set->rs_by_thread.find(key);
                    if (iter != this->c_set->rs_by_thread.end()) {
                        picked.insert(picked.end(),
                                      iter->second.begin(),
                                      iter->second.end());
                    }
                };
                if (idxNum == TOP_IN) {
                    sqlite3_value* val = nullptr;
                    for (auto rc = sqlite3_vtab_in_first(argv[0], &val);
                         rc == SQLITE_OK && val != nullptr;
                         rc = sqlite3_vtab_in_next(argv[0], &val))
                    {
                        add_thread(val);
                    }
                } else {
                    add_thread(argv[0]);
                }
                // In time order, like the whole table, and once each even if
                // the IN list repeats a thread.
                std::sort(picked.begin(), picked.end());
                picked.erase(std::unique(picked.begin(), picked.end()),
                             picked.end());
                this->c_picked = std::move(picked);
            }
            return this->reset();
        }

        int reset()
        {
            this->c_pos = 0;
            return SQLITE_OK;
        }

        size_t size() const
        {
            return this->c_picked ? this->c_picked->size()
                                  : this->c_set->rs_rows.size();
        }

        const row& current() const
        {
            return this->c_set
                ->rs_rows[this->c_picked ? (*this->c_picked)[this->c_pos]
                                         : this->c_pos];
        }

        int next()
        {
            if (this->c_pos < this->size()) {
                this->c_pos += 1;
            }
            return SQLITE_OK;
        }

        int eof() const { return this->c_pos >= this->size(); }

        int get_rowid(sqlite_int64& rowid_out) const
        {
            rowid_out = this->c_pos;
            return SQLITE_OK;
        }
    };

    static constexpr int TOP_ALL = 0;
    static constexpr int TOP_EQ = 1;
    static constexpr int TOP_IN = 2;

    /**
     * Uses a `thread_id = ?` or `thread_id IN (...)` constraint (the whole
     * IN list at once) to pick the threads' rows from the index.
     */
    int best_index(sqlite3_vtab* tab, sqlite3_index_info* p_info)
    {
        p_info->idxNum = TOP_ALL;
        p_info->estimatedCost = 1000000.0;
        for (int lpc = 0; lpc < p_info->nConstraint; lpc++) {
            const auto& cons = p_info->aConstraint[lpc];
            if (!cons.usable || cons.iColumn != 0
                || cons.op != SQLITE_INDEX_CONSTRAINT_EQ)
            {
                continue;
            }
            p_info->aConstraintUsage[lpc].argvIndex = 1;
            p_info->aConstraintUsage[lpc].omit = 1;
            if (sqlite3_vtab_in(p_info, lpc, -1)) {
                sqlite3_vtab_in(p_info, lpc, 1);
                p_info->idxNum = TOP_IN;
                p_info->estimatedCost = 100.0;
            } else {
                p_info->idxNum = TOP_EQ;
                p_info->estimatedCost = 10.0;
            }
            break;
        }

        return SQLITE_OK;
    }

    static void to_sqlite_id(sqlite3_context* ctx, const string_fragment& sf)
    {
        if (!sf.is_valid()) {
            sqlite3_result_null(ctx);
        } else {
            // An empty fragment may have no data, which SQLite would take
            // for NULL.
            sqlite3_result_text(ctx,
                                sf.empty() ? "" : sf.data(),
                                sf.length(),
                                SQLITE_TRANSIENT);
        }
    }

    /** The opid's description, as all_opids shows it. */
    static std::string description(const string_fragment& opid)
    {
        if (!opid.is_valid()) {
            return {};
        }
        const auto& active_files = injector::get<file_collection&>();
        all_opids::cursor::opid_time_pair pair{opid};
        for (const auto& lf : active_files.fc_files) {
            auto opids = lf->get_opids().readAccess();
            auto iter = opids->los_opid_ranges.find(opid);
            if (iter != opids->los_opid_ranges.end()) {
                all_opids::cursor::choose_description(pair, lf, iter->second);
            }
        }
        return pair.description();
    }

    int get_column(cursor& vc, sqlite3_context* ctx, int col)
    {
        const auto& r = vc.current();
        switch (col) {
            case 0:
                to_sqlite_id(ctx, r.r_thread);
                break;
            case 1:
                to_sqlite_id(ctx, r.r_opid);
                break;
            case 2:
                to_sqlite(ctx, r.r_time.tr_begin);
                break;
            case 3:
                to_sqlite(ctx, r.r_time.tr_end);
                break;
            case 4:
                to_sqlite(ctx, r.r_time.duration().count());
                break;
            case 5:
                to_sqlite(ctx, r.r_stats.lls_error_count);
                break;
            case 6:
                to_sqlite(ctx, r.r_stats.lls_warning_count);
                break;
            case 7:
                to_sqlite(ctx, r.r_stats.lls_total_count);
                break;
            case 8: {
                auto desc = description(r.r_opid);
                if (desc.empty()) {
                    sqlite3_result_null(ctx);
                } else {
                    to_sqlite(ctx, desc);
                }
                break;
            }
            case 9:
                to_sqlite(ctx, (int64_t) r.r_time.tr_begin.count());
                break;
            case 10:
                to_sqlite(ctx, (int64_t) r.r_time.tr_end.count());
                break;
        }

        return SQLITE_OK;
    }
};

auto all_vtabs_binder
    = injector::bind_multiple<vtab_module_base>()
          .add<vtab_module<all_opids>>()
          .add<vtab_module<tvt_no_update<all_thread_ids>>>()
          .add<vtab_module<tvt_no_update<all_thread_opids>>>();

}  // namespace
