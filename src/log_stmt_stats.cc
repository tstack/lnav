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

#include <algorithm>
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "log_stmt_stats.hh"

#include "all_logs_vtab.hh"
#include "ArenaAlloc/arenaalloc.h"
#include "base/injector.bind.hh"
#include "base/lnav_log.hh"
#include "base/parallel_for.hh"
#include "base/progress.hh"
#include "base/string_attr_type.hh"
#include "config.h"
#include "fmt/format.h"
#include "hasher.hh"
#include "lnav.hh"
#include "log_format.hh"
#include "logfile.hh"
#include "logfile_sub_source.hh"
#include "vtab_module.hh"

#ifdef HAVE_RUST_DEPS
#    include "lnav_rs_ext.cxx.hh"
#endif

namespace {

/**
 * The per-line values in a file's map: not looked at yet, no statement (also
 * used for continuation lines, which never start a message), or a statement.
 */
constexpr uint32_t UNMAPPED = 0;
constexpr uint32_t NO_STATEMENT = 1;
constexpr uint32_t FIRST_STATEMENT = 2;

/** At most this many time buckets per statement in the summary. */
constexpr int64_t MAX_BUCKETS = 32;

/** Bucket keys are log_time_msecs shifted right by this, about 4 seconds. */
constexpr int BASE_SHIFT = 12;

struct statement {
    /** log2src's source details, as JSON, like all_logs.log_msg_src. */
    std::string s_src;
    /** hash(src, pattern), like all_logs.log_msg_schema. */
    std::string s_schema;
};

struct stmt_map {
    /** Set by the first query of the summary table. */
    bool sm_active{false};
    std::vector<statement> sm_statements{statement{}, statement{}};
#ifdef HAVE_RUST_DEPS
    /** The source scan version the statements are from. */
    std::optional<size_t> sm_scan_version;
#endif

    // Read by the progress reporter on the external-access threads.
    std::atomic<bool> sm_working{false};
    std::atomic<size_t> sm_version{0};
    std::atomic<size_t> sm_completed{0};
    std::atomic<size_t> sm_total{0};

    /** Bumped when the mapping changes, so a cached summary is redone. */
    size_t sm_changes{0};
    /**
     * Bumped when a mapped line gets a different statement (after a source
     * re-scan), unlike lines mapped for the first time.
     */
    size_t sm_remaps{0};

    /**
     * Statement ids by log_msg_schema.  The schema covers the pattern as well
     * as the source details, so a statement whose message changes in a source
     * re-scan gets a new id.
     */
    std::unordered_map<std::string, uint32_t> sm_schema_ids;

    /**
     * Each statement's LOG view rows, ascending, for all_logs' schema
     * constraints.  Kept up to date like the column indexes in
     * log_vtab_impl.cc: rows from the first changed one on are read again.
     */
    struct statement_rows {
        std::vector<std::vector<vis_line_t>> sr_rows;
        bool sr_valid{false};
        uint64_t sr_index_change_seq{0};
        uint32_t sr_index_generation{0};
        size_t sr_remaps{0};
        size_t sr_row_count{0};
    } sm_rows;

    /**
     * Guards sm_statements and sm_schema_ids while files are mapped in
     * parallel.
     */
    std::mutex sm_intern_mutex;

    /**
     * The statement's id, from the worker's own [cache] when it has seen the
     * statement before, else from the shared table.
     */
    template<typename S>
    uint32_t intern_cached(
        std::unordered_map<string_fragment, uint32_t, frag_hasher, frag_equal>&
            cache,
        ArenaAlloc::Alloc<char>& arena,
        const S& src,
        const S& pattern)
    {
        char schema_buf[hasher::STRING_SIZE];
        all_logs_vtab::statement_schema(string_fragment::from(src),
                                        string_fragment::from(pattern),
                                        schema_buf);
        auto schema = string_fragment::from(schema_buf);

        auto iter = cache.find(schema);
        if (iter != cache.end()) {
            return iter->second;
        }

        uint32_t id;
        {
            std::lock_guard<std::mutex> lg(this->sm_intern_mutex);
            id = this->intern(src, schema);
        }
        cache.emplace(schema.to_owned(arena), id);
        return id;
    }

