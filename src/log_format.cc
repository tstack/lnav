/**
 * Copyright (c) 2007-2012, Timothy Stack
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

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "log_format.hh"

using namespace std;

/*
 * Supported formats:
 *   generic
 *   syslog
 *   apache
 *   tcpdump
 *   strace
 *   vstrace
 *   csv (?)
 *   file system (?)
 *   plugins
 *   vmstat
 *   iostat
 */

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
    	if (t->tm_zone)
    		days -= t->tm_gmtoff;
	return days;
    }                          /* must be a valid time */
}

const char *logline::level_names[LEVEL__MAX] = {
    "unknown",
    "trace",
    "debug",
    "info",
    "warning",
    "error",
    "critical",
    "fatal",
};

logline::level_t logline::string2level(const char *levelstr)
{
    logline::level_t retval = logline::LEVEL_UNKNOWN;
    
    if (strcasestr(levelstr, "TRACE")) {
	retval = logline::LEVEL_TRACE;
    }
    else if (strcasestr(levelstr, "VERBOSE")) {
	retval = logline::LEVEL_DEBUG;
    }
    else if (strcasestr(levelstr, "DEBUG")) {
	retval = logline::LEVEL_DEBUG;
    }
    else if (strcasestr(levelstr, "INFO")) {
	retval = logline::LEVEL_INFO;
    }
    else if (strcasestr(levelstr, "WARNING")) {
	retval = logline::LEVEL_WARNING;
    }
    else if (strcasestr(levelstr, "ERROR")) {
	retval = logline::LEVEL_ERROR;
    }
    else if (strcasestr(levelstr, "CRITICAL")) {
	retval = logline::LEVEL_CRITICAL;
    }
    else if (strcasestr(levelstr, "FATAL")) {
    	retval = logline::LEVEL_FATAL;
    }
    
    return retval;
}

vector<log_format *> log_format::lf_root_formats;

vector<log_format *> &log_format::get_root_formats(void)
{
    return lf_root_formats;
}

static bool next_format(const char *fmt[], int &index, int &locked_index)
{
    bool retval = true;

    if (locked_index == -1) {
	index += 1;
	if (fmt[index] == NULL)
	    retval = false;
    }
    else if (index == locked_index) {
	retval = false;
    }
    else {
	index = locked_index;
    }

    return retval;
}

char *log_format::log_scanf(const char *line,
			    const char *fmt[],
			    int expected_matches,
			    const char *time_fmt[],
			    char *time_dest,
			    struct tm *tm_out,
			    time_t &time_out,
			    ...)
{
    static const char *std_time_fmt[] = {
	"%Y-%m-%d %H:%M:%S",
	"%Y-%m-%d %H:%M",
	"%Y-%m-%dT%H:%M:%S",
	"%Y/%m/%d %H:%M:%S",
	"%Y/%m/%d %H:%M",
	
	"%a %b %d %H:%M:%S %Y",
	
	"%d/%b/%Y:%H:%M:%S %z",
	
	"%b %d %H:%M:%S",
	
	NULL,
    };
    
    int curr_fmt = -1;
    char *retval = NULL;
    va_list args;

    while (next_format(fmt, curr_fmt, this->lf_fmt_lock)) {
	va_start(args, time_out);
	int matches;

	time_dest[0] = '\0';

	matches = vsscanf(line, fmt[curr_fmt], args);
	if (matches < expected_matches) {
	    retval = NULL;
	    continue;
	}

	if (time_dest[0] == '\0') {
	    retval = NULL;
	}
	else {
	    int curr_time_fmt = -1;
	    bool found = false;
	    
	    if (!time_fmt)
		time_fmt = std_time_fmt;
	    
	    while (next_format(time_fmt,
			       curr_time_fmt,
			       this->lf_time_fmt_lock)) {
		memset(tm_out, 0, sizeof(struct tm));
		if ((retval = strptime(time_dest,
				       time_fmt[curr_time_fmt],
			               tm_out)) != NULL) {
		    if (tm_out->tm_year < 70) {
			// XXX We should pull the time from the file mtime (?)
			tm_out->tm_year = 80;
		    }
		    time_out = tm2sec(tm_out);
		    
		    this->lf_fmt_lock = curr_fmt;
		    this->lf_time_fmt_lock = curr_time_fmt;

		    found = true;
		    break;
		}
	    }
	    
	    if (!found)
		retval = NULL;
	}

	va_end(args);
    }

    return retval;
}

// XXX
#include "log_format_impls.cc"
