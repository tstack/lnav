/**
 * Copyright (c) 2007-2015, Timothy Stack
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
string_attr_type logline::L_MODULE;
string_attr_type logline::L_OPID;

const char *logline::level_names[LEVEL__MAX + 1] = {
    "unknown",
    "trace",
    "debug5",
    "debug4",
    "debug3",
    "debug2",
    "debug",
    "info",
    "stats",
    "warning",
    "error",
    "critical",
    "fatal",

    NULL
};

static pcrepp LEVEL_RE(
        "(?i)(TRACE|DEBUG\\d*|INFO|STATS|WARN(?:ING)?|ERR(?:OR)?|CRITICAL|SEVERE|FATAL)");

external_log_format::mod_map_t external_log_format::MODULE_FORMATS;
std::vector<external_log_format *> external_log_format::GRAPH_ORDERED_FORMATS;

logline::level_t logline::string2level(const char *levelstr, ssize_t len, bool exact)
{
    logline::level_t retval = logline::LEVEL_UNKNOWN;

    if (len == (ssize_t)-1) {
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
            if (len > 1) {
                switch (levelstr[len - 1]) {
                    case '2':
                        return LEVEL_DEBUG2;
                    case '3':
                        return LEVEL_DEBUG3;
                    case '4':
                        return LEVEL_DEBUG4;
                    case '5':
                        return LEVEL_DEBUG5;
                }
            }
            return LEVEL_DEBUG;
        case 'I':
            return LEVEL_INFO;
        case 'S':
            return LEVEL_STATS;
        case 'W':
            return LEVEL_WARNING;
        case 'E':
            return LEVEL_ERROR;
        case 'C':
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
                                  struct timeval *tv_out,
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

        pi.reset(line, 0, len);
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
                    pi.get_substr_start(ts), ts->length(), NULL, tm_out, *tv_out);

            if (retval) {
                this->lf_fmt_lock = curr_fmt;
                done = true;
            }
        }

        va_end(args);
    }

    return retval;
}

void log_format::check_for_new_year(std::vector<logline> &dst, exttm etm,
                                    struct timeval log_tv)
{
    if (dst.empty()) {
        return;
    }

    time_t diff = dst.back().get_time() - log_tv.tv_sec;
    int off_year = 0, off_month = 0, off_day = 0, off_hour = 0;
    std::vector<logline>::iterator iter;
    bool do_change = true;

    if (diff <= 0) {
        return;
    }
    if (diff > (60 * 24 * 60 * 60)) {
        off_year = 1;
    } else if (diff > (15 * 24 * 60 * 60)) {
        off_month = 1;
    } else if (diff > (12 * 60 * 60)) {
        off_day = 1;
    } else if (!(etm.et_flags & ETF_DAY_SET)) {
        off_hour = 1;
    } else {
        do_change = false;
    }

    if (!do_change) {
        return;
    }
    for (iter = dst.begin(); iter != dst.end(); iter++) {
        time_t     ot = iter->get_time();
        struct tm otm;

        gmtime_r(&ot, &otm);
        otm.tm_year -= off_year;
        otm.tm_mon  -= off_month;
        otm.tm_yday -= off_day;
        otm.tm_hour -= off_hour;
        iter->set_time(tm2sec(&otm));
    }
}

/*
 * XXX This needs some cleanup. 
 */
