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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef LNAV_COMMAND_EXECUTOR_H
#define LNAV_COMMAND_EXECUTOR_H

#include <filesystem>
#include <future>
#include <optional>
#include <stack>
#include <string>

#include <sqlite3.h>

#include "base/auto_fd.hh"
#include "base/lnav.console.hh"
#include "db_sub_source.hh"
#include "fmt/format.h"
#include "help_text.hh"
#include "shlex.resolver.hh"
#include "vis_line.hh"

struct exec_context;
class attr_line_t;
class logline_value;
struct logline_value_vector;

using sql_callback_t = int (*)(exec_context&, sqlite3_stmt*);
int sql_callback(exec_context& ec, sqlite3_stmt* stmt);
int internal_sql_callback(exec_context& ec, sqlite3_stmt* stmt);

using pipe_callback_t
    = std::future<std::string> (*)(exec_context&, const std::string&, auto_fd&);

using error_callback_t
    = std::function<void(const lnav::console::user_message&)>;

struct exec_context {
    enum class perm_t {
        READ_WRITE,
        READ_ONLY,
    };

    using output_t = std::pair<FILE*, int (*)(FILE*)>;

    exec_context(logline_value_vector* line_values = nullptr,
                 sql_callback_t sql_callback = ::sql_callback,
                 pipe_callback_t pipe_callback = nullptr);

    bool is_read_write() const { return this->ec_perms == perm_t::READ_WRITE; }

    bool is_read_only() const { return this->ec_perms == perm_t::READ_ONLY; }

    exec_context& with_perms(perm_t perms)
    {
        this->ec_perms = perms;
        return *this;
    }

    void add_error_context(lnav::console::user_message& um);

    template<typename... Args>
    lnav::console::user_message make_error_msg(fmt::string_view format_str,
                                               const Args&... args)
    {
        auto retval = lnav::console::user_message::error(
            fmt::vformat(format_str, fmt::make_format_args(args...)));

        this->add_error_context(retval);

        return retval;
    }

    template<typename... Args>
    Result<std::string, lnav::console::user_message> make_error(
        fmt::string_view format_str, const Args&... args)
    {
        return Err(this->make_error_msg(format_str, args...));
    }

    std::optional<FILE*> get_output()
    {
        for (auto iter = this->ec_output_stack.rbegin();
             iter != this->ec_output_stack.rend();
             ++iter)
        {
            if (iter->second && (*iter->second).first) {
                return (*iter->second).first;
            }
        }

        return std::nullopt;
    }

    void set_output(const std::string& name, FILE* file, int (*closer)(FILE*));

    void clear_output();

    struct mouse_input {};
    struct user {};
    struct file_open {
        std::string fo_name;
    };

    using provenance_t = mapbox::util::variant<user, mouse_input, file_open>;

    struct provenance_guard {
        explicit provenance_guard(exec_context* context, provenance_t prov)
            : pg_context(context)
        {
            this->pg_context->ec_provenance.push_back(prov);
        }

        provenance_guard(const provenance_guard&) = delete;
        provenance_guard(provenance_guard&& other)
            : pg_context(other.pg_context)
        {
            other.pg_context = nullptr;
        }

        ~provenance_guard()
        {
            if (this->pg_context != nullptr) {
                this->pg_context->ec_provenance.pop_back();
            }
        }

        exec_context* operator->() { return this->pg_context; }

        exec_context* pg_context;
    };

    provenance_guard with_provenance(provenance_t prov)
    {
        return provenance_guard{this, prov};
    }

    struct source_guard {
        source_guard(exec_context* context) : sg_context(context) {}

        source_guard(const source_guard&) = delete;

        source_guard(source_guard&& other)
            : sg_context(std::exchange(other.sg_context, nullptr))
        {
        }

        ~source_guard()
        {
            if (this->sg_context != nullptr) {
                this->sg_context->ec_source.pop_back();
            }
        }

        exec_context* sg_context;
    };

    struct output_guard {
        explicit output_guard(exec_context& context,
                              std::string name = "default",
                              const std::optional<output_t>& file
                              = std::nullopt);

        ~output_guard();

        exec_context& sg_context;
    };

    struct sql_callback_guard {
        sql_callback_guard(exec_context& context, sql_callback_t cb);

        sql_callback_guard(const sql_callback_guard&) = delete;

        sql_callback_guard(sql_callback_guard&& other);

        ~sql_callback_guard();

        exec_context& scg_context;
        sql_callback_t scg_old_callback;
    };

    sql_callback_guard push_callback(sql_callback_t cb);

    source_guard enter_source(intern_string_t path,
                              int line_number,
                              const std::string& content);

    struct db_source_guard {
        explicit db_source_guard(exec_context* context) : dsg_context(context) {}

        db_source_guard(const db_source_guard&) = delete;

        db_source_guard(db_source_guard&& other) noexcept
            : dsg_context(std::exchange(other.dsg_context, nullptr))
        {
        }

