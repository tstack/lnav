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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include "lnav_log.hh"
#include "sql_util.hh"
#include "log_vtab_impl.hh"
#include "yajlpp_def.hh"
#include "vtab_module.hh"

#include "logfile_sub_source.hh"

using namespace std;

static struct log_cursor log_cursor_latest;

struct _log_vtab_data log_vtab_data;

static const char *LOG_COLUMNS = R"(  (
  log_line        INTEGER  PRIMARY KEY,            -- The line number for the log message
  log_part        TEXT     COLLATE naturalnocase,  -- The partition the message is in
  log_time        DATETIME,                        -- The adjusted timestamp for the log message
  log_actual_time DATETIME HIDDEN,                 -- The timestamp from the original log file for this message
  log_idle_msecs  INTEGER,                         -- The difference in time between this messages and the previous
  log_level       TEXT     COLLATE loglevel,       -- The log message level
  log_mark        BOOLEAN,                         -- True if the log message was marked
  log_comment     TEXT,                            -- The comment for this message
  log_tags        TEXT,                            -- A JSON list of tags for this message
  log_filters     TEXT,                            -- A JSON list of filter IDs that matched this message
  -- BEGIN Format-specific fields:
)";

static const char *LOG_FOOTER_COLUMNS = R"(
  -- END Format-specific fields
  log_path        TEXT HIDDEN COLLATE naturalnocase, -- The path to the log file this message is from
  log_text        TEXT HIDDEN,                       -- The full text of the log message
  log_body        TEXT HIDDEN                        -- The body of the log message
);
)";

static const char *type_to_string(int type)
{
    switch (type) {
    case SQLITE_FLOAT:
        return "FLOAT";

    case SQLITE_INTEGER:
        return "INTEGER";

    case SQLITE_TEXT:
        return "TEXT";
    }

    ensure("Invalid sqlite type");

    return nullptr;
}

std::string log_vtab_impl::get_table_statement()
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
        auto_mem<char, sqlite3_free> coldecl;
        auto_mem<char, sqlite3_free> colname;
        string comment;

        require(!iter->vc_name.empty());

        if (!iter->vc_comment.empty()) {
            comment.append(" -- ")
                   .append(iter->vc_comment);
        }

        colname = sql_quote_ident(iter->vc_name.c_str());
        coldecl = sqlite3_mprintf("  %-*s %-7s %s COLLATE %-15Q,%s\n",
                                  max_name_len,
                                  colname.in(),
                                  type_to_string(iter->vc_type),
                                  iter->vc_hidden ? "hidden" : "",
                                  (iter->vc_collator == NULL ||
                                   iter->vc_collator[0] == '\0') ?
                                  "BINARY" : iter->vc_collator,
                                  comment.c_str());
        oss << coldecl;
    }
    oss << LOG_FOOTER_COLUMNS;

    log_debug("log_vtab_impl.get_table_statement() -> %s", oss.str().c_str());

    return oss.str();
}

pair<int, unsigned int> log_vtab_impl::logline_value_to_sqlite_type(logline_value::kind_t kind)
{
    int type = 0;
    unsigned int subtype = 0;

    switch (kind) {
        case logline_value::VALUE_JSON:
            type = SQLITE3_TEXT;
            subtype = 74;
            break;
        case logline_value::VALUE_NULL:
        case logline_value::VALUE_TEXT:
        case logline_value::VALUE_STRUCT:
        case logline_value::VALUE_QUOTED:
        case logline_value::VALUE_TIMESTAMP:
            type = SQLITE3_TEXT;
            break;
        case logline_value::VALUE_FLOAT:
            type = SQLITE_FLOAT;
            break;
        case logline_value::VALUE_BOOLEAN:
        case logline_value::VALUE_INTEGER:
            type = SQLITE_INTEGER;
            break;
        case logline_value::VALUE_UNKNOWN:
        case logline_value::VALUE__MAX:
            ensure(0);
            break;
    }
    return make_pair(type, subtype);
}

struct vtab {
    sqlite3_vtab        base;
    sqlite3 *           db;
    textview_curses *tc;
    logfile_sub_source *lss;
    log_vtab_impl *     vi;
};

