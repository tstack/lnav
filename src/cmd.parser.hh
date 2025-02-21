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

#ifndef lnav_cmd_parser_hh
#define lnav_cmd_parser_hh

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/intern_string.hh"
#include "base/lnav.console.hh"
#include "base/result.h"
#include "command_executor.hh"
#include "help_text.hh"
#include "shlex.hh"

namespace lnav::command {

struct parsed {
    struct arg_t {
        const help_text* a_help;
        std::vector<shlex::split_element_t> a_values;
    };

    struct arg_at_result {
        const help_text* aar_help;
        bool aar_required;
        shlex::split_element_t aar_element;
    };

    std::optional<arg_at_result> arg_at(int x) const;

    const help_text* p_help;
    std::map<std::string, arg_t> p_args;
};

parsed parse_for_prompt(exec_context& ec,
                        string_fragment args,
                        const help_text& ht);
Result<parsed, lnav::console::user_message> parse_for_call(exec_context& ec,
                                                           string_fragment args,
                                                           const help_text& ht);

}  // namespace lnav::command

#endif
