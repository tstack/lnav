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

#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>

#include "yajlpp.hh"
#include "sql_util.hh"
#include "log_format.hh"
#include "log_vtab_impl.hh"
#include "ptimec.hh"

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

string_attr_type logline::L_PREFIX;
string_attr_type logline::L_TIMESTAMP;
string_attr_type logline::L_FILE;
string_attr_type logline::L_PARTITION;

const char *logline::level_names[LEVEL__MAX + 1] = {
    "unknown",
    "trace",
    "debug",
    "info",
    "warning",
    "error",
    "critical",
    "fatal",

    NULL
};

static pcrepp LEVEL_RE(
        "(?i)(TRACE|VERBOSE|DEBUG|INFO|WARN(?:ING)?|ERROR|CRITICAL|SEVERE|FATAL)");

static int strncasestr_i(const char *s1, const char *s2, size_t len)
{
    return strcasestr(s1, s2) == NULL;
}

logline::level_t logline::string2level(const char *levelstr, ssize_t len, bool exact)
{
    logline::level_t retval = logline::LEVEL_UNKNOWN;

    if (len == (size_t)-1) {
        len = strlen(levelstr);
    }

    if (((len == 1) || ((len > 1) && (levelstr[1] == ' '))) &&
        (retval = abbrev2level(levelstr, len)) != LEVEL_UNKNOWN) {
        return retval;
    }

    pcre_input pi(levelstr, 0, len);
    pcre_context_static<10> pc;

    if (LEVEL_RE.match(pc, pi)) {
        retval = abbrev2level(pi.get_substr_start(pc.begin()), 1);
    }

    return retval;
}

logline::level_t logline::abbrev2level(const char *levelstr, ssize_t len)
{
    if (len == 0 || levelstr[0] == '\0') {
        return LEVEL_UNKNOWN;
    }

    switch (toupper(levelstr[0])) {
        case 'T':
            return LEVEL_TRACE;
        case 'D':
        case 'V':
            return LEVEL_DEBUG;
        case 'I':
            return LEVEL_INFO;
        case 'W':
            return LEVEL_WARNING;
        case 'E':
            return LEVEL_ERROR;
        case 'C':
        case 'S':
            return LEVEL_CRITICAL;
        case 'F':
            return LEVEL_FATAL;
        default:
            return LEVEL_UNKNOWN;
    }
}

int logline::levelcmp(const char *l1, ssize_t l1_len, const char *l2, ssize_t l2_len)
{
    return abbrev2level(l1, l1_len) - abbrev2level(l2, l2_len);
}

const char *logline_value::value_names[VALUE__MAX] = {
    "null",
    "text",
    "int",
    "float",
    "bool",
    "json"
};

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
    else if (strcmp(kindstr, "boolean") == 0) {
        return VALUE_BOOLEAN;
    }
    else if (strcmp(kindstr, "json") == 0) {
        return VALUE_JSON;
    }
    else if (strcmp(kindstr, "quoted") == 0) {
        return VALUE_QUOTED;
    }

    return VALUE_UNKNOWN;
}

vector<log_format *> log_format::lf_root_formats;

vector<log_format *> &log_format::get_root_formats(void)
{
    return lf_root_formats;
}

