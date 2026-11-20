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

#include <chrono>
#include <cmath>
#include <optional>
#include <string_view>
#include <vector>

#include "base/date_time_scanner.hh"
#include "base/injector.bind.hh"
#include "base/time_util.hh"
#include "base/lnav_log.hh"
#include "lnav.hh"
#include "logfile.hh"
#include "logfile_sub_source.hh"
#include "logline_window.hh"
#include "sql_util.hh"
#include "textview_curses.hh"
#include "vtab_module.hh"

namespace {

// Long-format view over every metrics_log file lnav currently has open:
// each cell (file × row × non-null column) produces one virtual row.
// Designed so that a query like
//
//   SELECT log_time, source, metric, value FROM all_metrics WHERE
//   metric='cpu_pct'
//
// works uniformly regardless of which CSV file supplied the value.
//
// Uses a custom cursor (rather than `tvt_iterator_cursor`) so the
// cursor can own a `logline_window` and a move-only
// `logline_window::iterator` directly — sqlite only takes the cursor
// by pointer, so the copy-constructibility constraint of the iterator
// model doesn't apply here.
struct metrics_vtab {
    static constexpr const char* NAME = "all_metrics";

    static constexpr int LOG_TIME_COL = 1;
    static constexpr int METRIC_COL = 4;
    static constexpr int LOG_TIME_USECS_COL = 10;
    static constexpr int UNIT_SUFFIX_COL = 11;
    static constexpr int UNIT_DIVISOR_COL = 12;

    // What best_index() found to narrow the scan by: idxStr has one of these
    // for each argument to filter(), in order.  Every bound is used, since
    // SQLite only passes constraints that all have to hold.
    static constexpr char PLAN_TIME_EQ = 'e';
    static constexpr char PLAN_TIME_LO = 'l';
    static constexpr char PLAN_TIME_HI = 'h';
    // The same bounds on log_time_usecs, so a number, not text.
    static constexpr char PLAN_USECS_EQ = 'E';
    static constexpr char PLAN_USECS_LO = 'L';
    static constexpr char PLAN_USECS_HI = 'H';
    static constexpr char PLAN_METRIC_EQ = 'm';
    static constexpr char PLAN_METRIC_IN = 'i';
    static constexpr const char* CREATE_STMT = R"(
-- A long-format view over every metrics_log file lnav currently has open.
-- Each (file, row, column) combination produces one virtual row, so
-- queries can filter by metric name without knowing which CSV file
-- carried the value.
-- Tags: display
CREATE TABLE lnav_db.all_metrics (
    log_line INTEGER,         -- The line number in the LOG view of the
                              -- sample row (shared across all metrics at
                              -- this timestamp, including sibling files).
    log_time DATETIME,        -- The timestamp for the metric sample.
    log_path TEXT,            -- The file the sample came from.
    source TEXT,              -- The file-stem shown above the sample's
                              -- column in the focused-row overlay.
    metric TEXT,              -- The column name of the metric.
    value REAL,               -- The parsed numeric value.
    raw_value TEXT HIDDEN COLLATE measure_with_units,
                              -- The original cell text from the file (e.g. "20.0KB").
    log_mark INTEGER HIDDEN,  -- True if the sample's row is user-marked.
    kind TEXT HIDDEN,         -- 'counter' if the metric's samples are
                              -- running totals, otherwise 'gauge'.
    change REAL HIDDEN,       -- For a counter, the change since the
                              -- previous sample of the metric, NULL for
                              -- its first sample.  NULL for a gauge.
    log_time_usecs INTEGER HIDDEN,
                              -- The log_time column as the number of
                              -- microseconds from the epoch, as in all_logs.
    unit_suffix TEXT HIDDEN,  -- The suffix the value is shown with, e.g.
                              -- 's' or 'B', or NULL if it has none.
    unit_divisor REAL HIDDEN  -- What the value is divided by to be in the
                              -- unit of the suffix.
);
)";

    struct cursor {
        sqlite3_vtab_cursor base{};

