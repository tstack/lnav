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

#include <cmath>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include <string.h>
#include <unistd.h>

#include "base/distributed_slice.hh"
#include "base/injector.bind.hh"
#include "base/lnav.gzip.hh"
#include "base/lnav_log.hh"
#include "config.h"
#include "file_collection.hh"
#include "file_vtab.cfg.hh"
#include "lnav.hh"
#include "log_format.hh"
#include "log_format_ext.hh"
#include "log_format_loader.hh"
#include "logfile.hh"
#include "session_data.hh"
#include "text_format.hh"
#include "vtab_module.hh"
#include "vtab_module_json.hh"
#include "yajlpp/yajlpp_def.hh"

namespace {

// Schema for the lnav_file.stats column.  Field names mirror the
// logfile_activity members (minus the `la_` prefix); the container's
// to_json_string() handles encoding + JSON_SUBTYPE tagging via
// vtab_module_json.hh's to_sqlite overload.
const typed_json_path_container<logfile_activity::index_stats>
    index_stats_handlers = {
        yajlpp::property_handler("wall-us").for_field(
            &logfile_activity::index_stats::is_wall_us),
        yajlpp::property_handler("cpu-us").for_field(
            &logfile_activity::index_stats::is_cpu_us),
        yajlpp::property_handler("memory-bytes")
            .for_field(&logfile_activity::index_stats::is_memory_bytes),
};

const typed_json_path_container<logfile_activity> activity_handlers = {
    yajlpp::property_handler("polls").for_field(&logfile_activity::la_polls),
    yajlpp::property_handler("reads").for_field(&logfile_activity::la_reads),
    yajlpp::property_handler("index")
        .for_child(&logfile_activity::la_index)
        .with_children(index_stats_handlers),
    yajlpp::property_handler("line-buffer-memory-bytes")
        .for_field(&logfile_activity::la_line_buffer_memory_bytes),
};

const char*
note_name(logfile::note_type nt)
{
    switch (nt) {
        case logfile::note_type::indexing_disabled:
            return "indexing-disabled";
        case logfile::note_type::duplicate:
            return "duplicate";
        case logfile::note_type::not_utf:
            return "not-utf";
        case logfile::note_type::line_limit:
            return "line-limit";
    }
    return "unknown";
}

/**
 * Write a message as plain text: the level, the message, and the reason and
 * help when there are any.
 */
void
gen_message(yajl_gen gen, const lnav::console::user_message& um)
{
    yajlpp_map obj(gen);

    obj.gen("level");
    obj.gen(lnav::console::user_message::level_name(um.um_level));
    obj.gen("message");
    obj.gen(um.um_message.get_string());
    if (!um.um_reason.empty()) {
        obj.gen("reason");
        obj.gen(um.um_reason.get_string());
    }
    if (!um.um_help.empty()) {
        obj.gen("help");
        obj.gen(um.um_help.get_string());
    }
}

/**
 * The lnav_file.details column: what the FILES view's details panel shows
 * about a file, other than what lnav_file has its own columns for.  Times
 * from the log messages are formatted like log_time.
 */
json_string
details_to_json(const logfile::details& fd)
{
    yajlpp_gen gen;
    yajl_gen_config(gen, yajl_gen_beautify, false);
    {
        yajlpp_map root(gen);

        root.gen("actual-path");
        if (fd.d_actual_path) {
            root.gen(fd.d_actual_path->string());
        } else {
            root.gen();
        }
        if (fd.d_glob_pattern) {
            root.gen("glob-pattern");
            root.gen(fd.d_glob_pattern.value());
        }
        if (fd.d_child_command) {
            const auto& child = fd.d_child_command.value();

            root.gen("child-command");
            yajlpp_map cmd(gen);
            cmd.gen("command");
            cmd.gen(child.cc_description);
            cmd.gen("running");
            cmd.gen(child.cc_running);
            cmd.gen("exit-status");
            if (child.cc_exit_status) {
                cmd.gen(child.cc_exit_status.value());
            } else {
                cmd.gen();
            }
        }
        root.gen("last-modified");
        root.gen(lnav::to_rfc3339_string(
            std::chrono::seconds{convert_log_time_to_local(fd.d_mtime)}, ' '));
        root.gen("size");
        root.gen((int64_t) fd.d_size);
        if (fd.d_compressed_size) {
            root.gen("compressed-size");
            root.gen((int64_t) fd.d_compressed_size.value());
        }
        if (fd.d_times) {
            const auto& times = fd.d_times.value();

            root.gen("time-range");
            {
                yajlpp_map tr(gen);
                tr.gen("begin");
                tr.gen(lnav::to_rfc3339_string(times.t_content.tr_begin, ' '));
                tr.gen("end");
                tr.gen(lnav::to_rfc3339_string(times.t_content.tr_end, ' '));
            }
            if (times.t_cutoff.has_lower_bound()
                || times.t_cutoff.has_upper_bound())
            {
                root.gen("cutoff");
                yajlpp_map cutoff(gen);
                if (times.t_cutoff.has_lower_bound()) {
                    cutoff.gen("from");
                    cutoff.gen(
                        lnav::to_rfc3339_string(times.t_cutoff.tr_begin, ' '));
                }
                if (times.t_cutoff.has_upper_bound()) {
                    cutoff.gen("to");
                    cutoff.gen(
                        lnav::to_rfc3339_string(times.t_cutoff.tr_end, ' '));
                }
            }
            root.gen("base-time");
            {
                yajlpp_map base(gen);
                base.gen("utc");
                base.gen(lnav::to_rfc3339_string(times.t_base_time, ' '));
                base.gen("source");
                times.t_base_time_source.match(
                    [&base](const logfile::file_line& fl) {
                        base.gen("line");
                        base.gen("line");
                        base.gen((int64_t) fl.value_);
                    },
                    [&base](const logfile::file_mtime&) {
                        base.gen("mtime");
                    },
                    [&base](const logfile::embedded_file_time&) {
                        base.gen("embedded");
                    });
            }
        }
        root.gen("provenance");
        root.gen(fmt::to_string(fd.d_provenance));
        root.gen("include-in-session");
        root.gen(fd.d_include_in_session);
        root.gen("detect-format");
        root.gen(fd.d_detect_format);
        if (fd.d_init_location.valid()) {
            root.gen("initial-location");
            root.gen(fd.d_init_location.match(
                [](default_for_text_format) { return std::string("default"); },
                [](file_location_tail) { return std::string("tail"); },
                [](int vl) { return fmt::format(FMT_STRING("L{}"), vl); },
                [](std::string section) { return section; }));
        }
        root.gen("format-match");
        {
            yajlpp_array msgs(gen);
            for (const auto& um : fd.d_format_match_messages) {
                gen_message(gen, um);
            }
        }
        if (!fd.d_decompress_error.empty()) {
            root.gen("error");
            root.gen(fd.d_decompress_error);
        }
        root.gen("notes");
        {
            yajlpp_array notes(gen);
            for (const auto& [note_kind, note_um] : fd.d_notes.entries()) {
                yajlpp_map note(gen);
                note.gen("kind");
                note.gen(note_name(note_kind));
                note.gen("message");
                note.gen(note_um.um_message.get_string());
                if (!note_um.um_reason.empty()) {
                    note.gen("reason");
                    note.gen(note_um.um_reason.get_string());
                }
            }
        }
        if (fd.d_invalid_lines.ili_total > 0) {
            root.gen("invalid-lines");
            yajlpp_map ili(gen);
            ili.gen("count");
            ili.gen((int64_t) fd.d_invalid_lines.ili_total);
            ili.gen("first");
            {
                yajlpp_array lines(gen);
                for (auto line : fd.d_invalid_lines.ili_lines) {
                    lines.gen((int64_t) line);
                }
            }
        }
    }

    return json_string(gen);
}

struct lnav_file : tvt_iterator_cursor<lnav_file> {
    using iterator = std::vector<std::shared_ptr<logfile>>::iterator;

