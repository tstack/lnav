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

#include <sqlite3.h>

#include "auto_fd.hh"
#include "lnav_util.hh"

std::string hash_string(const std::string &str)
{
    byte_array<SHA_DIGEST_LENGTH> hash;
    SHA_CTX context;

    SHA_Init(&context);
    SHA_Update(&context, str.c_str(), str.length());
    SHA_Final(hash.out(), &context);

    return hash.to_string();
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

bool next_format(const char *fmt[], int &index, int &locked_index)
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

    "%d/%b/%Y:%H:%M:%S %z",

    "%b %d %H:%M:%S",

    "%m/%d/%y %H:%M:%S",

    "%m%d %H:%M:%S",

    "+%s",

    NULL,
};

const char *date_time_scanner::scan(const char *time_dest,
                                    const char *time_fmt[],
                                    struct tm *tm_out,
                                    struct timeval &tv_out)
{
    int  curr_time_fmt = -1;
    bool found         = false;
    const char *retval = NULL;

    if (!time_fmt) {
        time_fmt = std_time_fmt;
    }

    while (next_format(time_fmt,
                       curr_time_fmt,
                       this->dts_fmt_lock)) {
        *tm_out = this->dts_base_tm;
        if (time_fmt[curr_time_fmt][0] == '+') {
            int gmt_int, off;

            retval = NULL;
            if (sscanf(time_dest, "+%d%n", &gmt_int, &off) == 1) {
                time_t gmt = gmt_int;

                if (this->dts_local_time) {
                    localtime_r(&gmt, tm_out);
#ifdef HAVE_STRUCT_TM_TM_ZONE
                    tm_out->tm_zone = NULL;
#endif
                    tm_out->tm_isdst = 0;
                    gmt = tm2sec(tm_out);
                }
                tv_out.tv_sec = gmt;

                this->dts_fmt_lock = curr_time_fmt;
                this->dts_fmt_len = off;
                retval = time_dest + off;
                found = true;
                break;
            }
        }
        else if ((retval = strptime(time_dest,
                                    time_fmt[curr_time_fmt],
                                    tm_out)) != NULL) {
            if (time_fmt[curr_time_fmt] == time_fmt_with_zone) {
                int lpc;

                for (lpc = 0; retval[lpc] && retval[lpc] != ' '; lpc++) {

                }
                if (retval[lpc] == ' ' &&
                    sscanf(&retval[lpc], "%d", &tm_out->tm_year) == 1) {
                    lpc += 1;
                    for (; retval[lpc] && isdigit(retval[lpc]); lpc++) {

                    }
                    retval = &retval[lpc];
                }
            }

            if (tm_out->tm_year < 70) {
                tm_out->tm_year = 80;
            }
            if (this->dts_local_time) {
                time_t gmt = tm2sec(tm_out);

                localtime_r(&gmt, tm_out);
#ifdef HAVE_STRUCT_TM_TM_ZONE
                tm_out->tm_zone = NULL;
#endif
                tm_out->tm_isdst = 0;
            }
            tv_out.tv_sec = tm2sec(tm_out);
            tv_out.tv_usec = 0;

            this->dts_fmt_lock = curr_time_fmt;
            this->dts_fmt_len  = retval - time_dest;

            found = true;
            break;
        }
    }

    if (!found) {
        retval = NULL;
    }

    if (retval != NULL) {
        /* Try to pull out the milli/micro-second value. */
        if (retval[0] == '.' || retval[0] == ',') {
            int sub_seconds = 0, sub_len = 0;

            if (sscanf(retval + 1, "%d%n", &sub_seconds, &sub_len) == 1) {
                switch (sub_len) {
                case 3:
                    tv_out.tv_usec = sub_seconds * 1000;
                    this->dts_fmt_len += 1 + sub_len;
                    break;
                case 6:
                    tv_out.tv_usec = sub_seconds;
                    this->dts_fmt_len += 1 + sub_len;
                    break;
                }
            }
        }
    }

    return retval;
}
