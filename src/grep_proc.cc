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
 * @file grep_proc.cc
 */

#include "grep_proc.hh"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "base/auto_pid.hh"
#include "base/itertools.enumerate.hh"
#include "base/lnav_log.hh"
#include "base/opt_util.hh"
#include "base/string_util.hh"
#include "config.h"
#include "lnav_util.hh"
#include "scn/scan.h"
#include "vis_line.hh"

template<typename LineType>
grep_proc<LineType>::grep_proc(std::shared_ptr<lnav::pcre2pp::code> code,
                               grep_proc_source<LineType>& gps,
                               std::shared_ptr<pollable_supervisor> ps)
    : pollable(ps, pollable::category::background), gp_pcre(code),
      gp_source(gps)
{
    require(this->invariant());

    gps.register_proc(this);
}

template<typename LineType>
grep_proc<LineType>::~grep_proc()
{
    this->invalidate();
}

template<typename LineType>
void
grep_proc<LineType>::start()
{
    require(this->invariant());

    if (this->gp_sink) {
        // XXX hack to make sure threads used by line_buffer are not active
        // before the fork.
        this->gp_sink->grep_quiesce();
    }

    log_info(
        "grep_proc(%p): start with highest %d", this, this->gp_highest_line);
    if (this->gp_child_started || this->gp_queue.empty()) {
        log_debug("grep_proc(%p): nothing to do?", this);
        return;
    }
    for (const auto& [index, elem] : lnav::itertools::enumerate(this->gp_queue))
    {
        log_info("  queue[%d]: [%d:%d)", index, elem.first, elem.second);
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
        this->gp_err_pipe = std::move(err_pipe.read_end());
        this->gp_child_started = true;
        this->gp_child_queue_size = this->gp_queue.size();

        this->gp_queue.clear();

        log_debug("grep_proc(%p): started child %d", this, this->gp_child);
        return;
    }

    /* In the child... */
    lnav::pid::in_child = true;

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

template<typename LineType>
void
grep_proc<LineType>::child_loop()
{
    auto md = lnav::pcre2pp::match_data::unitialized();
    char outbuf[BUFSIZ * 2];
    std::string line_value;

    /* Make sure buffering is on, not sure of the state in the parent. */
    if (setvbuf(stdout, outbuf, _IOFBF, BUFSIZ * 2) < 0) {
        perror("setvbuf");
    }
    lnav_log_file
        = make_optional_from_nullable(fopen("/tmp/lnav.grep.err", "a"));
    line_value.reserve(BUFSIZ * 2);
    while (!this->gp_queue.empty()) {
        LineType start_line = this->gp_queue.front().first;
        LineType stop_line = this->gp_queue.front().second;
        bool done = false;
        LineType line;

        this->gp_queue.pop_front();
        for (line = this->gp_source.grep_initial_line(start_line,
                                                      this->gp_highest_line);
             line != -1 && (stop_line == -1 || line < stop_line) && !done;
             this->gp_source.grep_next_line(line))
        {
            line_value.clear();
            auto val_res
                = this->gp_source.grep_value_for_line(line, line_value);
            if (!val_res) {
                done = true;
            } else {
                auto li = val_res.value();
                uint32_t re_opts = 0;
                if (li.li_utf8_scan_result.is_valid()) {
                    re_opts = PCRE2_NO_UTF_CHECK;
                }
                auto match_res = this->gp_pcre->capture_from(line_value)
                                     .into(md)
                                     .matches(re_opts)
                                     .ignore_error();
                if (match_res) {
                    fmt::println(stdout, FMT_STRING("{}"), (int) line);
                }
            }

            if (((line + 1) % 10000) == 0) {
                /* Periodically flush the buffer so the parent sees progress */
                this->child_batch();
            }
        }

        if (line != -1 && stop_line == -1) {
            // When scanning to the end of the source, we need to return the
            // highest line that was seen so that the next request that
            // continues from the end works properly.
            fmt::println(stdout, FMT_STRING("h{}"), line - 1);
        }
        this->gp_highest_line = line - 1_vl;
        this->child_term();
    }
}

template<typename LineType>
void
grep_proc<LineType>::cleanup()
{
    if (this->gp_child != -1 && this->gp_child != 0) {
        int status = 0;

        kill(this->gp_child, SIGTERM);
        while (waitpid(this->gp_child, &status, 0) < 0 && (errno == EINTR)) {
            ;
        }
        require(!WIFSIGNALED(status) || WTERMSIG(status) != SIGABRT);

        log_info("cleaned up grep child %d", this->gp_child);
        this->gp_child = -1;
        this->gp_child_started = false;

        if (this->gp_sink) {
            for (size_t lpc = 0; lpc < this->gp_child_queue_size; lpc++) {
                this->gp_sink->grep_end(*this);
            }
        }
    }

    if (this->gp_err_pipe != -1) {
        this->gp_err_pipe.reset();
    }

    this->gp_pipe_range.clear();
    this->gp_line_buffer.reset();

    ensure(this->invariant());

    if (!this->gp_queue.empty()) {
        this->start();
    }
}

template<typename LineType>
void
grep_proc<LineType>::dispatch_line(const string_fragment& line)
{
    require(line.is_valid());

    auto sv = line.to_string_view();
    auto h_scan_res = scn::scan<int>(sv, "h{}");
    if (h_scan_res) {
        this->gp_highest_line = LineType{h_scan_res->value()};
    } else {
        auto ll_scan_res = scn::scan<int>(sv, "{}");
        if (ll_scan_res) {
            this->gp_last_line = LineType{ll_scan_res->value()};
            /* Starting a new line with matches. */
            ensure(this->gp_last_line >= 0);
            /* Pass the match offsets to the sink delegate. */
            if (this->gp_sink != nullptr) {
                this->gp_sink->grep_match(*this, this->gp_last_line);
            }
        } else {
            log_error("bad line from child -- %s", line);
        }
    }
}

template<typename LineType>
void
grep_proc<LineType>::check_poll_set(const std::vector<struct pollfd>& pollfds)
{
    require(this->invariant());

    if (this->gp_err_pipe != -1 && pollfd_ready(pollfds, this->gp_err_pipe)) {
        char buffer[1024 + 1];
        ssize_t rc;

        rc = read(this->gp_err_pipe, buffer, sizeof(buffer) - 1);
        if (rc > 0) {
            static const char* PREFIX = ": ";

            buffer[rc] = '\0';
            if (strncmp(buffer, PREFIX, strlen(PREFIX)) == 0) {
                char* lf;

                if ((lf = strchr(buffer, '\n')) != nullptr) {
                    *lf = '\0';
                }
                if (this->gp_control != nullptr) {
                    this->gp_control->grep_error(&buffer[strlen(PREFIX)]);
                }
            }
        } else if (rc == 0) {
            this->gp_err_pipe.reset();
        }
    }

    if (this->gp_line_buffer.get_fd() != -1
        && pollfd_ready(pollfds, this->gp_line_buffer.get_fd()))
    {
        try {
            static const int MAX_LOOPS = 100;

            int loop_count = 0;
            bool drained = false;

            while (loop_count < MAX_LOOPS) {
                auto load_result
                    = this->gp_line_buffer.load_next_line(this->gp_pipe_range);

                if (load_result.isErr()) {
                    log_error("failed to read from grep_proc child: %s",
                              load_result.unwrapErr().c_str());
                    break;
                }

                auto li = load_result.unwrap();

                if (li.li_file_range.empty()) {
                    drained = true;
                    break;
                }

                this->gp_pipe_range = li.li_file_range;
                this->gp_line_buffer.read_range(li.li_file_range)
                    .then([this](auto sbr) {
                        sbr.rtrim(is_line_ending);
                        this->dispatch_line(sbr.to_string_fragment());
                    });

                loop_count += 1;
            }

            if (this->gp_sink != nullptr) {
                this->gp_sink->grep_end_batch(*this);
            }

            if (drained && this->gp_line_buffer.is_pipe_closed()) {
                this->cleanup();
            }
        } catch (line_buffer::error& e) {
            this->cleanup();
        }
    }

    ensure(this->invariant());
}

template<typename LineType>
grep_proc<LineType>&
grep_proc<LineType>::invalidate()
{
    if (this->gp_sink) {
        for (size_t lpc = 0; lpc < this->gp_queue.size(); lpc++) {
            this->gp_sink->grep_end(*this);
        }
    }
    this->gp_queue.clear();
    this->cleanup();
    return *this;
}

template<typename LineType>
void
grep_proc<LineType>::update_poll_set(std::vector<struct pollfd>& pollfds)
{
    if (this->gp_line_buffer.get_fd() != -1) {
        pollfds.push_back(
            (struct pollfd) {this->gp_line_buffer.get_fd(), POLLIN, 0});
    }
    if (this->gp_err_pipe.get() != -1) {
        pollfds.push_back((struct pollfd) {this->gp_err_pipe, POLLIN, 0});
    }
}

template class grep_proc<vis_line_t>;
