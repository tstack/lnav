/**
 * Copyright (c) 2013, Timothy Stack
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
 * @file lnav_util.cc
 *
 * Dumping ground for useful functions with no other home.
 */

#include "config.h"

#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <wordexp.h>

#include <sqlite3.h>

#include <fstream>

#include "auto_fd.hh"
#include "lnav_util.hh"
#include "pcrepp.hh"
#include "lnav_config.hh"

using namespace std;

bool is_url(const char *fn)
{
    static pcrepp url_re("^(file|https?|ftps?||scp|sftp):");

    pcre_context_static<30> pc;
    pcre_input pi(fn);

    return url_re.match(pc, pi);
}

std::string hash_string(const std::string &str)
{
    byte_array<2, uint64> hash;
    SpookyHash context;

    context.Init(0, 0);
    context.Update(str.c_str(), str.length());
    context.Final(hash.out(0), hash.out(1));

    return hash.to_string();
}

std::string hash_bytes(const char *str1, size_t s1len, ...)
{
    byte_array<2, uint64> hash;
    SpookyHash context;
    va_list args;

    va_start(args, s1len);

    context.Init(0, 0);
    while (str1 != NULL) {
        context.Update(str1, s1len);

        str1 = va_arg(args, const char *);
        if (str1 == NULL) {
            break;
        }
        s1len = va_arg(args, size_t);
    }
    context.Final(hash.out(0), hash.out(1));

    va_end(args);

    return hash.to_string();
}

size_t unquote(char *dst, const char *str, size_t len)
{
    if (str[0] == 'r' || str[0] == 'u') {
        str += 1;
        len -= 1;
    }
    char quote_char = str[0];
    size_t index = 0;

    require(str[0] == '\'' || str[0] == '"');

    for (size_t lpc = 1; lpc < (len - 1); lpc++, index++) {
        dst[index] = str[lpc];
        if (str[lpc] == quote_char) {
            lpc += 1;
        }
        else if (str[lpc] == '\\' && (lpc + 1) < len) {
            switch (str[lpc] + 1) {
                case 'n':
                    dst[index] = '\n';
                    break;
                case 'r':
                    dst[index] = '\r';
                    break;
                case 't':
                    dst[index] = '\t';
                    break;
                default:
                    dst[index] = str[lpc + 1];
                    break;
            }
            lpc += 1;
        }
    }
    dst[index] = '\0';

    return index;
}

std::string time_ago(time_t last_time)
{
    time_t      delta, current_time = time(NULL);
    const char *fmt;
    char        buffer[64];
    int         amount;

    delta = current_time - last_time;
    if (delta < 0) {
        return "in the future";
    }
    else if (delta < 60) {
        return "just now";
    }
    else if (delta < (60 * 2)) {
        return "one minute ago";
    }
    else if (delta < (60 * 60)) {
        fmt    = "%d minutes ago";
        amount = delta / 60;
    }
    else if (delta < (2 * 60 * 60)) {
        return "one hour ago";
    }
    else if (delta < (24 * 60 * 60)) {
        fmt    = "%d hours ago";
        amount = delta / (60 * 60);
    }
    else if (delta < (2 * 24 * 60 * 60)) {
        return "one day ago";
    }
    else if (delta < (365 * 24 * 60 * 60)) {
        fmt    = "%d days ago";
        amount = delta / (24 * 60 * 60);
    }
    else {
        return "over a year ago";
    }

    snprintf(buffer, sizeof(buffer), fmt, amount);

    return std::string(buffer);
}

/* XXX figure out how to do this with the template */
void sqlite_close_wrapper(void *mem)
{
    sqlite3_close((sqlite3 *)mem);
}

std::string get_current_dir(void)
{
    char        cwd[FILENAME_MAX];
    std::string retval = ".";

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd");
    }
    else {
        retval = std::string(cwd);
    }

    if (retval != "/") {
        retval += "/";
    }

    return retval;
}

bool change_to_parent_dir(void)
{
    bool retval = false;
    char cwd[3] = "";

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        /* perror("getcwd"); */
    }
    if (strcmp(cwd, "/") != 0) {
        if (chdir("..") == -1) {
            perror("chdir('..')");
        }
        else {
            retval = true;
        }
    }

    return retval;
}

