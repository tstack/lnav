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
 * @file grep_proc.cc
 */

#include "config.h"

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "lnav_log.hh"
#include "lnav_util.hh"
#include "grep_proc.hh"

#include "time_T.hh"

using namespace std;

grep_proc::grep_proc(pcre *code, grep_proc_source &gps)
    : gp_pcre(code),
      gp_code(code),
      gp_source(gps),
      gp_pipe_offset(0),
      gp_child(-1),
      gp_child_started(false),
      gp_last_line(0),
      gp_sink(NULL),
      gp_control(NULL)
{
    require(this->invariant());
}

grep_proc::~grep_proc()
{
    this->gp_queue.clear();
    this->cleanup();
}

void grep_proc::handle_match(int line,
                             string &line_value,
                             int off,
                             int *matches,
                             int count)
{
    int lpc;

    if (off == 0) {
        fprintf(stdout, "%d\n", line);
    }
    fprintf(stdout, "[%d:%d]\n", matches[0], matches[1]);
    for (lpc = 1; lpc < count; lpc++) {
        fprintf(stdout,
                "(%d:%d)",
                matches[lpc * 2],
                matches[lpc * 2 + 1]);
        fwrite(&(line_value.c_str()[matches[lpc * 2]]),
               1,
               matches[lpc * 2 + 1] -
               matches[lpc * 2],
               stdout);
        fputc('\n', stdout);
    }
}

void grep_proc::start(void)
{
    require(this->invariant());

    if (this->gp_child_started || this->gp_queue.empty()) {
        return;
    }

    auto_pipe in_pipe(STDIN_FILENO);
    auto_pipe out_pipe(STDOUT_FILENO);
    auto_pipe err_pipe(STDERR_FILENO);

    /* Get ahold of some pipes for stdout and stderr. */
    if (out_pipe.open() < 0) {
        throw error(errno);
    }

    if (err_pipe.open() < 0) {
        throw error(errno);
    }

    if ((this->gp_child = fork()) < 0) {
        throw error(errno);
    }

    in_pipe.after_fork(this->gp_child);
    out_pipe.after_fork(this->gp_child);
    err_pipe.after_fork(this->gp_child);

    if (this->gp_child != 0) {
        log_perror(fcntl(out_pipe.read_end(), F_SETFL, O_NONBLOCK));
        log_perror(fcntl(out_pipe.read_end(), F_SETFD, 1));
        this->gp_line_buffer.set_fd(out_pipe.read_end());

        log_perror(fcntl(err_pipe.read_end(), F_SETFL, O_NONBLOCK));
        log_perror(fcntl(err_pipe.read_end(), F_SETFD, 1));
        require(this->gp_err_pipe.get() == -1);
        this->gp_err_pipe      = err_pipe.read_end();
        this->gp_child_started = true;

        this->gp_queue.clear();
        return;
    }

    /* In the child... */

    /*
     * Restore the default signal handlers so we don't hang around
     * forever if there is a problem.
     */
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);

    this->child_init();

    this->child_loop();

    _exit(0);
}

void grep_proc::child_loop(void)
{
    char   outbuf[BUFSIZ * 2];
    string line_value;

    stdout = fdopen(STDOUT_FILENO, "w");
    /* Make sure buffering is on, not sure of the state in the parent. */
    if (setvbuf(stdout, outbuf, _IOFBF, BUFSIZ * 2) < 0) {
        perror("setvbuf");
    }
    line_value.reserve(BUFSIZ * 2);
    while (!this->gp_queue.empty()) {
        grep_line_t start_line = this->gp_queue.front().first;
        grep_line_t stop_line  = this->gp_queue.front().second;
        bool        done       = false;
        int         line;

        this->gp_queue.pop_front();
        if (start_line == -1) {
            start_line = this->gp_highest_line;
        }
        for (line = start_line;
             (stop_line == -1 || line < stop_line) && !done;
             line++) {
            line_value.clear();
            done = !this->gp_source.grep_value_for_line(line, line_value);
            if (!done) {
                pcre_context_static<128> pc;
                pcre_input pi(line_value);

                while (this->gp_pcre.match(pc, pi)) {
                    pcre_context::iterator   pc_iter;
                    pcre_context::capture_t *m;

                    if (pi.pi_offset == 0) {
                        fprintf(stdout, "%d\n", line);
                    }
                    m = pc.all();
                    fprintf(stdout, "[%d:%d]\n", m->c_begin, m->c_end);
                    for (pc_iter = pc.begin(); pc_iter != pc.end();
                         pc_iter++) {
                        if (!pc_iter->is_valid()) {
                            continue;
                        }
                        fprintf(stdout,
                                "(%d:%d)",
                                pc_iter->c_begin,
                                pc_iter->c_end);

                        /* If the capture was conditional, pcre will return a -1
                         * here.
                         */
                        if (pc_iter->c_begin >= 0) {
                            fwrite(pi.get_substr_start(pc_iter),
                                   1,
                                   pc_iter->length(),
                                   stdout);
                        }
                        fputc('\n', stdout);
                    }
                    fprintf(stdout, "/\n");
                }
            }

            if (((line + 1) % 10000) == 0) {
                /* Periodically flush the buffer so the parent sees progress */
                this->child_batch();
            }
        }

        if (stop_line == -1) {
            // When scanning to the end of the source, we need to return the
            // highest line that was seen so that the next request that
            // continues from the end works properly.
            fprintf(stdout, "h%d\n", line - 1);
        }
        this->child_term();
    }
}

