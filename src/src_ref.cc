/**
 * Copyright (c) 2025, Timothy Stack
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

#include <vector>

#include "src_ref.hh"

#include "base/intern_string.hh"
#include "base/lnav.console.hh"
#include "base/result.h"
#include "lnav_util.hh"
#include "yajlpp/yajlpp.hh"
#include "yajlpp/yajlpp_def.hh"

namespace lnav {

static const typed_json_path_container<src_ref> ref_handlers = {
    yajlpp::property_handler("file")
        .with_synopsis("<path>")
        .with_description("The path to the source file")
        .for_field(&src_ref::sr_path),
    yajlpp::property_handler("line")
        .with_synopsis("<line-number>")
        .with_description("The line number containing the log statement")
        .for_field(&src_ref::sr_line_number),
    yajlpp::property_handler("name")
        .with_synopsis("<function-name>")
        .with_description(
            "The name of the function containing the log statement")
        .for_field(&src_ref::sr_function_name),
};

template<>
Result<src_ref, std::vector<lnav::console::user_message>>
from_json(const std::string& frag)
{
    static const auto STRING_SRC = intern_string::lookup("string");
    return ref_handlers.parser_for(STRING_SRC).of(frag);
}

}  // namespace lnav
