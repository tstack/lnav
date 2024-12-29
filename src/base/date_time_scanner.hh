/**
 * Copyright (c) 2020, Timothy Stack
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
 * @file date_time_scanner.hh
 */

#ifndef lnav_date_time_scanner_hh
#define lnav_date_time_scanner_hh

#include <ctime>
#include <string>

#include <sys/types.h>

#include "date/tz.h"
#include "time_util.hh"

/**
 * Scans a timestamp string to discover the date-time format using the custom
 * ptimec parser.  Once a format is found, it is locked in so that the next
 * time a timestamp needs to be scanned, the format does not have to be
 * rediscovered.  The discovered date-time format can also be used to convert
 * an exttm struct to a string using the ftime() method.
 */
struct date_time_scanner {
    date_time_scanner() { this->clear(); }

    void clear();

    struct lock_state {
        int ls_fmt_index{-1};
        int ls_fmt_len{-1};
    };

    /**
     * Unlock this scanner so that the format is rediscovered.
     */
    lock_state unlock()
    {
        auto retval = lock_state{this->dts_fmt_lock, this->dts_fmt_len};

        this->dts_fmt_lock = -1;
        this->dts_fmt_len = -1;
        return retval;
    }

    void relock(const lock_state& ls)
    {
        this->dts_fmt_lock = ls.ls_fmt_index;
        this->dts_fmt_len = ls.ls_fmt_len;
    }

    void set_base_time(time_t base_time, const tm& local_tm);

    /**
     * Convert a timestamp to local time.
     *
     * Calling localtime_r is slow since it wants to lookup the timezone on
     * every call, so we cache the result and only call it again if the
     * requested time falls outside of a fifteen minute range.
     */
    void to_localtime(time_t t, struct exttm& tm_out);

    bool dts_keep_base_tz{false};
    bool dts_local_time{false};
    bool dts_zoned_to_local{true};
    time_t dts_base_time{0};
    struct exttm dts_base_tm;
    int dts_fmt_lock{-1};
    int dts_fmt_len{-1};
    struct exttm dts_last_tm {};
    struct timeval dts_last_tv {};
    time_t dts_local_offset_cache{0};
    time_t dts_local_offset_valid{0};
    time_t dts_local_offset_expiry{0};
    time_t dts_localtime_cached_gmt{0};
    tm dts_localtime_cached_tm{};
    const date::time_zone* dts_default_zone{nullptr};

    static const int EXPIRE_TIME = 15 * 60;

    const char* scan(const char* time_src,
                     size_t time_len,
                     const char* const time_fmt[],
                     struct exttm* tm_out,
                     struct timeval& tv_out,
                     bool convert_local = true);

    size_t ftime(char* dst,
                 size_t len,
                 const char* const time_fmt[],
                 const struct exttm& tm) const;

    bool convert_to_timeval(const char* time_src,
                            ssize_t time_len,
                            const char* const time_fmt[],
                            struct timeval& tv_out)
    {
        struct exttm tm;

        if (time_len == -1) {
            time_len = strlen(time_src);
        }
        if (this->scan(time_src, time_len, time_fmt, &tm, tv_out) != nullptr) {
            return true;
        }
        return false;
    }

    bool convert_to_timeval(const std::string& time_src, struct timeval& tv_out)
    {
        struct exttm tm;

        if (this->scan(time_src.c_str(), time_src.size(), nullptr, &tm, tv_out)
            != nullptr)
        {
            return true;
        }
        return false;
    }
};

#endif