        logfile_sub_source* c_lss{nullptr};
        // The outer window: a `logline_window` over every visible row.
        // Created fresh in `reset()` (sqlite's `xFilter` callback).
        std::unique_ptr<logline_window> c_window;
        // Position within `c_window`.  Move-only, fine here because
        // the cursor itself is heap-allocated and never copied.
        std::optional<logline_window::iterator> c_iter;
        // End sentinel for `c_window`.  Cached so the per-step
        // comparison doesn't rebuild the `logmsg_info` behind `end()`.
        std::optional<logline_window::iterator> c_end;
        // Fan-out iteration for the current lead.  The range yields
        // the lead itself first, then each suppressed sibling, so the
        // cursor just walks it as one sequence.
        std::optional<logline_window::logmsg_info::sibling_range> c_sib_range;
        std::optional<logline_window::logmsg_info::sibling_range::iterator>
            c_sib_iter;
        std::optional<logline_window::logmsg_info::sibling_range::iterator>
            c_sib_end;
        // Column within the current row's parsed values.
        size_t c_col_idx{0};
        sqlite3_int64 c_rowid{0};
        // The time range from the query's `log_time` constraints.  They are
        // only used to narrow the scan; SQLite still checks every row.
        std::optional<std::chrono::microseconds> c_time_lo;
        std::optional<std::chrono::microseconds> c_time_hi;
        // The metrics from the query's `metric = ?` or `metric IN (...)`
        // constraint, or empty for every metric.
        std::vector<intern_string_t> c_names;
        // Set when the time range is past the last row.
        bool c_done{false};

        explicit cursor(sqlite3_vtab* vt) { this->base.pVtab = vt; }

        int reset()
        {
            this->c_time_lo.reset();
            this->c_time_hi.reset();
            this->c_names.clear();

            return this->start();
        }

        int filter(int idxNum,
                   const char* idxStr,
                   int argc,
                   sqlite3_value** argv)
        {
            this->c_time_lo.reset();
            this->c_time_hi.reset();
            this->c_names.clear();

            auto raise_lo = [this](std::optional<std::chrono::microseconds> us) {
                if (us && (!this->c_time_lo || us.value() > this->c_time_lo)) {
                    this->c_time_lo = us;
                }
            };
            auto lower_hi = [this](std::optional<std::chrono::microseconds> us) {
                if (us && (!this->c_time_hi || us.value() < this->c_time_hi)) {
                    this->c_time_hi = us;
                }
            };
            const auto plan = std::string_view(idxStr != nullptr ? idxStr : "");
            auto by_name = false;
            for (int argi = 0; argi < argc && argi < (int) plan.size(); argi++) {
                auto* val = argv[argi];
                switch (plan[argi]) {
                    case PLAN_TIME_EQ:
                    case PLAN_USECS_EQ: {
                        const auto us = plan[argi] == PLAN_USECS_EQ
                            ? parse_usecs(val)
                            : parse_time(val);
                        raise_lo(us);
                        lower_hi(us);
                        break;
                    }
                    case PLAN_TIME_LO:
                        raise_lo(parse_time(val));
                        break;
                    case PLAN_USECS_LO:
                        raise_lo(parse_usecs(val));
                        break;
                    case PLAN_TIME_HI:
                        lower_hi(parse_time(val));
                        break;
                    case PLAN_USECS_HI:
                        lower_hi(parse_usecs(val));
                        break;
                    case PLAN_METRIC_EQ:
                        by_name = true;
                        this->add_name(val);
                        break;
                    case PLAN_METRIC_IN: {
                        by_name = true;
                        sqlite3_value* elem = nullptr;
                        for (auto rc = sqlite3_vtab_in_first(val, &elem);
                             rc == SQLITE_OK && elem != nullptr;
                             rc = sqlite3_vtab_in_next(val, &elem))
                        {
                            this->add_name(elem);
                        }
                        break;
                    }
                    default:
                        break;
                }
            }
            if (by_name && this->c_names.empty()) {
                // NULL or blob names, which match nothing.
                this->c_done = true;
                this->c_iter.reset();
                return SQLITE_OK;
            }

            return this->start();
        }

