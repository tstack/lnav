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
 * @file file_format.hh
 */

#ifndef lnav_file_format_hh
#define lnav_file_format_hh

#include <filesystem>
#include <optional>

#include "base/lnav.console.hh"
#include "fmt/format.h"

enum class file_format_t : int {
    UNKNOWN,
    SQLITE_DB,
    ARCHIVE,
    MULTIPLEXED,
    REMOTE,
};

struct external_file_format {
    std::string eff_format_name;
    std::string eff_converter;
    std::filesystem::path eff_source_path;
};

struct detect_file_format_result {
    file_format_t dffr_file_format{file_format_t::UNKNOWN};
    std::vector<lnav::console::user_message> dffr_details;
};

detect_file_format_result detect_file_format(
    const std::filesystem::path& filename);

std::optional<external_file_format> detect_mime_type(
    const std::filesystem::path& filename);

namespace fmt {
template<>
struct formatter<file_format_t> : formatter<string_view> {
    template<typename FormatContext>
    auto format(file_format_t ff, FormatContext& ctx)
    {
        string_view name = "unknown";
        switch (ff) {
            case file_format_t::SQLITE_DB:
                name = "\U0001F5C2  SQLite DB";
                break;
            case file_format_t::ARCHIVE:
                name = "\U0001F5C4  Archive";
                break;
            case file_format_t::MULTIPLEXED:
                name = "\u22fa  Multiplexed";
                break;
            case file_format_t::REMOTE:
                name = "\U0001F5A5  Remote";
                break;
            default:
                break;
        }
        return formatter<string_view>::format(name, ctx);
    }
};
}  // namespace fmt

#endif
