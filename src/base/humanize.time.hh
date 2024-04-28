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

#ifndef lnav_humanize_time_hh
#define lnav_humanize_time_hh

#include <optional>
#include <string>

#include <sys/time.h>

namespace humanize {
namespace time {

class point {
public:
    static point from_tv(const struct timeval& tv);

    point& with_recent_point(const struct timeval& tv)
    {
        this->p_recent_point = tv;
        return *this;
    }

    point& with_convert_to_local(bool convert_to_local)
    {
        this->p_convert_to_local = convert_to_local;
        return *this;
    }

    std::string as_time_ago() const;

    std::string as_precise_time_ago() const;

private:
    explicit point(const struct timeval& tv)
        : p_past_point{tv.tv_sec, tv.tv_usec}
    {
    }

    struct timeval p_past_point;
    std::optional<struct timeval> p_recent_point;
    bool p_convert_to_local{false};
};

class duration {
public:
    static duration from_tv(const struct timeval& tv);

    template<class Rep, class Period>
    duration& with_resolution(const std::chrono::duration<Rep, Period>& res)
    {
        this->d_msecs_resolution
            = std::chrono::duration_cast<std::chrono::milliseconds>(res)
                  .count();
        return *this;
    }

    std::string to_string() const;

private:
    explicit duration(const struct timeval& tv) : d_timeval(tv) {}

    struct timeval d_timeval;
    uint64_t d_msecs_resolution{1};
};

}  // namespace time
}  // namespace humanize

#endif
