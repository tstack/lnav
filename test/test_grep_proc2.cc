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
 */

#include <assert.h>
#include <map>
#include <set>
#include <vector>

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "config.h"
#include "grep_proc.hh"
#include "vis_line.hh"

using namespace std;

static constexpr int MS_LINE_COUNT = 20;

/** @return True if the given line is one that the pattern should match. */
static bool
is_matching_line(int line_number)
{
    return (line_number % 2) == 0;
}

class my_source : public grep_proc_source<vis_line_t> {
public:
    std::optional<line_info> grep_value_for_line(vis_line_t line_number,
                                                 string& value_out) override
    {
        // The value has to be a function of the line number alone: the
        // requests are split across several children, so a cursor into a
        // fixed list would be wrong for every child but the first.
        if (line_number < 0 || line_number >= MS_LINE_COUNT) {
            return std::nullopt;
        }

        value_out = is_matching_line(line_number) ? "foobar" : "nothing here";

        return line_info{};
    }
};

/**
 * A source that walks a list of interesting lines and latches "done" when it
 * runs off the end, the way the metadata grepper does.  A child works through
 * several requests in a row, so this only comes out right if grep_proc calls
 * grep_initial_line() at the start of every one of them.
 */
class my_walking_source : public grep_proc_source<vis_line_t> {
public:
    vis_line_t grep_initial_line(vis_line_t start) override
    {
        this->mws_done = false;

        return this->next_from(start - 1_vl);
    }

    void grep_next_line(vis_line_t& line) override
    {
        line = this->next_from(line);
        if (line == -1) {
            this->mws_done = true;
        }
    }

    std::optional<line_info> grep_value_for_line(vis_line_t line_number,
                                                 string& value_out) override
    {
        if (this->mws_done) {
            return std::nullopt;
        }

        value_out = "foobar";

        return line_info{};
    }

private:
    /** @return The first interesting line after the given one, or -1. */
    vis_line_t next_from(vis_line_t line)
    {
        for (auto next = line + 1_vl; next < MS_LINE_COUNT; next += 1_vl) {
            if (is_matching_line((int) next)) {
                return next;
            }
        }

        return -1_vl;
    }

    bool mws_done{false};
};

class my_sleeper_source : public grep_proc_source<vis_line_t> {
    std::optional<line_info> grep_value_for_line(vis_line_t line_number,
                                                 string& value_out) override
    {
        sleep(1000);
        return {};
    }
};

class my_sink : public grep_proc_sink<vis_line_t> {
public:
    void grep_begin(grep_proc<vis_line_t>& gp,
                    vis_line_t start,
                    vis_line_t stop,
                    grep_pattern_mask_t patterns) override
    {
        this->ms_outstanding += 1;
        this->ms_begin_starts.push_back(start);
    }

    void grep_match(grep_proc<vis_line_t>& gp,
                    vis_line_t line,
                    grep_pattern_mask_t patterns) override
    {
        this->ms_matches.insert((int) line);
        this->ms_matches_for_pattern[patterns].insert((int) line);
    }

    void grep_end(grep_proc<vis_line_t>& gp) override
    {
        this->ms_outstanding -= 1;
        // Every begin is answered by exactly one end; the views count on that
        // to know when a search has finished.
        assert(this->ms_outstanding >= 0);
        this->ms_ended += 1;
    }

    std::vector<vis_line_t> ms_begin_starts;
    std::set<int> ms_matches;
    std::map<grep_pattern_mask_t, std::set<int>> ms_matches_for_pattern;
    int ms_outstanding{0};
    int ms_ended{0};
};

static void
looper(grep_proc<vis_line_t>& gp, my_sink& msink)
{
    while (msink.ms_ended == 0 || msink.ms_outstanding > 0) {
        vector<struct pollfd> pollfds;

        gp.update_poll_set(pollfds);
        if (pollfds.empty()) {
            break;
        }
        poll(&pollfds[0], pollfds.size(), -1);

        gp.check_poll_set(pollfds);
    }
}

