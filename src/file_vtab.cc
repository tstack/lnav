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

#include <string.h>
#include <unistd.h>

#include "base/injector.bind.hh"
#include "base/lnav.gzip.hh"
#include "base/lnav_log.hh"
#include "config.h"
#include "file_collection.hh"
#include "file_vtab.cfg.hh"
#include "log_format.hh"
#include "logfile.hh"
#include "session_data.hh"
#include "vtab_module.hh"
#include "vtab_module_json.hh"

namespace {

struct lnav_file : public tvt_iterator_cursor<lnav_file> {
    using iterator = std::vector<std::shared_ptr<logfile>>::iterator;

    static constexpr const char* NAME = "lnav_file";
    static constexpr const char* CREATE_STMT = R"(
-- Access lnav's open file list through this table.
CREATE TABLE lnav_file (
    device integer,       -- The device the file is stored on.
    inode integer,        -- The inode for the file on the device.
    filepath text,        -- The path to the file.
    mimetype text,        -- The MIME type for the file.
    content_id text,      -- The hash of some unique content in the file.
    format text,          -- The log file format for the file.
    lines integer,        -- The number of lines in the file.
    time_offset integer,  -- The millisecond offset for timestamps.
    options_path TEXT,    -- The matched path for the file options.
    options TEXT,         -- The effective options for the file.

    content BLOB HIDDEN   -- The contents of the file.
);
)";

    explicit lnav_file(file_collection& fc) : lf_collection(fc) {}

    iterator begin() { return this->lf_collection.fc_files.begin(); }

    iterator end() { return this->lf_collection.fc_files.end(); }

    int get_column(const cursor& vc, sqlite3_context* ctx, int col)
    {
        auto lf = *vc.iter;
        const struct stat& st = lf->get_stat();
        const auto& name = lf->get_filename();
        auto format = lf->get_format();
        const char* format_name = format != nullptr ? format->get_name().get()
                                                    : nullptr;

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
                to_sqlite(ctx, fmt::to_string(lf->get_text_format()));
                break;
            case 4:
                to_sqlite(
                    ctx,
                    fmt::format(FMT_STRING("v1:{}"), lf->get_content_id()));
                break;
            case 5:
                to_sqlite(ctx, format_name);
                break;
            case 6:
                to_sqlite(ctx, (int64_t) lf->size());
                break;
            case 7: {
                auto tv = lf->get_time_offset();
                int64_t ms = (tv.tv_sec * 1000LL) + tv.tv_usec / 1000LL;

                to_sqlite(ctx, ms);
                break;
            }
            case 8: {
                if (sqlite3_vtab_nochange(ctx)) {
                    return SQLITE_OK;
                }

                auto opts = lf->get_file_options();
                if (opts) {
                    to_sqlite(ctx, opts.value().first);
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            }
            case 9: {
                if (sqlite3_vtab_nochange(ctx)) {
                    return SQLITE_OK;
                }

                auto opts = lf->get_file_options();
                if (opts) {
                    to_sqlite(ctx, opts.value().second.to_json_string());
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            }
            case 10: {
                if (sqlite3_vtab_nochange(ctx)) {
                    return SQLITE_OK;
                }

                auto& cfg = injector::get<const file_vtab::config&>();
                auto lf_stat = lf->get_stat();

                if (lf_stat.st_size > cfg.fvc_max_content_size) {
                    sqlite3_result_error(ctx, "file is too large", -1);
                } else {
                    auto fd = lf->get_fd();
                    auto buf = auto_mem<char>::malloc(lf_stat.st_size);
                    auto rc = pread(fd, buf, lf_stat.st_size, 0);

                    if (rc == -1) {
                        auto errmsg
                            = fmt::format(FMT_STRING("unable to read file: {}"),
                                          strerror(errno));

                        sqlite3_result_error(
                            ctx, errmsg.c_str(), errmsg.length());
                    } else if (rc != lf_stat.st_size) {
                        auto errmsg = fmt::format(
                            FMT_STRING("short read of file: {} < {}"),
                            rc,
                            lf_stat.st_size);

                        sqlite3_result_error(
                            ctx, errmsg.c_str(), errmsg.length());
                    } else if (lnav::gzip::is_gzipped(buf, rc)) {
                        lnav::gzip::uncompress(lf->get_unique_path(), buf, rc)
                            .then([ctx](auto uncomp) {
                                auto pair = uncomp.release();

                                sqlite3_result_blob64(
                                    ctx, pair.first, pair.second, free);
                            })
                            .otherwise([ctx](auto msg) {
                                sqlite3_result_error(
                                    ctx, msg.c_str(), msg.size());
                            });
                    } else {
                        sqlite3_result_blob64(ctx, buf.release(), rc, free);
                    }
                }
                break;
            }
            default:
                ensure(0);
                break;
        }

        return SQLITE_OK;
    }

    int delete_row(sqlite3_vtab* vt, sqlite3_int64 rowid)
    {
        vt->zErrMsg = sqlite3_mprintf("Rows cannot be deleted from this table");
        return SQLITE_ERROR;
    }

    int insert_row(sqlite3_vtab* tab, sqlite3_int64& rowid_out)
    {
        tab->zErrMsg
            = sqlite3_mprintf("Rows cannot be inserted into this table");
        return SQLITE_ERROR;
    }

    int update_row(sqlite3_vtab* tab,
                   sqlite3_int64& rowid,
                   int64_t device,
                   int64_t inode,
                   std::string path,
                   const char* text_format,
                   const char* content_id,
                   const char* format,
                   int64_t lines,
                   int64_t time_offset,
                   const char* options_path,
                   const char* options,
                   const char* content)
    {
        auto lf = this->lf_collection.fc_files[rowid];
        struct timeval tv = {
            (int) (time_offset / 1000LL),
            (int) (time_offset / (1000LL * 1000LL)),
        };

        lf->adjust_content_time(0, tv, true);

        if (path != lf->get_filename()) {
            if (lf->is_valid_filename()) {
                throw sqlite_func_error(
                    "real file paths cannot be updated, only symbolic ones");
            }

            auto iter
                = this->lf_collection.fc_file_names.find(lf->get_filename());

            if (iter != this->lf_collection.fc_file_names.end()) {
                auto loo = iter->second;

                this->lf_collection.fc_file_names.erase(iter);

                loo.loo_include_in_session = true;
                this->lf_collection.fc_file_names[path] = loo;
            }

            lf->set_filename(path);
            lf->set_include_in_session(true);
            this->lf_collection.regenerate_unique_file_names();

            init_session();
            load_session();
        }

        return SQLITE_OK;
    }

    file_collection& lf_collection;
};

struct lnav_file_metadata {
    static constexpr const char* NAME = "lnav_file_metadata";
    static constexpr const char* CREATE_STMT = R"(
-- Access the metadata embedded in open files
CREATE TABLE lnav_file_metadata (
    filepath text,    -- The path to the file.
    descriptor text,  -- The descriptor that identifies the source of the metadata.
    mimetype text,    -- The MIME type of the metadata.
    content text      -- The metadata itself.
);
)";

