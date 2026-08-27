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

#include <charconv>
#include <iterator>
#include <utility>
#include <vector>

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
#include "vis_line.hh"

template<typename LineType>
grep_proc<LineType>::grep_proc(std::shared_ptr<lnav::pcre2pp::code> code,
                               grep_proc_source<LineType>& gps,
                               std::shared_ptr<pollable_supervisor> ps)
    : pollable(ps, pollable::category::background), gp_source(gps)
{
    require(this->invariant());

    this->set_pattern(0, std::move(code));
    gps.register_proc(this);
}

template<typename LineType>
grep_proc<LineType>::~grep_proc()
{
    this->invalidate();
}

/**
 * @return True if the given request covers everything that [start, stop) asks
 * for, so a run of its own would be redundant.
 */
template<typename LineType>
static bool
request_covers(const typename grep_proc<LineType>::request_t& req,
               LineType start,
               LineType stop)
{
    if (req.r_start > start) {
        return false;
    }

    return req.r_stop >= stop;
}

template<typename LineType>
bool
grep_proc<LineType>::merge_into_queue(LineType start,
                                      LineType stop,
                                      grep_pattern_mask_t patterns)
{
    // Fold the patterns into a request that is already waiting to cover this
    // ground instead of scanning it a second time.  Only the pending queue can
    // take them: a dispatched request belongs to a forked child that cannot be
    // told about a new pattern.  The sink is told to drop what the added
    // patterns found before, but not that another run is starting, since the
    // request that absorbed them still ends exactly once.
    for (auto& req : this->gp_queue) {
        if (!request_covers<LineType>(req, start, stop)) {
            continue;
        }
        if ((req.r_patterns & patterns) == patterns) {
            return true;
        }

        req.r_patterns |= patterns;
        if (this->gp_sink) {
            // The added patterns will be scanned over the whole of the
            // absorbing request, so that is the range whose old results are
            // being replaced.
            this->gp_sink->grep_reset(
                *this, req.r_start, req.r_stop, patterns);
        }
        return true;
    }

    return false;
}

