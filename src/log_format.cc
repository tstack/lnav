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
#include "log_vtab_impl.hh"

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
        if (t->tm_zone) {
            days -= t->tm_gmtoff;
        }
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

static int strcasestr_i(const char *s1, const char *s2)
{
    return strcasestr(s1, s2) == NULL;
}

logline::level_t logline::string2level(const char *levelstr, bool exact)
{
    logline::level_t retval = logline::LEVEL_UNKNOWN;

    int (*cmpfunc)(const char *, const char *);

    if (exact) {
        cmpfunc = strcasecmp;
    }
    else{
        cmpfunc = strcasestr_i;
    }

    if (cmpfunc(levelstr, "TRACE") == 0) {
        retval = logline::LEVEL_TRACE;
    }
    else if (cmpfunc(levelstr, "VERBOSE") == 0) {
        retval = logline::LEVEL_DEBUG;
    }
    else if (cmpfunc(levelstr, "DEBUG") == 0) {
        retval = logline::LEVEL_DEBUG;
    }
    else if (cmpfunc(levelstr, "INFO") == 0) {
        retval = logline::LEVEL_INFO;
    }
    else if (cmpfunc(levelstr, "WARNING") == 0) {
        retval = logline::LEVEL_WARNING;
    }
    else if (cmpfunc(levelstr, "ERROR") == 0) {
        retval = logline::LEVEL_ERROR;
    }
    else if (cmpfunc(levelstr, "CRITICAL") == 0) {
        retval = logline::LEVEL_CRITICAL;
    }
    else if (cmpfunc(levelstr, "FATAL") == 0) {
        retval = logline::LEVEL_FATAL;
    }

    return retval;
}

logline_value::kind_t logline_value::string2kind(const char *kindstr)
{
    if (strcmp(kindstr, "string") == 0) {
        return VALUE_TEXT;
    }
    else if (strcmp(kindstr, "integer") == 0) {
        return VALUE_INTEGER;
    }
    else if (strcmp(kindstr, "float") == 0) {
        return VALUE_FLOAT;
    }

    return VALUE_UNKNOWN;
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

static const char *std_time_fmt[] = {
    "%Y-%m-%d %H:%M:%S",
    "%Y-%m-%d %H:%M",
    "%Y-%m-%dT%H:%M:%S",
    "%Y-%m-%dT%H:%M:%SZ",
    "%Y/%m/%d %H:%M:%S",
    "%Y/%m/%d %H:%M",

    "%a %b %d %H:%M:%S %Y",
    "%a %b %d %H:%M:%S %Z %Y",

    "%d/%b/%Y:%H:%M:%S %z",

    "%b %d %H:%M:%S",

    NULL,
};

const char *log_format::time_scanf(const char *time_dest,
                                   const char *time_fmt[],
                                   struct tm *tm_out,
                                   time_t &time_out)
{
    int  curr_time_fmt = -1;
    bool found         = false;
    const char *retval = NULL;

    if (!time_fmt) {
        time_fmt = std_time_fmt;
    }

    while (next_format(time_fmt,
                       curr_time_fmt,
                       this->lf_time_fmt_lock)) {
        memset(tm_out, 0, sizeof(struct tm));
        if ((retval = strptime(time_dest,
            time_fmt[curr_time_fmt],
            tm_out)) != NULL) {
            if (tm_out->tm_year < 70) {
                /* XXX We should pull the time from the file mtime (?) */
                tm_out->tm_year = 80;
            }
            time_out = tm2sec(tm_out);

            // this->lf_fmt_lock      = curr_fmt;
            this->lf_time_fmt_lock = curr_time_fmt;
            this->lf_time_fmt_len  = retval - time_dest;

            found = true;
            break;
        }
    }

    if (!found) {
        retval = NULL;
    }

    return retval;
}

const char *log_format::log_scanf(const char *line,
                                  const char *fmt[],
                                  int expected_matches,
                                  const char *time_fmt[],
                                  char *time_dest,
                                  struct tm *tm_out,
                                  time_t &time_out,
                                  ...)
{
    int     curr_fmt = -1;
    const char *  retval   = NULL;
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
            retval = this->time_scanf(time_dest, time_fmt, tm_out, time_out);

            if (retval) {
                this->lf_fmt_lock = curr_fmt;
                break;
            }
        }

        va_end(args);
    }

    return retval;
}

