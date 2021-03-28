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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file readline_curses.hh
 */

#ifndef readline_curses_hh
#define readline_curses_hh

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include <map>
#include <set>
#include <stack>
#include <string>
#include <utility>
#include <vector>
#include <exception>
#include <functional>

#include <readline/readline.h>
#include <readline/history.h>

#include "base/func_util.hh"
#include "base/result.h"
#include "auto_fd.hh"
#include "vt52_curses.hh"
#include "log_format.hh"
#include "help_text_formatter.hh"

struct exec_context;

typedef void (*readline_highlighter_t)(attr_line_t &line, int x);

extern exec_context INIT_EXEC_CONTEXT;

/**
 * Container for information related to different readline contexts.  Since
 * lnav uses readline for different inputs, we need a way to keep things like
 * history and tab-completions separate.
 */
class readline_context {
public:
    typedef Result<std::string, std::string> (*command_func_t)(exec_context &ec,
            std::string cmdline, std::vector<std::string> &args);
    typedef std::string (*prompt_func_t)(exec_context &ec,
        const std::string &cmdline);
    typedef struct _command_t {
        const char *c_name;
        command_func_t c_func;

        struct help_text c_help;
        prompt_func_t c_prompt{nullptr};

        _command_t(const char *name,
                   command_func_t func,
                   help_text help = {},
                   prompt_func_t prompt = nullptr) noexcept
            : c_name(name), c_func(func), c_help(std::move(help)), c_prompt(prompt) {};

        _command_t(command_func_t func) noexcept
            : c_name("anon"), c_func(func) {};
    } command_t;
    typedef std::map<std::string, command_t *> command_map_t;

    readline_context(std::string name,
                     command_map_t *commands = nullptr,
                     bool case_sensitive = true);

    const std::string &get_name() const { return this->rc_name; };

    void load();

    void save();

    void add_possibility(const std::string& type, const std::string& value)
    {
        this->rc_possibilities[type].insert(value);
    };

    void rem_possibility(const std::string& type, const std::string& value)
    {
        this->rc_possibilities[type].erase(value);
    };

    void clear_possibilities(const std::string& type)
    {
        this->rc_possibilities[type].clear();
    };

    bool is_case_sensitive() const
    {
        return this->rc_case_sensitive;
    };

    readline_context &set_append_character(int ch) {
        this->rc_append_character = ch;

        return *this;
    };

    int get_append_character() const {
        return this->rc_append_character;
    }

    readline_context &set_highlighter(readline_highlighter_t hl) {
        this->rc_highlighter = hl;
        return *this;
    };

    readline_context &set_quote_chars(const char *qc) {
        this->rc_quote_chars = qc;

        return *this;
    };

    readline_context &with_readline_var(char **var, const char *val) {
        this->rc_vars.emplace_back(var, val);

        return *this;
    };

    readline_highlighter_t get_highlighter() const {
        return this->rc_highlighter;
    };

    static int command_complete(int, int);

    std::map<std::string, std::string> rc_prefixes;
private:
    static char **attempted_completion(const char *text, int start, int end);
    static char *completion_generator(const char *text, int state);

    static readline_context *     loaded_context;
    static std::set<std::string> *arg_possibilities;

    struct readline_var {
        readline_var(char **dst, const char *val) {
            this->rv_dst.ch = dst;
            this->rv_val.ch = val;
        }

        union {
            char **ch;
        } rv_dst;
        union {
            const char *ch;
        } rv_val;
    };

    std::string   rc_name;
    HISTORY_STATE rc_history;
    std::map<std::string, std::set<std::string> >    rc_possibilities;
    std::map<std::string, std::vector<std::string> > rc_prototypes;
    bool rc_case_sensitive;
    int rc_append_character;
    const char *rc_quote_chars;
    readline_highlighter_t rc_highlighter;
    std::vector<readline_var> rc_vars;
};

/**
 * Adapter between readline and curses.  The curses and readline libraries
 * normally do not get along.  So, we need to put readline in another process
 * and present it with a vt52 interface that we then translate to curses.  The
 * vt52 translation is done by the parent class, vt52_curses, while this class
 * takes care of the communication between the two processes.
 */