    /** Callers hold sm_intern_mutex while files are being mapped. */
    template<typename S>
    uint32_t intern(const S& src, const string_fragment& schema)
    {
        auto schema_str = schema.to_string();
        auto iter = this->sm_schema_ids.find(schema_str);
        if (iter != this->sm_schema_ids.end()) {
            return iter->second;
        }

        auto id = static_cast<uint32_t>(this->sm_statements.size());
        this->sm_statements.emplace_back(
            statement{(std::string) src, schema_str});
        this->sm_schema_ids.emplace(std::move(schema_str), id);
        return id;
    }

    /**
     * Sizes each open file's statement ids to the file.  The file clears them
     * when it throws its index away, and they go with it when it's closed.
     */
    static void sync_files(logfile_sub_source& lss)
    {
        for (const auto& ld : lss) {
            auto* lf = ld->get_file_ptr();
            if (lf == nullptr) {
                continue;
            }

            auto& si = lf->get_stmt_ids();
            if (si.si_ids.size() > lf->size()) {
                si.si_ids.resize(lf->size());
                si.si_verified = std::min(si.si_verified, si.si_ids.size());
            }
            si.si_ids.resize(lf->size(), UNMAPPED);
        }
    }

    /** True if a file has lines that haven't been matched yet. */
    static bool is_behind(logfile_sub_source& lss)
    {
        for (const auto& ld : lss) {
            const auto* lf = ld->get_file_ptr();
            if (lf == nullptr) {
                continue;
            }
            const auto& si = lf->get_stmt_ids();
            if (si.si_ids.size() != lf->size() || si.si_verified < lf->size()) {
                return true;
            }
        }
        return false;
    }

    /** A worker's buffers for match_line(), reused from line to line. */
    struct match_scratch {
        std::unordered_map<string_fragment, uint32_t, frag_hasher, frag_equal>
            ms_cache;
        /** Holds the schemas the cache is keyed by. */
        ArenaAlloc::Alloc<char> ms_arena{4 * 1024};
        string_attrs_t ms_sa;
        logline_value_vector ms_values;
    };

    /** The statement for the message starting at line_number of lf. */
    uint32_t match_line(logfile* lf, size_t line_number, match_scratch& ms)
    {
#ifdef HAVE_RUST_DEPS
        auto ll = lf->begin() + line_number;
        if (ll->is_continued()) {
            return NO_STATEMENT;
        }

        auto* format = lf->get_format_ptr();
        if (format == nullptr) {
            return NO_STATEMENT;
        }

        auto& values = ms.ms_values;
        auto& sa = ms.ms_sa;
        values.clear();
        sa.clear();
        auto& sbr = values.lvv_sbr;
        lf->read_full_message(ll, sbr);
        sbr.erase_ansi();
        if (!sbr.get_metadata().m_valid_utf) {
            return NO_STATEMENT;
        }

        format->annotate(lf, line_number, sa, values);

        auto find_res = all_logs_vtab::find_statement(sbr, sa, values, false);
        if (find_res == nullptr) {
            return NO_STATEMENT;
        }

        return this->intern_cached(
            ms.ms_cache, ms.ms_arena, find_res->src, find_res->pattern);
#else
        return NO_STATEMENT;
#endif
    }
};

stmt_map&
get_map()
{
    static stmt_map retval;

    return retval;
}

lnav::task_progress
stmt_map_progress()
{
    const auto& sm = get_map();

    return lnav::task_progress{
        "log-stmt-map",
        sm.sm_working ? lnav::progress_status_t::working
                      : lnav::progress_status_t::idle,
        sm.sm_version,
        sm.sm_working ? "Mapping log messages to statements" : "",
        sm.sm_completed,
        sm.sm_total,
        {},
        lnav::task_origin_t::incidental,
    };
}

/**
 * One statement's totals over the LOG view, with its message times as runs of
 * (bucket key, count) in view order.
 */
struct stmt_totals {
    int64_t st_hits{0};
    int64_t st_first_time{0};
    int64_t st_last_time{0};
    std::vector<std::pair<int64_t, int64_t>> st_runs;
};

struct log_stmt_stats_vtab : public tvt_iterator_cursor<log_stmt_stats_vtab> {
    struct row {
        uint32_t r_id;
        int64_t r_hits;
        int64_t r_first_time;
        int64_t r_last_time;
        std::string r_buckets;
        int r_bucket_shift;
    };

