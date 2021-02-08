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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file date_time_scanner.hh
 */

#ifndef lnav_date_time_scanner_hh
#define lnav_date_time_scanner_hh

#include <time.h>
#include <sys/types.h>

#include <string>

#include "time_util.hh"

struct date_time_scanner {
    date_time_scanner() : dts_keep_base_tz(false),
                          dts_local_time(false),
                          dts_local_offset_cache(0),
                          dts_local_offset_valid(0),
                          dts_local_offset_expiry(0) {
        this->clear();
    };

    void clear() {
        this->dts_base_time = 0;
        memset(&this->dts_base_tm, 0, sizeof(this->dts_base_tm));
        this->dts_fmt_lock = -1;
        this->dts_fmt_len = -1;
    };

    void unlock() {
        this->dts_fmt_lock = -1;
        this->dts_fmt_len = -1;
    }

    void set_base_time(time_t base_time) {
        this->dts_base_time = base_time;
        localtime_r(&base_time, &this->dts_base_tm.et_tm);
    };

    /**
     * Convert a timestamp to local time.
     *
     * Calling localtime_r is slow since it wants to lookup the timezone on
     * every call, so we cache the result and only call it again if the
     * requested time falls outside of a fifteen minute range.
     */
    void to_localtime(time_t t, struct exttm &tm_out) {
        if (t < (24 * 60 * 60)) {
            // Don't convert and risk going past the epoch.
            return;
        }

        if (t < this->dts_local_offset_valid ||
            t >= this->dts_local_offset_expiry) {
            time_t new_gmt;

            localtime_r(&t, &tm_out.et_tm);
#ifdef HAVE_STRUCT_TM_TM_ZONE
            tm_out.et_tm.tm_zone = nullptr;
#endif
            tm_out.et_tm.tm_isdst = 0;

            new_gmt = tm2sec(&tm_out.et_tm);
            this->dts_local_offset_cache = t - new_gmt;
            this->dts_local_offset_valid = t;
            this->dts_local_offset_expiry = t + (EXPIRE_TIME - 1);
            this->dts_local_offset_expiry -=
                this->dts_local_offset_expiry % EXPIRE_TIME;
        }
        else {
            time_t adjust_gmt = t - this->dts_local_offset_cache;
            gmtime_r(&adjust_gmt, &tm_out.et_tm);
        }
    };

    bool dts_keep_base_tz;
    bool dts_local_time;
    time_t dts_base_time;
    struct exttm dts_base_tm;
    int dts_fmt_lock;
    int dts_fmt_len;
    time_t dts_local_offset_cache;
    time_t dts_local_offset_valid;
    time_t dts_local_offset_expiry;

    static const int EXPIRE_TIME = 15 * 60;

    const char *scan(const char *time_src,
                     size_t time_len,
                     const char * const time_fmt[],
                     struct exttm *tm_out,
                     struct timeval &tv_out,
                     bool convert_local = true);

    size_t ftime(char *dst, size_t len, const struct exttm &tm);;

    bool convert_to_timeval(const char *time_src,
                            ssize_t time_len,
                            const char * const time_fmt[],
                            struct timeval &tv_out) {
        struct exttm tm;

        if (time_len == -1) {
            time_len = strlen(time_src);
        }
        if (this->scan(time_src, time_len, time_fmt, &tm, tv_out) != nullptr) {
            return true;
        }
        return false;
    };

    bool convert_to_timeval(const std::string &time_src,
                            struct timeval &tv_out) {
        struct exttm tm;

        if (this->scan(time_src.c_str(), time_src.size(),
                       nullptr, &tm, tv_out) != nullptr) {
            return true;
        }
        return false;
    }
};

#endif
