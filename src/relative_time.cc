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
    { "time",
        pcrepp("\\A(\\d{1,2}):(\\d{2})(?::(\\d{2})(?:\\.(\\d{3,6}))?)?") },
    { "num", pcrepp("\\A((?:-|\\+)?\\d+)") },
    { "us", pcrepp("\\A(?:micros(?:econds?)?|us(?![a-zA-Z]))") },
    { "ms", pcrepp("\\A(?:millis(?:econds?)?|ms(?![a-zA-Z]))") },
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
    { "the", pcrepp("\\Athe\\b") },
    { "ago", pcrepp("\\Aago\\b") },
    { "lter", pcrepp("\\Alater\\b") },
    { "bfor", pcrepp("\\Abefore\\b") },
    { "aft", pcrepp("\\Aafter\\b") },
    { "now", pcrepp("\\Anow\\b") },
    { "here", pcrepp("\\Ahere\\b") },
    { "next", pcrepp("\\Anext\\b") },
    { "previous", pcrepp("\\A(?:previous\\b|last\\b)") },
};

static int64_t TIME_SCALES[] = {
        1000 * 1000,
        60,
        60,
        24,
};

const char relative_time::FIELD_CHARS[] = {
    'u',
    's',
    'm',
    'h',
    'd',
    'M',
    'y',
};

