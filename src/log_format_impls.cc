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
 *
 * @file log_format_impls.cc
 */

#include <stdio.h>

#include "log_format.hh"
#include "log_vtab_impl.hh"

using namespace std;

static pcrepp RDNS_PATTERN("^(?:com|net|org|edu|[a-z][a-z])"
                           "(\\.\\w+)+(.+)");

/**
 * Attempt to scrub a reverse-DNS string.
 * 
 * @param  str The string to scrub.  If the string looks like a reverse-DNS
 *   string, the leading components of the name will be reduced to a single
 *   letter.  For example, "com.example.foo" will be reduced to "c.e.foo".
 * @return     The scrubbed version of the input string or the original string
 *   if it is not a reverse-DNS string.
 */
static string scrub_rdns(const string &str)
{
        pcre_context_static<30> context;
        pcre_input input(str);
        string retval;

        if (RDNS_PATTERN.match(context, input)) {
                pcre_context::capture_t *cap;

                cap = context.begin();
                for (int index = 0; index < cap->c_begin; index++) {
                        if (index == 0 || str[index - 1] == '.') {
                                if (index > 0) {
                                        retval.append(1, '.');
                                }
                                retval.append(1, str[index]);
                        }
                }
                retval += input.get_substr(cap);
                retval += input.get_substr(cap + 1);
        }
        else {
                retval = str;
        }
        return retval;
}

class access_log_format : public log_format {
    string get_name() { return "access_log"; };

    bool scan(vector < logline > &dst,
	      off_t offset,
	      char *prefix,
	      int len) {
	static const char *log_fmt[] = {
	    "%*s %*s %*s [%63[^]]] \"%*[^\"]\" %d",
	    NULL
	};

	bool retval = false;
	struct tm log_time;
	int http_code = 0;
	char timestr[64];
	time_t line_time;

	if (this->log_scanf(prefix,
			    log_fmt,
			    2,
			    NULL,
			    timestr,
			    &log_time,
			    line_time,

			    timestr,
			    &http_code)) {
	    logline::level_t ll = logline::LEVEL_UNKNOWN;

	    if (http_code < 400) {
		ll = logline::LEVEL_INFO;
	    }
	    else {
		ll = logline::LEVEL_ERROR;
	    }
	    dst.push_back(logline(offset,
				  line_time,
				  0,
				  ll));
	    retval = true;
	}

	return retval;
    };

    auto_ptr<log_format> specialized() {
	auto_ptr<log_format> retval((log_format *)new access_log_format(*this));

	return retval;
    };
};

log_format::register_root_format<access_log_format> access_log_instance;

class syslog_log_format : public log_format {

    static pcrepp &scrub_pattern(void) {
        static pcrepp SCRUB_PATTERN("(\\w+\\s[\\s\\d]\\d \\d+:\\d+:\\d+) [\\.\\-\\w]+( .*)");

        return SCRUB_PATTERN;
    }

    static pcrepp &error_pattern(void) {
        static pcrepp ERROR_PATTERN("(?:failed|failure|error)", PCRE_CASELESS);

        return ERROR_PATTERN;
    }

    static pcrepp &warning_pattern(void) {
        static pcrepp WARNING_PATTERN(
                "(?:warn|not responding|init: cannot execute)", PCRE_CASELESS);

        return WARNING_PATTERN;
    }

    string get_name() { return "syslog_log"; };

    void scrub(string &line) {
        pcre_context_static<30> context;
        pcre_input pi(line);
        string new_line = "";

        if (scrub_pattern().match(context, pi)) {
            pcre_context::capture_t *cap;

            for (cap = context.begin(); cap != context.end(); cap++) {
                new_line += pi.get_substr(cap);
            }

            line = new_line;
        }
    };

