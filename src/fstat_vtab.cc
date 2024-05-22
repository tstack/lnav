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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "fstat_vtab.hh"

#include <glob.h>
#include <grp.h>
#include <pwd.h>
#include <string.h>
#include <sys/stat.h>

#include "base/auto_mem.hh"
#include "base/fs_util.hh"
#include "base/injector.hh"
#include "base/lnav_log.hh"
#include "bound_tags.hh"
#include "config.h"
#include <filesystem>
#include "sql_help.hh"
#include "sql_util.hh"
#include "vtab_module.hh"

namespace {

enum {
    FSTAT_COL_PARENT,
    FSTAT_COL_NAME,
    FSTAT_COL_DEV,
    FSTAT_COL_INO,
    FSTAT_COL_TYPE,
    FSTAT_COL_MODE,
    FSTAT_COL_NLINK,
    FSTAT_COL_UID,
    FSTAT_COL_USER,
    FSTAT_COL_GID,
    FSTAT_COL_GROUP,
    FSTAT_COL_RDEV,
    FSTAT_COL_SIZE,
    FSTAT_COL_BLKSIZE,
    FSTAT_COL_BLOCKS,
    FSTAT_COL_ATIME,
    FSTAT_COL_MTIME,
    FSTAT_COL_CTIME,
    FSTAT_COL_ERROR,
    FSTAT_COL_PATTERN,
    FSTAT_COL_DATA,
};

/**
 * @feature f0:sql.tables.fstat
 */
struct fstat_table {
    static constexpr const char* NAME = "fstat";
    static constexpr const char* CREATE_STMT = R"(
-- The fstat() table-valued function allows you to query the file system.
CREATE TABLE fstat (
    st_parent TEXT,
    st_name TEXT,
    st_dev INTEGER,
    st_ino INTEGER,
    st_type TEXT,
    st_mode INTEGER,
    st_nlink INTEGER,
    st_uid TEXT,
    st_user TEXT,
    st_gid TEXT,
    st_group TEXT,
    st_rdev INTEGER,
    st_size INTEGER,
    st_blksize INTEGER,
    st_blocks INTEGER,
    st_atime DATETIME,
    st_mtime DATETIME,
    st_ctime DATETIME,
    error TEXT,
    pattern TEXT HIDDEN,
    data BLOB HIDDEN
);
)";

    struct cursor {
        sqlite3_vtab_cursor base;
        std::string c_pattern;
        static_root_mem<glob_t, globfree> c_glob;
        size_t c_path_index{0};
        struct stat c_stat;
        std::string c_error;

        explicit cursor(sqlite3_vtab* vt) : base({vt})
        {
            memset(&this->c_stat, 0, sizeof(this->c_stat));
        }

        void load_stat()
        {
            auto rc = lstat(this->c_glob->gl_pathv[this->c_path_index],
                            &this->c_stat);

            if (rc == -1) {
                this->c_error = strerror(errno);
            }
        }

        int next()
        {
            if (this->c_path_index < this->c_glob->gl_pathc) {
                this->c_path_index += 1;
                this->load_stat();
            }

            return SQLITE_OK;
        }

        int reset() { return SQLITE_OK; }

        int eof() { return this->c_path_index >= this->c_glob->gl_pathc; }

        int get_rowid(sqlite3_int64& rowid_out)
        {
            rowid_out = this->c_path_index;

            return SQLITE_OK;
        }
    };

