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
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
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
#include <utility>

#include "base/paths.hh"
#include "base/string_util.hh"
#include "fmt/format.h"
#include "shlex.hh"
#include "auto_mem.hh"
#include "base/lnav_log.hh"
#include "lnav_util.hh"
#include "ansi_scrubber.hh"
#include "readline_curses.hh"
#include "spookyhash/SpookyV2.h"
#include "fts_fuzzy_match.hh"

using namespace std;

static int              got_line    = 0;
static bool             alt_done    = 0;
static sig_atomic_t     got_timeout = 0;
static sig_atomic_t     got_winch   = 0;
static readline_curses *child_this;
static sig_atomic_t     looping      = 1;
static const int        HISTORY_SIZE = 256;
static int              completion_start;
static const int        FUZZY_PEER_THRESHOLD = 30;

static const char *RL_INIT[] = {
    /*
     * XXX Need to keep the input on a single line since the display screws
     * up if it wraps around.
     */
    "set horizontal-scroll-mode on",
    "set bell-style none",
    "set show-all-if-ambiguous on",
    "set show-all-if-unmodified on",
    "set menu-complete-display-prefix on",
    "TAB: menu-complete",
    "\"\\e[Z\": menu-complete-backward",
};

readline_context *readline_context::loaded_context;
set<string> *     readline_context::arg_possibilities;
static string last_match_str;
static bool last_match_str_valid;
static bool arg_needs_shlex;
static nonstd::optional<std::string> rewrite_line_start;

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
        ssize_t rc = recv(sock, &buf[offset], len, 0);

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