struct vtab_cursor {
    sqlite3_vtab_cursor        base;
    struct log_cursor          log_cursor;
    shared_buffer_ref          log_msg;
    std::vector<logline_value> line_values;
};

static int vt_destructor(sqlite3_vtab *p_svt);

static int vt_create(sqlite3 *db,
                     void *pAux,
                     int argc, const char *const *argv,
                     sqlite3_vtab **pp_vt,
                     char **pzErr)
{
    log_vtab_manager *vm = (log_vtab_manager *)pAux;
    int   rc             = SQLITE_OK;
    vtab *p_vt;

    /* Allocate the sqlite3_vtab/vtab structure itself */
    p_vt = (vtab *)sqlite3_malloc(sizeof(*p_vt));

    if (p_vt == NULL) {
        return SQLITE_NOMEM;
    }

    memset(&p_vt->base, 0, sizeof(sqlite3_vtab));
    p_vt->db = db;

    /* Declare the vtable's structure */
    p_vt->vi = vm->lookup_impl(intern_string::lookup(argv[3]));
    if (p_vt->vi == NULL) {
        return SQLITE_ERROR;
    }
    p_vt->tc = vm->get_view();
    p_vt->lss = vm->get_source();
    rc        = sqlite3_declare_vtab(db, p_vt->vi->get_table_statement().c_str());

    /* Success. Set *pp_vt and return */
    *pp_vt = &p_vt->base;

    log_debug("creating log format table: %s = %p", argv[3], p_vt);

    return rc;
}

static int vt_destructor(sqlite3_vtab *p_svt)
{
    vtab *p_vt = (vtab *)p_svt;

    /* Free the SQLite structure */
    sqlite3_free(p_vt);

    return SQLITE_OK;
}

static int vt_connect(sqlite3 *db, void *p_aux,
                      int argc, const char *const *argv,
                      sqlite3_vtab **pp_vt, char **pzErr)
{
    return vt_create(db, p_aux, argc, argv, pp_vt, pzErr);
}

static int vt_disconnect(sqlite3_vtab *pVtab)
{
    return vt_destructor(pVtab);
}

static int vt_destroy(sqlite3_vtab *p_vt)
{
    return vt_destructor(p_vt);
}

static int vt_next(sqlite3_vtab_cursor *cur);

static int vt_open(sqlite3_vtab *p_svt, sqlite3_vtab_cursor **pp_cursor)
{
    vtab *p_vt = (vtab *)p_svt;

    p_vt->base.zErrMsg = NULL;

    vtab_cursor *p_cur = new vtab_cursor();

    *pp_cursor = (sqlite3_vtab_cursor *)p_cur;

    p_cur->base.pVtab = p_svt;
    p_cur->log_cursor.lc_curr_line = vis_line_t(-1);
    p_cur->log_cursor.lc_end_line = vis_line_t(p_vt->lss->text_line_count());
    p_cur->log_cursor.lc_sub_index = 0;
    vt_next((sqlite3_vtab_cursor *)p_cur);

    return SQLITE_OK;
}

static int vt_close(sqlite3_vtab_cursor *cur)
{
    vtab_cursor *p_cur = (vtab_cursor *)cur;

    /* Free cursor struct. */
    delete p_cur;

    return SQLITE_OK;
}

static int vt_eof(sqlite3_vtab_cursor *cur)
{
    vtab_cursor *vc = (vtab_cursor *)cur;

    return vc->log_cursor.is_eof();
}

static int vt_next(sqlite3_vtab_cursor *cur)
{
    vtab_cursor *vc   = (vtab_cursor *)cur;
    vtab *       vt   = (vtab *)cur->pVtab;
    bool         done = false;

    vc->line_values.clear();
    do {
        log_cursor_latest = vc->log_cursor;
        if (((log_cursor_latest.lc_curr_line % 1024) == 0) &&
            (log_vtab_data.lvd_progress != NULL &&
             log_vtab_data.lvd_progress(log_cursor_latest))) {
            break;
        }
        done = vt->vi->next(vc->log_cursor, *vt->lss);
    } while (!done);

    return SQLITE_OK;
}

