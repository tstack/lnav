/**
 * Copyright (c) 2021, Timothy Stack
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

#include <chrono>

#include "humanize.time.hh"

#include "config.h"
#include "fmt/format.h"
#include "math_util.hh"
#include "time_util.hh"

namespace humanize {
namespace time {

using namespace std::chrono_literals;

point
point::from_tv(const timeval& tv)
{
    return point(tv);
}

std::string
point::as_time_ago() const
{
    struct timeval current_time
        = this->p_recent_point.value_or(current_timeval());

    if (this->p_convert_to_local) {
        current_time.tv_sec = convert_log_time_to_local(current_time.tv_sec);
    }

    auto curr_secs = std::chrono::seconds(current_time.tv_sec);
    auto past_secs = std::chrono::seconds(this->p_past_point.tv_sec);
    auto delta = curr_secs - past_secs;
    if (delta < 0s) {
        return "in the future";
    }
    if (delta < 1min) {
        return "just now";
    }
    if (delta < 2min) {
        return "one minute ago";
    }
    if (delta < 1h) {
        return fmt::format(
            FMT_STRING("{} minutes ago"),
            std::chrono::duration_cast<std::chrono::minutes>(delta).count());
    }
    if (delta < 2h) {
        return "one hour ago";
    }
    if (delta < 24h) {
        return fmt::format(
            FMT_STRING("{} hours ago"),
            std::chrono::duration_cast<std::chrono::hours>(delta).count());
    }
    if (delta < 48h) {
        return "one day ago";
    }
    if (delta < 365 * 24h) {
        return fmt::format(FMT_STRING("{} days ago"), delta / 24h);
    }
    if (delta < (2 * 365 * 24h)) {
        return "over a year ago";
    }
    return fmt::format(FMT_STRING("over {} years ago"), delta / (365 * 24h));
}

std::string
point::as_precise_time_ago() const
{
    struct timeval now, diff;

    now = this->p_recent_point.value_or(current_timeval());
    if (this->p_convert_to_local) {
        now.tv_sec = convert_log_time_to_local(now.tv_sec);
    }

    timersub(&now, &this->p_past_point, &diff);
    if (diff.tv_sec < 0) {
        return this->as_time_ago();
    } else if (diff.tv_sec <= 1) {
        return "a second ago";
    } else if (diff.tv_sec < (10 * 60)) {
        if (diff.tv_sec < 60) {
            return fmt::format(FMT_STRING("{:2} seconds ago"), diff.tv_sec);
        }

        lnav::time64_t seconds = diff.tv_sec % 60;
        lnav::time64_t minutes = diff.tv_sec / 60;

        return fmt::format(FMT_STRING("{:2} minute{} and {:2} second{} ago"),
                           minutes,
                           minutes > 1 ? "s" : "",
                           seconds,
                           seconds == 1 ? "" : "s");
    } else {
        return this->as_time_ago();
    }
}

duration
duration::from_tv(const struct timeval& tv)
{
    return duration{tv};
}

std::string
duration::to_string() const
{
    /* 24h22m33s111 */

    static const struct rel_interval {
        uint64_t length;
        const char* format;
        const char* symbol;
    } intervals[] = {
        {1000, "%03lld%s", ""},
        {60, "%lld%s", "s"},
        {60, "%lld%s", "m"},
        {24, "%lld%s", "h"},
        {0, "%lld%s", "d"},
    };

    const auto* curr_interval = intervals;
    auto usecs = std::chrono::duration_cast<std::chrono::microseconds>(
                     std::chrono::seconds(this->d_timeval.tv_sec))
        + std::chrono::microseconds(this->d_timeval.tv_usec);
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(usecs);
    std::string retval;
    bool neg = false;

    if (millis < 0s) {
        neg = true;
        millis = -millis;
    }

    uint64_t remaining = millis.count();
    uint64_t scale = 1;
    if (this->d_msecs_resolution > 0) {
        remaining = roundup(remaining, this->d_msecs_resolution);
    }
    if (millis >= 10min) {
        remaining /= curr_interval->length;
        scale *= curr_interval->length;
        curr_interval += 1;
    }

    for (; curr_interval != std::end(intervals); curr_interval++) {
        uint64_t amount;
        char segment[32];
        auto skip = scale < this->d_msecs_resolution;

        if (curr_interval->length) {
            amount = remaining % curr_interval->length;
            remaining = remaining / curr_interval->length;
        } else {
            amount = remaining;
            remaining = 0;
        }
        scale *= curr_interval->length;

        if (amount == 0 && remaining == 0) {
            break;
        }

        if (skip) {
            continue;
        }

        snprintf(segment,
                 sizeof(segment),
                 curr_interval->format,
                 amount,
                 curr_interval->symbol);
        retval.insert(0, segment);
        if (remaining > 0 && amount < 10 && curr_interval->symbol[0]) {
            retval.insert(0, "0");
        }
    }

    if (neg) {
        retval.insert(0, "-");
    }

    return retval;
}

}  // namespace time
}  // namespace humanize
