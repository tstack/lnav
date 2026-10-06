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

#include <algorithm>
#include <map>
#include <memory>
#include <string>

#include "fs_util.hh"

#include "lnav_log.hh"

#define GLOBCPP_EXCEPTION_LOG(context, ex_what) \
    log_warning("glob-cpp: %s -- %s", context, ex_what)
#include "glob-cpp/glob.h"

namespace lnav::filesystem {

/**
 * Escape the characters that glob-cpp treats as the start of a group or
 * brace expansion so that they are matched literally like they are in a
 * POSIX glob.  Bracket expressions are copied as-is.
 */
static std::string
to_glob_cpp_pattern(const std::string& pattern)
{
    std::string retval;

    retval.reserve(pattern.size());
    for (size_t lpc = 0; lpc < pattern.size(); lpc++) {
        auto ch = pattern[lpc];

        switch (ch) {
            case '\\':
                retval.push_back(ch);
                if (lpc + 1 < pattern.size()) {
                    lpc += 1;
                    retval.push_back(pattern[lpc]);
                }
                break;
            case '[': {
                // A ']' right after the "[" or "[!" is part of the set.
                auto end = lpc + 1;
                if (end < pattern.size()
                    && (pattern[end] == '!' || pattern[end] == '^'))
                {
                    end += 1;
                }
                if (end < pattern.size() && pattern[end] == ']') {
                    end += 1;
                }
                end = pattern.find(']', end);
                if (end == std::string::npos) {
                    // Not a valid set, leave the rest for glob-cpp to reject.
                    retval.append(pattern, lpc, std::string::npos);
                    return retval;
                }
                retval.append(pattern, lpc, end - lpc + 1);
                lpc = end;
                break;
            }
            case '(':
            case ')':
            case '{':
            case '}':
                retval.push_back('\\');
                retval.push_back(ch);
                break;
            default:
                retval.push_back(ch);
                break;
        }
    }

    return retval;
}

/**
 * @return The number of path separators in the pattern, not counting the
 *   ones inside bracket expressions.
 */
static size_t
count_separators(const std::string& pattern)
{
    size_t retval = 0;

    for (size_t lpc = 0; lpc < pattern.size(); lpc++) {
        switch (pattern[lpc]) {
            case '\\':
                lpc += 1;
                if (lpc < pattern.size() && pattern[lpc] == '/') {
                    retval += 1;
                }
                break;
            case '[': {
                // The same rules as to_glob_cpp_pattern() for where the
                // expression ends.
                auto end = lpc + 1;
                if (end < pattern.size()
                    && (pattern[end] == '!' || pattern[end] == '^'))
                {
                    end += 1;
                }
                if (end < pattern.size() && pattern[end] == ']') {
                    end += 1;
                }
                end = pattern.find(']', end);
                if (end != std::string::npos) {
                    lpc = end;
                }
                break;
            }
            case '/':
                retval += 1;
                break;
            default:
                break;
        }
    }

    return retval;
}

bool
glob_match(const std::string& pattern, const std::string& path)
{
    // glob-cpp only matches one component at a time when there is a "**".
    // Otherwise, a "*" stops at a "/", but a "?" or a bracket expression can
    // match one.  So, each "/" in the path has to be matched by one in the
    // pattern, which means there must be the same number of them.
    if (!is_recursive_glob(pattern)
        && count_separators(pattern)
            != static_cast<size_t>(
                std::count(path.begin(), path.end(), '/')))
    {
        return false;
    }

    static constexpr size_t MAX_CACHED_PATTERNS = 64;
    thread_local std::map<std::string, std::unique_ptr<glob::glob>> CACHE;

    auto iter = CACHE.find(pattern);
    if (iter == CACHE.end()) {
        if (CACHE.size() >= MAX_CACHED_PATTERNS) {
            CACHE.clear();
        }
        iter = CACHE
                   .emplace(pattern,
                            std::make_unique<glob::glob>(
                                to_glob_cpp_pattern(pattern)))
                   .first;
    }

    return glob::glob_match(path, *iter->second);
}

}  // namespace lnav::filesystem
