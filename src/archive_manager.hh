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
 * @file archive_manager.hh
 */

#ifndef lnav_archive_manager_hh
#define lnav_archive_manager_hh

#include <atomic>
#include <functional>
#include <string>
#include <utility>

#include "base/file_range.hh"
#include "base/result.h"
#include "ghc/filesystem.hpp"
#include "mapbox/variant.hpp"

namespace archive_manager {

struct extract_progress {
    extract_progress(ghc::filesystem::path path, ssize_t total)
        : ep_path(std::move(path)), ep_total_size(total)
    {
    }

    const ghc::filesystem::path ep_path;
    const ssize_t ep_total_size;
    std::atomic<size_t> ep_out_size{0};
};

using extract_cb
    = std::function<extract_progress*(const ghc::filesystem::path&, ssize_t)>;

struct archive_info {
    struct entry {
        ghc::filesystem::path e_name;
        const char* e_mode;
        time_t e_mtime;
        std::optional<file_ssize_t> e_size;
    };
    const char* ai_format_name;
    std::vector<entry> ai_entries;
};
struct unknown_file {};

using describe_result = mapbox::util::variant<archive_info, unknown_file>;

Result<describe_result, std::string> describe(
    const ghc::filesystem::path& filename);

ghc::filesystem::path filename_to_tmp_path(const std::string& filename);

using walk_result_t = Result<void, std::string>;

/**
 *
 * @feature f0:archive
 *
 * @param filename
 * @param cb
 * @return
 */
walk_result_t walk_archive_files(
    const std::string& filename,
    const extract_cb& cb,
    const std::function<void(const ghc::filesystem::path&,
                             const ghc::filesystem::directory_entry&)>&);

void cleanup_cache();

}  // namespace archive_manager

#endif
