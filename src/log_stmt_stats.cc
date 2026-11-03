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
#include <charconv>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "log_stmt_stats.hh"

#include "all_logs_vtab.hh"
#include "ArenaAlloc/arenaalloc.h"
#include "base/guard_util.hh"
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
#include "logfile_sub_source.cfg.hh"
#include "logfile_sub_source.hh"
#include "sql_help.hh"
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

using steady_ns = std::chrono::nanoseconds;

uint64_t
ns_since(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration_cast<steady_ns>(
               std::chrono::steady_clock::now() - start)
        .count();
}

/** Adds the time from its construction to its destruction to [dst]. */
struct scoped_ns {
    explicit scoped_ns(uint64_t& dst) : sn_dst(dst) {}
    scoped_ns(const scoped_ns&) = delete;
    scoped_ns& operator=(const scoped_ns&) = delete;
    ~scoped_ns() { this->sn_dst += ns_since(this->sn_start); }

    uint64_t& sn_dst;
    std::chrono::steady_clock::time_point sn_start{
        std::chrono::steady_clock::now()};
};

using map_counters = logfile::stmt_ids::counters;

/**
 * The step() loop's view of a pass, kept on the main thread: from the first
 * slice with work to the one that finishes it.
 */
struct pass_counters {
    std::optional<std::chrono::steady_clock::time_point> pc_start;
    uint64_t pc_slices{0};
    /** Time in step()'s parallel_for_each(), the main loop being held. */
    uint64_t pc_slice_ns{0};
    uint64_t pc_max_slice_ns{0};
    /** How far past their deadline the slices ran, at most and in all. */
    uint64_t pc_max_overrun_ns{0};
    uint64_t pc_overrun_ns{0};
    /** Slices that ran out of time with files left. */
    uint64_t pc_short_slices{0};
    /** Files given to the workers, and the ones left for a later slice. */
    uint64_t pc_files{0};
    uint64_t pc_files_deferred{0};
    uint64_t pc_max_width{0};
};

double
to_ms(uint64_t ns)
{
    return static_cast<double>(ns) / 1e6;
}

double
pct(uint64_t part, uint64_t total)
{
    return total == 0 ? 0.0 : static_cast<double>(part) * 100.0 / total;
}

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

/**
 * A location in a stack trace: a path as the trace has it and a line, and the
 * function the trace says it's in, if the pattern captures one (invalid when
 * not).  In sm_frames, the path and function are in stmt_map's
 * sm_frame_arena.
 */
struct stack_frame {
    string_key f_path;
    int64_t f_line{0};
    string_key f_func;

    bool operator==(const stack_frame& rhs) const
    {
        if (this->f_line != rhs.f_line || this->f_path != rhs.f_path
            || this->f_func.is_valid() != rhs.f_func.is_valid())
        {
            return false;
        }
        return !this->f_func.is_valid() || this->f_func == rhs.f_func;
    }
};

struct stack_frame_hasher {
    size_t operator()(const stack_frame& sf) const
    {
        size_t h1 = sf.f_path.hash();
        auto h2 = std::hash<int64_t>{}(sf.f_line);
        if (sf.f_func.is_valid()) {
            h2 ^= sf.f_func.hash() * 0x9e3779b97f4a7c15ULL;
        }
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};

using frame_id = logfile::stmt_ids::frame_id;

/** Frame ids by frame. */
using stack_frame_ids
    = std::unordered_map<stack_frame, frame_id, stack_frame_hasher>;

/**
 * A source location's id, its log_stack_frames rowid.  Locations are only
 * ever appended, so an id stays valid while lnav runs.
 */
struct location_id
    : type_safe::strong_typedef<location_id, uint32_t>
    , type_safe::strong_typedef_op::equality_comparison<location_id>
    , type_safe::strong_typedef_op::relational_comparison<location_id> {
    using strong_typedef::strong_typedef;
};

/**
 * Where frames are in the source: a line of a file under the source roots,
 * which frames with different paths in their traces can be at, or, for a
 * frame whose path isn't a file there, the path as the trace has it.
 */
struct frame_location {
    /** The file under the source roots, or nullopt for a frame's own. */
    std::optional<std::string> loc_src_path;
    /**
     * The path in the trace of the first frame found at the location.  In
     * sm_frame_arena, like the frames' paths.
     */
    string_key loc_path;
    int64_t loc_line{0};
};

/** What frame_location ids are keyed by: its file under the roots, or not. */
struct location_key {
    bool lk_in_src{false};
    std::string lk_path;
    int64_t lk_line{0};

    bool operator==(const location_key& rhs) const
    {
        return this->lk_in_src == rhs.lk_in_src && this->lk_line == rhs.lk_line
            && this->lk_path == rhs.lk_path;
    }
};

struct location_key_hasher {
    size_t operator()(const location_key& lk) const
    {
        size_t h1 = std::hash<std::string>{}(lk.lk_path);
        auto h2 = std::hash<int64_t>{}(lk.lk_line * 2 + lk.lk_in_src);
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};

/**
 * The frames a log_stack_frame_rows() call asks for: the ones that match all
 * of the given parts.
 */
struct frame_query {
    /** The file under the source roots that the frame's path is. */
    std::optional<std::string> fq_src_path;
    /** The frame's line, as the trace has it. */
    std::optional<int64_t> fq_line;
    /** The frame's location, as its log_stack_frames rowid. */
    std::optional<int64_t> fq_frame;

    bool operator==(const frame_query& rhs) const
    {
        return this->fq_src_path == rhs.fq_src_path
            && this->fq_line == rhs.fq_line && this->fq_frame == rhs.fq_frame;
    }

    bool operator!=(const frame_query& rhs) const { return !(*this == rhs); }
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
    std::atomic<std::chrono::steady_clock::time_point> sm_status_change_time;
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

    /** The workers' counters for this pass, see map_counters. */
    std::mutex sm_counters_mutex;
    map_counters sm_counters;
    /** Only used by step(), on the main thread. */
    pass_counters sm_pass;

    /**
     * The stack frames found in multi-line messages, by id.  Like the
     * statements, they are only ever appended, so an id stays valid while
     * the files' frame ids are thrown away and found again.
     */
    std::deque<stack_frame> sm_frames;
    /** The ids of sm_frames, keyed by copies of them. */
    stack_frame_ids sm_frame_ids;
    /**
     * Holds the frames' paths.  Never reset: frames are only appended, and
     * SQLite is handed their paths without a copy.
     */
    ArenaAlloc::Alloc<char> sm_frame_arena{16 * 1024};
    /** Bumped when any file's frames change. */
    size_t sm_frame_changes{0};
    /** Bumped when every file's frames are found again. */
    size_t sm_frame_resets{0};

    /**
     * The LOG view rows, ascending, of the messages with a frame that matches
     * the last log_stack_frame_rows() query; kept like sm_rows while the
     * query stays the same.  Rows before fr_row_count are in it; the scan
     * stops at the first row whose file's frames haven't been found yet, and
     * picks up there on the next call.
     */
    struct frame_rows {
        frame_query fr_query;
        /**
         * Whether each of sm_frames matches fr_query, by id.  Frames found
         * since are checked on the next call; the rows already read can't
         * have them, since a row's frames are found before it's read.
         */
        std::vector<bool> fr_frames;
        std::vector<vis_line_t> fr_rows;
        bool fr_valid{false};
        uint64_t fr_index_change_seq{0};
        uint32_t fr_index_generation{0};
        size_t fr_resets{0};
        /** The sm_frame_locations_generation fr_frames was checked with. */
        size_t fr_locations_generation{0};
        size_t fr_row_count{0};
    } sm_frame_rows;
    /**
     * The enabled /log/stack-frames patterns the frames were found with; when
     * they change, every message's frames are found again.
     */
    std::optional<std::string> sm_frame_patterns;
    /** The stack_frames_generation() that sm_frame_patterns is from. */
    std::optional<size_t> sm_frame_generation;
    /**
     * The source languages log2src found statements in, as of the source
     * scan the statements are from.
     */
    lnav::enums::bitset<logfile_sub_source_ns::source_language_t> sm_languages;
    /**
     * The file under the source roots that each frame path is, for
     * log_stack_frames.src_path, empty when there's none.  Looked up once per
     * path, since log2src walks the source tree for each, and again after a
     * source scan, which can add or remove files.
     */
    std::unordered_map<string_key, std::string, frag_hasher, frag_equal>
        sm_frame_src;
    std::optional<size_t> sm_frame_src_version;
    /**
     * Changes when sm_frame_src is cleared, so log_stack_frames knows its
     * rows' src_paths are out of date.
     */
    size_t sm_frame_src_generation{0};