    using iterator = std::vector<row>::iterator;

    static constexpr const char* NAME = "log_stmt_stats";
    static constexpr const char* CREATE_STMT = R"(
-- The log statements found by log2src (see :add-source-path) with the number
-- of messages each one produced in the LOG view, after filters.  Querying this
-- table starts a background task that maps every message to its statement;
-- until it finishes, the counts cover only the messages mapped so far.
CREATE TABLE lnav_db.log_stmt_stats (
    src TEXT,            -- The statement's source details, like log_msg_src.
    schema TEXT,         -- The statement's log_msg_schema in all_logs.
    hits INTEGER,        -- The number of messages from the statement.
    first_time INTEGER,  -- The log_time_msecs of its first message.
    last_time INTEGER,   -- The log_time_msecs of its last message.
    buckets TEXT,        -- A JSON array of [key, count] for its message times,
                         -- where key is log_time_msecs >> 12, merged so there
                         -- are at most 32 buckets.
    bucket_shift INTEGER,-- How far the keys were merged: each bucket covers
                         -- 4096 << bucket_shift milliseconds.
    mapped INTEGER HIDDEN, -- The messages in the LOG view mapped so far.
    total INTEGER HIDDEN   -- The messages in the LOG view.
);
)";

    std::vector<row> lss_rows;
    int64_t lss_mapped{0};
    int64_t lss_total{0};
    std::optional<std::tuple<uint64_t, uint32_t, size_t, size_t>> lss_rows_key;

    iterator begin()
    {
        this->refresh();
        return this->lss_rows.begin();
    }

    iterator end() { return this->lss_rows.end(); }

    void refresh()
    {
        auto& sm = get_map();
        auto& lss = lnav_data.ld_log_source;

        if (!sm.sm_active) {
            sm.sm_active = true;
            log_info("log_stmt_stats: starting the statement map");
        }
        if (lnav_data.ld_flags.is_set<lnav_flags::headless>()) {
            // There's no main loop to map in slices, so map everything now.
            // step() doesn't map while log2src is scanning the source, so
            // wait for the scan, which is requested work, to finish first.
            lnav::progress_tracker::instance().wait_for_completion(
                &lnav_data.ld_status_refresher);
            lnav::stmt_stats::step(
                std::chrono::steady_clock::time_point::max());
        } else {
            sm.sync_files(lss);
        }

        // The cursor asks for the rows when it's opened and again when it's
        // reset, so only redo them when something changed.
        auto key = std::make_tuple(lss.lss_index_change_seq,
                                   lss.lss_index_generation,
                                   sm.sm_changes,
                                   lss.text_line_count());
        if (this->lss_rows_key == key) {
            return;
        }
        this->lss_rows_key = key;

        std::vector<stmt_totals> totals(sm.sm_statements.size());
        int64_t mapped = 0;
        int64_t total = 0;
        const auto row_count = lss.text_line_count();
        for (size_t vl = 0; vl < row_count; vl++) {
            uint64_t line_number;
            auto ld_iter = lss.find_data(lss.at(vis_line_t(vl)), line_number);
            auto* lf = (*ld_iter)->get_file_ptr();
            if (lf == nullptr) {
                continue;
            }
            auto ll = lf->begin() + line_number;
            if (ll->is_continued()) {
                continue;
            }

            total += 1;
            const auto& ids = lf->get_stmt_ids().si_ids;
            if (line_number >= ids.size()) {
                continue;
            }
            auto id = ids[line_number];
            if (id == UNMAPPED) {
                continue;
            }
            mapped += 1;
            if (id < FIRST_STATEMENT) {
                continue;
            }

            auto& st = totals[id];
            auto t = ll->get_time<std::chrono::milliseconds>().count();
            if (st.st_hits == 0) {
                st.st_first_time = st.st_last_time = t;
            } else {
                st.st_first_time = std::min(st.st_first_time, t);
                st.st_last_time = std::max(st.st_last_time, t);
            }
            st.st_hits += 1;
            auto bkey = t >> BASE_SHIFT;
            if (!st.st_runs.empty() && st.st_runs.back().first == bkey) {
                st.st_runs.back().second += 1;
            } else {
                st.st_runs.emplace_back(bkey, 1);
            }
        }

        this->lss_rows.clear();
        this->lss_mapped = mapped;
        this->lss_total = total;
        for (uint32_t id = FIRST_STATEMENT; id < totals.size(); id++) {
            auto& st = totals[id];
            if (st.st_hits == 0) {
                continue;
            }

            // The smallest power-of-two merge that fits the statement's span
            // in MAX_BUCKETS, as the IntelliJ plugin's Histogram does, so the
            // output doesn't grow with the span.
            std::sort(st.st_runs.begin(), st.st_runs.end());
            auto lo = st.st_runs.front().first;
            auto hi = st.st_runs.back().first;
            int shift = 0;
            while ((hi >> shift) - (lo >> shift) + 1 > MAX_BUCKETS) {
                shift += 1;
            }

            std::string buckets = "[";
            std::optional<std::pair<int64_t, int64_t>> curr;
            auto emit = [&buckets](const std::pair<int64_t, int64_t>& b) {
                if (buckets.size() > 1) {
                    buckets.push_back(',');
                }
                buckets.append(
                    fmt::format(FMT_STRING("[{},{}]"), b.first, b.second));
            };
            for (const auto& [bkey, n] : st.st_runs) {
                auto merged = (bkey >> shift) << shift;
                if (curr && curr->first == merged) {
                    curr->second += n;
                } else {
                    if (curr) {
                        emit(curr.value());
                    }
                    curr = std::make_pair(merged, n);
                }
            }
            if (curr) {
                emit(curr.value());
            }
            buckets.push_back(']');

            this->lss_rows.emplace_back(row{
                id,
                st.st_hits,
                st.st_first_time,
                st.st_last_time,
                std::move(buckets),
                shift,
            });
        }
    }

