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

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>

#include "yajlpp.hh"
#include "yajlpp_def.hh"
#include "sql_util.hh"
#include "log_format.hh"
#include "log_vtab_impl.hh"
#include "ptimec.hh"
#include "log_search_table.hh"
#include "command_executor.hh"

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

string_attr_type logline::L_PREFIX("prefix");
string_attr_type logline::L_TIMESTAMP("timestamp");
string_attr_type logline::L_FILE("file");
string_attr_type logline::L_PARTITION("partition");
string_attr_type logline::L_MODULE("module");
string_attr_type logline::L_OPID("opid");
string_attr_type logline::L_META("meta");

external_log_format::mod_map_t external_log_format::MODULE_FORMATS;
std::vector<external_log_format *> external_log_format::GRAPH_ORDERED_FORMATS;

struct line_range logline_value::origin_in_full_msg(const char *msg, size_t len) const
{
    if (this->lv_sub_offset == 0) {
        return this->lv_origin;
    }

    if (len == -1) {
        len = strlen(msg);
    }

    struct line_range retval = this->lv_origin;
    const char *last = msg, *msg_end = msg + len;

    for (int lpc = 0; lpc < this->lv_sub_offset; lpc++) {
        const auto *next = (const char *) memchr(last, '\n', msg_end - last);
        require(next != NULL);

        next += 1;
        int amount = (next - last);

        retval.lr_start += amount;
        if (retval.lr_end != -1) {
            retval.lr_end += amount;
        }

        last = next + 1;
    }

    if (retval.lr_end == -1) {
        const auto *eol = (const char *) memchr(last, '\n', msg_end - last);

        if (eol == nullptr) {
            retval.lr_end = len;
        } else {
            retval.lr_end = eol - msg;
        }
    }

    return retval;
}

vector<log_format *> log_format::lf_root_formats;

vector<log_format *> &log_format::get_root_formats()
{
    return lf_root_formats;
}

