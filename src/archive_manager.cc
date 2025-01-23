/**
 * Copyright (c) 2020, Timothy Stack
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
 *
 * @file archive_manager.cc
 */

#include <future>

#include <unistd.h>

#include "config.h"

#if HAVE_ARCHIVE_H
#    include "archive.h"
#    include "archive_entry.h"
#endif

#include "archive_manager.cfg.hh"
#include "archive_manager.hh"
#include "base/auto_fd.hh"
#include "base/auto_mem.hh"
#include "base/fs_util.hh"
#include "base/humanize.hh"
#include "base/injector.hh"
#include "base/lnav_log.hh"
#include "base/paths.hh"
#include "fmt/format.h"
#include "hasher.hh"

namespace fs = std::filesystem;

namespace archive_manager {

#if HAVE_ARCHIVE_H
/**
 * Enables a subset of the supported archive formats to speed up detection,
 * since some formats, like xar are unlikely to be used.
 */
static void
enable_desired_archive_formats(archive* arc)
{
    /** @feature f0:archive.formats */
    archive_read_support_format_7zip(arc);
    archive_read_support_format_cpio(arc);
    archive_read_support_format_lha(arc);
    archive_read_support_format_rar(arc);
    archive_read_support_format_tar(arc);
    archive_read_support_format_zip(arc);
}
#endif

Result<describe_result, std::string>
describe(const fs::path& filename)
{
#if HAVE_ARCHIVE_H
    static constexpr auto RAW_FORMAT_NAME = string_fragment::from_const("raw");
    static constexpr auto GZ_FILTER_NAME = string_fragment::from_const("gzip");

    auto_mem<archive> arc(archive_read_free);

    arc = archive_read_new();

    archive_read_support_filter_all(arc);
    enable_desired_archive_formats(arc);
    archive_read_support_format_raw(arc);
    log_debug("read open %s", filename.c_str());
    auto r = archive_read_open_filename(arc, filename.c_str(), 128 * 1024);
    if (r == ARCHIVE_OK) {
        struct archive_entry* entry = nullptr;

        const auto* format_name = archive_format_name(arc);

        log_debug("read next header %s %s", format_name, filename.c_str());
        if (archive_read_next_header(arc, &entry) == ARCHIVE_OK) {
            log_debug("read next done %s", filename.c_str());

            format_name = archive_format_name(arc);
            if (RAW_FORMAT_NAME == format_name) {
                auto filter_count = archive_filter_count(arc);

                if (filter_count == 1) {
                    return Ok(describe_result{unknown_file{}});
                }

                const auto* first_filter_name = archive_filter_name(arc, 0);
                if (filter_count == 2 && GZ_FILTER_NAME == first_filter_name) {
                    return Ok(describe_result{unknown_file{}});
                }
            }
            log_info(
                "detected archive: %s -- %s", filename.c_str(), format_name);
            auto ai = archive_info{
                format_name,
            };

            do {
                ai.ai_entries.emplace_back(archive_info::entry{
                    archive_entry_pathname_utf8(entry),
                    archive_entry_strmode(entry),
                    archive_entry_mtime(entry),
                    archive_entry_size_is_set(entry)
                        ? std::make_optional(archive_entry_size(entry))
                        : std::nullopt,
                });
            } while (archive_read_next_header(arc, &entry) == ARCHIVE_OK);

            return Ok(describe_result{ai});
        }

        const auto* errstr = archive_error_string(arc);
        log_info(
            "archive read header failed: %s -- %s", filename.c_str(), errstr);
        return Err(
            fmt::format(FMT_STRING("unable to read archive header: {} -- {}"),
                        filename,
                        errstr ? errstr : "not an archive"));
    } else {
        const auto* errstr = archive_error_string(arc);
        log_info("archive open failed: %s -- %s", filename.c_str(), errstr);
        return Err(fmt::format(FMT_STRING("unable to open file: {} -- {}"),
                               filename,
                               errstr ? errstr : "unknown"));
    }
#endif

    return Ok(describe_result{unknown_file{}});
}

static fs::path
archive_cache_path()
{
    return lnav::paths::workdir() / "archives";
}

fs::path
filename_to_tmp_path(const std::string& filename)
{
    auto fn_path = fs::path(filename);
    auto basename = fn_path.filename().string();
    hasher h;

    h.update(basename);
    auto fd = auto_fd(lnav::filesystem::openp(filename, O_RDONLY | O_CLOEXEC));
    if (fd != -1) {
        char buffer[1024];
        int rc;

        rc = read(fd, buffer, sizeof(buffer));
        if (rc >= 0) {
            h.update(buffer, rc);
        }
    }
    basename = fmt::format(FMT_STRING("arc-{}-{}"), h.to_string(), basename);

    return archive_cache_path() / basename;
}

#if HAVE_ARCHIVE_H
static walk_result_t
copy_data(const std::string& filename,
          struct archive* ar,
          struct archive_entry* entry,
          struct archive* aw,
          const fs::path& entry_path,
          struct extract_progress* ep)
{
    int r;
    const void* buff;
    size_t size, total = 0, next_space_check = 0;
    la_int64_t offset;

