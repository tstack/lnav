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
 * @file piper_proc.cc
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <paths.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <poll.h>

#include "lnav_log.hh"
#include "piper_proc.hh"
#include "line_buffer.hh"

using namespace std;

static const char *STDIN_EOF_MSG = "---- END-OF-STDIN ----";

static ssize_t write_timestamp(int fd, off_t woff)
{
    char           time_str[64];
    struct timeval tv;
    char           ms_str[8];

    gettimeofday(&tv, NULL);
    strftime(time_str, sizeof(time_str), "%FT%T", localtime(&tv.tv_sec));
    snprintf(ms_str, sizeof(ms_str), ".%03d", (int)(tv.tv_usec / 1000));
    strcat(time_str, ms_str);
    strcat(time_str, "  ");
    return pwrite(fd, time_str, strlen(time_str), woff);
}

piper_proc::piper_proc(int pipefd, bool timestamp, const char *filename)
    : pp_fd(-1), pp_child(-1)
{
    require(pipefd >= 0);

    if (filename) {
        if ((this->pp_fd =
                 open(filename, O_RDWR | O_CREAT | O_TRUNC, 0600)) == -1) {
            perror("Unable to open output file for stdin");
            throw error(errno);
        }
    }
    else {
        char piper_tmpname[PATH_MAX];
        const char *tmpdir;

        if ((tmpdir = getenv("TMPDIR")) == NULL) {
            tmpdir = _PATH_VARTMP;
        }
        snprintf(piper_tmpname, sizeof(piper_tmpname),
                "%s/lnav.piper.XXXXXX",
                tmpdir);
        if ((this->pp_fd = mkstemp(piper_tmpname)) == -1) {
            throw error(errno);
        }

        unlink(piper_tmpname);
    }

    log_perror(fcntl(this->pp_fd.get(), F_SETFD, FD_CLOEXEC));

    this->pp_child = fork();
    switch (this->pp_child) {
    case -1:
        throw error(errno);

    case 0:
    {
        auto_fd     infd(pipefd);
        line_buffer lb;
        off_t       woff = 0, last_woff = 0;
        off_t       off  = 0, last_off = 0;
        int nullfd;

        nullfd = open("/dev/null", O_RDWR);
        if (pipefd != STDIN_FILENO) {
            dup2(nullfd, STDIN_FILENO);
        }
        dup2(nullfd, STDOUT_FILENO);
        for (int fd_to_close = 0; fd_to_close < 1024; fd_to_close++) {
            int flags;

            if (fd_to_close == this->pp_fd.get()) {
                continue;
            }
            if ((flags = fcntl(fd_to_close, F_GETFD)) == -1) {
                continue;
            }
            if (flags & FD_CLOEXEC) {
                close(fd_to_close);
            }
        }
        log_perror(fcntl(infd.get(), F_SETFL, O_NONBLOCK));
        lb.set_fd(infd);
        do {
            line_value lv;
            struct pollfd pfd = {
                    lb.get_fd(),
                    POLLIN,
                    0
            };

            poll(&pfd, 1, -1);
            last_off = off;
            while (lb.read_line(off, lv, true)) {
                ssize_t wrc;

                last_woff = woff;
                if (timestamp) {
                    wrc = write_timestamp(this->pp_fd, woff);
                    if (wrc == -1) {
                        perror("Unable to write to output file for stdin");
                        break;
                    }
                    woff += wrc;
                }

                /* Need to do pwrite here since the fd is used by the main
                 * lnav process as well.
                 */
                wrc = pwrite(this->pp_fd, lv.lv_start, lv.lv_len, woff);
                if (wrc == -1) {
                    perror("Unable to write to output file for stdin");
                    break;
                }
                woff += wrc;

                if (lv.lv_start[lv.lv_len - 1] != '\n' &&
                        (off != lb.get_file_size())) {
                    off = last_off;
                    woff = last_woff;
                }
                last_off = off;
            }
        } while (lb.is_pipe() && !lb.is_pipe_closed());

        if (timestamp) {
            ssize_t wrc;

            wrc = write_timestamp(this->pp_fd, woff);
            if (wrc == -1) {
                perror("Unable to write to output file for stdin");
                break;
            }
            woff += wrc;
            wrc   = pwrite(this->pp_fd,
                           STDIN_EOF_MSG, strlen(STDIN_EOF_MSG),
                           woff);
            if (wrc == -1) {
                perror("Unable to write to output file for stdin");
                break;
            }
        }
    }
        _exit(0);
        break;

    default:
        break;
    }
}

bool piper_proc::has_exited()
{
    if (this->pp_child > 0) {
        int rc, status;

        rc = waitpid(this->pp_child, &status, WNOHANG);
        if (rc == -1 || rc == 0) {
            return false;
        }
        this->pp_child = -1;
    }

    return true;
}

piper_proc::~piper_proc()
{
    if (this->pp_child > 0) {
        int status;

        kill(this->pp_child, SIGTERM);
        while (waitpid(this->pp_child, &status, 0) < 0 && (errno == EINTR)) {
            ;
        }

        this->pp_child = -1;
    }
}
