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

#include <chrono>
#include <optional>
#include <string>

#include <sys/time.h>

#include "intern_string.hh"
#include "lnav.console.hh"
#include "result.h"

namespace humanize::time {

class point {
public:
    static point from_tv(const timeval& tv);

    static Result<point, lnav::console::user_message> from(
        string_fragment in, std::optional<timeval> ref_point = {});

    timeval get_point() const { return this->p_past_point; }

    point& with_recent_point(const timeval& tv)
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
    explicit point(const timeval& tv) : p_past_point{tv.tv_sec, tv.tv_usec} {}

    timeval p_past_point;
    std::optional<timeval> p_recent_point;
    bool p_convert_to_local{false};
};

class duration {
public:
    // Accept any chrono::duration source and canonicalize to ns.
    // Lets callers pass e.g. `std::chrono::nanoseconds`, `microseconds`,
    // or a `steady_clock::duration` directly.
    template<class Rep, class Period>
    static duration from(const std::chrono::duration<Rep, Period>& d)
    {
        return duration{
            std::chrono::duration_cast<std::chrono::nanoseconds>(d)};
    }

    duration& with_compact(bool compact)
    {
        this->d_compact = compact;
        return *this;
    }

    // The smallest unit `to_string()` will render.  Sub-microsecond
    // values (below `1us`) always print as `Nns` regardless of this;
    // values in `[1us, 1ms)` print as `Nus`; for `>= 1ms`, the
    // resolution is interpreted in milliseconds (anything finer than
    // 1ms collapses to 1ms in the segmented hh/mm/ss/ms output).
    template<class Rep, class Period>
    duration& with_resolution(const std::chrono::duration<Rep, Period>& res)
    {
        this->d_resolution
            = std::chrono::duration_cast<std::chrono::nanoseconds>(res);
        return *this;
    }

    [[nodiscard]] std::string to_string() const;

private:
    explicit duration(std::chrono::nanoseconds d) : d_nsecs(d) {}

    std::chrono::nanoseconds d_nsecs{0};
    std::chrono::nanoseconds d_resolution{std::chrono::milliseconds{1}};
    bool d_compact{true};
};

}  // namespace humanize::time

#endif
