/**
 * Copyright (c) 2007-2012, Timothy Stack
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
 * @file readline_curses.hh
 */

#ifndef readline_curses_hh
#define readline_curses_hh

#include <cstdio>
#include <exception>
#include <functional>
#include <map>
#include <set>
#include <stack>
#include <string>
#include <utility>
#include <vector>

#include <errno.h>
#include <poll.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>

#include "base/auto_fd.hh"
#include "base/enum_util.hh"
#include "base/func_util.hh"
#include "base/result.h"
#include "help_text_formatter.hh"
#include "log_format.hh"
#include "pollable.hh"
#include "readline_context.hh"
#include "vt52_curses.hh"

extern exec_context INIT_EXEC_CONTEXT;

/**
 * Adapter between readline and curses.  The curses and readline libraries
 * normally do not get along.  So, we need to put readline in another process
 * and present it with a vt52 interface that we then translate to curses.  The
 * vt52 translation is done by the parent class, vt52_curses, while this class
 * takes care of the communication between the two processes.
 */
class readline_curses
    : public vt52_curses
    , public pollable {
public:
    using action = std::function<void(readline_curses*)>;

    class error : public std::exception {
    public:
        error(int err) : e_err(err) {}

        int e_err;
    };

    static const int KEY_TIMEOUT = 750 * 1000;

    static const int VALUE_EXPIRATION = 20;

    readline_curses(std::shared_ptr<pollable_supervisor>);
    ~readline_curses() override;

    using injectable = readline_curses(std::shared_ptr<pollable_supervisor>);

    void add_context(int id, readline_context& rc)
    {
        this->rc_contexts[id] = &rc;
    }

    template<typename T,
             typename... Args,
             std::enable_if_t<std::is_enum<T>::value, bool> = true>
    void add_context(T context, Args&... args)
    {
        this->add_context(lnav::enums::to_underlying(context), args...);
    }

    void set_focus_action(const action& va) { this->rc_focus = va; }

    void set_change_action(const action& va) { this->rc_change = va; }

    void set_perform_action(const action& va) { this->rc_perform = va; }

    void set_alt_perform_action(const action& va) { this->rc_alt_perform = va; }

    void set_timeout_action(const action& va) { this->rc_timeout = va; }

    void set_abort_action(const action& va) { this->rc_abort = va; }

    void set_display_match_action(const action& va)
    {
        this->rc_display_match = va;
    }

    void set_display_next_action(const action& va)
    {
        this->rc_display_next = va;
    }

    void set_blur_action(const action& va) { this->rc_blur = va; }

    void set_completion_request_action(const action& va)
    {
        this->rc_completion_request = va;
    }

    void set_value(const std::string& value);

    void set_attr_value(const attr_line_t& al);

    void clear_value() { this->rc_value.clear(); }

    const attr_line_t& get_value() const { return this->rc_value; }

    std::string get_line_buffer() const { return this->rc_line_buffer; }

    void set_alt_value(const std::string& value) { this->rc_alt_value = value; }

    std::string get_alt_value() const { return this->rc_alt_value; }

    void update_poll_set(std::vector<struct pollfd>& pollfds) override;

    void handle_key(int ch);

    void check_poll_set(const std::vector<struct pollfd>& pollfds) override;

    void focus(int context,
               const std::string& prompt,
               const std::string& initial = "");

    template<typename T,
             typename... Args,
             std::enable_if_t<std::is_enum<T>::value, bool> = true>
    void focus(T context, const Args&... args)
    {
        this->focus(lnav::enums::to_underlying(context), args...);
    }

    void set_alt_focus(bool alt_focus) { this->rc_is_alt_focus = alt_focus; }

    void rewrite_line(int pos, const std::string& value);

    void set_suggestion(const std::string& value);

    bool is_active() const { return this->rc_active_context != -1; }

    readline_context* get_active_context() const
    {
        require(this->rc_active_context != -1);

        std::map<int, readline_context*>::const_iterator iter;
        iter = this->rc_contexts.find(this->rc_active_context);
        return iter->second;
    }

    void abort();

    void start();

    bool do_update() override;

    bool handle_mouse(mouse_event& me) override;

    void window_change();

    void line_ready(const char* line);

    void add_prefix(int context,
                    const std::vector<std::string>& prefix,
                    const std::string& value);

    void clear_prefixes(int context);

    template<typename T,
             typename... Args,
             std::enable_if_t<std::is_enum<T>::value, bool> = true>
    void add_prefix(T context, const Args&... args)
    {
        this->add_prefix(lnav::enums::to_underlying(context), args...);
    }

    template<typename T, std::enable_if_t<std::is_enum<T>::value, bool> = true>
    void clear_prefixes(T context)
    {
        this->clear_prefixes(lnav::enums::to_underlying(context));
    }

    void add_possibility(int context,
                         const std::string& type,
                         const std::string& value);

    void add_possibility(int context,
                         const std::string& type,
                         const char* values[])
    {
        for (int lpc = 0; values[lpc]; lpc++) {
            this->add_possibility(context, type, values[lpc]);
        }
    }

    void add_possibility(int context,
                         const std::string& type,
                         const char** first,
                         const char** last)
    {
        for (; first < last; first++) {
            this->add_possibility(context, type, *first);
        }
    }

    template<template<typename...> class Container>
    void add_possibility(int context,
                         const std::string& type,
                         const Container<std::string>& values)
    {
        for (const auto& str : values) {
            this->add_possibility(context, type, str);
        }
    }

    void rem_possibility(int context,
                         const std::string& type,
                         const std::string& value);
    void clear_possibilities(int context, std::string type);

    template<typename T,
             typename... Args,
             std::enable_if_t<std::is_enum<T>::value, bool> = true>
    void add_possibility(T context, Args... args)
    {
        this->add_possibility(lnav::enums::to_underlying(context), args...);
    }

    template<typename T,
             typename... Args,
             std::enable_if_t<std::is_enum<T>::value, bool> = true>
    void rem_possibility(T context, const Args&... args)
    {
        this->rem_possibility(lnav::enums::to_underlying(context), args...);
    }

    template<typename T,
             typename... Args,
             std::enable_if_t<std::is_enum<T>::value, bool> = true>
    void clear_possibilities(T context, Args... args)
    {
        this->clear_possibilities(lnav::enums::to_underlying(context), args...);
    }

    void append_to_history(int context, const std::string& line);

    const std::vector<std::string>& get_matches() const
    {
        return this->rc_matches;
    }

    int get_match_start() const { return this->rc_match_start; }

    std::string get_match_string() const;

    int get_max_match_length() const { return this->rc_max_match_length; }

    bool consume_ready_for_input()
    {
        auto retval = this->rc_ready_for_input;

        this->rc_ready_for_input = false;
        return retval;
    }

    std::string get_remote_complete_path() const
    {
        return this->rc_remote_complete_path;
    }

    void set_save_history(bool value) { this->rc_save_history = value; }

private:
    enum {
        RCF_MASTER,
        RCF_SLAVE,

        RCF_MAX_VALUE,
    };

    static void store_matches(char** matches, int num_matches, int max_len);

    friend class readline_context;

    bool rc_save_history{true};
    int rc_active_context{-1};
    pid_t rc_child{-1};
    auto_fd rc_pty[2];
    auto_fd rc_command_pipe[2];
    std::map<int, readline_context*> rc_contexts;
    attr_line_t rc_value;
    std::string rc_line_buffer;
    time_t rc_value_expiration{0};
    std::string rc_alt_value;
    int rc_match_start{0};
    int rc_matches_remaining{0};
    int rc_max_match_length{0};
    int rc_match_index{0};
    std::vector<std::string> rc_matches;
    bool rc_is_alt_focus{false};
    bool rc_ready_for_input{false};
    std::string rc_remote_complete_path;
    std::string rc_suggestion;

    action rc_focus;
    action rc_change;
    action rc_perform;
    action rc_alt_perform;
    action rc_timeout;
    action rc_abort;
    action rc_display_match;
    action rc_display_next;
    action rc_blur;
    action rc_completion_request;
};

#endif