    static constexpr const char* NAME = "lnav_file";
    static constexpr const char* CREATE_STMT = R"(
-- Access lnav's open file list through this table.
-- Tags: io
CREATE TABLE lnav_db.lnav_file (
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
    stats TEXT,           -- JSON-encoded indexing statistics for the file.
    archive_path TEXT,    -- The archive the file was extracted from, if any.
    archive_member TEXT,  -- The path of the file within its archive.
    details TEXT,         -- JSON-encoded details from the files panel.

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
        const auto* format = lf->get_format_ptr();
        std::optional<intern_string_t> format_name;
        if (format != nullptr) {
            format_name = format->get_name();
        }

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
                to_sqlite(ctx,
                          fmt::to_string(lf->get_text_format().value_or(
                              text_format_t::TF_BINARY)));
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
            case 10:
                to_sqlite(ctx,
                          activity_handlers.to_json_string(lf->get_activity()));
                break;
            case 11: {
                const auto& archive = lf->get_open_options().loo_archive_path;
                if (archive) {
                    to_sqlite(ctx, archive->string());
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            }
            case 12: {
                const auto& member = lf->get_open_options().loo_archive_member;
                if (member) {
                    to_sqlite(ctx, member->string());
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            }
            case 13: {
                if (sqlite3_vtab_nochange(ctx)) {
                    return SQLITE_OK;
                }

                to_sqlite(ctx, details_to_json(lf->get_details()));
                break;
            }
            case 14: {
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
                   const char* stats,
                   const char* archive_path,
                   const char* archive_member,
                   const char* details,
                   const char* content)
    {
        auto lf = this->lf_collection.fc_files[rowid];

        // Where the file came from isn't something that can be changed.
        const auto& open_opts = lf->get_open_options();
        auto same_path = [](const char* value,
                            const std::optional<std::filesystem::path>& curr) {
            if (value == nullptr || curr == std::nullopt) {
                return value == nullptr && curr == std::nullopt;
            }
            return curr->string() == value;
        };
        if (!same_path(archive_path, open_opts.loo_archive_path)
            || !same_path(archive_member, open_opts.loo_archive_member))
        {
            throw sqlite_func_error(
                "the archive_path and archive_member columns cannot be "
                "updated");
        }
        // Round toward negative infinity so the microseconds stay positive,
        // which is how a timeval is kept.
        auto offset_secs = time_offset / 1000LL;
        auto offset_msecs = time_offset % 1000LL;
        if (offset_msecs < 0) {
            offset_secs -= 1;
            offset_msecs += 1000LL;
        }
        struct timeval tv = {
            (time_t) offset_secs,
            (suseconds_t) (offset_msecs * 1000LL),
        };

        // Every update passes all of the columns, so only re-time the file
        // when the offset was actually changed.
        if (tv != lf->get_time_offset()) {
            lf->adjust_content_time(0, tv, true);
        }

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
            load_time_bookmarks();
        }

        return SQLITE_OK;
    }

    file_collection& lf_collection;
};

struct lnav_file_metadata {
    static constexpr const char* NAME = "lnav_file_metadata";
    static constexpr const char* CREATE_STMT = R"(
-- Access the metadata embedded in open files
-- Tags: io, metadata
CREATE TABLE lnav_db.lnav_file_metadata (
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
              c_meta(
                  ((vtab_module<tvt_no_update<lnav_file_metadata>>::vtab*) vt)
                      ->v_impl)
        {
            for (auto& lf : this->c_meta.lfm_collection.fc_files) {
                auto& lf_meta = lf->get_embedded_metadata();

                for (const auto& meta_pair : lf_meta) {
                    this->c_rows.emplace_back(lf, meta_pair.first);
                }
            }
        }

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
        // The descriptors were collected when the cursor was opened, so the
        // metadata might not have one anymore.  Look it up without adding it.
        const auto& lf_meta
            = std::as_const(*mr.mr_logfile).get_embedded_metadata();
        const auto meta_iter = lf_meta.find(mr.mr_descriptor);

        switch (col) {
            case 0:
                to_sqlite(ctx, mr.mr_logfile->get_filename());
                break;
            case 1:
                to_sqlite(ctx, mr.mr_descriptor);
                break;
            case 2:
                if (meta_iter == lf_meta.end()) {
                    sqlite3_result_null(ctx);
                } else {
                    to_sqlite(ctx, fmt::to_string(meta_iter->second.m_format));
                }
                break;
            case 3:
                if (meta_iter == lf_meta.end()) {
                    sqlite3_result_null(ctx);
                } else {
                    to_sqlite(ctx, fmt::to_string(meta_iter->second.m_value));
                }
                break;
            default:
                ensure(0);
                break;
        }

        return SQLITE_OK;
    }

    file_collection& lfm_collection;
};

/**
 * @return The name of the kind, as it is written in a format file.  The
 *   kinds that a format file cannot use are given the name of the closest
 *   kind it can.
 */
static string_fragment
value_kind_name(value_kind_t kind)
{
    switch (kind) {
        case value_kind_t::VALUE_W3C_QUOTED:
            kind = value_kind_t::VALUE_QUOTED;
            break;
        case value_kind_t::VALUE_UNKNOWN:
        case value_kind_t::VALUE_NULL:
        case value_kind_t::VALUE__MAX:
            kind = value_kind_t::VALUE_ANY;
            break;
        default:
            break;
    }
    for (const auto* ev = VALUE_KIND_ENUM; !ev->first.empty(); ++ev) {
        if (ev->second == (int) kind) {
            return ev->first;
        }
    }

    return "any"_frag;
}

struct lnav_format_values {
    static constexpr const char* NAME = "lnav_format_values";
    static constexpr const char* CREATE_STMT = R"(
-- The values (fields) that the loaded log formats extract from messages.  Each
-- is a column of the format's own table, named after the format (e.g. the "xid"
-- value of format "fullsail_log" is fullsail_log.xid), which has the messages
-- of that format; all_logs.log_format says which formats have messages.  Only
-- the values declared in a format's "value" section are listed, so a field the
-- format uses without declaring it, like an undeclared level-field, has no row.
-- Tags: metadata
CREATE TABLE lnav_db.lnav_format_values (
    format text,         -- The name of the log format.
    name text,           -- The name of the value.
    kind text,           -- The kind of value, e.g. 'string' or 'integer'.
    unit_suffix text,    -- The suffix used when humanizing the value, e.g. 's' or 'B'.
    unit_divisor real,   -- What the raw value is divided by to get the base unit implied by the suffix.
    identifier integer,  -- 1 when the value names something, like an ID, so grouping by it makes sense.
    roles text,          -- A JSON array of what the format uses the value for, e.g. ["opid"]: timestamp, start-timestamp, subsecond, time, level, body, opid, subid, thread-id, src-file, src-line, src-location, or duration.  The opid and thread-id ones are also all_logs.log_opid and log_thread_id.
    description text     -- What the value is, as the format describes it, or NULL.
);
)";

