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
#include <unordered_set>
#include <deque>
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
#include "data_parser.hh"
#include "fmt/format.h"
#include "hasher.hh"
#include "lnav.hh"
#include "log_data_table.hh"
#include "log_format.hh"
#include "logfile.hh"
#include "logfile_sub_source.hh"
#include "vtab_module.hh"
#include "yajlpp/json_schema_builder.hh"
#include "yajlpp/yajlpp.hh"

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

/** How many of a statement's messages var_schema is derived from. */
constexpr uint32_t VAR_SAMPLES = 32;

/**
 * How many lines a worker maps between looks at the clock in a slice (see
 * step()): a line can take a millisecond, and the slice is 20ms.
 */
constexpr size_t STEP_CLOCK_LINES = 16;

/**
 * A log_msg_schema that messages were mapped to: a log statement log2src
 * found, or, for messages it found none for, the schema all_logs falls back
 * to.  Only the statements are reported in log_stmt_stats.
 */
struct statement {
    all_logs_vtab::schema_kind s_kind{all_logs_vtab::schema_kind::statement};
    /**
     * log2src's source details, as JSON, like all_logs.log_msg_src; empty
     * when the schema isn't a statement's.
     */
    std::string s_src;
    /** Like all_logs.log_msg_schema: hash(src, pattern) for a statement. */
    std::string s_schema;
    /**
     * For a message-kind schema, the data_parser schema of its pairs alone
     * (dp_pairs_schema_id), as a logline table has it; else empty.
     */
    std::string s_pairs_schema;
    /**
     * The argument values of sampled messages, from log2src's variables:
     * [{"expr": ..., "value": ...}, ...].  The values are always text.
     */
    json_schema_builder s_vars{{true, true}};

    /**
     * A JSON Schema (2020-12) for the statement's argument values, as a tuple
     * in call order, derived from the sampled messages; or nullopt before any
     * were sampled.  Each item's title is the source expression and its type
     * the narrowest that covers the samples.
     */
    std::optional<std::string> var_schema() const
    {
        auto samples = this->s_vars.sample_count();
        if (samples == 0) {
            return std::nullopt;
        }

        yajlpp_gen gen;
        yajl_gen_config(gen, yajl_gen_beautify, false);
        {
            yajlpp_map root(gen);
            root.gen("$schema");
            root.gen(json_schema_builder::SCHEMA_URI);
            root.gen("description");
            root.gen(fmt::format(FMT_STRING("Derived from {} sampled {}"),
                                 samples,
                                 samples == 1 ? "message" : "messages"));
            root.gen("type");
            root.gen("array");
            root.gen("prefixItems");
            {
                yajlpp_array items(gen);
                for (const auto& var : this->s_vars.root().n_prefix_items) {
                    const auto* expr = var.property("expr"_frag);
                    const auto* value = var.property("value"_frag);

                    yajlpp_map item(gen);
                    if (expr != nullptr && expr->n_example) {
                        item.gen("title");
                        item.gen(expr->n_example.value());
                    }
                    if (value != nullptr) {
                        this->s_vars.gen_keywords(gen, *value);
                    }
                }
            }
            root.gen("items");
            root.gen(false);
        }
        return gen.to_string_fragment().to_string();
    }
};

struct stmt_map {
    /** Set by the first query of the summary table. */
    bool sm_active{false};
    // Statements are only ever appended: none is removed, and a statement's
    // s_src and s_schema never change once it's interned, so they last as
    // long as lnav.  all_logs and log_stmt_stats hand them to SQLite without
    // a copy (SQLITE_STATIC, permanent_fragment).  A deque, so a statement
    // never moves either: statements can be interned while a cursor still
    // holds them (schema_lines() steps the map in xFilter).
    std::deque<statement> sm_statements = std::deque<statement>(2);
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
    /** How many slices step() has run, to rotate which file goes first. */
    size_t sm_slices{0};

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
     * The schema's id, from the worker's own [cache] when it has seen the
     * schema before, else from the shared table.  The [src] and
     * [pairs_schema] are only used the first time.
     */
    uint32_t intern_cached(
        std::unordered_map<string_fragment, uint32_t, frag_hasher, frag_equal>&
            cache,
        ArenaAlloc::Alloc<char>& arena,
        all_logs_vtab::schema_kind kind,
        const string_fragment& src,
        const string_fragment& schema,
        const string_fragment& pairs_schema)
    {
        auto iter = cache.find(schema);
        if (iter != cache.end()) {
            return iter->second;
        }

        uint32_t id;
        {
            std::lock_guard<std::mutex> lg(this->sm_intern_mutex);
            id = this->intern(kind, src, schema, pairs_schema);
        }
        cache.emplace(schema.to_owned(arena), id);
        return id;
    }

