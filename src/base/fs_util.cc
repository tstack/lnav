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
#include <fstream>
#include <utility>

#include "fs_util.hh"

#include <stdlib.h>
#include <sys/param.h>

#include "config.h"
#include "fmt/format.h"
#include "itertools.hh"
#include "lnav_log.hh"
#include "opt_util.hh"
#include "scn/scan.h"

#ifdef HAVE_LIBPROC_H
#    include <libproc.h>
#endif

namespace lnav::filesystem {

std::optional<std::filesystem::path>
self_path()
{
#ifdef HAVE_LIBPROC_H
    auto pid = getpid();
    char pathbuf[PROC_PIDPATHINFO_MAXSIZE];

    auto rc = proc_pidpath(pid, pathbuf, sizeof(pathbuf));
    if (rc <= 0) {
        log_error("unable to determine self path: %s", strerror(errno));
    } else {
        log_info("self path: %s", pathbuf);
        return std::filesystem::path(pathbuf);
    }
    return std::nullopt;
#else
    std::error_code ec;
    auto target = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) {
        log_error("failed to read /proc/self/exe: %s", ec.message().c_str());
        return std::nullopt;
    }
    return target;
#endif
}

static time_t
init_self_mtime()
{
    auto retval = time_t{};
    auto path_opt = self_path();

    time(&retval);
    if (path_opt) {
        auto stat_res = stat_file(path_opt.value());
        if (stat_res.isErr()) {
            log_error("unable to stat self: %s", stat_res.unwrapErr().c_str());
        } else {
            retval = stat_res.unwrap().st_mtime;
        }
    }

    return retval;
}

time_t
self_mtime()
{
    static auto RETVAL = init_self_mtime();

    return RETVAL;
}

std::string
escape_path(const std::filesystem::path& p)
{
    auto p_str = p.string();
    std::string retval;

    for (const auto ch : p_str) {
        switch (ch) {
            case ' ':
            case '$':
            case '\\':
            case ';':
            case '&':
            case '<':
            case '>':
            case '\'':
            case '"':
            case '*':
            case '[':
            case ']':
            case '?':
                retval.push_back('\\');
                break;
        }
        retval.push_back(ch);
    }

    return retval;
}

std::pair<std::string, file_location_t>
split_file_location(const std::string& file_path_str)
{
    auto hash_index = file_path_str.rfind('#');
    if (hash_index != std::string::npos) {
        return std::make_pair(file_path_str.substr(0, hash_index),
                              file_path_str.substr(hash_index));
    }

    auto colon_index = file_path_str.rfind(':');
    if (colon_index != std::string::npos) {
        auto top_range
            = std::string_view{&file_path_str[colon_index + 1],
                               file_path_str.size() - colon_index - 1};
        auto scan_res = scn::scan_value<int>(top_range);

        if (scan_res && scan_res->range().empty()) {
            return std::make_pair(file_path_str.substr(0, colon_index),
                                  scan_res->value());
        }
        log_info("did not parse line number from file path with colon: %s",
                 file_path_str.c_str());
    }

    return std::make_pair(file_path_str, file_location_tail{});
}

Result<std::filesystem::path, std::string>
realpath(const std::filesystem::path& path)
{
    char resolved[PATH_MAX];
    auto rc = ::realpath(path.c_str(), resolved);

    if (rc == nullptr) {
        return Err(std::string(strerror(errno)));
    }

    return Ok(std::filesystem::path(resolved));
}

Result<auto_fd, std::string>
create_file(const std::filesystem::path& path, int flags, mode_t mode)
{
    auto fd = openp(path, flags | O_CREAT, mode);

    if (fd == -1) {
        return Err(fmt::format(FMT_STRING("Failed to open: {} -- {}"),
                               path.string(),
                               strerror(errno)));
    }

    return Ok(auto_fd(fd));
}

Result<auto_fd, std::string>
open_file(const std::filesystem::path& path, int flags)
{
    auto fd = openp(path, flags);

    if (fd == -1) {
        return Err(fmt::format(FMT_STRING("Failed to open: {} -- {}"),
                               path.string(),
                               strerror(errno)));
    }

    return Ok(auto_fd(fd));
}

