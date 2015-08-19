/**
 * Copyright (c) 2015, Suresh Sundriyal
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
 * * Neither the name of the copyright holder nor the names of its contributors
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
 */

#include "config.h"

#include "timer.hh"
#include "lnav_log.hh"

static const struct itimerval DISABLE_TV = {
    { 0, 0 },
    { 0, 0 }
};

timer::error::error(int err):e_err(err) { }

timer::interrupt_timer::interrupt_timer(struct timeval t,
        sighandler_t_ sighandler=SIG_IGN) : new_handler(sighandler),
        new_val((struct itimerval){{0,0},t}),
        old_val(DISABLE_TV), armed(false) {
    memset(&this->old_handler, 0, sizeof(this->old_handler));
}

int timer::interrupt_timer::arm_timer() {
    struct sigaction sa;

    // Disable the interval timer before setting the handler and arming the
    // interval timer or else we will have a race-condition where the timer
    // might fire and the appropriate handler might not be set.
    if (setitimer(ITIMER_REAL, &DISABLE_TV, &this->old_val) != 0) {
        log_error("Unable to disable the timer: %s",
                  strerror(errno));
        return -1;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = this->new_handler;
    if (sigaction(SIGALRM, &sa, &this->old_handler) == -1) {
        log_error("Unable to set the signal handler: %s",
                  strerror(errno));
        if (setitimer(ITIMER_REAL, &this->old_val, NULL) != 0) {
            log_error("Unable to reset the interrupt timer: %s",
                      strerror(errno));
            throw timer::error(errno);
        }
        return -1;
    }

    if (setitimer(ITIMER_REAL, &this->new_val, NULL) != 0) {
        if(sigaction(SIGALRM, &this->old_handler, NULL) == -1) {
            log_error("Unable to reset the signal handler: %s",
                      strerror(errno));
            throw timer::error(errno);
        }
        log_error("Unable to set the timer: %s", strerror(errno));
        return -1;
    }
    this->armed = true;
    return 0;
}

bool timer::interrupt_timer::is_armed() {
    return this->armed;
}

void timer::interrupt_timer::disarm_timer() {
    if (this->armed) {
        // Disable the interval timer before resetting the handler and rearming
        // the previous interval timer or else we will have a race-condition
        // where the timer might fire and the appropriate handler might not be
        // set.
        if (setitimer(ITIMER_REAL, &DISABLE_TV, NULL) != 0) {
            log_error("Failed to disable the timer: %s",
                      strerror(errno));
            throw timer::error(errno);
        }
        if (sigaction(SIGALRM, &this->old_handler, NULL) == -1) {
            log_error("Failed to reinstall previous SIGALRM handler: %s",
                      strerror(errno));
            throw timer::error(errno);
        }
        if (setitimer(ITIMER_REAL, &this->old_val, NULL) != 0) {
            log_error("Failed to reset the timer to previous value: %s",
                      strerror(errno));
            throw timer::error(errno);
        }
        this->armed = false;
        this->old_val = DISABLE_TV;
        memset(&this->old_handler, 0, sizeof(this->old_handler));
    }
}

timer::interrupt_timer::~interrupt_timer() {
    this->disarm_timer();
}
