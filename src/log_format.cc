
#include "config.h"

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

static time_t tm2sec(const struct tm *t)
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
    "critical"
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

int log_format::log_scanf(const char *line,
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
	"%Y/%m/%d %H:%M:%S",
	"%Y/%m/%d %H:%M",
	
	"%a %b %d %H:%M:%S %Y",
	
	"%d/%b/%Y:%H:%M:%S %z",
	
	"%b %d %H:%M:%S",
	
	NULL,
    };
    
    int curr_fmt = -1, retval = 0;
    va_list args;

    va_start(args, time_out);

    while (next_format(fmt, curr_fmt, this->lf_fmt_lock)) {
	time_dest[0] = '\0';
	
	retval = vsscanf(line, fmt[curr_fmt], args);
	if (retval < expected_matches) {
	    retval = 0;
	    continue;
	}

	if (time_dest[0] == '\0') {
	    retval = 0;
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
		if (strptime(time_dest,
			     time_fmt[curr_time_fmt],
			     tm_out) != NULL) {
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
		retval = 0;
	}
    }

    va_end(args);

    return retval;
}

// XXX
#include "log_format_impls.cc"
