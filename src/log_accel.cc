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
 *
 * @file log_accel.cc
 */

#include <algorithm>

#include "log_accel.hh"

#include "base/lnav_log.hh"
#include "config.h"

const double log_accel::MIN_RANGE = 5.0;
const double log_accel::THRESHOLD = 0.1;

bool
log_accel::add_point(int64_t point)
{
    require(this->la_velocity_size < HISTORY_SIZE);

    if (this->la_last_point_set) {
        // TODO Reenable this when we find the bug that causes some older
        // timestamps to show up after more recent ones.
        // require(this->la_last_point >= point);

        // Compute the message velocity.
        this->la_velocity[this->la_velocity_size]
            = (this->la_last_point - point);

        // Find the range of velocities so we can normalize.
        this->la_min_velocity = std::min(
            this->la_min_velocity, this->la_velocity[this->la_velocity_size]);
        this->la_max_velocity = std::max(
            this->la_max_velocity, this->la_velocity[this->la_velocity_size]);

        this->la_velocity_size += 1;
    }

    this->la_last_point = point;
    this->la_last_point_set = true;

    return this->la_velocity_size < HISTORY_SIZE;
}

double
log_accel::get_avg_accel() const
{
    double avg_accel = 0, total_accel = 0;
    // Compute the range of values so we can normalize.
    double range = (double) (this->la_max_velocity - this->la_min_velocity);

    range = std::max(range, MIN_RANGE);
    for (int lpc = 0; lpc < (this->la_velocity_size - 1); lpc++) {
        double accel
            = (this->la_velocity[lpc] - this->la_velocity[lpc + 1]) / range;
        total_accel += accel;
    }

    if (this->la_velocity_size > 1) {
        avg_accel = total_accel / (double) (this->la_velocity_size - 1);
    }

    return avg_accel;
}

log_accel::direction_t
log_accel::get_direction() const
{
    double avg_accel = this->get_avg_accel();
    direction_t retval;

    if (std::fabs(avg_accel) <= THRESHOLD) {
        retval = direction_t::A_STEADY;
    } else if (avg_accel < 0.0) {
        retval = direction_t::A_ACCEL;
    } else {
        retval = direction_t::A_DECEL;
    }

    return retval;
}
