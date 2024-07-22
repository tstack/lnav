/**
 * Copyright (c) 2007-2012, Timothy Stack
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

#include "log_vtab_impl.hh"

#include "base/ansi_scrubber.hh"
#include "base/itertools.hh"
#include "base/lnav_log.hh"
#include "base/string_util.hh"
#include "bookmarks.json.hh"
#include "config.h"
#include "hasher.hh"
#include "lnav_util.hh"
#include "logfile_sub_source.hh"
#include "sql_util.hh"
#include "vtab_module.hh"
#include "vtab_module_json.hh"
#include "yajlpp/json_op.hh"
#include "yajlpp/yajlpp_def.hh"

// #define DEBUG_INDEXING 1

using namespace lnav::roles::literals;

static auto intern_lifetime = intern_string::get_table_lifetime();

static struct log_cursor log_cursor_latest;

thread_local _log_vtab_data log_vtab_data;

static const char* LOG_COLUMNS = R"(  (
  log_line        INTEGER,                         -- The line number for the log message
  log_time        DATETIME,                        -- The adjusted timestamp for the log message
  log_level       TEXT     COLLATE loglevel,       -- The log message level
  -- BEGIN Format-specific fields:
)";

static const char* LOG_FOOTER_COLUMNS = R"(
  -- END Format-specific fields
  log_part         TEXT     COLLATE naturalnocase,    -- The partition the message is in
  log_actual_time  DATETIME HIDDEN,                   -- The timestamp from the original log file for this message
  log_idle_msecs   INTEGER,                           -- The difference in time between this messages and the previous
  log_mark         BOOLEAN,                           -- True if the log message was marked
  log_comment      TEXT,                              -- The comment for this message
  log_tags         TEXT,                              -- A JSON list of tags for this message
  log_annotations  TEXT,                              -- A JSON object of annotations for this messages
  log_filters      TEXT,                              -- A JSON list of filter IDs that matched this message
  log_opid         TEXT HIDDEN,                       -- The message's OPID from the log message or user
  log_user_opid    TEXT HIDDEN,                       -- The message's OPID as set by the user
  log_format       TEXT HIDDEN,                       -- The name of the log file format
  log_format_regex TEXT HIDDEN,                       -- The name of the regex used to parse this log message
  log_time_msecs   INTEGER HIDDEN,                    -- The adjusted timestamp for the log message as the number of milliseconds from the epoch
  log_path         TEXT HIDDEN COLLATE naturalnocase, -- The path to the log file this message is from
  log_unique_path  TEXT HIDDEN COLLATE naturalnocase, -- The unique portion of the path this message is from
  log_text         TEXT HIDDEN,                       -- The full text of the log message
  log_body         TEXT HIDDEN,                       -- The body of the log message
  log_raw_text     TEXT HIDDEN,                       -- The raw text from the log file
  log_line_hash    TEXT HIDDEN                        -- A hash of the first line of the log message
)";

enum class log_footer_columns : uint32_t {
    partition,
    actual_time,
    idle_msecs,
    mark,
    comment,
    tags,
    annotations,
    filters,
    opid,
    user_opid,
    format,
    format_regex,
    time_msecs,
    path,
    unique_path,
    text,
    body,
    raw_text,
    line_hash,
};

std::string
log_vtab_impl::get_table_statement()
{
    std::vector<log_vtab_impl::vtab_column> cols;
    std::vector<log_vtab_impl::vtab_column>::const_iterator iter;
    std::ostringstream oss;
    size_t max_name_len = 15;

    oss << "CREATE TABLE " << this->get_name().to_string() << LOG_COLUMNS;
    this->get_columns(cols);
    this->vi_column_count = cols.size();
    for (iter = cols.begin(); iter != cols.end(); iter++) {
        max_name_len = std::max(max_name_len, iter->vc_name.length());
    }
    for (iter = cols.begin(); iter != cols.end(); iter++) {
        std::string comment;

        require(!iter->vc_name.empty());

        if (!iter->vc_comment.empty()) {
            comment.append(" -- ").append(iter->vc_comment);
        }

        auto colname = sql_quote_ident(iter->vc_name.c_str());
        auto coldecl = lnav::sql::mprintf(
            "  %-*s %-7s %s COLLATE %-15Q,%s\n",
            max_name_len,
            colname.in(),
            sqlite3_type_to_string(iter->vc_type),
            iter->vc_hidden ? "hidden" : "",
            iter->vc_collator.empty() ? "BINARY" : iter->vc_collator.c_str(),
            comment.c_str());
        oss << coldecl;
    }
    oss << LOG_FOOTER_COLUMNS;

    {
        std::vector<std::string> primary_keys;

        this->get_primary_keys(primary_keys);
        if (!primary_keys.empty()) {
            auto first = true;

            oss << ", PRIMARY KEY (";
            for (const auto& pkey : primary_keys) {
                if (!first) {
                    oss << ", ";
                }
                oss << pkey;
                first = false;
            }
            oss << ")\n";
        } else {
            oss << ", PRIMARY KEY (log_line)\n";
        }
    }

    oss << ");\n";

    log_trace("log_vtab_impl.get_table_statement() -> %s", oss.str().c_str());

    return oss.str();
}

std::pair<int, unsigned int>
log_vtab_impl::logline_value_to_sqlite_type(value_kind_t kind)
{
    int type = 0;
    unsigned int subtype = 0;

    switch (kind) {
        case value_kind_t::VALUE_JSON:
            type = SQLITE3_TEXT;
            subtype = JSON_SUBTYPE;
            break;
        case value_kind_t::VALUE_NULL:
        case value_kind_t::VALUE_TEXT:
        case value_kind_t::VALUE_STRUCT:
        case value_kind_t::VALUE_QUOTED:
        case value_kind_t::VALUE_W3C_QUOTED:
        case value_kind_t::VALUE_TIMESTAMP:
        case value_kind_t::VALUE_XML:
            type = SQLITE3_TEXT;
            break;
        case value_kind_t::VALUE_FLOAT:
            type = SQLITE_FLOAT;
            break;
        case value_kind_t::VALUE_BOOLEAN:
        case value_kind_t::VALUE_INTEGER:
            type = SQLITE_INTEGER;
            break;
        case value_kind_t::VALUE_UNKNOWN:
        case value_kind_t::VALUE__MAX:
            ensure(0);
            break;
    }
    return std::make_pair(type, subtype);
}

void
log_vtab_impl::get_foreign_keys(std::vector<std::string>& keys_inout) const
{
    keys_inout.emplace_back("id");
    keys_inout.emplace_back("parent");
    keys_inout.emplace_back("notused");

    keys_inout.emplace_back("log_line");
    keys_inout.emplace_back("min(log_line)");
    keys_inout.emplace_back("log_mark");
    keys_inout.emplace_back("log_time_msecs");
    keys_inout.emplace_back("log_top_line()");
    keys_inout.emplace_back("log_msg_line()");
}

void
log_vtab_impl::extract(logfile* lf,
                       uint64_t line_number,
                       logline_value_vector& values)
{
    auto format = lf->get_format();

    this->vi_attrs.clear();
    format->annotate(lf, line_number, this->vi_attrs, values, false);
}

bool
log_vtab_impl::is_valid(log_cursor& lc, logfile_sub_source& lss)
{
    content_line_t cl(lss.at(lc.lc_curr_line));
    auto* lf = lss.find_file_ptr(cl);
    auto lf_iter = lf->begin() + cl;

    if (!lf_iter->is_message()) {
        return false;
    }

    if (!lc.lc_format_name.empty()
        && lc.lc_format_name != lf->get_format_name())
    {
        return false;
    }

    if (!lc.lc_pattern_name.empty()
        && lc.lc_pattern_name != lf->get_format_ptr()->get_pattern_name(cl))
    {
        return false;
    }

    if (lc.lc_level_constraint
        && !lc.lc_level_constraint->matches(lf_iter->get_msg_level()))
    {
        return false;
    }

    if (!lc.lc_log_path.empty()) {
        if (lf == lc.lc_last_log_path_match) {
        } else if (lf == lc.lc_last_log_path_mismatch) {
            return false;
        } else {
            for (const auto& path_cons : lc.lc_log_path) {
                if (!path_cons.matches(lf->get_filename())) {
                    lc.lc_last_log_path_mismatch = lf;
                    return false;
                }
            }
            lc.lc_last_log_path_match = lf;
        }
    }

    if (!lc.lc_unique_path.empty()) {
        if (lf == lc.lc_last_unique_path_match) {
        } else if (lf == lc.lc_last_unique_path_mismatch) {
            return false;
        } else {
            for (const auto& path_cons : lc.lc_unique_path) {
                if (!path_cons.matches(lf->get_unique_path())) {
                    lc.lc_last_unique_path_mismatch = lf;
                    return false;
                }
            }
            lc.lc_last_unique_path_match = lf;
        }
    }

    if (lc.lc_opid && lf_iter->get_opid() != lc.lc_opid.value().value) {
        return false;
    }

    return true;
}

struct log_vtab {
    sqlite3_vtab base;
    sqlite3* db;
    textview_curses* tc{nullptr};
    logfile_sub_source* lss{nullptr};
    std::shared_ptr<log_vtab_impl> vi;

    size_t footer_index(log_footer_columns col) const
    {
        return VT_COL_MAX + this->vi->vi_column_count
            + lnav::enums::to_underlying(col);
    }
};

struct vtab_cursor {
    void cache_msg(logfile* lf, logfile::const_iterator ll)
    {
        if (this->log_msg_line == this->log_cursor.lc_curr_line) {
            return;
        }
        auto& sbr = this->line_values.lvv_sbr;
        lf->read_full_message(ll, sbr);
        sbr.erase_ansi();
        this->log_msg_line = this->log_cursor.lc_curr_line;
    }

    void invalidate()
    {
        this->line_values.clear();
        this->log_msg_line = -1_vl;
    }

    sqlite3_vtab_cursor base;
    struct log_cursor log_cursor;
    vis_line_t log_msg_line{-1_vl};
    logline_value_vector line_values;
};

static int vt_destructor(sqlite3_vtab* p_svt);

static int
vt_create(sqlite3* db,
          void* pAux,
          int argc,
          const char* const* argv,
          sqlite3_vtab** pp_vt,
          char** pzErr)
{
    auto* vm = (log_vtab_manager*) pAux;
    int rc = SQLITE_OK;
    /* Allocate the sqlite3_vtab/vtab structure itself */
    auto p_vt = std::make_unique<log_vtab>();

    p_vt->db = db;

    /* Declare the vtable's structure */
    p_vt->vi = vm->lookup_impl(intern_string::lookup(argv[3]));
    if (p_vt->vi == nullptr) {
        return SQLITE_ERROR;
    }
    p_vt->tc = vm->get_view();
    p_vt->lss = vm->get_source();
    rc = sqlite3_declare_vtab(db, p_vt->vi->get_table_statement().c_str());

    /* Success. Set *pp_vt and return */
    auto loose_p_vt = p_vt.release();
    *pp_vt = &loose_p_vt->base;

    log_debug("creating log format table: %s = %p", argv[3], p_vt.get());

    return rc;
}

