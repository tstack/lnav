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

    static const char **get_log_formats()
    {
        static const char *log_fmt[] = {
            "%63[0-9TZ: ,.-]%63[^:]%n",
            "%63[a-zA-Z0-9:-+/.] [%*x %63[^\n]%n",
            "%63[a-zA-Z0-9:.,-/] %63[^\n]%n",
            "%63[a-zA-Z0-9: .,-/] [%*[^]]]%63[^:]%n",
            "%63[a-zA-Z0-9: .,-/] %63[^\n]%n",
            "[%63[0-9: .-] %*s %63[^\n]%n",
            "[%63[a-zA-Z0-9: -+/]] %63[^\n]%n",
            "[%63[a-zA-Z0-9: -+/]] [%63[a-zA-Z]]%n",
            "[%63[a-zA-Z0-9: .-+/] %*s %63[^\n]%n",
            "[%63[a-zA-Z0-9: -+/]] (%*d) %63[^\n]%n",
            NULL
        };

        return log_fmt;
    };

    struct pcre_format {
        const char *name;
        pcrepp pcre;
    };

    static pcre_format *get_pcre_log_formats() {
        static pcre_format log_fmt[] = {
            { "", pcrepp("([\\dTZ: ,\\.-]+)([^:]+)") },
            { "", pcrepp("([\\w:+/\\.-]+) \\[\\w (.*)") },
            { "", pcrepp("([\\w:,/\\.-]+) (.*)") },
            { "", pcrepp("([\\w: \\.,/-]+) \\[[^\\]+](.*)") },
            { "", pcrepp("([\\w: \\.,/-]+) (.*)") },

            { "", pcrepp("\\[([\\d: \\.-]+) \\w+ (.*)") },
            { "", pcrepp("\\[([\\w: +/-]+)\\] (.*)") },
            { "", pcrepp("\\[([\\w: +/-]+)\\] \\[(\\w+)\\]") },
            { "", pcrepp("\\[([\\w: \\.+/-]+)\\] \\w+ (.*)") },
            { "", pcrepp("\\[([\\w: +/-]+)\\] \\(\\d+\\) (.*)") },

            { NULL, pcrepp("") }
        };

        return log_fmt;
    };

    string get_name() const { return "generic_log"; };

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

    bool scan(vector<logline> &dst,
              off_t offset,
              char *prefix,
              int len)
    {
        bool      retval = false;
        struct tm log_time;
        char      timestr[64 + 32];
        struct timeval log_tv;
        char      level[64];
        const char *last_pos;
        int       prefix_len;

        if ((last_pos = this->log_scanf(prefix,
                                        get_log_formats(),
                                        2,
                                        NULL,
                                        timestr,
                                        &log_time,
                                        log_tv,

                                        timestr,
                                        level,
                                        &prefix_len)) != NULL) {
            uint16_t millis = 0;

            if (last_pos[0] == ',' || last_pos[0] == '.') {
                int subsec_len = 0;

                sscanf(last_pos + 1, "%hd%n", &millis, &subsec_len);
                if (millis >= 1000) {
                    millis = 0;
                }
                this->lf_date_time.dts_fmt_len += 1 + subsec_len;
            }
            dst.push_back(logline(offset,
                                  log_tv,
                                  logline::string2level(level)));
            retval = true;
        }

        return retval;
    };

    void annotate(shared_buffer_ref &line,
                  string_attrs_t &sa,
                  std::vector<logline_value> &values) const
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

    auto_ptr<log_format> specialized()
    {
        auto_ptr<log_format> retval((log_format *)
                                    new generic_log_format(*this));

        return retval;
    };
};

log_format::register_root_format<generic_log_format> generic_log_instance;

class strace_log_format : public log_format {
    static pcrepp &value_pattern(void)
    {
        static pcrepp VALUE_PATTERN(
            "([0-9:.]*) ([a-zA-Z_][a-zA-Z_0-9]*)\\("
            "(.*)\\)"
            "\\s+= ([-xa-fA-F\\d\\?]+)[^<]+(?:<(\\d+\\.\\d+)>)?");

        return VALUE_PATTERN;
    };

    string get_name() const { return "strace_log"; };

    bool scan(vector<logline> &dst,
              off_t offset,
              char *prefix,
              int len)
    {
        static const char *log_fmt[] = {
            "%63[0-9:].%d",
            NULL
        };

        static const char *time_fmt[] = {
            "%H:%M:%S",
            NULL
        };

        bool      retval = false;
        struct tm log_time;
        char      timestr[64];
        struct timeval log_tv;
        int       usecs;

        if (this->log_scanf(prefix,
                            log_fmt,
                            2,
                            time_fmt,
                            timestr,
                            &log_time,
                            log_tv,

                            timestr,
                            &usecs)) {
            logline::level_t level = logline::LEVEL_UNKNOWN;
            const char *     eq;

            if ((eq = strrchr(prefix, '=')) != NULL) {
                int rc;

                if (sscanf(eq, "= %d", &rc) == 1 && rc < 0) {
                    level = logline::LEVEL_ERROR;
                }
            }

            if (!dst.empty() && (log_tv.tv_sec < dst.back().get_time())) {
                log_tv.tv_sec += (24 * 60 * 60);
            }
            log_tv.tv_usec = usecs;
            dst.push_back(logline(offset, log_tv, level));
            retval = true;
        }

        return retval;
    };

    auto_ptr<log_format> specialized()
    {
        auto_ptr<log_format> retval((log_format *)
                                    new strace_log_format(*this));

        return retval;
    };

    void annotate(shared_buffer_ref &line,
                  string_attrs_t &sa,
                  std::vector<logline_value> &values) const
    {
        pcre_context_static<30> pc;
        pcre_input pi(line.get_data(), 0, line.length());

        if (value_pattern().match(pc, pi)) {
            static struct {
                const char *          name;
                logline_value::kind_t kind;
            } columns[] = {
                { "",         logline_value::VALUE_TEXT },
                { "funcname", logline_value::VALUE_TEXT },
                { "args",     logline_value::VALUE_TEXT },
                { "result",   logline_value::VALUE_TEXT },
                { "duration", logline_value::VALUE_FLOAT },

                { NULL, logline_value::VALUE_UNKNOWN },
            };

            pcre_context::iterator iter;
            struct line_range      lr;

            iter        = pc.begin();
            if (iter->c_begin != -1) {
                lr.lr_start = iter->c_begin;
                lr.lr_end   = iter->c_end;
                sa.push_back(string_attr(lr, &logline::L_TIMESTAMP));
            }

            lr.lr_start = 0;
            lr.lr_end   = line.length();
            sa.push_back(string_attr(lr, &logline::L_PREFIX));

            lr.lr_start = line.length();
            lr.lr_end   = line.length();
            sa.push_back(string_attr(lr, &textview_curses::SA_BODY));

            for (int lpc = 0; columns[lpc].name; lpc++) {
                pcre_context::iterator cap = pc.begin() + lpc;
                shared_buffer_ref value_str;

                if (columns[lpc].name[0] == '\0') {
                    continue;
                }
                value_str.subset(line, cap->c_begin, cap->length());
                values.push_back(logline_value(columns[lpc].name,
                                               columns[lpc].kind,
                                               value_str));
            }
        }
    };
};

log_format::register_root_format<strace_log_format> strace_log_instance;
