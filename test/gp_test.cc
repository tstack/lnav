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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "grep_proc.hh"
#include "line_buffer.hh"
#include "logfile.hh"

class my_source : public grep_proc_source {
public:
    logfile* ms_lf;

    my_source(logfile* lf) : ms_lf(lf){};

    size_t grep_lines(void)
    {
        return this->ms_lf->size();
    };

    void grep_value_for_line(int line, std::string& value_out, int pass)
    {
        value_out = this->ms_lf->read_line(this->ms_lf->begin() + line);
    };
};

class my_sink : public grep_proc_sink {
public:
    void grep_match(grep_line_t line, int start, int end)
    {
        printf("%d - %d:%d\n", (int) line, start, end);
    };
};

int
main(int argc, char* argv[])
{
    int retval = EXIT_SUCCESS;
    auto_fd fd;

    fd = open("/tmp/gp.err", O_WRONLY | O_CREAT | O_APPEND, 0666);
    dup2(fd, STDERR_FILENO);
    fprintf(stderr, "startup\n");

    if (argc < 2) {
        fprintf(stderr, "error: no file given\n");
    } else {
        logfile lf(argv[1]);
        lf.rebuild_index();
        my_source ms(&lf);
        my_sink msink;
        grep_proc gp("pnp", ms);

        gp.start();
        gp.set_sink(&msink);

        fd_set read_fds;

        int maxfd = gp.update_fd_set(read_fds);

        while (1) {
            fd_set rfds = read_fds;
            select(maxfd + 1, &rfds, NULL, NULL, NULL);

            gp.check_fd_set(rfds);
            if (!FD_ISSET(maxfd, &read_fds))
                break;
        }
    }

    return retval;
}
