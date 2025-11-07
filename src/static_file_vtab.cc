/**
 * Copyright (c) 2022, Timothy Stack
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

#include <filesystem>
#include <map>
#include <optional>
#include <vector>

#include "static_file_vtab.hh"

#include <stdio.h>
#include <string.h>

#include "apps.cfg.hh"
#include "apps.hh"
#include "base/auto_mem.hh"
#include "base/fs_util.hh"
#include "base/lnav_log.hh"
#include "config.h"
#include "lnav.hh"
#include "vtab_module.hh"

namespace {

struct app_file_vtab {
    sqlite3_vtab base;
    sqlite3* db;
};

struct sf_vtab_cursor {
    sqlite3_vtab_cursor base;
    sqlite_int64 vc_rowid{0};
    std::vector<lnav::apps::app_files> vc_files;
    std::optional<size_t> vc_apps_index;
    std::optional<size_t> vc_files_index;
};

int sfvt_destructor(sqlite3_vtab* p_svt);

int
sfvt_create(sqlite3* db,
            void* pAux,
            int argc,
            const char* const* argv,
            sqlite3_vtab** pp_vt,
            char** pzErr)
{
    app_file_vtab* p_vt;

    /* Allocate the sqlite3_vtab/vtab structure itself */
    p_vt = (app_file_vtab*) sqlite3_malloc(sizeof(*p_vt));

    if (p_vt == nullptr) {
        return SQLITE_NOMEM;
    }

    memset(&p_vt->base, 0, sizeof(sqlite3_vtab));
    p_vt->db = db;

    *pp_vt = &p_vt->base;

    int rc = sqlite3_declare_vtab(db, STATIC_FILE_CREATE_STMT);

    return rc;
}

int
sfvt_destructor(sqlite3_vtab* p_svt)
{
    app_file_vtab* p_vt = (app_file_vtab*) p_svt;

    /* Free the SQLite structure */
    sqlite3_free(p_vt);

    return SQLITE_OK;
}

int
sfvt_connect(sqlite3* db,
             void* p_aux,
             int argc,
             const char* const* argv,
             sqlite3_vtab** pp_vt,
             char** pzErr)
{
    return sfvt_create(db, p_aux, argc, argv, pp_vt, pzErr);
}

int
sfvt_disconnect(sqlite3_vtab* pVtab)
{
    return sfvt_destructor(pVtab);
}

int
sfvt_destroy(sqlite3_vtab* p_vt)
{
    return sfvt_destructor(p_vt);
}

int sfvt_next(sqlite3_vtab_cursor* cur);

int
sfvt_open(sqlite3_vtab* p_svt, sqlite3_vtab_cursor** pp_cursor)
{
    auto p_vt = (app_file_vtab*) p_svt;

    p_vt->base.zErrMsg = nullptr;

    auto p_cur = new (std::nothrow) sf_vtab_cursor();
    if (p_cur == nullptr) {
        return SQLITE_NOMEM;
    }

    *pp_cursor = (sqlite3_vtab_cursor*) p_cur;

    p_cur->base.pVtab = p_svt;
    p_cur->vc_files = lnav::apps::find_app_files();
    log_info("opened app file vtab with %zu files", p_cur->vc_files.size());

    return SQLITE_OK;
}

int
sfvt_close(sqlite3_vtab_cursor* cur)
{
    auto* p_cur = (sf_vtab_cursor*) cur;

    /* Free cursor struct. */
    delete p_cur;

    return SQLITE_OK;
}

int
sfvt_eof(sqlite3_vtab_cursor* cur)
{
    auto* vc = (sf_vtab_cursor*) cur;

    return vc->vc_apps_index.value_or(0) == vc->vc_files.size();
}

int
sfvt_next(sqlite3_vtab_cursor* cur)
{
    auto* vc = (sf_vtab_cursor*) cur;

    if (!vc->vc_apps_index) {
        vc->vc_apps_index = 0;
    } else {
        if (!vc->vc_files_index) {
            vc->vc_files_index = 0;
        } else if (vc->vc_files_index.value() + 1
                   < vc->vc_files[vc->vc_apps_index.value()].af_files.size())
        {
            vc->vc_files_index = vc->vc_files_index.value() + 1;
        } else {
            vc->vc_files_index = std::nullopt;
            vc->vc_apps_index = vc->vc_apps_index.value() + 1;
            if (vc->vc_apps_index.value() < vc->vc_files.size()) {
                vc->vc_files_index = 0;
            }
        }
        vc->vc_rowid += 1;
    }

    return SQLITE_OK;
}

int
sfvt_column(sqlite3_vtab_cursor* cur, sqlite3_context* ctx, int col)
{
    auto* vc = (sf_vtab_cursor*) cur;
    const auto& af = vc->vc_files[vc->vc_apps_index.value()];
    const auto& fi = af.af_files[vc->vc_files_index.value()];

    switch (col) {
        case 0:
            to_sqlite(ctx, fi.fi_app_path);
            break;
        case 1:
            to_sqlite(ctx, af.af_name);
            break;
        case 2:
            to_sqlite(ctx, fi.fi_full_path);
            break;
        case 3: {
            auto read_res = lnav::filesystem::read_file(fi.fi_full_path);
            if (read_res.isErr()) {
                auto um = lnav::console::user_message::error(
                              "unable to read static file")
                              .with_reason(read_res.unwrapErr())
                              .move();

                to_sqlite(ctx, um);
            } else {
                auto str = read_res.unwrap();

                sqlite3_result_blob(
                    ctx, str.c_str(), str.size(), SQLITE_TRANSIENT);
            }
            break;
        }
    }

    return SQLITE_OK;
}