    struct cursor {
        struct value_row {
            intern_string_t vr_format;
            logline_value_meta vr_meta;
            /** What the format uses this value for, e.g. "opid". */
            std::vector<const char*> vr_roles;
            /** Owned by the format, which outlives the query. */
            const std::string* vr_description{nullptr};
        };

        sqlite3_vtab_cursor base;
        std::vector<value_row>::iterator c_iter;
        std::vector<value_row> c_rows;

        explicit cursor(sqlite3_vtab* vt) : base({vt})
        {
            for (const auto& format : log_format::get_root_formats()) {
                // Only formats defined in files say what their values are for.
                const auto* ext
                    = dynamic_cast<const external_log_format*>(format.get());
                for (auto& meta : format->get_value_metadata()) {
                    value_row row{format->get_name(), std::move(meta)};
                    const auto& name = row.vr_meta.lvm_name;
                    const std::pair<const char*, intern_string_t> roles[] = {
                        {"timestamp", format->lf_timestamp_field},
                        {"start-timestamp", format->lf_start_timestamp_field},
                        {"subsecond", format->lf_subsecond_field},
                        {"time", format->lf_time_field},
                    };
                    for (const auto& [role, field] : roles) {
                        if (!field.empty() && field == name) {
                            row.vr_roles.emplace_back(role);
                        }
                    }
                    if (ext != nullptr) {
                        const std::pair<const char*, intern_string_t>
                            ext_roles[] = {
                                {"level", ext->elf_level_field},
                                {"body", ext->elf_body_field},
                                {"opid", ext->elf_opid_field},
                                {"subid", ext->elf_subid_field},
                                {"thread-id", ext->elf_thread_id_field},
                                {"src-file", ext->elf_src_file_field},
                                {"src-line", ext->elf_src_line_field},
                                {"src-location", ext->elf_src_loc_field},
                                {"duration", ext->elf_duration_field},
                            };
                        for (const auto& [role, field] : ext_roles) {
                            if (!field.empty() && field == name) {
                                row.vr_roles.emplace_back(role);
                            }
                        }
                        auto vd_iter = ext->elf_value_defs.find(name);
                        if (vd_iter != ext->elf_value_defs.end()) {
                            row.vr_description
                                = &vd_iter->second->vd_description;
                        }
                    }
                    this->c_rows.emplace_back(std::move(row));
                }
            }
        }

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