        ~db_source_guard()
        {
            if (this->dsg_context != nullptr) {
                this->dsg_context->ec_label_source_stack.pop_back();
            }
        }

        exec_context* dsg_context;
    };

    db_source_guard enter_db_source(db_label_source* dls)
    {
        this->ec_label_source_stack.push_back(dls);

        return db_source_guard{this};
    }

    struct error_cb_guard {
        explicit error_cb_guard(exec_context* context) : sg_context(context) {}

        error_cb_guard(const error_cb_guard&) = delete;
        error_cb_guard(error_cb_guard&& other) noexcept
            : sg_context(other.sg_context)
        {
            other.sg_context = nullptr;
        }

        ~error_cb_guard()
        {
            if (this->sg_context != nullptr) {
                this->sg_context->ec_error_callback_stack.pop_back();
            }
        }

        exec_context* sg_context;
    };

    error_cb_guard add_error_callback(error_callback_t cb)
    {
        this->ec_error_callback_stack.emplace_back(std::move(cb));
        return error_cb_guard{this};
    }

    scoped_resolver create_resolver()
    {
        return {
            &this->ec_local_vars.top(),
            &this->ec_global_vars,
        };
    }

    Result<std::string, lnav::console::user_message> execute(
        const std::string& cmdline);

    using kv_pair_t = std::pair<std::string, std::string>;

    Result<std::string, lnav::console::user_message> execute_with_int(
        const std::string& cmdline)
    {
        return this->execute(cmdline);
    }

    template<typename... Args>
    Result<std::string, lnav::console::user_message> execute_with_int(
        const std::string& cmdline, kv_pair_t pair, Args... args)
    {
        this->ec_local_vars.top().emplace(pair);
        return this->execute(cmdline, args...);
    }

    template<typename... Args>
    Result<std::string, lnav::console::user_message> execute_with(
        const std::string& cmdline, Args... args)
    {
        this->ec_local_vars.push({});
        auto retval = this->execute_with_int(cmdline, args...);
        this->ec_local_vars.pop();

        return retval;
    }

    template<typename T>
    std::optional<T> get_provenance() const
    {
        for (const auto& elem : this->ec_provenance) {
            if (elem.is<T>()) {
                return elem.get<T>();
            }
        }

        return std::nullopt;
    }

    vis_line_t ec_top_line{0_vl};
    bool ec_dry_run{false};
    perm_t ec_perms{perm_t::READ_WRITE};

    logline_value_vector* ec_line_values;
    std::stack<std::map<std::string, scoped_value_t>> ec_local_vars;
    std::vector<provenance_t> ec_provenance;
    std::map<std::string, scoped_value_t> ec_global_vars;
    std::vector<std::filesystem::path> ec_path_stack;
    std::vector<lnav::console::snippet> ec_source;
    help_text* ec_current_help{nullptr};

    std::vector<std::pair<std::string, std::optional<output_t>>>
        ec_output_stack;

    std::unique_ptr<attr_line_t> ec_accumulator;

    sql_callback_t ec_sql_callback;
    pipe_callback_t ec_pipe_callback;
    std::vector<error_callback_t> ec_error_callback_stack;
    std::vector<db_label_source*> ec_label_source_stack;

    struct ui_callbacks {
        std::function<void()> uc_pre_stdout_write;
        std::function<void()> uc_post_stdout_write;
        std::function<void()> uc_redraw;
    };
    ui_callbacks ec_ui_callbacks;
};

Result<std::string, lnav::console::user_message> execute_command(
    exec_context& ec, const std::string& cmdline);

Result<std::string, lnav::console::user_message> execute_sql(
    exec_context& ec, const std::string& sql, std::string& alt_msg);

class multiline_executor {
public:
    exec_context& me_exec_context;
    std::string me_source;
    std::optional<std::string> me_cmdline;
    int me_line_number{0};
    int me_starting_line_number{0};
    std::string me_last_result;

    multiline_executor(exec_context& ec, std::string src)
        : me_exec_context(ec), me_source(src)
    {
    }

    Result<void, lnav::console::user_message> push_back(string_fragment line);

    Result<std::string, lnav::console::user_message> final();
};

Result<std::string, lnav::console::user_message> execute_file(
    exec_context& ec, const std::string& path_and_args);
Result<std::string, lnav::console::user_message> execute_any(
    exec_context& ec, const std::string& cmdline);
void execute_init_commands(
    exec_context& ec,
    std::vector<std::pair<Result<std::string, lnav::console::user_message>,
                          std::string>>& msgs);

std::future<std::string> pipe_callback(exec_context& ec,
                                       const std::string& cmdline,
                                       auto_fd& fd);

int sql_progress(const struct log_cursor& lc);
void sql_progress_finished();

void add_global_vars(exec_context& ec);

#endif  // LNAV_COMMAND_EXECUTOR_H
