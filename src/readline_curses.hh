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

#ifndef __readline_curses_hh
#define __readline_curses_hh

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
#include <string>
#include <vector>
#include <exception>

#include <readline/readline.h>
#include <readline/history.h>

#include "auto_fd.hh"
#include "vt52_curses.hh"

typedef void (*readline_highlighter_t)(attr_line_t &line, int x);

/**
 * Container for information related to different readline contexts.  Since
 * lnav uses readline for different inputs, we need a way to keep things like
 * history and tab-completions separate.
 */
class readline_context {
public:
    typedef std::string (*command_func_t)(
            std::string cmdline, std::vector<std::string> &args);
    typedef struct {
        const char *c_name;
        const char *c_args;
        const char *c_description;
        command_func_t c_func;

        void operator=(command_func_t func) {
            this->c_name = "anon";
            this->c_args = NULL;
            this->c_description = NULL;
            this->c_func = func;
        }
    } command_t;
    typedef std::map<std::string, command_t> command_map_t;

    readline_context(const std::string &name,
                     command_map_t *commands = NULL,
                     bool case_sensitive = true)
        : rc_name(name),
          rc_case_sensitive(case_sensitive),
          rc_quote_chars("\"'"),
          rc_highlighter(NULL)
    {
        char *home;

        if (commands != NULL) {
            command_map_t::iterator iter;

            for (iter = commands->begin(); iter != commands->end(); ++iter) {
                std::string cmd = iter->first;

                this->rc_possibilities["__command"].insert(cmd);
                iter->second.c_func(cmd, this->rc_prototypes[cmd]);
            }
        }

        memset(&this->rc_history, 0, sizeof(this->rc_history));
        history_set_history_state(&this->rc_history);
        home = getenv("HOME");
        if (home) {
            char hpath[2048];

            snprintf(hpath, sizeof(hpath),
                     "%s/.lnav/%s.history",
                     home, this->rc_name.c_str());
            read_history(hpath);
            this->save();
        }

        this->rc_append_character = ' ';
    };

    const std::string &get_name() const { return this->rc_name; };

    void load(void)
    {
        char buffer[128];

        /*
         * XXX Need to keep the input on a single line since the display screws
         * up if it wraps around.
         */
        snprintf(buffer, sizeof(buffer),
                 "set completion-ignore-case %s",
                 this->rc_case_sensitive ? "off" : "on");
        rl_parse_and_bind(buffer); /* NOTE: buffer is modified */

        loaded_context = this;
        rl_attempted_completion_function = attempted_completion;
        history_set_history_state(&this->rc_history);
    };

    void save(void)
    {
        HISTORY_STATE *hs = history_get_history_state();

        this->rc_history = *hs;
        free(hs);
        hs = NULL;
    };

    void add_possibility(std::string type, std::string value)
    {
        this->rc_possibilities[type].insert(value);
    };

    void rem_possibility(std::string type, std::string value)
    {
        this->rc_possibilities[type].erase(value);
    };

    void clear_possibilities(std::string type)
    {
        this->rc_possibilities[type].clear();
    };

    bool is_case_sensitive(void) const
    {
        return this->rc_case_sensitive;
    };

    readline_context &set_append_character(int ch) {
        this->rc_append_character = ch;

        return *this;
    };

    readline_context &set_highlighter(readline_highlighter_t hl) {
        this->rc_highlighter = hl;
        return *this;
    };

    readline_context &set_quote_chars(const char *qc) {
        this->rc_quote_chars = qc;

        return *this;
    };

    readline_highlighter_t get_highlighter() const {
        return this->rc_highlighter;
    };

private:
    static char **attempted_completion(const char *text, int start, int end);
    static char *completion_generator(const char *text, int state);

    static readline_context *     loaded_context;
    static std::set<std::string> *arg_possibilities;

    std::string   rc_name;
    HISTORY_STATE rc_history;
    std::map<std::string, std::set<std::string> >    rc_possibilities;
    std::map<std::string, std::vector<std::string> > rc_prototypes;
    bool rc_case_sensitive;
    int rc_append_character;
    const char *rc_quote_chars;
    readline_highlighter_t rc_highlighter;
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
    typedef view_action<readline_curses> action;

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
    virtual ~readline_curses();

    void add_context(int id, readline_context &rc)
    {
        this->rc_contexts[id] = &rc;
    };

    void set_change_action(action va) { this->rc_change = va; };
    void set_perform_action(action va) { this->rc_perform = va; };
    void set_timeout_action(action va) { this->rc_timeout = va; };
    void set_abort_action(action va) { this->rc_abort = va; };
    void set_display_match_action(action va) { this->rc_display_match = va; };
    void set_display_next_action(action va) { this->rc_display_next = va; };
    void set_blur_action(action va) { this->rc_blur = va; };

    void set_value(const std::string &value)
    {
        this->rc_value            = value;
        this->rc_value_expiration = time(NULL) + VALUE_EXPIRATION;
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

    void focus(int context, const char *prompt);

    readline_context *get_active_context() const {
        require(this->rc_active_context != -1);

        std::map<int, readline_context *>::const_iterator iter;
        iter = this->rc_contexts.find(this->rc_active_context);
        return iter->second;
    };

    void abort();

    void start(void);

    void do_update(void);

    void window_change(void)
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
                         const std::vector<std::string> &values)
    {
        for (std::vector<std::string>::const_iterator iter = values.begin();
             iter != values.end();
             ++iter) {
            this->add_possibility(context, type, *iter);
        }
    };

    void rem_possibility(int context,
                         const std::string &type,
                         const std::string &value);
    void clear_possibilities(int context, std::string type);

    const std::vector<std::string> &get_matches() const {
        return this->rc_matches;
    };

    int get_max_match_length() const {
        return this->rc_max_match_length;
    };

private:
    enum {
        RCF_MASTER,
        RCF_SLAVE,

        RCF_MAX_VALUE,
    };

    static void store_matches(char **matches, int num_matches, int max_len);

    friend class readline_context;

    int     rc_active_context;
    pid_t   rc_child;
    auto_fd rc_pty[2];
    auto_fd rc_command_pipe[2];
    std::map<int, readline_context *> rc_contexts;
    std::string rc_value;
    std::string rc_line_buffer;
    time_t      rc_value_expiration;
    std::string rc_alt_value;
    int rc_matches_remaining;
    int rc_max_match_length;
    std::vector<std::string> rc_matches;

    action rc_change;
    action rc_perform;
    action rc_timeout;
    action rc_abort;
    action rc_display_match;
    action rc_display_next;
    action rc_blur;
};
#endif