template<typename LineType>
grep_proc<LineType>&
grep_proc<LineType>::queue_request(LineType start,
                                   LineType stop,
                                   std::optional<grep_pattern_mask_t> patterns)
{
    static constexpr LineType CHUNK_SIZE = LineType{100'000};

    auto mask = patterns.value_or(this->all_patterns_mask());
    if (mask == 0) {
        // Nothing to search for, so there is nothing to do.
        return *this;
    }

    auto merge_into_queue = [this, mask](LineType r_start, LineType r_stop) {
        return this->merge_into_queue(r_start, r_stop, mask);
    };

    if (start < LineType(0) || start > stop) {
        // A caller working from a line count that has since shrunk can ask for
        // a range that no longer exists.  There is nothing to search, and it
        // is not worth taking the process down over.
        log_error("grep_proc(%p): ignoring out-of-range request [%d:%d)",
                  this,
                  (int) start,
                  (int) stop);
        return *this;
    }

    auto total_lines = stop - start;
    while (total_lines > CHUNK_SIZE) {
        auto chunk_stop = start + CHUNK_SIZE;
        if (!merge_into_queue(start, chunk_stop)) {
            this->gp_queue.push_back(request_t{start, chunk_stop, mask});
            if (this->gp_sink) {
                this->gp_sink->grep_begin(*this, start, chunk_stop, mask);
            }
        }
        start += CHUNK_SIZE;
        total_lines -= CHUNK_SIZE;
    }
    if (total_lines > 0) {
        if (!merge_into_queue(start, stop)) {
            this->gp_queue.push_back(request_t{start, stop, mask});
            if (this->gp_sink) {
                this->gp_sink->grep_begin(*this, start, stop, mask);
            }
        }
    }

    return *this;
}

template<typename LineType>
void
grep_proc<LineType>::start()
{
    static constexpr size_t MAX_CHILDREN = 8;

    require(this->invariant());

    log_info("grep_proc(%p): start", this);
    if (this->children_active() || this->gp_queue.empty()) {
        log_debug("grep_proc(%p): nothing to do?", this);
        return;
    }

    if (this->gp_sink) {
        // XXX hack to make sure threads used by line_buffer are not active
        // before the fork.
        this->gp_sink->grep_quiesce();
    }

    for (const auto& [index, elem] : lnav::itertools::enumerate(this->gp_queue))
    {
        log_info("  queue[%lu]: [%d:%d) patterns=0x%x",
                 index,
                 (int) elem.r_start,
                 (int) elem.r_stop,
                 (unsigned) elem.r_patterns);
    }

    auto num_children = std::min({
        static_cast<size_t>(std::thread::hardware_concurrency()),
        this->gp_queue.size(),
        MAX_CHILDREN,
    });
    if (num_children == 0) {
        num_children = 1;
    }

    // Distribute queue items round-robin across children.
    std::vector<std::deque<request_t>> sub_queues(num_children);
    for (size_t i = 0; i < this->gp_queue.size(); i++) {
        sub_queues[i % num_children].push_back(this->gp_queue[i]);
    }

    for (size_t i = 0; i < num_children; i++) {
        if (sub_queues[i].empty()) {
            continue;
        }

        auto_pipe in_pipe(STDIN_FILENO);
        auto_pipe out_pipe(STDOUT_FILENO);
        auto_pipe err_pipe(STDERR_FILENO);

        if (out_pipe.open() < 0) {
            throw error(errno);
        }
        if (err_pipe.open() < 0) {
            throw error(errno);
        }

        auto child_res = lnav::pid::from_fork();
        if (child_res.isErr()) {
            throw error(errno);
        }
        auto child = child_res.unwrap();

        if (child.in_child()) {
            // Need to do this before the after_fork() stuff below
            lnav::pid::in_child = true;
            signal(SIGINT, SIG_DFL);
            signal(SIGTERM, SIG_DFL);

            // The JIT'd form of every pattern this process inherited belongs
            // to the parent and cannot be run here.
            lnav::pcre2pp::jit_after_fork();
        }

        in_pipe.after_fork(child.in());
        out_pipe.after_fork(child.in());
        err_pipe.after_fork(child.in());

        if (child.in_child()) {
            this->gp_queue = std::move(sub_queues[i]);
            this->child_init();
            this->child_loop();
            _exit(0);
        }

        // Parent: store child state.
        log_perror(fcntl(out_pipe.read_end(), F_SETFL, O_NONBLOCK));
        log_perror(fcntl(out_pipe.read_end(), F_SETFD, 1));
        log_perror(fcntl(err_pipe.read_end(), F_SETFL, O_NONBLOCK));
        log_perror(fcntl(err_pipe.read_end(), F_SETFD, 1));

        auto cs_ptr = std::make_unique<child_state>(std::move(child));
        cs_ptr->cs_line_buffer.set_fd(out_pipe.read_end());
        cs_ptr->cs_err_pipe = std::move(err_pipe.read_end());

        log_debug("grep_proc(%p): started child[%zu] %d with %zu request(s)",
                  this,
                  i,
                  cs_ptr->cs_child.in(),
                  sub_queues[i].size());
        this->gp_children.emplace_back(std::move(cs_ptr));
    }

    // Only now that the forks have all happened is the queue accounted for as
    // dispatched.  Committing this before the loop would leave a failure part
    // way through with the same requests both queued and dispatched, and they
    // would then be ended twice -- which the sink counts.
    //
    // What is handed to the children is remembered because a child is a forked
    // snapshot that cannot be partially cancelled, so if some other pattern is
    // invalidated later, this is what has to be queued again for the patterns
    // that survive.
    this->gp_child_queue_size = this->gp_queue.size();
    this->gp_dispatched = std::move(this->gp_queue);
    this->gp_queue.clear();
}

template<typename LineType>
void
grep_proc<LineType>::child_loop()
{
    // Matches are packed several to an output line so that the parent pays
    // the cost of pulling a line out of the pipe and picking it apart once
    // per batch instead of once per hit.  A pattern that matches a good
    // fraction of a large file can easily produce hundreds of thousands of
    // hits, at which point that per-line cost is what holds the search up.
    static constexpr size_t MATCHES_PER_LINE = 128;

    auto md = lnav::pcre2pp::match_data::unitialized();
    char outbuf[BUFSIZ * 2];
    std::string line_value;
    std::string match_batch;
    size_t batch_count = 0;

    /* Make sure buffering is on, not sure of the state in the parent. */
    if (setvbuf(stdout, outbuf, _IOFBF, BUFSIZ * 2) < 0) {
        perror("setvbuf");
    }
    lnav_log_file
        = make_optional_from_nullable(fopen("/tmp/lnav.grep.err", "a"));
    line_value.reserve(BUFSIZ * 2);
    match_batch.reserve(MATCHES_PER_LINE * 16);

    auto flush_batch = [&match_batch, &batch_count]() {
        if (batch_count == 0) {
            return;
        }

        match_batch.push_back('\n');
        fwrite(match_batch.data(), 1, match_batch.size(), stdout);
        match_batch.clear();
        batch_count = 0;
    };
    while (!this->gp_queue.empty()) {
        auto req = this->gp_queue.front();
        auto start_line = req.r_start;
        auto stop_line = req.r_stop;
        bool done = false;
        LineType line;

        // Collect the slots to test up front so the inner loop does not have
        // to walk the empty ones for every line.
        std::vector<size_t> slots;
        for (size_t slot = 0; slot < GREP_MAX_PATTERNS; slot++) {
            if ((req.r_patterns & grep_pattern_bit(slot))
                && this->gp_patterns[slot] != nullptr)
            {
                slots.push_back(slot);
            }
        }

        this->gp_queue.pop_front();
        for (line = this->gp_source.grep_initial_line(start_line);
             line != -1 && line < stop_line && !done;
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
                // The line is read once and then tested against every pattern
                // so that several searches can share a single pass over the
                // source.
                grep_pattern_mask_t matched = 0;
                for (const auto slot : slots) {
                    auto match_res = this->gp_patterns[slot]
                                         ->capture_from(line_value)
                                         .into(md)
                                         .matches(re_opts)
                                         .ignore_error();
                    if (match_res) {
                        matched |= grep_pattern_bit(slot);
                    }
                }
                if (matched != 0) {
                    if (batch_count > 0) {
                        match_batch.push_back(',');
                    }
                    fmt::format_to(std::back_inserter(match_batch),
                                   FMT_STRING("{}/{:x}"),
                                   (int) line,
                                   matched);
                    batch_count += 1;
                    if (batch_count == MATCHES_PER_LINE) {
                        flush_batch();
                    }
                }
            }

            if (((line + 1) % 10000) == 0) {
                /* Periodically flush the buffer so the parent sees progress */
                flush_batch();
                this->child_batch();
            }
        }

        flush_batch();
        this->child_term();
    }
}

