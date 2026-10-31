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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef lnav_logfile_sub_source_cfg_hh
#define lnav_logfile_sub_source_cfg_hh

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "pcrepp/pcre2pp.hh"
#include "yajlpp/yajlpp.hh"

namespace logfile_sub_source_ns {

enum class time_column_feature_t {
    Disabled,
    Enabled,
    Default,
};

struct watch_expression {
    std::string we_expr;
    bool we_enabled{true};
};

/**
 * A source language whose stack frames a /log/stack-frames pattern matches.
 * Declared ahead in logfile_sub_source.hh, so the underlying type is spelled
 * out.
 */
enum class source_language_t : uint8_t {
    C,
    Cpp,
    CSharp,
    FSharp,
    Go,
    Java,
    JavaScript,
    Kotlin,
    Php,
    Python,
    Ruby,
    Rust,
    TypeScript,
    VisualBasic,
};

/**
 * A pattern for a frame in a stack trace.  The "file" and "line" captures
 * are required, "col" and "pkg" (a dotted package that is turned into a
 * directory prefix of the file) are optional.
 */
struct stack_frame_def {
    bool sfd_enabled{true};
    factory_container<lnav::pcre2pp::code> sfd_pattern;
    /**
     * The source languages whose frames the pattern matches.  Empty for a
     * pattern that's always used.
     */
    std::vector<source_language_t> sfd_languages;

    /** Set when the pattern has passed the checks in reload_config(). */
    bool sfd_valid{false};
    int sfd_file_index{-1};
    int sfd_line_index{-1};
    int sfd_col_index{-1};
    int sfd_pkg_index{-1};
};

struct config {
    std::map<std::string, watch_expression> c_watch_exprs;
    std::map<std::string, stack_frame_def> c_stack_frames;
    time_column_feature_t c_time_column{time_column_feature_t::Disabled};
};

}  // namespace logfile_sub_source_ns

#endif