int
main(int argc, char* argv[])
{
    int retval = EXIT_SUCCESS;

    auto code
        = lnav::pcre2pp::code::from_const("foobar", PCRE2_CASELESS).to_shared();

    auto psuperv = std::make_shared<pollable_supervisor>();
    {
        my_source ms;
        my_sink msink;
        grep_proc<vis_line_t> gp(code, ms, psuperv);

        // The sink has to be in place before anything is queued, otherwise it
        // sees the ends without the matching begins.
        gp.set_sink(&msink);
        gp.queue_request(10_vl, 14_vl);
        gp.queue_request(0_vl, 3_vl);
        gp.start();
        looper(gp, msink);

        // These requests are handed to different children, so a child that
        // died early shows up here as a missing match.
        std::set<int> expected;
        for (const auto line : {10, 11, 12, 13, 0, 1, 2}) {
            if (is_matching_line(line)) {
                expected.insert(line);
            }
        }
        if (msink.ms_matches != expected) {
            fprintf(stderr, "error: matched lines are wrong:");
            for (const auto line : msink.ms_matches) {
                fprintf(stderr, " %d", line);
            }
            fprintf(stderr, "\n");
            retval = EXIT_FAILURE;
        }
        assert(msink.ms_outstanding == 0);
    }

    {
        // A tail scan must not be treated as covering a request that starts at
        // the beginning, or a pattern added later is never run over the lines
        // before that point.
        my_source ms;
        my_sink msink;
        grep_proc<vis_line_t> gp(code, ms, psuperv);
        auto second_pattern = gp.set_pattern(1, code);

        gp.set_sink(&msink);

        // With children running, start() leaves anything queued behind for the
        // next batch, which is what puts these two requests in the queue
        // together.
        gp.queue_request(0_vl, 2_vl, grep_pattern_bit(0));
        gp.start();
        gp.queue_request(
            vis_line_t(MS_LINE_COUNT - 2), vis_line_t(MS_LINE_COUNT),
            second_pattern);
        gp.queue_request(0_vl, vis_line_t(MS_LINE_COUNT), second_pattern);
        looper(gp, msink);

        auto& second_matches = msink.ms_matches_for_pattern[second_pattern];
        for (int lpc = 0; lpc < MS_LINE_COUNT; lpc++) {
            if (!is_matching_line(lpc)) {
                continue;
            }
            if (second_matches.count(lpc) == 0) {
                fprintf(stderr,
                        "error: line %d was not matched by the second "
                        "pattern\n",
                        lpc);
                retval = EXIT_FAILURE;
                break;
            }
        }
        assert(msink.ms_outstanding == 0);
    }

    {
        // More requests than there are children means at least one child works
        // through two of them, which is where a source that only arms its walk
        // once would come up empty for the second.
        static constexpr size_t REQUEST_COUNT = 9;

        my_walking_source mws;
        my_sink msink;
        grep_proc<vis_line_t> gp(code, mws, psuperv);

        gp.set_sink(&msink);
        // Queued from the highest start down, so that no request covers the
        // ground of one already in the queue and they all survive as separate
        // runs.  Each gets its own pattern slot, so a run that reports nothing
        // is visible here.
        for (size_t lpc = REQUEST_COUNT; lpc > 0; lpc--) {
            auto slot_bit = gp.set_pattern(lpc - 1, code);

            gp.queue_request(
                vis_line_t(lpc - 1), vis_line_t(MS_LINE_COUNT), slot_bit);
        }
        gp.start();
        looper(gp, msink);

        grep_pattern_mask_t reported = 0;
        for (const auto& pair : msink.ms_matches_for_pattern) {
            reported |= pair.first;
        }
        for (size_t lpc = 0; lpc < REQUEST_COUNT; lpc++) {
            if (!(reported & grep_pattern_bit(lpc))) {
                fprintf(stderr,
                        "error: the request for slot %zu reported nothing\n",
                        lpc);
                retval = EXIT_FAILURE;
                break;
            }
        }
        assert(msink.ms_outstanding == 0);
    }

    {
        // Two requests over the same ground fold together rather than each
        // forking a child of its own.
        my_source ms;
        my_sink msink;
        grep_proc<vis_line_t> gp(code, ms, psuperv);
        auto second_pattern = gp.set_pattern(1, code);

        gp.set_sink(&msink);
        gp.queue_request(0_vl, vis_line_t(MS_LINE_COUNT), grep_pattern_bit(0));
        gp.queue_request(0_vl, vis_line_t(MS_LINE_COUNT), second_pattern);

        if (msink.ms_begin_starts.size() != 1) {
            fprintf(stderr,
                    "error: %zu runs were announced for the same ground\n",
                    msink.ms_begin_starts.size());
            retval = EXIT_FAILURE;
        }
        gp.start();
        looper(gp, msink);

        // The folded-in pattern still has to be searched for.
        auto& second_matches = msink.ms_matches_for_pattern[second_pattern
                                                            | grep_pattern_bit(0)];
        if (second_matches.empty()) {
            fprintf(stderr, "error: the folded-in pattern found nothing\n");
            retval = EXIT_FAILURE;
        }
        assert(msink.ms_outstanding == 0);
    }

    {
        my_sleeper_source mss;
        grep_proc<vis_line_t>* gp
            = new grep_proc<vis_line_t>(code, mss, psuperv);
        int status;

        // The source blocks forever, so the stop line is never reached; this
        // is about the child being killed off with the proc.
        gp->queue_request(0_vl, vis_line_t(INT32_MAX));
        gp->start();

        assert(wait3(&status, WNOHANG, NULL) == 0);

        delete gp;

        assert(wait(&status) == -1);
        assert(errno == ECHILD);
    }

    return retval;
}