static int
vt_destructor(sqlite3_vtab* p_svt)
{
    log_vtab* p_vt = (log_vtab*) p_svt;

    delete p_vt;

    return SQLITE_OK;
}

static int
vt_connect(sqlite3* db,
           void* p_aux,
           int argc,
           const char* const* argv,
           sqlite3_vtab** pp_vt,
           char** pzErr)
{
    return vt_create(db, p_aux, argc, argv, pp_vt, pzErr);
}

static int
vt_disconnect(sqlite3_vtab* pVtab)
{
    return vt_destructor(pVtab);
}

static int
vt_destroy(sqlite3_vtab* p_vt)
{
    return vt_destructor(p_vt);
}

static int vt_next(sqlite3_vtab_cursor* cur);

static int
vt_open(sqlite3_vtab* p_svt, sqlite3_vtab_cursor** pp_cursor)
{
    log_vtab* p_vt = (log_vtab*) p_svt;

    p_vt->base.zErrMsg = nullptr;

    vtab_cursor* p_cur = new vtab_cursor();

    *pp_cursor = (sqlite3_vtab_cursor*) p_cur;

    p_cur->base.pVtab = p_svt;
    p_cur->log_cursor.lc_opid = std::nullopt;
    p_cur->log_cursor.lc_curr_line = 0_vl;
    p_cur->log_cursor.lc_end_line = vis_line_t(p_vt->lss->text_line_count());
    p_cur->log_cursor.lc_sub_index = 0;

    for (auto& ld : *p_vt->lss) {
        auto* lf = ld->get_file_ptr();

        if (lf == nullptr) {
            continue;
        }

        lf->enable_cache();
    }

    return SQLITE_OK;
}

static int
vt_close(sqlite3_vtab_cursor* cur)
{
    auto* p_cur = (vtab_cursor*) cur;

    /* Free cursor struct. */
    delete p_cur;

    return SQLITE_OK;
}

static int
vt_eof(sqlite3_vtab_cursor* cur)
{
    auto* vc = (vtab_cursor*) cur;

    return vc->log_cursor.is_eof();
}

static void
populate_indexed_columns(vtab_cursor* vc, log_vtab* vt)
{
    if (vc->log_cursor.is_eof() || vc->log_cursor.lc_indexed_columns.empty()) {
        return;
    }

    logfile* lf = nullptr;

    for (const auto& ic : vc->log_cursor.lc_indexed_columns) {
        auto& ci = vt->vi->vi_column_indexes[ic.cc_column];

        if (vc->log_cursor.lc_curr_line < ci.ci_max_line) {
            continue;
        }

        if (lf == nullptr) {
            content_line_t cl(vt->lss->at(vc->log_cursor.lc_curr_line));
            uint64_t line_number;
            auto ld = vt->lss->find_data(cl, line_number);
            lf = (*ld)->get_file_ptr();
            auto ll = lf->begin() + line_number;

            vc->cache_msg(lf, ll);
            require(vc->line_values.lvv_sbr.get_data() != nullptr);
            vt->vi->extract(lf, line_number, vc->line_values);
        }

        auto sub_col = logline_value_meta::table_column{
            (size_t) (ic.cc_column - VT_COL_MAX)};
        auto lv_iter = find_if(vc->line_values.lvv_values.begin(),
                               vc->line_values.lvv_values.end(),
                               logline_value_cmp(nullptr, sub_col));
        if (lv_iter == vc->line_values.lvv_values.end()
            || lv_iter->lv_meta.lvm_kind == value_kind_t::VALUE_NULL)
        {
            continue;
        }

        auto value = lv_iter->to_string();

#ifdef DEBUG_INDEXING
        log_debug("updated index for column %d %s -> %d",
                  ic.cc_column,
                  value.c_str(),
                  (int) vc->log_cursor.lc_curr_line);
#endif

        if (ci.ci_value_to_lines[value].empty()
            || ci.ci_value_to_lines[value].back()
                != vc->log_cursor.lc_curr_line)
        {
            ci.ci_value_to_lines[value].push_back(vc->log_cursor.lc_curr_line);
        }
    }
}

static int
vt_next(sqlite3_vtab_cursor* cur)
{
    auto* vc = (vtab_cursor*) cur;
    auto* vt = (log_vtab*) cur->pVtab;
    auto done = false;

    vc->invalidate();
    if (!vc->log_cursor.lc_indexed_lines.empty()) {
        vc->log_cursor.lc_curr_line = vc->log_cursor.lc_indexed_lines.back();
        vc->log_cursor.lc_indexed_lines.pop_back();
    } else {
        vc->log_cursor.lc_curr_line += 1_vl;
    }
    vc->log_cursor.lc_sub_index = 0;
    do {
        log_cursor_latest = vc->log_cursor;
        if (((log_cursor_latest.lc_curr_line % 1024) == 0)
            && (log_vtab_data.lvd_progress != nullptr
                && log_vtab_data.lvd_progress(log_cursor_latest)))
        {
            break;
        }

        while (vc->log_cursor.lc_curr_line != -1_vl && !vc->log_cursor.is_eof()
               && !vt->vi->is_valid(vc->log_cursor, *vt->lss))
        {
            vc->log_cursor.lc_curr_line += 1_vl;
            vc->log_cursor.lc_sub_index = 0;
        }
        if (vc->log_cursor.is_eof()) {
            done = true;
        } else {
            done = vt->vi->next(vc->log_cursor, *vt->lss);
            if (done) {
                populate_indexed_columns(vc, vt);
            } else {
                if (!vc->log_cursor.lc_indexed_lines.empty()) {
                    vc->log_cursor.lc_curr_line
                        = vc->log_cursor.lc_indexed_lines.back();
                    vc->log_cursor.lc_indexed_lines.pop_back();
                } else {
                    vc->log_cursor.lc_curr_line += 1_vl;
                }
                vc->log_cursor.lc_sub_index = 0;
            }
        }
    } while (!done);

    return SQLITE_OK;
}

static int
vt_next_no_rowid(sqlite3_vtab_cursor* cur)
{
    auto* vc = (vtab_cursor*) cur;
    auto* vt = (log_vtab*) cur->pVtab;
    auto done = false;

    vc->invalidate();
    do {
        log_cursor_latest = vc->log_cursor;
        if (((log_cursor_latest.lc_curr_line % 1024) == 0)
            && (log_vtab_data.lvd_progress != nullptr
                && log_vtab_data.lvd_progress(log_cursor_latest)))
        {
            break;
        }

        done = vt->vi->next(vc->log_cursor, *vt->lss);
        if (done) {
            populate_indexed_columns(vc, vt);
        } else if (vc->log_cursor.is_eof()) {
            done = true;
        } else {
            require(vc->log_cursor.lc_curr_line < vt->lss->text_line_count());

            if (!vc->log_cursor.lc_indexed_lines.empty()) {
                vc->log_cursor.lc_curr_line
                    = vc->log_cursor.lc_indexed_lines.back();
                vc->log_cursor.lc_indexed_lines.pop_back();
#ifdef DEBUG_INDEXING
                log_debug("going to next line from index %d",
                          (int) vc->log_cursor.lc_curr_line);
#endif
            } else {
                vc->log_cursor.lc_curr_line += 1_vl;
            }
            vc->log_cursor.lc_sub_index = 0;
            for (auto& col_constraint : vc->log_cursor.lc_indexed_columns) {
                vt->vi->vi_column_indexes[col_constraint.cc_column].ci_max_line
                    = std::max(
                        vt->vi->vi_column_indexes[col_constraint.cc_column]
                            .ci_max_line,
                        vc->log_cursor.lc_curr_line);
            }
        }
    } while (!done);

#ifdef DEBUG_INDEXING
    log_debug("vt_next_no_rowid() -> %d:%d",
              vc->log_cursor.lc_curr_line,
              vc->log_cursor.lc_end_line);
#endif

    return SQLITE_OK;
}

