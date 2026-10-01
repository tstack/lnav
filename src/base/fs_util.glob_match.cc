/**
 * Copyright (c) 2026, Timothy Stack
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

/*
 * This is separate from fs_util.cc because the "glob" namespace from
 * glob-cpp conflicts with the glob() function from <glob.h>.
 */

#include <map>
#include <memory>
#include <string>

#include "fs_util.hh"

#include "lnav_log.hh"

#define GLOBCPP_EXCEPTION_LOG(context, ex_what) \
    log_warning("glob-cpp: %s -- %s", context, ex_what)
#include "glob-cpp/glob.h"

namespace lnav::filesystem {

bool
glob_match(const std::string& pattern, const std::string& path)
{
    static constexpr size_t MAX_CACHED_PATTERNS = 64;
    thread_local std::map<std::string, std::unique_ptr<glob::glob>> CACHE;

    auto iter = CACHE.find(pattern);
    if (iter == CACHE.end()) {
        if (CACHE.size() >= MAX_CACHED_PATTERNS) {
            CACHE.clear();
        }
        iter = CACHE.emplace(pattern, std::make_unique<glob::glob>(pattern))
                   .first;
    }

    return glob::glob_match(path, *iter->second);
}

}  // namespace lnav::filesystem
