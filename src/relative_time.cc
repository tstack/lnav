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
#include <unordered_set>

#include "base/time_util.hh"
#include "pcrepp/pcrepp.hh"
#include "relative_time.hh"

using namespace std;
using namespace std::chrono_literals;

static const struct {
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

    { "sun", pcrepp("\\Asun(days?)?\\b") },
    { "mon", pcrepp("\\Amon(days?)?\\b") },
    { "tue", pcrepp("\\Atue(s(days?)?)?\\b") },
    { "wed", pcrepp("\\Awed(nesdays?)?\\b") },
    { "thu", pcrepp("\\Athu(rsdays?)?\\b") },
    { "fri", pcrepp("\\Afri(days?)?\\b") },
    { "sat", pcrepp("\\Asat(urdays?)?\\b") },

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

Result<relative_time, relative_time::parse_error>
relative_time::from_str(const char *str, size_t len)
{
    pcre_input pi(str, 0, len);
    pcre_context_static<30> pc;
    int64_t number = 0;
    bool number_set = false, number_was_set = false;
    bool next_set = false;
    token_t base_token = RTT_INVALID;
    rt_field_type last_field_type = RTF__MAX;
    relative_time retval;
    parse_error pe_out;
    std::unordered_set<token_t> seen_tokens;

    pe_out.pe_column = -1;
    pe_out.pe_msg.clear();

    while (true) {
        rt_field_type curr_field_type = RTF__MAX;

        if (pi.pi_next_offset >= pi.pi_length) {
            if (number_set) {
                if (number > 1970 && number < 2050) {
                    retval.rt_field[RTF_YEARS] = number - 1900;
                    retval.rt_absolute_field_end = RTF__MAX;

                    switch (base_token) {
                        case RTT_BEFORE: {
                            auto epoch = retval.to_timeval();
                            retval.rt_duration =
                                std::chrono::duration_cast<std::chrono::microseconds>(
                                    std::chrono::seconds(epoch.tv_sec)) +
                                    std::chrono::microseconds(epoch.tv_usec);
                            retval.rt_field[RTF_YEARS] = 70;
                            break;
                        }
                        case RTT_AFTER:
                            retval.rt_duration = std::chrono::duration_cast<
                                std::chrono::microseconds>(
                                    std::chrono::hours(24 * 365 * 200));
                            break;
                        default:
                            break;
                    }
                    return Ok(retval);
                }

                pe_out.pe_msg = "Number given without a time unit";
                return Err(pe_out);
            }

            if (base_token != RTT_INVALID) {
                switch (base_token) {
                    case RTT_BEFORE:
                        pe_out.pe_msg = "'before' requires a point in time (e.g. before 10am)";
                        break;
                    case RTT_AFTER:
                        pe_out.pe_msg = "'after' requires a point in time (e.g. after 10am)";
                        break;
                    default:
                        ensure(false);
                        break;
                }
                return Err(pe_out);
            }

            retval.rollover();
            return Ok(retval);
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
                        retval.rt_absolute_field_end = RTF__MAX;
                        continue;
                    }
                    if (!retval.rt_next && !retval.rt_previous) {
                        pe_out.pe_msg = "Expecting a number before time unit";
                        return Err(pe_out);
                    }
                }
                number_was_set = number_set;
                number_set = false;
            }
            switch (token) {
                case RTT_YESTERDAY:
                case RTT_TODAY:
                case RTT_NOW: {
                    if (seen_tokens.count(token) > 0) {
                        pe_out.pe_msg =
                            "Current time reference has already been used";
                        return Err(pe_out);
                    }

                    seen_tokens.insert(RTT_YESTERDAY);
                    seen_tokens.insert(RTT_TODAY);
                    seen_tokens.insert(RTT_NOW);

                    struct timeval tv;
                    struct exttm tm;

                    gettimeofday(&tv, nullptr);
                    localtime_r(&tv.tv_sec, &tm.et_tm);
                    tm.et_nsec = tv.tv_usec * 1000;
                    tm = retval.adjust(tm);

                    retval.rt_field[RTF_YEARS] = tm.et_tm.tm_year;
                    retval.rt_field[RTF_MONTHS] = tm.et_tm.tm_mon;
                    retval.rt_field[RTF_DAYS] = tm.et_tm.tm_mday;
                    switch (token) {
                        case RTT_NOW:
                            retval.rt_field[RTF_HOURS] = tm.et_tm.tm_hour;
                            retval.rt_field[RTF_MINUTES] = tm.et_tm.tm_min;
                            retval.rt_field[RTF_SECONDS] = tm.et_tm.tm_sec;
                            retval.rt_field[RTF_MICROSECONDS] = tm.et_nsec / 1000;
                            break;
                        case RTT_YESTERDAY:
                            retval.rt_field[RTF_DAYS].value -= 1;
                        case RTT_TODAY:
                            retval.rt_field[RTF_HOURS] = 0;
                            retval.rt_field[RTF_MINUTES] = 0;
                            retval.rt_field[RTF_SECONDS] = 0;
                            retval.rt_field[RTF_MICROSECONDS] = 0;
                            break;
                        default:
                            break;
                    }
                    retval.rt_absolute_field_end = RTF__MAX;
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
                    if (seen_tokens.count(token) > 0) {
                        pe_out.pe_msg = "Time has already been set";
                        return Err(pe_out);
                    }
                    seen_tokens.insert(RTT_AM);
                    seen_tokens.insert(RTT_PM);
                    if (number_set) {
                        retval.rt_field[RTF_HOURS] = number;
                        retval.rt_field[RTF_MINUTES] = 0;
                        retval.rt_field[RTF_SECONDS] = 0;
                        retval.rt_field[RTF_MICROSECONDS] = 0;
                        retval.rt_duration = 1min;
                        retval.rt_absolute_field_end = RTF__MAX;
                        number_set = false;
                    }
                    if (!retval.is_absolute(RTF_YEARS)) {
                        pe_out.pe_msg = "Expecting absolute time with A.M. or P.M.";
                        return Err(pe_out);
                    }
                    if (token == RTT_AM) {
                        if (retval.rt_field[RTF_HOURS].value == 12) {
                            retval.rt_field[RTF_HOURS] = 0;
                        }
                    }
                    else if (retval.rt_field[RTF_HOURS].value < 12) {
                        retval.rt_field[RTF_HOURS].value += 12;
                    }
                    if (base_token == RTT_AFTER) {
                        std::chrono::microseconds usecs = 0s;
                        uint64_t carry = 0;

                        if (retval.rt_field[RTF_MICROSECONDS].value > 0) {
                            usecs += std::chrono::microseconds(
                                1000000ULL - retval.rt_field[RTF_MICROSECONDS].value);
                            carry = 1;
                        }
                        if (carry || retval.rt_field[RTF_SECONDS].value > 0) {
                            usecs += std::chrono::seconds(
                                60 - carry - retval.rt_field[RTF_SECONDS].value);
                            carry = 1;
                        }
                        if (carry || retval.rt_field[RTF_MINUTES].value > 0) {
                            usecs += std::chrono::minutes(
                                60 - carry - retval.rt_field[RTF_MINUTES].value);
                            carry = 1;
                        }
                        usecs += std::chrono::hours(
                            24 - retval.rt_field[RTF_HOURS].value);
                        retval.rt_duration = usecs;
                    }
                    if (base_token == RTT_BEFORE) {
                        retval.rt_duration =
                            std::chrono::hours(retval.rt_field[RTF_HOURS].value) +
                            std::chrono::minutes(retval.rt_field[RTF_MINUTES].value) +
                            std::chrono::seconds(retval.rt_field[RTF_SECONDS].value) +
                            std::chrono::microseconds(retval.rt_field[RTF_MICROSECONDS].value);
                        retval.rt_field[RTF_HOURS].value = 0;
                        retval.rt_field[RTF_MINUTES].value = 0;
                        retval.rt_field[RTF_SECONDS].value = 0;
                        retval.rt_field[RTF_MICROSECONDS].value = 0;
                    }
                    base_token = RTT_INVALID;
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
                    retval.rt_field[RTF_HOURS] = atoi(hstr.c_str());
                    retval.rt_field[RTF_MINUTES] = atoi(mstr.c_str());
                    if (pc[2]->is_valid()) {
                        string sstr = pi.get_substr(pc[2]);
                        retval.rt_field[RTF_SECONDS] = atoi(sstr.c_str());
                        if (pc[3]->is_valid()) {
                            string substr = pi.get_substr(pc[3]);

                            switch (substr.length()) {
                                case 3:
                                    retval.rt_field[RTF_MICROSECONDS] =
                                        atoi(substr.c_str()) * 1000;
                                    break;
                                case 6:
                                    retval.rt_field[RTF_MICROSECONDS] =
                                        atoi(substr.c_str());
                                    break;
                            }
                        } else {
                            retval.rt_field[RTF_MICROSECONDS].clear();
                            retval.rt_duration = 1s;
                        }
                    }
                    else {
                        retval.rt_field[RTF_SECONDS].clear();
                        retval.rt_field[RTF_MICROSECONDS].clear();
                        retval.rt_duration = 1min;
                    }
                    retval.rt_absolute_field_end = RTF__MAX;
                    break;
                }
                case RTT_NUMBER: {
                    if (number_set) {
                        pe_out.pe_msg = "No time unit given for the previous number";
                        return Err(pe_out);
                    }

                    string numstr = pi.get_substr(pc[0]);

                    if (sscanf(numstr.c_str(), "%" PRId64, &number) != 1) {
                        pe_out.pe_msg = "Invalid number: " + numstr;
                        return Err(pe_out);
                    }
                    number_set = true;
                    break;
                }
                case RTT_MICROS:
                    retval.rt_field[RTF_MICROSECONDS] = number;
                    break;
                case RTT_MILLIS:
                    retval.rt_field[RTF_MICROSECONDS] = number * 1000;
                    break;
                case RTT_SECONDS:
                    if (number_was_set) {
                        retval.rt_field[RTF_SECONDS] = number;
                        curr_field_type = RTF_SECONDS;
                    } else if (next_set) {
                        retval.rt_field[RTF_MICROSECONDS] = 0;
                        retval.rt_absolute_field_end = RTF__MAX;
                    }
                    break;
                case RTT_MINUTES:
                    if (number_was_set) {
                        retval.rt_field[RTF_MINUTES] = number;
                        curr_field_type = RTF_MINUTES;
                    } else if (next_set) {
                        retval.rt_field[RTF_MICROSECONDS] = 0;
                        retval.rt_field[RTF_SECONDS] = 0;
                        retval.rt_absolute_field_end = RTF__MAX;
                    }
                    break;
                case RTT_HOURS:
                    if (number_was_set) {
                        retval.rt_field[RTF_HOURS] = number;
                        curr_field_type = RTF_HOURS;
                    } else if (next_set) {
                        retval.rt_field[RTF_MICROSECONDS] = 0;
                        retval.rt_field[RTF_SECONDS] = 0;
                        retval.rt_field[RTF_MINUTES] = 0;
                        retval.rt_absolute_field_end = RTF__MAX;
                    }
                    break;
                case RTT_DAYS:
                    if (number_was_set) {
                        retval.rt_field[RTF_DAYS] = number;
                        curr_field_type = RTF_DAYS;
                    } else if (next_set) {
                        retval.rt_field[RTF_MICROSECONDS] = 0;
                        retval.rt_field[RTF_SECONDS] = 0;
                        retval.rt_field[RTF_MINUTES] = 0;
                        retval.rt_field[RTF_HOURS] = 0;
                        retval.rt_absolute_field_end = RTF__MAX;
                    }
                    break;
                case RTT_WEEKS:
                    retval.rt_field[RTF_DAYS] = number * 7;
                    break;
                case RTT_MONTHS:
                    if (number_was_set) {
                        retval.rt_field[RTF_MONTHS] = number;
                        curr_field_type = RTF_MONTHS;
                    } else if (next_set) {
                        retval.rt_field[RTF_MICROSECONDS] = 0;
                        retval.rt_field[RTF_SECONDS] = 0;
                        retval.rt_field[RTF_MINUTES] = 0;
                        retval.rt_field[RTF_HOURS] = 0;
                        retval.rt_field[RTF_DAYS] = 0;
                        retval.rt_absolute_field_end = RTF__MAX;
                    }
                    break;
                case RTT_YEARS:
                    if (number_was_set) {
                        retval.rt_field[RTF_YEARS] = number;
                        curr_field_type = RTF_YEARS;
                    } else if (next_set) {
                        retval.rt_field[RTF_MICROSECONDS] = 0;
                        retval.rt_field[RTF_SECONDS] = 0;
                        retval.rt_field[RTF_MINUTES] = 0;
                        retval.rt_field[RTF_HOURS] = 0;
                        retval.rt_field[RTF_DAYS] = 0;
                        retval.rt_field[RTF_MONTHS] = 0;
                        retval.rt_absolute_field_end = RTF__MAX;
                    }
                    break;
                case RTT_AGO:
                    if (retval.empty()) {
                        pe_out.pe_msg = "Expecting a time unit";
                        return Err(pe_out);
                    }
                    for (int field = 0; field < RTF__MAX; field++) {
                        if (retval.rt_field[field].value > 0) {
                            retval.rt_field[field] = -retval.rt_field[field].value;
                        }
                        if (last_field_type != RTF__MAX && field < last_field_type) {
                            retval.rt_field[field] = 0;
                        }
                    }
                    if (last_field_type != RTF__MAX) {
                        retval.rt_absolute_field_end = last_field_type;
                    }
                    break;
                case RTT_BEFORE:
                case RTT_AFTER:
                    if (base_token != RTT_INVALID) {
                        pe_out.pe_msg = "Before/after ranges are not supported yet";
                        return Err(pe_out);
                    }
                    base_token = token;
                    break;
                case RTT_LATER:
                    if (retval.empty()) {
                        pe_out.pe_msg = "Expecting a time unit before 'later'";
                        return Err(pe_out);
                    }
                    break;
                case RTT_HERE:
                    break;
                case RTT_NEXT:
                    retval.rt_next = true;
                    next_set = true;
                    break;
                case RTT_PREVIOUS:
                    retval.rt_previous = true;
                    next_set = true;
                    break;
                case RTT_TOMORROW:
                    retval.rt_field[RTF_DAYS] = 1;
                    break;
                case RTT_NOON:
                    retval.rt_field[RTF_HOURS] = 12;
                    retval.rt_absolute_field_end = RTF__MAX;
                    for (int lpc2 = RTF_MICROSECONDS;
                         lpc2 < RTF_HOURS;
                         lpc2++) {
                        retval.rt_field[lpc2] = 0;
                    }
                    break;

                case RTT_SUNDAY:
                case RTT_MONDAY:
                case RTT_TUESDAY:
                case RTT_WEDNESDAY:
                case RTT_THURSDAY:
                case RTT_FRIDAY:
                case RTT_SATURDAY:
                    if (retval.rt_duration == 0s) {
                        switch (base_token) {
                            case RTT_BEFORE:
                                if (token == RTT_SUNDAY) {
                                    pe_out.pe_msg =
                                        "Sunday is the start of the week, so "
                                        "there is nothing before it";
                                    return Err(pe_out);
                                }
                                for (int wday = RTT_SUNDAY;
                                     wday < token;
                                     wday++) {
                                    retval.rt_included_days.insert((token_t) wday);
                                }
                                break;
                            case RTT_AFTER:
                                if (token == RTT_SATURDAY) {
                                    pe_out.pe_msg =
                                        "Saturday is the end of the week, so "
                                        "there is nothing after it";
                                    return Err(pe_out);
                                }
                                for (int wday = RTT_SATURDAY;
                                     wday > token;
                                     wday--) {
                                    retval.rt_included_days.insert((token_t) wday);
                                }
                                break;
                            default:
                                retval.rt_included_days.insert(token);
                                break;
                        }
                        base_token = RTT_INVALID;
                    } else {
                        retval.rt_included_days.insert(token);
                    }
                    if (retval.rt_duration == 0s) {
                        retval.rt_duration = 24h;
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
            seen_tokens.insert(token);
        }

        if (!found) {
            pe_out.pe_msg = "Unrecognized input";
            return Err(pe_out);
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
        this->rt_field[lpc].value = val % TIME_SCALES[lpc];
        this->rt_field[lpc + 1].value += val / TIME_SCALES[lpc];
        if (this->rt_field[lpc + 1].value) {
            this->rt_field[lpc + 1].is_set = true;
        }
    }
    if (std::abs(this->rt_field[RTF_DAYS].value) > 31) {
        int64_t val = this->rt_field[RTF_DAYS].value;
        this->rt_field[RTF_DAYS].value = val % 31;
        this->rt_field[RTF_MONTHS].value += val / 31;
        if (this->rt_field[RTF_MONTHS].value) {
            this->rt_field[RTF_MONTHS].is_set = true;
        }
    }
    if (std::abs(this->rt_field[RTF_MONTHS].value) > 12) {
        int64_t val = this->rt_field[RTF_MONTHS].value;
        this->rt_field[RTF_MONTHS].value = val % 12;
        this->rt_field[RTF_YEARS].value += val / 12;
        if (this->rt_field[RTF_YEARS].value) {
            this->rt_field[RTF_YEARS].is_set = true;
        }
    }
}

relative_time relative_time::from_timeval(const struct timeval& tv)
{
    relative_time retval;

    retval.clear();
    retval.rt_field[RTF_SECONDS] = tv.tv_sec;
    retval.rt_field[RTF_MICROSECONDS] = tv.tv_usec;
    retval.rollover();

    return retval;
}

relative_time relative_time::from_usecs(std::chrono::microseconds usecs)
{
    relative_time retval;

    retval.clear();
    retval.rt_field[RTF_MICROSECONDS] = usecs.count();
    retval.rollover();

    return retval;
}

std::string relative_time::to_string() const
{
    static const char *DAYS[] = {
        "sun",
        "mon",
        "tue",
        "wed",
        "thu",
        "fri",
        "sat",
    };

    char dst[128] = "";
    char *pos = dst;

    if (this->is_absolute()) {
        for (const auto& day_token : this->rt_included_days) {
            pos += snprintf(pos, sizeof(dst) - (pos - dst),
                            "%s ", DAYS[day_token - RTT_SUNDAY]);
        }

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

    if (dst[0] == '\0') {
        dst[0] = '0';
        dst[1] = 's';
        dst[2] = '\0';
    }

    return dst;
}

struct exttm relative_time::adjust(const exttm &tm) const
{
    auto retval = tm;

    if (this->rt_field[RTF_MICROSECONDS].is_set && this->is_absolute(RTF_MICROSECONDS)) {
        retval.et_nsec = this->rt_field[RTF_MICROSECONDS].value * 1000;
    }
    else {
        retval.et_nsec += this->rt_field[RTF_MICROSECONDS].value * 1000;
    }
    if (this->rt_field[RTF_SECONDS].is_set && this->is_absolute(RTF_SECONDS)) {
        if (this->rt_next &&
            this->rt_field[RTF_SECONDS].value <= tm.et_tm.tm_sec) {
            retval.et_tm.tm_min += 1;
        }
        if (this->rt_previous &&
            this->rt_field[RTF_SECONDS].value >= tm.et_tm.tm_sec) {
            retval.et_tm.tm_min -= 1;
        }
        retval.et_tm.tm_sec = this->rt_field[RTF_SECONDS].value;
    }
    else {
        retval.et_tm.tm_sec += this->rt_field[RTF_SECONDS].value;
    }
    if (this->rt_field[RTF_MINUTES].is_set && this->is_absolute(RTF_MINUTES)) {
        if (this->rt_next &&
            this->rt_field[RTF_MINUTES].value <= tm.et_tm.tm_min) {
            retval.et_tm.tm_hour += 1;
        }
        if (this->rt_previous && (this->rt_field[RTF_MINUTES].value == 0 ||
                                  (this->rt_field[RTF_MINUTES].value >= tm.et_tm.tm_min))) {
            retval.et_tm.tm_hour -= 1;
        }
        retval.et_tm.tm_min = this->rt_field[RTF_MINUTES].value;
    }
    else {
        retval.et_tm.tm_min += this->rt_field[RTF_MINUTES].value;
    }
    if (this->rt_field[RTF_HOURS].is_set && this->is_absolute(RTF_HOURS)) {
        if (this->rt_next &&
            this->rt_field[RTF_HOURS].value <= tm.et_tm.tm_hour) {
            retval.et_tm.tm_mday += 1;
        }
        if (this->rt_previous &&
            this->rt_field[RTF_HOURS].value >= tm.et_tm.tm_hour) {
            retval.et_tm.tm_mday -= 1;
        }
        retval.et_tm.tm_hour = this->rt_field[RTF_HOURS].value;
    }
    else {
        retval.et_tm.tm_hour += this->rt_field[RTF_HOURS].value;
    }
    if (this->rt_field[RTF_DAYS].is_set && this->is_absolute(RTF_DAYS)) {
        if (this->rt_next &&
            this->rt_field[RTF_DAYS].value <= tm.et_tm.tm_mday) {
            retval.et_tm.tm_mon += 1;
        }
        if (this->rt_previous &&
            this->rt_field[RTF_DAYS].value >= tm.et_tm.tm_mday) {
            retval.et_tm.tm_mon -= 1;
        }
        retval.et_tm.tm_mday = this->rt_field[RTF_DAYS].value;
    }
    else {
        retval.et_tm.tm_mday += this->rt_field[RTF_DAYS].value;
    }
    if (this->rt_field[RTF_MONTHS].is_set && this->is_absolute(RTF_MONTHS)) {
        if (this->rt_next &&
            this->rt_field[RTF_MONTHS].value <= tm.et_tm.tm_mon) {
            retval.et_tm.tm_year += 1;
        }
        if (this->rt_previous &&
            this->rt_field[RTF_MONTHS].value >= tm.et_tm.tm_mon) {
            retval.et_tm.tm_year -= 1;
        }
        retval.et_tm.tm_mon = this->rt_field[RTF_MONTHS].value;
    }
    else {
        retval.et_tm.tm_mon += this->rt_field[RTF_MONTHS].value;
    }
    if (this->rt_field[RTF_YEARS].is_set && this->is_absolute(RTF_YEARS)) {
        retval.et_tm.tm_year = this->rt_field[RTF_YEARS].value;
    }
    else {
        retval.et_tm.tm_year += this->rt_field[RTF_YEARS].value;
    }

    return retval;
}

nonstd::optional<exttm> relative_time::window_start(
    const struct exttm &tm) const
{
    auto retval = tm;

    if (this->is_relative()) {
        uint64_t us, remainder;

        auto tv = tm.to_timeval();
        us = (uint64_t) tv.tv_sec * 1000000ULL + (uint64_t) tv.tv_usec;
        remainder = us % this->to_microseconds();
        us -= remainder;

        tv.tv_sec = us / 1000000ULL;
        tv.tv_usec = us % 1000000ULL;

        retval.et_tm = *gmtime(&tv.tv_sec);
        retval.et_nsec = tv.tv_usec * 1000ULL;

        return retval;
    }

    bool clear = false;

    if (this->rt_field[RTF_YEARS].is_set) {
        if (this->rt_field[RTF_YEARS].value > tm.et_tm.tm_year) {
            return nonstd::nullopt;
        }
        retval.et_tm.tm_year = this->rt_field[RTF_YEARS].value;
        clear = true;
    }

    if (this->rt_field[RTF_MONTHS].is_set) {
        if (this->rt_field[RTF_MONTHS].value > tm.et_tm.tm_mon) {
            return nonstd::nullopt;
        }
        retval.et_tm.tm_mon = this->rt_field[RTF_MONTHS].value;
        clear = true;
    } else if (clear) {
        retval.et_tm.tm_mon = 0;
    }

    if (this->rt_field[RTF_DAYS].is_set) {
        if (this->rt_field[RTF_DAYS].value > tm.et_tm.tm_mday) {
            return nonstd::nullopt;
        }
        retval.et_tm.tm_mday = this->rt_field[RTF_DAYS].value;
        clear = true;
    } else if (clear) {
        retval.et_tm.tm_mday = 1;
    }

    if (!this->rt_included_days.empty()) {
        auto iter = this->rt_included_days.find((token_t) (RTT_SUNDAY + tm.et_tm.tm_wday));

        if (iter == this->rt_included_days.end()) {
            return nonstd::nullopt;
        }
        clear = true;
    }

    if (this->rt_field[RTF_HOURS].is_set) {
        if (this->rt_field[RTF_HOURS].value > tm.et_tm.tm_hour) {
            return nonstd::nullopt;
        }
        retval.et_tm.tm_hour = this->rt_field[RTF_HOURS].value;
        clear = true;
    } else if (clear) {
        retval.et_tm.tm_hour = 0;
    }

    if (this->rt_field[RTF_MINUTES].is_set) {
        if (this->rt_field[RTF_MINUTES].value > tm.et_tm.tm_min) {
            return nonstd::nullopt;
        }
        retval.et_tm.tm_min = this->rt_field[RTF_MINUTES].value;
        clear = true;
    } else if (clear) {
        retval.et_tm.tm_min = 0;
    }

    if (this->rt_field[RTF_SECONDS].is_set) {
        if (this->rt_field[RTF_SECONDS].value > tm.et_tm.tm_sec) {
            return nonstd::nullopt;
        }
        retval.et_tm.tm_sec = this->rt_field[RTF_SECONDS].value;
        clear = true;
    } else if (clear) {
        retval.et_tm.tm_sec = 0;
    }

    if (this->rt_field[RTF_MICROSECONDS].is_set) {
        if (this->rt_field[RTF_MICROSECONDS].value > tm.et_nsec / 1000) {
            return nonstd::nullopt;
        }
        retval.et_nsec = this->rt_field[RTF_MICROSECONDS].value * 1000ULL;
        clear = true;
    } else if (clear) {
        retval.et_nsec = 0;
    }

    auto tv = tm.to_timeval();
    auto start_time = retval.to_timeval();
    auto end_time = relative_time::from_usecs(this->rt_duration)
        .adjust(retval).to_timeval();

    if (tv < start_time || end_time < tv) {
        return nonstd::nullopt;
    }

    return retval;
}

int64_t relative_time::to_microseconds() const
{
    int64_t retval;

    if (this->is_absolute()) {
        struct exttm etm;

        memset(&etm, 0, sizeof(etm));
        etm.et_tm.tm_year = this->rt_field[RTF_YEARS].value;
        etm.et_tm.tm_mon = this->rt_field[RTF_MONTHS].value;
        if (this->rt_field[RTF_DAYS].is_set) {
            etm.et_tm.tm_mday = this->rt_field[RTF_DAYS].value;
        } else {
            etm.et_tm.tm_mday = 1;
        }
        etm.et_tm.tm_min = this->rt_field[RTF_MINUTES].value;
        etm.et_tm.tm_sec = this->rt_field[RTF_SECONDS].value;

        auto epoch_secs = std::chrono::seconds(tm2sec(&etm.et_tm));
        retval = std::chrono::duration_cast<std::chrono::microseconds>(epoch_secs).count();
        retval += this->rt_field[RTF_MICROSECONDS].value;
    } else {
        retval = this->rt_field[RTF_YEARS].value * 12;
        retval = (retval + this->rt_field[RTF_MONTHS].value) * 30;
        retval = (retval + this->rt_field[RTF_DAYS].value) * 24;
        retval = (retval + this->rt_field[RTF_HOURS].value) * 60;
        retval = (retval + this->rt_field[RTF_MINUTES].value) * 60;
        retval = (retval + this->rt_field[RTF_SECONDS].value) * 1000 * 1000;
        retval = (retval + this->rt_field[RTF_MICROSECONDS].value);
    }

    return retval;
}