static int
vt_column(sqlite3_vtab_cursor* cur, sqlite3_context* ctx, int col)
{
    auto* vc = (vtab_cursor*) cur;
    auto* vt = (log_vtab*) cur->pVtab;

#ifdef DEBUG_INDEXING
    log_debug("vt_column(%s, %d:%d)",
              vt->vi->get_name().get(),
              (int) vc->log_cursor.lc_curr_line,
              col);
#endif

    content_line_t cl(vt->lss->at(vc->log_cursor.lc_curr_line));
    uint64_t line_number;
    auto ld = vt->lss->find_data(cl, line_number);
    auto* lf = (*ld)->get_file_ptr();
    auto ll = lf->begin() + line_number;

    require(col >= 0);

    /* Just return the ordinal of the column requested. */
    switch (col) {
        case VT_COL_LINE_NUMBER: {
            sqlite3_result_int64(ctx, vc->log_cursor.lc_curr_line);
            break;
        }

        case VT_COL_LOG_TIME: {
            char buffer[64];

            sql_strftime(
                buffer, sizeof(buffer), ll->get_time(), ll->get_millis());
            sqlite3_result_text(ctx, buffer, strlen(buffer), SQLITE_TRANSIENT);
            break;
        }

        case VT_COL_LEVEL: {
            const char* level_name = ll->get_level_name();

            sqlite3_result_text(
                ctx, level_name, strlen(level_name), SQLITE_STATIC);
            break;
        }

        default:
            if (col > (VT_COL_MAX + vt->vi->vi_column_count - 1)) {
                auto footer_column = static_cast<log_footer_columns>(
                    col - (VT_COL_MAX + vt->vi->vi_column_count - 1) - 1);

                switch (footer_column) {
                    case log_footer_columns::partition: {
                        auto& vb = vt->tc->get_bookmarks();
                        const auto& bv = vb[&textview_curses::BM_PARTITION];

                        if (bv.empty()) {
                            sqlite3_result_null(ctx);
                        } else {
                            vis_line_t curr_line(vc->log_cursor.lc_curr_line);
                            auto iter = lower_bound(
                                bv.begin(), bv.end(), curr_line + 1_vl);

                            if (iter != bv.begin()) {
                                --iter;
                                auto line_meta_opt
                                    = vt->lss->find_bookmark_metadata(*iter);
                                if (line_meta_opt
                                    && !line_meta_opt.value()->bm_name.empty())
                                {
                                    sqlite3_result_text(
                                        ctx,
                                        line_meta_opt.value()->bm_name.c_str(),
                                        line_meta_opt.value()->bm_name.size(),
                                        SQLITE_TRANSIENT);
                                } else {
                                    sqlite3_result_null(ctx);
                                }
                            } else {
                                sqlite3_result_null(ctx);
                            }
                        }
                        break;
                    }
                    case log_footer_columns::actual_time: {
                        char buffer[64] = "";

                        if (ll->is_time_skewed()) {
                            if (vc->line_values.lvv_values.empty()) {
                                vc->cache_msg(lf, ll);
                                require(vc->line_values.lvv_sbr.get_data()
                                        != nullptr);
                                vt->vi->extract(
                                    lf, line_number, vc->line_values);
                            }

                            struct line_range time_range;

                            time_range = find_string_attr_range(
                                vt->vi->vi_attrs, &logline::L_TIMESTAMP);

                            const auto* time_src
                                = vc->line_values.lvv_sbr.get_data()
                                + time_range.lr_start;
                            struct timeval actual_tv;
                            struct exttm tm;

                            if (lf->get_format()->lf_date_time.scan(
                                    time_src,
                                    time_range.length(),
                                    lf->get_format()->get_timestamp_formats(),
                                    &tm,
                                    actual_tv,
                                    false))
                            {
                                sql_strftime(buffer, sizeof(buffer), actual_tv);
                            }
                        } else {
                            sql_strftime(buffer,
                                         sizeof(buffer),
                                         ll->get_time(),
                                         ll->get_millis());
                        }
                        sqlite3_result_text(
                            ctx, buffer, strlen(buffer), SQLITE_TRANSIENT);
                        break;
                    }
                    case log_footer_columns::idle_msecs: {
                        if (vc->log_cursor.lc_curr_line == 0) {
                            sqlite3_result_int64(ctx, 0);
                        } else {
                            content_line_t prev_cl(vt->lss->at(
                                vis_line_t(vc->log_cursor.lc_curr_line - 1)));
                            auto prev_lf = vt->lss->find(prev_cl);
                            auto prev_ll = prev_lf->begin() + prev_cl;
                            uint64_t prev_time, curr_line_time;

                            prev_time = prev_ll->get_time() * 1000ULL;
                            prev_time += prev_ll->get_millis();
                            curr_line_time = ll->get_time() * 1000ULL;
                            curr_line_time += ll->get_millis();
                            // require(curr_line_time >= prev_time);
                            sqlite3_result_int64(ctx,
                                                 curr_line_time - prev_time);
                        }
                        break;
                    }
                    case log_footer_columns::mark: {
                        sqlite3_result_int(ctx, ll->is_marked());
                        break;
                    }
                    case log_footer_columns::comment: {
                        auto line_meta_opt = vt->lss->find_bookmark_metadata(
                            vc->log_cursor.lc_curr_line);
                        if (!line_meta_opt
                            || line_meta_opt.value()->bm_comment.empty())
                        {
                            sqlite3_result_null(ctx);
                        } else {
                            const auto& meta = *(line_meta_opt.value());
                            sqlite3_result_text(ctx,
                                                meta.bm_comment.c_str(),
                                                meta.bm_comment.length(),
                                                SQLITE_TRANSIENT);
                        }
                        break;
                    }
                    case log_footer_columns::tags: {
                        auto line_meta_opt = vt->lss->find_bookmark_metadata(
                            vc->log_cursor.lc_curr_line);
                        if (!line_meta_opt
                            || line_meta_opt.value()->bm_tags.empty())
                        {
                            sqlite3_result_null(ctx);
                        } else {
                            const auto& meta = *(line_meta_opt.value());

                            yajlpp_gen gen;

                            yajl_gen_config(gen, yajl_gen_beautify, false);

                            {
                                yajlpp_array arr(gen);

                                for (const auto& str : meta.bm_tags) {
                                    arr.gen(str);
                                }
                            }

                            to_sqlite(ctx, json_string(gen));
                        }
                        break;
                    }
                    case log_footer_columns::annotations: {
                        if (sqlite3_vtab_nochange(ctx)) {
                            return SQLITE_OK;
                        }

                        auto line_meta_opt = vt->lss->find_bookmark_metadata(
                            vc->log_cursor.lc_curr_line);
                        if (!line_meta_opt
                            || line_meta_opt.value()
                                   ->bm_annotations.la_pairs.empty())
                        {
                            sqlite3_result_null(ctx);
                        } else {
                            const auto& meta = *(line_meta_opt.value());
                            to_sqlite(
                                ctx,
                                logmsg_annotations_handlers.to_json_string(
                                    meta.bm_annotations));
                        }
                        break;
                    }
                    case log_footer_columns::filters: {
                        const auto& filter_mask
                            = (*ld)->ld_filter_state.lfo_filter_state.tfs_mask;

                        if (!filter_mask[line_number]) {
                            sqlite3_result_null(ctx);
                        } else {
                            const auto& filters = vt->lss->get_filters();
                            yajlpp_gen gen;

                            yajl_gen_config(gen, yajl_gen_beautify, false);

                            {
                                yajlpp_array arr(gen);

                                for (const auto& filter : filters) {
                                    if (filter->lf_deleted) {
                                        continue;
                                    }

                                    uint32_t mask
                                        = (1UL << filter->get_index());

                                    if (filter_mask[line_number] & mask) {
                                        arr.gen(filter->get_index());
                                    }
                                }
                            }

                            to_sqlite(ctx, gen.to_string_fragment());
                            sqlite3_result_subtype(ctx, JSON_SUBTYPE);
                        }
                        break;
                    }
                    case log_footer_columns::opid: {
                        if (vc->line_values.lvv_values.empty()) {
                            vc->cache_msg(lf, ll);
                            require(vc->line_values.lvv_sbr.get_data()
                                    != nullptr);
                            vt->vi->extract(lf, line_number, vc->line_values);
                        }

                        if (vc->line_values.lvv_opid_value) {
                            to_sqlite(ctx,
                                      vc->line_values.lvv_opid_value.value());
                        } else {
                            sqlite3_result_null(ctx);
                        }
                        break;
                    }
                    case log_footer_columns::user_opid: {
                        if (vc->line_values.lvv_values.empty()) {
                            vc->cache_msg(lf, ll);
                            require(vc->line_values.lvv_sbr.get_data()
                                    != nullptr);
                            vt->vi->extract(lf, line_number, vc->line_values);
                        }

                        if (vc->line_values.lvv_opid_value
                            && vc->line_values.lvv_opid_provenance
                                == logline_value_vector::opid_provenance::user)
                        {
                            to_sqlite(ctx,
                                      vc->line_values.lvv_opid_value.value());
                        } else {
                            sqlite3_result_null(ctx);
                        }
                        break;
                    }
                    case log_footer_columns::format: {
                        auto format_name = lf->get_format_name();
                        sqlite3_result_text(ctx,
                                            format_name.get(),
                                            format_name.size(),
                                            SQLITE_STATIC);
                        break;
                    }
                    case log_footer_columns::format_regex: {
                        auto pat_name
                            = lf->get_format()->get_pattern_name(line_number);
                        sqlite3_result_text(ctx,
                                            pat_name.get(),
                                            pat_name.size(),
                                            SQLITE_STATIC);
                        break;
                    }
                    case log_footer_columns::time_msecs: {
                        sqlite3_result_int64(ctx, ll->get_time_in_millis());
                        break;
                    }
                    case log_footer_columns::path: {
                        const auto& fn = lf->get_filename();

                        sqlite3_result_text(ctx,
                                            fn.c_str(),
                                            fn.native().length(),
                                            SQLITE_STATIC);
                        break;
                    }
                    case log_footer_columns::unique_path: {
                        const auto& fn = lf->get_unique_path();

                        sqlite3_result_text(ctx,
                                            fn.c_str(),
                                            fn.native().length(),
                                            SQLITE_STATIC);
                        break;
                    }
                    case log_footer_columns::text: {
                        shared_buffer_ref line;

                        lf->read_full_message(ll, line);
                        line.erase_ansi();
                        sqlite3_result_text(ctx,
                                            line.get_data(),
                                            line.length(),
                                            SQLITE_TRANSIENT);
                        break;
                    }
                    case log_footer_columns::body: {
                        if (vc->line_values.lvv_values.empty()) {
                            vc->cache_msg(lf, ll);
                            require(vc->line_values.lvv_sbr.get_data()
                                    != nullptr);
                            vt->vi->extract(lf, line_number, vc->line_values);
                        }

                        struct line_range body_range;

                        body_range = find_string_attr_range(vt->vi->vi_attrs,
                                                            &SA_BODY);
                        if (!body_range.is_valid()) {
                            sqlite3_result_null(ctx);
                        } else {
                            const char* msg_start
                                = vc->line_values.lvv_sbr.get_data();

                            sqlite3_result_text(ctx,
                                                &msg_start[body_range.lr_start],
                                                body_range.length(),
                                                SQLITE_TRANSIENT);
                        }
                        break;
                    }
                    case log_footer_columns::raw_text: {
                        auto read_res = lf->read_raw_message(ll);

                        if (read_res.isErr()) {
                            auto msg = fmt::format(
                                FMT_STRING("unable to read line -- {}"),
                                read_res.unwrapErr());
                            sqlite3_result_error(
                                ctx, msg.c_str(), msg.length());
                        } else {
                            auto sbr = read_res.unwrap();

                            sqlite3_result_text(ctx,
                                                sbr.get_data(),
                                                sbr.length(),
                                                SQLITE_TRANSIENT);
                        }
                        break;
                    }
                    case log_footer_columns::line_hash: {
                        auto read_res = lf->read_line(ll);

                        if (read_res.isErr()) {
                            auto msg = fmt::format(
                                FMT_STRING("unable to read line -- {}"),
                                read_res.unwrapErr());
                            sqlite3_result_error(
                                ctx, msg.c_str(), msg.length());
                        } else {
                            auto sbr = read_res.unwrap();
                            hasher line_hasher;

                            auto outbuf
                                = auto_buffer::alloc(3 + hasher::STRING_SIZE);
                            outbuf.push_back('v');
                            outbuf.push_back('1');
                            outbuf.push_back(':');
                            line_hasher.update(sbr.get_data(), sbr.length())
                                .update(cl)
                                .to_string(outbuf);
                            to_sqlite(ctx, text_auto_buffer{std::move(outbuf)});
                        }
                        break;
                    }
                }
            } else {
                if (vc->line_values.lvv_values.empty()) {
                    vc->cache_msg(lf, ll);
                    require(vc->line_values.lvv_sbr.get_data() != nullptr);
                    vt->vi->extract(lf, line_number, vc->line_values);
                }

                auto sub_col = logline_value_meta::table_column{
                    (size_t) (col - VT_COL_MAX)};
                auto lv_iter = find_if(vc->line_values.lvv_values.begin(),
                                       vc->line_values.lvv_values.end(),
                                       logline_value_cmp(nullptr, sub_col));

                if (lv_iter != vc->line_values.lvv_values.end()) {
                    if (!lv_iter->lv_meta.lvm_struct_name.empty()) {
                        yajlpp_gen gen;
                        yajl_gen_config(gen, yajl_gen_beautify, false);

                        {
                            yajlpp_map root(gen);

                            for (const auto& lv_struct :
                                 vc->line_values.lvv_values)
                            {
                                if (lv_struct.lv_meta.lvm_column != sub_col) {
                                    continue;
                                }

                                root.gen(lv_struct.lv_meta.lvm_name);
                                switch (lv_struct.lv_meta.lvm_kind) {
                                    case value_kind_t::VALUE_NULL:
                                        root.gen();
                                        break;
                                    case value_kind_t::VALUE_BOOLEAN:
                                        root.gen((bool) lv_struct.lv_value.i);
                                        break;
                                    case value_kind_t::VALUE_INTEGER:
                                        root.gen(lv_struct.lv_value.i);
                                        break;
                                    case value_kind_t::VALUE_FLOAT:
                                        root.gen(lv_struct.lv_value.d);
                                        break;
                                    case value_kind_t::VALUE_JSON: {
                                        auto_mem<yajl_handle_t> parse_handle(
                                            yajl_free);
                                        json_ptr jp("");
                                        json_op jo(jp);

                                        jo.jo_ptr_callbacks
                                            = json_op::gen_callbacks;
                                        jo.jo_ptr_data = gen;
                                        parse_handle.reset(
                                            yajl_alloc(&json_op::ptr_callbacks,
                                                       nullptr,
                                                       &jo));

                                        const auto* json_in
                                            = (const unsigned char*)
                                                  lv_struct.text_value();
                                        auto json_len = lv_struct.text_length();

                                        if (yajl_parse(parse_handle.in(),
                                                       json_in,
                                                       json_len)
                                                != yajl_status_ok
                                            || yajl_complete_parse(
                                                   parse_handle.in())
                                                != yajl_status_ok)
                                        {
                                            log_error(
                                                "failed to parse json value: "
                                                "%.*s",
                                                lv_struct.text_length(),
                                                lv_struct.text_value());
                                            root.gen(lv_struct.to_string());
                                        }
                                        break;
                                    }
                                    default:
                                        root.gen(lv_struct.to_string());
                                        break;
                                }
                            }
                        }

                        auto sf = gen.to_string_fragment();
                        sqlite3_result_text(
                            ctx, sf.data(), sf.length(), SQLITE_TRANSIENT);
                        sqlite3_result_subtype(ctx, JSON_SUBTYPE);
                    } else {
                        switch (lv_iter->lv_meta.lvm_kind) {
                            case value_kind_t::VALUE_NULL:
                                sqlite3_result_null(ctx);
                                break;
                            case value_kind_t::VALUE_JSON: {
                                sqlite3_result_text(ctx,
                                                    lv_iter->text_value(),
                                                    lv_iter->text_length(),
                                                    SQLITE_TRANSIENT);
                                sqlite3_result_subtype(ctx, JSON_SUBTYPE);
                                break;
                            }
                            case value_kind_t::VALUE_STRUCT:
                            case value_kind_t::VALUE_TEXT:
                            case value_kind_t::VALUE_XML:
                            case value_kind_t::VALUE_TIMESTAMP: {
                                sqlite3_result_text(ctx,
                                                    lv_iter->text_value(),
                                                    lv_iter->text_length(),
                                                    SQLITE_TRANSIENT);
                                break;
                            }
                            case value_kind_t::VALUE_W3C_QUOTED:
                            case value_kind_t::VALUE_QUOTED:
                                if (lv_iter->text_length() == 0) {
                                    sqlite3_result_text(
                                        ctx, "", 0, SQLITE_STATIC);
                                } else {
                                    const char* text_value
                                        = lv_iter->text_value();
                                    size_t text_len = lv_iter->text_length();

                                    switch (text_value[0]) {
                                        case '\'':
                                        case '"': {
                                            char* val = (char*) sqlite3_malloc(
                                                text_len);

                                            if (val == nullptr) {
                                                sqlite3_result_error_nomem(ctx);
                                            } else {
                                                auto unquote_func
                                                    = lv_iter->lv_meta.lvm_kind
                                                        == value_kind_t::
                                                            VALUE_W3C_QUOTED
                                                    ? unquote_w3c
                                                    : unquote;

                                                size_t unquoted_len
                                                    = unquote_func(val,
                                                                   text_value,
                                                                   text_len);
                                                sqlite3_result_text(
                                                    ctx,
                                                    val,
                                                    unquoted_len,
                                                    sqlite3_free);
                                            }
                                            break;
                                        }
                                        default: {
                                            sqlite3_result_text(
                                                ctx,
                                                text_value,
                                                lv_iter->text_length(),
                                                SQLITE_TRANSIENT);
                                            break;
                                        }
                                    }
                                }
                                break;

                            case value_kind_t::VALUE_BOOLEAN:
                            case value_kind_t::VALUE_INTEGER:
                                sqlite3_result_int64(ctx, lv_iter->lv_value.i);
                                break;

                            case value_kind_t::VALUE_FLOAT:
                                sqlite3_result_double(ctx, lv_iter->lv_value.d);
                                break;

                            case value_kind_t::VALUE_UNKNOWN:
                            case value_kind_t::VALUE__MAX:
                                require(0);
                                break;
                        }
                    }
                } else {
                    sqlite3_result_null(ctx);
                }
            }
            break;
    }

    return SQLITE_OK;
}

