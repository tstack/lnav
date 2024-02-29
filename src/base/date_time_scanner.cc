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
 * @file date_time_scanner.cc
 */

#include <chrono>

#include "date_time_scanner.hh"

#include "config.h"
#include "date_time_scanner.cfg.hh"
#include "injector.hh"
#include "ptimec.hh"
#include "scn/scn.h"

size_t
date_time_scanner::ftime(char* dst,
                         size_t len,
                         const char* const time_fmt[],
                         const exttm& tm) const
{
    off_t off = 0;

    if (time_fmt == nullptr) {
        PTIMEC_FORMATS[this->dts_fmt_lock].pf_ffunc(dst, off, len, tm);
        if (tm.et_flags & ETF_SUB_NOT_IN_FORMAT) {
            if (tm.et_flags & ETF_MILLIS_SET) {
                dst[off++] = '.';
                ftime_L(dst, off, len, tm);
            } else if (tm.et_flags & ETF_MICROS_SET) {
                dst[off++] = '.';
                ftime_f(dst, off, len, tm);
            } else if (tm.et_flags & ETF_NANOS_SET) {
                dst[off++] = '.';
                ftime_N(dst, off, len, tm);
            }
        }
        dst[off] = '\0';
    } else {
        off = ftime_fmt(dst, len, time_fmt[this->dts_fmt_lock], tm);
    }

    return (size_t) off;
}

bool
next_format(const char* const fmt[], int& index, int& locked_index)
{
    bool retval = true;

    if (locked_index == -1) {
        index += 1;
        if (fmt[index] == nullptr) {
            retval = false;
        }
    } else if (index == locked_index) {
        retval = false;
    } else {
        index = locked_index;
    }

    return retval;
}

const char*
date_time_scanner::scan(const char* time_dest,
                        size_t time_len,
                        const char* const time_fmt[],
                        struct exttm* tm_out,
                        struct timeval& tv_out,
                        bool convert_local)
{
    static const auto& cfg
        = injector::get<const date_time_scanner_ns::config&>();

    int curr_time_fmt = -1;
    bool found = false;
    const char* retval = nullptr;

    if (!time_fmt) {
        time_fmt = PTIMEC_FORMAT_STR;
    }

    this->dts_zoned_to_local = cfg.c_zoned_to_local;
    while (next_format(time_fmt, curr_time_fmt, this->dts_fmt_lock)) {
        *tm_out = this->dts_base_tm;
        tm_out->et_flags = 0;
        if (time_len > 1 && time_dest[0] == '+' && isdigit(time_dest[1])) {
            retval = nullptr;
            auto sv = scn::string_view{time_dest, time_len};
            auto epoch_scan_res = scn::scan_value<int64_t>(sv);
            if (epoch_scan_res) {
                time_t gmt = epoch_scan_res.value();

                if (convert_local
                    && (this->dts_local_time || this->dts_zoned_to_local))
                {
                    localtime_r(&gmt, &tm_out->et_tm);
#ifdef HAVE_STRUCT_TM_TM_ZONE
                    tm_out->et_tm.tm_zone = nullptr;
#endif
                    tm_out->et_tm.tm_isdst = 0;
                    gmt = tm_out->to_timeval().tv_sec;
                }
                tv_out.tv_sec = gmt;
                tv_out.tv_usec = 0;
                tm_out->et_flags = ETF_DAY_SET | ETF_MONTH_SET | ETF_YEAR_SET
                    | ETF_MACHINE_ORIENTED | ETF_EPOCH_TIME | ETF_ZONE_SET;

                this->dts_fmt_lock = curr_time_fmt;
                this->dts_fmt_len = sv.length() - epoch_scan_res.range().size();
                retval = time_dest + this->dts_fmt_len;
                found = true;
                break;
            }
        } else if (time_fmt == PTIMEC_FORMAT_STR) {
            ptime_func func = PTIMEC_FORMATS[curr_time_fmt].pf_func;
            off_t off = 0;

#ifdef HAVE_STRUCT_TM_TM_ZONE
            if (!this->dts_keep_base_tz) {
                tm_out->et_tm.tm_zone = nullptr;
            }
#endif
            if (func(tm_out, time_dest, off, time_len)) {
                retval = &time_dest[off];

                if (tm_out->et_tm.tm_year < 70) {
                    tm_out->et_tm.tm_year = 80;
                }
                if (convert_local
                    && (this->dts_local_time
                        || tm_out->et_flags & ETF_EPOCH_TIME
                        || (tm_out->et_flags & ETF_ZONE_SET
                            && this->dts_zoned_to_local)))
                {
                    time_t gmt = tm_out->to_timeval().tv_sec;

                    this->to_localtime(gmt, *tm_out);
                }
                const auto& last_tm = this->dts_last_tm.et_tm;
                if (last_tm.tm_year == tm_out->et_tm.tm_year
                    && last_tm.tm_mon == tm_out->et_tm.tm_mon
                    && last_tm.tm_mday == tm_out->et_tm.tm_mday
                    && last_tm.tm_hour == tm_out->et_tm.tm_hour
                    && last_tm.tm_min == tm_out->et_tm.tm_min)
                {
                    const auto sec_diff = tm_out->et_tm.tm_sec - last_tm.tm_sec;

                    // log_debug("diff %d", sec_diff);
                    tv_out = this->dts_last_tv;
                    tv_out.tv_sec += sec_diff;
                    tm_out->et_tm.tm_wday = last_tm.tm_wday;
                } else {
                    tv_out = tm_out->to_timeval();
                    secs2wday(tv_out, &tm_out->et_tm);
                }
                tv_out.tv_usec = tm_out->et_nsec / 1000;

                this->dts_fmt_lock = curr_time_fmt;
                this->dts_fmt_len = retval - time_dest;

                found = true;
                break;
            }
        } else {
            off_t off = 0;

#ifdef HAVE_STRUCT_TM_TM_ZONE
            if (!this->dts_keep_base_tz) {
                tm_out->et_tm.tm_zone = nullptr;
            }
#endif
            if (ptime_fmt(
                    time_fmt[curr_time_fmt], tm_out, time_dest, off, time_len)
                && (time_dest[off] == '.' || time_dest[off] == ','
                    || off == (off_t) time_len))
            {
                retval = &time_dest[off];
                if (tm_out->et_tm.tm_year < 70) {
                    tm_out->et_tm.tm_year = 80;
                }
                if (convert_local
                    && (this->dts_local_time
                        || tm_out->et_flags & ETF_EPOCH_TIME
                        || (tm_out->et_flags & ETF_ZONE_SET
                            && this->dts_zoned_to_local)))
                {
                    time_t gmt = tm_out->to_timeval().tv_sec;

                    this->to_localtime(gmt, *tm_out);
#ifdef HAVE_STRUCT_TM_TM_ZONE
                    tm_out->et_tm.tm_zone = nullptr;
#endif
                    tm_out->et_tm.tm_isdst = 0;
                }

                tv_out = tm_out->to_timeval();
                secs2wday(tv_out, &tm_out->et_tm);

                this->dts_fmt_lock = curr_time_fmt;
                this->dts_fmt_len = retval - time_dest;

                found = true;
                break;
            }
        }
    }

    if (!found) {
        retval = nullptr;
    }

    if (retval != nullptr) {
        this->dts_last_tm = *tm_out;
        this->dts_last_tv = tv_out;
    }

    if (retval != nullptr && static_cast<size_t>(retval - time_dest) < time_len)
    {
        /* Try to pull out the milli/micro-second value. */
        if (!(tm_out->et_flags
              & (ETF_MILLIS_SET | ETF_MICROS_SET | ETF_NANOS_SET))
            && (retval[0] == '.' || retval[0] == ','))
        {
            off_t off = (retval - time_dest) + 1;

            if (ptime_N(tm_out, time_dest, off, time_len)) {
                tv_out.tv_usec
                    = std::chrono::duration_cast<std::chrono::microseconds>(
                          std::chrono::nanoseconds{tm_out->et_nsec})
                          .count();
                this->dts_fmt_len += 10;
                tm_out->et_flags |= ETF_NANOS_SET | ETF_SUB_NOT_IN_FORMAT;
                retval += 10;
            } else if (ptime_f(tm_out, time_dest, off, time_len)) {
                tv_out.tv_usec
                    = std::chrono::duration_cast<std::chrono::microseconds>(
                          std::chrono::nanoseconds{tm_out->et_nsec})
                          .count();
                this->dts_fmt_len += 7;
                tm_out->et_flags |= ETF_MICROS_SET | ETF_SUB_NOT_IN_FORMAT;
                retval += 7;
            } else if (ptime_L(tm_out, time_dest, off, time_len)) {
                tv_out.tv_usec
                    = std::chrono::duration_cast<std::chrono::microseconds>(
                          std::chrono::nanoseconds{tm_out->et_nsec})
                          .count();
                this->dts_fmt_len += 4;
                tm_out->et_flags |= ETF_MILLIS_SET | ETF_SUB_NOT_IN_FORMAT;
                retval += 4;
            }
        }
    }

    return retval;
}