std::pair<std::string, std::string> split_path(const char *path, ssize_t len)
{
    ssize_t dir_len = len;

    while (dir_len >= 0 && (path[dir_len] == '/' || path[dir_len] == '\\')) {
        dir_len -= 1;
    }

    while (dir_len >= 0) {
        if (path[dir_len] == '/' || path[dir_len] == '\\') {
            return make_pair(string(path, dir_len),
                             string(&path[dir_len + 1], len - dir_len));
        }

        dir_len -= 1;
    }

    return make_pair(path[0] == '/' ? "/" : ".",
                     path[0] == '/' ? string(&path[1], len - 1) : string(path, len));
}

file_format_t detect_file_format(const std::string &filename)
{
    file_format_t retval = FF_UNKNOWN;
    auto_fd       fd;

    if ((fd = open(filename.c_str(), O_RDONLY)) != -1) {
        char buffer[32];
        int  rc;

        if ((rc = read(fd, buffer, sizeof(buffer))) > 0) {
            if (rc > 16 &&
                strncmp(buffer, "SQLite format 3", 16) == 0) {
                retval = FF_SQLITE_DB;
            }
        }
    }

    return retval;
}

static time_t BAD_DATE = -1;

time_t tm2sec(const struct tm *t)
{
    int       year;
    time_t    days;
    const int dayoffset[12] =
    { 306, 337, 0, 31, 61, 92, 122, 153, 184, 214, 245, 275 };

    year = t->tm_year;

    if (year < 70 || ((sizeof(time_t) <= 4) && (year >= 138))) {
        return BAD_DATE;
    }

    /* shift new year to 1st March in order to make leap year calc easy */

    if (t->tm_mon < 2) {
        year--;
    }

    /* Find number of days since 1st March 1900 (in the Gregorian calendar). */

    days  = year * 365 + year / 4 - year / 100 + (year / 100 + 3) / 4;
    days += dayoffset[t->tm_mon] + t->tm_mday - 1;
    days -= 25508; /* 1 jan 1970 is 25508 days since 1 mar 1900 */

    days = ((days * 24 + t->tm_hour) * 60 + t->tm_min) * 60 + t->tm_sec;

    if (days < 0) {
        return BAD_DATE;
    }                          /* must have overflowed */
    else {
#ifdef HAVE_STRUCT_TM_TM_ZONE
        if (t->tm_zone) {
            days -= t->tm_gmtoff;
        }
#endif
        return days;
    }                          /* must be a valid time */
}

static const int MONSPERYEAR = 12;
static const int SECSPERMIN = 60;
static const int SECSPERHOUR = 60 * SECSPERMIN;
static const int SECSPERDAY = 24 * SECSPERHOUR;
static const int YEAR_BASE = 1900;
static const int EPOCH_WDAY = 4;
static const int DAYSPERWEEK = 7;
static const int EPOCH_YEAR = 1970;

#define isleap(y) ((((y) % 4) == 0 && ((y) % 100) != 0) || ((y) % 400) == 0)