    int get_column(const cursor& vc, sqlite3_context* ctx, int col)
    {
        const auto& vr = *vc.c_iter;
        const auto& meta = vr.vr_meta;

        switch (col) {
            case 0:
                to_sqlite(ctx, vr.vr_format);
                break;
            case 1:
                to_sqlite(ctx, meta.lvm_name);
                break;
            case 2:
                to_sqlite(ctx, value_kind_name(meta.lvm_kind));
                break;
            case 3:
                if (meta.lvm_unit_suffix.empty()) {
                    sqlite3_result_null(ctx);
                } else {
                    to_sqlite(ctx, meta.lvm_unit_suffix);
                }
                break;
            case 4:
                to_sqlite(ctx, meta.lvm_unit_divisor);
                break;
            case 5:
                to_sqlite(ctx, meta.lvm_identifier);
                break;
            case 6: {
                yajlpp_gen gen;
                {
                    yajlpp_array arr(gen);
                    for (const auto* role : vr.vr_roles) {
                        arr.gen(role);
                    }
                }
                to_sqlite(ctx, json_string(gen));
                break;
            }
            case 7:
                if (vr.vr_description == nullptr
                    || vr.vr_description->empty())
                {
                    sqlite3_result_null(ctx);
                } else {
                    to_sqlite(ctx, *vr.vr_description);
                }
                break;
            default:
                ensure(0);
                break;
        }

        return SQLITE_OK;
    }
};

/**
 * The columns that the value stats tables have in common, in order, starting
 * from the count.
 */
enum class value_stats_col {
    count,
    text_count,
    min,
    max,
    mean,
    p50,
    p90,
    p99,
    distinct_estimate,
};

void
value_stats_to_sqlite(sqlite3_context* ctx,
                      const logline_value_stats* stats,
                      value_stats_col col)
{
    const auto has_numbers = stats != nullptr && stats->lvs_count > 0;
    auto quantile = [&](double p) {
        if (has_numbers && stats->lvs_tdigest.has_value()) {
            to_sqlite(ctx, stats->lvs_tdigest->quantile(p));
        } else {
            sqlite3_result_null(ctx);
        }
    };

    switch (col) {
        case value_stats_col::count:
            if (stats == nullptr) {
                sqlite3_result_null(ctx);
            } else {
                to_sqlite(ctx, stats->lvs_count);
            }
            break;
        case value_stats_col::text_count:
            if (stats == nullptr) {
                sqlite3_result_null(ctx);
            } else {
                to_sqlite(ctx, stats->lvs_text_count);
            }
            break;
        case value_stats_col::min:
            if (has_numbers) {
                to_sqlite(ctx, stats->lvs_min_value);
            } else {
                sqlite3_result_null(ctx);
            }
            break;
        case value_stats_col::max:
            if (has_numbers) {
                to_sqlite(ctx, stats->lvs_max_value);
            } else {
                sqlite3_result_null(ctx);
            }
            break;
        case value_stats_col::mean:
            if (has_numbers) {
                to_sqlite(ctx, stats->lvs_total / stats->lvs_count);
            } else {
                sqlite3_result_null(ctx);
            }
            break;
        case value_stats_col::p50:
            quantile(50);
            break;
        case value_stats_col::p90:
            quantile(90);
            break;
        case value_stats_col::p99:
            quantile(99);
            break;
        case value_stats_col::distinct_estimate: {
            auto est = stats != nullptr ? stats->distinct_estimate()
                                        : std::nullopt;
            if (est) {
                to_sqlite(ctx, (int64_t) std::llround(est.value()));
            } else {
                sqlite3_result_null(ctx);
            }
            break;
        }
    }
}

struct lnav_file_value_stats {
    static constexpr const char* NAME = "lnav_file_value_stats";
    static constexpr const char* CREATE_STMT = R"(
-- Statistics for the values in each open log file.  The numbers are raw
-- values, divide them by the unit_divisor in lnav_format_values to get the
-- base unit.
-- Tags: metadata
CREATE TABLE lnav_db.lnav_file_value_stats (
    filepath text,              -- The path to the file.
    format text,                -- The name of the file's log format.
    name text,                  -- The name of the value.
    count integer,              -- The number of numeric values seen.
    text_count integer,         -- The number of non-numeric values seen.
    min real,                   -- The smallest numeric value.
    max real,                   -- The largest numeric value.
    mean real,                  -- The mean of the numeric values.
    p50 real,                   -- The estimated median of the numeric values.
    p90 real,                   -- The estimated 90th percentile of the numeric values.
    p99 real,                   -- The estimated 99th percentile of the numeric values.
    distinct_estimate integer   -- The estimated number of distinct non-numeric values.
);
)";