static int vt_column(sqlite3_vtab_cursor *cur, sqlite3_context *ctx, int col)
{
    vtab_cursor *vc = (vtab_cursor *)cur;
    vtab *       vt = (vtab *)cur->pVtab;

    content_line_t    cl(vt->lss->at(vc->log_cursor.lc_curr_line));
    uint64_t line_number;
    auto ld = vt->lss->find_data(cl, line_number);
    shared_ptr<logfile> lf = ld->get_file();
    auto ll = lf->begin() + line_number;

    require(col >= 0);

    /* Just return the ordinal of the column requested. */
    switch (col) {
    case VT_COL_LINE_NUMBER:
    {
        sqlite3_result_int64(ctx, vc->log_cursor.lc_curr_line);
    }
    break;

    case VT_COL_PARTITION:
    {
        vis_bookmarks &vb = vt->tc->get_bookmarks();
        bookmark_vector<vis_line_t> &bv = vb[&textview_curses::BM_META];

        if (bv.empty()) {
            sqlite3_result_null(ctx);
        }
        else {
            vis_line_t curr_line(vc->log_cursor.lc_curr_line);
            auto iter = lower_bound(bv.begin(), bv.end(), curr_line + 1_vl);

            if (iter != bv.begin()) {
                --iter;
                content_line_t part_line = vt->lss->at(*iter);
                std::map<content_line_t, bookmark_metadata> &bm_meta = vt->lss->get_user_bookmark_metadata();
                std::map<content_line_t, bookmark_metadata>::iterator meta_iter;

                meta_iter = bm_meta.find(part_line);
                if (meta_iter != bm_meta.end() &&
                    !meta_iter->second.bm_name.empty()) {
                    sqlite3_result_text(ctx,
                                        meta_iter->second.bm_name.c_str(),
                                        meta_iter->second.bm_name.size(),
                                        SQLITE_TRANSIENT);
                } else {
                    sqlite3_result_null(ctx);
                }
            } else {
                sqlite3_result_null(ctx);
            }
        }
    }
    break;

    case VT_COL_LOG_TIME:
    {
        char   buffer[64];

        sql_strftime(buffer, sizeof(buffer), ll->get_time(), ll->get_millis());
        sqlite3_result_text(ctx, buffer, strlen(buffer), SQLITE_TRANSIENT);
    }
    break;

        case VT_COL_LOG_ACTUAL_TIME: {
            char buffer[64];

            if (ll->is_time_skewed()) {
                if (vc->line_values.empty()) {
                    lf->read_full_message(ll, vc->log_msg);
                    vt->vi->extract(lf, vc->log_msg, vc->line_values);
                }

                struct line_range time_range;

                time_range = find_string_attr_range(
                    vt->vi->vi_attrs, &logline::L_TIMESTAMP);

                const char *time_src = vc->log_msg.get_data() + time_range.lr_start;
                struct timeval actual_tv;
                struct exttm tm;

                if (lf->get_format()->lf_date_time.scan(
                    time_src, time_range.length(),
                    lf->get_format()->get_timestamp_formats(),
                    &tm, actual_tv,
                    false)) {
                    sql_strftime(buffer, sizeof(buffer), actual_tv);
                }
            }
            else {
                sql_strftime(buffer, sizeof(buffer), ll->get_time(), ll->get_millis());
            }
            sqlite3_result_text(ctx, buffer, strlen(buffer), SQLITE_TRANSIENT);
            break;
        }

    case VT_COL_IDLE_MSECS:
        if (vc->log_cursor.lc_curr_line == 0) {
            sqlite3_result_int64(ctx, 0);
        }
        else {
            content_line_t prev_cl(vt->lss->at(vis_line_t(
                                                   vc->log_cursor.lc_curr_line -
                                                   1)));
            shared_ptr<logfile>         prev_lf = vt->lss->find(prev_cl);
            logfile::iterator prev_ll = prev_lf->begin() + prev_cl;
            uint64_t          prev_time, curr_line_time;

            prev_time       = prev_ll->get_time() * 1000ULL;
            prev_time      += prev_ll->get_millis();
            curr_line_time  = ll->get_time() * 1000ULL;
            curr_line_time += ll->get_millis();
            // require(curr_line_time >= prev_time);
            sqlite3_result_int64(ctx, curr_line_time - prev_time);
        }
        break;

    case VT_COL_LEVEL:
    {
        const char *level_name = ll->get_level_name();

        sqlite3_result_text(ctx,
                            level_name,
                            strlen(level_name),
                            SQLITE_STATIC);
    }
    break;

    case VT_COL_MARK:
    {
        sqlite3_result_int(ctx, ll->is_marked());
    }
    break;

        case VT_COL_LOG_COMMENT: {
            const map<content_line_t, bookmark_metadata> &bm = vt->lss->get_user_bookmark_metadata();

            auto bm_iter = bm.find(vt->lss->at(vc->log_cursor.lc_curr_line));
            if (bm_iter == bm.end() || bm_iter->second.bm_comment.empty()) {
                sqlite3_result_null(ctx);
            } else {
                const bookmark_metadata &meta = bm_iter->second;
                sqlite3_result_text(ctx,
                                    meta.bm_comment.c_str(),
                                    meta.bm_comment.length(),
                                    SQLITE_TRANSIENT);
            }
            break;
        }

        case VT_COL_LOG_TAGS: {
            const map<content_line_t, bookmark_metadata> &bm = vt->lss->get_user_bookmark_metadata();

            auto bm_iter = bm.find(vt->lss->at(vc->log_cursor.lc_curr_line));
            if (bm_iter == bm.end() || bm_iter->second.bm_tags.empty()) {
                sqlite3_result_null(ctx);
            } else {
                const bookmark_metadata &meta = bm_iter->second;

                yajlpp_gen gen;

                yajl_gen_config(gen, yajl_gen_beautify, false);

                {
                    yajlpp_array arr(gen);

                    for (const auto &str : meta.bm_tags) {
                        arr.gen(str);
                    }
                }

                string_fragment sf = gen.to_string_fragment();

                sqlite3_result_text(ctx,
                                    sf.data(),
                                    sf.length(),
                                    SQLITE_TRANSIENT);
                sqlite3_result_subtype(ctx, 'J');
            }
            break;
        }

        case VT_COL_FILTERS: {
            auto &filters = vt->lss->get_filters();
            auto &filter_state = ld->ld_filter_state;
            yajlpp_gen gen;

            yajl_gen_config(gen, yajl_gen_beautify, false);

            {
                yajlpp_array arr(gen);

                for (auto &filter : filters) {
                    if (filter->lf_deleted) {
                        continue;
                    }

                    uint32_t mask = (1UL << filter->get_index());

                    if (filter_state.lfo_filter_state.tfs_mask[line_number] &
                        mask) {
                        arr.gen(filter->get_index());
                    }
                }
            }

            to_sqlite(ctx, gen.to_string_fragment());
            sqlite3_result_subtype(ctx, 'J');
            break;
        }

    default:
        if (col > (VT_COL_MAX + vt->vi->vi_column_count - 1)) {
            int post_col_number = col -
                                  (VT_COL_MAX + vt->vi->vi_column_count -
                                   1) - 1;

            switch (post_col_number) {
                case 0: {
                    const string &fn = lf->get_filename();

                    sqlite3_result_text(ctx,
                                        fn.c_str(),
                                        fn.length(),
                                        SQLITE_STATIC);
                    break;
                }
                case 1: {
                    shared_buffer_ref line;

                    lf->read_full_message(ll, line);
                    sqlite3_result_text(ctx,
                                        line.get_data(),
                                        line.length(),
                                        SQLITE_TRANSIENT);
                    break;
                }
                case 2: {
                    if (vc->line_values.empty()) {
                        lf->read_full_message(ll, vc->log_msg);
                        vt->vi->extract(lf, vc->log_msg, vc->line_values);
                    }

                    struct line_range body_range;

                    body_range = find_string_attr_range(
                        vt->vi->vi_attrs, &textview_curses::SA_BODY);
                    if (!body_range.is_valid()) {
                        sqlite3_result_null(ctx);
                    }
                    else {
                        const char *msg_start = vc->log_msg.get_data();

                        sqlite3_result_text(ctx,
                                            &msg_start[body_range.lr_start],
                                            body_range.length(),
                                            SQLITE_TRANSIENT);
                    }
                    break;
                }
            }
        }
        else {
            if (vc->line_values.empty()) {
                lf->read_full_message(ll, vc->log_msg);
                vt->vi->extract(lf, vc->log_msg, vc->line_values);
            }

            size_t sub_col = col - VT_COL_MAX;
            std::vector<logline_value>::iterator lv_iter;

            lv_iter = find_if(vc->line_values.begin(), vc->line_values.end(),
                              logline_value_cmp(NULL, sub_col));

            if (lv_iter != vc->line_values.end()) {
                switch (lv_iter->lv_kind) {
                case logline_value::VALUE_NULL:
                    sqlite3_result_null(ctx);
                    break;
                case logline_value::VALUE_JSON: {
                    sqlite3_result_text(ctx,
                                        lv_iter->text_value(),
                                        lv_iter->text_length(),
                                        SQLITE_TRANSIENT);
                    sqlite3_result_subtype(ctx, 74);
                    break;
                }
                case logline_value::VALUE_STRUCT:
                case logline_value::VALUE_TEXT:
                case logline_value::VALUE_TIMESTAMP: {
                    sqlite3_result_text(ctx,
                                        lv_iter->text_value(),
                                        lv_iter->text_length(),
                                        SQLITE_TRANSIENT);
                    break;
                }
                case logline_value::VALUE_QUOTED:
                    if (lv_iter->lv_sbr.length() == 0) {
                        sqlite3_result_text(ctx, "", 0, SQLITE_STATIC);
                    }
                    else {
                        const char *text_value = lv_iter->lv_sbr.get_data();
                        size_t text_len = lv_iter->lv_sbr.length();

                        switch (text_value[0]) {
                        case '\'':
                        case '"': {
                            char *val = (char *)sqlite3_malloc(text_len);

                            if (val == NULL) {
                                sqlite3_result_error_nomem(ctx);
                            }
                            else {
                                size_t unquoted_len = unquote(val, text_value, text_len);
                                sqlite3_result_text(ctx, val, unquoted_len, sqlite3_free);
                            }
                            break;
                        }
                        default: {
                            sqlite3_result_text(ctx, text_value,
                                lv_iter->lv_sbr.length(), SQLITE_TRANSIENT);
                            break;
                        }
                        }
                    }
                    break;

                case logline_value::VALUE_BOOLEAN:
                case logline_value::VALUE_INTEGER:
                    sqlite3_result_int64(ctx, lv_iter->lv_value.i);
                    break;

                case logline_value::VALUE_FLOAT:
                    sqlite3_result_double(ctx, lv_iter->lv_value.d);
                    break;

                case logline_value::VALUE_UNKNOWN:
                case logline_value::VALUE__MAX:
                    require(0);
                    break;
                }
            }
            else {
                sqlite3_result_null(ctx);
            }
        }
        break;
    }

    return SQLITE_OK;
}

