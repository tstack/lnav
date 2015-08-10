/**
 * Copyright (c) 2013, Timothy Stack
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
 * @file auto_pid.hh
 */

#ifndef __auto_pid_hh
#define __auto_pid_hh

#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

class auto_pid {
public:
    auto_pid(pid_t child = -1) : ap_child(child), ap_status(0) {};

    auto_pid(auto_pid &other) : ap_child(other.release()), ap_status(0) { };

    ~auto_pid() { this->reset(); };

    auto_pid &operator =(auto_pid &other) {
        this->reset(other.release());
        this->ap_status = other.ap_status;
        return *this;
    };

    bool in_child() const {
        return this->ap_child == 0;
    };

    pid_t release() {
        pid_t retval = this->ap_child;

        this->ap_child = -1;
        return retval;
    };

    int status() const {
        return this->ap_status;
    };

    bool wait_for_child(int options = 0) {
        if (this->ap_child != -1) {
            int rc;

            while ((rc = waitpid(this->ap_child,
                                 &this->ap_status,
                                 options)) < 0 && (errno == EINTR)) {
                ;
            }
            if (rc > 0) {
                this->ap_child = -1;
            }
        }

        return this->ap_child == -1;
    };

    void reset(pid_t child = -1) {
        if (this->ap_child != child) {
            this->ap_status = 0;
            if (this->ap_child != -1) {
                kill(this->ap_child, SIGTERM);
                this->wait_for_child();
            }
            this->ap_child = child;
        }
    };

private:
    auto_pid(const auto_pid &other) { };

    pid_t ap_child;
    int ap_status;
};

#endif