    /**
     * The frames' source locations, by location id.  Only ever appended: a
     * location keeps its id while lnav runs, even once no frame is at it.
     */
    std::deque<frame_location> sm_locations;
    /** The ids of sm_locations. */
    std::unordered_map<location_key, location_id, location_key_hasher>
        sm_location_ids;
    /** Each of sm_frames' location, by frame id. */
    std::vector<location_id> sm_frame_locations;
    /** The sm_frame_src_generation that sm_frame_locations is from. */
    size_t sm_frame_locations_src_generation{0};
    /**
     * Set when a frame's file couldn't be looked up during a source scan, so
     * it's at its own location for now and they're all found again next time.
     */
    bool sm_frame_locations_pending{false};
    /**
     * Bumped when frames already in sm_frame_locations may have moved to
     * another location, unlike frames found since getting theirs.
     */
    size_t sm_frame_locations_generation{0};

    /**
     * The function names in the frames, for log_stack_frames.functions: name
     * id - 1 is the index in sm_func_names, and 0 is no name.  Only appended,
     * like the frames, whose arena holds the names.
     */
    std::deque<string_key> sm_func_names;
    std::unordered_map<string_key, uint32_t, frag_hasher, frag_equal>
        sm_func_ids;
    /** Each of sm_frames' function name id, by frame id. */
    std::vector<uint32_t> sm_frame_funcs;

    /** Gives the frames found since their function name ids. */
    void sync_frame_funcs()
    {
        for (auto id = this->sm_frame_funcs.size(); id < this->sm_frames.size();
             id++)
        {
            const auto& func = this->sm_frames[id].f_func;
            if (!func.is_valid() || func.to_frag().empty()) {
                this->sm_frame_funcs.push_back(0);
                continue;
            }
            auto iter = this->sm_func_ids.find(func);
            if (iter == this->sm_func_ids.end()) {
                this->sm_func_names.push_back(func);
                iter = this->sm_func_ids
                           .emplace(func,
                                    static_cast<uint32_t>(
                                        this->sm_func_names.size()))
                           .first;
            }
            this->sm_frame_funcs.push_back(iter->second);
        }
    }

    /** The location of frame [id], added to sm_locations if it's new. */
    location_id locate_frame(size_t id, bool& pending)
    {
        const auto& frame = this->sm_frames[id];
        auto src = this->frame_src(frame.f_path, pending);
        location_key key;
        if (src) {
            key.lk_in_src = true;
            key.lk_path = src.value();
        } else {
            key.lk_path = frame.f_path.to_frag().to_string();
        }
        key.lk_line = frame.f_line;

        auto iter = this->sm_location_ids.find(key);
        if (iter != this->sm_location_ids.end()) {
            return iter->second;
        }
        auto retval
            = location_id(static_cast<uint32_t>(this->sm_locations.size()));
        this->sm_locations.emplace_back(frame_location{
            std::move(src),
            frame.f_path,
            frame.f_line,
        });
        this->sm_location_ids.emplace(std::move(key), retval);
        return retval;
    }

    /**
     * Brings sm_frame_locations up to date: the frames found since get
     * theirs, and all of them do again after a source scan, which can change
     * the files under the roots, or when some were left pending.
     */
    void sync_frame_locations()
    {
        this->sync_frame_src();
        if (this->sm_frame_locations_src_generation
                != this->sm_frame_src_generation
            || this->sm_frame_locations_pending)
        {
            this->sm_frame_locations.clear();
            this->sm_frame_locations_src_generation
                = this->sm_frame_src_generation;
            this->sm_frame_locations_generation += 1;
        }
        auto pending = false;
        for (auto id = this->sm_frame_locations.size();
             id < this->sm_frames.size();
             id++)
        {
            this->sm_frame_locations.push_back(this->locate_frame(id, pending));
        }
        this->sm_frame_locations_pending = pending;
    }

    /**
     * Forgets the frames' files under the source roots after a source scan
     * finishes, since it may have added or removed files.
     */
    void sync_frame_src()
    {
#ifdef HAVE_RUST_DEPS
        auto ext = lnav_rs_ext::get_status();
        if (ext.status != lnav_rs_ext::Status::working
            && this->sm_frame_src_version != ext.version)
        {
            this->sm_frame_src.clear();
            this->sm_frame_src_version = ext.version;
            this->sm_frame_src_generation += 1;
        }
#endif
    }

    /**
     * [path]'s file under the source roots, or nullopt when there's none.
     * Without an answer cached, nothing is looked up while log2src scans the
     * source, since it holds the tree's lock; [pending] is set instead.
     */
    std::optional<std::string> frame_src(const string_key& path, bool& pending)
    {
#ifdef HAVE_RUST_DEPS
        auto iter = this->sm_frame_src.find(path);
        if (iter == this->sm_frame_src.end()) {
            if (lnav_rs_ext::get_status().status
                == lnav_rs_ext::Status::working)
            {
                pending = true;
                return std::nullopt;
            }
            auto path_sf = path.to_frag();
            iter = this->sm_frame_src
                       .emplace(
                           path,
                           static_cast<std::string>(lnav_rs_ext::find_src_file(
                               rust::Str(path_sf.data(), path_sf.length()))))
                       .first;
        }
        if (iter->second.empty()) {
            return std::nullopt;
        }
        return iter->second;
#else
        return std::nullopt;
#endif
    }

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
        const string_fragment& pairs_schema,
        uint64_t* shared_count = nullptr)
    {
        auto iter = cache.find(schema);
        if (iter != cache.end()) {
            return iter->second;
        }

        if (shared_count != nullptr) {
            *shared_count += 1;
        }
        uint32_t id;
        {
            std::lock_guard<std::mutex> lg(this->sm_intern_mutex);
            id = this->intern(kind, src, schema, pairs_schema);
        }
        cache.emplace(schema.to_owned(arena), id);
        return id;
    }