    int get_column(cursor& vc, sqlite3_context* ctx, int col)
    {
        const auto& r = *vc.iter;
        const auto& stmt = get_map().sm_statements[r.r_id];

        switch (col) {
            case 0:
                to_sqlite(ctx, stmt.s_src);
                sqlite3_result_subtype(ctx, JSON_SUBTYPE);
                break;
            case 1:
                to_sqlite(ctx, stmt.s_schema);
                break;
            case 2:
                to_sqlite(ctx, r.r_hits);
                break;
            case 3:
                to_sqlite(ctx, r.r_first_time);
                break;
            case 4:
                to_sqlite(ctx, r.r_last_time);
                break;
            case 5:
                to_sqlite(ctx, r.r_buckets);
                sqlite3_result_subtype(ctx, JSON_SUBTYPE);
                break;
            case 6:
                to_sqlite(ctx, (int64_t) r.r_bucket_shift);
                break;
            case 7:
                to_sqlite(ctx, this->lss_mapped);
                break;
            case 8:
                to_sqlite(ctx, this->lss_total);
                break;
        }

        return SQLITE_OK;
    }
};

auto log_stmt_stats_binder
    = injector::bind_multiple<vtab_module_base>()
          .add<vtab_module<tvt_no_update<log_stmt_stats_vtab>>>();

}  // namespace

DIST_SLICE(prog_reps)
lnav::progress_reporter_t LOG_STMT_MAP_PROG_REP = stmt_map_progress;

/**
 * The LOG view rows logged by the statement with the schema, for all_logs'
 * log_msg_schema constraints, or nullptr if the map can't say: it hasn't
 * been started, it's behind, or the schema isn't a statement's (messages
 * log2src didn't match can have a schema from the format's source fields).
 */