    struct cursor {
        struct metadata_row {
            metadata_row(std::shared_ptr<logfile> lf, std::string desc)
                : mr_logfile(lf), mr_descriptor(std::move(desc))
            {
            }
            std::shared_ptr<logfile> mr_logfile;
            std::string mr_descriptor;
        };

        sqlite3_vtab_cursor base;
        lnav_file_metadata& c_meta;
        std::vector<metadata_row>::iterator c_iter;
        std::vector<metadata_row> c_rows;

        cursor(sqlite3_vtab* vt)
            : base({vt}),
              c_meta(((vtab_module<lnav_file_metadata>::vtab*) vt)->v_impl)
        {
            for (auto& lf : this->c_meta.lfm_collection.fc_files) {
                auto& lf_meta = lf->get_embedded_metadata();

                for (const auto& meta_pair : lf_meta) {
                    this->c_rows.emplace_back(lf, meta_pair.first);
                }
            }
        }

        ~cursor() { this->c_iter = this->c_rows.end(); }

        int next()
        {
            if (this->c_iter != this->c_rows.end()) {
                ++this->c_iter;
            }
            return SQLITE_OK;
        }

        int eof() { return this->c_iter == this->c_rows.end(); }

        int reset()
        {
            this->c_iter = this->c_rows.begin();
            return SQLITE_OK;
        }

        int get_rowid(sqlite3_int64& rowid_out)
        {
            rowid_out = this->c_iter - this->c_rows.begin();

            return SQLITE_OK;
        }
    };

    explicit lnav_file_metadata(file_collection& fc) : lfm_collection(fc) {}

    int get_column(const cursor& vc, sqlite3_context* ctx, int col)
    {
        auto& mr = *vc.c_iter;

        switch (col) {
            case 0:
                to_sqlite(ctx, mr.mr_logfile->get_filename());
                break;
            case 1:
                to_sqlite(ctx, mr.mr_descriptor);
                break;
            case 2:
                to_sqlite(
                    ctx,
                    fmt::to_string(
                        mr.mr_logfile->get_embedded_metadata()[mr.mr_descriptor]
                            .m_format));
                break;
            case 3:
                to_sqlite(
                    ctx,
                    fmt::to_string(
                        mr.mr_logfile->get_embedded_metadata()[mr.mr_descriptor]
                            .m_value));
                break;
            default:
                ensure(0);
                break;
        }

        return SQLITE_OK;
    }

    file_collection& lfm_collection;
};

struct injectable_lnav_file : vtab_module<lnav_file> {
    using vtab_module<lnav_file>::vtab_module;
    using injectable = injectable_lnav_file(file_collection&);
};

struct injectable_lnav_file_metadata
    : vtab_module<tvt_no_update<lnav_file_metadata>> {
    using vtab_module<tvt_no_update<lnav_file_metadata>>::vtab_module;
    using injectable = injectable_lnav_file_metadata(file_collection&);
};

static auto file_binder
    = injector::bind_multiple<vtab_module_base>().add<injectable_lnav_file>();

static auto file_meta_binder = injector::bind_multiple<vtab_module_base>()
                                   .add<injectable_lnav_file_metadata>();

}  // namespace