    for (;;) {
        if (total >= next_space_check) {
            const auto& cfg = injector::get<const config&>();
            auto tmp_space = fs::space(entry_path);

            if (tmp_space.available < cfg.amc_min_free_space) {
                return Err(fmt::format(
                    FMT_STRING("available space on disk ({}) is below the "
                               "minimum-free threshold ({}).  Unable to unpack "
                               "'{}' to '{}'"),
                    humanize::file_size(tmp_space.available,
                                        humanize::alignment::none),
                    humanize::file_size(cfg.amc_min_free_space,
                                        humanize::alignment::none),
                    entry_path.filename().string(),
                    entry_path.parent_path().string()));
            }
            next_space_check += 1024 * 1024;
        }

        r = archive_read_data_block(ar, &buff, &size, &offset);
        if (r == ARCHIVE_EOF) {
            return Ok();
        }
        if (r != ARCHIVE_OK) {
            return Err(fmt::format(
                FMT_STRING("failed to extract '{}' from archive '{}' -- {}"),
                archive_entry_pathname_utf8(entry),
                filename,
                archive_error_string(ar)));
        }
        r = archive_write_data_block(aw, buff, size, offset);
        if (r != ARCHIVE_OK) {
            return Err(fmt::format(FMT_STRING("failed to write file: {} -- {}"),
                                   entry_path.string(),
                                   archive_error_string(aw)));
        }

        total += size;
        ep->ep_out_size.fetch_add(size);
    }
}

static walk_result_t
extract(const std::string& filename, const extract_cb& cb)
{
    static const int FLAGS = ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM
        | ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS;

    std::error_code ec;
    auto tmp_path = filename_to_tmp_path(filename);

    fs::create_directories(tmp_path.parent_path(), ec);
    if (ec) {
        return Err(
            fmt::format(FMT_STRING("unable to create directory: {} -- {}"),
                        tmp_path.parent_path().string(),
                        ec.message()));
    }

    auto arc_lock = lnav::filesystem::file_lock(tmp_path);
    auto lock_guard = lnav::filesystem::file_lock::guard(&arc_lock);
    auto done_path = tmp_path;

    done_path += ".done";

    if (fs::exists(done_path)) {
        size_t file_count = 0;
        if (fs::is_directory(tmp_path)) {
            for (const auto& entry : fs::directory_iterator(tmp_path)) {
                (void) entry;
                file_count += 1;
            }
        }
        if (file_count > 0) {
            auto now = fs::file_time_type::clock::now();
            fs::last_write_time(done_path, now);
            log_info("%s: archive has already been extracted!",
                     done_path.c_str());
            return Ok();
        }
        log_warning("%s: archive cache has been damaged, re-extracting",
                    done_path.c_str());

        fs::remove(done_path);
    }

    auto_mem<archive> arc(archive_free);
    auto_mem<archive> ext(archive_free);

    arc = archive_read_new();
    enable_desired_archive_formats(arc);
    archive_read_support_format_raw(arc);
    archive_read_support_filter_all(arc);
    ext = archive_write_disk_new();
    archive_write_disk_set_options(ext, FLAGS);
    archive_write_disk_set_standard_lookup(ext);
    if (archive_read_open_filename(arc, filename.c_str(), 10240) != ARCHIVE_OK)
    {
        return Err(fmt::format(FMT_STRING("unable to open archive: {} -- {}"),
                               filename,
                               archive_error_string(arc)));
    }

    log_info("extracting %s to %s", filename.c_str(), tmp_path.c_str());
    while (true) {
        struct archive_entry* entry = nullptr;
        auto r = archive_read_next_header(arc, &entry);
        if (r == ARCHIVE_EOF) {
            log_info("all done");
            break;
        }
        if (r != ARCHIVE_OK) {
            return Err(
                fmt::format(FMT_STRING("unable to read entry header: {} -- {}"),
                            filename,
                            archive_error_string(arc)));
        }

        const auto* format_name = archive_format_name(arc);
        auto filter_count = archive_filter_count(arc);

        auto_mem<archive_entry> wentry(archive_entry_free);
        wentry = archive_entry_clone(entry);
        auto desired_pathname = fs::path(archive_entry_pathname(entry));
        if (strcmp(format_name, "raw") == 0 && filter_count >= 2) {
            desired_pathname = fs::path(filename).filename();
        }
        auto entry_path = tmp_path / desired_pathname;
        auto* prog = cb(
            entry_path,
            archive_entry_size_is_set(entry) ? archive_entry_size(entry) : -1);
        archive_entry_copy_pathname(wentry, entry_path.c_str());
        auto entry_mode = archive_entry_mode(wentry);

        archive_entry_set_perm(
            wentry, S_IRUSR | (S_ISDIR(entry_mode) ? S_IXUSR | S_IWUSR : 0));
        r = archive_write_header(ext, wentry);
        if (r < ARCHIVE_OK) {
            return Err(
                fmt::format(FMT_STRING("unable to write entry: {} -- {}"),
                            entry_path.string(),
                            archive_error_string(ext)));
        }

        if (!archive_entry_size_is_set(entry) || archive_entry_size(entry) > 0)
        {
            TRY(copy_data(filename, arc, entry, ext, entry_path, prog));
        }
        r = archive_write_finish_entry(ext);
        if (r != ARCHIVE_OK) {
            return Err(
                fmt::format(FMT_STRING("unable to finish entry: {} -- {}"),
                            entry_path.string(),
                            archive_error_string(ext)));
        }
    }
    archive_read_close(arc);
    archive_write_close(ext);

    lnav::filesystem::create_file(done_path, O_WRONLY, 0600);

    return Ok();
}
#endif

walk_result_t
walk_archive_files(
    const std::string& filename,
    const extract_cb& cb,
    const std::function<void(const fs::path&, const fs::directory_entry&)>&
        callback)
{
#if HAVE_ARCHIVE_H
    auto tmp_path = filename_to_tmp_path(filename);

    auto result = extract(filename, cb);
    if (result.isErr()) {
        fs::remove_all(tmp_path);
        return result;
    }

    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(tmp_path, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        callback(tmp_path, entry);
    }
    if (ec) {
        return Err(fmt::format(FMT_STRING("failed to walk temp dir: {} -- {}"),
                               tmp_path.string(),
                               ec.message()));
    }

    return Ok();
#else
    return Err(std::string("not compiled with libarchive"));
#endif
}

void
cleanup_cache()
{
    (void) std::async(std::launch::async, []() {
        auto now = std::filesystem::file_time_type::clock::now();
        auto cache_path = archive_cache_path();
        const auto& cfg = injector::get<const config&>();
        std::vector<fs::path> to_remove;

        log_debug("cache-ttl %d", cfg.amc_cache_ttl.count());
        for (const auto& entry : fs::directory_iterator(cache_path)) {
            if (entry.path().extension() != ".done") {
                continue;
            }

            auto mtime = fs::last_write_time(entry.path());
            auto exp_time = mtime + cfg.amc_cache_ttl;
            if (now < exp_time) {
                continue;
            }

            to_remove.emplace_back(entry.path());
        }

        for (auto& entry : to_remove) {
            log_debug("removing cached archive: %s", entry.c_str());
            fs::remove(entry);

            entry.replace_extension(".lck");
            fs::remove(entry);

            entry.replace_extension();
            fs::remove_all(entry);
        }
    });
}

}  // namespace archive_manager