static bool next_format(const std::vector<std::shared_ptr<external_log_format::pattern>> &patterns,
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
    int curr_fmt = -1;
    const char *retval = NULL;
    bool done = false;
    pcre_input pi(line, 0, len);
    pcre_context_static<128> pc;
    va_list args;
    int pat_index = this->last_pattern_index();

    while (!done && next_format(fmt, curr_fmt, pat_index)) {
        va_start(args, tv_out);

        pi.reset(line, 0, len);
        if (!fmt[curr_fmt].pcre.match(pc, pi, PCRE_NO_UTF8_CHECK)) {
            retval = NULL;
        }
        else {
            pcre_context::capture_t *ts = pc[fmt[curr_fmt].pf_timestamp_index];

            for (auto &iter : pc) {
                pcre_context::capture_t *cap = va_arg(
                        args, pcre_context::capture_t *);

                *cap = iter;
            }

            retval = this->lf_date_time.scan(
                    pi.get_substr_start(ts), ts->length(), NULL, tm_out, *tv_out);

            if (retval) {
                if (curr_fmt != pat_index) {
                    this->lf_pattern_locks.emplace_back(0, curr_fmt);
                }
                this->lf_timestamp_flags = tm_out->et_flags;
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
    if ((etm.et_flags & ETF_MONTH_SET) && diff >= (24 * 60 * 60)) {
        off_year = 1;
    } else if (diff >= (24 * 60 * 60)) {
        off_month = 1;
    } else if (!(etm.et_flags & ETF_DAY_SET) && (diff >= (60 * 60))) {
        off_day = 1;
    } else if (!(etm.et_flags & ETF_DAY_SET)) {
        off_hour = 1;
    } else {
        do_change = false;
    }

    if (!do_change) {
        return;
    }
    log_debug("%d:detected time rollover; offsets=%d %d %d %d", dst.size(),
              off_year, off_month, off_day, off_hour);
    for (iter = dst.begin(); iter != dst.end(); iter++) {
        time_t     ot = iter->get_time();
        struct tm otm;

        gmtime_r(&ot, &otm);
        otm.tm_year -= off_year;
        otm.tm_mon  -= off_month;
        otm.tm_mday -= off_day;
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

static int read_json_field(yajlpp_parse_context *ypc, const unsigned char *str, size_t len);

static int read_json_null(yajlpp_parse_context *ypc)
{
    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path();

    jlu->jlu_sub_line_count += jlu->jlu_format->value_line_count(
        field_name, ypc->is_level(1));

    return 1;
}

static int read_json_bool(yajlpp_parse_context *ypc, int val)
{
    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path();

    jlu->jlu_sub_line_count += jlu->jlu_format->value_line_count(
        field_name, ypc->is_level(1));

    return 1;
}

static int read_json_int(yajlpp_parse_context *ypc, long long val)
{
    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path();

    if (jlu->jlu_format->lf_timestamp_field == field_name) {
        long long divisor = jlu->jlu_format->elf_timestamp_divisor;
        struct timeval tv;

        tv.tv_sec = val / divisor;
        tv.tv_usec = (val % divisor) * (1000000.0 / divisor);
        jlu->jlu_base_line->set_time(tv);
    }
    else if (jlu->jlu_format->elf_level_field == field_name) {
        if (jlu->jlu_format->elf_level_pairs.empty()) {
            char level_buf[128];

            snprintf(level_buf, sizeof(level_buf), "%lld", val);

            pcre_input pi(level_buf);
            pcre_context::capture_t level_cap = {0, (int) strlen(level_buf)};

            jlu->jlu_base_line->set_level(jlu->jlu_format->convert_level(pi, &level_cap));
        } else {
            vector<pair<int64_t, log_level_t> >::iterator iter;

            for (iter = jlu->jlu_format->elf_level_pairs.begin();
                 iter != jlu->jlu_format->elf_level_pairs.end();
                 ++iter) {
                if (iter->first == val) {
                    jlu->jlu_base_line->set_level(iter->second);
                    break;
                }
            }
        }
    }

    jlu->jlu_sub_line_count += jlu->jlu_format->value_line_count(
        field_name, ypc->is_level(1));

    return 1;
}

static int read_json_double(yajlpp_parse_context *ypc, double val)
{
    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path();

    if (jlu->jlu_format->lf_timestamp_field == field_name) {
        double divisor = jlu->jlu_format->elf_timestamp_divisor;
        struct timeval tv;

        tv.tv_sec = val / divisor;
        tv.tv_usec = fmod(val, divisor) * (1000000.0 / divisor);
        jlu->jlu_base_line->set_time(tv);
    }

    jlu->jlu_sub_line_count += jlu->jlu_format->value_line_count(
        field_name, ypc->is_level(1));

    return 1;
}

static int json_array_start(void *ctx)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;
    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;

    if (ypc->ypc_path_index_stack.size() == 2) {
        const intern_string_t field_name = ypc->get_path_fragment_i(0);

        jlu->jlu_sub_line_count += jlu->jlu_format->value_line_count(field_name, true);
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
    jlu->jlu_format->jlf_line_values.emplace_back(field_name);

    return 1;
}

static int rewrite_json_bool(yajlpp_parse_context *ypc, int val)
{
    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path();

    if (!ypc->is_level(1) && !jlu->jlu_format->has_value_def(field_name)) {
        return 1;
    }
    jlu->jlu_format->jlf_line_values.emplace_back(field_name, (bool)val);

    return 1;
}

static int rewrite_json_int(yajlpp_parse_context *ypc, long long val)
{
    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path();

    if (!ypc->is_level(1) && !jlu->jlu_format->has_value_def(field_name)) {
        return 1;
    }
    jlu->jlu_format->jlf_line_values.emplace_back(field_name, (int64_t)val);

    return 1;
}

static int rewrite_json_double(yajlpp_parse_context *ypc, double val)
{
    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path();

    if (!ypc->is_level(1) && !jlu->jlu_format->has_value_def(field_name)) {
        return 1;
    }
    jlu->jlu_format->jlf_line_values.emplace_back(field_name, val);

    return 1;
}

static struct json_path_handler json_log_rewrite_handlers[] = {
    json_path_handler("^/\\w+$")
        .add_cb(rewrite_json_null)
        .add_cb(rewrite_json_bool)
        .add_cb(rewrite_json_int)
        .add_cb(rewrite_json_double)
        .add_cb(rewrite_json_field),

    json_path_handler()
};

bool external_log_format::scan_for_partial(shared_buffer_ref &sbr, size_t &len_out)
{
    if (this->elf_type != ELF_TYPE_TEXT) {
        return false;
    }

    auto pat = this->elf_pattern_order[this->last_pattern_index()];
    pcre_input pi(sbr.get_data(), 0, sbr.length());

    if (!this->elf_multiline) {
        len_out = pat->p_pcre->match_partial(pi);
        return true;
    }

    if (pat->p_timestamp_end == -1 || pat->p_timestamp_end > (int)sbr.length()) {
        len_out = 0;
        return false;
    }

    len_out = pat->p_pcre->match_partial(pi);
    return (int)len_out > pat->p_timestamp_end;
}

log_format::scan_result_t external_log_format::scan(nonstd::optional<logfile *> lf,
                                                    std::vector<logline> &dst,
                                                    off_t offset,
                                                    shared_buffer_ref &sbr)
{
    if (this->elf_type == ELF_TYPE_JSON) {
        yajlpp_parse_context &ypc = *(this->jlf_parse_context);
        logline ll(offset, 0, 0, LEVEL_INFO);
        yajl_handle handle = this->jlf_yajl_handle.in();
        json_log_userdata jlu(sbr);

        if (sbr.empty()) {
            return log_format::SCAN_INCOMPLETE;
        }

        const auto *line_data = (const unsigned char *) sbr.get_data();
        size_t line_end = sbr.length();

        if (line_end > 0) {
            line_end -= 1;
        }
        while (line_end > 0 && isspace(line_data[line_end])) {
            line_end -= 1;
        }

        if (line_end == 0 || line_data[line_end] != '}') {
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
        if (yajl_parse(handle, line_data, sbr.length()) == yajl_status_ok &&
            yajl_complete_parse(handle) == yajl_status_ok) {
            if (ll.get_time() == 0) {
                return log_format::SCAN_NO_MATCH;
            }

            for (int lpc = 0; lpc < jlu.jlu_sub_line_count; lpc++) {
                ll.set_sub_offset(lpc);
                if (lpc > 0) {
                    ll.set_level((log_level_t) (ll.get_level_and_flags() |
                        LEVEL_CONTINUED));
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
    int curr_fmt = -1, orig_lock = this->last_pattern_index();
    int pat_index = orig_lock;

    while (::next_format(this->elf_pattern_order, curr_fmt, pat_index)) {
        auto fpat = this->elf_pattern_order[curr_fmt];
        pcrepp *pat = fpat->p_pcre;

        if (fpat->p_module_format) {
            continue;
        }

        if (!pat->match(pc, pi)) {
            if (!this->lf_pattern_locks.empty() && pat_index != -1) {
                log_debug("no match on pattern %d", pat_index);
                curr_fmt = -1;
                pat_index = -1;
            }
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
        uint8_t mod_index = 0, opid = 0;

        if ((last = this->lf_date_time.scan(ts_str,
                                            ts->length(),
                                            this->get_timestamp_formats(),
                                            &log_time_tm,
                                            log_tv)) == NULL) {
            this->lf_date_time.unlock();
            if ((last = this->lf_date_time.scan(ts_str,
                                                ts->length(),
                                                this->get_timestamp_formats(),
                                                &log_time_tm,
                                                log_tv)) == NULL) {
                continue;
            }
        }

        log_level_t level = this->convert_level(pi, level_cap);

        this->lf_timestamp_flags = log_time_tm.et_flags;

        if (!((log_time_tm.et_flags & ETF_DAY_SET) &&
              (log_time_tm.et_flags & ETF_MONTH_SET) &&
              (log_time_tm.et_flags & ETF_YEAR_SET))) {
            this->check_for_new_year(dst, log_time_tm, log_tv);
        }

        if (opid_cap != nullptr) {
            opid = hash_str(pi.get_substr_start(opid_cap), opid_cap->length());
        }

        if (mod_cap != nullptr) {
            intern_string_t mod_name = intern_string::lookup(
                    pi.get_substr_start(mod_cap), mod_cap->length());
            auto mod_iter = MODULE_FORMATS.find(mod_name);

            if (mod_iter == MODULE_FORMATS.end()) {
                mod_index = module_scan(pi, body_cap, mod_name);
            }
            else if (mod_iter->second.mf_mod_format) {
                mod_index = mod_iter->second.mf_mod_format->lf_mod_index;
            }
        }

        for (auto value_index : fpat->p_numeric_value_indexes) {
            const indexed_value_def &ivd = fpat->p_value_by_index[value_index];
            const value_def &vd = *ivd.ivd_value_def;
            pcre_context::capture_t *num_cap = pc[ivd.ivd_index];

            if (num_cap != nullptr && num_cap->is_valid()) {
                const struct scaling_factor *scaling = nullptr;

                if (ivd.ivd_unit_field_index >= 0) {
                    pcre_context::iterator unit_cap = pc[ivd.ivd_unit_field_index];

                    if (unit_cap != nullptr && unit_cap->is_valid()) {
                        intern_string_t unit_val = intern_string::lookup(
                            pi.get_substr_start(unit_cap), unit_cap->length());
                        std::map<const intern_string_t, scaling_factor>::const_iterator unit_iter;

                        unit_iter = vd.vd_unit_scaling.find(unit_val);
                        if (unit_iter != vd.vd_unit_scaling.end()) {
                            const struct scaling_factor &sf = unit_iter->second;

                            scaling = &sf;
                        }
                    }
                }

                const char *num_cap_start = pi.get_substr_start(num_cap);
                const char *num_cap_end = num_cap_start + num_cap->length();
                double dvalue = strtod(num_cap_start, (char **) &num_cap_end);

                if (num_cap_end == num_cap_start + num_cap->length()) {
                    if (scaling != nullptr) {
                        scaling->scale(dvalue);
                    }
                    this->lf_value_stats[vd.vd_values_index].add_value(dvalue);
                }
            }
        }

        dst.emplace_back(offset, log_tv, level, mod_index, opid);

        if (orig_lock != curr_fmt) {
            log_debug("changing pattern lock %d -> %d", orig_lock, curr_fmt);
            this->lf_pattern_locks.emplace_back(dst.size() - 1, curr_fmt);
        }
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

    for (auto elf : ext_fmts) {
        int curr_fmt = -1, fmt_lock = -1;

        while (::next_format(elf->elf_pattern_order, curr_fmt, fmt_lock)) {
            auto fpat = elf->elf_pattern_order[curr_fmt];
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

void external_log_format::annotate(uint64_t line_number, shared_buffer_ref &line, string_attrs_t &sa,
                                   std::vector<logline_value> &values, bool annotate_module) const
{
    pcre_context_static<128> pc;
    pcre_input pi(line.get_data(), 0, line.length());
    struct line_range lr;
    pcre_context::capture_t *cap, *body_cap, *module_cap = NULL;

    if (this->elf_type != ELF_TYPE_TEXT) {
        values = this->jlf_line_values;
        sa = this->jlf_line_attrs;
        return;
    }

    int pat_index = this->pattern_index_for_line(line_number);
    pattern &pat = *this->elf_pattern_order[pat_index];

    if (!pat.p_pcre->match(pc, pi)) {
        // A continued line still needs a body.
        lr.lr_start = 0;
        lr.lr_end = line.length();
        sa.emplace_back(lr, &textview_curses::SA_BODY);
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
        const indexed_value_def &ivd = pat.p_value_by_index[lpc];
        const struct scaling_factor *scaling = NULL;
        pcre_context::capture_t *cap = pc[ivd.ivd_index];
        const value_def &vd = *ivd.ivd_value_def;
        shared_buffer_ref field;

        if (ivd.ivd_unit_field_index >= 0) {
            pcre_context::iterator unit_cap = pc[ivd.ivd_unit_field_index];

            if (unit_cap != NULL && unit_cap->c_begin != -1) {
                intern_string_t unit_val = intern_string::lookup(
                    pi.get_substr_start(unit_cap), unit_cap->length());
                map<const intern_string_t, scaling_factor>::const_iterator unit_iter;

                unit_iter = vd.vd_unit_scaling.find(unit_val);
                if (unit_iter != vd.vd_unit_scaling.end()) {
                    const struct scaling_factor &sf = unit_iter->second;

                    scaling = &sf;
                }
            }
        }

        field.subset(line, cap->c_begin, cap->length());

        values.emplace_back(vd.vd_name,
                            vd.vd_kind,
                            field,
                            vd.vd_identifier,
                            scaling,
                            vd.vd_column,
                            cap->c_begin,
                            cap->c_end,
                            pat.p_module_format,
                            this);
        values.back().lv_hidden = vd.vd_hidden || vd.vd_user_hidden;
    }

    if (annotate_module && module_cap != NULL && body_cap != NULL &&
            body_cap->is_valid()) {
        intern_string_t mod_name = intern_string::lookup(
                pi.get_substr_start(module_cap), module_cap->length());
        auto mod_iter = MODULE_FORMATS.find(mod_name);

        if (mod_iter != MODULE_FORMATS.end() &&
                mod_iter->second.mf_mod_format != NULL) {
            module_format &mf = mod_iter->second;
            shared_buffer_ref body_ref;

            body_cap->ltrim(line.get_data());
            body_ref.subset(line, body_cap->c_begin, body_cap->length());
            mf.mf_mod_format->annotate(line_number, body_ref, sa, values, false);
            for (auto &value : values) {
                if (!value.lv_from_module) {
                    continue;
                }
                value.lv_origin.lr_start += body_cap->c_begin;
                value.lv_origin.lr_end += body_cap->c_begin;
            }
        }
    }
}

void external_log_format::rewrite(exec_context &ec,
                                  shared_buffer_ref &line,
                                  string_attrs_t &sa,
                                  string &value_out)
{
    vector<logline_value>::iterator iter, shift_iter;
    vector<logline_value> &values = *ec.ec_line_values;

    value_out.assign(line.get_data(), line.length());

    for (iter = values.begin(); iter != values.end(); ++iter) {
        if (!iter->lv_origin.is_valid()) {
            log_debug("not rewriting value with invalid origin -- %s", iter->lv_name.get());
            continue;
        }

        auto vd_iter = this->elf_value_defs.find(iter->lv_name);
        if (vd_iter == this->elf_value_defs.end()) {
            log_debug("not rewriting undefined value -- %s", iter->lv_name.get());
            continue;
        }

        value_def &vd = *vd_iter->second;

        if (vd.vd_rewriter.empty()) {
            continue;
        }

        ec.ec_source.emplace(this->elf_name.to_string() +
                             ":" +
                             vd_iter->first.to_string(),
                             1);
        string field_value = execute_any(ec, vd.vd_rewriter);
        struct line_range adj_origin = iter->origin_in_full_msg(
            value_out.c_str(), value_out.length());

        value_out.erase(adj_origin.lr_start, adj_origin.length());

        int32_t shift_amount = field_value.length() - adj_origin.length();
        value_out.insert(adj_origin.lr_start, field_value);
        for (shift_iter = values.begin();
             shift_iter != values.end(); ++shift_iter) {
            shift_iter->lv_origin.shift(adj_origin.lr_start, shift_amount);
        }
        shift_string_attrs(sa, adj_origin.lr_start, shift_amount);
    }
}

static int read_json_field(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    json_log_userdata *jlu = (json_log_userdata *)ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path();
    struct exttm tm_out;
    struct timeval tv_out;

    if (jlu->jlu_format->lf_timestamp_field == field_name) {
        jlu->jlu_format->lf_date_time.scan((const char *)str, len, jlu->jlu_format->get_timestamp_formats(), &tm_out, tv_out);
        // Leave off the machine oriented flag since we convert it anyhow
        jlu->jlu_format->lf_timestamp_flags = tm_out.et_flags & ~ETF_MACHINE_ORIENTED;
        jlu->jlu_base_line->set_time(tv_out);
    }
    else if (jlu->jlu_format->elf_level_field == field_name) {
        pcre_input pi((const char *) str, 0, len);
        pcre_context::capture_t level_cap = {0, (int) len};

        jlu->jlu_base_line->set_level(jlu->jlu_format->convert_level(pi, &level_cap));
    }
    else if (jlu->jlu_format->elf_opid_field == field_name) {
        uint8_t opid = hash_str((const char *) str, len);
        jlu->jlu_base_line->set_opid(opid);
    }

    jlu->jlu_sub_line_count += jlu->jlu_format->value_line_count(
        field_name, ypc->is_level(1), str, len);

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
        if (jlu->jlu_line->is_time_skewed()) {
            struct timeval tv;
            struct exttm tm;

            jlu->jlu_format->lf_date_time.scan((const char *) str, len,
                                               jlu->jlu_format->get_timestamp_formats(),
                                               &tm, tv);
            sql_strftime(time_buf, sizeof(time_buf), tv, 'T');
        }
        else {
            sql_strftime(time_buf, sizeof(time_buf),
                         jlu->jlu_line->get_timeval(), 'T');
        }
        tmp_shared_buffer tsb(time_buf);
        jlu->jlu_format->jlf_line_values.emplace_back(field_name, tsb.tsb_ref);
    }
    else if (jlu->jlu_shared_buffer.contains((const char *)str)) {
        shared_buffer_ref sbr;

        sbr.subset(jlu->jlu_shared_buffer,
                (off_t) ((const char *)str - jlu->jlu_line_value),
                len);
        if (field_name == jlu->jlu_format->elf_body_field) {
            jlu->jlu_format->jlf_line_values.emplace_back(body_name, sbr);
        }
        if (!ypc->is_level(1) && !jlu->jlu_format->has_value_def(field_name)) {
            return 1;
        }

        jlu->jlu_format->jlf_line_values.emplace_back(field_name, sbr);
    }
    else {
        tmp_shared_buffer tsb((const char *)str, len);

        if (field_name == jlu->jlu_format->elf_body_field) {
            jlu->jlu_format->jlf_line_values.emplace_back(body_name, tsb.tsb_ref);
        }
        if (!ypc->is_level(1) && !jlu->jlu_format->has_value_def(field_name)) {
            return 1;
        }

        jlu->jlu_format->jlf_line_values.emplace_back(field_name, tsb.tsb_ref);
    }

    return 1;
}

void external_log_format::get_subline(const logline &ll, shared_buffer_ref &sbr, bool full_message)
{
    if (this->elf_type == ELF_TYPE_TEXT) {
        return;
    }

    if (this->jlf_cached_offset != ll.get_offset() ||
        this->jlf_cached_full != full_message) {
        yajlpp_parse_context &ypc = *(this->jlf_parse_context);
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
            bool used_values[this->jlf_line_values.size()];
            struct line_range lr;

            memset(used_values, 0, sizeof(used_values));

            for (lv_iter = this->jlf_line_values.begin();
                 lv_iter != this->jlf_line_values.end();
                 ++lv_iter) {
                lv_iter->lv_format = this;
                auto vd_iter = this->elf_value_defs.find(lv_iter->lv_name);
                if (vd_iter != this->elf_value_defs.end()) {
                    lv_iter->lv_identifier = vd_iter->second->vd_identifier;
                    lv_iter->lv_column = vd_iter->second->vd_column;
                    lv_iter->lv_hidden = vd_iter->second->vd_hidden;
                    lv_iter->lv_user_hidden = vd_iter->second->vd_user_hidden;
                } else {
                    lv_iter->lv_hidden = this->jlf_hide_extra;
                }
            }

            for (const auto &jfe : this->jlf_line_format) {
                static const intern_string_t ts_field = intern_string::lookup("__timestamp__", -1);
                static const intern_string_t level_field = intern_string::lookup("__level__");
                size_t begin_size = this->jlf_cached_line.size();

                switch (jfe.jfe_type) {
                case JLF_CONSTANT:
                    this->json_append_to_cache(jfe.jfe_default_value.c_str(),
                            jfe.jfe_default_value.size());
                    break;
                case JLF_VARIABLE:
                    lv_iter = find_if(this->jlf_line_values.begin(),
                                      this->jlf_line_values.end(),
                                      logline_value_cmp(&jfe.jfe_value));
                    if (lv_iter != this->jlf_line_values.end()) {
                        string str = lv_iter->to_string();
                        size_t nl_pos = str.find('\n');

                        lr.lr_start = this->jlf_cached_line.size();

                        lv_iter->lv_hidden = lv_iter->lv_user_hidden;
                        if ((int)str.size() > jfe.jfe_max_width) {
                            switch (jfe.jfe_overflow) {
                                case json_format_element::overflow_t::ABBREV: {
                                    this->json_append_to_cache(
                                        str.c_str(), str.size());
                                    size_t new_size = abbreviate_str(
                                        &this->jlf_cached_line[lr.lr_start],
                                        str.size(),
                                        jfe.jfe_max_width);

                                    this->jlf_cached_line.resize(
                                        lr.lr_start + new_size);
                                    break;
                                }
                                case json_format_element::overflow_t::TRUNCATE: {
                                    this->json_append_to_cache(
                                        str.c_str(), jfe.jfe_max_width);
                                    break;
                                }
                                case json_format_element::overflow_t::DOTDOT: {
                                    size_t middle = (jfe.jfe_max_width / 2) - 1;
                                    this->json_append_to_cache(
                                        str.c_str(), middle);
                                    this->json_append_to_cache("..", 2);
                                    size_t rest = (jfe.jfe_max_width - middle - 2);
                                    this->json_append_to_cache(
                                        str.c_str() + str.size() - rest, rest);
                                    break;
                                }
                            }
                        }
                        else {
                            this->json_append(jfe, str.c_str(), str.size());
                        }

                        if (nl_pos == string::npos || full_message) {
                            lr.lr_end = this->jlf_cached_line.size();
                        } else {
                            lr.lr_end = lr.lr_start + nl_pos;
                        }

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
                        lv_iter->lv_origin = lr;
                        used_values[distance(this->jlf_line_values.begin(),
                                             lv_iter)] = true;
                    }
                    else if (jfe.jfe_value == ts_field) {
                        struct line_range lr;
                        ssize_t ts_len;
                        char ts[64];

                        if (jfe.jfe_ts_format.empty()) {
                            ts_len = sql_strftime(ts, sizeof(ts),
                                                  ll.get_timeval(), 'T');
                        } else {
                            struct exttm et;

                            ll.to_exttm(et);
                            ts_len = ftime_fmt(ts, sizeof(ts),
                                               jfe.jfe_ts_format.c_str(),
                                               et);
                        }
                        lr.lr_start = this->jlf_cached_line.size();
                        this->json_append_to_cache(ts, ts_len);
                        lr.lr_end = this->jlf_cached_line.size();
                        this->jlf_line_attrs.emplace_back(lr, &logline::L_TIMESTAMP);

                        lv_iter = find_if(this->jlf_line_values.begin(),
                                          this->jlf_line_values.end(),
                                          logline_value_cmp(&this->lf_timestamp_field));
                        if (lv_iter != this->jlf_line_values.end()) {
                            used_values[distance(this->jlf_line_values.begin(),
                                                 lv_iter)] = true;
                        }
                    }
                    else if (jfe.jfe_value == level_field) {
                        this->json_append(jfe, ll.get_level_name(), -1);
                    }
                    else {
                        this->json_append(jfe,
                                          jfe.jfe_default_value.c_str(),
                                          jfe.jfe_default_value.size());
                    }

                    switch (jfe.jfe_text_transform) {
                        case external_log_format::json_format_element::transform_t::NONE:
                            break;
                        case external_log_format::json_format_element::transform_t::UPPERCASE:
                            for (size_t cindex = begin_size; cindex < this->jlf_cached_line.size(); cindex++) {
                                this->jlf_cached_line[cindex] = toupper(this->jlf_cached_line[cindex]);
                            }
                            break;
                        case external_log_format::json_format_element::transform_t::LOWERCASE:
                            for (size_t cindex = begin_size; cindex < this->jlf_cached_line.size(); cindex++) {
                                this->jlf_cached_line[cindex] = tolower(this->jlf_cached_line[cindex]);
                            }
                            break;
                        case external_log_format::json_format_element::transform_t::CAPITALIZE:
                            for (size_t cindex = begin_size; cindex < begin_size + 1; cindex++) {
                                this->jlf_cached_line[cindex] = toupper(this->jlf_cached_line[cindex]);
                            }
                            for (size_t cindex = begin_size + 1; cindex < this->jlf_cached_line.size(); cindex++) {
                                this->jlf_cached_line[cindex] = tolower(this->jlf_cached_line[cindex]);
                            }
                            break;
                    }
                    break;
                }
            }
            this->json_append_to_cache("\n", 1);
            int sub_offset = 1;

            for (size_t lpc = 0; lpc < this->jlf_line_values.size(); lpc++) {
                static const intern_string_t body_name = intern_string::lookup(
                    "body", -1);
                logline_value &lv = this->jlf_line_values[lpc];

                if (lv.lv_hidden || used_values[lpc] || body_name == lv.lv_name) {
                    continue;
                }

                const std::string str = lv.to_string();
                size_t curr_pos = 0, nl_pos, line_len = -1;

                lv.lv_sub_offset = sub_offset;
                lv.lv_origin.lr_start = 2 + lv.lv_name.size() + 2;
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
                    sub_offset += 1;
                } while (nl_pos != std::string::npos &&
                         nl_pos < str.size());
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
        this->jlf_cached_full = full_message;
    }

    off_t this_off = 0, next_off = 0;

    if (!this->jlf_line_offsets.empty()) {
        require(ll.get_sub_offset() < this->jlf_line_offsets.size());

        this_off = this->jlf_line_offsets[ll.get_sub_offset()];
        if (this->jlf_cached_line[this_off] == '\n') {
            this_off += 1;
        }
        if ((ll.get_sub_offset() + 1) < (int)this->jlf_line_offsets.size()) {
            next_off = this->jlf_line_offsets[ll.get_sub_offset() + 1];
        }
        else {
            next_off = this_off;
        }
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

void external_log_format::build(std::vector<std::string> &errors) {
    if (!this->lf_timestamp_field.empty()) {
        auto &vd = this->elf_value_defs[this->lf_timestamp_field];
        if (vd.get() == nullptr) {
            vd = make_shared<external_log_format::value_def>();
        }
        vd->vd_name = this->lf_timestamp_field;
        vd->vd_kind = logline_value::VALUE_TEXT;
        vd->vd_internal = true;
    }
    if (!this->elf_level_field.empty() && this->elf_value_defs.
        find(this->elf_level_field) == this->elf_value_defs.end()) {
        auto &vd = this->elf_value_defs[this->elf_level_field];
        if (vd.get() == nullptr) {
            vd = make_shared<external_log_format::value_def>();
        }
        vd->vd_name = this->elf_level_field;
        vd->vd_kind = logline_value::VALUE_TEXT;
        vd->vd_internal = true;
    }
    if (!this->elf_body_field.empty()) {
        auto &vd = this->elf_value_defs[this->elf_body_field];
        if (vd.get() == nullptr) {
            vd = make_shared<external_log_format::value_def>();
        }
        vd->vd_name = this->elf_body_field;
        vd->vd_kind = logline_value::VALUE_TEXT;
        vd->vd_internal = true;
    }

    if (!this->lf_timestamp_format.empty()) {
        this->lf_timestamp_format.push_back(NULL);
    }
    try {
        this->elf_filename_pcre = new pcrepp(this->elf_file_pattern);
    }
    catch (const pcrepp::error &e) {
        errors.push_back("error:" +
                         this->elf_name.to_string() + ".file-pattern:" +
                         e.what());
    }
    for (auto iter = this->elf_patterns.begin();
         iter != this->elf_patterns.end();
         ++iter) {
        pattern &pat = *iter->second;

        if (pat.p_module_format) {
            this->elf_has_module_format = true;
        }

        try {
            pat.p_pcre = new pcrepp(pat.p_string);
        }
        catch (const pcrepp::error &e) {
            errors.push_back("error:" +
                             this->elf_name.to_string() + ".regex[" +
                             iter->first + "]" +
                             ":" +
                             e.what());
            errors.push_back("error:" +
                             this->elf_name.to_string() + ".regex[" +
                             iter->first + "]" +
                             ":" + pat.p_string);
            errors.push_back("error:" +
                             this->elf_name.to_string() + ".regex[" +
                             iter->first + "]" +
                             ":" + string(e.e_offset, ' ') +
                             "^");
            continue;
        }
        for (pcre_named_capture::iterator name_iter = pat.p_pcre->named_begin();
             name_iter != pat.p_pcre->named_end();
             ++name_iter) {
            const intern_string_t name = intern_string::lookup(
                name_iter->pnc_name, -1);

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

            auto value_iter = this->elf_value_defs.find(name);
            if (value_iter != this->elf_value_defs.end()) {
                auto &vd = *value_iter->second;
                indexed_value_def ivd;

                ivd.ivd_index = name_iter->index();
                if (!vd.vd_unit_field.empty()) {
                    ivd.ivd_unit_field_index = pat.p_pcre->name_index(
                        vd.vd_unit_field.get());
                }
                else {
                    ivd.ivd_unit_field_index = -1;
                }
                if (!vd.vd_internal && vd.vd_column == -1) {
                    vd.vd_column = this->elf_column_count++;
                }
                ivd.ivd_value_def = value_iter->second.get();
                pat.p_value_by_index.push_back(ivd);
            }
        }

        stable_sort(pat.p_value_by_index.begin(), pat.p_value_by_index.end());

        for (int lpc = 0; lpc < (int)pat.p_value_by_index.size(); lpc++) {
            auto &ivd = pat.p_value_by_index[lpc];
            auto &vd = *ivd.ivd_value_def;

            if (!vd.vd_foreign_key && !vd.vd_identifier) {
                switch (vd.vd_kind) {
                    case logline_value::VALUE_INTEGER:
                    case logline_value::VALUE_FLOAT:
                        pat.p_numeric_value_indexes.push_back(lpc);
                        break;
                    default:
                        break;
                }
            }
        }

        if (!this->elf_level_field.empty() && pat.p_level_field_index == -1) {
            log_warning("%s:level field '%s' not found in pattern",
                        pat.p_config_path.c_str(),
                        this->elf_level_field.get());
        }
        if (!this->elf_module_id_field.empty() &&
            pat.p_module_field_index == -1) {
            log_warning("%s:module field '%s' not found in pattern",
                        pat.p_config_path.c_str(),
                        this->elf_module_id_field.get());
        }
        if (!this->elf_body_field.empty() && pat.p_body_field_index == -1) {
            log_warning("%s:body field '%s' not found in pattern",
                        pat.p_config_path.c_str(),
                        this->elf_body_field.get());
        }

        this->elf_pattern_order.push_back(iter->second);
    }

    if (this->elf_type != ELF_TYPE_TEXT) {
        if (!this->elf_patterns.empty()) {
            errors.push_back("error:" +
                             this->elf_name.to_string() +
                             ": structured logs cannot have regexes");
        }
        if (this->elf_type == ELF_TYPE_JSON) {
            this->jlf_parse_context.reset(
                new yajlpp_parse_context(this->elf_name.to_string()));
            this->jlf_yajl_handle.reset(yajl_alloc(
                &this->jlf_parse_context->ypc_callbacks,
                NULL,
                this->jlf_parse_context.get()));
            yajl_config(this->jlf_yajl_handle.in(), yajl_dont_validate_strings,
                        1);
        }

    }
    else {
        if (this->elf_patterns.empty()) {
            errors.push_back("error:" +
                             this->elf_name.to_string() +
                             ": no regexes specified for format");
        }
    }

    for (auto &elf_level_pattern : this->elf_level_patterns) {
        try {
            elf_level_pattern.second.lp_pcre = new pcrepp(
                elf_level_pattern.second.lp_regex.c_str());
        }
        catch (const pcrepp::error &e) {
            errors.push_back("error:" +
                             this->elf_name.to_string() + ".level:" + e.what());
        }
    }

    stable_sort(this->elf_level_pairs.begin(), this->elf_level_pairs.end());

    for (auto &elf_value_def : this->elf_value_defs) {
        std::vector<std::string>::iterator act_iter;

        if (!elf_value_def.second->vd_internal &&
            elf_value_def.second->vd_column == -1) {
            elf_value_def.second->vd_column = this->elf_column_count++;
        }

        if (elf_value_def.second->vd_kind == logline_value::VALUE_UNKNOWN) {
            elf_value_def.second->vd_kind = logline_value::VALUE_TEXT;
        }

        for (act_iter = elf_value_def.second->vd_action_list.begin();
             act_iter != elf_value_def.second->vd_action_list.end();
             ++act_iter) {
            if (this->lf_action_defs.find(*act_iter) ==
                this->lf_action_defs.end()) {
                errors.push_back("error:" +
                                 this->elf_name.to_string() + ":" +
                                     elf_value_def.first.get() +
                                 ": cannot find action -- " + (*act_iter));
            }
        }
    }

    if (this->elf_type == ELF_TYPE_TEXT && this->elf_samples.empty()) {
        errors.push_back("error:" +
                         this->elf_name.to_string() +
                         ":no sample logs provided, all formats must have samples");
    }

    for (auto &elf_sample : this->elf_samples) {
        pcre_context_static<128> pc;
        pcre_input pi(elf_sample.s_line);
        bool found = false;

        for (auto pat_iter = this->elf_pattern_order.begin();
             pat_iter != this->elf_pattern_order.end() && !found;
             ++pat_iter) {
            pattern &pat = *(*pat_iter);

            if (!pat.p_pcre) {
                continue;
            }

            if (!pat.p_module_format &&
                pat.p_pcre->name_index(this->lf_timestamp_field.to_string()) <
                0) {
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
                pcre_context::capture_t *level_cap = pc[pat.p_level_field_index];
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
                if (ts_len == -1 ||
                    dts.scan(ts, ts_len, custom_formats, &tm, tv) == NULL) {
                    errors.push_back("error:" +
                                     this->elf_name.to_string() +
                                     ":invalid sample -- " +
                                         elf_sample.s_line);
                    errors.push_back("error:" +
                                     this->elf_name.to_string() +
                                     ":unrecognized timestamp format -- " + ts);

                    if (custom_formats == NULL) {
                        for (int lpc = 0;
                             PTIMEC_FORMATS[lpc].pf_fmt != NULL; lpc++) {
                            off_t off = 0;

                            PTIMEC_FORMATS[lpc].pf_func(&tm, ts, off, ts_len);
                            errors.push_back("  format: " +
                                             string(
                                                 PTIMEC_FORMATS[lpc].pf_fmt) +
                                             "; matched: " + string(ts, off));
                        }
                    }
                    else {
                        for (int lpc = 0; custom_formats[lpc] != NULL; lpc++) {
                            off_t off = 0;

                            ptime_fmt(custom_formats[lpc], &tm, ts, off,
                                      ts_len);
                            errors.push_back("  format: " +
                                             string(custom_formats[lpc]) +
                                             "; matched: " + string(ts, off));
                        }
                    }
                }

                log_level_t level = this->convert_level(pi, level_cap);

                if (elf_sample.s_level != LEVEL_UNKNOWN) {
                    if (elf_sample.s_level != level) {
                        errors.push_back("error:" +
                                         this->elf_name.to_string() +
                                         ":invalid sample -- " +
                                             elf_sample.s_line);
                        errors.push_back("error:" +
                                         this->elf_name.to_string() +
                                         ":parsed level '" +
                                         level_names[level] +
                                         "' does not match expected level of '" +
                                         level_names[elf_sample.s_level] +
                                         "'");
                    }
                }
            }
        }

        if (!found) {
            errors.push_back("error:" +
                             this->elf_name.to_string() +
                             ":invalid sample         -- " +
                                 elf_sample.s_line);

            for (auto pat_iter = this->elf_pattern_order.begin();
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
                                         elf_sample.s_line.substr(0, partial_len));
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

    for (auto &elf_value_def : this->elf_value_defs) {
        if (elf_value_def.second->vd_foreign_key || elf_value_def.second->vd_identifier) {
            continue;
        }

        switch (elf_value_def.second->vd_kind) {
            case logline_value::VALUE_INTEGER:
            case logline_value::VALUE_FLOAT:
                elf_value_def.second->vd_values_index = this->elf_numeric_value_defs.size();
                this->elf_numeric_value_defs.push_back(elf_value_def.second);
                break;
            default:
                break;
        }
    }

    this->lf_value_stats.resize(this->elf_numeric_value_defs.size());

    int format_index = 0;
    for (auto iter = this->jlf_line_format.begin();
         iter != this->jlf_line_format.end();
         ++iter, format_index++) {
        static const intern_string_t ts = intern_string::lookup("__timestamp__");
        static const intern_string_t level_field = intern_string::lookup("__level__");
        json_format_element &jfe = *iter;

        if (jfe.jfe_value.empty() && !jfe.jfe_ts_format.empty()) {
            jfe.jfe_value = ts;
        }

        switch (jfe.jfe_type) {
            case JLF_VARIABLE: {
                auto vd_iter = this->elf_value_defs.find(jfe.jfe_value);
                if (jfe.jfe_value == ts) {
                    this->elf_value_defs[this->lf_timestamp_field]->vd_hidden = true;
                } else if (jfe.jfe_value == level_field) {
                    this->elf_value_defs[this->elf_level_field]->vd_hidden = true;
                } else if (vd_iter == this->elf_value_defs.end()) {
                    char index_str[32];

                    snprintf(index_str, sizeof(index_str), "%d", format_index);
                    errors.push_back("error:" +
                                     this->elf_name.to_string() +
                                     ":line_format[" +
                                     index_str +
                                     "]:line format variable is not defined -- " +
                                     jfe.jfe_value.to_string());
                }
                break;
            }
            default:
                break;
        }
    }

    for (auto &hd_pair : this->elf_highlighter_patterns) {
        external_log_format::highlighter_def &hd = hd_pair.second;
        const std::string &pattern = hd.hd_pattern;
        std::string errmsg;
        const char *errptr;
        rgb_color fg, bg;
        int eoff, attrs = 0;

        if (!hd.hd_color.empty()) {
            if (!rgb_color::from_str(hd.hd_color, fg, errmsg)) {
                errors.push_back("error:"
                                 + this->elf_name.to_string()
                                 + ":highlighters/"
                                 + hd_pair.first.to_string()
                                 + "/color:"
                                 + errmsg);
            }
        }

        if (!hd.hd_background_color.empty()) {
            if (!rgb_color::from_str(hd.hd_background_color, bg, errmsg)) {
                errors.push_back("error:"
                                 + this->elf_name.to_string()
                                 + ":highlighters/"
                                 + hd_pair.first.to_string()
                                 + "/color:"
                                 + errmsg);
            }
        }

        if (hd.hd_underline) {
            attrs |= A_UNDERLINE;
        }
        if (hd.hd_blink) {
            attrs |= A_BLINK;
        }

        pcre *code = pcre_compile(pattern.c_str(),
                                  0,
                                  &errptr,
                                  &eoff,
                                  nullptr);

        if (code == nullptr) {
            errors.push_back("error:"
                             + this->elf_name.to_string()
                             + ":highlighters/"
                             + hd_pair.first.to_string()
                             + ":"
                             + string(errptr));
            errors.push_back("error:"
                             + this->elf_name.to_string()
                             + ":highlighters/"
                             + hd_pair.first.to_string()
                             + ":"
                             + pattern);
            errors.push_back("error:"
                             + this->elf_name.to_string()
                             + ":highlighters/"
                             + hd_pair.first.to_string()
                             + ":"
                             + string(eoff, ' ')
                             + "^");
        } else {
            this->lf_highlighters.emplace_back(code);
            this->lf_highlighters.back()
                .with_pattern(pattern)
                .with_format_name(this->elf_name)
                .with_color(fg, bg)
                .with_attrs(attrs);
        }
    }
}

void external_log_format::register_vtabs(log_vtab_manager *vtab_manager,
                                         std::vector<std::string> &errors)
{
    vector<pair<intern_string_t, string> >::iterator search_iter;
    for (search_iter = this->elf_search_tables.begin();
         search_iter != this->elf_search_tables.end();
         ++search_iter) {
        log_search_table *lst;

        try {
            lst = new log_search_table(search_iter->second.c_str(), search_iter->first);
        } catch (pcrepp::error &e) {
            errors.push_back(
                    "error:" +
                    this->elf_name.to_string() +
                    ":" +
                    search_iter->first.to_string() +
                    ":unable to compile regex -- " +
                    search_iter->second);
            continue;
        }

        string errmsg;

        errmsg = vtab_manager->register_vtab(lst);
        if (!errmsg.empty()) {
            errors.push_back(
                "error:" +
                this->elf_name.to_string() +
                ":" +
                search_iter->first.to_string() +
                ":unable to register table -- " +
                errmsg);
        }
    }
}

bool external_log_format::match_samples(const vector<sample> &samples) const
{
    for (const auto &sample_iter : samples) {
        for (const auto &pat_iter : this->elf_pattern_order) {
            pattern &pat = *pat_iter;

            if (!pat.p_pcre) {
                continue;
            }

            pcre_context_static<128> pc;
            pcre_input pi(sample_iter.s_line);

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

    void get_columns(vector<vtab_column> &cols) const {
        const external_log_format &elf = this->elt_format;

        cols.resize(elf.elf_column_count);
        for (const auto &elf_value_def : elf.elf_value_defs) {
            const auto &vd = *elf_value_def.second;
            pair<int, unsigned int> type_pair = log_vtab_impl::logline_value_to_sqlite_type(vd.vd_kind);

            if (vd.vd_column == -1) {
                continue;
            }

            cols[vd.vd_column].vc_name = vd.vd_name.get();
            cols[vd.vd_column].vc_type = type_pair.first;
            cols[vd.vd_column].vc_subtype = type_pair.second;
            cols[vd.vd_column].vc_collator = vd.vd_collate.c_str();
            cols[vd.vd_column].vc_comment = vd.vd_description;
        }
    };

    void get_foreign_keys(std::vector<std::string> &keys_inout) const
    {
        log_vtab_impl::get_foreign_keys(keys_inout);

        for (const auto &elf_value_def : this->elt_format.elf_value_defs) {
            if (elf_value_def.second->vd_foreign_key) {
                keys_inout.push_back(elf_value_def.first.to_string());
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
        shared_ptr<logfile> lf = lss.find(cl);
        auto lf_iter = lf->begin() + cl;
        uint8_t mod_id = lf_iter->get_module_id();

        if (lf_iter->is_continued()) {
            return false;
        }

        log_format *format = lf->get_format();

        this->elt_module_format.mf_mod_format = nullptr;
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
            format->annotate(cl, line, this->vi_attrs, values, false);
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

    virtual void extract(shared_ptr<logfile> lf,
                         uint64_t line_number,
                         shared_buffer_ref &line,
                         std::vector<logline_value> &values)
    {
        log_format *format = lf->get_format();

        if (this->elt_module_format.mf_mod_format != nullptr) {
            shared_buffer_ref body_ref;

            body_ref.subset(line, this->elt_container_body.lr_start,
                            this->elt_container_body.length());
            this->vi_attrs.clear();
            values.clear();
            this->elt_module_format.mf_mod_format->annotate(line_number,
                                                            body_ref,
                                                            this->vi_attrs,
                                                            values,
                                                            false);
        }
        else {
            this->vi_attrs.clear();
            format->annotate(line_number, line, this->vi_attrs, values, false);
        }
    };

    const external_log_format &elt_format;
    module_format elt_module_format;
    struct line_range elt_container_body;
};

log_vtab_impl *external_log_format::get_vtab_impl() const
{
    return new external_log_table(*this);
}

int log_format::pattern_index_for_line(uint64_t line_number) const
{
    auto iter = lower_bound(this->lf_pattern_locks.cbegin(),
                            this->lf_pattern_locks.cend(),
                            line_number,
                            [](const pattern_for_lines &pfl, uint32_t line) {
        return pfl.pfl_line < line;
    });

    if (iter == this->lf_pattern_locks.end() ||
        iter->pfl_line != line_number) {
        --iter;
    }

    return iter->pfl_pat_index;
}

log_format::pattern_for_lines::pattern_for_lines(
    uint32_t pfl_line, uint32_t pfl_pat_index) :
    pfl_line(pfl_line), pfl_pat_index(pfl_pat_index)
{
}

/* XXX */
#include "log_format_impls.cc"
