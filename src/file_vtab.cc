/**
 * Copyright (c) 2017, Timothy Stack
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

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "lnav.hh"
#include "auto_mem.hh"
#include "lnav_log.hh"
#include "sql_util.hh"
#include "file_vtab.hh"
#include "vtab_module.hh"

using namespace std;

struct lnav_file : public tvt_iterator_cursor<lnav_file> {
    using iterator = vector<shared_ptr<logfile>>::iterator;

    static constexpr const char *CREATE_STMT = R"(
-- Access lnav's open file list through this table.
CREATE TABLE lnav_file (
    device integer,       -- The device the file is stored on.
    inode integer,        -- The inode for the file on the device.
    filepath text,        -- The path to the file.
    format text,          -- The log file format for the file.
    lines integer,        -- The number of lines in the file.
    time_offset integer   -- The millisecond offset for timestamps.
);
)";

    struct vtab {
        sqlite3_vtab base;

        explicit operator sqlite3_vtab *() {
            return &this->base;
        };
    };

    iterator begin() {
        return lnav_data.ld_files.begin();
    }

    iterator end() {
        return lnav_data.ld_files.end();
    }

    int get_column(const cursor &vc, sqlite3_context *ctx, int col) {
        auto lf = *vc.iter;
        const struct stat &st = lf->get_stat();
        const string &name = lf->get_filename();
        log_format *format = lf->get_format();
        const char *format_name =
            format != nullptr ? format->get_name().get() : nullptr;

        switch (col) {
            case 0:
                to_sqlite(ctx, (int64_t) st.st_dev);
                break;
            case 1:
                to_sqlite(ctx, (int64_t) st.st_ino);
                break;
            case 2:
                to_sqlite(ctx, name);
                break;
            case 3:
                to_sqlite(ctx, format_name);
                break;
            case 4:
                to_sqlite(ctx, (int64_t) lf->size());
                break;
            case 5: {
                auto tv = lf->get_time_offset();
                int64_t ms = (tv.tv_sec * 1000LL) + tv.tv_usec / 1000LL;

                to_sqlite(ctx, ms);
                break;
            }
            default:
                ensure(0);
                break;
        }

        return SQLITE_OK;
    }

    int delete_row(sqlite3_vtab *vt, sqlite3_int64 rowid) {
        vt->zErrMsg = sqlite3_mprintf(
            "Rows cannot be deleted from this table");
        return SQLITE_ERROR;
    };

    int insert_row(sqlite3_vtab *tab, sqlite3_int64 &rowid_out) {
        tab->zErrMsg = sqlite3_mprintf(
            "Rows cannot be inserted into this table");
        return SQLITE_ERROR;
    };

    int update_row(sqlite3_vtab *tab,
                   sqlite3_int64 &rowid,
                   int64_t device,
                   int64_t inode,
                   const char *path,
                   const char *format,
                   int64_t lines,
                   int64_t time_offset) {
        auto lf = lnav_data.ld_files[rowid];
        struct timeval tv = {
            (int) (time_offset / 1000LL),
            (int) (time_offset / (1000LL * 1000LL)),
        };

        lf->adjust_content_time(0, tv, true);

        return SQLITE_OK;
    };
};


int register_file_vtab(sqlite3 *db)
{
    static vtab_module<lnav_file> LNAV_FILE_MODULE;

    int rc;

    rc = LNAV_FILE_MODULE.create(db, "lnav_file");

    ensure(rc == SQLITE_OK);

    return rc;
}