    int get_column(const cursor& vc, sqlite3_context* ctx, int col)
    {
        const char* path = vc.c_glob->gl_pathv[vc.c_path_index];
        char time_buf[32];

        switch (col) {
            case FSTAT_COL_PARENT: {
                const char* slash = strrchr(path, '/');

                if (slash == nullptr) {
                    sqlite3_result_text(ctx, ".", 1, SQLITE_STATIC);
                } else if (path[1] == '\0') {
                    sqlite3_result_text(ctx, "", 0, SQLITE_STATIC);
                } else {
                    sqlite3_result_text(
                        ctx, path, slash - path + 1, SQLITE_TRANSIENT);
                }
                break;
            }
            case FSTAT_COL_NAME: {
                const char* slash = strrchr(path, '/');

                if (slash == nullptr) {
                    sqlite3_result_text(ctx, path, -1, SQLITE_TRANSIENT);
                } else {
                    sqlite3_result_text(ctx, slash + 1, -1, SQLITE_TRANSIENT);
                }
                break;
            }
            case FSTAT_COL_DEV:
                if (vc.c_error.empty()) {
                    sqlite3_result_int(ctx, vc.c_stat.st_dev);
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            case FSTAT_COL_INO:
                if (vc.c_error.empty()) {
                    sqlite3_result_int64(ctx, vc.c_stat.st_ino);
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            case FSTAT_COL_TYPE:
                if (!vc.c_error.empty()) {
                    sqlite3_result_null(ctx);
                } else if (S_ISREG(vc.c_stat.st_mode)) {
                    sqlite3_result_text(ctx, "reg", 3, SQLITE_STATIC);
                } else if (S_ISBLK(vc.c_stat.st_mode)) {
                    sqlite3_result_text(ctx, "blk", 3, SQLITE_STATIC);
                } else if (S_ISCHR(vc.c_stat.st_mode)) {
                    sqlite3_result_text(ctx, "chr", 3, SQLITE_STATIC);
                } else if (S_ISDIR(vc.c_stat.st_mode)) {
                    sqlite3_result_text(ctx, "dir", 3, SQLITE_STATIC);
                } else if (S_ISFIFO(vc.c_stat.st_mode)) {
                    sqlite3_result_text(ctx, "fifo", 4, SQLITE_STATIC);
                } else if (S_ISLNK(vc.c_stat.st_mode)) {
                    sqlite3_result_text(ctx, "lnk", 3, SQLITE_STATIC);
                } else if (S_ISSOCK(vc.c_stat.st_mode)) {
                    sqlite3_result_text(ctx, "sock", 3, SQLITE_STATIC);
                }
                break;
            case FSTAT_COL_MODE:
                if (vc.c_error.empty()) {
                    sqlite3_result_int(ctx, vc.c_stat.st_mode & 0777);
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            case FSTAT_COL_NLINK:
                if (vc.c_error.empty()) {
                    sqlite3_result_int(ctx, vc.c_stat.st_nlink);
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            case FSTAT_COL_UID:
                if (vc.c_error.empty()) {
                    sqlite3_result_int(ctx, vc.c_stat.st_uid);
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            case FSTAT_COL_USER: {
                if (vc.c_error.empty()) {
                    struct passwd* pw = getpwuid(vc.c_stat.st_uid);

                    if (pw != nullptr) {
                        sqlite3_result_text(
                            ctx, pw->pw_name, -1, SQLITE_TRANSIENT);
                    } else {
                        sqlite3_result_int(ctx, vc.c_stat.st_uid);
                    }
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            }
            case FSTAT_COL_GID:
                if (vc.c_error.empty()) {
                    sqlite3_result_int(ctx, vc.c_stat.st_gid);
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            case FSTAT_COL_GROUP: {
                if (vc.c_error.empty()) {
                    struct group* gr = getgrgid(vc.c_stat.st_gid);

                    if (gr != nullptr) {
                        sqlite3_result_text(
                            ctx, gr->gr_name, -1, SQLITE_TRANSIENT);
                    } else {
                        sqlite3_result_int(ctx, vc.c_stat.st_gid);
                    }
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            }
            case FSTAT_COL_RDEV:
                if (vc.c_error.empty()) {
                    sqlite3_result_int(ctx, vc.c_stat.st_rdev);
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            case FSTAT_COL_SIZE:
                if (vc.c_error.empty()) {
                    sqlite3_result_int64(ctx, vc.c_stat.st_size);
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            case FSTAT_COL_BLKSIZE:
                if (vc.c_error.empty()) {
                    sqlite3_result_int(ctx, vc.c_stat.st_blksize);
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            case FSTAT_COL_BLOCKS:
                if (vc.c_error.empty()) {
                    sqlite3_result_int(ctx, vc.c_stat.st_blocks);
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            case FSTAT_COL_ATIME:
                if (vc.c_error.empty()) {
                    sql_strftime(
                        time_buf, sizeof(time_buf), vc.c_stat.st_atime, 0);
                    sqlite3_result_text(ctx, time_buf, -1, SQLITE_TRANSIENT);
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            case FSTAT_COL_MTIME:
                if (vc.c_error.empty()) {
                    sql_strftime(
                        time_buf, sizeof(time_buf), vc.c_stat.st_mtime, 0);
                    sqlite3_result_text(ctx, time_buf, -1, SQLITE_TRANSIENT);
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            case FSTAT_COL_CTIME:
                if (vc.c_error.empty()) {
                    sql_strftime(
                        time_buf, sizeof(time_buf), vc.c_stat.st_ctime, 0);
                    sqlite3_result_text(ctx, time_buf, -1, SQLITE_TRANSIENT);
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            case FSTAT_COL_ERROR:
                if (vc.c_error.empty()) {
                    sqlite3_result_null(ctx);
                } else {
                    to_sqlite(ctx, vc.c_error);
                }
                break;
            case FSTAT_COL_PATTERN:
                sqlite3_result_text(ctx,
                                    vc.c_pattern.c_str(),
                                    vc.c_pattern.length(),
                                    SQLITE_TRANSIENT);
                break;
            case FSTAT_COL_DATA: {
                auto fs_path = std::filesystem::path{path};
                if (!vc.c_error.empty()) {
                    sqlite3_result_null(ctx);
                } else if (S_ISREG(vc.c_stat.st_mode)) {
                    auto open_res
                        = lnav::filesystem::open_file(fs_path, O_RDONLY);

                    if (open_res.isErr()) {
                        log_error("unable to read file: %s -- %s",
                                  path,
                                  open_res.unwrapErr().c_str());
                        sqlite3_result_null(ctx);
                    } else {
                        auto buffer = auto_buffer::alloc(vc.c_stat.st_size);
                        auto fd = open_res.unwrap();

                        while (true) {
                            if (buffer.available() == 0) {
                                buffer.expand_by(4096);
                            }
                            auto rc = read(fd,
                                           buffer.next_available(),
                                           buffer.available());

                            if (rc <= 0) {
                                break;
                            }
                            buffer.resize_by(rc);
                        }
                        to_sqlite(ctx, blob_auto_buffer{std::move(buffer)});
                    }
                } else if (S_ISLNK(vc.c_stat.st_mode)) {
                    auto link_path = std::filesystem::read_symlink(fs_path);

                    to_sqlite(ctx, link_path.string());
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            }
        }

        return SQLITE_OK;
    }

#if 0
    int update_row(sqlite3_vtab *tab,
                   sqlite3_int64 &index,
                   const char *st_parent,
                   const char *st_name,
                   int64_t st_dev,
                   int64_t st_ino,
                   const char *st_type,
                   int64_t st_mode,
                   int64_t st_nlink,
                   int64_t st_uid,
                   const char *st_user,
                   int64_t st_gid,
                   const char *st_group,
                   int64_t st_rdev,
                   int64_t st_size,
                   int64_t st_blksize,
                   int64_t st_blocks,
                   const char *atime,
                   const char *mtime,
                   const char *ctime,
                   const char *pattern) {

    };
#endif
};

static int
rcBestIndex(sqlite3_vtab* tab, sqlite3_index_info* pIdxInfo)
{
    vtab_index_constraints vic(pIdxInfo);
    vtab_index_usage viu(pIdxInfo);

    for (auto iter = vic.begin(); iter != vic.end(); ++iter) {
        if (iter->op != SQLITE_INDEX_CONSTRAINT_EQ) {
            continue;
        }

        switch (iter->iColumn) {
            case FSTAT_COL_PATTERN:
                viu.column_used(iter);
                break;
        }
    }

    viu.allocate_args(FSTAT_COL_PATTERN, FSTAT_COL_PATTERN, 1);
    return SQLITE_OK;
}

static int
rcFilter(sqlite3_vtab_cursor* pVtabCursor,
         int idxNum,
         const char* idxStr,
         int argc,
         sqlite3_value** argv)
{
    fstat_table::cursor* pCur = (fstat_table::cursor*) pVtabCursor;

    if (argc != 1) {
        pCur->c_pattern.clear();
        return SQLITE_OK;
    }

    const char* pattern = (const char*) sqlite3_value_text(argv[0]);
    pCur->c_pattern = pattern;

    auto glob_flags = GLOB_ERR;
    if (!lnav::filesystem::is_glob(pCur->c_pattern)) {
        glob_flags |= GLOB_NOCHECK;
    }

#ifdef GLOB_TILDE
    glob_flags |= GLOB_TILDE;
#endif

    switch (glob(pattern, glob_flags, nullptr, pCur->c_glob.inout())) {
        case GLOB_NOSPACE:
            pVtabCursor->pVtab->zErrMsg
                = sqlite3_mprintf("No space to perform glob()");
            return SQLITE_ERROR;
        case GLOB_NOMATCH:
            return SQLITE_OK;
    }

    pCur->load_stat();

    return SQLITE_OK;
}

}  // namespace

int
register_fstat_vtab(sqlite3* db)
{
    static vtab_module<tvt_no_update<fstat_table>> FSTAT_MODULE;
    static auto fstat_help
        = help_text("fstat",
                    "A table-valued function for getting information about "
                    "file paths/globs")
              .sql_table_valued_function()
              .with_parameter(
                  {"pattern", "The file path or glob pattern to query."})
              .with_result(
                  {"st_parent", "The parent path of the directory entry"})
              .with_result({"st_name", "The name of the directory entry"})
              .with_result({"st_dev", "The device number"})
              .with_result({"st_ino", "The inode number"})
              .with_result(help_text{"st_type", "The type of the entry"}
                               .with_enum_values({
                                   "reg",
                                   "blk",
                                   "chr",
                                   "dir",
                                   "fifo",
                                   "lnk",
                                   "sock",
                               }))
              .with_result({"st_mode", "The protection mode"})
              .with_result(
                  {"st_nlink", "The number of hard links to the entry"})
              .with_result({"st_uid", "The ID of the owning user"})
              .with_result({"st_user", "The user name"})
              .with_result({"st_gid", "The ID of the owning group"})
              .with_result({"st_group", "The group name"})
              .with_result({"st_rdev", "The device type"})
              .with_result({"st_size", "The size of the entry in bytes"})
              .with_result({"st_blksize", "The optimal size for I/O"})
              .with_result({"st_blocks", "Blocks allocated for the file"})
              .with_result({"st_atime", "The last access time"})
              .with_result({"st_mtime", "The last modified time"})
              .with_result({"st_ctime", "The creation time"})
              .with_result({"error",
                            "Error message if there was a problem looking up "
                            "the entry"})
              .with_result({"data", "The contents of the file"})
              .with_example(help_example{
                  "To read a file and raise an error if there is a problem",
                  "SELECT ifnull(data, raise_error('cannot read: ' || st_name, "
                  "error)) FROM fstat('/non-existent')",
              });

    int rc;

    FSTAT_MODULE.vm_module.xBestIndex = rcBestIndex;
    FSTAT_MODULE.vm_module.xFilter = rcFilter;

    static auto& lnav_flags = injector::get<unsigned long&, lnav_flags_tag>();

    if (lnav_flags & LNF_SECURE_MODE) {
        return SQLITE_OK;
    }

    rc = FSTAT_MODULE.create(db, "fstat");
    sqlite_function_help.emplace("fstat", &fstat_help);
    fstat_help.index_tags();

    ensure(rc == SQLITE_OK);

    return rc;
}