    /**
     * The id of [frame], whose path may be in a scratch buffer: from the
     * worker's own [cache] when it has seen the frame before, else from the
     * shared table.  The cache's keys are copies of the shared frames, so
     * their paths are in sm_frame_arena too.
     */
    frame_id intern_frame_cached(stack_frame_ids& cache,
                                 const stack_frame& frame)
    {
        auto iter = cache.find(frame);
        if (iter != cache.end()) {
            return iter->second;
        }

        frame_id id;
        stack_frame owned;
        {
            std::lock_guard<std::mutex> lg(this->sm_intern_mutex);
            auto shared_iter = this->sm_frame_ids.find(frame);
            if (shared_iter != this->sm_frame_ids.end()) {
                id = shared_iter->second;
                owned = shared_iter->first;
            } else {
                id = frame_id(static_cast<uint32_t>(this->sm_frames.size()));
                owned = stack_frame{
                    frame.f_path.to_owned(this->sm_frame_arena),
                    frame.f_line,
                    frame.f_func.to_owned(this->sm_frame_arena),
                };
                this->sm_frames.emplace_back(owned);
                this->sm_frame_ids.emplace(owned, id);
            }
        }
        cache.emplace(owned, id);
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
        this->sm_statements.emplace_back(statement{
            kind, src.to_string(), schema_str, pairs_schema.to_string()});
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
                si.truncate(lf->size());
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
            if (si.si_ids.size() != lf->size() || si.si_verified < lf->size()
                || si.si_frames_verified < lf->size())
            {
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
        /** Frame ids, like ms_cache. */
        stack_frame_ids ms_frame_cache;
        /** The path of the frame being looked up. */
        std::string ms_frame_path;
        /** The frames found in the current message. */
        std::vector<frame_id> ms_frames;
        map_counters ms_counters;
    };

    /**
     * Puts the ids of the stack frames in the message read into [ms] in
     * ms.ms_frames, each once, if it's a multi-line message.  A JSON
     * message's frames are in its values (a trace in one string, with
     * newlines), which are only text once decoded; other messages are looked
     * at line by line.
     */
    void find_frames(logfile* lf,
                     logfile::iterator ll,
                     const log_format* format,
                     match_scratch& ms)
    {
        auto next = std::next(ll);
        if (next == lf->end() || !next->is_continued()) {
            return;
        }

        auto mlr = lf->message_byte_length(ll);
        // A message cut to the buffer size may end in a partial character.
        uint32_t options = mlr.mlr_metadata.m_valid_utf
                && mlr.mlr_length <= line_buffer::MAX_LINE_BUFFER_SIZE
            ? PCRE2_NO_UTF_CHECK
            : 0;
        auto on_frame
            = [this, &ms](const logfile_sub_source_ns::stack_frame_match& sfm) {
                  int64_t line = 0;
                  auto [ptr, ec] = std::from_chars(
                      sfm.sfm_line.begin(), sfm.sfm_line.end(), line);
                  if (ec != std::errc{} || ptr != sfm.sfm_line.end()) {
                      return;
                  }
                  auto& path = ms.ms_frame_path;
                  path.clear();
                  sfm.append_path(path);
                  ms.ms_frames.emplace_back(this->intern_frame_cached(
                      ms.ms_frame_cache,
                      stack_frame{
                          string_key::from(string_fragment::from_str(path)),
                          line,
                          sfm.sfm_func ? string_key::from(sfm.sfm_func.value())
                                       : string_key::invalid(),
                      }));
              };
        auto scan_lines = [this, &options, &on_frame](string_fragment text) {
            while (!text.empty()) {
                auto nl = static_cast<const char*>(
                    memchr(text.data(), '\n', text.length()));
                auto len = nl == nullptr
                    ? text.length()
                    : static_cast<size_t>(nl - text.data());
                logfile_sub_source_ns::for_each_stack_frame(
                    this->sm_languages,
                    string_fragment::from_bytes(text.data(), len),
                    options,
                    on_frame);
                if (nl == nullptr) {
                    break;
                }
                text = string_fragment::from_bytes(nl + 1,
                                                   text.length() - len - 1);
            }
        };

        if (format->lf_file_type == log_format::file_type_t::JSON) {
            for (const auto& lv : ms.ms_values.lvv_values) {
                if (lv.lv_meta.lvm_kind == value_kind_t::VALUE_TEXT) {
                    scan_lines(lv.text_value_fragment());
                }
            }
        } else {
            scan_lines(ms.ms_line.to_string_fragment());
        }

        std::sort(ms.ms_frames.begin(), ms.ms_frames.end());
        ms.ms_frames.erase(
            std::unique(ms.ms_frames.begin(), ms.ms_frames.end()),
            ms.ms_frames.end());
    }

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
        scoped_ns timer(ms.ms_counters.mc_sample_ns);
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

        ms.ms_counters.mc_samples += 1;
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
     * or its text.  With [want_frames], the message's stack frames go in
     * ms.ms_frames; without [want_stmt], only those are looked for.
     */
    uint32_t match_line(logfile* lf,
                        size_t line_number,
                        match_scratch& ms,
                        bool want_stmt,
                        bool want_frames)
    {
        using kind = all_logs_vtab::schema_kind;

        auto& mc = ms.ms_counters;
        mc.mc_lines += 1;
        ms.ms_frames.clear();
        auto ll = lf->begin() + line_number;
        if (ll->is_continued()) {
            mc.mc_continued += 1;
            return NO_STATEMENT;
        }

        auto* format = lf->get_format_ptr();
        if (format == nullptr) {
            mc.mc_no_format += 1;
            return NO_STATEMENT;
        }

        if (!want_stmt) {
            mc.mc_frames_only += 1;
            auto next = std::next(ll);
            if (next == lf->end() || !next->is_continued()) {
                return NO_STATEMENT;
            }
        }

        auto& values = ms.ms_values;
        auto& sa = ms.ms_sa;
        auto& sbr = ms.ms_line;
        values.clear();
        sa.clear();
        {
            scoped_ns timer(mc.mc_read_ns);
            lf->read_full_message(ll, sbr);
            sbr.erase_ansi();
        }
        mc.mc_read_bytes += sbr.length();
        mc.mc_max_message_bytes
            = std::max<uint64_t>(mc.mc_max_message_bytes, sbr.length());

        // As in all_logs::extract(): annotate() can swap in the format's own
        // copy of the message (a JSON message's rendering), but the attrs
        // are for the message as read, so that's what is matched and parsed.
        values.lvv_sbr = sbr.clone();
        // Only a JSON message's frames need its values; a text message's
        // are found in the message as read.
        if (want_stmt || format->lf_file_type == log_format::file_type_t::JSON)
        {
            scoped_ns timer(mc.mc_annotate_ns);
            format->annotate(lf, line_number, sa, values);
        }

        if (want_frames) {
            scoped_ns timer(mc.mc_frames_ns);
            this->find_frames(lf, ll, format, ms);
        }
        if (!want_stmt) {
            return NO_STATEMENT;
        }
        // A message without a body, like a line from a pattern without one,
        // is all fields, so there's nothing to match.
        const auto body = all_logs_vtab::schema_body(sbr, sa);
        if (body.empty()) {
            mc.mc_no_body += 1;
            return NO_STATEMENT;
        }

#ifdef HAVE_RUST_DEPS
        const auto find_start = std::chrono::steady_clock::now();
        auto find_res = all_logs_vtab::find_statement(
            sbr, sa, values, ll->get_msg_level(), false);
        const auto find_ns = ns_since(find_start);
        mc.mc_find_calls += 1;
        mc.mc_find_ns += find_ns;
        mc.mc_max_find_ns = std::max(mc.mc_max_find_ns, find_ns);
        if (find_res != nullptr) {
            mc.mc_statements += 1;
            uint32_t id;
            {
                scoped_ns timer(mc.mc_intern_ns);
                char schema_buf[hasher::STRING_SIZE];
                all_logs_vtab::statement_schema(
                    string_fragment::from(find_res->src),
                    string_fragment::from(find_res->pattern),
                    schema_buf);
                id = this->intern_cached(ms.ms_cache,
                                         ms.ms_arena,
                                         kind::statement,
                                         string_fragment::from(find_res->src),
                                         string_fragment::from(schema_buf),
                                         ""_frag,
                                         &mc.mc_shared_interns);
            }
            this->sample_vars(id, sbr, sa, values, ll->get_msg_level(), ms);
            return id;
        }
#endif

        auto loc_schema = all_logs_vtab::src_location_schema(format, values);
        if (loc_schema) {
            mc.mc_src_locations += 1;
            return this->intern_cached(ms.ms_cache,
                                       ms.ms_arena,
                                       kind::src_location,
                                       ""_frag,
                                       string_fragment::from(*loc_schema),
                                       ""_frag,
                                       &mc.mc_shared_interns);
        }

        // Parsed as all_logs does, with the message format, which goes into
        // its log_msg_schema.
        mc.mc_messages += 1;
        scoped_ns parse_timer(mc.mc_parse_ns);
        data_scanner ds(body);
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
                                   string_fragment::from(pairs_schema),
                                   &mc.mc_shared_interns);
    }

