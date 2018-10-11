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

#include "auto_fd.hh"
#include "attr_line.hh"
#include "textview_curses.hh"

struct exec_context;

typedef int (*sql_callback_t)(exec_context &ec, sqlite3_stmt *stmt);

typedef std::future<std::string> (*pipe_callback_t)(
    exec_context &ec, const std::string &cmdline, auto_fd &fd);

struct exec_context {
    exec_context(std::vector<logline_value> *line_values = NULL,
                 sql_callback_t sql_callback = NULL,
                 pipe_callback_t pipe_callback = NULL)
        : ec_top_line(vis_line_t(0)),
          ec_dry_run(false),
          ec_line_values(line_values),
          ec_sql_callback(sql_callback),
          ec_pipe_callback(pipe_callback) {
        this->ec_local_vars.push(std::map<std::string, std::string>());
        this->ec_path_stack.emplace_back(".");
        this->ec_source.emplace("unknown", 0);
    }

    std::string get_error_prefix() {
        if (this->ec_source.size() <= 1) {
            return "error: ";
        }

        std::pair<std::string, int> source = this->ec_source.top();

        return "error:" + source.first + ":" + std::to_string(source.second) + ":";
    }

    vis_line_t ec_top_line;
    bool ec_dry_run;

    std::map<std::string, std::string> ec_override;
    std::vector<logline_value> *ec_line_values;
    std::stack<std::map<std::string, std::string> > ec_local_vars;
    std::map<std::string, std::string> ec_global_vars;
    std::vector<std::string> ec_path_stack;
    std::stack<std::pair<std::string, int>> ec_source;

    attr_line_t ec_accumulator;

    sql_callback_t ec_sql_callback;
    pipe_callback_t ec_pipe_callback;
};

std::string execute_command(exec_context &ec, const std::string &cmdline);

std::string execute_sql(exec_context &ec, const std::string &sql, std::string &alt_msg);
std::string execute_file(exec_context &ec, const std::string &path_and_args, bool multiline = true);
std::string execute_any(exec_context &ec, const std::string &cmdline);
void execute_init_commands(exec_context &ec, std::vector<std::pair<std::string, std::string> > &msgs);

int sql_callback(exec_context &ec, sqlite3_stmt *stmt);
std::future<std::string> pipe_callback(
    exec_context &ec, const std::string &cmdline, auto_fd &fd);

int sql_progress(const struct log_cursor &lc);

void add_global_vars(exec_context &ec);

extern bookmark_type_t BM_QUERY;

#endif //LNAV_COMMAND_EXECUTOR_H