static int
vt_rowid(sqlite3_vtab_cursor* cur, sqlite_int64* p_rowid)
{
    vtab_cursor* p_cur = (vtab_cursor*) cur;

    *p_rowid = (((uint64_t) p_cur->log_cursor.lc_curr_line) << 8)
        | (p_cur->log_cursor.lc_sub_index & 0xff);

    return SQLITE_OK;
}

void
log_cursor::update(unsigned char op, vis_line_t vl, constraint_t cons)
{
    switch (op) {
        case SQLITE_INDEX_CONSTRAINT_EQ:
            if (vl < 0_vl) {
                this->lc_curr_line = this->lc_end_line;
            } else if (vl < this->lc_end_line) {
                this->lc_curr_line = vl;
                if (cons == constraint_t::unique) {
                    this->lc_end_line = this->lc_curr_line + 1_vl;
                }
            }
            break;
        case SQLITE_INDEX_CONSTRAINT_GE:
            if (vl < 0_vl) {
                vl = 0_vl;
            }
            this->lc_curr_line = vl;
            break;
        case SQLITE_INDEX_CONSTRAINT_GT:
            if (vl < 0_vl) {
                this->lc_curr_line = 0_vl;
            } else {
                this->lc_curr_line
                    = vl + (cons == constraint_t::unique ? 1_vl : 0_vl);
            }
            break;
        case SQLITE_INDEX_CONSTRAINT_LE:
            if (vl < 0_vl) {
                this->lc_curr_line = this->lc_end_line;
            } else if (vl < this->lc_end_line) {
                this->lc_end_line
                    = vl + (cons == constraint_t::unique ? 1_vl : 0_vl);
            }
            break;
        case SQLITE_INDEX_CONSTRAINT_LT:
            if (vl <= 0_vl) {
                this->lc_curr_line = this->lc_end_line;
            } else if (vl < this->lc_end_line) {
                this->lc_end_line = vl;
            }
            break;
    }
}

