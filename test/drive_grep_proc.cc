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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "config.h"
#include "grep_proc.hh"
#include "line_buffer.hh"
#include "listview_curses.hh"

using namespace std;

class my_source : public grep_proc_source<vis_line_t> {
public:
    my_source(auto_fd& fd) { this->ms_buffer.set_fd(fd); };

    std::optional<line_info> grep_value_for_line(vis_line_t line_number, string& value_out) override
    {
        try {
            auto load_result = this->ms_buffer.load_next_line(this->ms_range);

            if (load_result.isOk()) {
                auto li = load_result.unwrap();

                this->ms_range = li.li_file_range;
                if (!li.li_file_range.empty()) {
                    auto read_result
                        = this->ms_buffer.read_range(li.li_file_range)
                              .then([&value_out](auto sbr) {
                                  value_out = to_string(sbr);
                              });

                    if (read_result.isOk()) {
                        return line_info{};
                    }
                }
            }
        } catch (const line_buffer::error& e) {
            fprintf(stderr,
                    "error: source buffer error %d %s\n",
                    this->ms_buffer.get_fd(),
                    strerror(e.e_err));
        }

        return std::nullopt;
    };

private:
    line_buffer ms_buffer;
    file_range ms_range;
};

class my_sink : public grep_proc_sink<vis_line_t> {
public:
    my_sink() : ms_finished(false) {}

    void grep_match(grep_proc<vis_line_t>& gp,
                    vis_line_t line) override
    {
        printf("%d\n", (int) line);
    }

    void grep_end(grep_proc<vis_line_t>& gp) override
    {
        this->ms_finished = true;
    }

    bool ms_finished;
};

int
main(int argc, char* argv[])
{
    int retval = EXIT_SUCCESS;
    auto_fd fd;

    if (argc < 3) {
        fprintf(stderr, "error: expecting pattern and file arguments\n");
        retval = EXIT_FAILURE;
    } else if ((fd = open(argv[2], O_RDONLY)) == -1) {
        perror("open");
        retval = EXIT_FAILURE;
    } else {
        auto compile_res = lnav::pcre2pp::code::from(
            string_fragment::from_c_str(argv[1]), PCRE2_CASELESS);

        if (compile_res.isErr()) {
            auto ce = compile_res.unwrapErr();
            fprintf(stderr,
                    "error: invalid pattern -- %s\n",
                    ce.get_message().c_str());
        } else {
            auto co = compile_res.unwrap().to_shared();
            auto psuperv = std::make_shared<pollable_supervisor>();
            my_source ms(fd);
            my_sink msink;

            grep_proc<vis_line_t> gp(co, ms, psuperv);

            gp.set_sink(&msink);
            gp.queue_request();
            gp.start();

            while (!msink.ms_finished) {
                vector<struct pollfd> pollfds;

                psuperv->update_poll_set(pollfds);
                poll(&pollfds[0], pollfds.size(), -1);

                psuperv->check_poll_set(pollfds);
            }
        }
    }

    return retval;
}