class readline_curses
    : public vt52_curses {
public:
    using action = std::function<void(readline_curses*)>;

    class error
        : public std::exception {
public:
        error(int err)
            : e_err(err) { };

        int e_err;
    };

    static const int KEY_TIMEOUT = 750 * 1000;

    static const int VALUE_EXPIRATION = 20;


    readline_curses();
    ~readline_curses() override;

    void add_context(int id, readline_context &rc)
    {
        this->rc_contexts[id] = &rc;
    };

    void set_change_action(const action& va) { this->rc_change = va; };
    void set_perform_action(const action& va) { this->rc_perform = va; };
    void set_alt_perform_action(const action& va) { this->rc_alt_perform = va; };
    void set_timeout_action(const action& va) { this->rc_timeout = va; };
    void set_abort_action(const action& va) { this->rc_abort = va; };
    void set_display_match_action(const action& va) { this->rc_display_match = va; };
    void set_display_next_action(const action& va) { this->rc_display_next = va; };
    void set_blur_action(const action& va) { this->rc_blur = va; };

    void set_value(const std::string &value)
    {
        this->rc_value            = value;
        if (this->rc_value.length() > 1024) {
            this->rc_value = this->rc_value.substr(0, 1024);
        }
        this->rc_value_expiration = time(nullptr) + VALUE_EXPIRATION;
        this->set_needs_update();
    };
    std::string get_value() const { return this->rc_value; };

    std::string get_line_buffer() const {
        return this->rc_line_buffer;
    };

    void set_alt_value(const std::string &value)
    {
        this->rc_alt_value = value;
    };
    std::string get_alt_value() const { return this->rc_alt_value; };

    void update_poll_set(std::vector<struct pollfd> &pollfds)
    {
        pollfds.push_back((struct pollfd) {
                this->rc_pty[RCF_MASTER],
                POLLIN,
                0
        });
        pollfds.push_back((struct pollfd) {
                this->rc_command_pipe[RCF_MASTER],
                POLLIN,
                0
        });
    };

    void handle_key(int ch);

    void check_poll_set(const std::vector<struct pollfd> &pollfds);

    void focus(int context, const std::string& prompt, const std::string& initial = "");

    void set_alt_focus(bool alt_focus) {
        this->rc_is_alt_focus = alt_focus;
    }

    void rewrite_line(int pos, const std::string &value);

    readline_context *get_active_context() const {
        require(this->rc_active_context != -1);

        std::map<int, readline_context *>::const_iterator iter;
        iter = this->rc_contexts.find(this->rc_active_context);
        return iter->second;
    };

    void abort();

    void start();

    void do_update() override;

    void window_change()
    {
        struct winsize ws;

        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
            throw error(errno);
        }
        if (ioctl(this->rc_pty[RCF_MASTER], TIOCSWINSZ, &ws) == -1) {
            throw error(errno);
        }
    };

    void line_ready(const char *line);

    void add_prefix(int context,
                    const std::vector<std::string> &prefix,
                    const std::string &value);

    void clear_prefixes(int context);

    void add_possibility(int context,
                         const std::string &type,
                         const std::string &value);

    void add_possibility(int context,
                         const std::string &type,
                         const char *values[])
    {
        for (int lpc = 0; values[lpc]; lpc++) {
            this->add_possibility(context, type, values[lpc]);
        }
    };

    void add_possibility(int context,
                         const std::string &type,
                         const char **first,
                         const char **last)
    {
        for (; first < last; first++) {
            this->add_possibility(context, type, *first);
        }
    };

    template<template<typename ...> class Container>
    void add_possibility(int context,
                         const std::string &type,
                         const Container<std::string> &values)
    {
        for (const auto &str : values) {
            this->add_possibility(context, type, str);
        }
    };

    void rem_possibility(int context,
                         const std::string &type,
                         const std::string &value);
    void clear_possibilities(int context, std::string type);

    const std::vector<std::string> &get_matches() const {
        return this->rc_matches;
    };

    int get_match_start() const {
        return this->rc_match_start;
    }

    std::string get_match_string() const;

    int get_max_match_length() const {
        return this->rc_max_match_length;
    };

    bool consume_ready_for_input() {
        auto retval = this->rc_ready_for_input;

        this->rc_ready_for_input = false;
        return retval;
    }

private:
    enum {
        RCF_MASTER,
        RCF_SLAVE,

        RCF_MAX_VALUE,
    };

    static void store_matches(char **matches, int num_matches, int max_len);

    friend class readline_context;

    int     rc_active_context{-1};
    pid_t   rc_child{-1};
    auto_fd rc_pty[2];
    auto_fd rc_command_pipe[2];
    std::map<int, readline_context *> rc_contexts;
    std::string rc_value;
    std::string rc_line_buffer;
    time_t      rc_value_expiration{0};
    std::string rc_alt_value;
    int rc_match_start{0};
    int rc_matches_remaining{0};
    int rc_max_match_length{0};
    int rc_match_index{0};
    std::vector<std::string> rc_matches;
    bool rc_is_alt_focus{false};
    bool rc_ready_for_input{false};

    action rc_change;
    action rc_perform;
    action rc_alt_perform;
    action rc_timeout;
    action rc_abort;
    action rc_display_match;
    action rc_display_next;
    action rc_blur;
};
#endif