bool relative_time::parse(const char *str, size_t len, struct parse_error &pe_out)
{
    pcre_input pi(str, 0, len);
    pcre_context_static<30> pc;
    int64_t number = 0;
    bool number_set = false, number_was_set = false;
    bool next_set = false;
    token_t base_token = RTT_INVALID;
    rt_field_type last_field_type = RTF__MAX;

    pe_out.pe_column = -1;
    pe_out.pe_msg.clear();

    while (true) {
        rt_field_type curr_field_type = RTF__MAX;

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
                    if (base_token != RTT_INVALID) {
                        base_token = RTT_INVALID;
                        this->rt_absolute_field_end = RTF__MAX;
                        continue;
                    }
                    if (!this->rt_next && !this->rt_previous) {
                        pe_out.pe_msg = "Expecting a number before time unit";
                        return false;
                    }
                }
                number_was_set = number_set;
                number_set = false;
            }
            switch (token) {
                case RTT_YESTERDAY:
                case RTT_TODAY:
                case RTT_NOW: {
                    struct timeval tv;
                    struct exttm tm;

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
                            this->rt_field[RTF_DAYS].value -= 1;
                        case RTT_TODAY:
                            this->rt_field[RTF_HOURS] = 0;
                            this->rt_field[RTF_MINUTES] = 0;
                            this->rt_field[RTF_SECONDS] = 0;
                            this->rt_field[RTF_MICROSECONDS] = 0;
                            break;
                        default:
                            break;
                    }
                    this->rt_absolute_field_end = RTF__MAX;
                    break;
                }
                case RTT_INVALID:
                case RTT_WHITE:
                case RTT_AND:
                case RTT_THE:
                    curr_field_type = last_field_type;
                    break;
                case RTT_AM:
                case RTT_PM:
                    if (number_set) {
                        this->rt_field[RTF_HOURS] = number;
                        this->rt_field[RTF_MINUTES] = 0;
                        this->rt_field[RTF_SECONDS] = 0;
                        this->rt_field[RTF_MICROSECONDS] = 0;
                        this->rt_absolute_field_end = RTF__MAX;
                        number_set = false;
                    }
                    if (!this->is_absolute(RTF_YEARS)) {
                        pe_out.pe_msg = "Expecting absolute time with A.M. or P.M.";
                        return false;
                    }
                    if (token == RTT_AM) {
                        if (this->rt_field[RTF_HOURS].value == 12) {
                            this->rt_field[RTF_HOURS] = 0;
                        }
                    }
                    else if (this->rt_field[RTF_HOURS].value < 12) {
                        this->rt_field[RTF_HOURS].value += 12;
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
                    this->rt_field[RTF_MINUTES] = atoi(mstr.c_str());
                    if (pc[2]->is_valid()) {
                        string sstr = pi.get_substr(pc[2]);
                        this->rt_field[RTF_SECONDS] = atoi(sstr.c_str());
                        if (pc[3]->is_valid()) {
                            string substr = pi.get_substr(pc[3]);

                            switch (substr.length()) {
                                case 3:
                                    this->rt_field[RTF_MICROSECONDS] =
                                        atoi(substr.c_str()) * 1000;
                                    break;
                                case 6:
                                    this->rt_field[RTF_MICROSECONDS] =
                                        atoi(substr.c_str());
                                    break;
                            }
                        }
                    }
                    else {
                        this->rt_field[RTF_SECONDS] = 0;
                        this->rt_field[RTF_MICROSECONDS] = 0;
                    }
                    this->rt_absolute_field_end = RTF__MAX;
                    break;
                }
                case RTT_NUMBER: {
                    if (number_set) {
                        pe_out.pe_msg = "No time unit given for the previous number";
                        return false;
                    }

                    string numstr = pi.get_substr(pc[0]);

                    if (sscanf(numstr.c_str(), "%" PRId64, &number) != 1) {
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
                    if (number_was_set) {
                        this->rt_field[RTF_SECONDS] = number;
                        curr_field_type = RTF_SECONDS;
                    } else if (next_set) {
                        this->rt_field[RTF_MICROSECONDS] = 0;
                        this->rt_absolute_field_end = RTF__MAX;
                    }
                    break;
                case RTT_MINUTES:
                    if (number_was_set) {
                        this->rt_field[RTF_MINUTES] = number;
                        curr_field_type = RTF_MINUTES;
                    } else if (next_set) {
                        this->rt_field[RTF_MICROSECONDS] = 0;
                        this->rt_field[RTF_SECONDS] = 0;
                        this->rt_absolute_field_end = RTF__MAX;
                    }
                    break;
                case RTT_HOURS:
                    if (number_was_set) {
                        this->rt_field[RTF_HOURS] = number;
                        curr_field_type = RTF_HOURS;
                    } else if (next_set) {
                        this->rt_field[RTF_MICROSECONDS] = 0;
                        this->rt_field[RTF_SECONDS] = 0;
                        this->rt_field[RTF_MINUTES] = 0;
                        this->rt_absolute_field_end = RTF__MAX;
                    }
                    break;
                case RTT_DAYS:
                    if (number_was_set) {
                        this->rt_field[RTF_DAYS] = number;
                        curr_field_type = RTF_DAYS;
                    } else if (next_set) {
                        this->rt_field[RTF_MICROSECONDS] = 0;
                        this->rt_field[RTF_SECONDS] = 0;
                        this->rt_field[RTF_MINUTES] = 0;
                        this->rt_field[RTF_HOURS] = 0;
                        this->rt_absolute_field_end = RTF__MAX;
                    }
                    break;
                case RTT_WEEKS:
                    this->rt_field[RTF_DAYS] = number * 7;
                    break;
                case RTT_MONTHS:
                    if (number_was_set) {
                        this->rt_field[RTF_MONTHS] = number;
                        curr_field_type = RTF_MONTHS;
                    } else if (next_set) {
                        this->rt_field[RTF_MICROSECONDS] = 0;
                        this->rt_field[RTF_SECONDS] = 0;
                        this->rt_field[RTF_MINUTES] = 0;
                        this->rt_field[RTF_HOURS] = 0;
                        this->rt_field[RTF_DAYS] = 0;
                        this->rt_absolute_field_end = RTF__MAX;
                    }
                    break;
                case RTT_YEARS:
                    if (number_was_set) {
                        this->rt_field[RTF_YEARS] = number;
                        curr_field_type = RTF_YEARS;
                    } else if (next_set) {
                        this->rt_field[RTF_MICROSECONDS] = 0;
                        this->rt_field[RTF_SECONDS] = 0;
                        this->rt_field[RTF_MINUTES] = 0;
                        this->rt_field[RTF_HOURS] = 0;
                        this->rt_field[RTF_DAYS] = 0;
                        this->rt_field[RTF_MONTHS] = 0;
                        this->rt_absolute_field_end = RTF__MAX;
                    }
                    break;
                case RTT_BEFORE:
                case RTT_AGO:
                    if (this->empty()) {
                        pe_out.pe_msg = "Expecting a time unit";
                        return false;
                    }
                    for (int field = 0; field < RTF__MAX; field++) {
                        if (this->rt_field[field].value > 0) {
                            this->rt_field[field] = -this->rt_field[field].value;
                        }
                        if (last_field_type != RTF__MAX && field < last_field_type) {
                            this->rt_field[field] = 0;
                        }
                    }
                    if (last_field_type != RTF__MAX) {
                        this->rt_absolute_field_end = last_field_type;
                    }
                    break;
                case RTT_AFTER:
                    base_token = token;
                    break;
                case RTT_LATER:
                    if (this->empty()) {
                        pe_out.pe_msg = "Expecting a time unit before 'later'";
                        return false;
                    }
                    break;
                case RTT_HERE:
                    break;
                case RTT_NEXT:
                    this->rt_next = true;
                    next_set = true;
                    break;
                case RTT_PREVIOUS:
                    this->rt_previous = true;
                    next_set = true;
                    break;
                case RTT_TOMORROW:
                    this->rt_field[RTF_DAYS] = 1;
                    break;
                case RTT_NOON:
                    this->rt_field[RTF_HOURS] = 12;
                    this->rt_absolute_field_end = RTF__MAX;
                    for (int lpc2 = RTF_MICROSECONDS;
                         lpc2 < RTF_HOURS;
                         lpc2++) {
                        this->rt_field[lpc2] = 0;
                    }
                    break;

                case RTT__MAX:
                    assert(false);
                    break;
            }

            if (token != RTT_NEXT &&
                token != RTT_PREVIOUS &&
                token != RTT_WHITE) {
                next_set = false;
            }

            number_was_set = false;
        }

        if (!found) {
            pe_out.pe_msg = "Unrecognized input";
            return false;
        }

        last_field_type = curr_field_type;
    }
}

