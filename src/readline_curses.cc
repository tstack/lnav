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
 * @file readline_curses.cc
 */

#include "config.h"

#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>

#ifdef HAVE_PTY_H
#include <pty.h>
#endif

#ifdef HAVE_UTIL_H
#include <util.h>
#endif

#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif

#include <string>

#include "pcrepp.hh"
#include "auto_mem.hh"
#include "lnav_log.hh"
#include "lnav_util.hh"
#include "ansi_scrubber.hh"
#include "readline_curses.hh"

using namespace std;

static int              got_line    = 0;
static sig_atomic_t     got_timeout = 0;
static sig_atomic_t     got_winch   = 0;
static readline_curses *child_this;
static sig_atomic_t     looping      = 1;
static const int        HISTORY_SIZE = 256;

static const char *RL_INIT[] = {
    /*
     * XXX Need to keep the input on a single line since the display screws
     * up if it wraps around.
     */
    "set horizontal-scroll-mode on",

    NULL
};

readline_context *readline_context::loaded_context;
set<string> *     readline_context::arg_possibilities;
static string last_match_str;
static bool last_match_str_valid;

static void sigalrm(int sig)
{
    got_timeout = 1;
}

static void sigwinch(int sig)
{
    got_winch = 1;
}

static void sigterm(int sig)
{
    looping = 0;
}

static void line_ready_tramp(char *line)
{
    child_this->line_ready(line);
    got_line = 1;
    rl_callback_handler_remove();
}

static int sendall(int sock, const char *buf, size_t len)
{
    off_t offset = 0;

    while (len > 0) {
        int rc = send(sock, &buf[offset], len, 0);

        if (rc == -1) {
            switch (errno) {
            case EAGAIN:
            case EINTR:
                break;
            default:
                return -1;
            }
        }
        else {
            len -= rc;
            offset += rc;
        }
    }

    return 0;
}

static int sendstring(int sock, const char *buf, size_t len)
{
    if (sendall(sock, (char *)&len, sizeof(len)) == -1) {
        return -1;
    }
    else if (sendall(sock, buf, len) == -1) {
        return -1;
    }

    return 0;
}

static int sendcmd(int sock, char cmd, const char *buf, size_t len)
{
    size_t total_len = len + 2;
    char prefix[2] = { cmd, ':' };

    if (sendall(sock, (char *)&total_len, sizeof(total_len)) == -1) {
        return -1;
    }
    else if (sendall(sock, prefix, sizeof(prefix)) == -1 ||
             sendall(sock, buf, len) == -1) {
        return -1;
    }

    return 0;
}

static int recvall(int sock, char *buf, size_t len)
{
    off_t offset = 0;

    while (len > 0) {
        int rc = recv(sock, &buf[offset], len, 0);

        if (rc == -1) {
            switch (errno) {
            case EAGAIN:
            case EINTR:
                break;
            default:
                return -1;
            }
        }
        else if (rc == 0) {
            errno = EIO;
            return -1;
        }
        else {
            len -= rc;
            offset += rc;
        }
    }

    return 0;
}

static ssize_t recvstring(int sock, char *buf, size_t len)
{
    ssize_t retval;

    if (recvall(sock, (char *)&retval, sizeof(retval)) == -1) {
        return -1;
    }
    else if (retval > (ssize_t)len) {
        return -1;
    }
    else if (recvall(sock, buf, retval) == -1) {
        return -1;
    }

    return retval;
}

