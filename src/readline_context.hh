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
 * @file readline_context.hh
 */

#ifndef readline_context_hh
#define readline_context_hh

#include <set>
#include <string>

#include <readline/history.h>

#include "base/lnav.console.hh"
#include "base/result.h"
#include "help_text.hh"

class attr_line_t;
struct exec_context;

using readline_highlighter_t = void (*)(attr_line_t& line, int x);

/**
 * Container for information related to different readline contexts.  Since
 * lnav uses readline for different inputs, we need a way to keep things like
 * history and tab-completions separate.
 */
class readline_context {
public:
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
    using splitter_func_t
        = split_result_t (*)(readline_context& rc, const std::string& cmdline);
    using command_t = struct _command_t {
        const char* c_name;
        command_func_t c_func;

        struct help_text c_help;
        prompt_func_t c_prompt{nullptr};
        std::string c_provides;
        std::set<std::string> c_dependencies;

        _command_t(const char* name,
                   command_func_t func,
                   help_text help = {},
                   prompt_func_t prompt = nullptr,
                   std::string provides = {},
                   std::set<std::string> deps = {}) noexcept
            : c_name(name), c_func(func), c_help(std::move(help)),
              c_prompt(prompt), c_provides(provides), c_dependencies(deps)
        {
        }

        _command_t(command_func_t func) noexcept : c_name("anon"), c_func(func)
        {
        }
    };
    typedef std::map<std::string, command_t*> command_map_t;

    readline_context(std::string name,
                     command_map_t* commands = nullptr,
                     bool case_sensitive = true);

    const std::string& get_name() const { return this->rc_name; }

    void load();

    void set_history();

    void save();

    void add_possibility(const std::string& type, const std::string& value)
    {
        this->rc_possibilities[type].insert(value);
    }

    void rem_possibility(const std::string& type, const std::string& value)
    {
        this->rc_possibilities[type].erase(value);
    }

    void clear_possibilities(const std::string& type)
    {
        this->rc_possibilities[type].clear();
    }

    bool is_case_sensitive() const { return this->rc_case_sensitive; }

    readline_context& set_append_character(int ch)
    {
        this->rc_append_character = ch;

        return *this;
    }

    int get_append_character() const { return this->rc_append_character; }

    readline_context& set_highlighter(readline_highlighter_t hl)
    {
        this->rc_highlighter = hl;
        return *this;
    }

    readline_context& set_quote_chars(const char* qc)
    {
        this->rc_quote_chars = qc;

        return *this;
    }

    readline_context& with_readline_var(char** var, const char* val)
    {
        this->rc_vars.emplace_back(var, val);

        return *this;
    }

    readline_highlighter_t get_highlighter() const
    {
        return this->rc_highlighter;
    }

    readline_context& with_splitter(splitter_func_t sf)
    {
        this->rc_splitter = sf;
        return *this;
    }

    static int command_complete(int, int);

    std::map<std::string, std::string> rc_prefixes;

private:
    static char** attempted_completion(const char* text, int start, int end);
    static char* completion_generator(const char* text, int state);

    static readline_context* loaded_context;
    static std::set<std::string>* arg_possibilities;

    struct readline_var {
        readline_var(char** dst, const char* val)
        {
            this->rv_dst.ch = dst;
            this->rv_val.ch = val;
        }

        union {
            char** ch;
        } rv_dst;
        union {
            const char* ch;
        } rv_val;
    };

    std::string rc_name;
    HISTORY_STATE rc_history;
    std::map<std::string, std::set<std::string>> rc_possibilities;
    std::map<std::string, std::vector<std::string>> rc_prototypes;
    std::map<std::string, command_t*> rc_commands;
    bool rc_case_sensitive;
    int rc_append_character{' '};
    const char* rc_quote_chars;
    readline_highlighter_t rc_highlighter;
    std::vector<readline_var> rc_vars;
    splitter_func_t rc_splitter{nullptr};
};

#endif
