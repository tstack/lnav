/**
 * Copyright (c) 2014, Timothy Stack
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

#include "config.h"
#include "log_accel.hh"

static int64_t SIMPLE_TEST_POINTS[] = {90,
                                       80,
                                       40,
                                       30,
                                       20,
                                       10,

                                       -1};

static log_accel::direction_t SIMPLE_TEST_DIRS[] = {
    log_accel::direction_t::A_STEADY,
    log_accel::direction_t::A_DECEL,
    log_accel::direction_t::A_STEADY,
    log_accel::direction_t::A_STEADY,
    log_accel::direction_t::A_STEADY,
    log_accel::direction_t::A_STEADY,
    log_accel::direction_t::A_STEADY,
};

int
main(int argc, char* argv[])
{
    for (int point = 0; SIMPLE_TEST_POINTS[point] != -1; point++) {
        log_accel la;

        for (int lpc = point; SIMPLE_TEST_POINTS[lpc] != -1; lpc++) {
            if (!la.add_point(SIMPLE_TEST_POINTS[lpc])) {
                break;
            }
        }

        assert(SIMPLE_TEST_DIRS[point] == la.get_direction());
    }

    return EXIT_SUCCESS;
}