log_cursor::string_constraint::
string_constraint(unsigned char op, std::string value)
    : sc_op(op), sc_value(std::move(value))
{
    if (op == SQLITE_INDEX_CONSTRAINT_REGEXP) {
        auto compile_res = lnav::pcre2pp::code::from(this->sc_value);

        if (compile_res.isErr()) {
            auto ce = compile_res.unwrapErr();
            log_error("unable to compile regexp constraint: %s -- %s",
                      this->sc_value.c_str(),
                      ce.get_message().c_str());
        } else {
            this->sc_pattern = compile_res.unwrap().to_shared();
        }
    }
}

bool
log_cursor::string_constraint::matches(const std::string& sf) const
{
    switch (this->sc_op) {
        case SQLITE_INDEX_CONSTRAINT_EQ:
        case SQLITE_INDEX_CONSTRAINT_IS:
            return sf == this->sc_value;
        case SQLITE_INDEX_CONSTRAINT_NE:
        case SQLITE_INDEX_CONSTRAINT_ISNOT:
            return sf != this->sc_value;
        case SQLITE_INDEX_CONSTRAINT_GT:
            return sf > this->sc_value;
        case SQLITE_INDEX_CONSTRAINT_LE:
            return sf <= this->sc_value;
        case SQLITE_INDEX_CONSTRAINT_LT:
            return sf < this->sc_value;
        case SQLITE_INDEX_CONSTRAINT_GE:
            return sf >= this->sc_value;
        case SQLITE_INDEX_CONSTRAINT_LIKE:
            return sqlite3_strlike(this->sc_value.c_str(), sf.data(), 0) == 0;
        case SQLITE_INDEX_CONSTRAINT_GLOB:
            return sqlite3_strglob(this->sc_value.c_str(), sf.data()) == 0;
        case SQLITE_INDEX_CONSTRAINT_REGEXP: {
            if (this->sc_pattern != nullptr) {
                return this->sc_pattern->find_in(sf, PCRE2_NO_UTF_CHECK)
                    .ignore_error()
                    .has_value();
            }
            // return true here so that the regexp is actually run and fails
            return true;
        }
        case SQLITE_INDEX_CONSTRAINT_ISNOTNULL:
            return true;
        default:
            return false;
    }
}

struct vtab_time_range {
    std::optional<timeval> vtr_begin;
    std::optional<timeval> vtr_end;

    bool empty() const { return !this->vtr_begin && !this->vtr_end; }

    void add(const timeval& tv)
    {
        if (!this->vtr_begin || tv < this->vtr_begin) {
            this->vtr_begin = tv;
        }
        if (!this->vtr_end || this->vtr_end < tv) {
            this->vtr_end = tv;
        }
    }
};

