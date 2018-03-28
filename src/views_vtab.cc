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

                    sql_strftime(timestamp, sizeof(timestamp), time_source->time_for_row(tc.get_top()), 0, 'T');

                    sqlite3_result_text(ctx, timestamp, -1, SQLITE_TRANSIENT);
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            }
            case 6: {
                const string &str = lnav_data.ld_last_search[view_index];

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
                time_t last_time = time_source->time_for_row(tc.get_top());

                if (tv.tv_sec != last_time) {
                    int row = time_source->row_for_time(tv.tv_sec);

                    tc.set_top(vis_line_t(row));
                }
            } else {
                tab->zErrMsg = sqlite3_mprintf("Invalid time: %s", top_time);
                return SQLITE_ERROR;
            }
        }
        tc.set_left(left);

        if (search != lnav_data.ld_last_search[index]) {
            execute_search((lnav_view_t) index, search);
        }

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
        return lnav_data.ld_view_stack.begin();
    }

    iterator end() {
        return lnav_data.ld_view_stack.end();
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
        if ((size_t)rowid != lnav_data.ld_view_stack.size() - 1) {
            tab->zErrMsg = sqlite3_mprintf(
                "Only the top view in the stack can be deleted");
            return SQLITE_ERROR;
        }

        lnav_data.ld_last_view = lnav_data.ld_view_stack.back();
        lnav_data.ld_view_stack.pop_back();
        if (!lnav_data.ld_view_stack.empty()) {
            textview_curses *tc = lnav_data.ld_view_stack.back();

            tc->set_needs_update();
            lnav_data.ld_view_stack_broadcaster.invoke(tc);
        }

        return SQLITE_OK;
    };

    int insert_row(sqlite3_vtab *tab,
                   sqlite3_int64 &rowid_out,
                   const char *name) {
        if (name == nullptr) {
            tab->zErrMsg = sqlite3_mprintf("'name' cannot be null");
            return SQLITE_ERROR;
        }

        auto view_name_iter = find_if(
            ::begin(lnav_view_strings), ::end(lnav_view_strings),
            [&](const char *v) {
                return v != nullptr && strcmp(v, name) == 0;
            });

        if (view_name_iter == ::end(lnav_view_strings)) {
            tab->zErrMsg = sqlite3_mprintf("Unknown view name: %s", name);
            return SQLITE_ERROR;
        }

        lnav_view_t view_index = lnav_view_t(view_name_iter - lnav_view_strings);
        textview_curses *tc = &lnav_data.ld_views[view_index];
        lnav_data.ld_view_stack.push_back(tc);
        tc->set_needs_update();
        lnav_data.ld_view_stack_broadcaster.invoke(tc);

        rowid_out = lnav_data.ld_view_stack.size() - 1;

        return SQLITE_OK;
    };

    int update_row(sqlite3_vtab *tab, sqlite3_int64 &index) {
        tab->zErrMsg = sqlite3_mprintf(
            "The lnav_view_stack table cannot be updated");
        return SQLITE_ERROR;
    };
};

int register_views_vtab(sqlite3 *db)
{
    static vtab_module<lnav_views> LNAV_VIEWS_MODULE;
    static vtab_module<lnav_view_stack> LNAV_VIEW_STACK_MODULE;

    int rc;

    rc = LNAV_VIEWS_MODULE.create(db, "lnav_views");
    assert(rc == SQLITE_OK);

    rc = LNAV_VIEW_STACK_MODULE.create(db, "lnav_view_stack");
    assert(rc == SQLITE_OK);

    return rc;
}