static const std::vector<vis_line_t>*
schema_lines(const std::string& schema, logfile_sub_source& lss)
{
    auto& sm = get_map();
    if (!sm.sm_active) {
        return nullptr;
    }

    // This is called for every xFilter, so only step when there is something
    // to catch up on, like lines that came in since the last slice.
#ifdef HAVE_RUST_DEPS
    auto ext = lnav_rs_ext::get_status();
    if (ext.status == lnav_rs_ext::Status::working) {
        return nullptr;
    }
    const auto statements_changed = sm.sm_scan_version != ext.version;
#else
    const auto statements_changed = false;
#endif
    if (statements_changed || stmt_map::is_behind(lss)) {
        lnav::stmt_stats::step(std::chrono::steady_clock::now()
                               + std::chrono::milliseconds(100));
        if (stmt_map::is_behind(lss)) {
            log_debug("schema_lines: the statement map is behind");
            return nullptr;
        }
    }

    auto id_iter = sm.sm_schema_ids.find(schema);
    if (id_iter == sm.sm_schema_ids.end()) {
        return nullptr;
    }

    auto& sr = sm.sm_rows;
    const auto row_count = lss.text_line_count();
    std::optional<size_t> trim_from;
    if (!sr.sr_valid || sr.sr_remaps != sm.sm_remaps
        || sr.sr_index_generation != lss.lss_index_generation)
    {
        trim_from = 0;
    } else if (sr.sr_index_change_seq != lss.lss_index_change_seq) {
        const auto& changes = lss.lss_index_changes;
        if (changes.empty()
            || changes.front().ic_seq > sr.sr_index_change_seq + 1)
        {
            trim_from = 0;
        } else {
            for (const auto& ic : changes) {
                if (ic.ic_seq > sr.sr_index_change_seq) {
                    trim_from = std::min(trim_from.value_or(ic.ic_from_row),
                                         ic.ic_from_row);
                }
            }
        }
    }
    if (row_count < sr.sr_row_count) {
        trim_from = std::min(trim_from.value_or(row_count), row_count);
    }
    if (trim_from) {
        const auto end = vis_line_t(trim_from.value());
        for (auto& rows : sr.sr_rows) {
            rows.erase(std::lower_bound(rows.begin(), rows.end(), end),
                       rows.end());
        }
        sr.sr_row_count = std::min(sr.sr_row_count, trim_from.value());
    }
    sr.sr_rows.resize(sm.sm_statements.size());

    for (auto vl = sr.sr_row_count; vl < row_count; vl++) {
        uint64_t line_number;
        auto ld_iter = lss.find_data(lss.at(vis_line_t(vl)), line_number);
        const auto* lf = (*ld_iter)->get_file_ptr();
        if (lf == nullptr) {
            continue;
        }
        const auto& ids = lf->get_stmt_ids().si_ids;
        if (line_number >= ids.size() || ids[line_number] < FIRST_STATEMENT) {
            continue;
        }
        sr.sr_rows[ids[line_number]].push_back(vis_line_t(vl));
    }
    sr.sr_valid = true;
    sr.sr_row_count = row_count;
    sr.sr_index_change_seq = lss.lss_index_change_seq;
    sr.sr_index_generation = lss.lss_index_generation;
    sr.sr_remaps = sm.sm_remaps;

    return &sr.sr_rows[id_iter->second];
}

/**
 * Whether the mapped statements are the current ones: no source scan running
 * or finished since the lines were last checked.  log2src's status is only
 * read every so often, since this is asked for every row.
 */
static bool
statements_current()
{
#ifdef HAVE_RUST_DEPS
    static std::chrono::steady_clock::time_point last_check;
    static bool last_result = false;

    auto now = std::chrono::steady_clock::now();
    if (now - last_check > std::chrono::milliseconds(50)) {
        auto& sm = get_map();
        auto ext = lnav_rs_ext::get_status();
        last_result = ext.status != lnav_rs_ext::Status::working
            && sm.sm_scan_version == ext.version;
        last_check = now;
    }
    return last_result;
#else
    return true;
#endif
}

/** The schema of the statement for the message at a line, if mapped. */
static const std::string*
line_schema(const logfile* lf, uint64_t line_number)
{
    const auto& sm = get_map();
    const auto& si = lf->get_stmt_ids();
    if (!sm.sm_active || line_number >= si.si_verified
        || si.si_ids[line_number] < FIRST_STATEMENT || !statements_current())
    {
        return nullptr;
    }

    return &sm.sm_statements[si.si_ids[line_number]].s_schema;
}