char *readline_context::completion_generator(const char *text_in, int state)
{
    static vector<string> matches;

    vector<string> long_matches;
    char *retval = nullptr;

    if (state == 0) {
        auto text_str = std::string(text_in);

        if (arg_needs_shlex) {
            shlex arg_lexer(text_str);
            map<string, string> scope;
            std::string result;

            if (arg_lexer.eval(result, scope)) {
                text_str = result;
            }
        }

        matches.clear();
        if (arg_possibilities != nullptr) {
            for (const auto &poss : (*arg_possibilities)) {
                auto cmpfunc = (loaded_context->is_case_sensitive() ?
                                strncmp : strncasecmp);
                auto poss_str = poss.c_str();

                // Check for an exact match and for the quoted version.
                if (cmpfunc(text_str.c_str(), poss_str, text_str.length()) == 0 ||
                    ((strchr(loaded_context->rc_quote_chars, poss_str[0]) !=
                        nullptr) &&
                     cmpfunc(text_str.c_str(), &poss_str[1], text_str.length()) == 0)) {
                    auto poss_slash_count = std::count(poss.begin(), poss.end(), '/');

                    if (endswith(poss, "/")) {
                        poss_slash_count -= 1;
                    }
                    if (std::count(text_str.begin(), text_str.end(), '/') == poss_slash_count) {
                        matches.emplace_back(poss);
                    } else {
                        long_matches.emplace_back(poss);
                    }
                }
            }

            if (matches.empty()) {
                matches = std::move(long_matches);
            }

            if (matches.empty()) {
                vector<pair<int, string>> fuzzy_matches;
                vector<pair<int, string>> fuzzy_long_matches;

                for (const auto &poss : (*arg_possibilities)) {
                    string poss_str = tolower(poss);
                    int score;

                    if (fts::fuzzy_match(text_str.c_str(), poss_str.c_str(), score) && score > 0) {
                        if (score <= 0) {
                            continue;
                        }

                        auto poss_slash_count = std::count(poss_str.begin(), poss_str.end(), '/');
                        if (endswith(poss, "/")) {
                            poss_slash_count -= 1;
                        }
                        if (std::count(text_str.begin(), text_str.end(), '/') == poss_slash_count) {
                            fuzzy_matches.emplace_back(score, poss);
                        } else {
                            fuzzy_long_matches.emplace_back(score, poss);
                        }
                    }
                }

                if (fuzzy_matches.empty()) {
                    fuzzy_matches = std::move(fuzzy_long_matches);
                }

                if (!fuzzy_matches.empty()) {
                    stable_sort(begin(fuzzy_matches), end(fuzzy_matches),
                        [](auto l, auto r) { return r.first < l.first; });

                    int highest = fuzzy_matches[0].first;

                    for (const auto &pair : fuzzy_matches) {
                        if (highest - pair.first < FUZZY_PEER_THRESHOLD) {
                            matches.push_back(pair.second);
                        } else {
                            break;
                        }
                    }
                }
            }
        }

        if (matches.size() == 1) {
            if (text_str == matches[0]) {
                matches.pop_back();
            }

            last_match_str_valid = false;
            if (sendstring(child_this->rc_command_pipe[readline_curses::RCF_SLAVE],
                           "m:0:0:0",
                           7) == -1) {
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
    char **retval = nullptr;

    completion_start = start;
    if (start == 0 && loaded_context->rc_possibilities.find("__command") !=
            loaded_context->rc_possibilities.end()) {
        arg_possibilities = &loaded_context->rc_possibilities["__command"];
        arg_needs_shlex = false;
        rl_completion_append_character = loaded_context->rc_append_character;
    }
    else {
        char * space;
        string cmd;
        vector<string> prefix;
        int point = rl_point;
        while (point > 0 && rl_line_buffer[point] != ' ') {
            point -= 1;
        }
        shlex lexer(rl_line_buffer, point);
        map<string, string> scope;

        arg_possibilities = nullptr;
        rl_completion_append_character = 0;
        if (lexer.split(prefix, scope)) {
            auto prefix2 = fmt::format("{}", fmt::join(prefix, "\x1f"));
            auto prefix_iter = loaded_context->rc_prefixes.find(prefix2);

            if (prefix_iter != loaded_context->rc_prefixes.end()) {
                arg_possibilities = &(loaded_context->rc_possibilities[prefix_iter->second]);
                arg_needs_shlex = false;
            }
        }

        if (arg_possibilities == nullptr) {
            space = strchr(rl_line_buffer, ' ');
            if (space == nullptr) {
                space = rl_line_buffer + strlen(rl_line_buffer);
            }
            cmd = string(rl_line_buffer, space - rl_line_buffer);

            auto iter = loaded_context->rc_prototypes.find(cmd);

            if (iter == loaded_context->rc_prototypes.end()) {
                if (loaded_context->rc_possibilities.find("*") !=
                    loaded_context->rc_possibilities.end()) {
                    arg_possibilities = &loaded_context->rc_possibilities["*"];
                    arg_needs_shlex = false;
                    rl_completion_append_character = loaded_context->rc_append_character;
                }
            } else {
                vector<string> &proto = loaded_context->rc_prototypes[cmd];

                if (proto.empty()) {
                    arg_possibilities = nullptr;
                } else if (proto[0] == "filename") {
                    shlex fn_lexer(rl_line_buffer, rl_point);
                    std::vector<std::string> fn_list;
                    int found = 0;

                    fn_lexer.split(fn_list, scope);

                    const auto& last_fn =
                        fn_list.size() <= 1 ? "" : fn_list.back();

                    if (last_fn.find(':') != string::npos) {
                        auto rp_iter = loaded_context->rc_possibilities.find("remote-path");
                        if (rp_iter != loaded_context->rc_possibilities.end()) {
                            for (const auto& poss : rp_iter->second) {
                                if (startswith(poss, last_fn.c_str())) {
                                    found += 1;
                                }
                            }
                            if (found) {
                                arg_possibilities = &rp_iter->second;
                                arg_needs_shlex = false;
                            }
                        }
                        if (!found ||
                            (endswith(last_fn, "/") && found == 1)) {
                            char msg[2048];

                            snprintf(msg, sizeof(msg),
                                     "\t:%s",
                                     last_fn.c_str());
                            sendstring(child_this->rc_command_pipe[1],
                                       msg, strlen(msg));
                        }
                    }
                    if (!found) {
                        static std::set<std::string> file_name_set;

                        file_name_set.clear();
                        auto_mem<char> completed_fn;
                        int fn_state = 0;
                        auto recent_netlocs_iter = loaded_context->
                            rc_possibilities.find("recent-netlocs");

                        if (recent_netlocs_iter !=
                            loaded_context->rc_possibilities.end()) {
                            file_name_set.insert(
                                recent_netlocs_iter->second.begin(),
                                recent_netlocs_iter->second.end());
                        }
                        while ((completed_fn = rl_filename_completion_function(
                            last_fn.c_str(), fn_state)) != nullptr) {
                            file_name_set.insert(completed_fn.in());
                            fn_state += 1;
                        }
                        arg_possibilities = &file_name_set;
                        arg_needs_shlex = true;
                    }
                } else {
                    arg_possibilities = &(loaded_context->rc_possibilities[proto[0]]);
                    arg_needs_shlex = false;
                }
            }
        }
    }

    retval = rl_completion_matches(text, completion_generator);
    if (retval == nullptr) {
        rl_attempted_completion_over = 1;
    }

    return retval;
}

static int rubout_char_or_abort(int count, int key)
{
    if (rl_line_buffer[0] == '\0') {
        rl_done = true;
        return 0;
    } else {
        return rl_rubout(count, '\b');
    }
}

static int alt_done_func(int count, int key)
{
    alt_done = true;
    rl_newline(count, key);
    return 0;
}

int readline_context::command_complete(int count, int key)
{
    if (loaded_context->rc_possibilities.find("__command") !=
        loaded_context->rc_possibilities.end()) {
        char *space = strchr(rl_line_buffer, ' ');

        if (space == nullptr) {
            return rl_menu_complete(count, key);
        }
    }
    return rl_insert(count, key);
}

readline_context::readline_context(std::string name,
                                   readline_context::command_map_t *commands,
                                   bool case_sensitive)
    : rc_name(std::move(name)),
      rc_case_sensitive(case_sensitive),
      rc_quote_chars("\"'"),
      rc_highlighter(nullptr)
{
    if (commands != nullptr) {
        command_map_t::iterator iter;

        for (iter = commands->begin(); iter != commands->end(); ++iter) {
            std::string cmd = iter->first;

            this->rc_possibilities["__command"].insert(cmd);
            iter->second->c_func(INIT_EXEC_CONTEXT, cmd, this->rc_prototypes[cmd]);
        }
    }

    memset(&this->rc_history, 0, sizeof(this->rc_history));
    history_set_history_state(&this->rc_history);

    auto config_dir = lnav::paths::dotlnav();
    auto hpath = (config_dir / this->rc_name).string() + ".history";
    read_history(hpath.c_str());
    this->save();

    this->rc_append_character = ' ';
}

void readline_context::load()
{
    char buffer[128];

    rl_completer_word_break_characters = (char *)" \t\n|()"; /* XXX */
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
    for (auto &rc_var : this->rc_vars) {
        *(rc_var.rv_dst.ch) = (char *) rc_var.rv_val.ch;
    }
}

void readline_context::save()
{
    HISTORY_STATE *hs = history_get_history_state();

    this->rc_history = *hs;
    free(hs);
    hs = nullptr;
}

readline_curses::readline_curses()
    : rc_focus(noop_func{}),
      rc_change(noop_func{}),
      rc_perform(noop_func{}),
      rc_alt_perform(noop_func{}),
      rc_timeout(noop_func{}),
      rc_abort(noop_func{}),
      rc_display_match(noop_func{}),
      rc_display_next(noop_func{}),
      rc_blur(noop_func{}),
      rc_completion_request(noop_func{})
{
}

readline_curses::~readline_curses()
{
    this->rc_pty[RCF_MASTER].reset();
    this->rc_command_pipe[RCF_MASTER].reset();
    if (this->rc_child == 0) {
        _exit(0);
    }
    else if (this->rc_child > 0) {
        int status;

        log_debug("term child %d", this->rc_child);
        log_perror(kill(this->rc_child, SIGTERM));
        this->rc_child = -1;

        while (wait(&status) < 0 && (errno == EINTR)) {
            ;
        }
    }
}

void readline_curses::store_matches(
    char **matches, int num_matches, int max_len)
{
    static int match_index = 0;
    char msg[64];
    int rc;

    max_len = 0;
    for (int lpc = 0; lpc <= num_matches; lpc++) {
        max_len = max(max_len, (int)strlen(matches[lpc]));
    }

    if (last_match_str_valid && strcmp(last_match_str.c_str(), matches[0]) == 0) {
        match_index += 1;
        rc = snprintf(msg, sizeof(msg), "n:%d", match_index);

        if (sendstring(child_this->rc_command_pipe[RCF_SLAVE], msg, rc) == -1) {
            _exit(1);
        }
    }
    else {
        match_index = 0;
        rc = snprintf(msg, sizeof(msg),
                      "m:%d:%d:%d",
                      completion_start, num_matches, max_len);
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

void readline_curses::start()
{
    if (this->rc_child > 0) {
        return;
    }

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
                nullptr,
                nullptr,
                &ws) < 0) {
        perror("error: failed to open terminal(openpty)");
        throw error(errno);
    }

    if ((this->rc_child = fork()) == -1) {
        throw error(errno);
    }

    if (this->rc_child != 0) {
        this->rc_command_pipe[RCF_SLAVE].reset();
        this->rc_pty[RCF_SLAVE].reset();
        return;
    }

    {
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

        rl_add_defun("rubout-char-or-abort", rubout_char_or_abort, '\b');
        rl_add_defun("alt-done", alt_done_func, '\x0a');
        // rl_add_defun("command-complete", readline_context::command_complete, ' ');

        for (const auto* init_cmd : RL_INIT) {
            snprintf(buffer, sizeof(buffer), "%s", init_cmd);
            rl_parse_and_bind(buffer); /* NOTE: buffer is modified */
        }

        child_this = this;
    }

    map<int, readline_context *>::iterator current_context;
    int    maxfd;

    require(!this->rc_contexts.empty());

    rl_completion_display_matches_hook = store_matches;

    current_context = this->rc_contexts.end();

    maxfd = max(STDIN_FILENO, this->rc_command_pipe[RCF_SLAVE].get());

    while (looping) {
        fd_set ready_rfds;
        int    rc;

        FD_ZERO(&ready_rfds);
        if (current_context != this->rc_contexts.end()) {
            FD_SET(STDIN_FILENO, &ready_rfds);
        }
        FD_SET(this->rc_command_pipe[RCF_SLAVE], &ready_rfds);

        rc = select(maxfd + 1, &ready_rfds, nullptr, nullptr, nullptr);
        if (rc < 0) {
            switch (errno) {
            case EINTR:
                break;
            }
        }
        else {
            if (FD_ISSET(STDIN_FILENO, &ready_rfds)) {
                static uint64_t last_h1, last_h2;

                struct itimerval itv;

                itv.it_value.tv_sec     = 0;
                itv.it_value.tv_usec    = KEY_TIMEOUT;
                itv.it_interval.tv_sec  = 0;
                itv.it_interval.tv_usec = 0;
                setitimer(ITIMER_REAL, &itv, nullptr);

                rl_callback_read_char();
                if (RL_ISSTATE(RL_STATE_DONE) && !got_line) {
                    got_line = 1;
                    this->line_ready("");
                    rl_callback_handler_remove();
                }
                else {
                    uint64_t h1 = 1, h2 = 2;

                    if (rl_last_func == readline_context::command_complete) {
                        rl_last_func = rl_menu_complete;
                    }

                    bool complete_done = (
                        rl_last_func != rl_menu_complete &&
                        rl_last_func != rl_backward_menu_complete);

                    if (complete_done) {
                        last_match_str_valid = false;
                    } else if (rewrite_line_start &&
                               !startswith(rl_line_buffer,
                                           rewrite_line_start->c_str())) {
                        // If the line was rewritten, the extra text stays on
                        // the screen, so we need to delete it, make sure the
                        // append character is there, and redisplay.  For
                        // example, ':co<TAB>' will complete ':comment' and
                        // append the current comment.  Pressing '<TAB>' again
                        // would switch to ':config' and the comment text would
                        // be left on the display.
                        rl_delete_text(rl_point, rl_end);
                        if (rl_completion_append_character &&
                            rl_line_buffer[rl_point] !=
                            rl_completion_append_character) {
                            char buf[2] = {
                                (char) rl_completion_append_character, '\0'
                            };

                            rl_insert_text(buf);
                        }
                        rl_redisplay();
                    }
                    rewrite_line_start = nonstd::nullopt;

                    SpookyHash::Hash128(rl_line_buffer, rl_end, &h1, &h2);

                    if (h1 == last_h1 && h2 == last_h2) {
                        // do nothing
                    } else if (sendcmd(this->rc_command_pipe[RCF_SLAVE],
                                       complete_done ? 'l': 'c',
                                       rl_line_buffer,
                                       rl_end) != 0) {
                        perror("line: write failed");
                        _exit(1);
                    }
                    last_h1 = h1;
                    last_h2 = h2;
                    if (sendcmd(this->rc_command_pipe[RCF_SLAVE],
                                'w',
                                "",
                                0) != 0) {
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
                    looping = false;
                }
                else {
                    int  context, prompt_start = 0;
                    char type[1024];

                    msg[rc] = '\0';
                    if (sscanf(msg, "i:%d:%n", &rl_point, &prompt_start) == 1) {
                        const char *initial = &msg[prompt_start];

                        rl_extend_line_buffer(strlen(initial) + 1);
                        strcpy(rl_line_buffer, initial);
                        rl_end = strlen(initial);
                        rewrite_line_start = std::string(rl_line_buffer, rl_point);
                        rl_redisplay();
                        if (sendcmd(this->rc_command_pipe[RCF_SLAVE],
                                    'c',
                                    rl_line_buffer,
                                    rl_end) != 0) {
                            perror("line: write failed");
                            _exit(1);
                        }
                    }
                    else if (sscanf(msg, "f:%d:%n", &context, &prompt_start) == 1 &&
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
                        if (sendcmd(this->rc_command_pipe[RCF_SLAVE],
                                    'w',
                                    "",
                                    0) != 0) {
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
                                    "apre:%d:%1023[^\x1d]\x1d%n",
                                    &context,
                                    type,
                                    &prompt_start) == 2) {
                        require(this->rc_contexts[context] != nullptr);

                        this->rc_contexts[context]->rc_prefixes[string(type)] =
                            string(&msg[prompt_start]);
                    }
                    else if (sscanf(msg,
                                    "ap:%d:%31[^:]:%n",
                                    &context,
                                    type,
                                    &prompt_start) == 2) {
                        require(this->rc_contexts[context] != nullptr);

                        this->rc_contexts[context]->
                                                      add_possibility(string(type),
                                                                      string(&msg[prompt_start]));
                        if (rl_last_func == rl_complete ||
                            rl_last_func == rl_menu_complete) {
                            rl_last_func = NULL;
                        }
                    }
                    else if (sscanf(msg,
                                    "rp:%d:%31[^:]:%n",
                                    &context,
                                    type,
                                    &prompt_start) == 2) {
                        require(this->rc_contexts[context] != nullptr);

                        this->rc_contexts[context]->
                                                      rem_possibility(string(type),
                                                                      string(&msg[prompt_start]));
                    }
                    else if (sscanf(msg, "cpre:%d", &context) == 1) {
                        this->rc_contexts[context]->rc_prefixes.clear();
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
            if (setitimer(ITIMER_REAL, &itv, nullptr) < 0) {
                log_error("setitimer: %s", strerror(errno));
            }
            if (current_context != this->rc_contexts.end()) {
                current_context->second->save();
                current_context = this->rc_contexts.end();
            }
        }
        if (got_winch) {
            struct winsize new_ws;

            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &new_ws) == -1) {
                throw error(errno);
            }
            got_winch = 0;
            rl_set_screen_size(new_ws.ws_row, new_ws.ws_col);
        }
    }

    auto config_dir = lnav::paths::dotlnav();
    for (auto& pair : this->rc_contexts) {
        pair.second->load();

        auto hpath = (config_dir / pair.second->get_name()).string() + ".history";
        write_history(hpath.c_str());
        pair.second->save();
    }

    _exit(0);
}

void readline_curses::line_ready(const char *line)
{
    auto_mem<char> expanded;
    char           msg[1024] = {0};
    int            rc;
    const char *cmd_ch = alt_done ? "D" : "d";

    alt_done = false;
    if (line == nullptr) {
        snprintf(msg, sizeof(msg), "a");

        if (sendstring(this->rc_command_pipe[RCF_SLAVE],
                       msg,
                       strlen(msg)) == -1) {
            perror("abort: write failed");
            _exit(1);
        }
        return;
    }

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
        snprintf(msg, sizeof(msg), "%s:%s", cmd_ch, line);
        break;

    case 0:
    case 1:
    case 2: /* XXX */
        snprintf(msg, sizeof(msg), "%s:%s", cmd_ch, expanded.in());
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
            (entry = history_get(history_base + history_length - 1)) == nullptr ||
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
            int old_x = this->vc_x;

            this->map_output(buffer, rc);
            if (this->vc_x != old_x) {
                this->rc_change(this);
            }
        }
    }
    if (pollfd_ready(pollfds, this->rc_command_pipe[RCF_MASTER])) {
        char msg[1024 + 1];

        rc = recvstring(this->rc_command_pipe[RCF_MASTER], msg, sizeof(msg) - 1);
        if (rc >= 0) {
            string old_value = this->rc_value;

            msg[rc] = '\0';
            if (this->rc_matches_remaining > 0) {
                this->rc_matches.emplace_back(msg);
                this->rc_matches_remaining -= 1;
                if (this->rc_matches_remaining == 0) {
                    this->rc_display_match(this);
                }
            }
            else if (msg[0] == 'm') {
                if (sscanf(msg, "m:%d:%d:%d",
                           &this->rc_match_start,
                           &this->rc_matches_remaining,
                           &this->rc_max_match_length) != 3) {
                    require(0);
                }
                this->rc_matches.clear();
                if (this->rc_matches_remaining == 0) {
                    this->rc_display_match(this);
                }
                this->rc_match_index = 0;
            }
            else if (msg[0] == '\t') {
                char path[2048];

                if (sscanf(msg, "\t:%s", path) != 1) {
                    require(0);
                }
                this->rc_remote_complete_path = path;
                this->rc_completion_request(this);
            }
            else if (msg[0] == 'n') {
                if (sscanf(msg, "n:%d", &this->rc_match_index) != 1) {
                    require(0);
                }
                this->rc_display_next(this);
            }
            else {
                switch (msg[0]) {
                case 't':
                case 'd':
                case 'D':
                    this->rc_value = string(&msg[2]);
                    break;
                }
                switch (msg[0]) {
                case 'a':
                    curs_set(0);
                    this->vc_line.clear();
                    this->rc_active_context = -1;
                    this->rc_matches.clear();
                    this->rc_abort(this);
                    this->rc_display_match(this);
                    this->rc_blur(this);
                    break;

                case 't':
                    this->rc_timeout(this);
                    break;

                case 'd':
                case 'D':
                    curs_set(0);
                    this->rc_active_context = -1;
                    this->rc_matches.clear();
                    if (msg[0] == 'D' || this->rc_is_alt_focus) {
                        this->rc_alt_perform(this);
                    } else {
                        this->rc_perform(this);
                    }
                    this->rc_display_match(this);
                    this->rc_blur(this);
                    break;

                case 'l':
                    this->rc_line_buffer = &msg[2];
                    if (this->rc_active_context != -1) {
                        this->rc_change(this);
                    }
                    this->rc_matches.clear();
                    if (this->rc_active_context != -1) {
                        this->rc_display_match(this);
                    }
                    break;

                case 'c':
                    this->rc_line_buffer = &msg[2];
                    this->rc_change(this);
                    this->rc_display_match(this);
                    break;
                case 'w':
                    this->rc_ready_for_input = true;
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
}

void readline_curses::focus(int context, const std::string& prompt, const std::string& initial)
{
    char buffer[1024];

    curs_set(1);

    this->rc_active_context = context;

    snprintf(buffer, sizeof(buffer), "f:%d:%s", context, prompt.c_str());
    if (sendstring(this->rc_command_pipe[RCF_MASTER],
                   buffer,
                   strlen(buffer) + 1) == -1) {
        perror("focus: write failed");
    }
    wmove(this->vc_window, this->get_actual_y(), this->vc_left);
    wclrtoeol(this->vc_window);
    if (!initial.empty()) {
        this->rewrite_line(initial.size(), initial);
    }
    this->rc_is_alt_focus = false;
    this->rc_focus(this);
}

void readline_curses::rewrite_line(int pos, const std::string& value)
{
    char buffer[1024];

    snprintf(buffer, sizeof(buffer), "i:%d:%s", pos, value.c_str());
    if (sendstring(this->rc_command_pipe[RCF_MASTER],
                   buffer,
                   strlen(buffer) + 1) == -1) {
        perror("rewrite_line: write failed");
    }
}

void readline_curses::abort()
{
    char buffer[1024];

    this->vc_x = 0;
    snprintf(buffer, sizeof(buffer), "a");
    if (sendstring(this->rc_command_pipe[RCF_MASTER],
                   buffer,
                   strlen(buffer)) == -1) {
        perror("abort: write failed");
    }
}

void readline_curses::add_prefix(int context,
                                 const vector<string> &prefix,
                                 const string &value)
{
    char buffer[1024];
    auto prefix_wire = fmt::format("{}", fmt::join(prefix, "\x1f"));

    snprintf(buffer, sizeof(buffer),
             "apre:%d:%s\x1d%s",
             context,
             prefix_wire.c_str(),
             value.c_str());
    if (sendstring(this->rc_command_pipe[RCF_MASTER],
                   buffer,
                   strlen(buffer) + 1) == -1) {
        perror("add_prefix: write failed");
    }
}

void readline_curses::clear_prefixes(int context)
{
    char buffer[1024];

    snprintf(buffer, sizeof(buffer), "cpre:%d", context);
    if (sendstring(this->rc_command_pipe[RCF_MASTER],
                   buffer,
                   strlen(buffer) + 1) == -1) {
        perror("add_possibility: write failed");
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

void readline_curses::do_update()
{
    if (!this->vc_visible) {
        return;
    }

    if (this->rc_active_context == -1) {
        int alt_start        = -1;
        struct line_range lr(0, 0);
        attr_line_t       al, alt_al;
        view_colors &vc = view_colors::singleton();

        wmove(this->vc_window, this->get_actual_y(), this->vc_left);
        wattron(this->vc_window, vc.attrs_for_role(view_colors::VCR_TEXT));
        whline(this->vc_window, ' ', this->vc_width);

        if (time(nullptr) > this->rc_value_expiration) {
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
                                 this->vc_left,
                                 al,
                                 lr);
        this->set_x(0);
    }

    if (this->rc_active_context != -1) {
        readline_context *rc = this->rc_contexts[this->rc_active_context];
        readline_highlighter_t hl = rc->get_highlighter();
        attr_line_t al = this->vc_line;

        if (hl != nullptr) {
            hl(al, this->vc_left + this->vc_x);
        }
        view_curses::mvwattrline(this->vc_window,
                                 this->get_actual_y(), this->vc_left,
                                 al,
                                 line_range{ 0, (int) this->vc_width });

        wmove(this->vc_window, this->get_actual_y(), this->vc_left + this->vc_x);
    }
}

std::string readline_curses::get_match_string() const
{
    auto len = ::min((size_t) this->vc_x, this->rc_line_buffer.size()) - this->rc_match_start;
    auto context = this->get_active_context();

    if (context->get_append_character() != 0) {
        if (this->rc_line_buffer.length() > (this->rc_match_start + len - 1) &&
            this->rc_line_buffer[this->rc_match_start + len - 1] ==
            context->get_append_character()) {
            len -= 1;
        } else if (this->rc_line_buffer.length() >
                   (this->rc_match_start + len - 2) &&
                   this->rc_line_buffer[this->rc_match_start + len - 2] ==
                   context->get_append_character()) {
            len -= 2;
        }
    }

    return this->rc_line_buffer.substr(this->rc_match_start, len);
}