static bool next_format(const std::vector<external_log_format::pattern *> &patterns,
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

bool log_format::next_format(pcre_format *fmt, int &index, int &locked_index)
{
    bool retval = true;

    if (locked_index == -1) {
        index += 1;
        if (fmt[index].name == NULL) {
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
                                  size_t len,
                                  pcre_format *fmt,
                                  const char *time_fmt[],
                                  struct exttm *tm_out,
                                  struct timeval &tv_out,
                                  ...)
{
    int     curr_fmt = -1;
    const char *  retval   = NULL;
    bool done = false;
    pcre_input pi(line, 0, len);
    pcre_context_static<128> pc;
    va_list args;

    while (!done && next_format(fmt, curr_fmt, this->lf_fmt_lock)) {
        va_start(args, tv_out);

        if (!fmt[curr_fmt].pcre.match(pc, pi)) {
            retval = NULL;
        }
        else {
            pcre_context::capture_t *ts = pc["timestamp"];

            for (pcre_context::iterator iter = pc.begin();
                    iter != pc.end();
                    ++iter) {
                pcre_context::capture_t *cap = va_arg(
                        args, pcre_context::capture_t *);

                *cap = *iter;
            }

            retval = this->lf_date_time.scan(
                    pi.get_substr_start(ts), ts->length(), NULL, tm_out, tv_out);

            if (retval) {
                this->lf_fmt_lock = curr_fmt;
                done = true;
            }
        }

        va_end(args);
    }

    return retval;
}

void log_format::check_for_new_year(std::vector<logline> &dst,
    const struct timeval &log_tv)
{
    if (dst.empty()) {
        return;
    }

    time_t diff = dst.back().get_time() - log_tv.tv_sec;

    if (diff > (5 * 60)) {
        int off_year = 0, off_month = 0, off_day = 0, off_hour = 0;
        std::vector<logline>::iterator iter;

        if (diff > (60 * 24 * 60 * 60)) {
            off_year = 1;
        } else if (diff > (15 * 24 * 60 * 60)) {
            off_month = 1;
        } else if (diff > (12 * 60 * 60)) {
            off_day = 1;
        } else {
            off_hour = 1;
        }

        for (iter = dst.begin(); iter != dst.end(); iter++) {
            time_t     ot = iter->get_time();
            struct tm *otm;

            otm           = gmtime(&ot);
            otm->tm_year -= off_year;
            otm->tm_mon  -= off_month;
            otm->tm_yday -= off_day;
            otm->tm_hour -= off_hour;
            iter->set_time(tm2sec(otm));
        }
    }
}

/*
 * XXX This needs some cleanup. 
 */
struct json_log_userdata {
    json_log_userdata(shared_buffer_ref &sbr)
            : jlu_sub_line_count(1), jlu_shared_buffer(sbr) {

    };

    external_log_format *jlu_format;
    const logline *jlu_line;
    logline *jlu_base_line;
    int jlu_sub_line_count;
    yajl_handle jlu_handle;
    const char *jlu_line_value;
    size_t jlu_line_size;
    size_t jlu_sub_start;
    shared_buffer_ref &jlu_shared_buffer;
};

struct json_field_cmp {
    json_field_cmp(external_log_format::json_log_field type,
                   const intern_string_t name)
        : jfc_type(type), jfc_field_name(name) {
    };

    bool operator()(const external_log_format::json_format_element &jfe) const {
        return (this->jfc_type == jfe.jfe_type &&
                this->jfc_field_name == jfe.jfe_value);
    };

    external_log_format::json_log_field jfc_type;
    const intern_string_t jfc_field_name;
};

static int read_json_field(yajlpp_parse_context *ypc, const unsigned char *str, size_t len);

static int read_json_null(yajlpp_parse_context *ypc)
{
    if (!ypc->is_level(1)) {
        return 1;
    }

    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    vector<external_log_format::json_format_element> &line_format =
        jlu->jlu_format->jlf_line_format;
    const intern_string_t field_name = ypc->get_path_fragment_i(0);

    if (find_if(line_format.begin(), line_format.end(),
                json_field_cmp(external_log_format::JLF_VARIABLE,
                               field_name)) == line_format.end()) {
        jlu->jlu_sub_line_count += 1;
    }

    return 1;
}

static int read_json_bool(yajlpp_parse_context *ypc, int val)
{
    if (!ypc->is_level(1)) {
        return 1;
    }

    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    vector<external_log_format::json_format_element> &line_format =
        jlu->jlu_format->jlf_line_format;
    const intern_string_t field_name = ypc->get_path_fragment_i(0);

    if (find_if(line_format.begin(), line_format.end(),
            json_field_cmp(external_log_format::JLF_VARIABLE,
                    field_name)) == line_format.end()) {
        jlu->jlu_sub_line_count += 1;
    }

    return 1;
}

static int read_json_int(yajlpp_parse_context *ypc, long long val)
{
    if (!ypc->is_level(1)) {
        return 1;
    }

    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    vector<external_log_format::json_format_element> &line_format =
        jlu->jlu_format->jlf_line_format;
    const intern_string_t field_name = ypc->get_path_fragment_i(0);

    if (jlu->jlu_format->lf_timestamp_field == field_name) {
        long long divisor = jlu->jlu_format->elf_timestamp_divisor;
        struct timeval tv;

        tv.tv_sec = val / divisor;
        tv.tv_usec = (val % divisor) * (1000000.0 / divisor);
        jlu->jlu_base_line->set_time(tv);
    }
    else if (find_if(line_format.begin(), line_format.end(),
                     json_field_cmp(external_log_format::JLF_VARIABLE,
                         field_name)) == line_format.end()) {
        jlu->jlu_sub_line_count += 1;
    }

    return 1;
}

static int read_json_double(yajlpp_parse_context *ypc, double val)
{
    if (!ypc->is_level(1)) {
        return 1;
    }

    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    vector<external_log_format::json_format_element> &line_format =
        jlu->jlu_format->jlf_line_format;
    const intern_string_t field_name = ypc->get_path_fragment_i(0);

    if (jlu->jlu_format->lf_timestamp_field == field_name) {
        double divisor = jlu->jlu_format->elf_timestamp_divisor;
        struct timeval tv;

        tv.tv_sec = val / divisor;
        tv.tv_usec = fmod(val, divisor) * (1000000.0 / divisor);
        jlu->jlu_base_line->set_time(tv);
    }
    else if (find_if(line_format.begin(), line_format.end(),
                     json_field_cmp(external_log_format::JLF_VARIABLE,
                         field_name)) == line_format.end()) {
        jlu->jlu_sub_line_count += 1;
    }

    return 1;
}

static int json_array_start(void *ctx)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;
    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    vector<external_log_format::json_format_element> &line_format =
        jlu->jlu_format->jlf_line_format;

    if (ypc->ypc_path_index_stack.size() == 2) {
        const intern_string_t field_name = ypc->get_path_fragment_i(0);

        if (find_if(line_format.begin(), line_format.end(),
                    json_field_cmp(external_log_format::JLF_VARIABLE,
                                   field_name)) == line_format.end()) {
            jlu->jlu_sub_line_count += 1;
        }

        jlu->jlu_sub_start = yajl_get_bytes_consumed(jlu->jlu_handle) - 1;
    }

    return 1;
}

static int json_array_end(void *ctx)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;
    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;

    if (ypc->ypc_path_index_stack.size() == 1) {
        const intern_string_t field_name = ypc->get_path_fragment_i(0);
        size_t sub_end = yajl_get_bytes_consumed(jlu->jlu_handle);
        tmp_shared_buffer tsb(&jlu->jlu_line_value[jlu->jlu_sub_start],
            sub_end - jlu->jlu_sub_start);

        jlu->jlu_format->jlf_line_values.push_back(
            logline_value(field_name, tsb.tsb_ref));
        jlu->jlu_format->jlf_line_values.back().lv_kind = logline_value::VALUE_JSON;
    }

    return 1;
}

static struct json_path_handler json_log_handlers[] = {
    json_path_handler("^/\\w+$").
    add_cb(read_json_null).
    add_cb(read_json_bool).
    add_cb(read_json_int).
    add_cb(read_json_double).
    add_cb(read_json_field),

    json_path_handler()
};

static int rewrite_json_field(yajlpp_parse_context *ypc, const unsigned char *str, size_t len);

static int rewrite_json_null(yajlpp_parse_context *ypc)
{
    if (!ypc->is_level(1)) {
        return 1;
    }

    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path_fragment_i(0);

    jlu->jlu_format->jlf_line_values.push_back(logline_value(field_name));

    return 1;
}

static int rewrite_json_bool(yajlpp_parse_context *ypc, int val)
{
    if (!ypc->is_level(1)) {
        return 1;
    }

    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path_fragment_i(0);

    jlu->jlu_format->jlf_line_values.push_back(logline_value(field_name, (bool)val));

    return 1;
}

static int rewrite_json_int(yajlpp_parse_context *ypc, long long val)
{
    if (!ypc->is_level(1)) {
        return 1;
    }

    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path_fragment_i(0);

    jlu->jlu_format->jlf_line_values.push_back(logline_value(field_name, (int64_t)val));

    return 1;
}

static int rewrite_json_double(yajlpp_parse_context *ypc, double val)
{
    if (!ypc->is_level(1)) {
        return 1;
    }

    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path_fragment_i(0);

    jlu->jlu_format->jlf_line_values.push_back(logline_value(field_name, val));

    return 1;
}

static struct json_path_handler json_log_rewrite_handlers[] = {
    json_path_handler("^/\\w+$").
    add_cb(rewrite_json_null).
    add_cb(rewrite_json_bool).
    add_cb(rewrite_json_int).
    add_cb(rewrite_json_double).
    add_cb(rewrite_json_field),

    json_path_handler()
};

bool external_log_format::scan(std::vector<logline> &dst,
                               off_t offset,
                               shared_buffer_ref &sbr)
{
    if (this->jlf_json) {
        yajlpp_parse_context &ypc = *(this->jlf_parse_context);
        logline ll(offset, 0, 0, logline::LEVEL_INFO);
        yajl_handle handle = this->jlf_yajl_handle.in();
        json_log_userdata jlu(sbr);
        bool retval = false;

        yajl_reset(handle);
        ypc.set_static_handler(json_log_handlers[0]);
        ypc.ypc_userdata = &jlu;
        ypc.ypc_ignore_unused = true;
        ypc.ypc_alt_callbacks.yajl_start_array = json_array_start;
        ypc.ypc_alt_callbacks.yajl_start_map = json_array_start;
        jlu.jlu_format = this;
        jlu.jlu_base_line = &ll;
        jlu.jlu_line_value = sbr.get_data();
        jlu.jlu_line_size = sbr.length();
        jlu.jlu_handle = handle;
        if (yajl_parse(handle,
                       (const unsigned char *)sbr.get_data(), sbr.length()) == yajl_status_ok &&
            yajl_complete_parse(handle) == yajl_status_ok) {
            for (int lpc = 0; lpc < jlu.jlu_sub_line_count; lpc++) {
                ll.set_sub_offset(lpc);
                if (lpc > 0) {
                    ll.set_level((logline::level_t) (ll.get_level() |
                        logline::LEVEL_CONTINUED));
                }
                dst.push_back(ll);
            }
            retval = true;
        }
        else {
            unsigned char *msg = yajl_get_error(handle, 1, (const unsigned char *)sbr.get_data(), sbr.length());
            log_debug("bad line %s", msg);
        }

        return retval;
    }

    pcre_input pi(sbr.get_data(), 0, sbr.length());
    pcre_context_static<128> pc;
    bool retval = false;
    int curr_fmt = -1;

    while (::next_format(this->elf_pattern_order, curr_fmt, this->lf_fmt_lock)) {
        pcrepp *pat = this->elf_pattern_order[curr_fmt]->p_pcre;

        if (!pat->match(pc, pi)) {
            continue;
        }

        if (this->lf_fmt_lock == -1) {
            this->lf_timestamp_field_index = pat->name_index(
                this->lf_timestamp_field.to_string());
            if (!this->elf_level_field.empty()) {
                this->elf_level_field_index = pat->name_index(
                        this->elf_level_field.to_string());
            }
            else {
                this->elf_level_field_index = -1;
            }
            if (!this->elf_body_field.empty()) {
                this->elf_body_field_index = pat->name_index(
                        this->elf_body_field.to_string());
            }
            else {
                this->elf_body_field_index = -1;
            }
        }

        pcre_context::capture_t *ts = pc[this->lf_timestamp_field_index];
        pcre_context::capture_t *level_cap = pc[this->elf_level_field_index];
        const char *ts_str = pi.get_substr_start(ts);
        const char *last;
        struct exttm log_time_tm;
        struct timeval log_tv;
        logline::level_t level = logline::LEVEL_INFO;

        if ((last = this->lf_date_time.scan(ts_str,
                                            ts->length(),
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

        this->check_for_new_year(dst, log_tv);

        dst.push_back(logline(offset, log_tv, level));

        this->lf_fmt_lock = curr_fmt;
        retval = true;
        break;
    }

    return retval;
}

void external_log_format::annotate(shared_buffer_ref &line,
                                   string_attrs_t &sa,
                                   std::vector<logline_value> &values) const
{
    pcre_context_static<128> pc;
    pcre_input pi(line.get_data(), 0, line.length());
    struct line_range lr;
    pcre_context::capture_t *cap;

    if (this->jlf_json) {
        values = this->jlf_line_values;
        sa = this->jlf_line_attrs;
        return;
    }

    pattern &pat = *this->elf_pattern_order[this->lf_fmt_lock];

    if (!pat.p_pcre->match(pc, pi)) {
        return;
    }

    cap = pc[this->lf_timestamp_field_index];
    lr.lr_start = cap->c_begin;
    lr.lr_end = cap->c_end;
    sa.push_back(string_attr(lr, &logline::L_TIMESTAMP));

    cap = pc[this->elf_body_field_index];
    if (cap != NULL && cap->c_begin != -1) {
        lr.lr_start = cap->c_begin;
        lr.lr_end = cap->c_end;
    }
    else {
        lr.lr_start = line.length();
        lr.lr_end = line.length();
    }
    sa.push_back(string_attr(lr, &textview_curses::SA_BODY));

    view_colors &vc = view_colors::singleton();

    for (size_t lpc = 0; lpc < pat.p_value_by_index.size(); lpc++) {
        const value_def &vd = pat.p_value_by_index[lpc];
        const struct scaling_factor *scaling = NULL;
        pcre_context::capture_t *cap = pc[vd.vd_index];
        shared_buffer_ref field;

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

        field.subset(line, cap->c_begin, cap->length());

        values.push_back(logline_value(vd.vd_name,
                         vd.vd_kind,
                         field,
                         vd.vd_identifier,
                         scaling,
                         vd.vd_column,
                         cap->c_begin,
                         cap->c_end));

        if (pc[vd.vd_index]->c_begin != -1 && vd.vd_identifier) {
            lr.lr_start = pc[vd.vd_index]->c_begin;
            lr.lr_end = pc[vd.vd_index]->c_end;
            sa.push_back(string_attr(lr, &view_curses::VC_STYLE,
                vc.attrs_for_ident(pi.get_substr_start(pc[vd.vd_index]), lr.length())));
        }
    }
}

static int read_json_field(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    if (!ypc->is_level(1)) {
        return 1;
    }

    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    vector<external_log_format::json_format_element> &line_format =
        jlu->jlu_format->jlf_line_format;
    const intern_string_t field_name = ypc->get_path_fragment_i(0);
    struct exttm tm_out;
    struct timeval tv_out;

    if (jlu->jlu_format->lf_timestamp_field == field_name) {
        jlu->jlu_format->lf_date_time.scan((const char *)str, len, NULL, &tm_out, tv_out);
        jlu->jlu_base_line->set_time(tv_out);
    }
    else if (jlu->jlu_format->elf_level_field == field_name) {
        jlu->jlu_base_line->set_level(logline::abbrev2level((const char *)str, len));
    }
    else {
        if (find_if(line_format.begin(), line_format.end(),
                    json_field_cmp(external_log_format::JLF_VARIABLE,
                                   field_name)) == line_format.end()) {
            jlu->jlu_sub_line_count += 1;
        }
        for (size_t lpc = 0; lpc < len; lpc++) {
            if (str[lpc] == '\n') {
                jlu->jlu_sub_line_count += 1;
            }
        }
    }

    return 1;
}

static int rewrite_json_field(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    if (!ypc->is_level(1)) {
        return 1;
    }

    static const intern_string_t body_name = intern_string::lookup("body", -1);
    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path_fragment_i(0);

    if (jlu->jlu_format->lf_timestamp_field == field_name) {
        char time_buf[64];

        // TODO add a timeval kind to logline_value
        sql_strftime(time_buf, sizeof(time_buf),
            jlu->jlu_line->get_timeval(), 'T');
        tmp_shared_buffer tsb(time_buf);
        jlu->jlu_format->jlf_line_values.push_back(logline_value(field_name, tsb.tsb_ref));
    }
    else if (jlu->jlu_shared_buffer.contains((const char *)str)) {
        shared_buffer_ref sbr;

        sbr.subset(jlu->jlu_shared_buffer,
                (off_t) ((const char *)str - jlu->jlu_line_value),
                len);
        if (field_name == jlu->jlu_format->elf_body_field) {
            jlu->jlu_format->jlf_line_values.push_back(logline_value(body_name,
                    sbr));
        }
        jlu->jlu_format->jlf_line_values.push_back(logline_value(field_name,
                sbr));
    }
    else {
        tmp_shared_buffer tsb((const char *)str, len);

        if (field_name == jlu->jlu_format->elf_body_field) {
            jlu->jlu_format->jlf_line_values.push_back(logline_value(body_name, tsb.tsb_ref));
        }
        jlu->jlu_format->jlf_line_values.push_back(logline_value(field_name, tsb.tsb_ref));
    }

    return 1;
}

void external_log_format::get_subline(const logline &ll, shared_buffer_ref &sbr)
{
    if (!this->jlf_json) {
        return;
    }

    if (this->jlf_cached_offset != ll.get_offset()) {
        yajlpp_parse_context &ypc = *(this->jlf_parse_context);
        view_colors &vc = view_colors::singleton();
        yajl_handle handle = this->jlf_yajl_handle.in();
        json_log_userdata jlu(sbr);

        this->jlf_share_manager.invalidate_refs();
        this->jlf_cached_line.clear();
        this->jlf_line_values.clear();
        this->jlf_line_offsets.clear();
        this->jlf_line_attrs.clear();

        yajl_reset(handle);
        ypc.set_static_handler(json_log_rewrite_handlers[0]);
        ypc.ypc_userdata = &jlu;
        ypc.ypc_ignore_unused = true;
        ypc.ypc_alt_callbacks.yajl_start_array = json_array_start;
        ypc.ypc_alt_callbacks.yajl_end_array = json_array_end;
        ypc.ypc_alt_callbacks.yajl_start_map = json_array_start;
        ypc.ypc_alt_callbacks.yajl_end_map = json_array_end;
        jlu.jlu_format = this;
        jlu.jlu_line = &ll;
        jlu.jlu_handle = handle;
        jlu.jlu_line_value = sbr.get_data();

        yajl_status parse_status = yajl_parse(handle,
            (const unsigned char *)sbr.get_data(), sbr.length());
        if (parse_status == yajl_status_ok &&
            yajl_complete_parse(handle) == yajl_status_ok) {
            std::vector<logline_value>::iterator lv_iter;
            std::vector<json_format_element>::iterator iter;
            bool used_values[this->jlf_line_values.size()];
            struct line_range lr;

            memset(used_values, 0, sizeof(used_values));

            for (lv_iter = this->jlf_line_values.begin();
                 lv_iter != this->jlf_line_values.end();
                 ++lv_iter) {
                map<const intern_string_t, external_log_format::value_def>::iterator vd_iter;

                vd_iter = this->elf_value_defs.find(lv_iter->lv_name);
                if (vd_iter != this->elf_value_defs.end()) {
                    lv_iter->lv_identifier = vd_iter->second.vd_identifier;
                    lv_iter->lv_column = vd_iter->second.vd_column;
                }
            }

            for (iter = this->jlf_line_format.begin();
                 iter != this->jlf_line_format.end();
                 ++iter) {
                static const intern_string_t ts_field = intern_string::lookup("__timestamp__", -1);

                switch (iter->jfe_type) {
                case JLF_CONSTANT:
                    this->json_append_to_cache(iter->jfe_default_value.c_str(),
                            iter->jfe_default_value.size());
                    break;
                case JLF_VARIABLE:
                    lv_iter = find_if(this->jlf_line_values.begin(),
                                      this->jlf_line_values.end(),
                                      logline_value_cmp(&iter->jfe_value));
                    if (lv_iter != this->jlf_line_values.end()) {
                        string str = lv_iter->to_string();
                        size_t nl_pos = str.find('\n');

                        lr.lr_start = this->jlf_cached_line.size();
                        this->json_append_to_cache(
                                str.c_str(), str.size());
                        if (nl_pos == string::npos)
                            lr.lr_end = this->jlf_cached_line.size();
                        else
                            lr.lr_end = lr.lr_start + nl_pos;
                        if (lv_iter->lv_name == this->lf_timestamp_field) {
                            this->jlf_line_attrs.push_back(
                                string_attr(lr, &logline::L_TIMESTAMP));
                        }
                        else if (lv_iter->lv_name == this->elf_body_field) {
                            this->jlf_line_attrs.push_back(
                                string_attr(lr, &textview_curses::SA_BODY));
                        }
                        else if (lv_iter->lv_identifier) {
                            this->jlf_line_attrs.push_back(
                                string_attr(lr, &view_curses::VC_STYLE,
                                    vc.attrs_for_ident(str.c_str(), lr.length())));
                        }
                        lv_iter->lv_origin = lr;
                        used_values[distance(this->jlf_line_values.begin(),
                                             lv_iter)] = true;
                    }
                    else if (iter->jfe_value == ts_field) {
                        struct line_range lr;
                        ssize_t ts_len;
                        char ts[64];

                        ts_len = sql_strftime(ts, sizeof(ts), ll.get_timeval(), 'T');
                        lr.lr_start = this->jlf_cached_line.size();
                        this->json_append_to_cache(ts, ts_len);
                        lr.lr_end = this->jlf_cached_line.size();
                        this->jlf_line_attrs.push_back(
                            string_attr(lr, &logline::L_TIMESTAMP));
                    }
                    else {
                        this->json_append_to_cache(
                                iter->jfe_default_value.c_str(),
                                iter->jfe_default_value.size());
                    }
                    break;
                }
            }
            this->json_append_to_cache("\n", 1);
            for (size_t lpc = 0; lpc < this->jlf_line_values.size(); lpc++) {
                static const intern_string_t body_name = intern_string::lookup("body", -1);
                logline_value &lv = this->jlf_line_values[lpc];

                if (used_values[lpc] ||
                    lv.lv_name == this->lf_timestamp_field ||
                    lv.lv_name == body_name ||
                    lv.lv_name == this->elf_level_field) {
                    continue;
                }

                const std::string str = lv.to_string();
                size_t curr_pos = 0, nl_pos, line_len = -1;

                lv.lv_origin.lr_start = this->jlf_cached_line.size();
                do {
                    nl_pos = str.find('\n', curr_pos);
                    if (nl_pos != std::string::npos) {
                        line_len = nl_pos - curr_pos;
                    }
                    else {
                        line_len = str.size() - curr_pos;
                    }
                    this->json_append_to_cache("  ", 2);
                    this->json_append_to_cache(lv.lv_name.get(),
                            lv.lv_name.size());
                    this->json_append_to_cache(": ", 2);
                    this->json_append_to_cache(
                            &str.c_str()[curr_pos], line_len);
                    this->json_append_to_cache("\n", 1);
                    curr_pos = nl_pos + 1;
                    line_len = -1;
                } while (nl_pos != std::string::npos &&
                         nl_pos < str.size());
                lv.lv_origin.lr_end = this->jlf_cached_line.size();
            }

            this->jlf_line_offsets.push_back(0);
            for (size_t lpc = 0; lpc < this->jlf_cached_line.size(); lpc++) {
                if (this->jlf_cached_line[lpc] == '\n') {
                    this->jlf_line_offsets.push_back(lpc);
                }
            }
            this->jlf_line_offsets.push_back(this->jlf_cached_line.size());
        }

        this->jlf_cached_offset = ll.get_offset();
    }

    off_t this_off = 0, next_off = 0;

    if (!this->jlf_line_offsets.empty()) {
        this_off = this->jlf_line_offsets[ll.get_sub_offset()];
        if (this->jlf_cached_line[this_off] == '\n') {
            this_off += 1;
        }
        next_off = this->jlf_line_offsets[ll.get_sub_offset() + 1];
    }

    sbr.share(this->jlf_share_manager,
              &this->jlf_cached_line[0] + this_off,
              next_off - this_off);
}

void external_log_format::build(std::vector<std::string> &errors)
{
    try {
        this->elf_filename_pcre = new pcrepp(this->elf_file_pattern.c_str());
    }
    catch (const pcrepp::error &e) {
        errors.push_back("error:" +
                         this->elf_name + ".file-pattern:" +
                         e.what());
    }
    for (std::map<string, pattern>::iterator iter = this->elf_patterns.begin();
         iter != this->elf_patterns.end();
         ++iter) {
        try {
            iter->second.p_pcre = new pcrepp(iter->second.p_string.c_str());
        }
        catch (const pcrepp::error &e) {
            errors.push_back("error:" +
                             this->elf_name + ".regex[]" +
                             ":" +
                             e.what());
            continue;
        }
        for (pcre_named_capture::iterator name_iter = iter->second.p_pcre->named_begin();
             name_iter != iter->second.p_pcre->named_end();
             ++name_iter) {
            std::map<const intern_string_t, value_def>::iterator value_iter;
            const intern_string_t name = intern_string::lookup(name_iter->pnc_name, -1);

            value_iter = this->elf_value_defs.find(name);
            if (value_iter != this->elf_value_defs.end()) {
                value_def &vd = value_iter->second;

                vd.vd_index = name_iter->index();
                if (!vd.vd_unit_field.empty()) {
                    vd.vd_unit_field_index = iter->second.p_pcre->name_index(vd.vd_unit_field.get());
                }
                else {
                    vd.vd_unit_field_index = -1;
                }
                if (vd.vd_column == -1) {
                    vd.vd_column = this->elf_column_count++;
                }
                iter->second.p_value_by_index.push_back(vd);
            }
        }

        stable_sort(iter->second.p_value_by_index.begin(),
                    iter->second.p_value_by_index.end());

        this->elf_pattern_order.push_back(&iter->second);
    }

    if (this->jlf_json) {
        if (!this->elf_patterns.empty()) {
            errors.push_back("error:" +
                             this->elf_name +
                             ": JSON logs cannot have regexes");
        }
        if (this->jlf_json) {
            this->jlf_parse_context.reset(new yajlpp_parse_context(this->elf_name));
            this->jlf_yajl_handle.reset(yajl_alloc(
                    &this->jlf_parse_context->ypc_callbacks,
                    NULL,
                    this->jlf_parse_context.get()));
            yajl_config(this->jlf_yajl_handle.in(), yajl_dont_validate_strings, 1);
        }

    }
    else {
        if (this->elf_patterns.empty()) {
            errors.push_back("error:" +
                             this->elf_name +
                             ": no regexes specified for format");
        }
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

    for (std::map<const intern_string_t, value_def>::iterator iter = this->elf_value_defs.begin();
         iter != this->elf_value_defs.end();
         ++iter) {
        std::vector<std::string>::iterator act_iter;

        if (iter->second.vd_column == -1) {
            iter->second.vd_column = this->elf_column_count++;
        }

        for (act_iter = iter->second.vd_action_list.begin();
            act_iter != iter->second.vd_action_list.end();
            ++act_iter) {
            if (this->lf_action_defs.find(*act_iter) ==
                this->lf_action_defs.end()) {
                errors.push_back("error:" +
                    this->elf_name + ":" + iter->first.get() +
                    ": cannot find action -- " + (*act_iter));
            }
        }
    }

    if (!this->jlf_json && this->elf_samples.empty()) {
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

        for (std::vector<pattern *>::iterator pat_iter = this->elf_pattern_order.begin();
             pat_iter != this->elf_pattern_order.end() && !found;
             ++pat_iter) {
            pattern &pat = *(*pat_iter);

            if (!pat.p_pcre)
                continue;

            if (pat.p_pcre->name_index(this->lf_timestamp_field.to_string()) < 0) {
                errors.push_back("error:" +
                    this->elf_name +
                    ":timestamp field '" +
                    this->lf_timestamp_field.get() +
                    "' not found in pattern -- " +
                    pat.p_string);
                continue;
            }

            if (pat.p_pcre->match(pc, pi)) {
                const char *ts = pi.get_substr_start(
                    pc[this->lf_timestamp_field.get()]);
                ssize_t ts_len = pc[this->lf_timestamp_field.get()]->length();
                date_time_scanner dts;
                struct timeval tv;
                struct exttm tm;

                found = true;
                if (ts_len == -1 || dts.scan(ts, ts_len, NULL, &tm, tv) == NULL) {
                    errors.push_back("error:" +
                        this->elf_name +
                        ":invalid sample -- " +
                        iter->s_line);
                    errors.push_back("error:" +
                        this->elf_name +
                        ":unrecognized timestamp format -- " + ts);

                    for (int lpc = 0; PTIMEC_FORMATS[lpc].pf_fmt != NULL; lpc++) {
                        off_t off = 0;

                        PTIMEC_FORMATS[lpc].pf_func(&tm, ts, off, ts_len);
                        errors.push_back("  format: " + string(PTIMEC_FORMATS[lpc].pf_fmt) +
                            "; matched: " + string(ts, off));
                    }
                }
            }
        }

        if (!found) {
            errors.push_back("error:" +
                             this->elf_name +
                             ":invalid sample -- " +
                             iter->s_line);

            for (std::vector<pattern *>::iterator pat_iter = this->elf_pattern_order.begin();
                 pat_iter != this->elf_pattern_order.end();
                 ++pat_iter) {
                pattern &pat = *(*pat_iter);

                if (!pat.p_pcre)
                    continue;

                std::string line_partial = iter->s_line;

                while (!line_partial.empty()) {
                    pcre_input pi_partial(line_partial);

                    if (pat.p_pcre->match(pc, pi_partial, PCRE_PARTIAL)) {
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
        std::map<const intern_string_t, external_log_format::value_def>::const_iterator iter;
        const external_log_format &elf = this->elt_format;

        cols.resize(elf.elf_value_defs.size());
        for (iter = elf.elf_value_defs.begin();
             iter != elf.elf_value_defs.end();
             ++iter) {
            const external_log_format::value_def &vd = iter->second;
            int type;

            switch (vd.vd_kind) {
            case logline_value::VALUE_NULL:
            case logline_value::VALUE_TEXT:
            case logline_value::VALUE_JSON:
            case logline_value::VALUE_QUOTED:
                type = SQLITE3_TEXT;
                break;
            case logline_value::VALUE_FLOAT:
                type = SQLITE_FLOAT;
                break;
            case logline_value::VALUE_BOOLEAN:
            case logline_value::VALUE_INTEGER:
                type = SQLITE_INTEGER;
                break;
            case logline_value::VALUE_UNKNOWN:
            case logline_value::VALUE__MAX:
                ensure(0);
                break;
            }
            cols[vd.vd_column].vc_name = vd.vd_name.get();
            cols[vd.vd_column].vc_type = type;
            cols[vd.vd_column].vc_collator = vd.vd_collate.c_str();
        }
    };

    void get_foreign_keys(std::vector<std::string> &keys_inout)
    {
        std::map<const intern_string_t, external_log_format::value_def>::const_iterator iter;

        log_vtab_impl::get_foreign_keys(keys_inout);

        for (iter = this->elt_format.elf_value_defs.begin();
             iter != this->elt_format.elf_value_defs.end();
             ++iter) {
            if (iter->second.vd_foreign_key) {
                keys_inout.push_back(iter->first.to_string());
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
