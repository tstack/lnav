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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include "fmt/format.h"
#include "fs_util.hh"
#include "opt_util.hh"

namespace lnav {
namespace filesystem {

Result<std::pair<ghc::filesystem::path, int>, std::string>
open_temp_file(const ghc::filesystem::path &pattern)
{
    auto pattern_str = pattern.string();
    char pattern_copy[pattern_str.size() + 1];
    int fd;

    strcpy(pattern_copy, pattern_str.c_str());
    if ((fd = mkstemp(pattern_copy)) == -1) {
        return Err(fmt::format("unable to create temporary file: {} -- {}",
                               pattern.string(), strerror(errno)));
    }

    return Ok(std::make_pair(ghc::filesystem::path(pattern_copy), fd));
}

Result<std::string, std::string> read_file(const ghc::filesystem::path &path)
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

std::string build_path(const std::vector<ghc::filesystem::path> &paths)
{
    std::string retval;

    for (const auto &path : paths) {
        if (path.empty()) {
            continue;
        }
        if (!retval.empty()) {
            retval += ":";
        }
        retval += path.string();
    }
    auto env_path = getenv_opt("PATH");
    if (env_path) {
        retval += ":" + std::string(*env_path);
    }
    return retval;
}

}
}
