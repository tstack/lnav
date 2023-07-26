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
    static const auto MD_EXT = ghc::filesystem::path(".md");
    static const auto MARKDOWN_EXT = ghc::filesystem::path(".markdown");

    static const auto MAN_MATCHERS = lnav::pcre2pp::code::from_const(
        R"(^[A-Za-z][A-Za-z\-_\+0-9]+\(\d\)\s+)", PCRE2_MULTILINE);

    // XXX This is a pretty crude way of detecting format...
    static const auto PYTHON_MATCHERS = lnav::pcre2pp::code::from_const(
        "(?:"
        "^\\s*def\\s+\\w+\\([^)]*\\):[^\\n]*$|"
        "^\\s*try:[^\\n]*$"
        ")",
        PCRE2_MULTILINE);

    static const auto RUST_MATCHERS
        = lnav::pcre2pp::code::from_const(R"(
(?:
^\s*use\s+[\w+:\{\}]+;$|
^\s*(?:pub)?\s+(?:const|enum|fn)\s+\w+.*$|
^\s*impl\s+\w+.*$
)
)",
                                          PCRE2_MULTILINE);

    static const auto JAVA_MATCHERS = lnav::pcre2pp::code::from_const(
        "(?:"
        "^package\\s+|"
        "^import\\s+|"
        "^\\s*(?:public)?\\s*class\\s*(\\w+\\s+)*\\s*{"
        ")",
        PCRE2_MULTILINE);

    static const auto C_LIKE_MATCHERS = lnav::pcre2pp::code::from_const(
        "(?:"
        "^#\\s*include\\s+|"
        "^#\\s*define\\s+|"
        "^\\s*if\\s+\\([^)]+\\)[^\\n]*$|"
        "^\\s*(?:\\w+\\s+)*class \\w+ {"
        ")",
        PCRE2_MULTILINE);

    static const auto SQL_MATCHERS = lnav::pcre2pp::code::from_const(
        "(?:"
        "select\\s+.+\\s+from\\s+|"
        "insert\\s+into\\s+.+\\s+values"
        ")",
        PCRE2_MULTILINE | PCRE2_CASELESS);

    static const auto XML_MATCHERS = lnav::pcre2pp::code::from_const(
        "(?:"
        R"(<\?xml(\s+\w+\s*=\s*"[^"]*")*\?>|)"
        R"(</?\w+(\s+\w+\s*=\s*"[^"]*")*\s*>)"
        ")",
        PCRE2_MULTILINE | PCRE2_CASELESS);

    text_format_t retval = text_format_t::TF_UNKNOWN;

    if (path) {
        while (FILTER_EXTS.count(path->extension()) > 0) {
            path = path->stem();
        }

        if (path->extension() == MD_EXT || path->extension() == MARKDOWN_EXT) {
            return text_format_t::TF_MARKDOWN;
        }
    }

    {
        auto_mem<yajl_handle_t> jhandle(yajl_free);

        jhandle = yajl_alloc(nullptr, nullptr, nullptr);
        if (yajl_parse(jhandle, sf.udata(), sf.length()) == yajl_status_ok) {
            return text_format_t::TF_JSON;
        }
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

    return retval;
}