template<typename LineType>
void
grep_proc<LineType>::cleanup()
{
    for (auto& cs_ptr : this->gp_children) {
        log_warning("terminating grep child %d", cs_ptr->cs_child.in());
        kill(cs_ptr->cs_child.in(), SIGTERM);
    }
    for (auto& cs_ptr : this->gp_children) {
        const auto runtime = cs_ptr->runtime();
        auto finished = std::move(cs_ptr->cs_child).wait_for_child();
        log_info("cleaned up grep child %d after %lldms",
                 finished.in(),
                 (long long) runtime.count());
    }
    this->gp_children.clear();

    if (this->gp_sink) {
        for (size_t lpc = 0; lpc < this->gp_child_queue_size; lpc++) {
            this->gp_sink->grep_end(*this);
        }
    }
    this->gp_child_queue_size = 0;
    // The children are gone, so there is nothing left to re-queue on their
    // behalf.  Any work that needed to survive has already been moved back
    // onto gp_queue by invalidate().
    this->gp_dispatched.clear();

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

    // The child writes a comma-separated run of "<line>/<patterns>", with the
    // patterns in hex.  It is picked apart by hand here: this runs over every
    // match, and a pattern that hits a good fraction of a large file will
    // produce hundreds of thousands of them, at which point a scan that has to
    // work out its format string first becomes the thing holding the search up.
    const auto* begin = line.data();
    const auto* end = begin + line.length();

    if (begin == end) {
        log_error("empty line from child");
        return;
    }

    const auto* iter = begin;
    while (iter != end) {
        int line_number = 0;
        auto num_res = std::from_chars(iter, end, line_number);
        if (num_res.ec != std::errc{} || num_res.ptr == end
            || *num_res.ptr != '/')
        {
            log_error("bad line from child -- %.*s", line.length(), begin);
            return;
        }

        grep_pattern_mask_t patterns = 0;
        auto mask_res = std::from_chars(num_res.ptr + 1, end, patterns, 16);
        if (mask_res.ec != std::errc{}
            || (mask_res.ptr != end && *mask_res.ptr != ','))
        {
            log_error("bad line from child -- %.*s", line.length(), begin);
            return;
        }

        /* Starting a new line with matches. */
        /* Pass the matching patterns to the sink delegate. */
        if (this->gp_sink != nullptr) {
            this->gp_sink->grep_match(*this, LineType{line_number}, patterns);
        }

        iter = mask_res.ptr == end ? end : mask_res.ptr + 1;
    }
}

