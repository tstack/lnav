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

#include "pcrepp.hh"
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
    string     retval;

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

class generic_log_format : public log_format {
    static pcrepp &scrub_pattern(void)
    {
        static pcrepp SCRUB_PATTERN(
            "\\d+-(\\d+-\\d+ \\d+:\\d+:\\d+(?:,\\d+)?:)\\w+:(.*)");

        return SCRUB_PATTERN;
    }

    static pcre_format *get_pcre_log_formats() {
        static pcre_format log_fmt[] = {
            pcre_format("^(?<timestamp>[\\dTZ: +/\\-,\\.-]+)([^:]+)"),
            pcre_format("^(?<timestamp>[\\w:+/\\.-]+) \\[\\w (.*)"),
            pcre_format("^(?<timestamp>[\\w:,/\\.-]+) (.*)"),
            pcre_format("^(?<timestamp>[\\w: \\.,/-]+)\\[[^\\]]+\\](.*)"),
            pcre_format("^(?<timestamp>[\\w: \\.,/-]+) (.*)"),

            pcre_format("^\\[(?<timestamp>[\\w: \\.,+/-]+)\\]\\s*(\\w+):?"),
            pcre_format("^\\[(?<timestamp>[\\w: \\.,+/-]+)\\] (.*)"),
            pcre_format("^\\[(?<timestamp>[\\w: \\.,+/-]+)\\] \\[(\\w+)\\]"),
            pcre_format("^\\[(?<timestamp>[\\w: \\.,+/-]+)\\] \\w+ (.*)"),
            pcre_format("^\\[(?<timestamp>[\\w: ,+/-]+)\\] \\(\\d+\\) (.*)"),

            pcre_format()
        };

        return log_fmt;
    };

    std::string get_pattern_regex() const {
        return get_pcre_log_formats()[this->lf_fmt_lock].name;
    }

    intern_string_t get_name() const {
        return intern_string::lookup("generic_log");
    };

    void scrub(string &line)
    {
        pcre_context_static<30> context;
        pcre_input pi(line);
        string     new_line = "";

        if (scrub_pattern().match(context, pi)) {
            pcre_context::capture_t *cap;

            for (cap = context.begin(); cap != context.end(); cap++) {
                new_line += scrub_rdns(pi.get_substr(cap));
            }

            line = new_line;
        }
    };

    scan_result_t scan(vector<logline> &dst,
                       off_t offset,
                       shared_buffer_ref &sbr)
    {
        struct exttm log_time;
        struct timeval log_tv;
        pcre_context::capture_t ts, level;
        const char *last_pos;

        if ((last_pos = this->log_scanf(
                sbr.get_data(),
                sbr.length(),
                get_pcre_log_formats(),
                NULL,
                &log_time,
                &log_tv,

                &ts,
                &level)) != NULL) {
            const char *level_str = &sbr.get_data()[level.c_begin];
            logline::level_t level_val = logline::string2level(
                    level_str, level.length());

            this->check_for_new_year(dst, log_time, log_tv);

            dst.push_back(logline(offset, log_tv, level_val));
            return SCAN_MATCH;
        }

        return SCAN_NO_MATCH;
    };

    void annotate(shared_buffer_ref &line,
                  string_attrs_t &sa,
                  std::vector<logline_value> &values,
                  bool annotate_module) const
    {
        pcre_format &fmt = get_pcre_log_formats()[this->lf_fmt_lock];
        struct line_range lr;
        int prefix_len = 0;
        pcre_input pi(line.get_data(), 0, line.length());
        pcre_context_static<30> pc;

        if (!fmt.pcre.match(pc, pi)) {
            return;
        }

        lr.lr_start = pc[0]->c_begin;
        lr.lr_end   = pc[0]->c_end;
        sa.push_back(string_attr(lr, &logline::L_TIMESTAMP));

        const char *level = &line.get_data()[pc[1]->c_begin];

        if (logline::string2level(level, pc[1]->length(), true) == logline::LEVEL_UNKNOWN) {
            prefix_len = pc[0]->c_end;
        }
        else {
            prefix_len = pc[1]->c_end;
        }

        lr.lr_start = 0;
        lr.lr_end   = prefix_len;
        sa.push_back(string_attr(lr, &logline::L_PREFIX));

        lr.lr_start = prefix_len;
        lr.lr_end   = line.length();
        sa.push_back(string_attr(lr, &textview_curses::SA_BODY));
    };

    auto_ptr<log_format> specialized(int fmt_lock)
    {
        auto_ptr<log_format> retval((log_format *)
                                    new generic_log_format(*this));

        return retval;
    };
};

log_format::register_root_format<generic_log_format> generic_log_instance;