    struct cursor {
        struct stats_row {
            std::shared_ptr<logfile> sr_logfile;
            intern_string_t sr_format;
            intern_string_t sr_name;
            size_t sr_index;
        };

        sqlite3_vtab_cursor base;
        std::vector<stats_row>::iterator c_iter;
        std::vector<stats_row> c_rows;

        cursor(sqlite3_vtab* vt) : base({vt})
        {
            const auto& fc
                = ((vtab_module<tvt_no_update<lnav_file_value_stats>>::vtab*)
                       vt)
                      ->v_impl.lfvs_collection;

            for (const auto& lf : fc.fc_files) {
                const auto* format = lf->get_format_ptr();
                if (format == nullptr) {
                    continue;
                }

                const auto stats_count = lf->get_value_stats().size();
                for (const auto& meta : format->get_value_metadata()) {
                    if (!meta.lvm_values_index
                        || meta.lvm_values_index.value() >= stats_count)
                    {
                        continue;
                    }
                    this->c_rows.emplace_back(
                        stats_row{lf,
                                  format->get_name(),
                                  meta.lvm_name,
                                  meta.lvm_values_index.value()});
                }
            }
        }

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

    explicit lnav_file_value_stats(file_collection& fc) : lfvs_collection(fc) {}

    int get_column(const cursor& vc, sqlite3_context* ctx, int col)
    {
        const auto& sr = *vc.c_iter;
        // The rows were collected when the cursor was opened, so check that
        // the file still has the stats.
        const auto& all_stats = sr.sr_logfile->get_value_stats();
        const auto* stats = sr.sr_index < all_stats.size()
            ? &all_stats[sr.sr_index]
            : nullptr;

        switch (col) {
            case 0:
                to_sqlite(ctx, sr.sr_logfile->get_filename());
                break;
            case 1:
                to_sqlite(ctx, sr.sr_format);
                break;
            case 2:
                to_sqlite(ctx, sr.sr_name);
                break;
            default:
                value_stats_to_sqlite(ctx, stats, value_stats_col(col - 3));
                break;
        }

        return SQLITE_OK;
    }