int
sfvt_rowid(sqlite3_vtab_cursor* cur, sqlite_int64* p_rowid)
{
    auto* p_cur = (sf_vtab_cursor*) cur;

    *p_rowid = p_cur->vc_rowid;

    return SQLITE_OK;
}

int
sfvt_best_index(sqlite3_vtab* tab, sqlite3_index_info* p_info)
{
    return SQLITE_OK;
}

int
sfvt_filter(sqlite3_vtab_cursor* cur,
            int idxNum,
            const char* idxStr,
            int argc,
            sqlite3_value** argv)
{
    auto* p_cur = (sf_vtab_cursor*) cur;

    p_cur->vc_apps_index = 0;
    p_cur->vc_files_index = 0;
    return SQLITE_OK;
}

const sqlite3_module static_file_vtab_module = {
    0, /* iVersion */
    sfvt_create, /* xCreate       - create a vtable */
    sfvt_connect, /* xConnect      - associate a vtable with a connection */
    sfvt_best_index, /* xBestIndex    - best index */
    sfvt_disconnect, /* xDisconnect   - disassociate a vtable with a connection
                      */
    sfvt_destroy, /* xDestroy      - destroy a vtable */
    sfvt_open, /* xOpen         - open a cursor */
    sfvt_close, /* xClose        - close a cursor */
    sfvt_filter, /* xFilter       - configure scan constraints */
    sfvt_next, /* xNext         - advance a cursor */
    sfvt_eof, /* xEof          - inidicate end of result set*/
    sfvt_column, /* xColumn       - read data */
    sfvt_rowid, /* xRowid        - read data */
    nullptr, /* xUpdate       - write data */
    nullptr, /* xBegin        - begin transaction */
    nullptr, /* xSync         - sync transaction */
    nullptr, /* xCommit       - commit transaction */
    nullptr, /* xRollback     - rollback transaction */
    nullptr, /* xFindFunction - function overloading */
};

}  // namespace

const char* const STATIC_FILE_CREATE_STMT = R"(
-- Access app files in the lnav configuration directories
CREATE TABLE lnav_app_files (
    name TEXT PRIMARY KEY,
    app TEXT,
    filepath TEXT,
    content BLOB HIDDEN
);
)";

struct lnav_apps_vtab {
    static constexpr const char* NAME = "lnav_apps";
    static constexpr const char* CREATE_STMT = R"(
CREATE TABLE lnav_apps (
    name TEXT PRIMARY KEY,
    description TEXT,
    root TEXT
);
)";

    struct cursor {
        struct app_info {
            std::string ai_name;
            lnav::apps::app_def ai_def;
        };

        sqlite3_vtab_cursor base;
        std::vector<app_info> c_apps;
        size_t c_index{0};

        explicit cursor(sqlite3_vtab* vt) : base({vt})
        {
            const auto& cfg = injector::get<lnav::apps::config&>();

            for (const auto& pd : cfg.c_publishers) {
                for (const auto& ad : pd.second.pd_apps) {
                    auto name
                        = fmt::format(FMT_STRING("{}/{}"), pd.first, ad.first);
                    this->c_apps.emplace_back(app_info{name, ad.second});
                }
            }
        }

        int next()
        {
            if (this->c_index < this->c_apps.size()) {
                this->c_index += 1;
            }

            return SQLITE_OK;
        }

        int reset()
        {
            this->c_index = 0;
            return SQLITE_OK;
        }

        int eof() { return this->c_index == this->c_apps.size(); }

        int get_rowid(sqlite3_int64& rowid_out)
        {
            rowid_out = this->c_index;

            return SQLITE_OK;
        }
    };

    int get_column(cursor& vc, sqlite3_context* ctx, int col)
    {
        const auto& ai = vc.c_apps[vc.c_index];
        switch (col) {
            case 0: {
                to_sqlite(ctx, ai.ai_name);
                break;
            }
            case 1: {
                to_sqlite(ctx, ai.ai_def.ad_description);
                break;
            }
            case 2: {
                const auto root = ai.ai_def.get_root_path();
                to_sqlite(ctx, root);
                break;
            }
        }

        return SQLITE_OK;
    }
};

int
register_static_file_vtab(sqlite3* db)
{
    auto_mem<char, sqlite3_free> errmsg;
    int rc = sqlite3_create_module(
        db, "lnav_app_file_vtab_impl", &static_file_vtab_module, nullptr);
    ensure(rc == SQLITE_OK);
    if ((rc = sqlite3_exec(db,
                           "CREATE VIRTUAL TABLE lnav_db.lnav_app_files USING "
                           "lnav_app_file_vtab_impl()",
                           nullptr,
                           nullptr,
                           errmsg.out()))
        != SQLITE_OK)
    {
        fprintf(
            stderr, "unable to create lnav_app_file table %s\n", errmsg.in());
    }

    static vtab_module<tvt_no_update<lnav_apps_vtab>> LNAV_APPS_MODULE;
    auto arc = LNAV_APPS_MODULE.create(db, "lnav_apps");
    ensure(arc == SQLITE_OK);
    return rc;
}
