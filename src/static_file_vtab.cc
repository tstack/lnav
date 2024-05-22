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

#include <map>

#include "static_file_vtab.hh"

#include <string.h>

#include "base/auto_mem.hh"
#include "base/fs_util.hh"
#include "base/lnav_log.hh"
#include "config.h"
#include <filesystem>
#include "lnav.hh"
#include "vtab_module.hh"

namespace {

struct static_file_vtab {
    sqlite3_vtab base;
    sqlite3* db;
};

struct static_file_info {
    std::filesystem::path sfi_path;
};

struct sf_vtab_cursor {
    sqlite3_vtab_cursor base;
    std::map<std::string, static_file_info>::iterator vc_files_iter;
    std::map<std::string, static_file_info> vc_files;
};

static int sfvt_destructor(sqlite3_vtab* p_svt);

static int
sfvt_create(sqlite3* db,
            void* pAux,
            int argc,
            const char* const* argv,
            sqlite3_vtab** pp_vt,
            char** pzErr)
{
    static_file_vtab* p_vt;

    /* Allocate the sqlite3_vtab/vtab structure itself */
    p_vt = (static_file_vtab*) sqlite3_malloc(sizeof(*p_vt));

    if (p_vt == nullptr) {
        return SQLITE_NOMEM;
    }

    memset(&p_vt->base, 0, sizeof(sqlite3_vtab));
    p_vt->db = db;

    *pp_vt = &p_vt->base;

    int rc = sqlite3_declare_vtab(db, STATIC_FILE_CREATE_STMT);

    return rc;
}

static int
sfvt_destructor(sqlite3_vtab* p_svt)
{
    static_file_vtab* p_vt = (static_file_vtab*) p_svt;

    /* Free the SQLite structure */
    sqlite3_free(p_vt);

    return SQLITE_OK;
}

static int
sfvt_connect(sqlite3* db,
             void* p_aux,
             int argc,
             const char* const* argv,
             sqlite3_vtab** pp_vt,
             char** pzErr)
{
    return sfvt_create(db, p_aux, argc, argv, pp_vt, pzErr);
}

static int
sfvt_disconnect(sqlite3_vtab* pVtab)
{
    return sfvt_destructor(pVtab);
}

static int
sfvt_destroy(sqlite3_vtab* p_vt)
{
    return sfvt_destructor(p_vt);
}

static int sfvt_next(sqlite3_vtab_cursor* cur);

static void
find_static_files(sf_vtab_cursor* p_cur, const std::filesystem::path& dir)
{
    auto& file_map = p_cur->vc_files;
    std::error_code ec;

    for (const auto& format_dir_entry :
         std::filesystem::directory_iterator(dir, ec))
    {
        if (!format_dir_entry.is_directory()) {
            continue;
        }
        auto format_static_files_dir = format_dir_entry.path() / "static-files";
        log_debug("format static files: %s", format_static_files_dir.c_str());
        for (const auto& static_file_entry :
             std::filesystem::recursive_directory_iterator(
                 format_static_files_dir, ec))
        {
            auto rel_path = std::filesystem::relative(static_file_entry.path(),
                                                      format_static_files_dir);

            file_map[rel_path.string()] = {static_file_entry.path()};
        }
    }
}

static int
sfvt_open(sqlite3_vtab* p_svt, sqlite3_vtab_cursor** pp_cursor)
{
    static_file_vtab* p_vt = (static_file_vtab*) p_svt;

    p_vt->base.zErrMsg = nullptr;

    sf_vtab_cursor* p_cur = (sf_vtab_cursor*) new sf_vtab_cursor();

    if (p_cur == nullptr) {
        return SQLITE_NOMEM;
    }

    *pp_cursor = (sqlite3_vtab_cursor*) p_cur;

    p_cur->base.pVtab = p_svt;

    for (const auto& config_path : lnav_data.ld_config_paths) {
        auto formats_root = config_path / "formats";
        log_debug("format root: %s", formats_root.c_str());
        find_static_files(p_cur, formats_root);
        auto configs_root = config_path / "configs";
        log_debug("configs root: %s", configs_root.c_str());
        find_static_files(p_cur, configs_root);
    }

    return SQLITE_OK;
}

static int
sfvt_close(sqlite3_vtab_cursor* cur)
{
    sf_vtab_cursor* p_cur = (sf_vtab_cursor*) cur;

    p_cur->vc_files_iter = p_cur->vc_files.end();
    /* Free cursor struct. */
    delete p_cur;

    return SQLITE_OK;
}

static int
sfvt_eof(sqlite3_vtab_cursor* cur)
{
    sf_vtab_cursor* vc = (sf_vtab_cursor*) cur;

    return vc->vc_files_iter == vc->vc_files.end();
}

static int
sfvt_next(sqlite3_vtab_cursor* cur)
{
    sf_vtab_cursor* vc = (sf_vtab_cursor*) cur;

    if (vc->vc_files_iter != vc->vc_files.end()) {
        ++vc->vc_files_iter;
    }

    return SQLITE_OK;
}

static int
sfvt_column(sqlite3_vtab_cursor* cur, sqlite3_context* ctx, int col)
{
    sf_vtab_cursor* vc = (sf_vtab_cursor*) cur;

    switch (col) {
        case 0:
            to_sqlite(ctx, vc->vc_files_iter->first);
            break;
        case 1: {
            sqlite3_result_text(ctx,
                                vc->vc_files_iter->second.sfi_path.c_str(),
                                -1,
                                SQLITE_TRANSIENT);
            break;
        }
        case 2: {
            auto read_res = lnav::filesystem::read_file(
                vc->vc_files_iter->second.sfi_path);
            if (read_res.isErr()) {
                auto um = lnav::console::user_message::error(
                              "unable to read static file")
                              .with_reason(read_res.unwrapErr());

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

static int
sfvt_rowid(sqlite3_vtab_cursor* cur, sqlite_int64* p_rowid)
{
    sf_vtab_cursor* p_cur = (sf_vtab_cursor*) cur;

    *p_rowid = std::distance(p_cur->vc_files.begin(), p_cur->vc_files_iter);

    return SQLITE_OK;
}

static int
sfvt_best_index(sqlite3_vtab* tab, sqlite3_index_info* p_info)
{
    return SQLITE_OK;
}

static int
sfvt_filter(sqlite3_vtab_cursor* cur,
            int idxNum,
            const char* idxStr,
            int argc,
            sqlite3_value** argv)
{
    sf_vtab_cursor* p_cur = (sf_vtab_cursor*) cur;

    p_cur->vc_files_iter = p_cur->vc_files.begin();
    return SQLITE_OK;
}

static sqlite3_module static_file_vtab_module = {
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
-- Access static files in the lnav configuration directories
CREATE TABLE lnav_static_files (
    name TEXT PRIMARY KEY,
    filepath TEXT,
    content BLOB HIDDEN
);
)";

int
register_static_file_vtab(sqlite3* db)
{
    auto_mem<char, sqlite3_free> errmsg;
    int rc;
    rc = sqlite3_create_module(
        db, "lnav_static_file_vtab_impl", &static_file_vtab_module, nullptr);
    ensure(rc == SQLITE_OK);
    if ((rc = sqlite3_exec(db,
                           "CREATE VIRTUAL TABLE lnav_static_files USING "
                           "lnav_static_file_vtab_impl()",
                           nullptr,
                           nullptr,
                           errmsg.out()))
        != SQLITE_OK)
    {
        fprintf(stderr,
                "unable to create lnav_static_file table %s\n",
                errmsg.in());
    }
    return rc;
}
