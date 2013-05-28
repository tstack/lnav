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

/**
 * Container for information related to different readline contexts.  Since
 * lnav uses readline for different inputs, we need a way to keep things like
 * history and tab-completions separate.
 */
class readline_context {
public:
    typedef std::string (*command_t)(std::string cmdline,
                                     std::vector<std::string> &args);
    typedef std::map<std::string, command_t> command_map_t;

    readline_context(const std::string &name,
                     command_map_t *commands = NULL,
                     bool case_sensitive = true)
        : rc_name(name),
          rc_case_sensitive(case_sensitive)
    {
        char *home;

        if (commands != NULL) {
            command_map_t::iterator iter;

            for (iter = commands->begin(); iter != commands->end(); ++iter) {
                std::string cmd = iter->first;

                this->rc_possibilities["__command"].insert(cmd);
                iter->second(cmd, this->rc_prototypes[cmd]);
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
        fprintf(stderr, "pos %d %p %s %s\n",
                (int)this->rc_possibilities[type].size(),
                &this->rc_possibilities[type],
                type.c_str(),
                value.c_str());
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

    readline_curses();
    virtual ~readline_curses();

    void add_context(int id, readline_context &rc)
    {
        this->rc_contexts[id] = &rc;
    };

    void set_perform_action(action va) { this->rc_perform = va; };
    void set_timeout_action(action va) { this->rc_timeout = va; };

    void set_value(std::string value) { this->rc_value = value; };
    std::string get_value() const { return this->rc_value; };

    int update_fd_set(fd_set &readfds)
    {
        FD_SET(this->rc_pty[RCF_MASTER], &readfds);
        FD_SET(this->rc_command_pipe[RCF_MASTER], &readfds);

        return std::max(this->rc_pty[RCF_MASTER].get(),
                        this->rc_command_pipe[RCF_MASTER].get());
    };

    void handle_key(int ch);

    void check_fd_set(fd_set &ready_rfds);

    void focus(int context, const char *prompt);

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

    void add_possibility(int context, std::string type, std::string value);
    void rem_possibility(int context, std::string type, std::string value);
    void clear_possibilities(int context, std::string type);

private:
    enum {
        RCF_MASTER,
        RCF_SLAVE,

        RCF_MAX_VALUE,
    };

    int     rc_active_context;
    pid_t   rc_child;
    auto_fd rc_pty[2];
    auto_fd rc_command_pipe[2];
    std::map<int, readline_context *> rc_contexts;
    std::string rc_value;

    action rc_perform;
    action rc_timeout;
};
#endif