static int
vt_filter(sqlite3_vtab_cursor* p_vtc,
          int idxNum,
          const char* idxStr,
          int argc,
          sqlite3_value** argv)
{
    auto* p_cur = (vtab_cursor*) p_vtc;
    auto* vt = (log_vtab*) p_vtc->pVtab;
    sqlite3_index_info::sqlite3_index_constraint* index = nullptr;

    if (idxStr) {
        auto desc_len = strlen(idxStr);
        auto index_len = idxNum * sizeof(*index);
        auto storage_len = desc_len + 128 + index_len;
        auto* remaining_storage = const_cast<void*>(
            static_cast<const void*>(idxStr + desc_len + 1));
        auto* index_storage
            = std::align(alignof(sqlite3_index_info::sqlite3_index_constraint),
                         index_len,
                         remaining_storage,
                         storage_len);
        index = reinterpret_cast<sqlite3_index_info::sqlite3_index_constraint*>(
            index_storage);
    }

#ifdef DEBUG_INDEXING
    log_debug("vt_filter(%s, %d)", vt->vi->get_name().get(), idxNum);
#endif
    p_cur->log_cursor.lc_format_name.clear();
    p_cur->log_cursor.lc_pattern_name.clear();
    p_cur->log_cursor.lc_opid = std::nullopt;
    p_cur->log_cursor.lc_level_constraint = std::nullopt;
    p_cur->log_cursor.lc_log_path.clear();
    p_cur->log_cursor.lc_indexed_columns.clear();
    p_cur->log_cursor.lc_last_log_path_match = nullptr;
    p_cur->log_cursor.lc_last_log_path_mismatch = nullptr;
    p_cur->log_cursor.lc_unique_path.clear();
    p_cur->log_cursor.lc_last_unique_path_match = nullptr;
    p_cur->log_cursor.lc_last_unique_path_mismatch = nullptr;
    p_cur->log_cursor.lc_curr_line = 0_vl;
    p_cur->log_cursor.lc_end_line = vis_line_t(vt->lss->text_line_count());

    std::optional<vtab_time_range> log_time_range;
    std::optional<log_cursor::opid_hash> opid_val;
    std::vector<log_cursor::string_constraint> log_path_constraints;
    std::vector<log_cursor::string_constraint> log_unique_path_constraints;

    for (int lpc = 0; lpc < idxNum; lpc++) {
        auto col = index[lpc].iColumn;
        auto op = index[lpc].op;
        switch (col) {
            case VT_COL_LINE_NUMBER: {
                auto vl = vis_line_t(sqlite3_value_int64(argv[lpc]));

                p_cur->log_cursor.update(
                    op, vl, log_cursor::constraint_t::unique);
                break;
            }
            case VT_COL_LEVEL: {
                if (sqlite3_value_type(argv[lpc]) != SQLITE3_TEXT) {
                    continue;
                }

                auto sf = from_sqlite<string_fragment>()(argc, argv, lpc);
                auto level = string2level(sf.data(), sf.length());

                p_cur->log_cursor.lc_level_constraint
                    = log_cursor::level_constraint{
                        op,
                        level,
                    };
                break;
            }

            case VT_COL_LOG_TIME:
                if (sqlite3_value_type(argv[lpc]) == SQLITE3_TEXT) {
                    const auto* datestr
                        = (const char*) sqlite3_value_text(argv[lpc]);
                    auto datelen = sqlite3_value_bytes(argv[lpc]);
                    date_time_scanner dts;
                    struct timeval tv;
                    struct exttm mytm;

                    const auto* date_end
                        = dts.scan(datestr, datelen, nullptr, &mytm, tv);
                    if (date_end != (datestr + datelen)) {
                        log_warning(
                            "  log_time constraint is not a valid datetime, "
                            "index will not be applied: %s",
                            datestr);
                    } else {
                        switch (op) {
                            case SQLITE_INDEX_CONSTRAINT_EQ:
                            case SQLITE_INDEX_CONSTRAINT_IS:
                                if (!log_time_range) {
                                    log_time_range = vtab_time_range{};
                                }
                                log_time_range->add(tv);
                                break;
                            case SQLITE_INDEX_CONSTRAINT_GT:
                            case SQLITE_INDEX_CONSTRAINT_GE:
                                if (!log_time_range) {
                                    log_time_range = vtab_time_range{};
                                }
                                log_time_range->vtr_begin = tv;
                                break;
                            case SQLITE_INDEX_CONSTRAINT_LT:
                            case SQLITE_INDEX_CONSTRAINT_LE:
                                if (!log_time_range) {
                                    log_time_range = vtab_time_range{};
                                }
                                log_time_range->vtr_end = tv;
                                break;
                        }
                    }
                } else {
                    log_warning(
                        "  log_time constraint is not text, index will not be "
                        "applied: value_type(%d)=%d",
                        lpc,
                        sqlite3_value_type(argv[lpc]));
                }
                break;
            default: {
                if (col > (VT_COL_MAX + vt->vi->vi_column_count - 1)) {
                    auto footer_column = static_cast<log_footer_columns>(
                        col - (VT_COL_MAX + vt->vi->vi_column_count - 1) - 1);

                    switch (footer_column) {
                        case log_footer_columns::time_msecs: {
                            auto msecs = sqlite3_value_int64(argv[lpc]);
                            struct timeval tv;

                            tv.tv_sec = msecs / 1000;
                            tv.tv_usec = (msecs - tv.tv_sec * 1000) * 1000;
                            switch (op) {
                                case SQLITE_INDEX_CONSTRAINT_EQ:
                                case SQLITE_INDEX_CONSTRAINT_IS:
                                    if (!log_time_range) {
                                        log_time_range = vtab_time_range{};
                                    }
                                    log_time_range->add(tv);
                                    break;
                                case SQLITE_INDEX_CONSTRAINT_GT:
                                case SQLITE_INDEX_CONSTRAINT_GE:
                                    if (!log_time_range) {
                                        log_time_range = vtab_time_range{};
                                    }
                                    log_time_range->vtr_begin = tv;
                                    break;
                                case SQLITE_INDEX_CONSTRAINT_LT:
                                case SQLITE_INDEX_CONSTRAINT_LE:
                                    if (!log_time_range) {
                                        log_time_range = vtab_time_range{};
                                    }
                                    log_time_range->vtr_end = tv;
                                    break;
                            }
                            break;
                        }
                        case log_footer_columns::format: {
                            const auto* format_name_str
                                = (const char*) sqlite3_value_text(argv[lpc]);

                            if (format_name_str != nullptr) {
                                p_cur->log_cursor.lc_format_name
                                    = intern_string::lookup(format_name_str);
                            }
                            break;
                        }
                        case log_footer_columns::format_regex: {
                            const auto* pattern_name_str
                                = (const char*) sqlite3_value_text(argv[lpc]);

                            if (pattern_name_str != nullptr) {
                                p_cur->log_cursor.lc_pattern_name
                                    = intern_string::lookup(pattern_name_str);
                            }
                            break;
                        }
                        case log_footer_columns::opid:
                        case log_footer_columns::user_opid: {
                            if (sqlite3_value_type(argv[lpc]) != SQLITE3_TEXT) {
                                continue;
                            }
                            auto opid = from_sqlite<string_fragment>()(
                                argc, argv, lpc);
                            if (!log_time_range) {
                                log_time_range = vtab_time_range{};
                            }
                            for (const auto& file_data : *vt->lss) {
                                if (file_data->get_file_ptr() == nullptr) {
                                    continue;
                                }
                                safe::ReadAccess<logfile::safe_opid_state>
                                    r_opid_map(
                                        file_data->get_file_ptr()->get_opids());
                                const auto& iter
                                    = r_opid_map->los_opid_ranges.find(opid);
                                if (iter == r_opid_map->los_opid_ranges.end()) {
                                    continue;
                                }
                                log_time_range->add(
                                    iter->second.otr_range.tr_begin);
                                log_time_range->add(
                                    iter->second.otr_range.tr_end);
                            }

                            opid_val = log_cursor::opid_hash{
                                static_cast<unsigned int>(
                                    hash_str(opid.data(), opid.length()))};
                            break;
                        }
                        case log_footer_columns::path: {
                            if (sqlite3_value_type(argv[lpc]) != SQLITE3_TEXT) {
                                continue;
                            }

                            const auto filename
                                = from_sqlite<std::string>()(argc, argv, lpc);
                            const auto fn_constraint
                                = log_cursor::string_constraint{op, filename};
                            auto found = false;

                            if (!log_time_range) {
                                log_time_range = vtab_time_range{};
                            }
                            for (const auto& file_data : *vt->lss) {
                                auto* lf = file_data->get_file_ptr();
                                if (lf == nullptr) {
                                    continue;
                                }
                                if (fn_constraint.matches(lf->get_filename())) {
                                    found = true;
                                    log_time_range->add(
                                        lf->front().get_timeval());
                                    log_time_range->add(
                                        lf->back().get_timeval());
                                }
                            }
                            if (found) {
                                log_path_constraints.emplace_back(
                                    fn_constraint);
                            }
                            break;
                        }
                        case log_footer_columns::unique_path: {
                            if (sqlite3_value_type(argv[lpc]) != SQLITE3_TEXT) {
                                continue;
                            }

                            const auto filename
                                = from_sqlite<std::string>()(argc, argv, lpc);
                            const auto fn_constraint
                                = log_cursor::string_constraint{op, filename};
                            auto found = false;

                            if (!log_time_range) {
                                log_time_range = vtab_time_range{};
                            }
                            for (const auto& file_data : *vt->lss) {
                                auto* lf = file_data->get_file_ptr();
                                if (lf == nullptr) {
                                    continue;
                                }
                                if (fn_constraint.matches(
                                        lf->get_unique_path()))
                                {
                                    found = true;
                                    log_time_range->add(
                                        lf->front().get_timeval());
                                    log_time_range->add(
                                        lf->back().get_timeval());
                                }
                            }
                            if (found) {
                                log_unique_path_constraints.emplace_back(
                                    fn_constraint);
                            }
                            break;
                        }
                        case log_footer_columns::partition:
                        case log_footer_columns::actual_time:
                        case log_footer_columns::idle_msecs:
                        case log_footer_columns::mark:
                        case log_footer_columns::comment:
                        case log_footer_columns::tags:
                        case log_footer_columns::annotations:
                        case log_footer_columns::filters:
                        case log_footer_columns::text:
                        case log_footer_columns::body:
                        case log_footer_columns::raw_text:
                        case log_footer_columns::line_hash:
                            break;
                    }
                } else {
                    const auto* value
                        = (const char*) sqlite3_value_text(argv[lpc]);

                    if (value != nullptr) {
                        auto value_len
                            = (size_t) sqlite3_value_bytes(argv[lpc]);

#ifdef DEBUG_INDEXING
                        log_debug("adding index request for column %d = %s",
                                  col,
                                  value);
#endif

                        p_cur->log_cursor.lc_indexed_columns.emplace_back(
                            col,
                            log_cursor::string_constraint{
                                op,
                                std::string{value, value_len},
                            });
                    }
                }
                break;
            }
        }
    }

    if (!p_cur->log_cursor.lc_indexed_columns.empty()) {
        std::optional<vis_line_t> max_indexed_line;

        for (const auto& icol : p_cur->log_cursor.lc_indexed_columns) {
            auto& coli = vt->vi->vi_column_indexes[icol.cc_column];

            if (coli.ci_index_generation != vt->lss->lss_index_generation) {
                coli.ci_value_to_lines.clear();
                coli.ci_index_generation = vt->lss->lss_index_generation;
                coli.ci_max_line = 0_vl;
            }

            if (!max_indexed_line) {
                max_indexed_line = coli.ci_max_line;
            } else if (coli.ci_max_line < max_indexed_line.value()) {
                max_indexed_line = coli.ci_max_line;
            }
        }

        for (const auto& icol : p_cur->log_cursor.lc_indexed_columns) {
            auto& coli = vt->vi->vi_column_indexes[icol.cc_column];

            auto iter
                = coli.ci_value_to_lines.find(icol.cc_constraint.sc_value);
            if (iter != coli.ci_value_to_lines.end()) {
                for (auto vl : iter->second) {
                    if (vl >= max_indexed_line.value()) {
                        continue;
                    }

                    if (vl < p_cur->log_cursor.lc_curr_line) {
                        continue;
                    }

#ifdef DEBUG_INDEXING
                    log_debug("adding indexed line %d", (int) vl);
#endif
                    p_cur->log_cursor.lc_indexed_lines.push_back(vl);
                }
            }
        }

        if (max_indexed_line && max_indexed_line.value() > 0_vl) {
            p_cur->log_cursor.lc_indexed_lines.push_back(
                max_indexed_line.value());
        }

        std::sort(p_cur->log_cursor.lc_indexed_lines.begin(),
                  p_cur->log_cursor.lc_indexed_lines.end(),
                  std::greater<>());

        if (max_indexed_line
            && max_indexed_line.value() < vt->lss->text_line_count())
        {
            log_debug("max indexed out of sync, clearing other indexes");
            p_cur->log_cursor.lc_level_constraint = std::nullopt;
            p_cur->log_cursor.lc_curr_line = 0_vl;
            opid_val = std::nullopt;
            log_time_range = std::nullopt;
            p_cur->log_cursor.lc_indexed_lines.clear();
            log_path_constraints.clear();
            log_unique_path_constraints.clear();
        }

#ifdef DEBUG_INDEXING
        log_debug("indexed lines:");
        for (auto indline : p_cur->log_cursor.lc_indexed_lines) {
            log_debug("  %d", (int) indline);
        }
#endif
    }

    if (!log_time_range) {
    } else if (log_time_range->empty()) {
        p_cur->log_cursor.lc_curr_line = p_cur->log_cursor.lc_end_line;
    } else {
        if (log_time_range->vtr_begin) {
            auto vl_opt
                = vt->lss->row_for_time(log_time_range->vtr_begin.value());
            if (!vl_opt) {
                p_cur->log_cursor.lc_curr_line = p_cur->log_cursor.lc_end_line;
            } else {
                p_cur->log_cursor.lc_curr_line = vl_opt.value();
            }
        }
        if (log_time_range->vtr_end) {
            auto vl_max_opt
                = vt->lss->row_for_time(log_time_range->vtr_end.value());
            if (vl_max_opt) {
                p_cur->log_cursor.lc_end_line = vl_max_opt.value();
                for (const auto& msg_info :
                     vt->lss->window_at(vl_max_opt.value(),
                                        vis_line_t(vt->lss->text_line_count())))
                {
                    if (log_time_range->vtr_end.value()
                        < msg_info.get_logline().get_timeval())
                    {
                        break;
                    }
                    p_cur->log_cursor.lc_end_line
                        = msg_info.get_vis_line() + 1_vl;
                }
            }
        }
    }

    p_cur->log_cursor.lc_opid = opid_val;
    p_cur->log_cursor.lc_log_path = std::move(log_path_constraints);
    p_cur->log_cursor.lc_unique_path = std::move(log_unique_path_constraints);

    if (p_cur->log_cursor.lc_indexed_lines.empty()) {
        p_cur->log_cursor.lc_indexed_lines.push_back(
            p_cur->log_cursor.lc_curr_line);
    }
    vt->vi->filter(p_cur->log_cursor, *vt->lss);

    auto rc = vt->base.pModule->xNext(p_vtc);

#ifdef DEBUG_INDEXING
    log_debug("vt_filter() -> cursor_range(%d:%d)",
              (int) p_cur->log_cursor.lc_curr_line,
              (int) p_cur->log_cursor.lc_end_line);
#endif

    return rc;
}

