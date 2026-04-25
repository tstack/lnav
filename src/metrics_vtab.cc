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

#include <optional>

#include "base/injector.bind.hh"
#include "base/lnav_log.hh"
#include "lnav.hh"
#include "logfile.hh"
#include "logfile_sub_source.hh"
#include "logline_window.hh"
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
    static constexpr const char* CREATE_STMT = R"(
-- A long-format view over every metrics_log file lnav currently has open.
-- Each (file, row, column) combination produces one virtual row, so
-- queries can filter by metric name without knowing which CSV file
-- carried the value.
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
    log_mark INTEGER HIDDEN   -- True if the sample's row is user-marked.
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

        explicit cursor(sqlite3_vtab* vt) { this->base.pVtab = vt; }

        int reset()
        {
            this->c_lss = &lnav_data.ld_log_source;
            this->c_col_idx = 0;
            this->c_rowid = 0;
            // Iterators have to be dropped before the ranges they
            // point into; rebuilding the window invalidates both.
            this->c_sib_iter.reset();
            this->c_sib_end.reset();
            this->c_sib_range.reset();
            this->c_iter.reset();
            this->c_end.reset();
            this->c_window = this->c_lss->window_to_end(0_vl);
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

        int eof() { return !this->c_iter || *this->c_iter == *this->c_end; }

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
                // Use the format's declared column count rather than
                // reading + annotating the row here.  For queries
                // that don't touch `metric`/`value`/`raw_value`, the
                // row's bytes never get parsed.  `get_column` loads
                // lazily via `sibling_info::get_values()` when a
                // value-dependent column is asked for.
                const auto col_count
                    = lf->get_format_ptr()->get_value_metadata_count();
                if (this->c_col_idx < col_count) {
                    return;
                }
                ++*this->c_sib_iter;
                this->c_col_idx = 0;
            }
        }

    private:
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
                if (vc.c_col_idx >= values.lvv_values.size()) {
                    sqlite3_result_null(ctx);
                    return SQLITE_OK;
                }
                const auto& lv = values.lvv_values[vc.c_col_idx];
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