    /**
     * Logs where the pass that just finished spent its time, and starts the
     * counts over for the next one.
     */
    void log_pass_stats(logfile_sub_source& lss)
    {
        map_counters mc;
        {
            std::lock_guard<std::mutex> lg(this->sm_counters_mutex);
            mc = std::exchange(this->sm_counters, map_counters{});
        }
        auto pc = std::exchange(this->sm_pass, pass_counters{});
        const auto wall_ns = pc.pc_start ? ns_since(*pc.pc_start) : 0;
        const auto in_lines = mc.mc_worker_ns;
        const auto matched_lines
            = mc.mc_lines - mc.mc_continued - mc.mc_no_format;

        log_info(
            "stmt map pass: %.1fms wall, %llu slices holding the main loop "
            "%.1fms (%.1f%%, max %.1fms), %.1fms in workers (%.1fx parallel, "
            "max width %llu)",
            to_ms(wall_ns),
            (unsigned long long) pc.pc_slices,
            to_ms(pc.pc_slice_ns),
            pct(pc.pc_slice_ns, wall_ns),
            to_ms(pc.pc_max_slice_ns),
            to_ms(mc.mc_worker_ns),
            pc.pc_slice_ns == 0
                ? 0.0
                : static_cast<double>(mc.mc_worker_ns) / pc.pc_slice_ns,
            (unsigned long long) pc.pc_max_width);
        log_info(
            "stmt map pass:   slices: %llu ran out of time, overran deadline "
            "by %.1fms in all (max %.1fms); %llu file runs, %llu deferred",
            (unsigned long long) pc.pc_short_slices,
            to_ms(pc.pc_overrun_ns),
            to_ms(pc.pc_max_overrun_ns),
            (unsigned long long) pc.pc_files,
            (unsigned long long) pc.pc_files_deferred);
        log_info(
            "stmt map pass:   lines: %llu looked at, %llu continued, %llu "
            "without a format, %llu for frames only, %llu without a body",
            (unsigned long long) mc.mc_lines,
            (unsigned long long) mc.mc_continued,
            (unsigned long long) mc.mc_no_format,
            (unsigned long long) mc.mc_frames_only,
            (unsigned long long) mc.mc_no_body);
        log_info(
            "stmt map pass:   mapped to: %llu statements, %llu source "
            "locations, %llu message formats",
            (unsigned long long) mc.mc_statements,
            (unsigned long long) mc.mc_src_locations,
            (unsigned long long) mc.mc_messages);
        log_info(
            "stmt map pass:   read %.1fms (%.1f%%) for %.1fMB (avg %.0fB, max "
            "%lluB), annotate %.1fms (%.1f%%), frames %.1fms (%.1f%%)",
            to_ms(mc.mc_read_ns),
            pct(mc.mc_read_ns, in_lines),
            static_cast<double>(mc.mc_read_bytes) / (1024.0 * 1024.0),
            matched_lines == 0 ? 0.0
                               : static_cast<double>(mc.mc_read_bytes)
                    / static_cast<double>(matched_lines),
            (unsigned long long) mc.mc_max_message_bytes,
            to_ms(mc.mc_annotate_ns),
            pct(mc.mc_annotate_ns, in_lines),
            to_ms(mc.mc_frames_ns),
            pct(mc.mc_frames_ns, in_lines));
        log_info(
            "stmt map pass:   log2src %llu calls %.1fms (%.1f%%, avg %.1fus, "
            "max %.1fms), intern %.1fms (%.1f%%, %llu shared), samples %llu "
            "%.1fms (%.1f%%), message formats %.1fms (%.1f%%)",
            (unsigned long long) mc.mc_find_calls,
            to_ms(mc.mc_find_ns),
            pct(mc.mc_find_ns, in_lines),
            mc.mc_find_calls == 0
                ? 0.0
                : static_cast<double>(mc.mc_find_ns) / 1e3 / mc.mc_find_calls,
            to_ms(mc.mc_max_find_ns),
            to_ms(mc.mc_intern_ns),
            pct(mc.mc_intern_ns, in_lines),
            (unsigned long long) mc.mc_shared_interns,
            (unsigned long long) mc.mc_samples,
            to_ms(mc.mc_sample_ns),
            pct(mc.mc_sample_ns, in_lines),
            to_ms(mc.mc_parse_ns),
            pct(mc.mc_parse_ns, in_lines));

        // The slowest files first, since that's what's being looked for.
        // The files closed during the pass took their counts with them.
        std::vector<std::pair<const logfile*, map_counters>> files;
        for (const auto& ld : lss) {
            auto* lf = ld->get_file_ptr();
            if (lf == nullptr) {
                continue;
            }
            auto fc = std::exchange(lf->get_stmt_ids().si_counters, {});
            if (fc.mc_lines > 0) {
                files.emplace_back(lf, fc);
            }
        }
        std::sort(
            files.begin(), files.end(), [](const auto& lhs, const auto& rhs) {
                return lhs.second.mc_worker_ns > rhs.second.mc_worker_ns;
            });
        for (const auto& [lf, fc] : files) {
            // The messages there was something to match: the ones without
            // a body or a format never get a statement.
            const auto with_body
                = fc.mc_statements + fc.mc_src_locations + fc.mc_messages;
            const auto without_stmt = fc.mc_src_locations + fc.mc_messages;
            log_info(
                "stmt map file: %.1fms (%.1f%% of workers), log2src %.1fms "
                "(avg %.1fus); %llu lines, %.1fMB; %llu messages with a body, "
                "%llu without a statement (%.1f%%: %llu source locations, "
                "%llu message formats), %llu without a body; %s",
                to_ms(fc.mc_worker_ns),
                pct(fc.mc_worker_ns, mc.mc_worker_ns),
                to_ms(fc.mc_find_ns),
                fc.mc_find_calls == 0 ? 0.0
                                      : static_cast<double>(fc.mc_find_ns) / 1e3
                        / fc.mc_find_calls,
                (unsigned long long) fc.mc_lines,
                static_cast<double>(fc.mc_read_bytes) / (1024.0 * 1024.0),
                (unsigned long long) with_body,
                (unsigned long long) without_stmt,
                pct(without_stmt, with_body),
                (unsigned long long) fc.mc_src_locations,
                (unsigned long long) fc.mc_messages,
                (unsigned long long) fc.mc_no_body,
                lf->get_filename().c_str());
        }
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
        sm.sm_status_change_time.load(),
        sm.sm_version,
        sm.sm_working ? "Mapping log messages to statements and stack frames"
                      : "",
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

    /** Counts a message logged at [t] (log_time_msecs). */
    void add(int64_t t)
    {
        if (this->st_hits == 0) {
            this->st_first_time = this->st_last_time = t;
        } else {
            this->st_first_time = std::min(this->st_first_time, t);
            this->st_last_time = std::max(this->st_last_time, t);
        }
        this->st_hits += 1;
        auto bkey = t >> BASE_SHIFT;
        if (!this->st_runs.empty() && this->st_runs.back().first == bkey) {
            this->st_runs.back().second += 1;
        } else {
            this->st_runs.emplace_back(bkey, 1);
        }
    }

    /**
     * The message times as the buckets column has them, merged by the
     * smallest power of two that fits the span in MAX_BUCKETS, as the
     * IntelliJ plugin's Histogram does, so the output doesn't grow with the
     * span; and that power, the bucket_shift column.
     */
    std::pair<std::string, int> buckets()
    {
        std::sort(this->st_runs.begin(), this->st_runs.end());
        auto lo = this->st_runs.front().first;
        auto hi = this->st_runs.back().first;
        int shift = 0;
        while ((hi >> shift) - (lo >> shift) + 1 > MAX_BUCKETS) {
            shift += 1;
        }

        std::string retval = "[";
        std::optional<std::pair<int64_t, int64_t>> curr;
        auto emit = [&retval](const std::pair<int64_t, int64_t>& b) {
            if (retval.size() > 1) {
                retval.push_back(',');
            }
            retval.append(
                fmt::format(FMT_STRING("[{},{}]"), b.first, b.second));
        };
        for (const auto& [bkey, n] : this->st_runs) {
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
        retval.push_back(']');
        return {std::move(retval), shift};
    }
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

            totals[id].add(ll->get_time<std::chrono::milliseconds>().count());
        }

        this->lss_rows.clear();
        this->lss_mapped = mapped;
        this->lss_total = total;
        for (uint32_t id = FIRST_STATEMENT; id < totals.size(); id++) {
            auto& st = totals[id];
            if (st.st_hits == 0) {
                continue;
            }

            auto [buckets, shift] = st.buckets();
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

struct log_stack_frames_vtab
    : public tvt_iterator_cursor<log_stack_frames_vtab> {
    struct row {
        int64_t r_hits;
        int64_t r_first_time;
        int64_t r_last_time;
        std::string r_buckets;
        int r_bucket_shift;
        /** The functions column's JSON, or nullopt without any names. */
        std::optional<std::string> r_functions;
    };

    /** By location id, which is the rowid. */
    using iterator = std::map<location_id, row>::iterator;

    static constexpr const char* NAME = "log_stack_frames";
    static constexpr const char* CREATE_STMT = R"(
-- The locations in the stack traces of multi-line messages in the LOG view,
-- after filters, as the /log/stack-frames patterns find them, with the number
-- of messages that have each one.  Frames whose paths are the same file under
-- the source paths are one location, however their traces write the path.  When log2src has found log statements
-- under the source paths (see :add-source-path), only the patterns for their
-- languages are used (/log/stack-frames/<name>/languages).  Querying this
-- table starts the background task that log_stmt_stats does; until it
-- finishes, the counts cover only the messages looked at so far.
CREATE TABLE lnav_db.log_stack_frames (
    path TEXT,           -- The file as the trace has it, with a Java frame's
                         -- package as directories; for a location in
                         -- src_path, as the first frame found there has it.
    line INTEGER,        -- The line number.
    src_path TEXT,       -- The file under the source roots (see
                         -- :add-source-path) that the path is, or NULL.
    hits INTEGER,        -- The number of messages with a frame at the
                         -- location, each counted once however often.
    first_time INTEGER,  -- The log_time_msecs of its first message.
    last_time INTEGER,   -- The log_time_msecs of its last message.
    buckets TEXT,        -- A JSON array of [key, count] for its message times,
                         -- where key is log_time_msecs >> 12, merged so there
                         -- are at most 32 buckets.
    bucket_shift INTEGER,-- How far the keys were merged: each bucket covers
                         -- 4096 << bucket_shift milliseconds.
    functions TEXT,      -- A JSON array of [name, count] for the functions the
                         -- traces say the location is in (a pattern's 'func'
                         -- capture), most messages first, or NULL when none
                         -- are named.  Traces from builds where the line was
                         -- in another function name more than one.
    mapped INTEGER HIDDEN, -- The messages in the LOG view looked at so far.
    total INTEGER HIDDEN   -- The messages in the LOG view.
);
)";

    std::map<location_id, row> lsf_rows;
    int64_t lsf_mapped{0};
    int64_t lsf_total{0};
    std::optional<std::tuple<uint64_t, uint32_t, size_t, size_t, size_t>>
        lsf_rows_key;

    iterator begin()
    {
        this->refresh();
        return this->lsf_rows.begin();
    }

    iterator end() { return this->lsf_rows.end(); }

    /**
     * The location's id: stable while lnav runs, since locations are never
     * removed or renumbered, so it's what log_stack_frame_rows() takes.  A
     * source scan can move a frame to another location, though.
     */
    sqlite3_int64 get_rowid(iterator iter)
    {
        return type_safe::get(iter->first);
    }

    void refresh()
    {
        auto& sm = get_map();
        auto& lss = lnav_data.ld_log_source;

        start_map();
        if (lnav_data.ld_flags.is_set<lnav_flags::headless>()) {
            // As in log_stmt_stats: no main loop to map in slices.
            lnav::progress_tracker::instance().wait_for_completion(
                &lnav_data.ld_status_refresher);
            lnav::stmt_stats::step(
                std::chrono::steady_clock::time_point::max());
        } else {
            sm.sync_files(lss);
        }

        sm.sync_frame_locations();

        auto key = std::make_tuple(lss.lss_index_change_seq,
                                   lss.lss_index_generation,
                                   sm.sm_frame_changes,
                                   lss.text_line_count(),
                                   sm.sm_frame_locations_generation);
        if (this->lsf_rows_key == key) {
            return;
        }
        this->lsf_rows_key = key;

        sm.sync_frame_funcs();

        std::vector<stmt_totals> totals(sm.sm_locations.size());
        std::vector<uint32_t> locs;
        // A message's (location, function name id) pairs, and their counts.
        std::vector<uint64_t> loc_funcs;
        std::unordered_map<uint64_t, int64_t> func_hits;
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
            const auto& si = lf->get_stmt_ids();
            if (line_number >= si.si_frames_verified) {
                continue;
            }
            mapped += 1;
            auto [lo, hi] = si.frames_of(line_number);
            if (lo == hi) {
                continue;
            }
            // Two of a message's frames can be at the same location, but it's
            // counted once there.
            locs.clear();
            loc_funcs.clear();
            for (auto index = lo; index < hi; index++) {
                auto fid = type_safe::get(si.si_frame_ids[index]);
                auto loc = type_safe::get(sm.sm_frame_locations[fid]);
                locs.push_back(loc);
                auto func = sm.sm_frame_funcs[fid];
                if (func != 0) {
                    loc_funcs.push_back((uint64_t) loc << 32 | func);
                }
            }
            std::sort(locs.begin(), locs.end());
            locs.erase(std::unique(locs.begin(), locs.end()), locs.end());
            auto t = ll->get_time<std::chrono::milliseconds>().count();
            for (auto loc : locs) {
                totals[loc].add(t);
            }
            std::sort(loc_funcs.begin(), loc_funcs.end());
            loc_funcs.erase(std::unique(loc_funcs.begin(), loc_funcs.end()),
                            loc_funcs.end());
            for (auto lf_pair : loc_funcs) {
                func_hits[lf_pair] += 1;
            }
        }

        // Each location's function names and counts.
        std::unordered_map<uint32_t, std::vector<std::pair<uint32_t, int64_t>>>
            loc_func_counts;
        for (const auto& [lf_pair, n] : func_hits) {
            loc_func_counts[(uint32_t) (lf_pair >> 32)].emplace_back(
                (uint32_t) (lf_pair & 0xffffffff), n);
        }

        this->lsf_rows.clear();
        this->lsf_mapped = mapped;
        this->lsf_total = total;
        for (uint32_t id = 0; id < totals.size(); id++) {
            auto& st = totals[id];
            if (st.st_hits == 0) {
                continue;
            }

            std::optional<std::string> functions;
            auto lfc_iter = loc_func_counts.find(id);
            if (lfc_iter != loc_func_counts.end()) {
                auto& counts = lfc_iter->second;
                // Most messages first, then by name, so the order is stable.
                std::sort(counts.begin(),
                          counts.end(),
                          [&sm](const auto& lhs, const auto& rhs) {
                              if (lhs.second != rhs.second) {
                                  return lhs.second > rhs.second;
                              }
                              return sm.sm_func_names[lhs.first - 1]
                                  < sm.sm_func_names[rhs.first - 1];
                          });
                yajlpp_gen gen;
                yajl_gen_config(gen, yajl_gen_beautify, false);
                {
                    yajlpp_array arr(gen);
                    for (const auto& [func, n] : counts) {
                        yajlpp_array pair(gen);
                        pair.gen(sm.sm_func_names[func - 1].to_frag());
                        pair.gen(n);
                    }
                }
                functions = gen.to_string_fragment().to_string();
            }

            auto [buckets, shift] = st.buckets();
            this->lsf_rows.emplace(location_id(id),
                                   row{
                                       st.st_hits,
                                       st.st_first_time,
                                       st.st_last_time,
                                       std::move(buckets),
                                       shift,
                                       std::move(functions),
                                   });
        }
        if (sm.sm_frame_locations_pending) {
            // Some paths couldn't be looked up during a source scan: do them
            // the next time.
            this->lsf_rows_key = std::nullopt;
        }
    }

    int get_column(cursor& vc, sqlite3_context* ctx, int col)
    {
        const auto& [id, r] = *vc.iter;
        const auto& loc = get_map().sm_locations[type_safe::get(id)];

        switch (col) {
            // A frame's path is never freed or changed (see sm_frames), so
            // SQLite can take it without a copy.
            case 0:
                sqlite3_result_text(ctx,
                                    loc.loc_path.to_frag().data(),
                                    loc.loc_path.to_frag().length(),
                                    SQLITE_STATIC);
                break;
            case 1:
                to_sqlite(ctx, loc.loc_line);
                break;
            case 2:
                if (loc.loc_src_path) {
                    to_sqlite(ctx, loc.loc_src_path.value());
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            case 3:
                to_sqlite(ctx, r.r_hits);
                break;
            case 4:
                to_sqlite(ctx, r.r_first_time);
                break;
            case 5:
                to_sqlite(ctx, r.r_last_time);
                break;
            case 6:
                to_sqlite(ctx, r.r_buckets);
                sqlite3_result_subtype(ctx, JSON_SUBTYPE);
                break;
            case 7:
                to_sqlite(ctx, (int64_t) r.r_bucket_shift);
                break;
            case 8:
                if (r.r_functions) {
                    to_sqlite(ctx, r.r_functions.value());
                    sqlite3_result_subtype(ctx, JSON_SUBTYPE);
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            case 9:
                to_sqlite(ctx, this->lsf_mapped);
                break;
            case 10:
                to_sqlite(ctx, this->lsf_total);
                break;
        }

        return SQLITE_OK;
    }
};

auto log_stmt_stats_binder
    = injector::bind_multiple<vtab_module_base>()
          .add<vtab_module<tvt_no_update<log_stmt_stats_vtab>>>()
          .add<vtab_module<tvt_no_update<log_stack_frames_vtab>>>();

}  // namespace

DIST_SLICE(prog_reps)
lnav::progress_reporter_t LOG_STMT_MAP_PROG_REP = stmt_map_progress;

/**
 * Where a list of LOG view rows, read up to [row_count] as of the index's
 * [seq] and [generation], has to be cut so the rows from there on are read
 * again: 0 when it isn't [valid] or the view was rebuilt, else the first row
 * the index changes since touched, or nullopt when it's current.
 */
static std::optional<size_t>
rows_changed_from(logfile_sub_source& lss,
                  bool valid,
                  uint64_t seq,
                  uint32_t generation,
                  size_t row_count)
{
    std::optional<size_t> retval;
    if (!valid || generation != lss.lss_index_generation) {
        retval = 0;
    } else if (seq != lss.lss_index_change_seq) {
        const auto& changes = lss.lss_index_changes;
        if (changes.empty() || changes.front().ic_seq > seq + 1) {
            retval = 0;
        } else {
            for (const auto& ic : changes) {
                if (ic.ic_seq > seq) {
                    retval = std::min(retval.value_or(ic.ic_from_row),
                                      ic.ic_from_row);
                }
            }
        }
    }
    const auto now_rows = lss.text_line_count();
    if (now_rows < row_count) {
        retval = std::min(retval.value_or(now_rows), now_rows);
    }
    return retval;
}

/**
 * [from], or the start of the message whose row it is, when it continues one
 * that started before it: rows appended to a message that started before them
 * (a trace still being written) change that whole message, whose statement and
 * frames are matched again, but lnav reports the change only from the first
 * new row.  [row_count] is the view's rows now.
 */
static size_t
message_start_row(logfile_sub_source& lss, size_t from, size_t row_count)
{
    while (from > 0 && from < row_count) {
        uint64_t line_number;
        auto ld_iter = lss.find_data(lss.at(vis_line_t(from)), line_number);
        const auto* lf = (*ld_iter)->get_file_ptr();
        if (lf == nullptr || !(lf->begin() + line_number)->is_continued()) {
            break;
        }
        from -= 1;
    }
    return from;
}

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
    auto trim_from
        = rows_changed_from(lss,
                            sr.sr_valid && sr.sr_remaps == sm.sm_remaps,
                            sr.sr_index_change_seq,
                            sr.sr_index_generation,
                            sr.sr_row_count);
    {
        auto from = message_start_row(
            lss,
            std::min(trim_from.value_or(sr.sr_row_count), sr.sr_row_count),
            row_count);
        if (from < sr.sr_row_count) {
            trim_from = from;
        }
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

/** Whether frame [id]'s location matches [q]. */
static bool
frame_matches(const stmt_map& sm, const frame_query& q, size_t id)
{
    auto loc_id = sm.sm_frame_locations[id];
    if (q.fq_frame && q.fq_frame.value() != (int64_t) type_safe::get(loc_id)) {
        return false;
    }
    const auto& loc = sm.sm_locations[type_safe::get(loc_id)];
    if (q.fq_line && q.fq_line.value() != loc.loc_line) {
        return false;
    }
    if (q.fq_src_path) {
        return loc.loc_src_path
            && loc.loc_src_path.value() == q.fq_src_path.value();
    }
    return true;
}

/**
 * The LOG view rows, ascending, of the messages whose stack traces have a
 * frame that matches [q], for log_stack_frame_rows().  Until the map is done,
 * only the messages looked at so far.  Good until the next call.
 */
static const std::vector<vis_line_t>&
frame_rows(logfile_sub_source& lss, const frame_query& q)
{
    auto& sm = get_map();

    start_map();
    if (lnav_data.ld_flags.is_set<lnav_flags::headless>()) {
        // As in log_stack_frames: no main loop to map in slices.
        lnav::progress_tracker::instance().wait_for_completion(
            &lnav_data.ld_status_refresher);
        lnav::stmt_stats::step(std::chrono::steady_clock::time_point::max());
    } else if (stmt_map::is_behind(lss)) {
        lnav::stmt_stats::step(std::chrono::steady_clock::now()
                               + std::chrono::milliseconds(100));
    }
    sm.sync_frame_locations();

    auto& fr = sm.sm_frame_rows;
    if (fr.fr_query != q || fr.fr_resets != sm.sm_frame_resets
        || fr.fr_locations_generation != sm.sm_frame_locations_generation)
    {
        fr.fr_valid = false;
    }
    if (!fr.fr_valid) {
        fr.fr_query = q;
        fr.fr_frames.clear();
        fr.fr_rows.clear();
        fr.fr_row_count = 0;
    }

    for (auto id = fr.fr_frames.size(); id < sm.sm_frames.size(); id++) {
        fr.fr_frames.push_back(frame_matches(sm, q, id));
    }

    const auto row_count = lss.text_line_count();
    auto trim_from = rows_changed_from(lss,
                                       fr.fr_valid,
                                       fr.fr_index_change_seq,
                                       fr.fr_index_generation,
                                       fr.fr_row_count);
    {
        auto from = message_start_row(
            lss,
            std::min(trim_from.value_or(fr.fr_row_count), fr.fr_row_count),
            row_count);
        if (from < fr.fr_row_count) {
            trim_from = from;
        }
    }
    if (trim_from) {
        fr.fr_rows.erase(std::lower_bound(fr.fr_rows.begin(),
                                          fr.fr_rows.end(),
                                          vis_line_t(trim_from.value())),
                         fr.fr_rows.end());
        fr.fr_row_count = std::min(fr.fr_row_count, trim_from.value());
    }

    auto vl = fr.fr_row_count;
    for (; vl < row_count; vl++) {
        uint64_t line_number;
        auto ld_iter = lss.find_data(lss.at(vis_line_t(vl)), line_number);
        const auto* lf = (*ld_iter)->get_file_ptr();
        if (lf == nullptr) {
            continue;
        }
        const auto& si = lf->get_stmt_ids();
        if (line_number >= si.si_frames_verified) {
            break;
        }
        auto [lo, hi] = si.frames_of(line_number);
        for (auto index = lo; index < hi; index++) {
            if (fr.fr_frames[type_safe::get(si.si_frame_ids[index])]) {
                fr.fr_rows.emplace_back(vl);
                break;
            }
        }
    }
    // With a file that couldn't be looked up, the frames' locations are found
    // again next time, which starts this over.
    fr.fr_valid = true;
    fr.fr_row_count = vl;
    fr.fr_index_change_seq = lss.lss_index_change_seq;
    fr.fr_index_generation = lss.lss_index_generation;
    fr.fr_resets = sm.sm_frame_resets;
    fr.fr_locations_generation = sm.sm_frame_locations_generation;

    return fr.fr_rows;
}

enum class frame_rows_col : int {
    log_line,
    src_path,
    line,
    frame,
};

/**
 * The log_stack_frame_rows() table-valued function: the LOG view rows of the
 * messages with a stack frame at a source location in their trace, for
 * finding them in all_logs with
 * `log_line IN (SELECT log_line FROM log_stack_frame_rows(?, ?))`.
 */
struct log_stack_frame_rows {
    static constexpr const char* NAME = "log_stack_frame_rows";
    static constexpr const char* CREATE_STMT = R"(
-- The log_stack_frame_rows() table-valued function lists the LOG view rows of
-- the messages with a stack frame in their trace.
CREATE TABLE lnav_db.log_stack_frame_rows (
    log_line INTEGER,     -- The row of a message with the frame in its stack trace.
    src_path TEXT HIDDEN, -- The frame's file under the source paths, as log_stack_frames.src_path has it.
    line INTEGER HIDDEN,  -- The frame's line; any line in the file when NULL.
    frame INTEGER HIDDEN  -- The location, as its log_stack_frames rowid.
);
)";

    /** idxNum bits: which of the hidden columns were given, in this order. */
    static constexpr int HAS_SRC_PATH = 1;
    static constexpr int HAS_LINE = 2;
    static constexpr int HAS_FRAME = 4;

    struct cursor {
        sqlite3_vtab_cursor base;
        /** A copy: the map's list can change on the next call. */
        std::vector<vis_line_t> c_rows;
        size_t c_index{0};
        frame_query c_query;

        explicit cursor(sqlite3_vtab* vt) : base({vt}) {}

        int next()
        {
            this->c_index += 1;
            return SQLITE_OK;
        }

        int filter(int idxNum,
                   const char* idxStr,
                   int argc,
                   sqlite3_value** argv)
        {
            this->c_rows.clear();
            this->c_index = 0;
            this->c_query = frame_query{};

            // A NULL for a part leaves it out, so the parts after it can be
            // given by position.
            int argi = 0;
            if (idxNum & HAS_SRC_PATH) {
                auto* val = argv[argi++];
                switch (sqlite3_value_type(val)) {
                    case SQLITE_NULL:
                        break;
                    case SQLITE_TEXT:
                        this->c_query.fq_src_path
                            = (const char*) sqlite3_value_text(val);
                        break;
                    default:
                        this->base.pVtab->zErrMsg = sqlite3_mprintf(
                            "%s: expecting a source path as the first "
                            "argument; for a frame's rowid, use "
                            "%s(NULL, NULL, rowid)",
                            NAME,
                            NAME);
                        return SQLITE_ERROR;
                }
            }
            if (idxNum & HAS_LINE) {
                auto* val = argv[argi++];
                if (sqlite3_value_type(val) != SQLITE_NULL) {
                    this->c_query.fq_line = sqlite3_value_int64(val);
                }
            }
            if (idxNum & HAS_FRAME) {
                auto* val = argv[argi++];
                if (sqlite3_value_type(val) != SQLITE_NULL) {
                    this->c_query.fq_frame = sqlite3_value_int64(val);
                }
            }
            // Without a file or a frame, it would be every message with a
            // trace, which isn't what this is for.
            if (!this->c_query.fq_src_path && !this->c_query.fq_frame) {
                return SQLITE_OK;
            }

            this->c_rows = frame_rows(lnav_data.ld_log_source, this->c_query);
            return SQLITE_OK;
        }

        int eof() const { return this->c_index >= this->c_rows.size(); }

        int get_rowid(sqlite3_int64& rowid_out)
        {
            rowid_out = (sqlite3_int64) this->c_index;
            return SQLITE_OK;
        }
    };

    int get_column(const cursor& vc, sqlite3_context* ctx, int col)
    {
        switch (frame_rows_col(col)) {
            case frame_rows_col::log_line:
                to_sqlite(ctx, (int64_t) vc.c_rows[vc.c_index]);
                break;
            case frame_rows_col::src_path:
                to_sqlite(ctx, vc.c_query.fq_src_path);
                break;
            case frame_rows_col::line:
                to_sqlite(ctx, vc.c_query.fq_line);
                break;
            case frame_rows_col::frame:
                to_sqlite(ctx, vc.c_query.fq_frame);
                break;
        }
        return SQLITE_OK;
    }

    int best_index(sqlite3_vtab* tab, sqlite3_index_info* p_info)
    {
        static constexpr std::pair<frame_rows_col, int> HIDDEN[] = {
            {frame_rows_col::src_path, HAS_SRC_PATH},
            {frame_rows_col::line, HAS_LINE},
            {frame_rows_col::frame, HAS_FRAME},
        };

        int used[3] = {-1, -1, -1};
        for (int lpc = 0; lpc < p_info->nConstraint; lpc++) {
            const auto& cons = p_info->aConstraint[lpc];
            if (cons.op != SQLITE_INDEX_CONSTRAINT_EQ) {
                continue;
            }
            if (!cons.usable) {
                // An argument from another table (`f.rowid` in a join) isn't
                // known in this plan; without it, the query would be another
                // one, so make SQLite pick a plan that has it.
                for (const auto& hidden : HIDDEN) {
                    if (cons.iColumn == (int) hidden.first) {
                        return SQLITE_CONSTRAINT;
                    }
                }
                continue;
            }
            for (size_t hi = 0; hi < 3; hi++) {
                if (cons.iColumn == (int) HIDDEN[hi].first && used[hi] == -1) {
                    used[hi] = lpc;
                }
            }
        }

        int idx_num = 0;
        int argv_index = 0;
        for (size_t hi = 0; hi < 3; hi++) {
            if (used[hi] == -1) {
                continue;
            }
            // The rows are exactly the query's, so SQLite needn't check.
            p_info->aConstraintUsage[used[hi]].argvIndex = ++argv_index;
            p_info->aConstraintUsage[used[hi]].omit = 1;
            idx_num |= HIDDEN[hi].second;
        }
        p_info->idxNum = idx_num;
        if (idx_num & (HAS_SRC_PATH | HAS_FRAME)) {
            p_info->estimatedCost = 1.0;
            p_info->estimatedRows = 100;
        } else {
            p_info->estimatedCost = 2147483647;
            p_info->estimatedRows = 2147483647;
        }
        // The rows come in order.
        if (p_info->nOrderBy == 1
            && p_info->aOrderBy[0].iColumn == (int) frame_rows_col::log_line
            && !p_info->aOrderBy[0].desc)
        {
            p_info->orderByConsumed = 1;
        }
        return SQLITE_OK;
    }
};

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
    auto languages_changed = false;

#ifdef HAVE_RUST_DEPS
    // While log2src is scanning the source, the statements are in flux, so
    // wait.  Once it's done, everything is matched again; until a line is, it
    // keeps the statement it had.
    auto ext = lnav_rs_ext::get_status();
    if (ext.status == lnav_rs_ext::Status::working) {
        return false;
    }
    if (sm.sm_scan_version != ext.version) {
        sm.sm_languages = {};
        for (const auto& lang : lnav_rs_ext::source_languages()) {
            auto sl = logfile_sub_source_ns::source_language_from_lsp_id(
                string_fragment::from_bytes(lang.data(), lang.size()));
            if (sl) {
                sm.sm_languages.set(sl.value());
            } else {
                log_warning("unknown log2src language: %.*s",
                            (int) lang.size(),
                            lang.data());
            }
        }
        languages_changed = true;
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

    // A config reload or a source scan can change the patterns used to find
    // the frames.  A reload can also leave them as they were, so the frames
    // are only found again if the patterns themselves differ.
    const auto frame_generation
        = logfile_sub_source_ns::stack_frames_generation();
    if (languages_changed || sm.sm_frame_generation != frame_generation) {
        static const auto& cfg
            = injector::get<const logfile_sub_source_ns::config&>();

        sm.sm_frame_generation = frame_generation;
        std::string patterns;
        std::string names;
        for (const auto& [name, sfd] : cfg.c_stack_frames) {
            if (!sfd.is_used_for(sm.sm_languages)) {
                continue;
            }
            patterns.append(name).push_back('\n');
            patterns.append(sfd.sfd_pattern.pp_value->get_pattern())
                .push_back('\n');
            if (!names.empty()) {
                names.append(", ");
            }
            names.append(name);
        }
        if (sm.sm_frame_patterns != patterns) {
            log_info("finding stack frames with: %s", names.c_str());
            if (sm.sm_frame_patterns) {
                log_info(
                    "stack frame patterns changed, finding the frames "
                    "again");
                for (const auto& ld : lss) {
                    auto* lf = ld->get_file_ptr();
                    if (lf != nullptr) {
                        lf->get_stmt_ids().reset_frames();
                    }
                }
                sm.sm_frame_changes += 1;
                sm.sm_frame_resets += 1;
            }
            sm.sm_frame_patterns = std::move(patterns);
        }
    }

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
        auto& si = lf->get_stmt_ids();
        // A format without a body has parsed every part of its messages into
        // fields, so the whole file has nothing to match.  Its frames aren't
        // looked for either: a format like that with stack traces in a field
        // seems unlikely.
        const auto* format = lf->get_format_ptr();
        if (format != nullptr && !format->lf_has_body) {
            std::fill(si.si_ids.begin() + si.si_verified,
                      si.si_ids.end(),
                      NO_STATEMENT);
            si.si_verified = si.si_ids.size();
            si.si_frames_verified = si.si_ids.size();
            continue;
        }
        if (si.si_verified < si.si_ids.size()
            || si.si_frames_verified < si.si_ids.size())
        {
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
    std::vector<char> frames_found(work.size(), 0);
    // Whether this slice has mapped anything yet: past the deadline, a worker
    // only starts another file while nothing has been.
    std::atomic<bool> progressed{false};
    if (!work.empty()) {
        if (!sm.sm_pass.pc_start) {
            sm.sm_pass.pc_start = std::chrono::steady_clock::now();
        }
        const auto width = lnav::logfile_indexing_width(work.size());
        sm.sm_pass.pc_max_width
            = std::max<uint64_t>(sm.sm_pass.pc_max_width, width);
        sm.sm_pass.pc_files += work.size();
        const auto slice_start = std::chrono::steady_clock::now();
        lnav::parallel_for_each(work.size(), width, [&](size_t index) {
            auto* lf = work[index];
            auto& si = lf->get_stmt_ids();
            stmt_map::match_scratch ms;
            size_t done = 0;
            const auto worker_start = std::chrono::steady_clock::now();
            // Whatever way the worker leaves, its counts go in the map's.
            auto merge_counters = lnav::finally([&] {
                ms.ms_counters.mc_worker_ns += ns_since(worker_start);
                // Only this worker has the file during the slice.
                si.si_counters.merge(ms.ms_counters);
                std::lock_guard<std::mutex> lg(sm.sm_counters_mutex);
                sm.sm_counters.merge(ms.ms_counters);
            });

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
            // The statements and the frames each go on from where they
            // got to: after a source re-scan only the statements are
            // matched again, after a change to the frame patterns only
            // the frames.
            while (true) {
                auto line = std::min(si.si_verified, si.si_frames_verified);
                if (line >= si.si_ids.size()) {
                    break;
                }
                // Check the clock every few lines: a line can take a
                // millisecond (a JSON message parsed, then matched by
                // log2src), so every 256 overran the slice by ~250ms.
                if (done > 0 && done % STEP_CLOCK_LINES == 0
                    && std::chrono::steady_clock::now() >= deadline)
                {
                    unfinished[index] = 1;
                    break;
                }
                auto want_stmt = line == si.si_verified;
                auto want_frames = line == si.si_frames_verified;
                auto id = sm.match_line(lf, line, ms, want_stmt, want_frames);
                if (want_stmt) {
                    auto old_id = si.si_ids[line];
                    if (old_id != id) {
                        si.si_ids[line] = id;
                        changed[index] = 1;
                        if (old_id != UNMAPPED) {
                            remapped[index] = 1;
                        }
                    }
                    si.si_verified += 1;
                }
                if (want_frames) {
                    si.add_frames(ms.ms_frames);
                    if (!ms.ms_frames.empty()) {
                        frames_found[index] = 1;
                    }
                }
                done += 1;
                progressed.store(true, std::memory_order_relaxed);
            }
        });
        const auto slice_end = std::chrono::steady_clock::now();
        const auto slice_ns
            = std::chrono::duration_cast<steady_ns>(slice_end - slice_start)
                  .count();
        sm.sm_pass.pc_slices += 1;
        sm.sm_pass.pc_slice_ns += slice_ns;
        sm.sm_pass.pc_max_slice_ns
            = std::max<uint64_t>(sm.sm_pass.pc_max_slice_ns, slice_ns);
        if (slice_end > deadline) {
            const uint64_t overrun_ns
                = std::chrono::duration_cast<steady_ns>(slice_end - deadline)
                      .count();
            sm.sm_pass.pc_overrun_ns += overrun_ns;
            sm.sm_pass.pc_max_overrun_ns
                = std::max(sm.sm_pass.pc_max_overrun_ns, overrun_ns);
        }
        const auto deferred
            = std::count(unfinished.begin(), unfinished.end(), 1);
        sm.sm_pass.pc_files_deferred += deferred;
        if (deferred > 0) {
            sm.sm_pass.pc_short_slices += 1;
        }
    }

    auto more = std::find(unfinished.begin(), unfinished.end(), 1)
        != unfinished.end();
    if (std::find(changed.begin(), changed.end(), 1) != changed.end()) {
        sm.sm_changes += 1;
    }
    if (std::find(remapped.begin(), remapped.end(), 1) != remapped.end()) {
        sm.sm_remaps += 1;
    }
    if (std::find(frames_found.begin(), frames_found.end(), 1)
        != frames_found.end())
    {
        sm.sm_frame_changes += 1;
    }
    size_t completed = 0;
    size_t total = 0;
    for (const auto& ld : lss) {
        const auto* lf = ld->get_file_ptr();
        if (lf != nullptr) {
            const auto& si = lf->get_stmt_ids();
            completed += std::min(si.si_verified, si.si_frames_verified);
            total += si.si_ids.size();
        }
    }

    // Report progress in steps of about a percent, so pollers aren't woken
    // for every slice.
    auto old_completed = sm.sm_completed.load();
    auto old_total = sm.sm_total.load();
    auto was_working = sm.sm_working.load();
    if (!more && progressed.load()) {
        size_t with_frames = 0;
        size_t frame_refs = 0;
        for (const auto& ld : lss) {
            const auto* lf = ld->get_file_ptr();
            if (lf == nullptr) {
                continue;
            }
            const auto& si = lf->get_stmt_ids();
            frame_refs += si.si_frame_ids.size();
            with_frames += si.si_frame_lines.size();
        }
        log_info(
            "mapped %zu lines; %zu messages have stack frames, %zu "
            "frames in all, %zu different",
            total,
            with_frames,
            frame_refs,
            sm.sm_frames.size());
        sm.log_pass_stats(lss);
#ifdef HAVE_RUST_DEPS
        lnav_rs_ext::log_match_stats();
#endif
    }
    sm.sm_completed = completed;
    sm.sm_total = total;
    if (was_working != more) {
        sm.sm_working = more;
        sm.sm_status_change_time = std::chrono::steady_clock::now();
    }
    if (was_working != more || (total == 0 && old_total != 0)
        || (total > 0
            && (completed - std::min(completed, old_completed)) * 100 >= total))
    {
        sm.sm_version += 1;
    }

    return more;
}

int
register_frame_rows(sqlite3* db)
{
    static vtab_module<tvt_no_update<log_stack_frame_rows>> FRAME_ROWS_MODULE;
    static auto frame_rows_help
        = help_text(log_stack_frame_rows::NAME,
                    "A table-valued function that lists the LOG view rows of "
                    "the messages with a stack frame at a source location in "
                    "their stack trace, in order, for finding them in "
                    "all_logs with log_line IN (...).  A message is listed "
                    "once, however many of its frames match.  Until the "
                    "background task that finds the frames is done, only the "
                    "messages looked at so far.")
              .sql_table_valued_function()
              .with_parameter({"src_path",
                               "The file under the source paths, as "
                               "log_stack_frames.src_path has it."})
              .with_parameter(help_text{
                  "line", "The line in the file.  Any line, if not given."}
                                  .optional())
              .with_parameter(help_text{"frame",
                                        "A location's rowid in "
                                        "log_stack_frames, to list the "
                                        "messages with a frame there instead.  "
                                        "Pass NULL for src_path and line to "
                                        "use it alone."}
                                  .optional())
              .with_result({"log_line",
                            "The row of a message with the frame in its "
                            "stack trace."})
              .with_tags({"text"})
              .with_example({
                  "To get the messages with a frame at a line of a file in "
                  "their stack trace",
                  "SELECT log_line, log_body FROM all_logs WHERE log_line IN "
                  "(SELECT log_line FROM "
                  "log_stack_frame_rows('/src/app/Foo.java', 83))",
              });

    auto rc = FRAME_ROWS_MODULE.create(db, log_stack_frame_rows::NAME);
    ensure(rc == SQLITE_OK);
    sqlite_function_help.emplace(log_stack_frame_rows::NAME, &frame_rows_help);
    frame_rows_help.index_tags();

    return rc;
}

}  // namespace lnav::stmt_stats