    /** Callers hold sm_intern_mutex while files are being mapped. */
    uint32_t intern(all_logs_vtab::schema_kind kind,
                    const string_fragment& src,
                    const string_fragment& schema,
                    const string_fragment& pairs_schema)
    {
        auto schema_str = schema.to_string();
        auto iter = this->sm_schema_ids.find(schema_str);
        if (iter != this->sm_schema_ids.end()) {
            return iter->second;
        }

        auto id = static_cast<uint32_t>(this->sm_statements.size());
        this->sm_statements.emplace_back(
            statement{kind,
                      src.to_string(),
                      schema_str,
                      pairs_schema.to_string()});
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
        /** The message, as all_logs gets it. */
        shared_buffer_ref ms_line;
        string_attrs_t ms_sa;
        /** What annotate() gave, on a copy of ms_line. */
        logline_value_vector ms_values;
        /** Statements this worker has seen with all of their samples. */
        std::unordered_set<uint32_t> ms_sampled;
    };

    /**
     * Folds the message's variables into its statement's samples, until it has
     * VAR_SAMPLES of them.  Extracting the variables is a second, slower match,
     * so it's only done for those few messages per statement.
     */
    void sample_vars(uint32_t id,
                     const shared_buffer_ref& sbr,
                     const string_attrs_t& sa,
                     const logline_value_vector& values,
                     log_level_t level,
                     match_scratch& ms)
    {
#ifdef HAVE_RUST_DEPS
        if (id < FIRST_STATEMENT || ms.ms_sampled.count(id) > 0) {
            return;
        }
        {
            std::lock_guard<std::mutex> lg(this->sm_intern_mutex);
            const auto& st = this->sm_statements[id];
            if (st.s_kind != all_logs_vtab::schema_kind::statement
                || st.s_vars.sample_count() >= VAR_SAMPLES)
            {
                ms.ms_sampled.insert(id);
                return;
            }
        }

        auto res = all_logs_vtab::find_statement(sbr, sa, values, level, true);
        if (res == nullptr) {
            return;
        }
        auto variables = (std::string) res->variables;

        std::lock_guard<std::mutex> lg(this->sm_intern_mutex);
        auto& st = this->sm_statements[id];
        if (st.s_vars.sample_count() < VAR_SAMPLES) {
            // A sample that is not valid JSON is just not counted.
            (void) st.s_vars.add_sample(string_fragment::from_str(variables));
        }
        if (st.s_vars.sample_count() >= VAR_SAMPLES) {
            ms.ms_sampled.insert(id);
        }
#endif
    }

    /**
     * The schema of the message starting at line_number of lf: its
     * statement, or else what all_logs falls back to, its source location
     * or its text.
     */
    uint32_t match_line(logfile* lf, size_t line_number, match_scratch& ms)
    {
        using kind = all_logs_vtab::schema_kind;

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
        auto& sbr = ms.ms_line;
        values.clear();
        sa.clear();
        lf->read_full_message(ll, sbr);
        sbr.erase_ansi();

        // As in all_logs::extract(): annotate() can swap in the format's own
        // copy of the message (a JSON message's rendering), but the attrs
        // are for the message as read, so that's what is matched and parsed.
        values.lvv_sbr = sbr.clone();
        format->annotate(lf, line_number, sa, values);

#ifdef HAVE_RUST_DEPS
        auto find_res = all_logs_vtab::find_statement(
            sbr, sa, values, ll->get_msg_level(), false);
        if (find_res != nullptr) {
            char schema_buf[hasher::STRING_SIZE];
            all_logs_vtab::statement_schema(
                string_fragment::from(find_res->src),
                string_fragment::from(find_res->pattern),
                schema_buf);
            auto id = this->intern_cached(ms.ms_cache,
                                          ms.ms_arena,
                                          kind::statement,
                                          string_fragment::from(find_res->src),
                                          string_fragment::from(schema_buf),
                                          ""_frag);
            this->sample_vars(id, sbr, sa, values, ll->get_msg_level(), ms);
            return id;
        }
#endif

        auto loc_schema = all_logs_vtab::src_location_schema(format, values);
        if (loc_schema) {
            return this->intern_cached(ms.ms_cache,
                                       ms.ms_arena,
                                       kind::src_location,
                                       ""_frag,
                                       string_fragment::from(*loc_schema),
                                       ""_frag);
        }

        // Parsed as all_logs does, with the message format, which goes into
        // its log_msg_schema.
        data_scanner ds(all_logs_vtab::schema_body(sbr, sa));
        data_parser dp(&ds);
        std::string msg_format;
        dp.dp_msg_format = &msg_format;
        dp.parse();
        auto msg_schema = dp.dp_schema_id.to_string();
        auto pairs_schema = dp.dp_pairs_schema_id.to_string();
        return this->intern_cached(ms.ms_cache,
                                   ms.ms_arena,
                                   kind::message,
                                   ""_frag,
                                   string_fragment::from(msg_schema),
                                   string_fragment::from(pairs_schema));
    }
};