        int start()
        {
            this->c_lss = &lnav_data.ld_log_source;
            this->c_col_idx = 0;
            this->c_rowid = 0;
            this->c_done = false;
            // Iterators have to be dropped before the ranges they
            // point into; rebuilding the window invalidates both.
            this->c_sib_iter.reset();
            this->c_sib_end.reset();
            this->c_sib_range.reset();
            this->c_iter.reset();
            this->c_end.reset();
            auto start_vl = 0_vl;
            if (this->c_time_lo) {
                auto lo_vl = this->c_lss->find_from_time(
                    to_timeval(this->c_time_lo.value()));
                start_vl = lo_vl.value_or(
                    vis_line_t(this->c_lss->text_line_count()));
            }
            this->c_window = this->c_lss->window_to_end(start_vl);
            this->c_iter.emplace(this->c_window->begin());
            this->c_end.emplace(this->c_window->end());
            this->advance_past_invalid();
            return SQLITE_OK;
        }

        int next()
        {
            this->c_col_idx += 1;
            this->advance_past_invalid();
            this->c_rowid += 1;
            return SQLITE_OK;
        }

        int eof()
        {
            return this->c_done || !this->c_iter
                || *this->c_iter == *this->c_end;
        }

        int get_rowid(sqlite3_int64& rowid_out)
        {
            rowid_out = this->c_rowid;
            return SQLITE_OK;
        }

        void advance_past_invalid()
        {
            // Respect the LOG view's active filters so `all_metrics`
            // narrows alongside the view.
            const logline_window::logmsg_info::sibling_policy policy{
                /*skip_filtered_out=*/true,
            };
            while (*this->c_iter != *this->c_end) {
                const auto& lead_msg = **this->c_iter;
                if (!lead_msg.is_metric_line()) {
                    this->advance_to_next_vl();
                    continue;
                }
                // The LOG view is in time order, so the rows past the end of
                // the range are all past it.
                if (this->c_time_hi
                    && lead_msg.get_logline().get_time<>()
                        > this->c_time_hi.value())
                {
                    this->c_done = true;
                    return;
                }
                // Lazily build the fan-out range for this lead.  The
                // range yields the lead as the first element, so the
                // cursor doesn't need a special-case for offset 0.
                if (!this->c_sib_range) {
                    this->c_sib_range.emplace(lead_msg.metric_siblings(policy));
                    this->c_sib_iter.emplace(this->c_sib_range->begin());
                    this->c_sib_end.emplace(this->c_sib_range->end());
                }
                if (*this->c_sib_iter == *this->c_sib_end) {
                    this->advance_to_next_vl();
                    continue;
                }
                const auto& row = **this->c_sib_iter;
                auto* lf = row.get_file_ptr();
                auto line_iter = lf->begin() + row.get_file_line_number();
                if (line_iter->is_ignored()) {
                    ++*this->c_sib_iter;
                    this->c_col_idx = 0;
                    continue;
                }
                // Use the format's sample count rather than reading +
                // annotating the row here.  For queries that don't
                // touch `metric`/`value`/`raw_value`, the row's bytes
                // never get parsed.  `get_column` loads lazily via
                // `sibling_info::get_metric_values()` when a
                // value-dependent column is asked for.
                const auto* format = lf->get_format_ptr();
                if (!this->c_names.empty()) {
                    if (format->metrics_are_columns()) {
                        // Every row has every column, so look for the ones
                        // asked for.
                        const auto& samples = row.get_metric_values();
                        while (this->c_col_idx < samples.size()
                               && !this->wants(
                                   samples[this->c_col_idx].lv_meta.lvm_name))
                        {
                            this->c_col_idx += 1;
                        }
                        if (this->c_col_idx < samples.size()) {
                            return;
                        }
                    } else if (this->c_col_idx == 0
                               && this->may_have_sample(lf, line_iter))
                    {
                        return;
                    }
                    ++*this->c_sib_iter;
                    this->c_col_idx = 0;
                    continue;
                }
                if (!format->metrics_are_columns()) {
                    // A line missing the name or the value is not a sample.
                    if (this->c_col_idx == 0
                        && !row.get_metric_values().empty())
                    {
                        return;
                    }
                    ++*this->c_sib_iter;
                    this->c_col_idx = 0;
                    continue;
                }
                const auto col_count = format->get_metric_value_count();
                if (this->c_col_idx < col_count) {
                    return;
                }
                ++*this->c_sib_iter;
                this->c_col_idx = 0;
            }
        }

