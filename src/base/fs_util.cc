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

#include "fs_util.hh"

#include <stdlib.h>

#include "config.h"
#include "fmt/format.h"
#include "itertools.hh"
#include "opt_util.hh"

namespace lnav {
namespace filesystem {

Result<ghc::filesystem::path, std::string>
realpath(const ghc::filesystem::path& path)
{
    char resolved[PATH_MAX];
    auto rc = ::realpath(path.c_str(), resolved);

    if (rc == nullptr) {
        return Err(std::string(strerror(errno)));
    }

    return Ok(ghc::filesystem::path(resolved));
}

Result<auto_fd, std::string>
create_file(const ghc::filesystem::path& path, int flags, mode_t mode)
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
open_file(const ghc::filesystem::path& path, int flags)
{
    auto fd = openp(path, flags);

    if (fd == -1) {
        return Err(fmt::format(FMT_STRING("Failed to open: {} -- {}"),
                               path.string(),
                               strerror(errno)));
    }

    return Ok(auto_fd(fd));
}

Result<std::pair<ghc::filesystem::path, auto_fd>, std::string>
open_temp_file(const ghc::filesystem::path& pattern)
{
    auto pattern_str = pattern.string();
    char pattern_copy[pattern_str.size() + 1];
    int fd;

    strcpy(pattern_copy, pattern_str.c_str());
    if ((fd = mkostemp(pattern_copy, O_CLOEXEC)) == -1) {
        return Err(
            fmt::format(FMT_STRING("unable to create temporary file: {} -- {}"),
                        pattern.string(),
                        strerror(errno)));
    }

    return Ok(std::make_pair(ghc::filesystem::path(pattern_copy), auto_fd(fd)));
}

Result<std::string, std::string>
read_file(const ghc::filesystem::path& path)
{
    try {
        ghc::filesystem::ifstream file_stream(path);

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
write_file(const ghc::filesystem::path& path,
           const string_fragment& content,
           std::set<write_file_options> options)
{
    write_file_result retval;
    auto tmp_pattern = path;
    tmp_pattern += ".XXXXXX";

    auto tmp_pair = TRY(open_temp_file(tmp_pattern));
    auto bytes_written
        = write(tmp_pair.second.get(), content.data(), content.length());
    if (bytes_written < 0) {
        return Err(
            fmt::format(FMT_STRING("unable to write to temporary file {}: {}"),
                        tmp_pair.first.string(),
                        strerror(errno)));
    }
    if (bytes_written != content.length()) {
        return Err(fmt::format(FMT_STRING("short write to file {}: {} < {}"),
                               tmp_pair.first.string(),
                               bytes_written,
                               content.length()));
    }

    std::error_code ec;
    if (options.count(write_file_options::backup_existing)) {
        if (ghc::filesystem::exists(path, ec)) {
            auto backup_path = path;

            backup_path += ".bak";
            ghc::filesystem::rename(path, backup_path, ec);
            if (ec) {
                return Err(
                    fmt::format(FMT_STRING("unable to backup file {}: {}"),
                                path.string(),
                                ec.message()));
            }

            retval.wfr_backup_path = backup_path;
        }
    }

    ghc::filesystem::rename(tmp_pair.first, path, ec);
    if (ec) {
        return Err(
            fmt::format(FMT_STRING("unable to move temporary file {}: {}"),
                        tmp_pair.first.string(),
                        ec.message()));
    }

    return Ok(retval);
}

std::string
build_path(const std::vector<ghc::filesystem::path>& paths)
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
stat_file(const ghc::filesystem::path& path)
{
    struct stat retval;

    if (statp(path, &retval) == 0) {
        return Ok(retval);
    }

    return Err(fmt::format(FMT_STRING("failed to find file: {} -- {}"),
                           path.string(),
                           strerror(errno)));
}

file_lock::file_lock(const ghc::filesystem::path& archive_path)
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

}  // namespace filesystem
}  // namespace lnav