static int
vt_best_index(sqlite3_vtab* tab, sqlite3_index_info* p_info)
{
    std::vector<sqlite3_index_info::sqlite3_index_constraint> indexes;
    std::vector<std::string> index_desc;
    int argvInUse = 0;
    auto* vt = (log_vtab*) tab;

    log_info("vt_best_index(%s, nConstraint=%d)",
             vt->vi->get_name().get(),
             p_info->nConstraint);
    if (!vt->vi->vi_supports_indexes) {
        return SQLITE_OK;
    }
    for (int lpc = 0; lpc < p_info->nConstraint; lpc++) {
        const auto& constraint = p_info->aConstraint[lpc];
        if (!constraint.usable || constraint.op == SQLITE_INDEX_CONSTRAINT_MATCH
#ifdef SQLITE_INDEX_CONSTRAINT_OFFSET
            || constraint.op == SQLITE_INDEX_CONSTRAINT_OFFSET
            || constraint.op == SQLITE_INDEX_CONSTRAINT_LIMIT
#endif
        )
        {
            log_debug("  column %d: is not usable (usable=%d, op: %s)",
                      lpc,
                      constraint.usable,
                      sql_constraint_op_name(constraint.op));
            continue;
        }

        auto col = constraint.iColumn;
        auto op = constraint.op;
        log_debug("  column %d: op: %s", col, sql_constraint_op_name(op));
        switch (col) {
            case VT_COL_LINE_NUMBER: {
                argvInUse += 1;
                indexes.push_back(constraint);
                p_info->aConstraintUsage[lpc].argvIndex = argvInUse;
                index_desc.emplace_back(fmt::format(
                    FMT_STRING("log_line {} ?"), sql_constraint_op_name(op)));
                break;
            }
            case VT_COL_LOG_TIME: {
                argvInUse += 1;
                indexes.push_back(p_info->aConstraint[lpc]);
                p_info->aConstraintUsage[lpc].argvIndex = argvInUse;
                index_desc.emplace_back(fmt::format(
                    FMT_STRING("log_time {} ?"), sql_constraint_op_name(op)));
                break;
            }
            case VT_COL_LEVEL: {
                if (log_cursor::level_constraint::op_is_supported(op)) {
                    argvInUse += 1;
                    indexes.push_back(constraint);
                    p_info->aConstraintUsage[lpc].argvIndex = argvInUse;
                    index_desc.emplace_back(
                        fmt::format(FMT_STRING("log_level {} ?"),
                                    sql_constraint_op_name(op)));
                }
                break;
            }
            default: {
                if (col > (VT_COL_MAX + vt->vi->vi_column_count - 1)) {
                    auto footer_column = static_cast<log_footer_columns>(
                        col - (VT_COL_MAX + vt->vi->vi_column_count - 1) - 1);

                    switch (footer_column) {
                        case log_footer_columns::time_msecs: {
                            argvInUse += 1;
                            indexes.push_back(p_info->aConstraint[lpc]);
                            p_info->aConstraintUsage[lpc].argvIndex = argvInUse;
                            index_desc.emplace_back(
                                fmt::format(FMT_STRING("log_time_msecs {} ?"),
                                            sql_constraint_op_name(op)));
                            break;
                        }
                        case log_footer_columns::format: {
                            if (op == SQLITE_INDEX_CONSTRAINT_EQ) {
                                argvInUse += 1;
                                indexes.push_back(constraint);
                                p_info->aConstraintUsage[lpc].argvIndex
                                    = argvInUse;
                                index_desc.emplace_back("log_format = ?");
                            }
                            break;
                        }
                        case log_footer_columns::format_regex: {
                            if (op == SQLITE_INDEX_CONSTRAINT_EQ) {
                                argvInUse += 1;
                                indexes.push_back(constraint);
                                p_info->aConstraintUsage[lpc].argvIndex
                                    = argvInUse;
                                index_desc.emplace_back("log_format_regex = ?");
                            }
                            break;
                        }
                        case log_footer_columns::opid:
                        case log_footer_columns::user_opid: {
                            if (op == SQLITE_INDEX_CONSTRAINT_EQ) {
                                argvInUse += 1;
                                indexes.push_back(constraint);
                                p_info->aConstraintUsage[lpc].argvIndex
                                    = argvInUse;
                                index_desc.emplace_back("log_opid = ?");
                            }
                            break;
                        }
                        case log_footer_columns::path: {
                            argvInUse += 1;
                            indexes.push_back(constraint);
                            p_info->aConstraintUsage[lpc].argvIndex = argvInUse;
                            index_desc.emplace_back(
                                fmt::format(FMT_STRING("log_path {} ?"),
                                            sql_constraint_op_name(op)));
                            break;
                        }
                        case log_footer_columns::unique_path: {
                            argvInUse += 1;
                            indexes.push_back(constraint);
                            p_info->aConstraintUsage[lpc].argvIndex = argvInUse;
                            index_desc.emplace_back(
                                fmt::format(FMT_STRING("log_unique_path {} ?"),
                                            sql_constraint_op_name(op)));
                            break;
                        }
                        case log_footer_columns::partition:
                        case log_footer_columns::actual_time:
                        case log_footer_columns::idle_msecs:
                        case log_footer_columns::mark:
                        case log_footer_columns::comment:
                        case log_footer_columns::tags:
                        case log_footer_columns::annotations:
                        case log_footer_columns::filters:
                        case log_footer_columns::text:
                        case log_footer_columns::body:
                        case log_footer_columns::raw_text:
                        case log_footer_columns::line_hash:
                            break;
                    }
                } else if (op == SQLITE_INDEX_CONSTRAINT_EQ) {
                    argvInUse += 1;
                    indexes.push_back(constraint);
                    p_info->aConstraintUsage[lpc].argvIndex = argvInUse;
                    index_desc.emplace_back(
                        fmt::format(FMT_STRING("col({}) {} ?"),
                                    col,
                                    sql_constraint_op_name(op)));
                }
                break;
            }
        }
    }

    if (argvInUse) {
        auto full_desc = fmt::format(FMT_STRING("SEARCH {} USING {}"),
                                     vt->vi->get_name().get(),
                                     fmt::join(index_desc, " AND "));
        log_info("found index: %s", full_desc.c_str());

        sqlite3_index_info::sqlite3_index_constraint* index_copy;
        auto index_len = indexes.size() * sizeof(*index_copy);
        size_t len = full_desc.size() + 128 + index_len;
        auto* storage = sqlite3_malloc(len);
        if (!storage) {
            return SQLITE_NOMEM;
        }
        auto* desc_storage = static_cast<char*>(storage);
        memcpy(desc_storage, full_desc.c_str(), full_desc.size() + 1);
        auto* remaining_storage
            = static_cast<void*>(desc_storage + full_desc.size() + 1);
        len -= full_desc.size() - 1;
        auto* index_storage
            = std::align(alignof(sqlite3_index_info::sqlite3_index_constraint),
                         index_len,
                         remaining_storage,
                         len);
        index_copy
            = reinterpret_cast<sqlite3_index_info::sqlite3_index_constraint*>(
                index_storage);
        memcpy(index_copy, &indexes[0], index_len);
        p_info->idxNum = argvInUse;
        p_info->idxStr = static_cast<char*>(storage);
        p_info->needToFreeIdxStr = 1;
        p_info->estimatedCost = 10.0;
    } else {
        static char fullscan_str[] = "fullscan";

        p_info->idxStr = fullscan_str;
        p_info->estimatedCost = 1000000000.0;
    }

    return SQLITE_OK;
}

static const struct json_path_container tags_handler = {
    json_path_handler("#")
        .with_synopsis("tag")
        .with_description("A tag for the log line")
        .with_pattern(R"(^#[^\s]+$)")
        .for_field(&bookmark_metadata::bm_tags),
};