char *readline_context::completion_generator(const char *text, int state)
{
    static vector<string> matches;

    char *retval = NULL;

    if (state == 0) {
        int len = strlen(text);

        matches.clear();
        if (arg_possibilities != NULL) {
            set<string>::iterator iter;

            for (iter = arg_possibilities->begin();
                 iter != arg_possibilities->end();
                 ++iter) {
                int (*cmpfunc)(const char *, const char *, size_t);
                const char *poss_str = iter->c_str();

                cmpfunc = (loaded_context->is_case_sensitive() ?
                           strncmp : strncasecmp);
                // Check for an exact match and for the quoted version.
                if (cmpfunc(text, poss_str, len) == 0 ||
                        ((strchr(loaded_context->rc_quote_chars, poss_str[0]) != NULL) &&
                         cmpfunc(text, &poss_str[1], len) == 0)) {
                    matches.push_back(*iter);
                }
            }
        }

        if (matches.size() == 1) {
            if (strcmp(text, matches[0].c_str()) == 0) {
                matches.pop_back();
            }

            last_match_str_valid = false;
            if (sendstring(child_this->rc_command_pipe[readline_curses::RCF_SLAVE],
                           "m:0:0",
                           5) == -1) {
                _exit(1);
            }
        }
    }

    if (!matches.empty()) {
        retval = strdup(matches.back().c_str());
        matches.pop_back();
    }

    return retval;
}

char **readline_context::attempted_completion(const char *text,
                                              int start,
                                              int end)
{
    char **retval = NULL;

    if (loaded_context->rc_possibilities.find("*") !=
        loaded_context->rc_possibilities.end()) {
        arg_possibilities = &loaded_context->rc_possibilities["*"];
        rl_completion_append_character = loaded_context->rc_append_character;
    }
    else if (start == 0) {
        arg_possibilities = &loaded_context->rc_possibilities["__command"];
        rl_completion_append_character = loaded_context->rc_append_character;
    }
    else {
        char * space;
        string cmd;

        rl_completion_append_character = 0;
        space = strchr(rl_line_buffer, ' ');
        require(space != NULL);
        cmd = string(rl_line_buffer, space - rl_line_buffer);

        vector<string> &proto = loaded_context->rc_prototypes[cmd];

        if (proto.empty()) {
            arg_possibilities = NULL;
        }
        else if (proto[0] == "filename") {
            return NULL; /* XXX */
        }
        else {
            arg_possibilities = &(loaded_context->rc_possibilities[proto[0]]);
        }
    }

    retval = rl_completion_matches(text, completion_generator);
    if (retval == NULL) {
        rl_attempted_completion_over = 1;
    }

    return retval;
}

readline_curses::readline_curses()
    : rc_active_context(-1),
      rc_child(-1),
      rc_value_expiration(0),
      rc_matches_remaining(0),
      rc_max_match_length(0)
{
    struct winsize ws;
    int            sp[2];

    if (socketpair(PF_UNIX, SOCK_STREAM, 0, sp) < 0) {
        throw error(errno);
    }

    this->rc_command_pipe[RCF_MASTER] = sp[RCF_MASTER];
    this->rc_command_pipe[RCF_SLAVE]  = sp[RCF_SLAVE];

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        throw error(errno);
    }

    if (openpty(this->rc_pty[RCF_MASTER].out(),
                this->rc_pty[RCF_SLAVE].out(),
                NULL,
                NULL,
                &ws) < 0) {
        perror("error: failed to open terminal(openpty)");
        throw error(errno);
    }

    if ((this->rc_child = fork()) == -1) {
        throw error(errno);
    }

    if (this->rc_child == 0) {
        char buffer[1024];

        this->rc_command_pipe[RCF_MASTER].reset();
        this->rc_pty[RCF_MASTER].reset();

        signal(SIGALRM, sigalrm);
        signal(SIGWINCH, sigwinch);
        signal(SIGINT, sigterm);
        signal(SIGTERM, sigterm);

        dup2(this->rc_pty[RCF_SLAVE], STDIN_FILENO);
        dup2(this->rc_pty[RCF_SLAVE], STDOUT_FILENO);

        setenv("TERM", "vt52", 1);

        rl_initialize();
        using_history();
        stifle_history(HISTORY_SIZE);

        for (int lpc = 0; RL_INIT[lpc]; lpc++) {
            snprintf(buffer, sizeof(buffer), "%s", RL_INIT[lpc]);
            rl_parse_and_bind(buffer); /* NOTE: buffer is modified */
        }

        child_this = this;
    }
    else {
        this->rc_command_pipe[RCF_SLAVE].reset();
        this->rc_pty[RCF_SLAVE].reset();
    }
}

