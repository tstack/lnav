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
 * and/or otherlist materials provided with the distribution.
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
 * @file log_accel.hh
 */

#ifndef log_accel_h
#define log_accel_h

#include <cmath>

#include <stdint.h>

#include "base/lnav_log.hh"

/**
 * Helper class for figuring out changes in the log message rate.
 */
class log_accel {
public:
    /*< The direction of the message rate: steady, accelerating, or decelerating
     */
    enum class direction_t {
        A_STEADY,
        A_DECEL,
        A_ACCEL,
    };

    /**
     * Add a time point that will be used to compute velocity and then
     * acceleration.  Points should be added in reverse order, from most
     * recent to oldest.
     *
     * @param  point The point in time.
     * @return       True if more points can be added.
     */
    bool add_point(int64_t point);

    /**
     * Get the average acceleration based on the time points we've received.
     *
     * @return The average message acceleration.
     */
    double get_avg_accel() const;

    /**
     * Compute the message rate direction.  If the average acceleration is less
     * than a certain threshold, then we consider the rate to be steady.
     * Otherwise, the message rate is increasing or decreasing.
     *
     * @return The direction of the message rate.
     */
    direction_t get_direction() const;

private:
    /**
     * The amount of historical data to include in the average acceleration
     * computation.
     */
    static constexpr int HISTORY_SIZE = 8;
    /**
     * The minimum range of velocities seen.  This value should limit false-
     * positives for small millisecond level fluctuations.
     */
    static const double MIN_RANGE;
    static const double THRESHOLD;

    int64_t la_last_point{0};
    bool la_last_point_set{false};
    int64_t la_min_velocity{INT64_MAX};
    int64_t la_max_velocity{INT64_MIN};
    int64_t la_velocity[HISTORY_SIZE] = {};
    int la_velocity_size{0};
};

#endif