static int
vt_update(sqlite3_vtab* tab,
          int argc,
          sqlite3_value** argv,
          sqlite_int64* rowid_out)
{
    auto* vt = (log_vtab*) tab;
    int retval = SQLITE_READONLY;

    if (argc > 1 && sqlite3_value_type(argv[0]) != SQLITE_NULL
        && sqlite3_value_int64(argv[0]) == sqlite3_value_int64(argv[1]))
    {
        int64_t rowid = sqlite3_value_int64(argv[0]) >> 8;
        int val = sqlite3_value_int(
            argv[2 + vt->footer_index(log_footer_columns::mark)]);
        vis_line_t vrowid(rowid);
        const auto msg_info = *vt->lss->window_at(vrowid).begin();
        const auto* part_name = sqlite3_value_text(
            argv[2 + vt->footer_index(log_footer_columns::partition)]);
        const auto* log_comment = sqlite3_value_text(
            argv[2 + vt->footer_index(log_footer_columns::comment)]);
        const auto log_tags = from_sqlite<std::optional<string_fragment>>()(
            argc, argv, 2 + vt->footer_index(log_footer_columns::tags));
        const auto log_annos = from_sqlite<std::optional<string_fragment>>()(
            argc, argv, 2 + vt->footer_index(log_footer_columns::annotations));
        auto log_opid = from_sqlite<std::optional<string_fragment>>()(
            argc, argv, 2 + vt->footer_index(log_footer_columns::opid));
        const auto log_user_opid
            = from_sqlite<std::optional<string_fragment>>()(
                argc,
                argv,
                2 + vt->footer_index(log_footer_columns::user_opid));
        bookmark_metadata tmp_bm;

        if (log_user_opid) {
            log_opid = log_user_opid;
        }
        if (log_tags) {
            std::vector<lnav::console::user_message> errors;
            yajlpp_parse_context ypc(vt->vi->get_tags_name(), &tags_handler);
            auto_mem<yajl_handle_t> handle(yajl_free);

            handle = yajl_alloc(&ypc.ypc_callbacks, nullptr, &ypc);
            ypc.ypc_userdata = &errors;
            ypc.ypc_line_number = log_vtab_data.lvd_location.sl_line_number;
            ypc.with_handle(handle)
                .with_error_reporter([](const yajlpp_parse_context& ypc,
                                        auto msg) {
                    auto& errors = *((std::vector<lnav::console::user_message>*)
                                         ypc.ypc_userdata);
                    errors.emplace_back(msg);
                })
                .with_obj(tmp_bm);
            ypc.parse_doc(log_tags.value());
            if (!errors.empty()) {
                auto top_error = lnav::console::user_message::error(
                                     attr_line_t("invalid value for ")
                                         .append_quoted("log_tags"_symbol)
                                         .append(" column of table ")
                                         .append_quoted(lnav::roles::symbol(
                                             vt->vi->get_name().to_string())))
                                     .with_reason(errors[0].to_attr_line({}))
                                     .move();
                set_vtable_errmsg(tab, top_error);
                return SQLITE_ERROR;
            }
        }
        if (log_annos) {
            static const intern_string_t SRC
                = intern_string::lookup("log_annotations");

            auto parse_res = logmsg_annotations_handlers.parser_for(SRC).of(
                log_annos.value());
            if (parse_res.isErr()) {
                set_vtable_errmsg(tab, parse_res.unwrapErr()[0]);
                return SQLITE_ERROR;
            }

            tmp_bm.bm_annotations = parse_res.unwrap();
        }

        auto& bv_meta = vt->tc->get_bookmarks()[&textview_curses::BM_META];
        bool has_meta = log_comment != nullptr || log_tags.has_value()
            || log_annos.has_value();

        if (std::binary_search(bv_meta.begin(), bv_meta.end(), vrowid)
            && !has_meta)
        {
            vt->tc->set_user_mark(&textview_curses::BM_META, vrowid, false);
            vt->lss->set_line_meta_changed();
        }

        if (!has_meta && part_name == nullptr
            && (!log_opid
                || msg_info.get_values().lvv_opid_provenance
                    == logline_value_vector::opid_provenance::file))
        {
            vt->lss->erase_bookmark_metadata(vrowid);
        }

        if (part_name) {
            auto& line_meta = vt->lss->get_bookmark_metadata(vrowid);
            line_meta.bm_name = std::string((const char*) part_name);
            vt->tc->set_user_mark(&textview_curses::BM_PARTITION, vrowid, true);
        } else {
            vt->tc->set_user_mark(
                &textview_curses::BM_PARTITION, vrowid, false);
        }

        if (log_opid) {
            auto& lvv = msg_info.get_values();
            if (!lvv.lvv_opid_value
                || lvv.lvv_opid_provenance
                    == logline_value_vector::opid_provenance::user)
            {
                msg_info.get_file_ptr()->set_logline_opid(
                    msg_info.get_file_line_number(), log_opid.value());
                vt->lss->set_line_meta_changed();
            }
        } else if (msg_info.get_values().lvv_opid_provenance
                   == logline_value_vector::opid_provenance::user)
        {
            msg_info.get_file_ptr()->clear_logline_opid(
                msg_info.get_file_line_number());
        }

        if (has_meta) {
            auto& line_meta = vt->lss->get_bookmark_metadata(vrowid);

            vt->tc->set_user_mark(&textview_curses::BM_META, vrowid, true);
            if (part_name == nullptr) {
                line_meta.bm_name.clear();
            }
            if (log_comment) {
                line_meta.bm_comment = std::string((const char*) log_comment);
            } else {
                line_meta.bm_comment.clear();
            }
            if (log_tags) {
                line_meta.bm_tags.clear();
                for (const auto& tag : tmp_bm.bm_tags) {
                    line_meta.add_tag(tag);
                }

                for (const auto& tag : line_meta.bm_tags) {
                    bookmark_metadata::KNOWN_TAGS.insert(tag);
                }
            } else {
                line_meta.bm_tags.clear();
            }
            if (log_annos) {
                line_meta.bm_annotations = std::move(tmp_bm.bm_annotations);
            } else if (!sqlite3_value_nochange(
                           argv[2
                                + vt->footer_index(
                                    log_footer_columns::annotations)]))
            {
                line_meta.bm_annotations.la_pairs.clear();
            }
            vt->lss->set_line_meta_changed();
        }

        vt->tc->set_user_mark(&textview_curses::BM_USER, vrowid, val);
        rowid += 1;
        while ((size_t) rowid < vt->lss->text_line_count()) {
            vis_line_t vl(rowid);
            auto cl = vt->lss->at(vl);
            auto* ll = vt->lss->find_line(cl);
            if (ll->is_message()) {
                break;
            }
            vt->tc->set_user_mark(&textview_curses::BM_USER, vl, val);
            rowid += 1;
        }

        if (retval != SQLITE_ERROR) {
            retval = SQLITE_OK;
        }
    }

    return retval;
}

static sqlite3_module generic_vtab_module = {
    0, /* iVersion */
    vt_create, /* xCreate       - create a vtable */
    vt_connect, /* xConnect      - associate a vtable with a connection */
    vt_best_index, /* xBestIndex    - best index */
    vt_disconnect, /* xDisconnect   - disassociate a vtable with a connection */
    vt_destroy, /* xDestroy      - destroy a vtable */
    vt_open, /* xOpen         - open a cursor */
    vt_close, /* xClose        - close a cursor */
    vt_filter, /* xFilter       - configure scan constraints */
    vt_next, /* xNext         - advance a cursor */
    vt_eof, /* xEof          - inidicate end of result set*/
    vt_column, /* xColumn       - read data */
    vt_rowid, /* xRowid        - read data */
    vt_update, /* xUpdate       - write data */
    nullptr, /* xBegin        - begin transaction */
    nullptr, /* xSync         - sync transaction */
    nullptr, /* xCommit       - commit transaction */
    nullptr, /* xRollback     - rollback transaction */
    nullptr, /* xFindFunction - function overloading */
};

static sqlite3_module no_rowid_vtab_module = {
    0, /* iVersion */
    vt_create, /* xCreate       - create a vtable */
    vt_connect, /* xConnect      - associate a vtable with a connection */
    vt_best_index, /* xBestIndex    - best index */
    vt_disconnect, /* xDisconnect   - disassociate a vtable with a connection */
    vt_destroy, /* xDestroy      - destroy a vtable */
    vt_open, /* xOpen         - open a cursor */
    vt_close, /* xClose        - close a cursor */
    vt_filter, /* xFilter       - configure scan constraints */
    vt_next_no_rowid, /* xNext         - advance a cursor */
    vt_eof, /* xEof          - inidicate end of result set*/
    vt_column, /* xColumn       - read data */
    nullptr, /* xRowid        - read data */
    nullptr, /* xUpdate       - write data */
    nullptr, /* xBegin        - begin transaction */
    nullptr, /* xSync         - sync transaction */
    nullptr, /* xCommit       - commit transaction */
    nullptr, /* xRollback     - rollback transaction */
    nullptr, /* xFindFunction - function overloading */
};

static int
progress_callback(void* ptr)
{
    int retval = 0;

    if (log_vtab_data.lvd_progress != nullptr) {
        retval = log_vtab_data.lvd_progress(log_cursor_latest);
    }
    if (!log_vtab_data.lvd_looping) {
        retval = 1;
    }

    return retval;
}

log_vtab_manager::
log_vtab_manager(sqlite3* memdb, textview_curses& tc, logfile_sub_source& lss)
    : vm_db(memdb), vm_textview(tc), vm_source(lss)
{
    sqlite3_create_module(
        this->vm_db, "log_vtab_impl", &generic_vtab_module, this);
    sqlite3_create_module(
        this->vm_db, "log_vtab_no_rowid_impl", &no_rowid_vtab_module, this);
    sqlite3_progress_handler(memdb, 32, progress_callback, nullptr);
}

log_vtab_manager::~
log_vtab_manager()
{
    while (!this->vm_impls.empty()) {
        auto first_name = this->vm_impls.begin()->first;

        this->unregister_vtab(first_name);
    }
}

std::string
log_vtab_manager::register_vtab(std::shared_ptr<log_vtab_impl> vi)
{
    std::string retval;

    if (this->vm_impls.find(vi->get_name()) == this->vm_impls.end()) {
        std::vector<std::string> primary_keys;
        auto_mem<char, sqlite3_free> errmsg;
        auto_mem<char, sqlite3_free> sql;
        int rc;

        this->vm_impls[vi->get_name()] = vi;

        vi->get_primary_keys(primary_keys);

        sql = sqlite3_mprintf(
            "CREATE VIRTUAL TABLE %s "
            "USING %s(%s)",
            vi->get_name().get(),
            primary_keys.empty() ? "log_vtab_impl" : "log_vtab_no_rowid_impl",
            vi->get_name().get());
        rc = sqlite3_exec(this->vm_db, sql, nullptr, nullptr, errmsg.out());
        if (rc != SQLITE_OK) {
            retval = errmsg;
        }
    } else {
        retval = "a table with the given name already exists";
    }

    return retval;
}

std::string
log_vtab_manager::unregister_vtab(intern_string_t name)
{
    std::string retval;

    if (this->vm_impls.find(name) == this->vm_impls.end()) {
        retval = fmt::format(FMT_STRING("unknown table -- {}"), name);
    } else {
        auto_mem<char, sqlite3_free> sql;
        __attribute((unused)) int rc;

        sql = sqlite3_mprintf("DROP TABLE %s ", name.get());
        rc = sqlite3_exec(this->vm_db, sql, NULL, NULL, NULL);

        this->vm_impls.erase(name);
    }

    return retval;
}

bool
log_format_vtab_impl::next(log_cursor& lc, logfile_sub_source& lss)
{
    if (lc.is_eof()) {
        return true;
    }

    auto cl = content_line_t(lss.at(lc.lc_curr_line));
    auto* lf = lss.find_file_ptr(cl);
    auto lf_iter = lf->begin() + cl;
    uint8_t mod_id = lf_iter->get_module_id();

    if (!lf_iter->is_message()) {
        return false;
    }

    auto format = lf->get_format();
    if (format->get_name() == this->lfvi_format.get_name()) {
        return true;
    }
    if (mod_id && mod_id == this->lfvi_format.lf_mod_index) {
        // XXX
        return true;
    }

    return false;
}