template<typename LineType>
void
grep_proc<LineType>::check_poll_set(const std::vector<struct pollfd>& pollfds)
{
    require(this->invariant());

    bool any_finished = false;

    for (auto& cs_ptr : this->gp_children) {
        auto& cs = *cs_ptr;
        if (cs.cs_err_pipe != -1 && pollfd_ready(pollfds, cs.cs_err_pipe)) {
            char buffer[1024 + 1];
            ssize_t rc;

            rc = read(cs.cs_err_pipe, buffer, sizeof(buffer) - 1);
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
                } else {
                    // A child only writes here when something has gone wrong,
                    // so do not let it go by unseen.
                    log_warning("grep child %d stderr: %.*s",
                                cs.cs_child.in(),
                                (int) rc,
                                buffer);
                }
            } else if (rc == 0) {
                cs.cs_err_pipe.reset();
            }
        }

        if (cs.cs_line_buffer.get_fd() != -1
            && pollfd_ready(pollfds, cs.cs_line_buffer.get_fd()))
        {
            try {
                static const int MAX_LOOPS = 100;

                int loop_count = 0;
                bool drained = false;
                bool failed = false;

                while (loop_count < MAX_LOOPS) {
                    auto load_result
                        = cs.cs_line_buffer.load_next_line(cs.cs_pipe_range);

                    if (load_result.isErr()) {
                        log_error("failed to read from grep_proc child: %s",
                                  load_result.unwrapErr().c_str());
                        // Leaving the child in place would keep
                        // children_active() true forever, and start() refuses
                        // to run while that is so -- every later search would
                        // quietly do nothing.
                        failed = true;
                        break;
                    }

                    auto li = load_result.unwrap();

                    if (li.li_file_range.empty()) {
                        drained = true;
                        break;
                    }

                    if (li.li_partial && !cs.cs_line_buffer.is_pipe_closed()) {
                        // The child is still writing this line, so leave the
                        // range where it is and pick the line up in one piece
                        // on a later pass.
                        break;
                    }

                    cs.cs_pipe_range = li.li_file_range;
                    cs.cs_line_buffer.read_range(li.li_file_range)
                        .then([this](auto sbr) {
                            sbr.rtrim(is_line_ending);
                            this->dispatch_line(sbr.to_string_fragment());
                        });

                    loop_count += 1;
                }

                if (this->gp_sink != nullptr) {
                    this->gp_sink->grep_end_batch(*this);
                }

                if (failed || (drained && cs.cs_line_buffer.is_pipe_closed())) {
                    cs.cs_pipe_range.clear();
                    cs.cs_line_buffer.reset();
                    const auto runtime = cs.runtime();
                    if (failed) {
                        // Nothing more will be read from this child, but it
                        // has not necessarily stopped writing.  wait_for_child()
                        // blocks, so make sure there is an exit to wait for.
                        kill(cs.cs_child.in(), SIGTERM);
                    }
                    auto finished = std::move(cs.cs_child).wait_for_child();
                    // A child that died has produced no matches for the rest
                    // of its range, which looks exactly like a range with
                    // nothing in it.  Say so loudly instead.
                    if (!finished.was_normal_exit()) {
                        log_error("grep child %d killed by signal %d in %lldms",
                                  finished.in(),
                                  finished.term_signal(),
                                  (long long) runtime.count());
                    } else if (finished.exit_status() != 0) {
                        log_error("grep child %d exited with %d in %lldms",
                                  finished.in(),
                                  finished.exit_status(),
                                  (long long) runtime.count());
                    } else {
                        log_info("grep child %d finished in %lldms",
                                 finished.in(),
                                 (long long) runtime.count());
                    }
                    any_finished = true;
                }
            } catch (line_buffer::error& e) {
                const auto runtime = cs.runtime();
                cs.cs_pipe_range.clear();
                cs.cs_line_buffer.reset();
                auto finished = std::move(cs.cs_child).wait_for_child();
                log_info("grep child %d finished (error) in %lldms",
                         finished.in(),
                         (long long) runtime.count());
                any_finished = true;
            }
        }
    }

    if (any_finished) {
        // Remove children that have been waited on (pid == -1 after move).
        this->gp_children.erase(
            std::remove_if(this->gp_children.begin(),
                           this->gp_children.end(),
                           [](const std::unique_ptr<child_state>& cs_ptr) {
                               return cs_ptr->cs_child.in() == -1;
                           }),
            this->gp_children.end());

        if (this->gp_children.empty()) {
            this->cleanup();
        }
    }

    ensure(this->invariant());
}