static const auto REGISTER_SCHEMA_LINES = []() {
    all_logs_vtab::alv_schema_lines = schema_lines;
    all_logs_vtab::alv_line_schema = line_schema;
    return true;
}();

namespace lnav::stmt_stats {

bool
step(std::chrono::steady_clock::time_point deadline)
{
    auto& sm = get_map();
    if (!sm.sm_active) {
        return false;
    }

    auto& lss = lnav_data.ld_log_source;

#ifdef HAVE_RUST_DEPS
    // While log2src is scanning the source, the statements are in flux, so
    // wait.  Once it's done, everything is matched again; until a line is, it
    // keeps the statement it had.
    auto ext = lnav_rs_ext::get_status();
    if (ext.status == lnav_rs_ext::Status::working) {
        return false;
    }
    if (sm.sm_scan_version != ext.version) {
        if (sm.sm_scan_version) {
            log_info("statements changed, re-mapping the log messages");
            for (const auto& ld : lss) {
                auto* lf = ld->get_file_ptr();
                if (lf != nullptr) {
                    lf->get_stmt_ids().si_verified = 0;
                }
            }
        }
        sm.sm_scan_version = ext.version;
    }
#endif

    stmt_map::sync_files(lss);

    // Each file with lines left goes to its own worker, which maps until the
    // deadline.  A file's line buffer and format are only used by its worker,
    // and this thread waits, as when the files are indexed.
    std::vector<logfile*> work;
    for (const auto& ld : lss) {
        auto* lf = ld->get_file_ptr();
        if (lf == nullptr) {
            continue;
        }
        const auto& si = lf->get_stmt_ids();
        if (si.si_verified < si.si_ids.size()) {
            work.emplace_back(lf);
        }
    }

    std::vector<char> unfinished(work.size(), 0);
    std::vector<char> changed(work.size(), 0);
    std::vector<char> remapped(work.size(), 0);
    if (!work.empty()) {
        lnav::parallel_for_each(
            work.size(),
            lnav::logfile_indexing_width(work.size()),
            [&](size_t index) {
                auto* lf = work[index];
                auto& si = lf->get_stmt_ids();
                stmt_map::match_scratch ms;
                auto start = si.si_verified;

                while (si.si_verified < si.si_ids.size()) {
                    // Check the clock every so often instead of for every
                    // line, starting after the first batch, so a slice maps
                    // some lines even if the deadline has already passed.
                    auto done = si.si_verified - start;
                    if (done > 0 && done % 256 == 0
                        && std::chrono::steady_clock::now() >= deadline)
                    {
                        unfinished[index] = 1;
                        break;
                    }
                    auto id = sm.match_line(lf, si.si_verified, ms);
                    auto old_id = si.si_ids[si.si_verified];
                    if (old_id != id) {
                        si.si_ids[si.si_verified] = id;
                        changed[index] = 1;
                        if (old_id != UNMAPPED) {
                            remapped[index] = 1;
                        }
                    }
                    si.si_verified += 1;
                }
            });
    }

    auto more = std::find(unfinished.begin(), unfinished.end(), 1)
        != unfinished.end();
    if (std::find(changed.begin(), changed.end(), 1) != changed.end()) {
        sm.sm_changes += 1;
    }
    if (std::find(remapped.begin(), remapped.end(), 1) != remapped.end()) {
        sm.sm_remaps += 1;
    }
    size_t completed = 0;
    size_t total = 0;
    for (const auto& ld : lss) {
        const auto* lf = ld->get_file_ptr();
        if (lf != nullptr) {
            completed += lf->get_stmt_ids().si_verified;
            total += lf->get_stmt_ids().si_ids.size();
        }
    }

    // Report progress in steps of about a percent, so pollers aren't woken
    // for every slice.
    auto old_completed = sm.sm_completed.load();
    auto old_total = sm.sm_total.load();
    auto was_working = sm.sm_working.load();
    sm.sm_completed = completed;
    sm.sm_total = total;
    sm.sm_working = more;
    if (was_working != more || (total == 0 && old_total != 0)
        || (total > 0
            && (completed - std::min(completed, old_completed)) * 100 >= total))
    {
        sm.sm_version += 1;
    }

    return more;
}

}  // namespace lnav::stmt_stats