void relative_time::rollover()
{
    for (int lpc = 0; lpc < RTF_DAYS; lpc++) {
        if (!this->rt_field[lpc].is_set) {
            continue;
        }
        int64_t val = this->rt_field[lpc].value;
        this->rt_field[lpc] = val % TIME_SCALES[lpc];
        this->rt_field[lpc + 1].value += val / TIME_SCALES[lpc];
    }
    if (std::abs(this->rt_field[RTF_DAYS].value) > 31) {
        int64_t val = this->rt_field[RTF_DAYS].value;
        this->rt_field[RTF_DAYS] = val % 31;
        this->rt_field[RTF_MONTHS].value += val / 31;
    }
    if (std::abs(this->rt_field[RTF_MONTHS].value) > 12) {
        int64_t val = this->rt_field[RTF_MONTHS].value;
        this->rt_field[RTF_MONTHS] = val % 12;
        this->rt_field[RTF_YEARS].value += val / 12;
    }
}

std::string relative_time::to_string()
{
    char dst[128] = "";
    char *pos = dst;

    if (this->is_absolute()) {
        pos += snprintf(pos, sizeof(dst) - (pos - dst),
                        "%s",
                        this->rt_next ? "next " :
                        (this->rt_previous ? "last " : ""));
        if (this->rt_field[RTF_YEARS].is_set &&
            (this->rt_next || this->rt_previous ||
             this->rt_field[RTF_YEARS].value != 0)) {
            pos += snprintf(pos, sizeof(dst) - (pos - dst),
                            "year %" PRId64 " ",
                            this->rt_field[RTF_YEARS].value);
        } else if ((this->rt_next || this->rt_previous) &&
                   this->rt_field[RTF_MONTHS].is_set) {
            pos += snprintf(pos, sizeof(dst) - (pos - dst), "year ");
        }
        if (this->rt_field[RTF_MONTHS].is_set &&
            (this->rt_next || this->rt_previous ||
             this->rt_field[RTF_MONTHS].value != 0)) {
            pos += snprintf(pos, sizeof(dst) - (pos - dst),
                            "month %" PRId64 " ",
                            this->rt_field[RTF_MONTHS].value);
        } else if ((this->rt_next || this->rt_previous) &&
                   this->rt_field[RTF_DAYS].is_set) {
            pos += snprintf(pos, sizeof(dst) - (pos - dst), "month ");
        }
        if (this->rt_field[RTF_DAYS].is_set &&
            (this->rt_next || this->rt_previous ||
             this->rt_field[RTF_DAYS].value != 0)) {
            pos += snprintf(pos, sizeof(dst) - (pos - dst),
                            "day %" PRId64 " ",
                            this->rt_field[RTF_DAYS].value);
        } else if ((this->rt_next || this->rt_previous) &&
                   this->rt_field[RTF_HOURS].is_set) {
            pos += snprintf(pos, sizeof(dst) - (pos - dst), "day ");
        }
        pos += snprintf(pos, sizeof(dst) - (pos - dst),
                        "%" PRId64 ":%02" PRId64,
                        this->rt_field[RTF_HOURS].value,
                        this->rt_field[RTF_MINUTES].value);
        if (this->rt_field[RTF_SECONDS].is_set &&
            this->rt_field[RTF_SECONDS].value != 0) {
            pos += snprintf(pos, sizeof(dst) - (pos - dst),
                            ":%.02" PRId64,
                            this->rt_field[RTF_SECONDS].value);
            if (this->rt_field[RTF_MICROSECONDS].is_set &&
                this->rt_field[RTF_MICROSECONDS].value != 0) {
                pos += snprintf(pos, sizeof(dst) - (pos - dst),
                                ".%.03" PRId64,
                                this->rt_field[RTF_MICROSECONDS].value / 1000);
            }
        }
    } else {
        for (int lpc = RTF__MAX - 1; lpc >= 0; lpc--) {
            if (this->rt_field[lpc].value == 0) {
                continue;
            }
            pos += snprintf(pos, sizeof(dst) - (pos - dst),
                            "%" PRId64 "%c",
                            this->rt_field[lpc].value,
                            FIELD_CHARS[lpc]);
        }
    }

    return dst;
}

size_t str2reltime(int64_t millis, std::string &value_out)
{
    /* 24h22m33s111 */

    static struct rel_interval {
        long long   length;
        const char *format;
        const char *symbol;
    } intervals[] = {
        { 1000, "%03qd%s", ""  },
        {   60, "%qd%s",   "s" },
        {   60, "%qd%s",   "m" },
        {   24, "%qd%s",   "h" },
        {    0, "%qd%s",   "d" },
        {    0, NULL, NULL }
    };

    struct rel_interval *curr_interval = intervals;
    size_t in_len = value_out.length(), retval = 0;

    if (millis >= (10 * 60 * 1000)) {
        millis /= 1000;
        curr_interval += 1;
    }

    for (; curr_interval->symbol != NULL; curr_interval++) {
        long long amount;
        char      segment[32];

        if (curr_interval->length) {
            amount = millis % curr_interval->length;
            millis = millis / curr_interval->length;
        }
        else {
            amount = millis;
            millis = 0;
        }

        if (!amount && !millis) {
            break;
        }

        snprintf(segment, sizeof(segment), curr_interval->format, amount,
                 curr_interval->symbol);
        retval += strlen(segment);
        value_out.insert(in_len, segment);
    }

    return retval;
}

