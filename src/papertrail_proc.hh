/**
 * Copyright (c) 2015, Timothy Stack
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
 * @file papertrail_proc.hh
 */

#ifndef LNAV_PAPERTRAIL_PROC_HH
#define LNAV_PAPERTRAIL_PROC_HH

#include <fcntl.h>
#include <paths.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#include <memory>
#include <string>

#include "auto_fd.hh"
#include "auto_mem.hh"
#include "yajlpp.hh"
#include "line_buffer.hh"

class papertrail_proc {

public:
    papertrail_proc(const std::string &search)
            : ptp_search(search), ptp_child(-1) {
    };

    ~papertrail_proc() {
        // TODO: refactor this with piper_proc
        if (this->ptp_child > 0) {
            int status;

            kill(this->ptp_child, SIGTERM);
            while (waitpid(this->ptp_child, &status, 0) < 0 && (errno == EINTR)) {
                ;
            }

            this->ptp_child = -1;
        }
    }

    bool start(void);
    void child_body(void);

    static size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp);

    static void yajl_writer(void *context, const char *str, size_t len);
    static struct json_path_handler FORMAT_HANDLERS[];

    const char *ptp_api_key;
    const std::string ptp_search;
    auto_fd ptp_fd;
    pid_t ptp_child;
    line_buffer ptp_line_buffer;
    yajl_gen ptp_gen;
    std::string ptp_last_max_id;
    std::string ptp_error;
};

#endif //LNAV_PAPERTRAIL_PROC_HH
