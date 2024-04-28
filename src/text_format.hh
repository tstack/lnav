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

#include <string>

#include <sys/types.h>

#include "base/intern_string.hh"
#include "fmt/format.h"
#include "ghc/filesystem.hpp"

enum class text_format_t {
    TF_UNKNOWN,
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
};

namespace fmt {
template<>
struct formatter<text_format_t> : formatter<string_view> {
    template<typename FormatContext>
    auto format(text_format_t tf, FormatContext& ctx)
    {
        string_view name = "unknown";
        switch (tf) {
            case text_format_t::TF_UNKNOWN:
                name = "text/plain";
                break;
            case text_format_t::TF_BINARY:
                name = "application/octet-stream";
                break;
            case text_format_t::TF_LOG:
                name = "text/log";
                break;
            case text_format_t::TF_PYTHON:
                name = "text/python";
                break;
            case text_format_t::TF_RUST:
                name = "text/rust";
                break;
            case text_format_t::TF_JAVA:
                name = "text/java";
                break;
            case text_format_t::TF_C_LIKE:
                name = "text/c";
                break;
            case text_format_t::TF_SQL:
                name = "application/sql";
                break;
            case text_format_t::TF_XML:
                name = "text/xml";
                break;
            case text_format_t::TF_JSON:
                name = "application/json";
                break;
            case text_format_t::TF_MAKEFILE:
                name = "text/x-makefile";
                break;
            case text_format_t::TF_MAN:
                name = "text/man";
                break;
            case text_format_t::TF_MARKDOWN:
                name = "text/markdown";
                break;
            case text_format_t::TF_YAML:
                name = "application/yaml";
                break;
            case text_format_t::TF_TOML:
                name = "application/toml";
                break;
            case text_format_t::TF_DIFF:
                name = "text/x-diff";
                break;
            case text_format_t::TF_SHELL_SCRIPT:
                name = "text/x-shellscript";
                break;
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
                                 std::optional<ghc::filesystem::path> path
                                 = std::nullopt);

struct text_format_meta_t {
    std::string tfm_filename;
};

std::optional<text_format_meta_t> extract_text_meta(string_fragment sf,
                                                       text_format_t tf);

#endif