static int vt_rowid(sqlite3_vtab_cursor *cur, sqlite_int64 *p_rowid)
{
    vtab_cursor *p_cur = (vtab_cursor *)cur;

    *p_rowid = (((uint64_t)p_cur->log_cursor.lc_curr_line) << 8) |
               (p_cur->log_cursor.lc_sub_index & 0xff);

    return SQLITE_OK;
}

void log_cursor::update(unsigned char op, vis_line_t vl, bool exact)
{
    if (vl < 0) {
        vl = vis_line_t(-1);
    }
    switch (op) {
    case SQLITE_INDEX_CONSTRAINT_EQ:
        if (vl < this->lc_end_line) {
            this->lc_curr_line = vl;
            this->lc_end_line = vis_line_t(this->lc_curr_line + 1);
        }
        break;
    case SQLITE_INDEX_CONSTRAINT_GE:
        this->lc_curr_line = vl;
        break;
    case SQLITE_INDEX_CONSTRAINT_GT:
        this->lc_curr_line = vis_line_t(vl + (exact ? 1 : 0));
        break;
    case SQLITE_INDEX_CONSTRAINT_LE:
        this->lc_end_line = vis_line_t(vl + (exact ? 1 : 0));
        break;
    case SQLITE_INDEX_CONSTRAINT_LT:
        this->lc_end_line = vl;
        break;
    }
}

