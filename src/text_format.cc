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
 * @file text_format.cc
 */

#include <set>

#include "text_format.hh"

#include "base/from_trait.hh"
#include "base/itertools.enumerate.hh"
#include "base/itertools.hh"
#include "base/lnav_log.hh"
#include "config.h"
#include "pcrepp/pcre2pp.hh"
#include "yajl/api/yajl_parse.h"

constexpr string_fragment TEXT_FORMAT_STRINGS[text_format_count] = {
    "application/octet-stream"_frag,
    "text/c"_frag,
    "text/java"_frag,
    "application/json"_frag,
    "text/log"_frag,
    "text/x-makefile"_frag,
    "text/man"_frag,
    "text/markdown"_frag,
    "text/python"_frag,
    "application/x-pcre"_frag,
    "text/rust"_frag,
    "application/sql"_frag,
    "text/xml"_frag,
    "application/yaml"_frag,
    "application/toml"_frag,
    "text/x-diff"_frag,
    "text/x-shellscript"_frag,
    "text/x-lnav-script"_frag,
    "text/x-rst"_frag,
    "text/plain"_frag,
};

text_format_t
detect_text_format(string_fragment sf,
                   std::optional<std::filesystem::path> path)
{
    static const std::set<std::filesystem::path> FILTER_EXTS = {
        ".bz2",
        ".gz",
        ".lzma",
        ".xz",
        ".zst",
    };
    static const auto C_EXTS = std::set<std::filesystem::path>{
        ".h",
        ".hh",
        ".hpp",
        ".c",
        ".cc",
        ".cpp",
        ".tpp",
    };
    static const auto PY_EXT = std::filesystem::path(".py");
    static const auto RS_EXT = std::filesystem::path(".rs");
    static const auto SQL_EXT = std::filesystem::path(".sql");
    static const auto JAVA_EXT = std::filesystem::path(".java");
    static const auto TOML_EXT = std::filesystem::path(".toml");
    static const auto XML_EXT = std::filesystem::path(".xml");
    static const auto YAML_EXT = std::filesystem::path(".yaml");
    static const auto YML_EXT = std::filesystem::path(".yml");
    static const auto MAKEFILE_STEM = std::filesystem::path("Makefile");
    static const auto MD_EXT = std::filesystem::path(".md");
    static const auto MARKDOWN_EXT = std::filesystem::path(".markdown");
    static const auto SH_EXT = std::filesystem::path(".sh");
    static const auto LNAV_EXT = std::filesystem::path(".lnav");
    static const auto RST_EXT = std::filesystem::path(".rst");

    static const auto DIFF_MATCHERS = lnav::pcre2pp::code::from_const(
        R"(^--- .*\n\+\+\+ .*\n)", PCRE2_MULTILINE);

    static const auto MAN_MATCHERS = lnav::pcre2pp::code::from_const(
        R"(^[A-Za-z][A-Za-z\-_\+0-9]+\(\d\)\s+)", PCRE2_MULTILINE);

    // XXX This is a pretty crude way of
    // detecting format...
    static const auto PYTHON_MATCHERS = lnav::pcre2pp::code::from_const(
        "(?:"
        "^\\s*def\\s+\\w+\\([^)]*\\):"
        "[^\\n]*$|"
        "^\\s*try:[^\\n]*$"
        ")",
        PCRE2_MULTILINE);

    static const auto RUST_MATCHERS = lnav::pcre2pp::code::from_const(
        R"(
(?:
^\s*use\s+[\w+:\{\}]+;$|
^\s*(?:pub enum|pub const|(?:pub )?fn)\s+\w+.*$|
^\s*impl\s+\w+.*$
)
)",
        PCRE2_MULTILINE);

    static const auto JAVA_MATCHERS = lnav::pcre2pp::code::from_const(
        "(?:"
        "^package\\s+|"
        "^import\\s+|"
        "^\\s*(?:public)?\\s*"
        "class\\s*(\\w+\\s+)*\\s*{"
        ")",
        PCRE2_MULTILINE);

    static const auto C_LIKE_MATCHERS = lnav::pcre2pp::code::from_const(
        "(?:"
        "^#\\s*include\\s+|"
        "^#\\s*define\\s+|"
        "^\\s*if\\s+\\([^)]+\\)[^\\n]"
        "*$|"
        "^\\s*(?:\\w+\\s+)*class "
        "\\w+ {"
        ")",
        PCRE2_MULTILINE);

    static const auto SQL_MATCHERS = lnav::pcre2pp::code::from_const(
        "(?:"
        "create\\s+table\\s+|"
        "select\\s+.+\\s+from\\s+|"
        "insert\\s+into\\s+.+\\s+"
        "values"
        ")",
        PCRE2_MULTILINE | PCRE2_CASELESS);

    static const auto XML_MATCHERS = lnav::pcre2pp::code::from_const(
        "(?:"
        R"(<\?xml(\s+\w+\s*=\s*"[^"]*")*\?>|)"
        R"(</?\w+(\s+\w+\s*=\s*"[^"]*")*\s*>)"
        ")",
        PCRE2_MULTILINE | PCRE2_CASELESS);

    static const auto SH_MATCHERS
        = lnav::pcre2pp::code::from_const("^#!.+sh\\b", PCRE2_MULTILINE);

    static const auto LNAV_MATCHERS = lnav::pcre2pp::code::from_const(
        "(?:"
        "^;SELECT\\s+|"
        "^:[a-z0-9\\-]+\\s+"
        ")",
        PCRE2_MULTILINE | PCRE2_CASELESS);

    if (path) {
        while (FILTER_EXTS.count(path->extension()) > 0) {
            path = path->stem();
        }

        auto stem = path->stem();
        auto ext = path->extension();
        if (ext == MD_EXT || ext == MARKDOWN_EXT) {
            return text_format_t::TF_MARKDOWN;
        }

        if (C_EXTS.count(ext) > 0) {
            return text_format_t::TF_C_LIKE;
        }

        if (ext == PY_EXT) {
            return text_format_t::TF_PYTHON;
        }

        if (ext == RS_EXT) {
            return text_format_t::TF_RUST;
        }

        if (ext == SQL_EXT) {
            return text_format_t::TF_SQL;
        }

        if (ext == TOML_EXT) {
            return text_format_t::TF_TOML;
        }

        if (ext == JAVA_EXT) {
            return text_format_t::TF_JAVA;
        }

        if (ext == YAML_EXT || ext == YML_EXT) {
            return text_format_t::TF_YAML;
        }

        if (ext == XML_EXT) {
            return text_format_t::TF_XML;
        }

        if (stem == MAKEFILE_STEM) {
            return text_format_t::TF_MAKEFILE;
        }

        if (ext == SH_EXT) {
            return text_format_t::TF_SHELL_SCRIPT;
        }

        if (ext == LNAV_EXT) {
            return text_format_t::TF_LNAV_SCRIPT;
        }

        if (ext == RST_EXT) {
            return text_format_t::TF_RESTRUCTURED_TEXT;
        }
    }

    {
        auto_mem<yajl_handle_t> jhandle(yajl_free);

        jhandle = yajl_alloc(nullptr, nullptr, nullptr);
        if (yajl_parse(jhandle, sf.udata(), sf.length()) == yajl_status_ok) {
            return text_format_t::TF_JSON;
        }
    }

    if (DIFF_MATCHERS.find_in(sf).ignore_error()) {
        return text_format_t::TF_DIFF;
    }

    if (SH_MATCHERS.find_in(sf).ignore_error()) {
        return text_format_t::TF_SHELL_SCRIPT;
    }

    if (MAN_MATCHERS.find_in(sf).ignore_error()) {
        return text_format_t::TF_MAN;
    }

    if (PYTHON_MATCHERS.find_in(sf).ignore_error()) {
        return text_format_t::TF_PYTHON;
    }

    if (RUST_MATCHERS.find_in(sf).ignore_error()) {
        return text_format_t::TF_RUST;
    }

    if (JAVA_MATCHERS.find_in(sf).ignore_error()) {
        return text_format_t::TF_JAVA;
    }

    if (C_LIKE_MATCHERS.find_in(sf).ignore_error()) {
        return text_format_t::TF_C_LIKE;
    }

    if (LNAV_MATCHERS.find_in(sf).ignore_error()) {
        return text_format_t::TF_LNAV_SCRIPT;
    }

    if (SQL_MATCHERS.find_in(sf).ignore_error()) {
        return text_format_t::TF_SQL;
    }

    if (XML_MATCHERS.find_in(sf).ignore_error()) {
        return text_format_t::TF_XML;
    }

    return text_format_t::TF_UNKNOWN;
}

std::optional<text_format_meta_t>
extract_text_meta(string_fragment sf, text_format_t tf)
{
    static const auto MAN_NAME = lnav::pcre2pp::code::from_const(
        R"(^([A-Za-z][A-Za-z\-_\+0-9]+\(\d\))\s+)", PCRE2_MULTILINE);

    switch (tf) {
        case text_format_t::TF_MAN: {
            thread_local auto md = lnav::pcre2pp::match_data::unitialized();

            auto find_res
                = MAN_NAME.capture_from(sf).into(md).matches().ignore_error();

            if (find_res) {
                return text_format_meta_t{
                    md.to_string(),
                };
            }
            break;
        }
        default:
            break;
    }

    return std::nullopt;
}

template<>
Result<text_format_t, std::string>
from(const string_fragment sf)
{
    for (const auto& [index, format_sf] :
         lnav::itertools::enumerate(TEXT_FORMAT_STRINGS))
    {
        if (format_sf == sf) {
            return Ok(static_cast<text_format_t>(index));
        }
    }
    return Err(fmt::format(FMT_STRING("unrecognized text format: {}"), sf));
}
