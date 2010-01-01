
#include "log_format.hh"
#include "log_vtab_impl.hh"

using namespace std;

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
    string get_name() { return "syslog_log"; };

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
	log_time = *localtime(&now);
	
	log_time.tm_isdst = 0;
	
	if ((rest = strptime(prefix,
			     "%b %d %H:%M:%S",
			     &log_time)) != NULL) {
	    logline::level_t ll = logline::LEVEL_UNKNOWN;
	    time_t           log_gmt;
	    
	    if (strcasestr(prefix, "failed") != NULL ||
		strcasestr(prefix, "failure") != NULL ||
		strcasestr(prefix, "error") != NULL) {
		ll = logline::LEVEL_ERROR;
	    }
	    else if (strcasestr(prefix, "warn") != NULL ||
		     strcasestr(prefix, "not responding") != NULL ||
		     strcasestr(prefix, "init: cannot execute") != NULL) {
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
	    memset(&log_tm, 0, sizeof(log_tm));
	    log_tm = *localtime( &log_time);
	    
	    log_tm.tm_isdst = 0;
	    dst.push_back(logline(offset,
				  mktime(&log_tm) /* - timezone XXX */,
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
    string get_name() { return "generic_log"; };

    bool scan(vector < logline > &dst,
	      off_t offset,
	      char *prefix,
	      int len) {
	static const char *log_fmt[] = {
	    "%63[a-zA-Z0-90-9: ,-] %15s",
	    "[%63[a-zA-Z0-9: -]] %15s",
	    "[%63[a-zA-Z0-9: -]] [%15[a-zA-Z]]",
	    "[%63[a-zA-Z0-90-9: .-] %*s %15s",
	    "[%63[a-zA-Z0-90-9: -]] (%*d) %15s",
	    NULL
	};
	
	bool retval = false;
	struct tm log_time;
	char timestr[64];
	time_t line_time;
	char level[16];
	
	if (this->log_scanf(prefix,
			    log_fmt,
			    2,
			    NULL,
			    timestr,
			    &log_time,
			    line_time,
			    
			    timestr,
			    level)) {
	    dst.push_back(logline(offset,
				  line_time,
				  0,
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