stmt_map&
get_map()
{
    static stmt_map retval;

    return retval;
}

/**
 * Starts mapping the messages, for whatever asks first: a query of
 * log_stmt_stats or a logline table.
 */
void
start_map()
{
    auto& sm = get_map();
    if (!sm.sm_active) {
        sm.sm_active = true;
        log_info("starting the statement map");
    }
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
    var_schema TEXT,     -- A JSON Schema for the statement's argument values
                         -- (log_msg_values), a tuple in call order, derived
                         -- from up to 32 sampled messages: each item's title
                         -- is the source expression and its type the
                         -- narrowest that covers the samples.
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

        start_map();
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
            if (id < FIRST_STATEMENT
                || sm.sm_statements[id].s_kind
                    != all_logs_vtab::schema_kind::statement)
            {
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
            // A statement's strings are never freed or changed (see
            // sm_statements), so SQLite can take them without a copy.
            case 0:
                sqlite3_result_text(
                    ctx, stmt.s_src.data(), stmt.s_src.size(), SQLITE_STATIC);
                sqlite3_result_subtype(ctx, JSON_SUBTYPE);
                break;
            case 1:
                sqlite3_result_text(ctx,
                                    stmt.s_schema.data(),
                                    stmt.s_schema.size(),
                                    SQLITE_STATIC);
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
            case 7: {
                auto schema = stmt.var_schema();
                if (schema) {
                    to_sqlite(ctx, schema.value());
                    sqlite3_result_subtype(ctx, JSON_SUBTYPE);
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            }
            case 8:
                to_sqlite(ctx, this->lss_mapped);
                break;
            case 9:
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
 * The LOG view rows of the messages with the schemas, for all_logs'
 * log_msg_schema constraints.  A schema the map can't say for goes to
 * unknown_out: all of them when the map hasn't been started or is behind,
 * otherwise the ones no message was mapped to.
 */
static void
schema_lines(const std::vector<std::string>& schemas,
             logfile_sub_source& lss,
             std::vector<vis_line_t>& lines_out,
             std::vector<std::string>& unknown_out)
{
    auto& sm = get_map();
    const auto all_unknown = [&]() {
        unknown_out.insert(unknown_out.end(), schemas.begin(), schemas.end());
    };
    if (!sm.sm_active) {
        all_unknown();
        return;
    }

    // This is called for every xFilter, so only step when there is something
    // to catch up on, like lines that came in since the last slice.
#ifdef HAVE_RUST_DEPS
    auto ext = lnav_rs_ext::get_status();
    if (ext.status == lnav_rs_ext::Status::working) {
        all_unknown();
        return;
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
            all_unknown();
            return;
        }
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

    for (const auto& schema : schemas) {
        auto id_iter = sm.sm_schema_ids.find(schema);
        if (id_iter == sm.sm_schema_ids.end()) {
            unknown_out.emplace_back(schema);
            continue;
        }
        const auto& rows = sr.sr_rows[id_iter->second];
        lines_out.insert(lines_out.end(), rows.begin(), rows.end());
    }
}

/**
 * Whether the mapped statements are the current ones: no source scan running
 * or finished since the lines were last checked.  Checked once per all_logs
 * scan (check_statements(), as the scan starts), not per row, since this is
 * asked for every row: a scan that starts while a query runs is noticed by
 * the next query.
 */
static bool s_statements_current = false;

static void
check_statements()
{
#ifdef HAVE_RUST_DEPS
    auto& sm = get_map();
    auto ext = lnav_rs_ext::get_status();
    s_statements_current = ext.status != lnav_rs_ext::Status::working
        && sm.sm_scan_version == ext.version;
#else
    s_statements_current = true;
#endif
}

static bool
statements_current()
{
    return s_statements_current;
}

/** What the statement map knows about the message at a line. */
static all_logs_vtab::line_stmt
line_stmt(const logfile* lf, uint64_t line_number)
{
    const auto& sm = get_map();
    const auto& si = lf->get_stmt_ids();
    if (!sm.sm_active || line_number >= si.si_verified
        || si.si_ids[line_number] == UNMAPPED || !statements_current())
    {
        return all_logs_vtab::line_stmt_unmapped{};
    }

    const auto id = si.si_ids[line_number];
    if (id < FIRST_STATEMENT) {
        return all_logs_vtab::line_stmt_unmatched{};
    }

    const auto& st = sm.sm_statements[id];
    return all_logs_vtab::line_stmt_mapped{
        st.s_kind == all_logs_vtab::schema_kind::statement ? &st.s_src
                                                           : nullptr,
        &st.s_schema,
        st.s_kind,
    };
}

/**
 * Checks a message against a logline table's data_parser schema.  Only a
 * message-kind schema is the text's, and that doesn't depend on the source,
 * so it holds even when the statements have changed since the line was
 * mapped.
 */
static log_data_table::schema_check
check_schema(const logfile* lf, uint64_t line_number, const std::string& schema)
{
    const auto& sm = get_map();
    const auto& ids = lf->get_stmt_ids().si_ids;
    if (!sm.sm_active || line_number >= ids.size()
        || ids[line_number] < FIRST_STATEMENT)
    {
        return log_data_table::schema_check::unknown;
    }

    const auto& st = sm.sm_statements[ids[line_number]];
    if (st.s_kind != all_logs_vtab::schema_kind::message) {
        return log_data_table::schema_check::unknown;
    }
    return st.s_pairs_schema == schema
        ? log_data_table::schema_check::same
        : log_data_table::schema_check::different;
}

static const auto REGISTER_SCHEMA_LINES = []() {
    all_logs_vtab::alv_schema_lines = schema_lines;
    all_logs_vtab::alv_line_stmt = line_stmt;
    all_logs_vtab::alv_scan_started = check_statements;
    log_data_table::ldt_check_schema = check_schema;
    log_data_table::ldt_start_map = start_map;
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

    // A different file goes first each slice, so the ones at the end of the
    // list aren't always the ones a slice runs out of time for.
    if (work.size() > 1) {
        std::rotate(work.begin(),
                    work.begin() + (sm.sm_slices % work.size()),
                    work.end());
    }
    sm.sm_slices += 1;

    std::vector<char> unfinished(work.size(), 0);
    std::vector<char> changed(work.size(), 0);
    std::vector<char> remapped(work.size(), 0);
    // Whether this slice has mapped anything yet: past the deadline, a worker
    // only starts another file while nothing has been.
    std::atomic<bool> progressed{false};
    if (!work.empty()) {
        lnav::parallel_for_each(
            work.size(),
            lnav::logfile_indexing_width(work.size()),
            [&](size_t index) {
                auto* lf = work[index];
                auto& si = lf->get_stmt_ids();
                stmt_map::match_scratch ms;
                auto start = si.si_verified;

                // Out of time already (the workers went through the other
                // files): leave this one for the next slice.  Each file used
                // to map a first batch whatever the time, so with 77 files a
                // 20ms slice held the main loop for 300ms.
                if (progressed.load(std::memory_order_relaxed)
                    && std::chrono::steady_clock::now() >= deadline)
                {
                    unfinished[index] = 1;
                    return;
                }
                while (si.si_verified < si.si_ids.size()) {
                    // Check the clock every few lines: a line can take a
                    // millisecond (a JSON message parsed, then matched by
                    // log2src), so every 256 overran the slice by ~250ms.
                    auto done = si.si_verified - start;
                    if (done > 0 && done % STEP_CLOCK_LINES == 0
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
                    progressed.store(true, std::memory_order_relaxed);
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
