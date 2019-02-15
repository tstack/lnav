/**
 * Copyright (c) 2015, Timothy Stack
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

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>

#include <utility>

#include "lnav.hh"
#include "lnav_log.hh"
#include "sql_util.hh"
#include "views_vtab.hh"
#include "view_curses.hh"

using namespace std;

template<>
struct from_sqlite<lnav_view_t> {
    inline lnav_view_t operator()(int argc, sqlite3_value **val, int argi) {
        const char *view_name = (const char *) sqlite3_value_text(val[argi]);

        if (view_name == nullptr) {
            throw from_sqlite_conversion_error("lnav view name", argi);
        }

        auto view_name_iter = find_if(
            ::begin(lnav_view_strings), ::end(lnav_view_strings),
            [&](const char *v) {
                return v != nullptr && strcasecmp(v, view_name) == 0;
            });

        if (view_name_iter == ::end(lnav_view_strings)) {
            throw from_sqlite_conversion_error("lnav view name", argi);
        }

        return lnav_view_t(view_name_iter - lnav_view_strings);
    }
};

template<>
struct from_sqlite<text_filter::type_t> {
    inline text_filter::type_t operator()(int argc, sqlite3_value **val, int argi) {
        const char *type_name = (const char *) sqlite3_value_text(val[argi]);

        if (strcasecmp(type_name, "in") == 0) {
            return text_filter::INCLUDE;
        } else if (strcasecmp(type_name, "out") == 0) {
            return text_filter::EXCLUDE;
        }

        throw from_sqlite_conversion_error("filter type", argi);
    }
};

template<>
struct from_sqlite<pair<string, pcre *>> {
    inline pair<string, pcre *>operator()(int argc, sqlite3_value **val, int argi) {
        const char *pattern = (const char *) sqlite3_value_text(val[argi]);
        const char *errptr;
        pcre *code;
        int eoff;

        if (pattern == nullptr || pattern[0] == '\0') {
            throw from_sqlite_conversion_error("non-empty pattern", argi);
        }

        code = pcre_compile(pattern,
                            PCRE_CASELESS,
                            &errptr,
                            &eoff,
                            nullptr);

        if (code == nullptr) {
            throw from_sqlite_conversion_error(errptr, argi);
        }

        return make_pair(string(pattern), code);
    }
};

struct lnav_views : public tvt_iterator_cursor<lnav_views> {
    struct vtab {
        sqlite3_vtab base;

        operator sqlite3_vtab *() {
            return &this->base;
        };
    };

    static constexpr const char *CREATE_STMT = R"(
-- Access lnav's views through this table.
CREATE TABLE lnav_views (
    -- The name of the view.
    name text PRIMARY KEY,
    -- The number of the line at the top of the view, starting from zero.
    top integer,
    -- The left position of the viewport.
    left integer,
    -- The height of the viewport.
    height integer,
    -- The number of lines in the view.
    inner_height integer,
    -- The time of the top line in the view, if the content is time-based.
    top_time datetime,
    -- The text to search for in the view.
    search text
);
)";

    using iterator = textview_curses *;

    iterator begin() {
        return std::begin(lnav_data.ld_views);
    }

    iterator end() {
        return std::end(lnav_data.ld_views);
    }

    int get_column(cursor &vc, sqlite3_context *ctx, int col) {
        lnav_view_t view_index = (lnav_view_t) distance(std::begin(lnav_data.ld_views), vc.iter);
        textview_curses &tc = *vc.iter;
        unsigned long width;
        vis_line_t height;

        tc.get_dimensions(height, width);
        switch (col) {
            case 0:
                sqlite3_result_text(ctx,
                                    lnav_view_strings[view_index], -1,
                                    SQLITE_STATIC);
                break;
            case 1:
                sqlite3_result_int(ctx, (int) tc.get_top());
                break;
            case 2:
                sqlite3_result_int(ctx, tc.get_left());
                break;
            case 3:
                sqlite3_result_int(ctx, height);
                break;
            case 4:
                sqlite3_result_int(ctx, tc.get_inner_height());
                break;
            case 5: {
                text_time_translator *time_source = dynamic_cast<text_time_translator *>(tc.get_sub_source());

                if (time_source != nullptr && tc.get_inner_height() > 0) {
                    char timestamp[64];

                    sql_strftime(timestamp, sizeof(timestamp), time_source->time_for_row(tc.get_top()), 'T');

                    sqlite3_result_text(ctx, timestamp, -1, SQLITE_TRANSIENT);
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            }
            case 6: {
                const string &str = tc.get_last_search();

                sqlite3_result_text(ctx, str.c_str(), str.length(), SQLITE_TRANSIENT);
                break;
            }
        }

        return SQLITE_OK;
    }

    int delete_row(sqlite3_vtab *tab, sqlite3_int64 rowid) {
        tab->zErrMsg = sqlite3_mprintf(
            "Rows cannot be deleted from the lnav_views table");
        return SQLITE_ERROR;
    }

    int insert_row(sqlite3_vtab *tab, sqlite3_int64 &rowid_out) {
        tab->zErrMsg = sqlite3_mprintf(
            "Rows cannot be inserted into the lnav_views table");
        return SQLITE_ERROR;
    };

    int update_row(sqlite3_vtab *tab,
                   sqlite3_int64 &index,
                   const char *name,
                   int64_t top_row,
                   int64_t left,
                   int64_t height,
                   int64_t inner_height,
                   const char *top_time,
                   const char *search) {
        textview_curses &tc = lnav_data.ld_views[index];
        text_time_translator *time_source = dynamic_cast<text_time_translator *>(tc.get_sub_source());

        if (tc.get_top() != top_row) {
            tc.set_top(vis_line_t(top_row));
        } else if (top_time != nullptr && time_source != nullptr) {
            date_time_scanner dts;
            struct timeval tv;

            if (dts.convert_to_timeval(top_time, -1, nullptr, tv)) {
                struct timeval last_time = time_source->time_for_row(tc.get_top());

                if (tv != last_time) {
                    int row = time_source->row_for_time(tv);

                    tc.set_top(vis_line_t(row));
                }
            } else {
                tab->zErrMsg = sqlite3_mprintf("Invalid time: %s", top_time);
                return SQLITE_ERROR;
            }
        }
        tc.set_left(left);
        tc.execute_search(search);

        return SQLITE_OK;
    };
};

struct lnav_view_stack : public tvt_iterator_cursor<lnav_view_stack> {
    using iterator = vector<textview_curses *>::iterator;

    static constexpr const char *CREATE_STMT = R"(
-- Access lnav's view stack through this table.
CREATE TABLE lnav_view_stack (
    name text
);
)";

    struct vtab {
        sqlite3_vtab base;

        operator sqlite3_vtab *() {
            return &this->base;
        };
    };

    iterator begin() {
        return lnav_data.ld_view_stack.vs_views.begin();
    }

    iterator end() {
        return lnav_data.ld_view_stack.vs_views.end();
    }

    int get_column(cursor &vc, sqlite3_context *ctx, int col) {
        textview_curses *tc = *vc.iter;
        lnav_view_t view = lnav_view_t(tc - lnav_data.ld_views);

        switch (col) {
            case 0:
                sqlite3_result_text(ctx,
                                    lnav_view_strings[view], -1,
                                    SQLITE_STATIC);
                break;
        }

        return SQLITE_OK;
    };

    int delete_row(sqlite3_vtab *tab, sqlite3_int64 rowid) {
        if ((size_t)rowid != lnav_data.ld_view_stack.vs_views.size() - 1) {
            tab->zErrMsg = sqlite3_mprintf(
                "Only the top view in the stack can be deleted");
            return SQLITE_ERROR;
        }

        lnav_data.ld_last_view = *lnav_data.ld_view_stack.top();
        lnav_data.ld_view_stack.vs_views.pop_back();
        if (!lnav_data.ld_view_stack.vs_views.empty()) {
            textview_curses *tc = *lnav_data.ld_view_stack.top();

            tc->set_needs_update();
            lnav_data.ld_view_stack_broadcaster.invoke(tc);
        }

        return SQLITE_OK;
    };

    int insert_row(sqlite3_vtab *tab,
                   sqlite3_int64 &rowid_out,
                   lnav_view_t view_index) {
        textview_curses *tc = &lnav_data.ld_views[view_index];

        ensure_view(tc);
        rowid_out = lnav_data.ld_view_stack.vs_views.size() - 1;

        return SQLITE_OK;
    };

    int update_row(sqlite3_vtab *tab, sqlite3_int64 &index) {
        tab->zErrMsg = sqlite3_mprintf(
            "The lnav_view_stack table cannot be updated");
        return SQLITE_ERROR;
    };
};

struct lnav_view_filters : public tvt_iterator_cursor<lnav_view_filters> {
    struct vtab {
        sqlite3_vtab base;

        operator sqlite3_vtab *() {
            return &this->base;
        };
    };

    struct iterator {
        using difference_type = int;
        using value_type = text_filter;
        using pointer = text_filter *;
        using reference = text_filter &;
        using iterator_category = forward_iterator_tag;

        lnav_view_t i_view_index;
        int i_filter_index;

        iterator(lnav_view_t view = LNV_LOG, int filter = -1)
            : i_view_index(view), i_filter_index(filter) {
        }

        iterator &operator++() {
            while (this->i_view_index < LNV__MAX) {
                textview_curses &tc = lnav_data.ld_views[this->i_view_index];
                text_sub_source *tss = tc.get_sub_source();

                if (tss == nullptr) {
                    this->i_view_index = lnav_view_t(this->i_view_index + 1);
                    continue;
                }

                filter_stack &fs = tss->get_filters();

                this->i_filter_index += 1;
                if (this->i_filter_index >= fs.size()) {
                    this->i_filter_index = -1;
                    this->i_view_index = lnav_view_t(this->i_view_index + 1);
                } else {
                    break;
                }
            }

            return *this;
        }

        bool operator==(const iterator &other) const {
            return this->i_view_index == other.i_view_index &&
                   this->i_filter_index == other.i_filter_index;
        }

        bool operator!=(const iterator &other) const {
            return !(*this == other);
        }
    };

    static constexpr const char *CREATE_STMT = R"(
-- Access lnav's filters through this table.
CREATE TABLE lnav_view_filters (
    view_name text,     -- The name of the view.
    filter_id integer,  -- The filter identifier.
    enabled integer,    -- Indicates if the filter is enabled/disabled.
    type text,          -- The type of filter (i.e. in/out).
    pattern text        -- The filter pattern.
);
)";

    iterator begin() {
        iterator retval = iterator();

        return ++retval;
    }

    iterator end() {
        return iterator(LNV__MAX, -1);
    }

    sqlite_int64 get_rowid(iterator iter) {
        textview_curses &tc = lnav_data.ld_views[iter.i_view_index];
        text_sub_source *tss = tc.get_sub_source();
        filter_stack &fs = tss->get_filters();
        auto &tf = *(fs.begin() + iter.i_filter_index);

        sqlite_int64 retval = iter.i_view_index;

        retval = retval << 32;
        retval = retval | tf->get_index();

        return retval;
    }

    int get_column(cursor &vc, sqlite3_context *ctx, int col) {
        textview_curses &tc = lnav_data.ld_views[vc.iter.i_view_index];
        text_sub_source *tss = tc.get_sub_source();
        filter_stack &fs = tss->get_filters();
        auto tf = *(fs.begin() + vc.iter.i_filter_index);

        switch (col) {
            case 0:
                sqlite3_result_text(ctx,
                                    lnav_view_strings[vc.iter.i_view_index], -1,
                                    SQLITE_STATIC);
                break;
            case 1:
                to_sqlite(ctx, tf->get_index());
                break;
            case 2:
                sqlite3_result_int(ctx, tf->is_enabled());
                break;
            case 3:
                switch (tf->get_type()) {
                    case text_filter::INCLUDE:
                        sqlite3_result_text(ctx, "in", 2, SQLITE_STATIC);
                        break;
                    case text_filter::EXCLUDE:
                        sqlite3_result_text(ctx, "out", 3, SQLITE_STATIC);
                        break;
                    default:
                        ensure(0);
                }
                break;
            case 4:
                sqlite3_result_text(ctx,
                                    tf->get_id().c_str(),
                                    -1,
                                    SQLITE_TRANSIENT);
                break;
        }

        return SQLITE_OK;
    }

    int insert_row(sqlite3_vtab *tab,
                   sqlite3_int64 &rowid_out,
                   lnav_view_t view_index,
                   int64_t _filter_id,
                   bool enabled,
                   text_filter::type_t type,
                   pair<string, pcre *> pattern) {
        textview_curses &tc = lnav_data.ld_views[view_index];
        text_sub_source *tss = tc.get_sub_source();
        filter_stack &fs = tss->get_filters();
        auto pf = make_shared<pcre_filter>(type,
                                           pattern.first,
                                           fs.next_index(),
                                           pattern.second);
        fs.add_filter(pf);
        tss->text_filters_changed();
        tc.set_needs_update();

        return SQLITE_OK;
    }

    int delete_row(sqlite3_vtab *tab, sqlite3_int64 rowid) {
        lnav_view_t view_index = lnav_view_t(rowid >> 32);
        int filter_index = rowid & 0xffffffffLL;
        textview_curses &tc = lnav_data.ld_views[view_index];
        text_sub_source *tss = tc.get_sub_source();
        filter_stack &fs = tss->get_filters();

        for (const auto &iter : fs) {
            if (iter->get_index() == filter_index) {
                fs.delete_filter(iter->get_id());
                tss->text_filters_changed();
                break;
            }
        }
        tc.set_needs_update();

        return SQLITE_OK;
    }

    int update_row(sqlite3_vtab *tab,
                   sqlite3_int64 &rowid,
                   lnav_view_t new_view_index,
                   int64_t new_filter_id,
                   bool enabled,
                   text_filter::type_t type,
                   pair<string, pcre *> pattern) {
        lnav_view_t view_index = lnav_view_t(rowid >> 32);
        int filter_index = rowid & 0xffffffffLL;
        textview_curses &tc = lnav_data.ld_views[view_index];
        text_sub_source *tss = tc.get_sub_source();
        filter_stack &fs = tss->get_filters();
        auto iter = fs.begin() + filter_index;
        shared_ptr<text_filter> tf = *iter;

        if (new_view_index != view_index) {
            tab->zErrMsg = sqlite3_mprintf(
                "The view for a filter cannot be changed");
            return SQLITE_ERROR;
        }

        tf->lf_deleted = true;
        tss->text_filters_changed();

        auto pf = make_shared<pcre_filter>(type,
                                           pattern.first,
                                           tf->get_index(),
                                           pattern.second);

        if (!enabled) {
            pf->disable();
        }

        *iter = pf;
        tss->text_filters_changed();
        tc.set_needs_update();

        return SQLITE_OK;
    };
};

int register_views_vtab(sqlite3 *db)
{
    static vtab_module<lnav_views> LNAV_VIEWS_MODULE;
    static vtab_module<lnav_view_stack> LNAV_VIEW_STACK_MODULE;
    static vtab_module<lnav_view_filters> LNAV_VIEW_FILTERS_MODULE;

    int rc;

    rc = LNAV_VIEWS_MODULE.create(db, "lnav_views");
    assert(rc == SQLITE_OK);

    rc = LNAV_VIEW_STACK_MODULE.create(db, "lnav_view_stack");
    assert(rc == SQLITE_OK);

    rc = LNAV_VIEW_FILTERS_MODULE.create(db, "lnav_view_filters");
    assert(rc == SQLITE_OK);

    return rc;
}