template<typename LineType>
grep_proc<LineType>&
grep_proc<LineType>::invalidate(grep_pattern_mask_t patterns)
{
    log_debug(
        "grep_proc(%p): invalidated patterns 0x%x", this, (unsigned) patterns);

    // Strip the doomed patterns out of the pending requests.  A request that
    // still covers something else is kept so that its work is not lost.
    std::deque<request_t> survivors;
    for (auto& req : this->gp_queue) {
        req.r_patterns &= ~patterns;
        if (req.r_patterns != 0) {
            survivors.push_back(req);
        }
    }

    if (this->gp_sink) {
        auto dropped = this->gp_queue.size() - survivors.size();
        for (size_t lpc = 0; lpc < dropped; lpc++) {
            this->gp_sink->grep_end(*this);
        }
    }

    this->gp_queue = std::move(survivors);

    // cleanup() is about to kill the children, which cannot be told to drop
    // just one pattern.  Queue their work again for the patterns that are not
    // being invalidated.  Re-scanning a range that had already partly finished
    // is harmless: the sink records matches idempotently and grep_begin()
    // clears the range first.
    for (auto& req : this->gp_dispatched) {
        req.r_patterns &= ~patterns;
        if (req.r_patterns == 0) {
            continue;
        }
        if (this->merge_into_queue(req.r_start, req.r_stop, req.r_patterns)) {
            // A request that is still waiting covers this ground, so there is
            // no run of its own to announce.
            continue;
        }

        this->gp_queue.push_back(req);
        if (this->gp_sink) {
            this->gp_sink->grep_begin(
                *this, req.r_start, req.r_stop, req.r_patterns);
        }
    }
    this->gp_dispatched.clear();

    this->cleanup();
    return *this;
}

template<typename LineType>
void
grep_proc<LineType>::update_poll_set(std::vector<pollfd>& pollfds)
{
    for (auto& cs_ptr : this->gp_children) {
        if (cs_ptr->cs_line_buffer.get_fd() != -1) {
            pollfds.push_back(
                pollfd{cs_ptr->cs_line_buffer.get_fd(), POLLIN, 0});
        }
        if (cs_ptr->cs_err_pipe.get() != -1) {
            pollfds.push_back(pollfd{cs_ptr->cs_err_pipe, POLLIN, 0});
        }
    }
}

template class grep_proc<vis_line_t>;