readline_curses::~readline_curses()
{
    if (this->rc_child == 0) {
        _exit(0);
    }
    else if (this->rc_child > 0) {
        int status;

        kill(this->rc_child, SIGTERM);
        this->rc_child = -1;

        while (wait(&status) < 0 && (errno == EINTR)) {
            ;
        }
    }
}

void readline_curses::store_matches(
    char **matches, int num_matches, int max_len)
{
    char msg[64];
    int rc;

    max_len = 0;
    for (int lpc = 0; lpc <= num_matches; lpc++) {
        max_len = max(max_len, (int)strlen(matches[lpc]));
    }

    if (last_match_str_valid && strcmp(last_match_str.c_str(), matches[0]) == 0) {
        if (sendstring(child_this->rc_command_pipe[RCF_SLAVE], "n", 1) == -1) {
            _exit(1);
        }
    }
    else {
        rc = snprintf(msg, sizeof(msg), "m:%d:%d", num_matches, max_len);
        if (sendstring(child_this->rc_command_pipe[RCF_SLAVE], msg, rc) == -1) {
            _exit(1);
        }
        for (int lpc = 1; lpc <= num_matches; lpc++) {
            if (sendstring(child_this->rc_command_pipe[RCF_SLAVE],
                           matches[lpc],
                           strlen(matches[lpc])) == -1) {
                _exit(1);
            }
        }

        last_match_str = matches[0];
        last_match_str_valid = true;
    }
}

