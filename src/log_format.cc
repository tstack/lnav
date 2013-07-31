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

static bool next_format(const std::vector<external_log_format::pattern> &patterns,
                        int &index,
                        int &locked_index)
{
    bool retval = true;

    if (locked_index == -1) {
        index += 1;
        if (index >= (int)patterns.size()) {
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

const char *log_format::log_scanf(const char *line,
                                  const char *fmt[],
                                  int expected_matches,
                                  const char *time_fmt[],
                                  char *time_dest,
                                  struct tm *tm_out,
                                  struct timeval &tv_out,
                                  ...)
{
    int     curr_fmt = -1;
    const char *  retval   = NULL;
    va_list args;

    while (next_format(fmt, curr_fmt, this->lf_fmt_lock)) {
        va_start(args, tv_out);
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
            retval = this->lf_date_time.scan(time_dest, time_fmt, tm_out, tv_out);

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
    pcre_context_static<128> pc;
    bool retval = false;
    int curr_fmt = -1;

    while (next_format(this->elf_patterns, curr_fmt, this->lf_fmt_lock)) {
        if (!this->elf_patterns[curr_fmt].p_pcre->match(pc, pi)) {
            continue;
        }

        pcre_context::capture_t *ts = pc["timestamp"];
        pcre_context::capture_t *level_cap = pc[this->elf_level_field];
        const char *ts_str = pi.get_substr_start(ts);
        const char *last;
        struct tm log_time_tm;
        struct timeval log_tv;
        logline::level_t level = logline::LEVEL_INFO;

        if ((last = this->lf_date_time.scan(ts_str,
                                            NULL,
                                            &log_time_tm,
                                            log_tv)) == NULL) {
            continue;
        }

        if (level_cap != NULL && level_cap->c_begin != -1) {
            pcre_context_static<128> pc_level;
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

        if (!dst.empty() &&
            ((dst.back().get_time() - log_tv.tv_sec) > (24 * 60 * 60))) {
            vector<logline>::iterator iter;

            for (iter = dst.begin(); iter != dst.end(); iter++) {
                time_t     ot = iter->get_time();
                struct tm *otm;

                otm           = gmtime(&ot);
                otm->tm_year -= 1;
                iter->set_time(tm2sec(otm));
            }
        }

        dst.push_back(logline(offset, log_tv, level));

        this->lf_fmt_lock = curr_fmt;
        retval = true;
        break;
    }

    return retval;
}

void external_log_format::annotate(const std::string &line,
                                   string_attrs_t &sa,
                                   std::vector<logline_value> &values) const
{
    pcre_context_static<128> pc;
    pcre_input pi(line);
    struct line_range lr;
    pcre_context::capture_t *cap;

    if (!this->elf_patterns[this->lf_fmt_lock].p_pcre->match(pc, pi)) {
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

    for (size_t lpc = 0; lpc < this->elf_patterns[this->lf_fmt_lock].p_value_by_index.size(); lpc++) {
        const value_def &vd = this->elf_patterns[this->lf_fmt_lock].p_value_by_index[lpc];
        const struct scaling_factor *scaling = NULL;

        if (vd.vd_unit_field_index >= 0) {
            pcre_context::iterator unit_cap = pc[vd.vd_unit_field_index];

            if (unit_cap != NULL && unit_cap->c_begin != -1) {
                std::string unit_val = pi.get_substr(unit_cap);
                std::map<string, scaling_factor>::const_iterator unit_iter;

                unit_iter = vd.vd_unit_scaling.find(unit_val);
                if (unit_iter != vd.vd_unit_scaling.end()) {
                    const struct scaling_factor &sf = unit_iter->second;

                    scaling = &sf;
                }
            }
        }

        values.push_back(logline_value(vd.vd_name,
                         vd.vd_kind,
                         pi.get_substr(pc[vd.vd_index]),
                         vd.vd_identifier,
                         scaling));

        if (pc[vd.vd_index]->c_begin != -1 && vd.vd_identifier) {
            lr.lr_start = pc[vd.vd_index]->c_begin;
            lr.lr_end = pc[vd.vd_index]->c_end;
            sa[lr].insert(make_string_attr("style", vc.attrs_for_ident(pi.get_substr_start(pc[vd.vd_index]), lr.length())));
        }
    }
}

void external_log_format::build(std::vector<std::string> &errors)
{
    for (std::vector<pattern>::iterator iter = this->elf_patterns.begin();
         iter != this->elf_patterns.end();
         ++iter) {
        try {
            iter->p_pcre = new pcrepp(iter->p_string.c_str());
        }
        catch (const pcrepp::error &e) {
            errors.push_back("error:" +
                             this->elf_name + ".regex[]" +
                             ":" +
                             e.what());
            continue;
        }
        for (pcre_named_capture::iterator name_iter = iter->p_pcre->named_begin();
             name_iter != iter->p_pcre->named_end();
             ++name_iter) {
            std::map<std::string, value_def>::iterator value_iter;

            value_iter = this->elf_value_defs.find(std::string(name_iter->pnc_name));
            if (value_iter != this->elf_value_defs.end()) {
                value_iter->second.vd_index = name_iter->index();
                value_iter->second.vd_unit_field_index = iter->p_pcre->name_index(value_iter->second.vd_unit_field.c_str());
                iter->p_value_by_index.push_back(value_iter->second);
            }
        }

        stable_sort(iter->p_value_by_index.begin(),
                    iter->p_value_by_index.end());
    }

    if (this->elf_patterns.empty()) {
        errors.push_back("error:" +
                         this->elf_name +
                         ": no regexes specified for format");
    }

    for (std::map<logline::level_t, level_pattern>::iterator iter = this->elf_level_patterns.begin();
         iter != this->elf_level_patterns.end();
         ++iter) {
        try {
            iter->second.lp_pcre = new pcrepp(iter->second.lp_regex.c_str());
        }
        catch (const pcrepp::error &e) {
            errors.push_back("error:" +
                             this->elf_name + ".level:" + e.what());
        }
    }

    if (this->elf_samples.empty()) {
        errors.push_back("error:" +
            this->elf_name +
            ":no sample logs provided, all formats must have samples");
    }

    for (std::vector<sample>::iterator iter = this->elf_samples.begin();
         iter != this->elf_samples.end();
         ++iter) {
        pcre_context_static<128> pc;
        pcre_input pi(iter->s_line);
        bool found = false;

        for (std::vector<pattern>::iterator pat_iter = this->elf_patterns.begin();
             pat_iter != this->elf_patterns.end();
             ++pat_iter) {
            if (!pat_iter->p_pcre)
                continue;

            if (pat_iter->p_pcre->match(pc, pi)) {
                found = true;
                break;
            }
        }

        if (!found) {
            errors.push_back("error:" +
                             this->elf_name +
                             ":invalid sample -- " +
                             iter->s_line);

            for (std::vector<pattern>::iterator pat_iter = this->elf_patterns.begin();
                 pat_iter != this->elf_patterns.end();
                 ++pat_iter) {
                if (!pat_iter->p_pcre)
                    continue;

                std::string line_partial = iter->s_line;

                while (!line_partial.empty()) {
                    pcre_input pi_partial(line_partial);

                    if (pat_iter->p_pcre->match(pc, pi_partial, PCRE_PARTIAL)) {
                        errors.push_back("error:" +
                                         this->elf_name +
                                         ":partial sample matched -- " +
                                         line_partial);
                        break;
                    }

                    line_partial = line_partial.substr(0, line_partial.size() - 1);
                }
                if (line_partial.empty()) {
                    errors.push_back("error:" +
                                     this->elf_name +
                                     ":no partial match found");
                }
            }
        }
    }
}

class external_log_table : public log_vtab_impl {
public:
    external_log_table(const external_log_format &elf) :
        log_vtab_impl(elf.get_name()), elt_format(elf) {
    };

    void get_columns(vector<vtab_column> &cols) {
        std::vector<external_log_format::value_def>::const_iterator iter;
        const external_log_format &elf = this->elt_format;

        for (iter = elf.elf_patterns[0].p_value_by_index.begin();
             iter != elf.elf_patterns[0].p_value_by_index.end();
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

    void get_foreign_keys(std::vector<std::string> &keys_inout)
    {
        std::map<std::string, external_log_format::value_def>::const_iterator iter;

        log_vtab_impl::get_foreign_keys(keys_inout);

        for (iter = this->elt_format.elf_value_defs.begin();
             iter != this->elt_format.elf_value_defs.end();
             ++iter) {
            if (iter->second.vd_foreign_key) {
                keys_inout.push_back(iter->first);
            }
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