struct json_log_userdata {
    json_log_userdata(shared_buffer_ref &sbr)
            : jlu_format(NULL), jlu_line(NULL), jlu_base_line(NULL),
              jlu_sub_line_count(1), jlu_handle(NULL), jlu_line_value(NULL),
              jlu_line_size(0), jlu_sub_start(0), jlu_shared_buffer(sbr) {

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
    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    vector<external_log_format::json_format_element> &line_format =
        jlu->jlu_format->jlf_line_format;
    const intern_string_t field_name = ypc->get_path();

    if (!ypc->is_level(1) && !jlu->jlu_format->has_value_def(field_name)) {
        return 1;
    }
    if (!jlu->jlu_format->jlf_hide_extra &&
            find_if(line_format.begin(), line_format.end(),
                json_field_cmp(external_log_format::JLF_VARIABLE,
                               field_name)) == line_format.end()) {
        jlu->jlu_sub_line_count += 1;
    }

    return 1;
}

static int read_json_bool(yajlpp_parse_context *ypc, int val)
{
    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    vector<external_log_format::json_format_element> &line_format =
        jlu->jlu_format->jlf_line_format;
    const intern_string_t field_name = ypc->get_path();

    if (!ypc->is_level(1) && !jlu->jlu_format->has_value_def(field_name)) {
        return 1;
    }
    if (!jlu->jlu_format->jlf_hide_extra &&
        find_if(line_format.begin(), line_format.end(),
            json_field_cmp(external_log_format::JLF_VARIABLE,
                    field_name)) == line_format.end()) {
        jlu->jlu_sub_line_count += 1;
    }

    return 1;
}

static int read_json_int(yajlpp_parse_context *ypc, long long val)
{
    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    vector<external_log_format::json_format_element> &line_format =
        jlu->jlu_format->jlf_line_format;
    const intern_string_t field_name = ypc->get_path();

    if (!ypc->is_level(1) && !jlu->jlu_format->has_value_def(field_name)) {
        return 1;
    }
    if (jlu->jlu_format->lf_timestamp_field == field_name) {
        long long divisor = jlu->jlu_format->elf_timestamp_divisor;
        struct timeval tv;

        tv.tv_sec = val / divisor;
        tv.tv_usec = (val % divisor) * (1000000.0 / divisor);
        jlu->jlu_base_line->set_time(tv);
    }
    else if (!jlu->jlu_format->jlf_hide_extra &&
             find_if(line_format.begin(), line_format.end(),
                     json_field_cmp(external_log_format::JLF_VARIABLE,
                         field_name)) == line_format.end()) {
        jlu->jlu_sub_line_count += 1;
    }

    return 1;
}

static int read_json_double(yajlpp_parse_context *ypc, double val)
{
    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    vector<external_log_format::json_format_element> &line_format =
        jlu->jlu_format->jlf_line_format;
    const intern_string_t field_name = ypc->get_path();

    if (!ypc->is_level(1) && !jlu->jlu_format->has_value_def(field_name)) {
        return 1;
    }
    if (jlu->jlu_format->lf_timestamp_field == field_name) {
        double divisor = jlu->jlu_format->elf_timestamp_divisor;
        struct timeval tv;

        tv.tv_sec = val / divisor;
        tv.tv_usec = fmod(val, divisor) * (1000000.0 / divisor);
        jlu->jlu_base_line->set_time(tv);
    }
    else if (!jlu->jlu_format->jlf_hide_extra &&
             find_if(line_format.begin(), line_format.end(),
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

        if (!jlu->jlu_format->jlf_hide_extra &&
            find_if(line_format.begin(), line_format.end(),
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
        shared_buffer_ref sbr;

        sbr.subset(jlu->jlu_shared_buffer, jlu->jlu_sub_start,
            sub_end - jlu->jlu_sub_start);
        jlu->jlu_format->jlf_line_values.push_back(
            logline_value(field_name, sbr));
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
    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path();

    if (!ypc->is_level(1) && !jlu->jlu_format->has_value_def(field_name)) {
        return 1;
    }
    jlu->jlu_format->jlf_line_values.push_back(logline_value(field_name));

    return 1;
}

static int rewrite_json_bool(yajlpp_parse_context *ypc, int val)
{
    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path();

    if (!ypc->is_level(1) && !jlu->jlu_format->has_value_def(field_name)) {
        return 1;
    }
    jlu->jlu_format->jlf_line_values.push_back(logline_value(field_name, (bool)val));

    return 1;
}

static int rewrite_json_int(yajlpp_parse_context *ypc, long long val)
{
    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path();

    if (!ypc->is_level(1) && !jlu->jlu_format->has_value_def(field_name)) {
        return 1;
    }
    jlu->jlu_format->jlf_line_values.push_back(logline_value(field_name, (int64_t)val));

    return 1;
}

static int rewrite_json_double(yajlpp_parse_context *ypc, double val)
{
    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path();

    if (!ypc->is_level(1) && !jlu->jlu_format->has_value_def(field_name)) {
        return 1;
    }
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

bool external_log_format::scan_for_partial(shared_buffer_ref &sbr, size_t &len_out)
{
    if (this->jlf_json) {
        return false;
    }

    pattern *pat = this->elf_pattern_order[this->lf_fmt_lock];
    pcre_input pi(sbr.get_data(), 0, sbr.length());

    if (!this->elf_multiline) {
        len_out = pat->p_pcre->match_partial(pi);
        return true;
    }

    if (pat->p_timestamp_end == -1 || pat->p_timestamp_end > sbr.length()) {
        len_out = 0;
        return false;
    }

    len_out = pat->p_pcre->match_partial(pi);
    return len_out > pat->p_timestamp_end;
}

log_format::scan_result_t external_log_format::scan(std::vector<logline> &dst,
                                                    off_t offset,
                                                    shared_buffer_ref &sbr)
{
    if (this->jlf_json) {
        yajlpp_parse_context &ypc = *(this->jlf_parse_context);
        logline ll(offset, 0, 0, logline::LEVEL_INFO);
        yajl_handle handle = this->jlf_yajl_handle.in();
        json_log_userdata jlu(sbr);

        if (sbr.empty() || sbr.get_data()[sbr.length() - 1] != '}') {
            return log_format::SCAN_INCOMPLETE;
        }

        yajl_reset(handle);
        ypc.set_static_handler(json_log_handlers[0]);
        ypc.ypc_userdata = &jlu;
        ypc.ypc_ignore_unused = true;
        ypc.ypc_alt_callbacks.yajl_start_array = json_array_start;
        ypc.ypc_alt_callbacks.yajl_start_map = json_array_start;
        ypc.ypc_alt_callbacks.yajl_end_array = NULL;
        ypc.ypc_alt_callbacks.yajl_end_map = NULL;
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
        }
        else {
            unsigned char *msg;

            msg = yajl_get_error(handle, 1, (const unsigned char *)sbr.get_data(), sbr.length());
            if (msg != NULL) {
                log_debug("Unable to parse line at offset %d: %s", offset, msg);
                yajl_free_error(handle, msg);
            }
            return log_format::SCAN_INCOMPLETE;
        }

        return log_format::SCAN_MATCH;
    }

    pcre_input pi(sbr.get_data(), 0, sbr.length());
    pcre_context_static<128> pc;
    int curr_fmt = -1;

    while (::next_format(this->elf_pattern_order, curr_fmt, this->lf_fmt_lock)) {
        pattern *fpat = this->elf_pattern_order[curr_fmt];
        pcrepp *pat = fpat->p_pcre;

        if (fpat->p_module_format) {
            continue;
        }

        if (!pat->match(pc, pi)) {
            continue;
        }

        pcre_context::capture_t *ts = pc[fpat->p_timestamp_field_index];
        pcre_context::capture_t *level_cap = pc[fpat->p_level_field_index];
        pcre_context::capture_t *mod_cap = pc[fpat->p_module_field_index];
        pcre_context::capture_t *opid_cap = pc[fpat->p_opid_field_index];
        pcre_context::capture_t *body_cap = pc[fpat->p_body_field_index];
        const char *ts_str = pi.get_substr_start(ts);
        const char *last;
        struct exttm log_time_tm;
        struct timeval log_tv;
        logline::level_t level = logline::LEVEL_INFO;
        uint8_t mod_index = 0, opid = 0;

        if ((last = this->lf_date_time.scan(ts_str,
                                            ts->length(),
                                            this->get_timestamp_formats(),
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

        if (!((log_time_tm.et_flags & ETF_DAY_SET) &&
                (log_time_tm.et_flags & ETF_MONTH_SET) &&
                (log_time_tm.et_flags & ETF_YEAR_SET))) {
            this->check_for_new_year(dst, log_time_tm, log_tv);
        }

        if (opid_cap != NULL) {
            opid = hash_str(pi.get_substr_start(opid_cap), opid_cap->length());
        }

        if (mod_cap != NULL) {
            intern_string_t mod_name = intern_string::lookup(
                    pi.get_substr_start(mod_cap), mod_cap->length());
            mod_map_t::iterator mod_iter = MODULE_FORMATS.find(mod_name);

            if (mod_iter == MODULE_FORMATS.end()) {
                mod_index = module_scan(pi, body_cap, mod_name);
            }
            else if (mod_iter->second.mf_mod_format) {
                mod_index = mod_iter->second.mf_mod_format->lf_mod_index;
            }
        }

        dst.push_back(logline(offset, log_tv, level, mod_index, opid));

        this->lf_fmt_lock = curr_fmt;
        return log_format::SCAN_MATCH;
    }

    return log_format::SCAN_NO_MATCH;
}

uint8_t external_log_format::module_scan(const pcre_input &pi,
                                         pcre_context::capture_t *body_cap,
                                         const intern_string_t &mod_name)
{
    uint8_t mod_index;
    body_cap->ltrim(pi.get_string());
    pcre_input body_pi(pi.get_substr_start(body_cap), 0, body_cap->length());
    vector<external_log_format *> &ext_fmts = GRAPH_ORDERED_FORMATS;
    pcre_context_static<128> pc;
    module_format mf;

    for (vector<external_log_format *>::iterator fmt_iter = ext_fmts.begin();
         fmt_iter != ext_fmts.end();
         ++fmt_iter) {
        external_log_format *elf = *fmt_iter;
        int curr_fmt = -1, fmt_lock = -1;

        while (::next_format(elf->elf_pattern_order, curr_fmt, fmt_lock)) {
            pattern *fpat = elf->elf_pattern_order[curr_fmt];
            pcrepp *pat = fpat->p_pcre;

            if (!fpat->p_module_format) {
                continue;
            }

            if (!pat->match(pc, body_pi)) {
                continue;
            }

            log_debug("%s:module format found -- %s (%d)",
                      mod_name.get(),
                      elf->get_name().get(),
                      elf->lf_mod_index);

            mod_index = elf->lf_mod_index;
            mf.mf_mod_format = (external_log_format *) elf->specialized(curr_fmt).release();
            MODULE_FORMATS[mod_name] = mf;

            return mod_index;
        }
    }

    MODULE_FORMATS[mod_name] = mf;

    return 0;
}

void external_log_format::annotate(shared_buffer_ref &line,
                                   string_attrs_t &sa,
                                   std::vector<logline_value> &values,
                                   bool annotate_module) const
{
    pcre_context_static<128> pc;
    pcre_input pi(line.get_data(), 0, line.length());
    struct line_range lr;
    pcre_context::capture_t *cap, *body_cap, *module_cap = NULL;

    if (this->jlf_json) {
        values = this->jlf_line_values;
        sa = this->jlf_line_attrs;
        return;
    }

    pattern &pat = *this->elf_pattern_order[this->lf_fmt_lock];

    if (!pat.p_pcre->match(pc, pi)) {
        return;
    }

    if (!pat.p_module_format) {
        cap = pc[pat.p_timestamp_field_index];
        lr.lr_start = cap->c_begin;
        lr.lr_end = cap->c_end;
        sa.push_back(string_attr(lr, &logline::L_TIMESTAMP));

        if (pat.p_module_field_index != -1) {
            module_cap = pc[pat.p_module_field_index];
            if (module_cap != NULL && module_cap->is_valid()) {
                lr.lr_start = module_cap->c_begin;
                lr.lr_end = module_cap->c_end;
                sa.push_back(string_attr(lr, &logline::L_MODULE));
            }
        }

        cap = pc[pat.p_opid_field_index];
        if (cap != NULL && cap->is_valid()) {
            lr.lr_start = cap->c_begin;
            lr.lr_end = cap->c_end;
            sa.push_back(string_attr(lr, &logline::L_OPID));
        }
    }

    body_cap = pc[pat.p_body_field_index];
    if (body_cap != NULL && body_cap->c_begin != -1) {
        lr.lr_start = body_cap->c_begin;
        lr.lr_end = body_cap->c_end;
    }
    else {
        lr.lr_start = line.length();
        lr.lr_end = line.length();
    }
    sa.push_back(string_attr(lr, &textview_curses::SA_BODY));

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
                                       cap->c_end,
                                       pat.p_module_format,
                                       this));
    }

    if (annotate_module && module_cap != NULL && body_cap != NULL &&
            body_cap->is_valid()) {
        intern_string_t mod_name = intern_string::lookup(
                pi.get_substr_start(module_cap), module_cap->length());
        mod_map_t::iterator mod_iter = MODULE_FORMATS.find(mod_name);

        if (mod_iter != MODULE_FORMATS.end() &&
                mod_iter->second.mf_mod_format != NULL) {
            module_format &mf = mod_iter->second;
            shared_buffer_ref body_ref;

            body_cap->ltrim(line.get_data());
            body_ref.subset(line, body_cap->c_begin, body_cap->length());
            mf.mf_mod_format->annotate(body_ref, sa, values, false);
            for (vector<logline_value>::iterator lv_iter = values.begin();
                 lv_iter != values.end();
                 ++lv_iter) {
                if (!lv_iter->lv_from_module) {
                    continue;
                }
                lv_iter->lv_origin.lr_start += body_cap->c_begin;
                lv_iter->lv_origin.lr_end += body_cap->c_begin;
            }
        }
    }
}

static int read_json_field(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    vector<external_log_format::json_format_element> &line_format =
        jlu->jlu_format->jlf_line_format;
    const intern_string_t field_name = ypc->get_path();
    struct exttm tm_out;
    struct timeval tv_out;

    if (jlu->jlu_format->lf_timestamp_field == field_name) {
        jlu->jlu_format->lf_date_time.scan((const char *)str, len, jlu->jlu_format->get_timestamp_formats(), &tm_out, tv_out);
        jlu->jlu_base_line->set_time(tv_out);
    }
    else if (jlu->jlu_format->elf_level_field == field_name) {
        jlu->jlu_base_line->set_level(logline::abbrev2level((const char *)str, len));
    }
    else if (jlu->jlu_format->elf_opid_field == field_name) {
        uint8_t opid = hash_str((const char *) str, len);
        jlu->jlu_base_line->set_opid(opid);
    }
    else if (ypc->is_level(1) || jlu->jlu_format->elf_value_defs.find(field_name) !=
             jlu->jlu_format->elf_value_defs.end()) {
        if (!jlu->jlu_format->jlf_hide_extra &&
            find_if(line_format.begin(), line_format.end(),
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
    static const intern_string_t body_name = intern_string::lookup("body", -1);
    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path();

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
        if (!ypc->is_level(1) && !jlu->jlu_format->has_value_def(field_name)) {
            return 1;
        }

        jlu->jlu_format->jlf_line_values.push_back(logline_value(field_name,
                sbr));
    }
    else {
        tmp_shared_buffer tsb((const char *)str, len);

        if (field_name == jlu->jlu_format->elf_body_field) {
            jlu->jlu_format->jlf_line_values.push_back(logline_value(body_name, tsb.tsb_ref));
        }
        if (!ypc->is_level(1) && !jlu->jlu_format->has_value_def(field_name)) {
            return 1;
        }

        jlu->jlu_format->jlf_line_values.push_back(logline_value(field_name, tsb.tsb_ref));
    }

    return 1;
}

void external_log_format::get_subline(const logline &ll, shared_buffer_ref &sbr, bool full_message)
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
                        else if (lv_iter->lv_name == this->elf_opid_field) {
                            this->jlf_line_attrs.push_back(
                                    string_attr(lr, &logline::L_OPID));
                        }
                        if (lv_iter->lv_identifier) {
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
            if (!this->jlf_hide_extra) {
                for (size_t lpc = 0;
                     lpc < this->jlf_line_values.size(); lpc++) {
                    static const intern_string_t body_name = intern_string::lookup(
                            "body", -1);
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
                    } while (nl_pos != std::string::npos &&
                             nl_pos < str.size());
                    lv.lv_origin.lr_end = this->jlf_cached_line.size();
                }
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
        require(ll.get_sub_offset() < this->jlf_line_offsets.size());

        this_off = this->jlf_line_offsets[ll.get_sub_offset()];
        if (this->jlf_cached_line[this_off] == '\n') {
            this_off += 1;
        }
        next_off = this->jlf_line_offsets[ll.get_sub_offset() + 1];
    }

    if (full_message) {
        sbr.share(this->jlf_share_manager,
                  &this->jlf_cached_line[0],
                  this->jlf_cached_line.size());
    }
    else {
        sbr.share(this->jlf_share_manager,
                  &this->jlf_cached_line[0] + this_off,
                  next_off - this_off);
    }
}

void external_log_format::build(std::vector<std::string> &errors)
{
    if (!this->lf_timestamp_format.empty()) {
        this->lf_timestamp_format.push_back(NULL);
    }
    try {
        this->elf_filename_pcre = new pcrepp(this->elf_file_pattern.c_str());
    }
    catch (const pcrepp::error &e) {
        errors.push_back("error:" +
                         this->elf_name.to_string() + ".file-pattern:" +
                         e.what());
    }
    for (std::map<string, pattern>::iterator iter = this->elf_patterns.begin();
         iter != this->elf_patterns.end();
         ++iter) {
        pattern &pat = iter->second;

        try {
            pat.p_pcre = new pcrepp(pat.p_string.c_str());
        }
        catch (const pcrepp::error &e) {
            errors.push_back("error:" +
                             this->elf_name.to_string() + ".regex[" + iter->first + "]" +
                             ":" +
                             e.what());
            continue;
        }
        for (pcre_named_capture::iterator name_iter = pat.p_pcre->named_begin();
             name_iter != pat.p_pcre->named_end();
             ++name_iter) {
            std::map<const intern_string_t, value_def>::iterator value_iter;
            const intern_string_t name = intern_string::lookup(name_iter->pnc_name, -1);

            if (name == this->lf_timestamp_field) {
                pat.p_timestamp_field_index = name_iter->index();
            }
            if (name == this->elf_level_field) {
                pat.p_level_field_index = name_iter->index();
            }
            if (name == this->elf_module_id_field) {
                pat.p_module_field_index = name_iter->index();
            }
            if (name == this->elf_opid_field) {
                pat.p_opid_field_index = name_iter->index();
            }
            if (name == this->elf_body_field) {
                pat.p_body_field_index = name_iter->index();
            }

            value_iter = this->elf_value_defs.find(name);
            if (value_iter != this->elf_value_defs.end()) {
                value_def &vd = value_iter->second;

                vd.vd_index = name_iter->index();
                if (!vd.vd_unit_field.empty()) {
                    vd.vd_unit_field_index = pat.p_pcre->name_index(vd.vd_unit_field.get());
                }
                else {
                    vd.vd_unit_field_index = -1;
                }
                if (vd.vd_column == -1) {
                    vd.vd_column = this->elf_column_count++;
                }
                pat.p_value_by_index.push_back(vd);
            }
        }

        stable_sort(pat.p_value_by_index.begin(), pat.p_value_by_index.end());

        if (!this->elf_level_field.empty() && pat.p_level_field_index == -1) {
            log_warning("%s:level field '%s' not found in pattern",
                        pat.p_config_path.c_str(),
                        this->elf_level_field.get());
        }
        if (!this->elf_module_id_field.empty() && pat.p_module_field_index == -1) {
            log_warning("%s:module field '%s' not found in pattern",
                        pat.p_config_path.c_str(),
                        this->elf_module_id_field.get());
        }
        if (!this->elf_body_field.empty() && pat.p_body_field_index == -1) {
            log_warning("%s:body field '%s' not found in pattern",
                        pat.p_config_path.c_str(),
                        this->elf_body_field.get());
        }

        this->elf_pattern_order.push_back(&iter->second);
    }

    if (this->jlf_json) {
        if (!this->elf_patterns.empty()) {
            errors.push_back("error:" +
                             this->elf_name.to_string() +
                             ": JSON logs cannot have regexes");
        }
        if (this->jlf_json) {
            this->jlf_parse_context.reset(new yajlpp_parse_context(this->elf_name.to_string()));
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
                             this->elf_name.to_string() +
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
                             this->elf_name.to_string() + ".level:" + e.what());
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
                    this->elf_name.to_string() + ":" + iter->first.get() +
                    ": cannot find action -- " + (*act_iter));
            }
        }
    }

    if (!this->jlf_json && this->elf_samples.empty()) {
        errors.push_back("error:" +
            this->elf_name.to_string() +
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

            if (!pat.p_pcre) {
                continue;
            }

            if (!pat.p_module_format &&
                    pat.p_pcre->name_index(this->lf_timestamp_field.to_string()) < 0) {
                errors.push_back("error:" +
                    this->elf_name.to_string() +
                    ":timestamp field '" +
                    this->lf_timestamp_field.get() +
                    "' not found in pattern -- " +
                    pat.p_string);
                continue;
            }

            if (pat.p_pcre->match(pc, pi)) {
                if (pat.p_module_format) {
                    found = true;
                    continue;
                }
                pcre_context::capture_t *ts_cap =
                        pc[this->lf_timestamp_field.get()];
                const char *ts = pi.get_substr_start(ts_cap);
                ssize_t ts_len = pc[this->lf_timestamp_field.get()]->length();
                const char *const *custom_formats = this->get_timestamp_formats();
                date_time_scanner dts;
                struct timeval tv;
                struct exttm tm;

                if (ts_cap->c_begin == 0) {
                    pat.p_timestamp_end = ts_cap->c_end;
                }
                found = true;
                if (ts_len == -1 || dts.scan(ts, ts_len, custom_formats, &tm, tv) == NULL) {
                    errors.push_back("error:" +
                        this->elf_name.to_string() +
                        ":invalid sample -- " +
                        iter->s_line);
                    errors.push_back("error:" +
                        this->elf_name.to_string() +
                        ":unrecognized timestamp format -- " + ts);

                    if (custom_formats == NULL) {
                        for (int lpc = 0; PTIMEC_FORMATS[lpc].pf_fmt != NULL; lpc++) {
                            off_t off = 0;

                            PTIMEC_FORMATS[lpc].pf_func(&tm, ts, off, ts_len);
                            errors.push_back("  format: " +
                                             string(PTIMEC_FORMATS[lpc].pf_fmt) +
                                             "; matched: " + string(ts, off));
                        }
                    }
                    else {
                        for (int lpc = 0; custom_formats[lpc] != NULL; lpc++) {
                            off_t off = 0;

                            ptime_fmt(custom_formats[lpc], &tm, ts, off, ts_len);
                            errors.push_back("  format: " +
                                             string(custom_formats[lpc]) +
                                             "; matched: " + string(ts, off));
                        }
                    }
                }
            }
        }

        if (!found) {
            errors.push_back("error:" +
                             this->elf_name.to_string() +
                             ":invalid sample         -- " +
                             iter->s_line);

            for (std::vector<pattern *>::iterator pat_iter = this->elf_pattern_order.begin();
                 pat_iter != this->elf_pattern_order.end();
                 ++pat_iter) {
                pattern &pat = *(*pat_iter);

                if (!pat.p_pcre) {
                    continue;
                }

                size_t partial_len = pat.p_pcre->match_partial(pi);

                if (partial_len > 0) {
                    errors.push_back("error:" +
                                     this->elf_name.to_string() +
                                     ":partial sample matched -- " +
                                     iter->s_line.substr(0, partial_len));
                    errors.push_back("error:  against pattern -- " +
                                     (*pat_iter)->p_string);
                }
                else {
                    errors.push_back("error:" +
                                     this->elf_name.to_string() +
                                     ":no partial match found");
                }
            }
        }
    }
}

bool external_log_format::match_samples(const vector<sample> &samples) const
{
    for (vector<sample>::const_iterator sample_iter = samples.begin();
         sample_iter != samples.end();
         ++sample_iter) {
        for (std::vector<external_log_format::pattern *>::const_iterator pat_iter = this->elf_pattern_order.begin();
             pat_iter != this->elf_pattern_order.end();
             ++pat_iter) {
            pattern &pat = *(*pat_iter);

            if (!pat.p_pcre) {
                continue;
            }

            pcre_context_static<128> pc;
            pcre_input pi(sample_iter->s_line);

            if (pat.p_pcre->match(pc, pi)) {
                return true;
            }
        }
    }

    return false;
}

class external_log_table : public log_format_vtab_impl {
public:
    external_log_table(const external_log_format &elf) :
        log_format_vtab_impl(elf), elt_format(elf) {
    };

    void get_columns(vector<vtab_column> &cols) {
        std::map<const intern_string_t, external_log_format::value_def>::const_iterator iter;
        const external_log_format &elf = this->elt_format;

        cols.resize(elf.elf_value_defs.size());
        for (iter = elf.elf_value_defs.begin();
             iter != elf.elf_value_defs.end();
             ++iter) {
            const external_log_format::value_def &vd = iter->second;
            int type = 0;

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

    virtual bool next(log_cursor &lc, logfile_sub_source &lss)
    {
        lc.lc_curr_line = lc.lc_curr_line + vis_line_t(1);
        lc.lc_sub_index = 0;

        if (lc.is_eof()) {
            return true;
        }

        content_line_t    cl(lss.at(lc.lc_curr_line));
        logfile *         lf      = lss.find(cl);
        logfile::iterator lf_iter = lf->begin() + cl;
        uint8_t mod_id = lf_iter->get_module_id();

        if (lf_iter->get_level() & logline::LEVEL_CONTINUED) {
            return false;
        }

        log_format *format = lf->get_format();

        this->elt_module_format.mf_mod_format = NULL;
        if (format->get_name() == this->lfvi_format.get_name()) {
            return true;
        } else if (mod_id && mod_id == this->lfvi_format.lf_mod_index) {
            std::vector<logline_value> values;
            shared_buffer_ref body_ref;
            struct line_range mod_name_range;
            intern_string_t mod_name;
            shared_buffer_ref line;

            lf->read_line(lf_iter, line);
            this->vi_attrs.clear();
            format->annotate(line, this->vi_attrs, values, false);
            this->elt_container_body = find_string_attr_range(this->vi_attrs, &textview_curses::SA_BODY);
            if (!this->elt_container_body.is_valid()) {
                return false;
            }
            this->elt_container_body.ltrim(line.get_data());
            body_ref.subset(line,
                            this->elt_container_body.lr_start,
                            this->elt_container_body.length());
            mod_name_range = find_string_attr_range(this->vi_attrs,
                                                    &logline::L_MODULE);
            if (!mod_name_range.is_valid()) {
                return false;
            }
            mod_name = intern_string::lookup(
                    &line.get_data()[mod_name_range.lr_start],
                    mod_name_range.length());
            this->vi_attrs.clear();
            this->elt_module_format = external_log_format::MODULE_FORMATS[mod_name];
            if (!this->elt_module_format.mf_mod_format) {
                return false;
            }
            return this->elt_module_format.mf_mod_format->get_name() ==
                    this->lfvi_format.get_name();
        }

        return false;
    };

    virtual void extract(logfile *lf,
                         shared_buffer_ref &line,
                         std::vector<logline_value> &values)
    {
        log_format *format = lf->get_format();

        if (this->elt_module_format.mf_mod_format != NULL) {
            shared_buffer_ref body_ref;

            body_ref.subset(line, this->elt_container_body.lr_start,
                            this->elt_container_body.length());
            this->vi_attrs.clear();
            values.clear();
            this->elt_module_format.mf_mod_format->annotate(body_ref, this->vi_attrs, values);
        }
        else {
            this->vi_attrs.clear();
            format->annotate(line, this->vi_attrs, values, false);
        }
    };

    const external_log_format &elt_format;
    module_format elt_module_format;
    struct line_range elt_container_body;
};

log_vtab_impl *external_log_format::get_vtab_impl(void) const
{
    return new external_log_table(*this);
}

/* XXX */
#include "log_format_impls.cc"