void readline_curses::start(void)
{
    if (this->rc_child != 0) {
        return;
    }

    map<int, readline_context *>::iterator current_context;
    fd_set rfds;
    int    maxfd;

    require(!this->rc_contexts.empty());

    rl_completer_word_break_characters = (char *)" \t\n"; /* XXX */
    rl_completion_display_matches_hook = store_matches;

    current_context = this->rc_contexts.end();

    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    FD_SET(this->rc_command_pipe[RCF_SLAVE], &rfds);

    maxfd = max(STDIN_FILENO, this->rc_command_pipe[RCF_SLAVE].get());

    while (looping) {
        fd_set ready_rfds = rfds;
        int    rc;

        rc = select(maxfd + 1, &ready_rfds, NULL, NULL, NULL);
        if (rc < 0) {
            switch (errno) {
            case EINTR:
                break;
            }
        }
        else {
            if (FD_ISSET(STDIN_FILENO, &ready_rfds)) {
                struct itimerval itv;

                if (current_context == this->rc_contexts.end()) {
                    looping = false;
                    break;
                }

                itv.it_value.tv_sec     = 0;
                itv.it_value.tv_usec    = KEY_TIMEOUT;
                itv.it_interval.tv_sec  = 0;
                itv.it_interval.tv_usec = 0;
                setitimer(ITIMER_REAL, &itv, NULL);

                rl_callback_read_char();
                if (RL_ISSTATE(RL_STATE_DONE) && !got_line) {
                    got_line = 1;
                    this->line_ready("");
                    rl_callback_handler_remove();
                }
                else {
                    if (sendcmd(this->rc_command_pipe[RCF_SLAVE],
                                'l',
                                rl_line_buffer,
                                rl_end) != 0) {
                        perror("line: write failed");
                        _exit(1);
                    }
                }
            }
            if (FD_ISSET(this->rc_command_pipe[RCF_SLAVE], &ready_rfds)) {
                char msg[1024 + 1];

                if ((rc = recvstring(this->rc_command_pipe[RCF_SLAVE],
                                     msg,
                                     sizeof(msg) - 1)) < 0) {
                }
                else {
                    int  context, prompt_start = 0;
                    char type[32];

                    msg[rc] = '\0';
                    if (sscanf(msg, "f:%d:%n", &context, &prompt_start) == 1 &&
                        prompt_start != 0 &&
                        (current_context = this->rc_contexts.find(context)) !=
                        this->rc_contexts.end()) {
                        current_context->second->load();
                        rl_callback_handler_install(&msg[prompt_start],
                                                    line_ready_tramp);
                        last_match_str_valid = false;
                        if (sendcmd(this->rc_command_pipe[RCF_SLAVE],
                                    'l',
                                    rl_line_buffer,
                                    rl_end) != 0) {
                            perror("line: write failed");
                            _exit(1);
                        }
                    }
                    else if (strcmp(msg, "a") == 0) {
                        char reply[4];

                        rl_done = 1;
                        got_timeout = 0;
                        got_line = 1;
                        rl_callback_handler_remove();

                        snprintf(reply, sizeof(reply), "a");

                        if (sendstring(this->rc_command_pipe[RCF_SLAVE],
                                       reply,
                                       strlen(reply)) == -1) {
                            perror("abort: write failed");
                            _exit(1);
                        }
                    }
                    else if (sscanf(msg,
                                    "ap:%d:%31[^:]:%n",
                                    &context,
                                    type,
                                    &prompt_start) == 2) {
                        require(this->rc_contexts[context] != NULL);

                        this->rc_contexts[context]->
                        add_possibility(string(type),
                                        string(&msg[prompt_start]));
                    }
                    else if (sscanf(msg,
                                    "rp:%d:%31[^:]:%n",
                                    &context,
                                    type,
                                    &prompt_start) == 2) {
                        require(this->rc_contexts[context] != NULL);

                        this->rc_contexts[context]->
                        rem_possibility(string(type),
                                        string(&msg[prompt_start]));
                    }
                    else if (sscanf(msg, "cp:%d:%s", &context, type)) {
                        this->rc_contexts[context]->clear_possibilities(type);
                    }
                    else {
                        log_error("unhandled message: %s", msg);
                    }
                }
            }
        }

        if (got_timeout) {
            got_timeout = 0;
            if (sendcmd(this->rc_command_pipe[RCF_SLAVE],
                        't',
                        rl_line_buffer,
                        rl_end) == -1) {
                _exit(1);
            }
        }
        if (got_line) {
            struct itimerval itv;

            got_line                = 0;
            itv.it_value.tv_sec     = 0;
            itv.it_value.tv_usec    = 0;
            itv.it_interval.tv_sec  = 0;
            itv.it_interval.tv_usec = 0;
            if (setitimer(ITIMER_REAL, &itv, NULL) < 0) {
                log_error("setitimer: %s", strerror(errno));
            }
            current_context->second->save();
            current_context = this->rc_contexts.end();
        }
        if (got_winch) {
            struct winsize ws;

            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
                throw error(errno);
            }
            got_winch = 0;
            rl_set_screen_size(ws.ws_row, ws.ws_col);
        }
    }

    std::map<int, readline_context *>::iterator citer;
    for (citer = this->rc_contexts.begin();
         citer != this->rc_contexts.end();
         ++citer) {
        char *home;

        citer->second->load();
        home = getenv("HOME");
        if (home) {
            char hpath[2048];

            snprintf(hpath, sizeof(hpath),
                     "%s/.lnav/%s.history",
                     home, citer->second->get_name().c_str());
            write_history(hpath);
        }
        citer->second->save();
    }

    _exit(0);
}