    private:
        // A bound on log_time_usecs, which SQLite compares as a number.
        static std::optional<std::chrono::microseconds> parse_usecs(
            sqlite3_value* val)
        {
            switch (sqlite3_value_numeric_type(val)) {
                case SQLITE_INTEGER:
                    return std::chrono::microseconds{sqlite3_value_int64(val)};
                case SQLITE_FLOAT: {
                    // Rounded down, which only lets the scan start or stop
                    // later than it has to; SQLite still checks every row.
                    const auto d = sqlite3_value_double(val);
                    if (!std::isfinite(d) || std::fabs(d) > 9.0e18) {
                        return std::nullopt;
                    }
                    return std::chrono::microseconds{
                        static_cast<int64_t>(std::floor(d))};
                }
                default:
                    return std::nullopt;
            }
        }

        static std::optional<std::chrono::microseconds> parse_time(
            sqlite3_value* val)
        {
            if (sqlite3_value_type(val) != SQLITE3_TEXT) {
                return std::nullopt;
            }
            const auto* datestr
                = reinterpret_cast<const char*>(sqlite3_value_text(val));
            const auto datelen = sqlite3_value_bytes(val);
            date_time_scanner dts;
            timeval tv;
            exttm tm;
            const auto* date_end = dts.scan(datestr, datelen, nullptr, &tm, tv);
            if (date_end != datestr + datelen) {
                return std::nullopt;
            }
            // SQLite compares log_time as text, which only orders the rows
            // the same as their times if the constraint is spelled like the
            // column, e.g. not with a space instead of the 'T'.
            char buf[64];
            const auto len = sql_strftime(buf, sizeof(buf), tv, 'T');
            if (datelen > len || std::string_view(buf, datelen) != datestr) {
                return std::nullopt;
            }
            return to_us(tv);
        }

        void add_name(sqlite3_value* val)
        {
            // `metric` has TEXT affinity, so SQLite compares a number with
            // it as text.  NULL and blobs never equal a name.
            switch (sqlite3_value_type(val)) {
                case SQLITE3_TEXT:
                case SQLITE_INTEGER:
                case SQLITE_FLOAT:
                    break;
                default:
                    return;
            }
            const auto name = std::string_view(
                reinterpret_cast<const char*>(sqlite3_value_text(val)),
                sqlite3_value_bytes(val));
            this->c_names.emplace_back(
                intern_string::lookup(name.data(), name.size()));
        }

        bool wants(intern_string_t name) const
        {
            for (const auto& wanted : this->c_names) {
                if (wanted == name) {
                    return true;
                }
            }
            return false;
        }

        // False only if the line can't have a sample of any of the metrics
        // asked for, without parsing it.  Lines that might are returned and
        // SQLite checks their metric.
        bool may_have_sample(logfile* lf, logfile::iterator line_iter) const
        {
            // The raw bytes, since read_line() of a JSON line renders it.
            auto read_res = lf->read_range(lf->get_file_range(line_iter, false));
            if (read_res.isErr()) {
                return true;
            }
            const auto sbr = read_res.unwrap();
            const auto text = std::string_view(sbr.get_data(), sbr.length());
            // A JSON string can spell any character with an escape, so a
            // line with one might have the name without it being in the
            // text as is.
            if (text.find('\\') != std::string_view::npos) {
                return true;
            }
            for (const auto& name : this->c_names) {
                if (text.find(name.to_string_fragment().to_string_view())
                    != std::string_view::npos)
                {
                    return true;
                }
            }
            return false;
        }

        // Step the outer `logline_window` iterator to the next visible
        // row and clear the fan-out state so a fresh sibling range
        // gets built for the new lead.
        void advance_to_next_vl()
        {
            ++*this->c_iter;
            this->c_sib_iter.reset();
            this->c_sib_end.reset();
            this->c_sib_range.reset();
            this->c_col_idx = 0;
        }
    };