Result<std::pair<std::filesystem::path, auto_fd>, std::string>
open_temp_file(const std::filesystem::path& pattern)
{
    auto pattern_str = pattern.string();
    char pattern_copy[pattern_str.size() + 1];
    int fd;

    strcpy(pattern_copy, pattern_str.c_str());
#if HAVE_MKOSTEMP
    fd = mkostemp(pattern_copy, O_CLOEXEC);
#else
    fd = mkstemp(pattern_copy);
    if (fd != -1) {
        fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
#endif
    if (fd == -1) {
        return Err(
            fmt::format(FMT_STRING("unable to create temporary file: {} -- {}"),
                        pattern.string(),
                        strerror(errno)));
    }

    return Ok(std::make_pair(std::filesystem::path(pattern_copy), auto_fd(fd)));
}

Result<std::string, std::string>
read_file(const std::filesystem::path& path)
{
    try {
        std::ifstream file_stream(path);

        if (!file_stream) {
            return Err(std::string(strerror(errno)));
        }

        std::string retval;
        retval.assign((std::istreambuf_iterator<char>(file_stream)),
                      std::istreambuf_iterator<char>());
        return Ok(retval);
    } catch (const std::exception& e) {
        return Err(std::string(e.what()));
    }
}

Result<write_file_result, std::string>
write_file(const std::filesystem::path& path,
           string_fragment_producer& content,
           std::set<write_file_options> options)
{
    write_file_result retval;
    auto tmp_pattern = path;
    tmp_pattern += ".XXXXXX";

    auto tmp_pair = TRY(open_temp_file(tmp_pattern));
    auto for_res = content.for_each(
        [&tmp_pair](string_fragment sf) -> Result<void, std::string> {
            auto bytes_written
                = write(tmp_pair.second.get(), sf.data(), sf.length());
            if (bytes_written < 0) {
                return Err(fmt::format(
                    FMT_STRING("unable to write to temporary file {}: {}"),
                    tmp_pair.first.string(),
                    strerror(errno)));
            }

            if (bytes_written != sf.length()) {
                return Err(
                    fmt::format(FMT_STRING("short write to file {}: {} < {}"),
                                tmp_pair.first.string(),
                                bytes_written,
                                sf.length()));
            }

            return Ok();
        });

    if (for_res.isErr()) {
        return Err(for_res.unwrapErr());
    }

    std::error_code ec;
    if (options.count(write_file_options::backup_existing)) {
        if (std::filesystem::exists(path, ec)) {
            auto backup_path = path;

            backup_path += ".bak";
            std::filesystem::rename(path, backup_path, ec);
            if (ec) {
                return Err(
                    fmt::format(FMT_STRING("unable to backup file {}: {}"),
                                path.string(),
                                ec.message()));
            }

            retval.wfr_backup_path = backup_path;
        }
    }

    auto mode = S_IRUSR | S_IWUSR;
    if (options.count(write_file_options::executable)) {
        mode |= S_IXUSR;
    }
    if (options.count(write_file_options::read_only)) {
        mode &= ~S_IWUSR;
    }

    fchmod(tmp_pair.second.get(), mode);

    std::filesystem::rename(tmp_pair.first, path, ec);
    if (ec) {
        return Err(
            fmt::format(FMT_STRING("unable to move temporary file {}: {}"),
                        tmp_pair.first.string(),
                        ec.message()));
    }

    log_debug("wrote file: %s", path.c_str());
    return Ok(retval);
}

std::string
build_path(const std::vector<std::filesystem::path>& paths)
{
    return paths
        | lnav::itertools::map([](const auto& path) { return path.string(); })
        | lnav::itertools::append(getenv_opt("PATH").value_or(""))
        | lnav::itertools::filter_out(&std::string::empty)
        | lnav::itertools::fold(
               [](const auto& elem, auto& accum) {
                   if (!accum.empty()) {
                       accum.push_back(':');
                   }
                   return accum.append(elem);
               },
               std::string());
}

Result<struct stat, std::string>
stat_file(const std::filesystem::path& path)
{
    struct stat retval;

    if (statp(path, &retval) == 0) {
        return Ok(retval);
    }

    return Err(fmt::format(FMT_STRING("failed to find file: {} -- {}"),
                           path.string(),
                           strerror(errno)));
}

file_lock::file_lock(const std::filesystem::path& archive_path)
{
    auto lock_path = archive_path;

    lock_path += ".lck";
    auto open_res
        = lnav::filesystem::create_file(lock_path, O_RDWR | O_CLOEXEC, 0600);
    if (open_res.isErr()) {
        throw std::runtime_error(open_res.unwrapErr());
    }
    this->lh_fd = open_res.unwrap();
}

}  // namespace lnav::filesystem

namespace fmt {

auto
formatter<std::filesystem::path>::format(const std::filesystem::path& p,
                                         format_context& ctx)
    -> decltype(ctx.out()) const
{
    auto esc_res = fmt::v10::detail::find_escape(&(*p.native().begin()),
                                                 &(*p.native().end()));
    if (esc_res.end == nullptr) {
        return formatter<string_view>::format(p.native(), ctx);
    }

    return format_to(ctx.out(), FMT_STRING("{:?}"), p.native());
}
}  // namespace fmt
