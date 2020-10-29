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
#include <stdarg.h>
#include <paths.h>

#include <fstream>

#include "auto_fd.hh"
#include "lnav_util.hh"
#include "pcrepp/pcrepp.hh"
#include "lnav_config.hh"
#include "base/result.h"
#include "ansi_scrubber.hh"
#include "view_curses.hh"
#include "archive_manager.hh"

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

std::string time_ago(time_t last_time, bool convert_local)
{
    time_t      delta, current_time = time(NULL);
    const char *fmt;
    char        buffer[64];
    int         amount;

    if (convert_local) {
        current_time = convert_log_time_to_local(current_time);
    }

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
    else if (delta < (2 * 365 * 24 * 60 * 60)) {
        return "over a year ago";
    }
    else {
        fmt = "over %d years ago";
        amount = delta / (365 * 24 * 60 * 60);
    }

    snprintf(buffer, sizeof(buffer), fmt, amount);

    return std::string(buffer);
}

std::string precise_time_ago(const struct timeval &tv, bool convert_local)
{
    struct timeval now, diff;

    gettimeofday(&now, NULL);
    if (convert_local) {
        now.tv_sec = convert_log_time_to_local(now.tv_sec);
    }

    timersub(&now, &tv, &diff);
    if (diff.tv_sec < 0) {
        return time_ago(tv.tv_sec);
    }
    else if (diff.tv_sec <= 1) {
        return "a second ago";
    }
    else if (diff.tv_sec < (10 * 60)) {
        char buf[64];

        if (diff.tv_sec < 60) {
            snprintf(buf, sizeof(buf),
                     "%2ld seconds ago",
                     diff.tv_sec);
        }
        else {
            time_t seconds = diff.tv_sec % 60;
            time_t minutes = diff.tv_sec / 60;

            snprintf(buf, sizeof(buf),
                     "%2ld minute%s and %2ld second%s ago",
                     minutes,
                     minutes > 1 ? "s" : "",
                     seconds,
                     seconds == 1 ? "" : "s");
        }

        return string(buf);
    }
    else {
        return time_ago(tv.tv_sec, convert_local);
    }
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

void split_ws(const std::string &str, std::vector<std::string> &toks_out)
{
    std::stringstream ss(str);
    std::string buf;

    while (ss >> buf) {
        toks_out.push_back(buf);
    }
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
    if (archive_manager::is_archive(filename)) {
        return file_format_t::FF_ARCHIVE;
    }

    file_format_t retval = file_format_t::FF_UNKNOWN;
    auto_fd       fd;

    if ((fd = open(filename.c_str(), O_RDONLY)) != -1) {
        char buffer[32];
        ssize_t rc;

        if ((rc = read(fd, buffer, sizeof(buffer))) > 0) {
            static auto SQLITE3_HEADER = "SQLite format 3";
            auto header_frag = string_fragment(buffer, 0, rc);

            if (header_frag.startswith(SQLITE3_HEADER)) {
                retval = file_format_t::FF_SQLITE_DB;
            }
        }
    }

    return retval;
}

static time_t BAD_DATE = -1;

time_t tm2sec(const struct tm *t)
{
    int       year;
    time_t    days, secs;
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

    secs = ((days * 24 + t->tm_hour) * 60 + t->tm_min) * 60 + t->tm_sec;

    if (secs < 0) {
        return BAD_DATE;
    }                          /* must have overflowed */
    else {
#ifdef HAVE_STRUCT_TM_TM_ZONE
        if (t->tm_zone) {
            secs -= t->tm_gmtoff;
        }
#endif
        return secs;
    }                          /* must be a valid time */
}

static const int SECSPERMIN = 60;
static const int SECSPERHOUR = 60 * SECSPERMIN;
static const int SECSPERDAY = 24 * SECSPERHOUR;
static const int YEAR_BASE = 1900;
static const int EPOCH_WDAY = 4;
static const int DAYSPERWEEK = 7;
static const int EPOCH_YEAR = 1970;

#define isleap(y) ((((y) % 4) == 0 && ((y) % 100) != 0) || ((y) % 400) == 0)

static const int year_lengths[2] = {
        365,
        366
};

const unsigned short int mon_yday[2][13] = {
    /* Normal years.  */
    { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
    /* Leap years.  */
    { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 }
};

static void secs2wday(const struct timeval &tv, struct tm *res)
{
    long days, rem;
    time_t lcltime;

    /* base decision about std/dst time on current time */
    lcltime = tv.tv_sec;

    days = ((long) lcltime) / SECSPERDAY;
    rem = ((long) lcltime) % SECSPERDAY;
    while (rem < 0) {
        rem += SECSPERDAY;
        --days;
    }

    /* compute day of week */
    if ((res->tm_wday = ((EPOCH_WDAY + days) % DAYSPERWEEK)) < 0)
        res->tm_wday += DAYSPERWEEK;
}

struct tm *secs2tm(time_t *tim_p, struct tm *res)
{
    long days, rem;
    time_t lcltime;
    int y;
    int yleap;
    const unsigned short int *ip;

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
    ip = mon_yday[isleap(y)];
    for (y = 11; days < (long int) ip[y]; --y)
        continue;
    days -= ip[y];
    res->tm_mon = y;
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

const char *date_time_scanner::scan(const char *time_dest,
                                    size_t time_len,
                                    const char * const time_fmt[],
                                    struct exttm *tm_out,
                                    struct timeval &tv_out,
                                    bool convert_local)
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
        if (time_len > 1 &&
            time_dest[0] == '+' &&
            isdigit(time_dest[1])) {
            char time_cp[time_len + 1];
            int gmt_int, off;

            retval = NULL;
            memcpy(time_cp, time_dest, time_len);
            time_cp[time_len] = '\0';
            if (sscanf(time_cp, "+%d%n", &gmt_int, &off) == 1) {
                time_t gmt = gmt_int;

                if (convert_local && this->dts_local_time) {
                    localtime_r(&gmt, &tm_out->et_tm);
#ifdef HAVE_STRUCT_TM_TM_ZONE
                    tm_out->et_tm.tm_zone = NULL;
#endif
                    tm_out->et_tm.tm_isdst = 0;
                    gmt = tm2sec(&tm_out->et_tm);
                }
                tv_out.tv_sec = gmt;
                tv_out.tv_usec = 0;
                tm_out->et_flags = ETF_DAY_SET|ETF_MONTH_SET|ETF_YEAR_SET|ETF_MACHINE_ORIENTED|ETF_EPOCH_TIME;

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
                if (convert_local &&
                    (this->dts_local_time || tm_out->et_flags & ETF_EPOCH_TIME)) {
                    time_t gmt = tm2sec(&tm_out->et_tm);

                    this->to_localtime(gmt, *tm_out);
                }
                tv_out.tv_sec = tm2sec(&tm_out->et_tm);
                tv_out.tv_usec = tm_out->et_nsec / 1000;
                secs2wday(tv_out, &tm_out->et_tm);

                this->dts_fmt_lock = curr_time_fmt;
                this->dts_fmt_len  = retval - time_dest;

                found = true;
                break;
            }
        }
        else {
            off_t off = 0;

#ifdef HAVE_STRUCT_TM_TM_ZONE
            if (!this->dts_keep_base_tz) {
                tm_out->et_tm.tm_zone = NULL;
            }
#endif
            if (ptime_fmt(time_fmt[curr_time_fmt], tm_out, time_dest, off, time_len) &&
                (time_dest[off] == '.' || time_dest[off] == ',' || off == (off_t)time_len)) {
                retval = &time_dest[off];
                if (tm_out->et_tm.tm_year < 70) {
                    tm_out->et_tm.tm_year = 80;
                }
                if (convert_local &&
                    (this->dts_local_time || tm_out->et_flags & ETF_EPOCH_TIME)) {
                    time_t gmt = tm2sec(&tm_out->et_tm);

                    this->to_localtime(gmt, *tm_out);
#ifdef HAVE_STRUCT_TM_TM_ZONE
                    tm_out->et_tm.tm_zone = NULL;
#endif
                    tm_out->et_tm.tm_isdst = 0;
                }

                tv_out.tv_sec = tm2sec(&tm_out->et_tm);
                tv_out.tv_usec = tm_out->et_nsec / 1000;
                secs2wday(tv_out, &tm_out->et_tm);

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
            else if (ptime_L(tm_out, time_dest, off, time_len)) {
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

string build_path(const vector<ghc::filesystem::path> &paths)
{
    string retval;

    for (const auto &path : paths) {
        if (path.empty()) {
            continue;
        }
        if (!retval.empty()) {
            retval += ":";
        }
        retval += path.string();
    }
    retval += ":" + string(getenv("PATH"));
    return retval;
}

bool read_file(const ghc::filesystem::path &filename, string &out)
{
    std::ifstream sql_file(filename.string());

    if (sql_file) {
        out.assign((std::istreambuf_iterator<char>(sql_file)),
                   std::istreambuf_iterator<char>());
        return true;
    }

    return false;
}

size_t abbreviate_str(char *str, size_t len, size_t max_len)
{
    size_t last_start = 1;

    if (len < max_len) {
        return len;
    }

    for (size_t index = 0; index < len; index++) {
        switch (str[index]) {
            case '.':
            case '-':
            case '/':
            case ':':
                memmove(&str[last_start], &str[index], len - index);
                len -= (index - last_start);
                index = last_start + 1;
                last_start = index + 1;

                if (len < max_len) {
                    return len;
                }
                break;
        }
    }

    return len;
}

ghc::filesystem::path system_tmpdir()
{
    const char *tmpdir;

    if ((tmpdir = getenv("TMPDIR")) == nullptr) {
        tmpdir = _PATH_VARTMP;
    }

    return ghc::filesystem::path(tmpdir);
}

Result<std::pair<ghc::filesystem::path, int>, std::string>
open_temp_file(const ghc::filesystem::path &pattern)
{
    auto pattern_str = pattern.string();
    char pattern_copy[pattern_str.size() + 1];
    int fd;

    strcpy(pattern_copy, pattern_str.c_str());
    if ((fd = mkstemp(pattern_copy)) == -1) {
        throw Err(strerror(errno));
    }

    return Ok(make_pair(ghc::filesystem::path(pattern_copy), fd));
}

bool is_dev_null(const struct stat &st)
{
    struct stat null_stat;

    stat("/dev/null", &null_stat);

    return st.st_dev == null_stat.st_dev &&
           st.st_ino == null_stat.st_ino;
}

bool is_dev_null(int fd)
{
    struct stat fd_stat;

    fstat(fd, &fd_stat);
    return is_dev_null(fd_stat);
}

std::string ok_prefix(std::string msg)
{
    if (msg.empty()) {
        return msg;
    }

    return std::string(ANSI_COLOR(COLOR_GREEN) "\u2714" ANSI_NORM " ") + msg;
}

std::string err_prefix(const std::string msg)
{
    if (msg.empty()) {
        return msg;
    }

    return std::string(ANSI_COLOR(COLOR_RED) "\u2718" ANSI_NORM " ") + msg;
}

Result<std::string, std::string> err_to_ok(const std::string msg)
{
    return Ok(err_prefix(msg));
}