static int vt_filter(sqlite3_vtab_cursor *p_vtc,
                     int idxNum, const char *idxStr,
                     int argc, sqlite3_value **argv)
{
    vtab_cursor *p_cur = (vtab_cursor *)p_vtc;
    vtab *       vt = (vtab *)p_vtc->pVtab;
    sqlite3_index_info::sqlite3_index_constraint *index = (
        sqlite3_index_info::sqlite3_index_constraint *)idxStr;

    log_info("(%p) filter called: %d", vt, idxNum);
    p_cur->log_cursor.lc_curr_line = vis_line_t(-1);
    p_cur->log_cursor.lc_end_line = vis_line_t(vt->lss->text_line_count());
    vt_next(p_vtc);

    if (!idxNum) {
        return SQLITE_OK;
    }

    for (int lpc = 0; lpc < idxNum; lpc++) {
        switch (index[lpc].iColumn) {
        case VT_COL_LINE_NUMBER:
            p_cur->log_cursor.update(index[lpc].op,
                vis_line_t(sqlite3_value_int64(argv[lpc])));
            break;

        case VT_COL_LOG_TIME:
            if (sqlite3_value_type(argv[lpc]) == SQLITE3_TEXT) {
                const unsigned char *datestr = sqlite3_value_text(argv[lpc]);
                date_time_scanner dts;
                struct timeval tv;
                struct exttm mytm;
                vis_line_t vl;

                dts.scan((const char *)datestr, strlen((const char *)datestr), NULL, &mytm, tv);
                if ((vl = vt->lss->find_from_time(tv)) == -1) {
                    p_cur->log_cursor.lc_curr_line = p_cur->log_cursor.lc_end_line;
                }
                else {
                    p_cur->log_cursor.update(index[lpc].op, vl, false);
                }
            }
            break;

        }
    }

    while (!p_cur->log_cursor.is_eof() && !vt->vi->is_valid(p_cur->log_cursor, *vt->lss)) {
        p_cur->log_cursor.lc_curr_line += vis_line_t(1);
    }

    return SQLITE_OK;
}