    int best_index(sqlite3_vtab* tab, sqlite3_index_info* p_info)
    {
        // The constraints aren't omitted, SQLite checks them on every row,
        // since the scan only narrows by them.
        std::string plan;
        auto use = [&](int lpc, char step) {
            plan.push_back(step);
            p_info->aConstraintUsage[lpc].argvIndex = plan.size();
            p_info->aConstraintUsage[lpc].omit = 0;
        };
        auto has_eq = false;
        auto has_lo = false;
        auto has_hi = false;
        auto has_metric = false;

        for (int lpc = 0; lpc < p_info->nConstraint; lpc++) {
            const auto& cons = p_info->aConstraint[lpc];
            if (!cons.usable) {
                continue;
            }
            // A bound on either time column narrows the scan the same way.
            if (cons.iColumn == LOG_TIME_COL
                || cons.iColumn == LOG_TIME_USECS_COL)
            {
                const auto usecs = cons.iColumn == LOG_TIME_USECS_COL;
                switch (cons.op) {
                    case SQLITE_INDEX_CONSTRAINT_EQ:
                        use(lpc, usecs ? PLAN_USECS_EQ : PLAN_TIME_EQ);
                        has_eq = true;
                        break;
                    case SQLITE_INDEX_CONSTRAINT_GT:
                    case SQLITE_INDEX_CONSTRAINT_GE:
                        use(lpc, usecs ? PLAN_USECS_LO : PLAN_TIME_LO);
                        has_lo = true;
                        break;
                    case SQLITE_INDEX_CONSTRAINT_LT:
                    case SQLITE_INDEX_CONSTRAINT_LE:
                        use(lpc, usecs ? PLAN_USECS_HI : PLAN_TIME_HI);
                        has_hi = true;
                        break;
                    default:
                        break;
                }
            } else if (cons.iColumn == METRIC_COL
                       && cons.op == SQLITE_INDEX_CONSTRAINT_EQ && !has_metric)
            {
                // The scan narrows by exact names, so a comparison with
                // another collation (e.g. NOCASE) has to see every row.
                const auto* coll = sqlite3_vtab_collation(p_info, lpc);
                if (coll != nullptr && sqlite3_stricmp(coll, "BINARY") != 0) {
                    continue;
                }
                has_metric = true;
                if (sqlite3_vtab_in(p_info, lpc, -1)) {
                    sqlite3_vtab_in(p_info, lpc, 1);
                    use(lpc, PLAN_METRIC_IN);
                } else {
                    use(lpc, PLAN_METRIC_EQ);
                }
            }
        }

        double cost = 1000000.0;
        if (has_eq) {
            cost /= 10000.0;
        } else {
            if (has_lo) {
                cost /= 10.0;
            }
            if (has_hi) {
                cost /= 10.0;
            }
        }
        if (has_metric) {
            cost /= 10.0;
        }
        p_info->idxNum = 0;
        if (!plan.empty()) {
            p_info->idxStr = sqlite3_mprintf("%s", plan.c_str());
            p_info->needToFreeIdxStr = 1;
        }
        p_info->estimatedCost = cost;
        p_info->estimatedRows = static_cast<sqlite3_int64>(cost);

        return SQLITE_OK;
    }

