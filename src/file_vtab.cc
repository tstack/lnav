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
    device integer,
    inode integer,
    filepath text,
    format text,
    lines integer
);
)";

    struct vtab {
        sqlite3_vtab base;

        operator sqlite3_vtab *() {
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
                sqlite3_result_int(ctx, st.st_dev);
                break;
            case 1:
                sqlite3_result_int(ctx, st.st_ino);
                break;
            case 2:
                sqlite3_result_text(ctx, name.c_str(), name.size(),
                                    SQLITE_TRANSIENT);
                break;
            case 3:
                if (format_name != nullptr) {
                    sqlite3_result_text(ctx, format_name, -1, SQLITE_STATIC);
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            case 4:
                sqlite3_result_int(ctx, lf->size());
                break;
        }

        return SQLITE_OK;
    }
};


int register_file_vtab(sqlite3 *db)
{
    static vtab_module<lnav_file> LNAV_FILE_MODULE;

    int rc;

    rc = LNAV_FILE_MODULE.create(db, "lnav_file");

    ensure(rc == SQLITE_OK);

    return rc;
}