void readline_curses::line_ready(const char *line)
{
    auto_mem<char> expanded;
    char           msg[1024] = {0};
    int            rc;

    if (rl_line_buffer[0] == '^') {
        rc = -1;
    }
    else {
        rc = history_expand(rl_line_buffer, expanded.out());
    }
    switch (rc) {
#if 0
        /* TODO: fix clash between history and pcre metacharacters */
        case -1:
            /* XXX */
            snprintf(msg, sizeof(msg),
                     "e:unable to expand history -- %s",
                     expanded.in());
            break;
#endif

    case -1:
        snprintf(msg, sizeof(msg), "d:%s", line);
        break;

    case 0:
    case 1:
    case 2: /* XXX */
        snprintf(msg, sizeof(msg), "d:%s", expanded.in());
        break;
    }

    if (sendstring(this->rc_command_pipe[RCF_SLAVE],
                   msg,
                   strlen(msg)) == -1) {
        perror("line_ready: write failed");
        _exit(1);
    }

    {
        HIST_ENTRY *entry;

        if (line[0] != '\0' && (
            history_length == 0 ||
            (entry = history_get(history_base + history_length - 1)) == NULL ||
            strcmp(entry->line, line) != 0)) {
            add_history(line);
        }
    }
}

void readline_curses::check_poll_set(const vector<struct pollfd> &pollfds)
{
    int rc;

    if (pollfd_ready(pollfds, this->rc_pty[RCF_MASTER])) {
        char buffer[128];

        rc = read(this->rc_pty[RCF_MASTER], buffer, sizeof(buffer));
        if (rc > 0) {
            this->map_output(buffer, rc);
        }
    }
    if (pollfd_ready(pollfds, this->rc_command_pipe[RCF_MASTER])) {
        char msg[1024 + 1];

        rc = recvstring(this->rc_command_pipe[RCF_MASTER], msg, sizeof(msg) - 1);
        if (rc >= 0) {
            string old_value = this->rc_value;

            msg[rc] = '\0';
            if (this->rc_matches_remaining > 0) {
                this->rc_matches.push_back(msg);
                this->rc_matches_remaining -= 1;
                if (this->rc_matches_remaining == 0) {
                    this->rc_display_match.invoke(this);
                }
            }
            else if (msg[0] == 'm') {
                if (sscanf(msg, "m:%d:%d", &this->rc_matches_remaining,
                           &this->rc_max_match_length) != 2) {
                    require(0);
                }
                this->rc_matches.clear();
                if (this->rc_matches_remaining == 0) {
                    this->rc_display_match.invoke(this);
                }
            }
            else {
                switch (msg[0]) {
                case 't':
                case 'd':
                    this->rc_value = string(&msg[2]);
                    break;
                }
                switch (msg[0]) {
                case 'a':
                    require(rc == 1);

                    this->rc_active_context = -1;
                    this->vc_past_lines.clear();
                    this->rc_matches.clear();
                    this->rc_abort.invoke(this);
                    this->rc_display_match.invoke(this);
                    this->rc_blur.invoke(this);
                    curs_set(0);
                    break;

                case 't':
                    if (this->rc_value != old_value) {
                        this->rc_timeout.invoke(this);
                    }
                    break;

                case 'd':
                    this->rc_active_context = -1;
                    this->vc_past_lines.clear();
                    this->rc_matches.clear();
                    this->rc_perform.invoke(this);
                    this->rc_display_match.invoke(this);
                    this->rc_blur.invoke(this);
                    curs_set(0);
                    break;

                case 'l':
                    this->rc_line_buffer = &msg[2];
                    this->rc_change.invoke(this);
                    break;

                case 'n':
                    this->rc_display_next.invoke(this);
                    break;
                }
            }
        }
    }
}

void readline_curses::handle_key(int ch)
{
    const char *bch;
    int         len;

    bch = this->map_input(ch, len);
    if (write(this->rc_pty[RCF_MASTER], bch, len) == -1) {
        perror("handle_key: write failed");
    }
    if (ch == '\t' || ch == '\r') {
        this->vc_past_lines.clear();
    }
}