void grep_proc::cleanup(void)
{
    if (this->gp_child != -1 && this->gp_child != 0) {
        int status;

        kill(this->gp_child, SIGTERM);
        while (waitpid(this->gp_child, &status, 0) < 0 && (errno == EINTR)) {
            ;
        }
        require(!WIFSIGNALED(status) || WTERMSIG(status) != SIGABRT);
        this->gp_child         = -1;
        this->gp_child_started = false;

        if (this->gp_sink) {
            this->gp_sink->grep_end(*this);
        }
    }

    if (this->gp_err_pipe != -1) {
        this->gp_err_pipe.reset();
    }

    this->gp_pipe_offset = 0;
    this->gp_line_buffer.reset();

    ensure(this->invariant());

    if (!this->gp_queue.empty()) {
        this->start();
    }
}

void grep_proc::dispatch_line(char *line)
{
    int start, end, capture_start;

    require(line != NULL);

    if (sscanf(line, "h%d", this->gp_highest_line.out()) == 1) {

    } else if (sscanf(line, "%d", this->gp_last_line.out()) == 1) {
        /* Starting a new line with matches. */
        ensure(this->gp_last_line >= 0);
    }
    else if (sscanf(line, "[%d:%d]", &start, &end) == 2) {
        require(start >= 0);
        require(end >= 0);

        /* Pass the match offsets to the sink delegate. */
        if (this->gp_sink != NULL) {
            this->gp_sink->grep_match(*this, this->gp_last_line, start, end);
        }
    }
    else if (sscanf(line, "(%d:%d)%n", &start, &end, &capture_start) == 2) {
        require(start == -1 || start >= 0);
        require(end >= 0);

        /* Pass the captured strings to the sink delegate. */
        if (this->gp_sink != NULL) {
            this->gp_sink->grep_capture(*this,
                                        this->gp_last_line,
                                        start,
                                        end,
                                        start < 0 ?
                                        NULL : &line[capture_start]);
        }
    }
    else if (line[0] == '/') {
        if (this->gp_sink != NULL) {
            this->gp_sink->grep_match_end(*this, this->gp_last_line);
        }
    }
    else {
        log_error("bad line from child -- %s", line);
    }
}

void grep_proc::check_poll_set(const std::vector<struct pollfd> &pollfds)
{
    require(this->invariant());

    if (this->gp_err_pipe != -1 && pollfd_ready(pollfds, this->gp_err_pipe)) {
        char buffer[1024 + 1];
        ssize_t rc;

        rc = read(this->gp_err_pipe, buffer, sizeof(buffer) - 1);
        if (rc > 0) {
            static const char *PREFIX = ": ";

            buffer[rc] = '\0';
            if (strncmp(buffer, PREFIX, strlen(PREFIX)) == 0) {
                char *lf;

                if ((lf = strchr(buffer, '\n')) != NULL) {
                    *lf = '\0';
                }
                if (this->gp_control != NULL) {
                    this->gp_control->grep_error(&buffer[strlen(PREFIX)]);
                }
            }
        }
        else if (rc == 0) {
            this->gp_err_pipe.reset();
        }
    }

    if (this->gp_line_buffer.get_fd() != -1 &&
        pollfd_ready(pollfds, this->gp_line_buffer.get_fd())) {
        try {
            static const int MAX_LOOPS = 100;

            int    loop_count = 0;
            line_value lv;

            while ((loop_count < MAX_LOOPS) &&
                   (this->gp_line_buffer.read_line(this->gp_pipe_offset, lv))) {
                lv.terminate();
                this->dispatch_line(lv.lv_start);
                loop_count += 1;
            }

            if (this->gp_sink != NULL) {
                this->gp_sink->grep_end_batch(*this);
            }

            if ((off_t) this->gp_line_buffer.get_file_size() ==
                this->gp_pipe_offset) {
                this->cleanup();
            }
        }
        catch (line_buffer::error & e) {
            this->cleanup();
        }
    }

    ensure(this->invariant());
}