    bool scan(vector < logline > &dst,
	      off_t offset,
	      char *prefix,
	      int len) {
	bool      retval = false;
	struct tm log_time;
	short     millis = 0;
	time_t    now;
	char      *rest;

	now      = time(NULL);
	localtime_r(&now, &log_time);

	log_time.tm_isdst = 0;

	if ((rest = strptime(prefix,
			     "%b %d %H:%M:%S",
			     &log_time)) != NULL) {
            pcre_context_static<20> context;
            pcre_input pi(prefix, 0, len);
	    logline::level_t ll = logline::LEVEL_UNKNOWN;
	    time_t           log_gmt;

	    if (error_pattern().match(context, pi)) {
		ll = logline::LEVEL_ERROR;
	    }
	    else if (warning_pattern().match(context, pi)) {
		ll = logline::LEVEL_WARNING;
	    }
	    log_gmt = tm2sec(&log_time);
	    if (!dst.empty() &&
		((dst.back().get_time() - log_gmt) > (24 * 60 * 60))) {
		vector<logline>::iterator iter;

		for (iter = dst.begin(); iter != dst.end(); iter++) {
		    time_t    ot = iter->get_time();
		    struct tm *otm;

		    otm           = gmtime(&ot);
		    otm->tm_year -= 1;
		    iter->set_time(tm2sec(otm));
		}
	    }
	    dst.push_back(logline(offset, log_gmt, millis, ll));

	    retval = true;
	}

	return retval;
    };

    auto_ptr<log_format> specialized() {
	auto_ptr<log_format> retval((log_format *)new syslog_log_format(*this));

	return retval;
    };
};

log_format::register_root_format<syslog_log_format> syslog_instance;

class tcsh_history_format : public log_format {
    string get_name() { return "tcsh_history"; };

    bool scan(vector < logline > &dst,
	      off_t offset,
	      char *prefix,
	      int len) {
	bool   retval = false;
	time_t log_time;
	int log_time_int;

	if (sscanf(prefix, "#+%d", &log_time_int) == 1) {
	    struct tm log_tm;

	    log_time = log_time_int;
	    /*
	     * NB: We convert any displayed dates to gm time, so we need to
	     * convert this time to local and then back to gmt.
	     */
	    memset(&log_tm, 0, sizeof(log_tm));
	    log_tm = *localtime( &log_time);
	    log_tm.tm_zone = NULL;
	    log_tm.tm_isdst = 0;

	    dst.push_back(logline(offset,
				  tm2sec(&log_tm),
				  0,
				  logline::LEVEL_UNKNOWN));

	    retval = true;
	}

	return retval;
    };

    auto_ptr<log_format> specialized() {
	auto_ptr<log_format> retval((log_format *)
				    new tcsh_history_format(*this));

	return retval;
    };
};

log_format::register_root_format<tcsh_history_format> tcsh_instance;

class generic_log_format : public log_format {
    static pcrepp &scrub_pattern(void) {
        static pcrepp SCRUB_PATTERN("\\d+-(\\d+-\\d+ \\d+:\\d+:\\d+(?:,\\d+)?:)\\w+:(.*)");

        return SCRUB_PATTERN;
    }

    string get_name() { return "generic_log"; };

    void scrub(string &line) {
        pcre_context_static<30> context;
        pcre_input pi(line);
        string new_line = "";

        if (scrub_pattern().match(context, pi)) {
            pcre_context::capture_t *cap;

            for (cap = context.begin(); cap != context.end(); cap++) {
                new_line += scrub_rdns(pi.get_substr(cap));
            }

            line = new_line;
        }
    };

    bool scan(vector < logline > &dst,
	      off_t offset,
	      char *prefix,
	      int len) {
	static const char *log_fmt[] = {
	    "%63[0-9: ,.-]%63[^:]",
            "%63[a-zA-Z0-9:-+/.] [%*x %63[^\n]",
            "%63[a-zA-Z0-9:.,-] %63[^\n]",
	    "%63[a-zA-Z0-9: .,-] [%*[^]]]%63[^:]",
	    "%63[a-zA-Z0-9: .,-] %63[^\n]",
	    "[%63[0-9: .-] %*s %63[^\n]",
	    "[%63[a-zA-Z0-9: -+/]] %63[^\n]",
	    "[%63[a-zA-Z0-9: -+/]] [%63[a-zA-Z]]",
	    "[%63[a-zA-Z0-9: .-+/] %*s %63[^\n]",
	    "[%63[a-zA-Z0-9: -+/]] (%*d) %63[^\n]",
	    NULL
	};

	bool retval = false;
	struct tm log_time;
	char timestr[64 + 32];
	time_t line_time;
	char level[64];
	char *last_pos;

	if ((last_pos = this->log_scanf(prefix,
					log_fmt,
					2,
					NULL,
					timestr,
					&log_time,
					line_time,

					timestr,
					level)) != NULL) {
	    uint16_t millis = 0;

	    /* Try to pull out the milliseconds value. */
	    if (last_pos[0] == ',' || last_pos[0] == '.') {
	    	sscanf(last_pos + 1, "%hd", &millis);
	    	if (millis >= 1000)
		    millis = 0;
	    }
	    dst.push_back(logline(offset,
				  line_time,
				  millis,
				  logline::string2level(level)));
	    retval = true;
	}

	return retval;
    };