bool external_log_format::scan(std::vector<logline> &dst,
                               off_t offset,
                               char *prefix,
                               int len)
{
    pcre_input pi(prefix, 0, len);
    pcre_context_static<30> pc;
    bool retval = false;

    if (this->elf_pcre->match(pc, pi)) {
        pcre_context::capture_t *ts = pc["timestamp"];
        pcre_context::capture_t *level_cap = pc[this->elf_level_field];
        const char *ts_str = pi.get_substr_start(ts);
        const char *last;
        time_t line_time;
        struct tm log_time;
        uint16_t millis = 0;
        logline::level_t level = logline::LEVEL_INFO;

        if ((last = this->time_scanf(ts_str,
                                     NULL,
                                     &log_time,
                                     line_time)) == NULL) {
            return false;
        }

        /* Try to pull out the milliseconds value. */
        if (last[0] == ',' || last[0] == '.') {
            int subsec_len = 0;

            sscanf(last + 1, "%hd%n", &millis, &subsec_len);
            if (millis >= 1000) {
                millis = 0;
            }
        }

        if (level_cap != NULL && level_cap->c_begin != -1) {
            pcre_context_static<30> pc_level;
            pcre_input pi_level(pi.get_substr_start(level_cap),
                                0,
                                level_cap->length());

            for (std::map<logline::level_t, level_pattern>::iterator iter = this->elf_level_patterns.begin();
                 iter != this->elf_level_patterns.end();
                 ++iter) {
                if (iter->second.lp_pcre->match(pc_level, pi_level)) {
                    level = iter->first;
                    break;
                }
            }
        }

        dst.push_back(logline(offset,
                      line_time,
                      millis,
                      level));

        retval = true;
    }

    return retval;
}

void external_log_format::annotate(const std::string &line,
                                   string_attrs_t &sa,
                                   std::vector<logline_value> &values) const
{
    pcre_context_static<30> pc;
    pcre_input pi(line);
    struct line_range lr;
    pcre_context::capture_t *cap;

    if (!this->elf_pcre->match(pc, pi)) {
        return;
    }

    cap = pc["timestamp"];
    lr.lr_start = cap->c_begin;
    lr.lr_end = cap->c_end;
    sa[lr].insert(make_string_attr("timestamp", 0));

    cap = pc["body"];
    if (cap != NULL && cap->c_begin != -1) {
        lr.lr_start = cap->c_begin;
        lr.lr_end = cap->c_end;
    }
    else {
        lr.lr_start = line.size();
        lr.lr_end = line.size();
    }
    sa[lr].insert(make_string_attr("body", 0));

    view_colors &vc = view_colors::singleton();

    for (size_t lpc = 0; lpc < this->elf_value_by_index.size(); lpc++) {
        const value_def &vd = this->elf_value_by_index[lpc];

        values.push_back(logline_value(vd.vd_name,
                         vd.vd_kind,
                         pi.get_substr(pc[vd.vd_index]),
                         vd.vd_identifier));

        if (pc[vd.vd_index]->c_begin != -1 && vd.vd_identifier) {
            fprintf(stderr, "ident %s \n", vd.vd_name.c_str());
            lr.lr_start = pc[vd.vd_index]->c_begin;
            lr.lr_end = pc[vd.vd_index]->c_end;
            sa[lr].insert(make_string_attr("style", vc.attrs_for_ident(pi.get_substr_start(pc[vd.vd_index]), lr.length())));
        }
    }
}

void external_log_format::build(void)
{
    this->elf_pcre = new pcrepp(this->elf_regex.c_str());
    for (std::map<logline::level_t, level_pattern>::iterator iter = this->elf_level_patterns.begin();
         iter != this->elf_level_patterns.end();
         ++iter) {
        iter->second.lp_pcre = new pcrepp(iter->second.lp_regex.c_str());
    }

    for (pcre_named_capture::iterator iter = this->elf_pcre->named_begin();
         iter != this->elf_pcre->named_end();
         ++iter) {
        std::map<std::string, value_def>::iterator value_iter;

        value_iter = this->elf_value_defs.find(std::string(iter->pnc_name));
        if (value_iter != this->elf_value_defs.end()) {
            value_iter->second.vd_index = iter->index();
            this->elf_value_by_index.push_back(value_iter->second);
        }
    }

    stable_sort(this->elf_value_by_index.begin(),
                this->elf_value_by_index.end());
}

class external_log_table : public log_vtab_impl {
public:
    external_log_table(const external_log_format &elf) :
        log_vtab_impl(elf.get_name()), elt_format(elf) {
    };

    void get_columns(vector<vtab_column> &cols) {
        std::vector<external_log_format::value_def>::const_iterator iter;

        for (iter = this->elt_format.elf_value_by_index.begin();
             iter != this->elt_format.elf_value_by_index.end();
             ++iter) {
            int type;

            switch (iter->vd_kind) {
            case logline_value::VALUE_TEXT:
                type = SQLITE3_TEXT;
                break;
            case logline_value::VALUE_FLOAT:
                type = SQLITE_FLOAT;
                break;
            case logline_value::VALUE_INTEGER:
                type = SQLITE_INTEGER;
                break;
            case logline_value::VALUE_UNKNOWN:
                assert(0);
                break;
            }
            cols.push_back(vtab_column(iter->vd_name.c_str(),
                           type,
                           iter->vd_collate.c_str()));
        }
    };

    const external_log_format &elt_format;
};

log_vtab_impl *external_log_format::get_vtab_impl(void) const
{
    return new external_log_table(*this);
}

/* XXX */
#include "log_format_impls.cc"
