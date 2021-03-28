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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include "time_util.hh"
#include "humanize.time.hh"

namespace humanize {
namespace time {

std::string time_ago(time_t last_time, bool convert_local)
{
    time_t delta, current_time = ::time(nullptr);
    const char *fmt;
    char buffer[64];
    int amount;

    if (convert_local) {
        current_time = convert_log_time_to_local(current_time);
    }

    delta = current_time - last_time;
    if (delta < 0) {
        return "in the future";
    } else if (delta < 60) {
        return "just now";
    } else if (delta < (60 * 2)) {
        return "one minute ago";
    } else if (delta < (60 * 60)) {
        fmt = "%d minutes ago";
        amount = delta / 60;
    } else if (delta < (2 * 60 * 60)) {
        return "one hour ago";
    } else if (delta < (24 * 60 * 60)) {
        fmt = "%d hours ago";
        amount = delta / (60 * 60);
    } else if (delta < (2 * 24 * 60 * 60)) {
        return "one day ago";
    } else if (delta < (365 * 24 * 60 * 60)) {
        fmt = "%d days ago";
        amount = delta / (24 * 60 * 60);
    } else if (delta < (2 * 365 * 24 * 60 * 60)) {
        return "over a year ago";
    } else {
        fmt = "over %d years ago";
        amount = delta / (365 * 24 * 60 * 60);
    }

    snprintf(buffer, sizeof(buffer), fmt, amount);

    return std::string(buffer);
}

std::string precise_time_ago(const struct timeval &tv, bool convert_local)
{
    struct timeval now, diff;

    gettimeofday(&now, nullptr);
    if (convert_local) {
        now.tv_sec = convert_log_time_to_local(now.tv_sec);
    }

    timersub(&now, &tv, &diff);
    if (diff.tv_sec < 0) {
        return time_ago(tv.tv_sec);
    } else if (diff.tv_sec <= 1) {
        return "a second ago";
    } else if (diff.tv_sec < (10 * 60)) {
        char buf[64];

        if (diff.tv_sec < 60) {
            snprintf(buf, sizeof(buf),
                     "%2ld seconds ago",
                     diff.tv_sec);
        } else {
            time_t seconds = diff.tv_sec % 60;
            time_t minutes = diff.tv_sec / 60;

            snprintf(buf, sizeof(buf),
                     "%2ld minute%s and %2ld second%s ago",
                     minutes,
                     minutes > 1 ? "s" : "",
                     seconds,
                     seconds == 1 ? "" : "s");
        }

        return std::string(buf);
    } else {
        return time_ago(tv.tv_sec, convert_local);
    }
}

}
}