    auto_ptr<log_format> specialized() {
	auto_ptr<log_format> retval((log_format *)
				    new generic_log_format(*this));

	return retval;
    };
};

log_format::register_root_format<generic_log_format> generic_log_instance;

class glog_log_format : public log_format {
    string get_name() { return "glog_log"; };

    bool scan(vector < logline > &dst,
	      off_t offset,
	      char *prefix,
	      int len) {
	bool      retval = false;
	struct tm log_time;
	short     millis = 0;
	time_t    now;
	char      *rest;

	now      = time(NULL);
	localtime_r(&now, &log_time);

	log_time.tm_isdst = 0;

	if ((rest = strptime(prefix + 1,
			     "%m%d %H:%M:%S.",
			     &log_time)) != NULL) {
	    logline::level_t ll = logline::LEVEL_UNKNOWN;
	    time_t           log_gmt;

	    millis = atoi(rest) / 1000;

	    switch (*prefix) {
		case 'I': // info
		    ll = logline::LEVEL_INFO;
		    break;
		case 'W': // warning
		    ll = logline::LEVEL_WARNING;
		    break;
		case 'E': // error
		    ll = logline::LEVEL_ERROR;
		    break;
		case 'C': // critical
		    ll = logline::LEVEL_CRITICAL;
		    break;
		case 'F': // fatal
		    ll = logline::LEVEL_CRITICAL;
		    break;
	    }
	    log_gmt = tm2sec(&log_time);
	    dst.push_back(logline(offset, log_gmt, millis, ll));

	    retval = true;
	}

	return retval;
    };

    auto_ptr<log_format> specialized() {
	auto_ptr<log_format> retval((log_format *)new glog_log_format(*this));

	return retval;
    };
};

log_format::register_root_format<glog_log_format> glog_instance;

class strace_log_format : public log_format {
    string get_name() { return "strace_log"; };

    bool scan(vector < logline > &dst,
	      off_t offset,
	      char *prefix,
	      int len) {
	static const char *log_fmt[] = {
	    "%63[0-9:].%d",
	    NULL
	};

	static const char *time_fmt[] = {
	    "%H:%M:%S",
	    NULL
	};

	bool retval = false;
	struct tm log_time;
	char timestr[64];
	time_t line_time;
	int usecs;

	if (this->log_scanf(prefix,
			    log_fmt,
			    2,
			    time_fmt,
			    timestr,
			    &log_time,
			    line_time,

			    timestr,
			    &usecs)) {
	    logline::level_t level = logline::LEVEL_UNKNOWN;
	    const char *eq;

	    if ((eq = strrchr(prefix, '=')) != NULL) {
		int rc;

		if (sscanf(eq, "= %d", &rc) == 1 && rc < 0) {
		    level = logline::LEVEL_ERROR;
		}
	    }

	    if (!dst.empty() && (line_time < dst.back().get_time())) {
		line_time += (24 * 60 * 60);
	    }
	    dst.push_back(logline(offset,
				  line_time,
				  usecs / 1000,
				  level));
	    retval = true;
	}

	return retval;
    };

    auto_ptr<log_format> specialized() {
	auto_ptr<log_format> retval((log_format *)
				    new strace_log_format(*this));

	return retval;
    };
};

log_format::register_root_format<strace_log_format> strace_log_instance;