static const int mon_lengths[2][MONSPERYEAR] = {
        {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
        {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
} ;

static const int year_lengths[2] = {
        365,
        366
} ;

struct tm *secs2tm(time_t *tim_p, struct tm *res)
{
    long days, rem;
    time_t lcltime;
    int y;
    int yleap;
    const int *ip;

    /* base decision about std/dst time on current time */
    lcltime = *tim_p;

    days = ((long)lcltime) / SECSPERDAY;
    rem = ((long)lcltime) % SECSPERDAY;
    while (rem < 0)
    {
        rem += SECSPERDAY;
        --days;
    }

    /* compute hour, min, and sec */
    res->tm_hour = (int) (rem / SECSPERHOUR);
    rem %= SECSPERHOUR;
    res->tm_min = (int) (rem / SECSPERMIN);
    res->tm_sec = (int) (rem % SECSPERMIN);

    /* compute day of week */
    if ((res->tm_wday = ((EPOCH_WDAY + days) % DAYSPERWEEK)) < 0)
        res->tm_wday += DAYSPERWEEK;

    /* compute year & day of year */
    y = EPOCH_YEAR;
    if (days >= 0)
    {
        for (;;)
        {
            yleap = isleap(y);
            if (days < year_lengths[yleap])
                break;
            y++;
            days -= year_lengths[yleap];
        }
    }
    else
    {
        do
        {
            --y;
            yleap = isleap(y);
            days += year_lengths[yleap];
        } while (days < 0);
    }

    res->tm_year = y - YEAR_BASE;
    res->tm_yday = days;
    ip = mon_lengths[yleap];
    for (res->tm_mon = 0; days >= ip[res->tm_mon]; ++res->tm_mon)
        days -= ip[res->tm_mon];
    res->tm_mday = days + 1;

    res->tm_isdst = 0;

    return (res);
}

bool next_format(const char * const fmt[], int &index, int &locked_index)
{
    bool retval = true;

    if (locked_index == -1) {
        index += 1;
        if (fmt[index] == NULL) {
            retval = false;
        }
    }
    else if (index == locked_index) {
        retval = false;
    }
    else {
        index = locked_index;
    }

    return retval;
}

static const char *time_fmt_with_zone = "%a %b %d %H:%M:%S ";

const char *std_time_fmt[] = {
    "%Y-%m-%d %H:%M:%S",
    "%Y-%m-%d %H:%M",
    "%Y-%m-%dT%H:%M:%S",
    "%Y-%m-%dT%H:%M:%SZ",
    "%Y/%m/%d %H:%M:%S",
    "%Y/%m/%d %H:%M",

    "%a %b %d %H:%M:%S %Y",
    "%a %b %d %H:%M:%S %Z %Y",
    time_fmt_with_zone,

    "%d/%b/%Y:%H:%M:%S +0000",
    "%d/%b/%Y:%H:%M:%S %z",

    "%b %d %H:%M:%S",

    "%m/%d/%y %H:%M:%S",

    "%m%d %H:%M:%S",

    "+%s",

    NULL,
};

const char *date_time_scanner::scan(const char *time_dest,
                                    size_t time_len,
                                    const char * const time_fmt[],
                                    struct exttm *tm_out,
                                    struct timeval &tv_out)
{
    int  curr_time_fmt = -1;
    bool found         = false;
    const char *retval = NULL;

    if (!time_fmt) {
        time_fmt = PTIMEC_FORMAT_STR;
    }

    while (next_format(time_fmt,
                       curr_time_fmt,
                       this->dts_fmt_lock)) {
        *tm_out = this->dts_base_tm;
        tm_out->et_flags = 0;
        if (time_dest[0] == '+') {
            char time_cp[time_len + 1];
            int gmt_int, off;

            retval = NULL;
            memcpy(time_cp, time_dest, time_len);
            time_cp[time_len] = '\0';
            if (sscanf(time_cp, "+%d%n", &gmt_int, &off) == 1) {
                time_t gmt = gmt_int;

                if (this->dts_local_time) {
                    localtime_r(&gmt, &tm_out->et_tm);
#ifdef HAVE_STRUCT_TM_TM_ZONE
                    tm_out->et_tm.tm_zone = NULL;
#endif
                    tm_out->et_tm.tm_isdst = 0;
                    gmt = tm2sec(&tm_out->et_tm);
                }
                tv_out.tv_sec = gmt;
                tv_out.tv_usec = 0;
                tm_out->et_flags = ETF_DAY_SET|ETF_MONTH_SET|ETF_YEAR_SET;

                this->dts_fmt_lock = curr_time_fmt;
                this->dts_fmt_len = off;
                retval = time_dest + off;
                found = true;
                break;
            }
        }
        else if (time_fmt == PTIMEC_FORMAT_STR) {
            ptime_func func = PTIMEC_FORMATS[curr_time_fmt].pf_func;
            off_t off = 0;

#ifdef HAVE_STRUCT_TM_TM_ZONE
            if (!this->dts_keep_base_tz) {
                tm_out->et_tm.tm_zone = NULL;
            }
#endif
            if (func(tm_out, time_dest, off, time_len)) {
                retval = &time_dest[off];

                if (tm_out->et_tm.tm_year < 70) {
                    tm_out->et_tm.tm_year = 80;
                }
                if (this->dts_local_time) {
                    time_t gmt = tm2sec(&tm_out->et_tm);

                    this->to_localtime(gmt, *tm_out);
                }
                tv_out.tv_sec = tm2sec(&tm_out->et_tm);
                tv_out.tv_usec = tm_out->et_nsec / 1000;

                this->dts_fmt_lock = curr_time_fmt;
                this->dts_fmt_len  = retval - time_dest;

                found = true;
                break;
            }
        }
        else {
            off_t off = 0;

            if (ptime_fmt(time_fmt[curr_time_fmt], tm_out, time_dest, off, time_len)) {
                retval = &time_dest[off];
                if (tm_out->et_tm.tm_year < 70) {
                    tm_out->et_tm.tm_year = 80;
                }
                if (this->dts_local_time) {
                    time_t gmt = tm2sec(&tm_out->et_tm);

                    this->to_localtime(gmt, *tm_out);
#ifdef HAVE_STRUCT_TM_TM_ZONE
                    tm_out->et_tm.tm_zone = NULL;
#endif
                    tm_out->et_tm.tm_isdst = 0;
                }

                tv_out.tv_sec = tm2sec(&tm_out->et_tm);
                tv_out.tv_usec = tm_out->et_nsec / 1000;

                this->dts_fmt_lock = curr_time_fmt;
                this->dts_fmt_len  = retval - time_dest;

                found = true;
                break;
            }
        }
    }

    if (!found) {
        retval = NULL;
    }

    if (retval != NULL) {
        /* Try to pull out the milli/micro-second value. */
        if (retval[0] == '.' || retval[0] == ',') {
            off_t off = (retval - time_dest) + 1;

            if (ptime_f(tm_out, time_dest, off, time_len)) {
                tv_out.tv_usec = tm_out->et_nsec / 1000;
                this->dts_fmt_len += 7;
                retval += 7;
            }
            else if (ptime_F(tm_out, time_dest, off, time_len)) {
                tv_out.tv_usec = tm_out->et_nsec / 1000;
                this->dts_fmt_len += 4;
                retval += 4;
            }
        }
    }

    return retval;
}

template<typename T>
size_t strtonum(T &num_out, const char *string, size_t len)
{
    size_t retval = 0;
    T sign = 1;

    num_out = 0;
    
    for (; retval < len && isspace(string[retval]); retval++);
    for (; retval < len && string[retval] == '-'; retval++) {
        sign *= -1;
    }
    for (; retval < len && string[retval] == '+'; retval++);
    for (; retval < len && isdigit(string[retval]); retval++) {
        num_out *= 10;
        num_out += string[retval] - '0';
    }

    return retval;
}

template
size_t strtonum<long long>(long long &num_out, const char *string, size_t len);

template
size_t strtonum<long>(long &num_out, const char *string, size_t len);

template
size_t strtonum<int>(int &num_out, const char *string, size_t len);

string build_path(const vector<string> &paths)
{
    string retval;

    for (vector<string>::const_iterator path_iter = paths.begin();
         path_iter != paths.end();
         ++path_iter) {
        if (path_iter->empty()) {
            continue;
        }
        if (!retval.empty()) {
            retval += ":";
        }
        retval += *path_iter;
    }
    retval += ":" + string(getenv("PATH"));
    return retval;
}

bool read_file(const char *filename, string &out)
{
    std::ifstream sql_file(filename);

    if (sql_file) {
        out.assign((std::istreambuf_iterator<char>(sql_file)),
                   std::istreambuf_iterator<char>());
        return true;
    }

    return false;
}

bool wordexperr(int rc, string &msg)
{
    switch (rc) {
        case WRDE_BADCHAR:
            msg = "error: invalid filename character";
            return false;

        case WRDE_CMDSUB:
            msg = "error: command substitution is not allowed";
            return false;

        case WRDE_BADVAL:
            msg = "error: unknown environment variable in file name";
            return false;

        case WRDE_NOSPACE:
            msg = "error: out of memory";
            return false;

        case WRDE_SYNTAX:
            msg = "error: invalid syntax";
            return false;

        default:
            break;
    }

    return true;
}
