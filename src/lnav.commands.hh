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
 *
 * @file lnav.commands.hh
 */

#ifndef lnav_commands_hh
#define lnav_commands_hh

#include <map>
#include <string>
#include <vector>

#include "base/intern_string.hh"
#include "base/lnav.console.hh"
#include "base/result.h"
#include "help_text.hh"

struct exec_context;

namespace lnav::commands {

using command_func_t = Result<std::string, lnav::console::user_message> (*)(
    exec_context& ec, std::string cmdline, std::vector<std::string>& args);

struct prompt_result_t {
    std::string pr_new_prompt;
    std::string pr_suggestion;
};

struct stage {
    std::vector<line_range> s_args;
};

struct split_result_t {
    std::vector<stage> sr_stages;
};

using prompt_func_t
    = prompt_result_t (*)(exec_context& ec, const std::string& cmdline);

struct command_t {
    string_fragment c_name;
    command_func_t c_func;

    help_text c_help;
    prompt_func_t c_prompt{nullptr};
    /**
     * What this command needs before it can be used, or empty when it
     * needs nothing.  Only ever tested for emptiness, to keep commands
     * that depend on something out of the completions for a fresh
     * prompt.
     */
    string_fragment c_dependencies;

    command_t(const char* name,
              command_func_t func,
              help_text help = {},
              prompt_func_t prompt = nullptr,
              string_fragment deps = {}) noexcept
        : c_name(name), c_func(func), c_help(std::move(help)),
          c_prompt(prompt), c_dependencies(deps)
    {
    }

    explicit command_t(command_func_t func) noexcept
        : c_name("anon"), c_func(func)
    {
    }
};

using command_map_t = std::map<string_fragment, command_t*>;

}  // namespace lnav::commands

#endif