    file_collection& lfvs_collection;
};

struct lnav_format_value_stats {
    static constexpr const char* NAME = "lnav_format_value_stats";
    static constexpr const char* CREATE_STMT = R"(
-- Statistics for the values in each log format, combined across the files
-- that are visible in the LOG view.  The numbers are raw values, divide them
-- by the unit_divisor in lnav_format_values to get the base unit.
-- Tags: metadata
CREATE TABLE lnav_db.lnav_format_value_stats (
    format text,                -- The name of the log format.
    name text,                  -- The name of the value.
    files integer,              -- The number of files the statistics were combined from.
    count integer,              -- The number of numeric values seen.
    text_count integer,         -- The number of non-numeric values seen.
    min real,                   -- The smallest numeric value.
    max real,                   -- The largest numeric value.
    mean real,                  -- The mean of the numeric values.
    p50 real,                   -- The estimated median of the numeric values.
    p90 real,                   -- The estimated 90th percentile of the numeric values.
    p99 real,                   -- The estimated 99th percentile of the numeric values.
    distinct_estimate integer   -- The estimated number of distinct non-numeric values.
);
)";

    struct cursor {
        struct stats_row {
            intern_string_t sr_format;
            intern_string_t sr_name;
            size_t sr_files{0};
            logline_value_stats sr_stats;
        };

