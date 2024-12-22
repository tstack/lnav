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
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "config.h"
#include "grep_proc.hh"
#include "vis_line.hh"

using namespace std;

static struct {
    int l_number;
    const char* l_value;
} MS_LINES[] = {
    {10, ""},
    {11, ""},
    {12, ""},
    {13, ""},
    {0, ""},
    {1, ""},
    {2, ""},
};

class my_source : public grep_proc_source<vis_line_t> {
public:
    my_source() : ms_current_line(0) {}

    std::optional<line_info> grep_value_for_line(vis_line_t line_number,
                                                 string& value_out) override
    {
        assert(line_number == MS_LINES[this->ms_current_line].l_number);
        value_out = MS_LINES[this->ms_current_line].l_value;

        this->ms_current_line += 1;

        return line_info{};
    }

    int ms_current_line;
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
    my_sink() : ms_finished(false) {};

    void grep_match(grep_proc<vis_line_t>& gp, vis_line_t line) override {}

    void grep_end(grep_proc<vis_line_t>& gp) override
    {
        this->ms_finished = true;
    }

    bool ms_finished;
};

static void
looper(grep_proc<vis_line_t>& gp)
{
    my_sink msink;

    gp.set_sink(&msink);

    while (!msink.ms_finished) {
        vector<struct pollfd> pollfds;

        gp.update_poll_set(pollfds);
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
        grep_proc<vis_line_t> gp(code, ms, psuperv);

        gp.queue_request(10_vl, 14_vl);
        gp.queue_request(0_vl, 3_vl);
        gp.start();
        looper(gp);
    }

    {
        my_sleeper_source mss;
        grep_proc<vis_line_t>* gp
            = new grep_proc<vis_line_t>(code, mss, psuperv);
        int status;

        gp->queue_request();
        gp->start();

        assert(wait3(&status, WNOHANG, NULL) == 0);

        delete gp;

        assert(wait(&status) == -1);
        assert(errno == ECHILD);
    }

    return retval;
}
