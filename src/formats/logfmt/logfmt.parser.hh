/**
 * Copyright (c) 2021, Timothy Stack
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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file logfmt.parser.hh
 */

#ifndef lnav_logfmt_parser_hh
#define lnav_logfmt_parser_hh

#include "base/intern_string.hh"
#include "base/result.h"
#include "mapbox/variant.hpp"

namespace logfmt {

class parser {
public:
    explicit parser(string_fragment sf);

    struct end_of_input {};
    struct error {
        int e_offset;
        const std::string e_msg;
    };
    struct unquoted_value {
        string_fragment uv_value;
    };
    struct quoted_value {
        string_fragment qv_value;
    };
    struct bool_value {
        bool bv_value{false};
        string_fragment bv_str_value;
    };
    struct int_value {
        int64_t iv_value{0};
        string_fragment iv_str_value;
    };
    struct float_value {
        double fv_value{0};
        string_fragment fv_str_value;
    };
    using value_type = mapbox::util::variant<
        bool_value,
        int_value,
        float_value,
        unquoted_value,
        quoted_value
    >;

    using kvpair = std::pair<string_fragment, value_type>;

    using step_result = mapbox::util::variant<
        end_of_input,
        kvpair,
        error
    >;

    step_result step();
private:
    string_fragment p_next_input;
};

}

#endif