static int vt_best_index(sqlite3_vtab *tab, sqlite3_index_info *p_info)
{
    std::vector<sqlite3_index_info::sqlite3_index_constraint> indexes;
    int argvInUse = 0;
    vtab *vt = (vtab *) tab;

    log_info("(%p) best index called: nConstraint=%d", tab, p_info->nConstraint);
    if (!vt->vi->vi_supports_indexes) {
        return SQLITE_OK;
    }
    for (int lpc = 0; lpc < p_info->nConstraint; lpc++) {
        if (!p_info->aConstraint[lpc].usable ||
            p_info->aConstraint[lpc].op == SQLITE_INDEX_CONSTRAINT_MATCH) {
            continue;
        }

        switch (p_info->aConstraint[lpc].iColumn) {
        case VT_COL_LINE_NUMBER:
            argvInUse += 1;
            indexes.push_back(p_info->aConstraint[lpc]);
            p_info->aConstraintUsage[lpc].argvIndex = argvInUse;
            break;
        }
    }

    if (!argvInUse) {
        for (int lpc = 0; lpc < p_info->nConstraint; lpc++) {
            if (!p_info->aConstraint[lpc].usable ||
                p_info->aConstraint[lpc].op == SQLITE_INDEX_CONSTRAINT_MATCH) {
                continue;
            }

            switch (p_info->aConstraint[lpc].iColumn) {
            case VT_COL_LOG_TIME:
                argvInUse += 1;
                indexes.push_back(p_info->aConstraint[lpc]);
                p_info->aConstraintUsage[lpc].argvIndex = argvInUse;
                break;
            }
        }
    }

    if (argvInUse) {
        sqlite3_index_info::sqlite3_index_constraint *index_copy;
        size_t len = indexes.size() * sizeof(*index_copy);

        log_info("found index, passing %d args", argvInUse);

        index_copy = (sqlite3_index_info::sqlite3_index_constraint *)
            sqlite3_malloc(len);
        if (!index_copy) {
            return SQLITE_NOMEM;
        }
        memcpy(index_copy, &indexes[0], len);
        p_info->idxNum = argvInUse;
        p_info->idxStr = (char *) index_copy;
        p_info->needToFreeIdxStr = 1;
        p_info->estimatedCost = 10.0;
    }

    return SQLITE_OK;
}

