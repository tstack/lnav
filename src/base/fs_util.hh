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

#ifndef lnav_fs_util_hh
#define lnav_fs_util_hh

#include <set>
#include <string>
#include <vector>

#include "auto_fd.hh"
#include "ghc/filesystem.hpp"
#include "intern_string.hh"
#include "result.h"

namespace lnav {
namespace filesystem {

inline bool
is_glob(const std::string& fn)
{
    return (fn.find('*') != std::string::npos
            || fn.find('?') != std::string::npos
            || fn.find('[') != std::string::npos);
}

inline int
statp(const ghc::filesystem::path& path, struct stat* buf)
{
    return stat(path.c_str(), buf);
}

inline int
openp(const ghc::filesystem::path& path, int flags)
{
    return open(path.c_str(), flags);
}

inline int
openp(const ghc::filesystem::path& path, int flags, mode_t mode)
{
    return open(path.c_str(), flags, mode);
}

Result<ghc::filesystem::path, std::string> realpath(
    const ghc::filesystem::path& path);

Result<auto_fd, std::string> create_file(const ghc::filesystem::path& path,
                                         int flags,
                                         mode_t mode);

Result<auto_fd, std::string> open_file(const ghc::filesystem::path& path,
                                       int flags);

Result<struct stat, std::string> stat_file(const ghc::filesystem::path& path);

Result<std::pair<ghc::filesystem::path, auto_fd>, std::string> open_temp_file(
    const ghc::filesystem::path& pattern);

Result<std::string, std::string> read_file(const ghc::filesystem::path& path);

enum class write_file_options {
    backup_existing,
};

struct write_file_result {
    nonstd::optional<ghc::filesystem::path> wfr_backup_path;
};

Result<write_file_result, std::string> write_file(
    const ghc::filesystem::path& path,
    const string_fragment& content,
    std::set<write_file_options> options = {});

std::string build_path(const std::vector<ghc::filesystem::path>& paths);

class file_lock {
public:
    class guard {
    public:
        explicit guard(file_lock* arc_lock) : g_lock(arc_lock)
        {
            this->g_lock->lock();
        }

        guard(guard&& other) noexcept
            : g_lock(std::exchange(other.g_lock, nullptr))
        {
        }

        ~guard()
        {
            if (this->g_lock != nullptr) {
                this->g_lock->unlock();
            }
        }

        guard(const guard&) = delete;
        guard& operator=(const guard&) = delete;
        guard& operator=(guard&&) = delete;

    private:
        file_lock* g_lock;
    };

    void lock() const { lockf(this->lh_fd, F_LOCK, 0); }

    void unlock() const { lockf(this->lh_fd, F_ULOCK, 0); }

    explicit file_lock(const ghc::filesystem::path& archive_path);

    auto_fd lh_fd;
};

}  // namespace filesystem
}  // namespace lnav

#endif
