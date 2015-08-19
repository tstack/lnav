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

#ifndef timer_hh
#define timer_hh

#include <errno.h>
#include <signal.h>
#include <exception>
#include <sys/time.h>
#include <sys/types.h>

// Linux and BSD seem to name the function pointer to signal handler differently.
// Linux names it 'sighandler_t' and BSD names it 'sig_t'. Instead of depending
// on configure scripts to find out the type name, typedef it right here.
typedef void (*sighandler_t_)(int);

namespace timer{
class error : public std::exception {
    public:
        error(int err);
        int e_err;
};

class interrupt_timer {
    public:
        interrupt_timer(struct timeval, sighandler_t_);
        int arm_timer();
        void disarm_timer();
        bool is_armed();
        ~interrupt_timer();
    private:
        sighandler_t_ new_handler;
        struct sigaction old_handler;
        struct itimerval new_val, old_val;
        bool armed;
};
}
#endif