    int get_column(const cursor& vc, sqlite3_context* ctx, int col)
    {
        if (!vc.c_sib_iter || *vc.c_sib_iter == *vc.c_sib_end) {
            sqlite3_result_null(ctx);
            return SQLITE_OK;
        }
        const auto& row = **vc.c_sib_iter;
        auto* lf = row.get_file_ptr();

        switch (col) {
            case 0:
                // log_line — every sibling fans out onto the lead's
                // visible row, which is simply our cursor's position.
                // Doesn't need the row's parsed values.
                sqlite3_result_int64(
                    ctx,
                    static_cast<sqlite3_int64>((*vc.c_iter)->get_vis_line()));
                break;
            case 1: {
                // log_time — from the logline's timestamp only.
                char buf[64];
                auto line_iter = lf->begin() + row.get_file_line_number();
                auto len = sql_strftime(
                    buf, sizeof(buf), line_iter->get_timeval(), 'T');
                sqlite3_result_text(ctx, buf, len, SQLITE_TRANSIENT);
                break;
            }
            case 2:
                to_sqlite(ctx, lf->get_filename());
                break;
            case LOG_TIME_USECS_COL: {
                auto line_iter = lf->begin() + row.get_file_line_number();
                sqlite3_result_int64(
                    ctx,
                    line_iter->get_time<std::chrono::microseconds>().count());
                break;
            }
            case 3:
                to_sqlite(ctx, lf->get_unique_path().stem());
                break;
            case 4:
            case 5:
            case 6: {
                // metric / value / raw_value — these actually need the
                // parsed cell, so pay for the read + annotate here.
                // `get_values` lazy-loads and caches, so subsequent
                // column reads on the same row share one parse.
                const auto& values = row.get_values();
                const auto& samples = row.get_metric_values();
                if (vc.c_col_idx >= samples.size()) {
                    sqlite3_result_null(ctx);
                    return SQLITE_OK;
                }
                const auto& lv = samples[vc.c_col_idx];
                if (col == 4) {
                    to_sqlite(ctx, lv.lv_meta.lvm_name);
                } else if (col == 5) {
                    if (lv.lv_meta.lvm_kind == value_kind_t::VALUE_INTEGER) {
                        sqlite3_result_int64(ctx, lv.lv_value.i);
                    } else if (lv.lv_meta.lvm_kind == value_kind_t::VALUE_FLOAT)
                    {
                        sqlite3_result_double(ctx, lv.lv_value.d);
                    } else if (lv.lv_meta.lvm_kind == value_kind_t::VALUE_TEXT)
                    {
                        to_sqlite(ctx, lv.text_value_fragment());
                    } else {
                        sqlite3_result_null(ctx);
                    }
                } else {
                    const auto& origin = lv.lv_origin;
                    const auto* sbr_data = values.lvv_sbr.get_data();
                    if (!origin.is_valid() || sbr_data == nullptr) {
                        sqlite3_result_null(ctx);
                    } else {
                        sqlite3_result_text(ctx,
                                            sbr_data + origin.lr_start,
                                            origin.length(),
                                            SQLITE_TRANSIENT);
                    }
                }
                break;
            }
            case 7: {
                // log_mark — fan-out in text_mark means any sibling at
                // this timestamp shares the marked state, so a direct
                // bookmark-set membership check is sufficient.
                auto& user_bms = vc.c_lss->get_user_bookmarks();
                sqlite3_result_int(ctx,
                                   user_bms[&textview_curses::BM_USER].contains(
                                       row.get_content_line())
                                       ? 1
                                       : 0);
                break;
            }
            case 8:
            case 9: {
                const auto& samples = row.get_metric_values();
                if (vc.c_col_idx >= samples.size()) {
                    sqlite3_result_null(ctx);
                    return SQLITE_OK;
                }
                const auto& lv = samples[vc.c_col_idx];
                const auto counter = !lf->get_format_ptr()->metrics_are_columns()
                    && lf->get_metrics().is_counter(lv.lv_meta.lvm_name);
                if (col == 8) {
                    sqlite3_result_text(
                        ctx, counter ? "counter" : "gauge", -1, SQLITE_STATIC);
                } else if (!counter) {
                    sqlite3_result_null(ctx);
                } else {
                    auto shown = lf->metric_sample_as_shown(
                        row.get_file_line_number(), lv);
                    if (shown) {
                        sqlite3_result_double(ctx, shown->lv_value.d);
                    } else {
                        sqlite3_result_null(ctx);
                    }
                }
                break;
            }
            case UNIT_SUFFIX_COL:
            case UNIT_DIVISOR_COL: {
                // The unit the overlay and the line show the sample with,
                // which the format's display entries can give it.
                const auto& samples = row.get_metric_values();
                if (vc.c_col_idx >= samples.size()) {
                    sqlite3_result_null(ctx);
                    return SQLITE_OK;
                }
                const auto& meta = samples[vc.c_col_idx].lv_meta;
                if (col == UNIT_SUFFIX_COL) {
                    if (meta.lvm_unit_suffix.empty()) {
                        sqlite3_result_null(ctx);
                    } else {
                        to_sqlite(ctx, meta.lvm_unit_suffix);
                    }
                } else {
                    sqlite3_result_double(ctx,
                                          meta.lvm_unit_divisor == 0.0
                                              ? 1.0
                                              : meta.lvm_unit_divisor);
                }
                break;
            }
            default:
                sqlite3_result_null(ctx);
                break;
        }
        return SQLITE_OK;
    }
};

}  // namespace

static auto metrics_binder
    = injector::bind_multiple<vtab_module_base>()
          .add<vtab_module<tvt_no_update<metrics_vtab>>>();