static struct json_path_handler tags_handler[] = {
    json_path_handler("#")
        .with_synopsis("<tag>")
        .with_description("A tag for the log line")
        .with_pattern(R"(^#[^\s]+$)")
        .FOR_FIELD(bookmark_metadata, bm_tags),

    json_path_handler()
};

static int vt_update(sqlite3_vtab *tab,
                     int argc,
                     sqlite3_value **argv,
                     sqlite_int64 *rowid_out)
{
    vtab *vt = (vtab *)tab;
    int retval = SQLITE_READONLY;

    if (argc > 1 && sqlite3_value_type(argv[0]) != SQLITE_NULL &&
        sqlite3_value_int64(argv[0]) == sqlite3_value_int64(argv[1])) {
        int64_t rowid = sqlite3_value_int64(argv[0]) >> 8;
        int val = sqlite3_value_int(argv[2 + VT_COL_MARK]);
        vis_line_t vrowid(rowid);

        std::map<content_line_t, bookmark_metadata> &bm = vt->lss->get_user_bookmark_metadata();
        const unsigned char *part_name = sqlite3_value_text(argv[2 + VT_COL_PARTITION]);
        const unsigned char *log_comment = sqlite3_value_text(argv[2 + VT_COL_LOG_COMMENT]);
        const unsigned char *log_tags = sqlite3_value_text(argv[2 + VT_COL_LOG_TAGS]);
        bookmark_metadata tmp_bm;

        if (log_tags) {
            vector<string> errors;
            yajlpp_parse_context ypc(log_vtab_data.lvd_source, tags_handler);
            auto_mem<yajl_handle_t> handle(yajl_free);

            handle = yajl_alloc(&ypc.ypc_callbacks, nullptr, &ypc);
            ypc.ypc_userdata = &errors;
            ypc.ypc_line_number = log_vtab_data.lvd_line_number;
            ypc.with_handle(handle)
                .with_error_reporter([](const yajlpp_parse_context &ypc,
                                        lnav_log_level_t level,
                                        const char *msg) {
                    vector<string> &errors = *((vector<string> *) ypc.ypc_userdata);
                    errors.emplace_back(msg);
                })
                .with_obj(tmp_bm);
            ypc.parse(log_tags, strlen((const char *) log_tags));
            ypc.complete_parse();
            if (!errors.empty()) {
                tab->zErrMsg = sqlite3_mprintf("%s",
                                               join(errors.begin(), errors.end(), "\n").c_str());
                return SQLITE_ERROR;
            }
        }

        bookmark_vector<vis_line_t> &bv = vt->tc->get_bookmarks()[
                &textview_curses::BM_META];
        bool has_meta = part_name != nullptr || log_comment != nullptr ||
            log_tags != nullptr;

        if (binary_search(bv.begin(), bv.end(), vrowid) && !has_meta) {
            vt->tc->set_user_mark(&textview_curses::BM_META, vrowid, false);
            bm.erase(vt->lss->at(vrowid));
        }

        if (has_meta) {
            bookmark_metadata &line_meta = bm[vt->lss->at(vrowid)];

            vt->tc->set_user_mark(&textview_curses::BM_META, vrowid, true);
            if (part_name) {
                line_meta.bm_name = string((const char *) part_name);
            } else {
                line_meta.bm_name.clear();
            }
            if (log_comment) {
                line_meta.bm_comment = string((const char *) log_comment);
            } else {
                line_meta.bm_comment.clear();
            }
            if (log_tags) {
                line_meta.bm_tags.clear();
                for (const auto &tag : tmp_bm.bm_tags) {
                    line_meta.add_tag(tag);
                }

                for (const auto &tag : line_meta.bm_tags) {
                    bookmark_metadata::KNOWN_TAGS.insert(tag);
                }
            } else {
                line_meta.bm_tags.clear();
            }
        }

        vt->tc->set_user_mark(&textview_curses::BM_USER, vrowid, val);
        rowid += 1;
        while ((size_t)rowid < vt->lss->text_line_count()) {
            vis_line_t vl(rowid);
            content_line_t cl = vt->lss->at(vl);
            logline *ll = vt->lss->find_line(cl);
            if (!ll->is_continued()) {
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
    0,              /* iVersion */
    vt_create,      /* xCreate       - create a vtable */
    vt_connect,     /* xConnect      - associate a vtable with a connection */
    vt_best_index,  /* xBestIndex    - best index */
    vt_disconnect,  /* xDisconnect   - disassociate a vtable with a connection */
    vt_destroy,     /* xDestroy      - destroy a vtable */
    vt_open,        /* xOpen         - open a cursor */
    vt_close,       /* xClose        - close a cursor */
    vt_filter,      /* xFilter       - configure scan constraints */
    vt_next,        /* xNext         - advance a cursor */
    vt_eof,         /* xEof          - inidicate end of result set*/
    vt_column,      /* xColumn       - read data */
    vt_rowid,       /* xRowid        - read data */
    vt_update,      /* xUpdate       - write data */
    NULL,           /* xBegin        - begin transaction */
    NULL,           /* xSync         - sync transaction */
    NULL,           /* xCommit       - commit transaction */
    NULL,           /* xRollback     - rollback transaction */
    NULL,           /* xFindFunction - function overloading */
};

static int progress_callback(void *ptr)
{
    int retval = 0;

    if (log_vtab_data.lvd_progress != NULL) {
        retval = log_vtab_data.lvd_progress(log_cursor_latest);
    }

    return retval;
}

log_vtab_manager::log_vtab_manager(sqlite3 *memdb,
                                   textview_curses &tc,
                                   logfile_sub_source &lss)
    : vm_db(memdb), vm_textview(tc), vm_source(lss)
{
    sqlite3_create_module(this->vm_db, "log_vtab_impl", &generic_vtab_module, this);
    sqlite3_progress_handler(memdb, 32, progress_callback, nullptr);
}

string log_vtab_manager::register_vtab(log_vtab_impl *vi)
{
    string retval;

    if (this->vm_impls.find(vi->get_name()) == this->vm_impls.end()) {
        auto_mem<char> errmsg(sqlite3_free);
        auto_mem<char> sql(sqlite3_free);
        int            rc;

        this->vm_impls[vi->get_name()] = vi;

        sql = sqlite3_mprintf("CREATE VIRTUAL TABLE %s "
                              "USING log_vtab_impl(%s)",
                              vi->get_name().get(),
                              vi->get_name().get());
        rc = sqlite3_exec(this->vm_db,
                          sql,
                          NULL,
                          NULL,
                          errmsg.out());
        if (rc != SQLITE_OK) {
            retval = errmsg;
        }
    }
    else {
        retval = "a table with the given name already exists";
    }

    return retval;
}

string log_vtab_manager::unregister_vtab(intern_string_t name)
{
    string retval = "";

    if (this->vm_impls.find(name) == this->vm_impls.end()) {
        retval = "unknown log line table -- " + name.to_string();
    }
    else {
        auto_mem<char> sql(sqlite3_free);
        __attribute((unused))
        int   rc;

        sql = sqlite3_mprintf("DROP TABLE %s ", name.get());
        rc  = sqlite3_exec(this->vm_db,
                           sql,
                           NULL,
                           NULL,
                           NULL);

        this->vm_impls.erase(name);
    }

    return retval;
}