void readline_curses::focus(int context, const char *prompt)
{
    char buffer[1024];

    curs_set(1);

    this->rc_active_context = context;

    snprintf(buffer, sizeof(buffer), "f:%d:%s", context, prompt);
    if (sendstring(this->rc_command_pipe[RCF_MASTER],
                   buffer,
                   strlen(buffer) + 1) == -1) {
        perror("focus: write failed");
    }
    wmove(this->vc_window, this->get_actual_y(), 0);
    wclrtoeol(this->vc_window);
}

void readline_curses::abort()
{
    char buffer[1024];

    snprintf(buffer, sizeof(buffer), "a");
    if (sendstring(this->rc_command_pipe[RCF_MASTER],
                   buffer,
                   strlen(buffer)) == -1) {
        perror("abort: write failed");
    }
}

void readline_curses::add_possibility(int context,
                                      const string &type,
                                      const string &value)
{
    char buffer[1024];

    if (value.empty()) {
        return;
    }

    snprintf(buffer, sizeof(buffer),
             "ap:%d:%s:%s",
             context, type.c_str(), value.c_str());
    if (sendstring(this->rc_command_pipe[RCF_MASTER],
                   buffer,
                   strlen(buffer) + 1) == -1) {
        perror("add_possibility: write failed");
    }
}

void readline_curses::rem_possibility(int context,
                                      const string &type,
                                      const string &value)
{
    char buffer[1024];

    snprintf(buffer, sizeof(buffer),
             "rp:%d:%s:%s",
             context, type.c_str(), value.c_str());
    if (sendstring(this->rc_command_pipe[RCF_MASTER],
                   buffer,
                   strlen(buffer) + 1) == -1) {
        perror("rem_possiblity: write failed");
    }
}

void readline_curses::clear_possibilities(int context, string type)
{
    char buffer[1024];

    snprintf(buffer, sizeof(buffer),
             "cp:%d:%s",
             context, type.c_str());
    if (sendstring(this->rc_command_pipe[RCF_MASTER],
                   buffer,
                   strlen(buffer) + 1) == -1) {
        perror("clear_possiblity: write failed");
    }
}

void readline_curses::do_update(void)
{
    if (this->rc_active_context == -1) {
        int alt_start        = -1;
        struct line_range lr(0, 0);
        attr_line_t       al, alt_al;

        wmove(this->vc_window, this->get_actual_y(), 0);
        wclrtoeol(this->vc_window);

        if (time(NULL) > this->rc_value_expiration) {
            this->rc_value.clear();
        }

        al.get_string() = this->rc_value;
        scrub_ansi_string(al.get_string(), al.get_attrs());

        if (!this->rc_alt_value.empty()) {
            alt_al.get_string() = this->rc_alt_value;
            scrub_ansi_string(alt_al.get_string(), alt_al.get_attrs());

            alt_start = getmaxx(this->vc_window) - alt_al.get_string().size();
        }

        if (alt_start >= (int)(al.get_string().length() + 5)) {
            lr.lr_end = alt_al.get_string().length();
            view_curses::mvwattrline(this->vc_window,
                                     this->get_actual_y(),
                                     alt_start,
                                     alt_al,
                                     lr);
        }

        lr.lr_end = al.get_string().length();
        view_curses::mvwattrline(this->vc_window,
                                 this->get_actual_y(),
                                 0,
                                 al,
                                 lr);
        this->set_x(0);
    }
    vt52_curses::do_update();

    if (this->rc_active_context != -1) {
        readline_context *rc = this->rc_contexts[this->rc_active_context];
        readline_highlighter_t hl = rc->get_highlighter();

        if (hl != NULL) {
            char line[1024];
            attr_line_t al;
            string &str = al.get_string();
            int rc;

            rc = mvwinnstr(this->vc_window,
                           this->get_actual_y(), 0,
                           line, sizeof(line));

            str = string(line, rc);

            hl(al, this->vc_x);
            view_curses::mvwattrline(this->vc_window,
                this->get_actual_y(), 0, al, line_range(0, rc));

            wmove(this->vc_window, this->get_actual_y(), this->vc_x);
        }
    }
}
