/**
 * Copyright (c) 2015, Timothy Stack
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

#include <assert.h>

#include <cstdlib>

#include "pcrepp.hh"
#include "lnav_util.hh"
#include "relative_time.hh"

using namespace std;

static struct {
    const char *name;
    pcrepp pcre;
} MATCHERS[relative_time::RTT__MAX] = {
    { "ws", pcrepp("\\A\\s+\\b") },
    { "am", pcrepp("\\Aam|a\\.m\\.\\b") },
    { "pm", pcrepp("\\Apm|p\\.m\\.\\b") },
    { "a", pcrepp("\\Aa\\b") },
    { "an", pcrepp("\\Aan\\b") },
    { "at", pcrepp("\\Aat\\b") },
    { "time", pcrepp("\\A(\\d{1,2}):(\\d{2})(?::(\\d{2}))?") },
    { "num", pcrepp("\\A((?:-|\\+)?\\d+)") },
    { "us", pcrepp("\\Amicros(?:econds?)?|us(?![a-zA-Z])") },
    { "ms", pcrepp("\\Amillis(?:econds?)?|ms(?![a-zA-Z])") },
    { "sec", pcrepp("\\As(?:ec(?:onds?)?)?(?![a-zA-Z])") },
    { "min", pcrepp("\\Am(?:in(?:utes?)?)?(?![a-zA-Z])") },
    { "h", pcrepp("\\Ah(?:ours?)?(?![a-zA-Z])") },
    { "day", pcrepp("\\Ad(?:ays?)?(?![a-zA-Z])") },
    { "week", pcrepp("\\Aw(?:eeks?)?(?![a-zA-Z])") },
    { "mon", pcrepp("\\Amon(?:ths?)?(?![a-zA-Z])") },
    { "year", pcrepp("\\Ay(?:ears?)?(?![a-zA-Z])") },
    { "today", pcrepp("\\Atoday\\b") },
    { "yest", pcrepp("\\Ayesterday\\b") },
    { "tomo", pcrepp("\\Atomorrow\\b") },
    { "noon", pcrepp("\\Anoon\\b") },
    { "and", pcrepp("\\Aand\\b") },
    { "ago", pcrepp("\\Aago\\b") },
    { "lter", pcrepp("\\Alater\\b") },
    { "bfor", pcrepp("\\Abefore\\b") },
    { "now", pcrepp("\\Anow\\b") },
    { "here", pcrepp("\\Ahere\\b") },
};

static int64_t TIME_SCALES[] = {
        1000 * 1000,
        60,
        60,
        24,
};

bool relative_time::parse(const char *str, size_t len, struct parse_error &pe_out)
{
    pcre_input pi(str, 0, len);
    pcre_context_static<30> pc;
    int64_t number = 0;
    bool number_set = false;

    pe_out.pe_column = -1;
    pe_out.pe_msg.clear();

    while (true) {
        if (pi.pi_next_offset >= pi.pi_length) {
            if (number_set) {
                pe_out.pe_msg = "Number given without a time unit";
                return false;
            }

            this->rollover();
            return true;
        }

        bool found = false;
        for (int lpc = 0; lpc < RTT__MAX && !found; lpc++) {
            token_t token = (token_t) lpc;
            if (!MATCHERS[lpc].pcre.match(pc, pi, PCRE_ANCHORED)) {
                continue;
            }

            pe_out.pe_column = pc.all()->c_begin;
            found = true;
            if (RTT_MICROS <= token && token <= RTT_YEARS) {
                if (!number_set) {
                    pe_out.pe_msg = "Expecting a number before time unit";
                    return false;
                }
                number_set = false;
            }
            switch (token) {
                case RTT_YESTERDAY:
                case RTT_TODAY:
                case RTT_NOW: {
                    struct timeval tv;
                    struct exttm tm;
                    int abs_start = 0, abs_end = RTF__MAX;

                    gettimeofday(&tv, NULL);
                    localtime_r(&tv.tv_sec, &tm.et_tm);
                    tm.et_nsec = tv.tv_usec * 1000;
                    this->add(tm);

                    this->rt_field[RTF_YEARS] = tm.et_tm.tm_year;
                    this->rt_field[RTF_MONTHS] = tm.et_tm.tm_mon;
                    this->rt_field[RTF_DAYS] = tm.et_tm.tm_mday;
                    switch (token) {
                        case RTT_NOW:
                            this->rt_field[RTF_HOURS] = tm.et_tm.tm_hour;
                            this->rt_field[RTF_MINUTES] = tm.et_tm.tm_min;
                            this->rt_field[RTF_SECONDS] = tm.et_tm.tm_sec;
                            this->rt_field[RTF_MICROSECONDS] = tm.et_nsec / 1000;
                            break;
                        case RTT_YESTERDAY:
                            this->rt_field[RTF_DAYS] -= 1;
                        case RTT_TODAY:
                            this->rt_field[RTF_HOURS] = 0;
                            this->rt_field[RTF_MINUTES] = 0;
                            this->rt_field[RTF_SECONDS] = 0;
                            this->rt_field[RTF_MICROSECONDS] = 0;
                            break;
                        default:
                            break;
                    }
                    for (int lpc = abs_start; lpc <= abs_end; lpc++) {
                        this->rt_is_absolute[lpc] = true;
                    }
                    break;
                }
                case RTT_INVALID:
                case RTT_WHITE:
                case RTT_AND:
                    break;
                case RTT_AM:
                case RTT_PM:
                    if (number_set) {
                        this->rt_field[RTF_HOURS] = number;
                        this->rt_is_absolute[RTF_HOURS] = true;
                        this->rt_field[RTF_MINUTES] = 0;
                        this->rt_is_absolute[RTF_MINUTES] = true;
                        this->rt_field[RTF_SECONDS] = 0;
                        this->rt_is_absolute[RTF_SECONDS] = true;
                        this->rt_field[RTF_MICROSECONDS] = 0;
                        this->rt_is_absolute[RTF_MICROSECONDS] = true;
                        number_set = false;
                    }
                    if (!this->rt_is_absolute[RTF_HOURS]) {
                        pe_out.pe_msg = "Expecting absolute time with A.M. or P.M.";
                        return false;
                    }
                    if (token == RTT_AM) {
                        if (this->rt_field[RTF_HOURS] == 12) {
                            this->rt_field[RTF_HOURS] = 0;
                        }
                    }
                    else {
                        this->rt_field[RTF_HOURS] += 12;
                    }
                    break;
                case RTT_A:
                case RTT_AN:
                    number = 1;
                    number_set = true;
                    break;
                case RTT_AT:
                    break;
                case RTT_TIME: {
                    string hstr = pi.get_substr(pc[0]);
                    string mstr = pi.get_substr(pc[1]);
                    this->rt_field[RTF_HOURS] = atoi(hstr.c_str());
                    this->rt_is_absolute[RTF_HOURS] = true;
                    this->rt_field[RTF_MINUTES] = atoi(mstr.c_str());
                    this->rt_is_absolute[RTF_MINUTES] = true;
                    if (pc[2]->is_valid()) {
                        string sstr = pi.get_substr(pc[2]);
                        this->rt_field[RTF_SECONDS] = atoi(sstr.c_str());
                    }
                    else {
                        this->rt_field[RTF_SECONDS] = 0;
                    }
                    this->rt_is_absolute[RTF_SECONDS] = true;
                    this->rt_field[RTF_MICROSECONDS] = 0;
                    this->rt_is_absolute[RTF_MICROSECONDS] = true;
                    break;
                }
                case RTT_NUMBER: {
                    if (number_set) {
                        pe_out.pe_msg = "No time unit given for the previous number";
                        return false;
                    }

                    string numstr = pi.get_substr(pc[0]);

                    if (sscanf(numstr.c_str(), "%qd", &number) != 1) {
                        pe_out.pe_msg = "Invalid number: " + numstr;
                        return false;
                    }
                    number_set = true;
                    break;
                }
                case RTT_MICROS:
                    this->rt_field[RTF_MICROSECONDS] = number;
                    break;
                case RTT_MILLIS:
                    this->rt_field[RTF_MICROSECONDS] = number * 1000;
                    break;
                case RTT_SECONDS:
                    this->rt_field[RTF_SECONDS] = number;
                    break;
                case RTT_MINUTES:
                    this->rt_field[RTF_MINUTES] = number;
                    break;
                case RTT_HOURS:
                    this->rt_field[RTF_HOURS] = number;
                    break;
                case RTT_DAYS:
                    this->rt_field[RTF_DAYS] = number;
                    break;
                case RTT_WEEKS:
                    this->rt_field[RTF_DAYS] = number * 7;
                    break;
                case RTT_MONTHS:
                    this->rt_field[RTF_MONTHS] = number;
                    break;
                case RTT_YEARS:
                    this->rt_field[RTF_YEARS] = number;
                    break;
                case RTT_BEFORE:
                case RTT_AGO:
                    if (this->empty()) {
                        pe_out.pe_msg = "Expecting a time unit";
                        return false;
                    }
                    for (int field = 0; field < RTF__MAX; field++) {
                        if (this->rt_field[field] > 0) {
                            this->rt_field[field] = -this->rt_field[field];
                        }
                    }
                    break;
                case RTT_LATER:
                    if (this->empty()) {
                        pe_out.pe_msg = "Expecting a time unit before 'later'";
                        return false;
                    }
                    break;
                case RTT_HERE:
                    break;
                case RTT_TOMORROW:
                    this->rt_field[RTF_DAYS] = 1;
                    break;
                case RTT_NOON:
                    this->rt_field[RTF_HOURS] = 12;
                    this->rt_is_absolute[RTF_HOURS] = true;
                    for (int lpc = RTF_MICROSECONDS; lpc < RTF_HOURS; lpc++) {
                        this->rt_field[lpc] = 0;
                        this->rt_is_absolute[lpc] = true;
                    }
                    break;

                case RTT__MAX:
                    assert(false);
                    break;
            }
        }

        if (!found) {
            pe_out.pe_msg = "Unrecognized input";
            return false;
        }
    }
}

void relative_time::rollover()
{
    for (int lpc = 0; lpc < RTF_DAYS; lpc++) {
        int64_t val = this->rt_field[lpc];
        this->rt_field[lpc] = val % TIME_SCALES[lpc];
        this->rt_field[lpc + 1] += val / TIME_SCALES[lpc];
    }
    if (std::abs(this->rt_field[RTF_DAYS]) > 31) {
        int64_t val = this->rt_field[RTF_DAYS];
        this->rt_field[RTF_DAYS] = val % 31;
        this->rt_field[RTF_MONTHS] += val / 31;
    }
    if (std::abs(this->rt_field[RTF_MONTHS]) > 12) {
        int64_t val = this->rt_field[RTF_MONTHS];
        this->rt_field[RTF_MONTHS] = val % 12;
        this->rt_field[RTF_YEARS] += val / 12;
    }
}