        sqlite3_vtab_cursor base;
        std::vector<stats_row>::iterator c_iter;
        std::vector<stats_row> c_rows;

        explicit cursor(sqlite3_vtab* vt) : base({vt})
        {
            // The rows are in the order the files and their values are
            // first seen, so the output does not depend on addresses.
            std::map<std::pair<const intern_string*, const intern_string*>,
                     size_t>
                row_indexes;

            for (const auto& ld : lnav_data.ld_log_source) {
                if (!ld->is_visible()) {
                    continue;
                }

                const auto* lf = ld->get_file_ptr();
                const auto* format = lf->get_format_ptr();
                if (format == nullptr) {
                    continue;
                }

                const auto& all_stats = lf->get_value_stats();
                for (const auto& meta : format->get_value_metadata()) {
                    if (!meta.lvm_values_index
                        || meta.lvm_values_index.value() >= all_stats.size())
                    {
                        continue;
                    }

                    const auto key = std::make_pair(format->get_name().unwrap(),
                                                    meta.lvm_name.unwrap());
                    auto iter = row_indexes.find(key);
                    if (iter == row_indexes.end()) {
                        iter = row_indexes.emplace(key, this->c_rows.size())
                                   .first;
                        this->c_rows.emplace_back(
                            stats_row{format->get_name(), meta.lvm_name});
                    }

                    auto& row = this->c_rows[iter->second];
                    row.sr_files += 1;
                    row.sr_stats.merge(
                        all_stats[meta.lvm_values_index.value()]);
                }
            }
            for (auto& row : this->c_rows) {
                row.sr_stats.finalize();
            }
        }

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

    int get_column(const cursor& vc, sqlite3_context* ctx, int col)
    {
        const auto& sr = *vc.c_iter;

        switch (col) {
            case 0:
                to_sqlite(ctx, sr.sr_format);
                break;
            case 1:
                to_sqlite(ctx, sr.sr_name);
                break;
            case 2:
                to_sqlite(ctx, (int64_t) sr.sr_files);
                break;
            default:
                value_stats_to_sqlite(
                    ctx, &sr.sr_stats, value_stats_col(col - 3));
                break;
        }

        return SQLITE_OK;
    }
};

struct injectable_lnav_file : vtab_module<lnav_file> {
    using vtab_module::vtab_module;
    using injectable = injectable_lnav_file(file_collection&);
};

struct injectable_lnav_file_metadata
    : vtab_module<tvt_no_update<lnav_file_metadata>> {
    using vtab_module::vtab_module;
    using injectable = injectable_lnav_file_metadata(file_collection&);
};

auto file_binder
    = injector::bind_multiple<vtab_module_base>().add<injectable_lnav_file>();

auto file_meta_binder = injector::bind_multiple<vtab_module_base>()
                            .add<injectable_lnav_file_metadata>();

struct injectable_lnav_file_value_stats
    : vtab_module<tvt_no_update<lnav_file_value_stats>> {
    using vtab_module::vtab_module;
    using injectable = injectable_lnav_file_value_stats(file_collection&);
};

auto file_value_stats_binder = injector::bind_multiple<vtab_module_base>()
                                   .add<injectable_lnav_file_value_stats>();

auto format_value_stats_binder
    = injector::bind_multiple<vtab_module_base>()
          .add<vtab_module<tvt_no_update<lnav_format_value_stats>>>();

auto format_values_binder
    = injector::bind_multiple<vtab_module_base>()
          .add<vtab_module<tvt_no_update<lnav_format_values>>>();

}  // namespace
