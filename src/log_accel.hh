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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file log_accel.hh
 */

#ifndef __log_accel_h
#define __log_accel_h

#include <math.h>
#include <stdint.h>

#include <algorithm>

#include "lnav_log.hh"

/**
 * Helper class for figuring out changes in the log message rate.
 */
class log_accel {

public:
    /*< The direction of the message rate: steady, accelerating, or decelerating */
    enum direction_t {
        A_STEADY,
        A_DECEL,
        A_ACCEL,
    };

    log_accel()
        : la_last_point(0),
          la_last_point_set(false),
          la_min_velocity(INT64_MAX),
          la_max_velocity(INT64_MIN),
          la_velocity_size(0) {

    };

    /**
     * Add a time point that will be used to compute velocity and then
     * acceleration.  Points should be added in reverse order, from most
     * recent to oldest.
     * 
     * @param  point The point in time.
     * @return       True if more points can be added.
     */
    bool add_point(int64_t point) {
        require(this->la_velocity_size < HISTORY_SIZE);

        if (this->la_last_point_set) {
            // TODO Reenable this when we find the bug that causes some older
            // timestamps to show up after more recent ones.
            // require(this->la_last_point >= point);

            // Compute the message velocity.
            this->la_velocity[this->la_velocity_size] = (
                this->la_last_point - point);

            // Find the range of velocities so we can normalize.
            this->la_min_velocity = std::min(this->la_min_velocity,
                this->la_velocity[this->la_velocity_size]);
            this->la_max_velocity = std::max(this->la_max_velocity,
                this->la_velocity[this->la_velocity_size]);

            this->la_velocity_size += 1;
        }

        this->la_last_point = point;
        this->la_last_point_set = true;

        return this->la_velocity_size < HISTORY_SIZE;
    };

    /**
     * Get the average acceleration based on the time points we've received.
     * 
     * @return The average message acceleration.
     */
    double get_avg_accel() const {
        double avg_accel = 0, total_accel = 0;
        // Compute the range of values so we can normalize.
        double range = (double) (this->la_max_velocity - this->la_min_velocity);

        range = std::max(range, MIN_RANGE);
        for (int lpc = 0; lpc < (this->la_velocity_size - 1); lpc++) {
            double accel =
                (this->la_velocity[lpc] - this->la_velocity[lpc + 1]) / range;
            total_accel += accel;
        }

        if (this->la_velocity_size > 1) {
            avg_accel = total_accel / (double) (this->la_velocity_size - 1);
        }

        return avg_accel;
    };

    /**
     * Compute the message rate direction.  If the average acceleration is less
     * than a certain threshold, then we consider the rate to be steady.
     * Otherwise, the message rate is increasing or decreasing.
     *
     * @return The direction of the message rate.
     */
    direction_t get_direction() const {
        double avg_accel = this->get_avg_accel();
        direction_t retval;

        if (::fabs(avg_accel) <= THRESHOLD) {
            retval = A_STEADY;
        }
        else if (avg_accel < 0.0) {
            retval = A_ACCEL;
        }
        else {
            retval = A_DECEL;
        }

        return retval;
    };

private:
    /** 
     * The amount of historical data to include in the average acceleration
     * computation.
     */
    static const int HISTORY_SIZE = 8;
    /**
     * The minimum range of velocities seen.  This value should limit false-
     * positives for small millisecond level fluctuations.
     */
    static const double MIN_RANGE;
    static const double THRESHOLD;

    int64_t la_last_point;
    bool la_last_point_set;
    int64_t la_min_velocity;
    int64_t la_max_velocity;
    int64_t la_velocity[HISTORY_SIZE];
    int la_velocity_size;
};

#endif
