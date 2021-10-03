/**
 * Copyright (c) 2015, Timothy Stack
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
 */

#ifndef LNAV_COMMAND_EXECUTOR_H
#define LNAV_COMMAND_EXECUTOR_H

#include <sqlite3.h>

#include <future>
#include <string>

#include "fmt/format.h"
#include "optional.hpp"
#include "auto_fd.hh"
#include "attr_line.hh"
#include "shlex.hh"
#include "log_format.hh"
#include "bookmarks.hh"

struct exec_context;

typedef int (*sql_callback_t)(exec_context &ec, sqlite3_stmt *stmt);
int sql_callback(exec_context &ec, sqlite3_stmt *stmt);

typedef std::future<std::string> (*pipe_callback_t)(
    exec_context &ec, const std::string &cmdline, auto_fd &fd);

struct exec_context {
    enum class perm_t {
        READ_WRITE,
        READ_ONLY,
    };

    using output_t = std::pair<FILE *, int(*)(FILE *)>;

    exec_context(std::vector<logline_value> *line_values = nullptr,
                 sql_callback_t sql_callback = ::sql_callback,
                 pipe_callback_t pipe_callback = nullptr)
        : ec_line_values(line_values),
          ec_sql_callback(sql_callback),
          ec_pipe_callback(pipe_callback) {
        this->ec_local_vars.push(std::map<std::string, std::string>());
        this->ec_path_stack.emplace_back(".");
        this->ec_source.emplace("command", 1);
        this->ec_output_stack.emplace_back("screen", nonstd::nullopt);
    }

    bool is_read_write() const {
        return this->ec_perms == perm_t::READ_WRITE;
    }

    bool is_read_only() const {
        return this->ec_perms == perm_t::READ_ONLY;
    }

    exec_context& with_perms(perm_t perms) {
        this->ec_perms = perms;
        return *this;
    }

    std::string get_error_prefix();

    template<typename ...Args>
    Result<std::string, std::string> make_error(
        fmt::string_view format_str, const Args& ...args) {
        return Err(this->get_error_prefix() +
               fmt::vformat(format_str, fmt::make_format_args(args...)));
    }

    nonstd::optional<FILE *> get_output() {
        for (auto iter = this->ec_output_stack.rbegin();
             iter != this->ec_output_stack.rend();
             ++iter) {
            if (iter->second && (*iter->second).first) {
                return (*iter->second).first;
            }
        }

        return nonstd::nullopt;
    }

    void set_output(const std::string& name, FILE *file, int (*closer)(FILE *));

    void clear_output();

    struct source_guard {
        source_guard(exec_context &context) : sg_context(context) {

        }

        ~source_guard() {
            this->sg_context.ec_source.pop();
        }

        exec_context &sg_context;
    };

    struct output_guard {
        explicit output_guard(exec_context &context,
                              std::string name = "default",
                              const nonstd::optional<output_t>& file = nonstd::nullopt);

        ~output_guard();

        exec_context &sg_context;
    };

    source_guard enter_source(const std::string& path, int line_number) {
        this->ec_source.emplace(path, line_number);
        return source_guard(*this);
    }

    scoped_resolver create_resolver() {
        return {
            &this->ec_local_vars.top(),
            &this->ec_global_vars,
        };
    }

    vis_line_t ec_top_line{0_vl};
    bool ec_dry_run{false};
    perm_t ec_perms{perm_t::READ_WRITE};

    std::map<std::string, std::string> ec_override;
    std::vector<logline_value> *ec_line_values;
    std::stack<std::map<std::string, std::string> > ec_local_vars;
    std::map<std::string, std::string> ec_global_vars;
    std::vector<ghc::filesystem::path> ec_path_stack;
    std::stack<std::pair<std::string, int>> ec_source;

    std::vector<std::pair<std::string, nonstd::optional<output_t>>> ec_output_stack;

    attr_line_t ec_accumulator;

    sql_callback_t ec_sql_callback;
    pipe_callback_t ec_pipe_callback;
};

Result<std::string, std::string> execute_command(exec_context &ec, const std::string &cmdline);

Result<std::string, std::string> execute_sql(exec_context &ec, const std::string &sql, std::string &alt_msg);
Result<std::string, std::string> execute_file(exec_context &ec, const std::string &path_and_args, bool multiline = true);
Result<std::string, std::string> execute_any(exec_context &ec, const std::string &cmdline);
void execute_init_commands(exec_context &ec, std::vector<std::pair<Result<std::string, std::string>, std::string> > &msgs);

std::future<std::string> pipe_callback(
    exec_context &ec, const std::string &cmdline, auto_fd &fd);

int sql_progress(const struct log_cursor &lc);
void sql_progress_finished();

void add_global_vars(exec_context &ec);

#endif //LNAV_COMMAND_EXECUTOR_H
