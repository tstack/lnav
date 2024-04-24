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

#include "base/lnav_log.hh"
#include "config.h"
#include "pcrepp/pcre2pp.hh"
#include "yajl/api/yajl_parse.h"

text_format_t
detect_text_format(string_fragment sf,
                   nonstd::optional<ghc::filesystem::path> path)
{
    static const std::set<ghc::filesystem::path> FILTER_EXTS = {
        ".bz2",
        ".gz",
        ".lzma",
        ".xz",
        ".zst",
    };
    static const auto C_EXTS = std::set<ghc::filesystem::path>{
        ".h",
        ".hh",
        ".hpp",
        ".c",
        ".cc",
        ".cpp",
        ".tpp",
    };
    static const auto PY_EXT = ghc::filesystem::path(".py");
    static const auto RS_EXT = ghc::filesystem::path(".rs");
    static const auto JAVA_EXT = ghc::filesystem::path(".java");
    static const auto TOML_EXT = ghc::filesystem::path(".toml");
    static const auto XML_EXT = ghc::filesystem::path(".xml");
    static const auto YAML_EXT = ghc::filesystem::path(".yaml");
    static const auto YML_EXT = ghc::filesystem::path(".yml");
    static const auto MAKEFILE_STEM = ghc::filesystem::path("Makefile");
    static const auto MD_EXT = ghc::filesystem::path(".md");
    static const auto MARKDOWN_EXT = ghc::filesystem::path(".markdown");
    static const auto SH_EXT = ghc::filesystem::path(".sh");

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

        if (stem == SH_EXT) {
            return text_format_t::TF_SHELL_SCRIPT;
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

    if (SQL_MATCHERS.find_in(sf).ignore_error()) {
        return text_format_t::TF_SQL;
    }

    if (XML_MATCHERS.find_in(sf).ignore_error()) {
        return text_format_t::TF_XML;
    }

    return text_format_t::TF_UNKNOWN;
}

nonstd::optional<text_format_meta_t>
extract_text_meta(string_fragment sf, text_format_t tf)
{
    static const auto MAN_NAME = lnav::pcre2pp::code::from_const(
        R"(^([A-Za-z][A-Za-z\-_\+0-9]+\(\d\))\s+)", PCRE2_MULTILINE);

    switch (tf) {
        case text_format_t::TF_MAN: {
            static thread_local auto md
                = lnav::pcre2pp::match_data::unitialized();

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

    return nonstd::nullopt;
}
