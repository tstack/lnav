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
 *
 * @file text_format.hh
 */

#ifndef text_format_hh
#define text_format_hh

#include <filesystem>
#include <string>

#include <sys/types.h>

#include "base/enum_util.hh"
#include "base/intern_string.hh"
#include "fmt/format.h"

enum class text_format_t : uint8_t {
    TF_BINARY,
    TF_C_LIKE,
    TF_JAVA,
    TF_JSON,
    TF_LOG,
    TF_MAKEFILE,
    TF_MAN,
    TF_MARKDOWN,
    TF_PYTHON,
    TF_RUST,
    TF_SQL,
    TF_XML,
    TF_YAML,
    TF_TOML,
    TF_DIFF,
    TF_SHELL_SCRIPT,
    TF_UNKNOWN,
};

extern const string_fragment
    TEXT_FORMAT_STRINGS[lnav::enums::to_underlying(text_format_t::TF_UNKNOWN)
                        + 1];

namespace fmt {
template<>
struct formatter<text_format_t> : formatter<string_view> {
    template<typename FormatContext>
    auto format(text_format_t tf, FormatContext& ctx)
    {
        string_view name = "unknown";
        if (lnav::enums::to_underlying(tf)
            <= lnav::enums::to_underlying(text_format_t::TF_UNKNOWN))
        {
            name = TEXT_FORMAT_STRINGS[lnav::enums::to_underlying(tf)]
                       .to_string_view();
        }
        return formatter<string_view>::format(name, ctx);
    }
};
}  // namespace fmt

/**
 * Try to detect the format of the given text file fragment.
 *
 * @return The detected format.
 */
text_format_t detect_text_format(string_fragment sf,
                                 std::optional<std::filesystem::path> path
                                 = std::nullopt);

struct text_format_meta_t {
    std::string tfm_filename;
};

std::optional<text_format_meta_t> extract_text_meta(string_fragment sf,
                                                    text_format_t tf);

#endif