void
date_time_scanner::set_base_time(time_t base_time, const tm& local_tm)
{
    this->dts_base_time = base_time;
    this->dts_base_tm.et_tm = local_tm;
    this->dts_last_tm = exttm{};
    this->dts_last_tv = timeval{};
}

void
date_time_scanner::to_localtime(time_t t, exttm& tm_out)
{
    if (t < (24 * 60 * 60)) {
        // Don't convert and risk going past the epoch.
        return;
    }

    if (t < this->dts_local_offset_valid || t >= this->dts_local_offset_expiry)
    {
        time_t new_gmt;

        localtime_r(&t, &tm_out.et_tm);
        // Clear the gmtoff set by localtime_r() otherwise tm2sec() will
        // convert the time back again.
#ifdef HAVE_STRUCT_TM_TM_ZONE
        tm_out.et_tm.tm_gmtoff = 0;
        tm_out.et_tm.tm_zone = nullptr;
#endif
        tm_out.et_tm.tm_isdst = 0;

        new_gmt = tm2sec(&tm_out.et_tm);
        this->dts_local_offset_cache = t - new_gmt;
        this->dts_local_offset_valid = t;
        this->dts_local_offset_expiry = t + (EXPIRE_TIME - 1);
        this->dts_local_offset_expiry
            -= this->dts_local_offset_expiry % EXPIRE_TIME;
    } else {
        time_t adjust_gmt = t - this->dts_local_offset_cache;
        gmtime_r(&adjust_gmt, &tm_out.et_tm);
    }
    tm_out.et_gmtoff = 0;
#ifdef HAVE_STRUCT_TM_TM_ZONE
    tm_out.et_tm.tm_gmtoff = 0;
    tm_out.et_tm.tm_zone = nullptr;
#endif
}
