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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <memory>

#include <fnmatch.h>
#include <stdio.h>
#include <string.h>

#include "base/snippet_highlighters.hh"
#include "base/string_util.hh"
#include "command_executor.hh"
#include "config.h"
#include "fmt/format.h"
#include "lnav_util.hh"
#include "log_format_ext.hh"
#include "log_search_table.hh"
#include "log_vtab_impl.hh"
#include "ptimec.hh"
#include "scn/scn.h"
#include "sql_util.hh"
#include "yajlpp/yajlpp.hh"
#include "yajlpp/yajlpp_def.hh"

using namespace lnav::roles::literals;

static auto intern_lifetime = intern_string::get_table_lifetime();

string_attr_type<void> logline::L_PREFIX("prefix");
string_attr_type<void> logline::L_TIMESTAMP("timestamp");
string_attr_type<std::shared_ptr<logfile>> logline::L_FILE("file");
string_attr_type<bookmark_metadata*> logline::L_PARTITION("partition");
string_attr_type<void> logline::L_MODULE("module");
string_attr_type<void> logline::L_OPID("opid");
string_attr_type<bookmark_metadata*> logline::L_META("meta");

external_log_format::mod_map_t external_log_format::MODULE_FORMATS;
std::vector<std::shared_ptr<external_log_format>>
    external_log_format::GRAPH_ORDERED_FORMATS;

struct line_range
logline_value::origin_in_full_msg(const char* msg, ssize_t len) const
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
        const auto* next = (const char*) memchr(last, '\n', msg_end - last);
        require(next != nullptr);

        next += 1;
        int amount = (next - last);

        retval.lr_start += amount;
        if (retval.lr_end != -1) {
            retval.lr_end += amount;
        }

        last = next + 1;
    }

    if (retval.lr_end == -1) {
        const auto* eol = (const char*) memchr(last, '\n', msg_end - last);

        if (eol == nullptr) {
            retval.lr_end = len;
        } else {
            retval.lr_end = eol - msg;
        }
    }

    return retval;
}

logline_value::logline_value(logline_value_meta lvm,
                             shared_buffer_ref& sbr,
                             struct line_range origin)
    : lv_meta(std::move(lvm)), lv_origin(origin)
{
    if (sbr.get_data() == nullptr) {
        this->lv_meta.lvm_kind = value_kind_t::VALUE_NULL;
    }

    switch (this->lv_meta.lvm_kind) {
        case value_kind_t::VALUE_JSON:
        case value_kind_t::VALUE_XML:
        case value_kind_t::VALUE_STRUCT:
        case value_kind_t::VALUE_TEXT:
        case value_kind_t::VALUE_QUOTED:
        case value_kind_t::VALUE_W3C_QUOTED:
        case value_kind_t::VALUE_TIMESTAMP:
            require(origin.lr_end != -1);
            this->lv_frag = string_fragment::from_byte_range(
                sbr.get_data(), origin.lr_start, origin.lr_end);
            break;

        case value_kind_t::VALUE_NULL:
            break;

        case value_kind_t::VALUE_INTEGER: {
            auto scan_res
                = scn::scan_value<int64_t>(sbr.to_string_view(origin));
            if (scan_res) {
                this->lv_value.i = scan_res.value();
            } else {
                this->lv_value.i = 0;
            }
            break;
        }

        case value_kind_t::VALUE_FLOAT: {
            auto scan_res = scn::scan_value<double>(sbr.to_string_view(origin));
            if (scan_res) {
                this->lv_value.d = scan_res.value();
            } else {
                this->lv_value.d = 0;
            }
            break;
        }

        case value_kind_t::VALUE_BOOLEAN:
            if (strncmp(
                    sbr.get_data_at(origin.lr_start), "true", origin.length())
                    == 0
                || strncmp(
                       sbr.get_data_at(origin.lr_start), "yes", origin.length())
                    == 0)
            {
                this->lv_value.i = 1;
            } else {
                this->lv_value.i = 0;
            }
            break;

        case value_kind_t::VALUE_UNKNOWN:
        case value_kind_t::VALUE__MAX:
            ensure(0);
            break;
    }
}

std::string
logline_value::to_string() const
{
    char buffer[128];

    switch (this->lv_meta.lvm_kind) {
        case value_kind_t::VALUE_NULL:
            return "null";

        case value_kind_t::VALUE_JSON:
        case value_kind_t::VALUE_XML:
        case value_kind_t::VALUE_STRUCT:
        case value_kind_t::VALUE_TEXT:
        case value_kind_t::VALUE_TIMESTAMP:
            if (this->lv_str) {
                return this->lv_str.value();
            }
            if (this->lv_frag.empty()) {
                return this->lv_intern_string.to_string();
            }
            return this->lv_frag.to_string();

        case value_kind_t::VALUE_QUOTED:
        case value_kind_t::VALUE_W3C_QUOTED:
            if (this->lv_frag.empty()) {
                return "";
            } else {
                switch (this->lv_frag.data()[0]) {
                    case '\'':
                    case '"': {
                        auto unquote_func = this->lv_meta.lvm_kind
                                == value_kind_t::VALUE_W3C_QUOTED
                            ? unquote_w3c
                            : unquote;
                        char unquoted_str[this->lv_frag.length()];
                        size_t unquoted_len;

                        unquoted_len = unquote_func(unquoted_str,
                                                    this->lv_frag.data(),
                                                    this->lv_frag.length());
                        return {unquoted_str, unquoted_len};
                    }
                    default:
                        return this->lv_frag.to_string();
                }
            }
            break;

        case value_kind_t::VALUE_INTEGER:
            snprintf(buffer, sizeof(buffer), "%" PRId64, this->lv_value.i);
            break;

        case value_kind_t::VALUE_FLOAT:
            snprintf(buffer, sizeof(buffer), "%lf", this->lv_value.d);
            break;

        case value_kind_t::VALUE_BOOLEAN:
            if (this->lv_value.i) {
                return "true";
            } else {
                return "false";
            }
            break;
        case value_kind_t::VALUE_UNKNOWN:
        case value_kind_t::VALUE__MAX:
            ensure(0);
            break;
    }

    return {buffer};
}

std::vector<std::shared_ptr<log_format>> log_format::lf_root_formats;

std::vector<std::shared_ptr<log_format>>&
log_format::get_root_formats()
{
    return lf_root_formats;
}

static bool
next_format(
    const std::vector<std::shared_ptr<external_log_format::pattern>>& patterns,
    int& index,
    int& locked_index)
{
    bool retval = true;

    if (locked_index == -1) {
        index += 1;
        if (index >= (int) patterns.size()) {
            retval = false;
        }
    } else if (index == locked_index) {
        retval = false;
    } else {
        index = locked_index;
    }

    return retval;
}

bool
log_format::next_format(pcre_format* fmt, int& index, int& locked_index)
{
    bool retval = true;

    if (locked_index == -1) {
        index += 1;
        if (fmt[index].name == nullptr) {
            retval = false;
        }
    } else if (index == locked_index) {
        retval = false;
    } else {
        index = locked_index;
    }

    return retval;
}

const char*
log_format::log_scanf(uint32_t line_number,
                      string_fragment line,
                      pcre_format* fmt,
                      const char* time_fmt[],
                      struct exttm* tm_out,
                      struct timeval* tv_out,

                      string_fragment* ts_out,
                      nonstd::optional<string_fragment>* level_out)
{
    int curr_fmt = -1;
    const char* retval = nullptr;
    bool done = false;
    int pat_index = this->last_pattern_index();

    while (!done && next_format(fmt, curr_fmt, pat_index)) {
        static thread_local auto md = lnav::pcre2pp::match_data::unitialized();

        auto match_res = fmt[curr_fmt]
                             .pcre->capture_from(line)
                             .into(md)
                             .matches(PCRE2_NO_UTF_CHECK)
                             .ignore_error();
        if (!match_res) {
            retval = nullptr;
        } else {
            auto ts = md[fmt[curr_fmt].pf_timestamp_index];

            retval = this->lf_date_time.scan(
                ts->data(), ts->length(), nullptr, tm_out, *tv_out);

            if (retval) {
                *ts_out = ts.value();
                *level_out = md[2];
                if (curr_fmt != pat_index) {
                    uint32_t lock_line;

                    if (this->lf_pattern_locks.empty()) {
                        lock_line = 0;
                    } else {
                        lock_line = line_number;
                    }

                    this->lf_pattern_locks.emplace_back(lock_line, curr_fmt);
                }
                this->lf_timestamp_flags = tm_out->et_flags;
                done = true;
            }
        }
    }

    return retval;
}

void
log_format::check_for_new_year(std::vector<logline>& dst,
                               exttm etm,
                               struct timeval log_tv)
{
    if (dst.empty()) {
        return;
    }

    time_t diff = dst.back().get_time() - log_tv.tv_sec;
    int off_year = 0, off_month = 0, off_day = 0, off_hour = 0;
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
    log_debug("%d:detected time rollover; offsets=%d %d %d %d",
              dst.size(),
              off_year,
              off_month,
              off_day,
              off_hour);
    for (auto& ll : dst) {
        time_t ot = ll.get_time();
        struct tm otm;

        gmtime_r(&ot, &otm);
        if (otm.tm_year < off_year) {
            otm.tm_year = 0;
        } else {
            otm.tm_year -= off_year;
        }
        otm.tm_mon -= off_month;
        if (otm.tm_mon < 0) {
            otm.tm_mon += 12;
        }
        auto new_time = tm2sec(&otm);
        if (new_time == -1) {
            continue;
        }
        new_time -= (off_day * 24 * 60 * 60) + (off_hour * 60 * 60);
        ll.set_time(new_time);
    }
}

/*
 * XXX This needs some cleanup.
 */
struct json_log_userdata {
    json_log_userdata(shared_buffer_ref& sbr, scan_batch_context* sbc)
        : jlu_shared_buffer(sbr), jlu_batch_context(sbc)
    {
    }

    external_log_format* jlu_format{nullptr};
    const logline* jlu_line{nullptr};
    logline* jlu_base_line{nullptr};
    int jlu_sub_line_count{1};
    yajl_handle jlu_handle{nullptr};
    const char* jlu_line_value{nullptr};
    size_t jlu_line_size{0};
    size_t jlu_sub_start{0};
    shared_buffer_ref& jlu_shared_buffer;
    scan_batch_context* jlu_batch_context;
};

static int read_json_field(yajlpp_parse_context* ypc,
                           const unsigned char* str,
                           size_t len);

static int
read_json_null(yajlpp_parse_context* ypc)
{
    json_log_userdata* jlu = (json_log_userdata*) ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path();

    jlu->jlu_sub_line_count
        += jlu->jlu_format->value_line_count(field_name, ypc->is_level(1));

    return 1;
}

static int
read_json_bool(yajlpp_parse_context* ypc, int val)
{
    json_log_userdata* jlu = (json_log_userdata*) ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path();

    jlu->jlu_sub_line_count
        += jlu->jlu_format->value_line_count(field_name, ypc->is_level(1));

    return 1;
}

static int
read_json_int(yajlpp_parse_context* ypc, long long val)
{
    json_log_userdata* jlu = (json_log_userdata*) ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path();

    if (jlu->jlu_format->lf_timestamp_field == field_name) {
        long long divisor = jlu->jlu_format->elf_timestamp_divisor;
        struct timeval tv;

        tv.tv_sec = val / divisor;
        tv.tv_usec = (val % divisor) * (1000000.0 / divisor);
        if (jlu->jlu_format->lf_date_time.dts_local_time) {
            struct tm ltm;
            localtime_r(&tv.tv_sec, &ltm);
#ifdef HAVE_STRUCT_TM_TM_ZONE
            ltm.tm_zone = nullptr;
#endif
            ltm.tm_isdst = 0;
            tv.tv_sec = tm2sec(&ltm);
        }
        jlu->jlu_base_line->set_time(tv);
    } else if (jlu->jlu_format->lf_subsecond_field == field_name) {
        uint64_t millis = 0;
        switch (jlu->jlu_format->lf_subsecond_unit.value()) {
            case log_format::subsecond_unit::milli:
                millis = val;
                break;
            case log_format::subsecond_unit::micro:
                millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::microseconds(val))
                             .count();
                break;
            case log_format::subsecond_unit::nano:
                millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::nanoseconds(val))
                             .count();
                break;
        }
        jlu->jlu_base_line->set_millis(millis);
    } else if (jlu->jlu_format->elf_level_field == field_name) {
        if (jlu->jlu_format->elf_level_pairs.empty()) {
            char level_buf[128];

            snprintf(level_buf, sizeof(level_buf), "%lld", val);

            jlu->jlu_base_line->set_level(jlu->jlu_format->convert_level(
                string_fragment::from_c_str(level_buf),
                jlu->jlu_batch_context));
        } else {
            std::vector<std::pair<int64_t, log_level_t>>::iterator iter;

            for (iter = jlu->jlu_format->elf_level_pairs.begin();
                 iter != jlu->jlu_format->elf_level_pairs.end();
                 ++iter)
            {
                if (iter->first == val) {
                    jlu->jlu_base_line->set_level(iter->second);
                    break;
                }
            }
        }
    }

    jlu->jlu_sub_line_count
        += jlu->jlu_format->value_line_count(field_name, ypc->is_level(1));

    return 1;
}

static int
read_json_double(yajlpp_parse_context* ypc, double val)
{
    json_log_userdata* jlu = (json_log_userdata*) ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path();

    if (jlu->jlu_format->lf_timestamp_field == field_name) {
        double divisor = jlu->jlu_format->elf_timestamp_divisor;
        struct timeval tv;

        tv.tv_sec = val / divisor;
        tv.tv_usec = fmod(val, divisor) * (1000000.0 / divisor);
        if (jlu->jlu_format->lf_date_time.dts_local_time) {
            struct tm ltm;
            localtime_r(&tv.tv_sec, &ltm);
#ifdef HAVE_STRUCT_TM_TM_ZONE
            ltm.tm_zone = nullptr;
#endif
            ltm.tm_isdst = 0;
            tv.tv_sec = tm2sec(&ltm);
        }
        jlu->jlu_base_line->set_time(tv);
    }

    jlu->jlu_sub_line_count
        += jlu->jlu_format->value_line_count(field_name, ypc->is_level(1));

    return 1;
}

static int
json_array_start(void* ctx)
{
    yajlpp_parse_context* ypc = (yajlpp_parse_context*) ctx;
    json_log_userdata* jlu = (json_log_userdata*) ypc->ypc_userdata;

    if (ypc->ypc_path_index_stack.size() == 2) {
        const intern_string_t field_name = ypc->get_path_fragment_i(0);

        jlu->jlu_sub_line_count
            += jlu->jlu_format->value_line_count(field_name, true);
        jlu->jlu_sub_start = yajl_get_bytes_consumed(jlu->jlu_handle) - 1;
    }

    return 1;
}

static int
json_array_end(void* ctx)
{
    yajlpp_parse_context* ypc = (yajlpp_parse_context*) ctx;
    json_log_userdata* jlu = (json_log_userdata*) ypc->ypc_userdata;

    if (ypc->ypc_path_index_stack.size() == 1) {
        const intern_string_t field_name = ypc->get_path_fragment_i(0);
        size_t sub_end = yajl_get_bytes_consumed(jlu->jlu_handle);
        jlu->jlu_format->jlf_line_values.lvv_values.emplace_back(
            jlu->jlu_format->get_value_meta(field_name,
                                            value_kind_t::VALUE_JSON),
            string_fragment::from_byte_range(jlu->jlu_shared_buffer.get_data(),
                                             jlu->jlu_sub_start,
                                             sub_end));
    }

    return 1;
}

static const struct json_path_container json_log_handlers = {
    yajlpp::pattern_property_handler("\\w+")
        .add_cb(read_json_null)
        .add_cb(read_json_bool)
        .add_cb(read_json_int)
        .add_cb(read_json_double)
        .add_cb(read_json_field),
};

static int rewrite_json_field(yajlpp_parse_context* ypc,
                              const unsigned char* str,
                              size_t len);

static int
rewrite_json_null(yajlpp_parse_context* ypc)
{
    json_log_userdata* jlu = (json_log_userdata*) ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path();

    if (!ypc->is_level(1) && !jlu->jlu_format->has_value_def(field_name)) {
        return 1;
    }
    jlu->jlu_format->jlf_line_values.lvv_values.emplace_back(
        jlu->jlu_format->get_value_meta(field_name, value_kind_t::VALUE_NULL));

    return 1;
}

static int
rewrite_json_bool(yajlpp_parse_context* ypc, int val)
{
    json_log_userdata* jlu = (json_log_userdata*) ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path();

    if (!ypc->is_level(1) && !jlu->jlu_format->has_value_def(field_name)) {
        return 1;
    }
    jlu->jlu_format->jlf_line_values.lvv_values.emplace_back(
        jlu->jlu_format->get_value_meta(field_name,
                                        value_kind_t::VALUE_BOOLEAN),
        (bool) val);
    return 1;
}

static int
rewrite_json_int(yajlpp_parse_context* ypc, long long val)
{
    json_log_userdata* jlu = (json_log_userdata*) ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path();

    if (!ypc->is_level(1) && !jlu->jlu_format->has_value_def(field_name)) {
        return 1;
    }
    jlu->jlu_format->jlf_line_values.lvv_values.emplace_back(
        jlu->jlu_format->get_value_meta(field_name,
                                        value_kind_t::VALUE_INTEGER),
        (int64_t) val);
    return 1;
}

static int
rewrite_json_double(yajlpp_parse_context* ypc, double val)
{
    json_log_userdata* jlu = (json_log_userdata*) ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path();

    if (!ypc->is_level(1) && !jlu->jlu_format->has_value_def(field_name)) {
        return 1;
    }
    jlu->jlu_format->jlf_line_values.lvv_values.emplace_back(
        jlu->jlu_format->get_value_meta(field_name, value_kind_t::VALUE_FLOAT),
        val);

    return 1;
}

static const struct json_path_container json_log_rewrite_handlers = {
    yajlpp::pattern_property_handler("\\w+")
        .add_cb(rewrite_json_null)
        .add_cb(rewrite_json_bool)
        .add_cb(rewrite_json_int)
        .add_cb(rewrite_json_double)
        .add_cb(rewrite_json_field),
};

bool
external_log_format::scan_for_partial(shared_buffer_ref& sbr,
                                      size_t& len_out) const
{
    if (this->elf_type != elf_type_t::ELF_TYPE_TEXT) {
        return false;
    }

    const auto& pat = this->elf_pattern_order[this->last_pattern_index()];
    if (!this->lf_multiline) {
        len_out = pat->p_pcre.pp_value->match_partial(sbr.to_string_fragment());
        return true;
    }

    if (pat->p_timestamp_end == -1 || pat->p_timestamp_end > (int) sbr.length())
    {
        len_out = 0;
        return false;
    }

    len_out = pat->p_pcre.pp_value->match_partial(sbr.to_string_fragment());
    return (int) len_out > pat->p_timestamp_end;
}

std::vector<lnav::console::snippet>
external_log_format::get_snippets() const
{
    std::vector<lnav::console::snippet> retval;

    for (const auto& src_pair : this->elf_format_sources) {
        retval.emplace_back(lnav::console::snippet::from(src_pair.first, "")
                                .with_line(src_pair.second));
    }

    return retval;
}

log_format::scan_result_t
external_log_format::scan(logfile& lf,
                          std::vector<logline>& dst,
                          const line_info& li,
                          shared_buffer_ref& sbr,
                          scan_batch_context& sbc)
{
    if (this->elf_type == elf_type_t::ELF_TYPE_JSON) {
        logline ll(li.li_file_range.fr_offset, 0, 0, LEVEL_INFO);
        auto line_frag = sbr.to_string_fragment();

        if (!line_frag.startswith("{")) {
            if (!this->lf_specialized) {
                return log_format::SCAN_NO_MATCH;
            }

            ll.set_time(dst.back().get_timeval());
            ll.set_level(LEVEL_INVALID);
            dst.emplace_back(ll);
            return log_format::SCAN_MATCH;
        }

        auto& ypc = *(this->jlf_parse_context);
        yajl_handle handle = this->jlf_yajl_handle.get();
        json_log_userdata jlu(sbr, &sbc);

        if (!this->lf_specialized && dst.size() >= 3) {
            return log_format::SCAN_NO_MATCH;
        }

        if (li.li_partial) {
            log_debug("skipping partial line at offset %d",
                      li.li_file_range.fr_offset);
            if (this->lf_specialized) {
                ll.set_level(LEVEL_INVALID);
                dst.emplace_back(ll);
            }
            return log_format::SCAN_INCOMPLETE;
        }

        const auto* line_data = (const unsigned char*) sbr.get_data();

        yajl_reset(handle);
        ypc.set_static_handler(json_log_handlers.jpc_children[0]);
        ypc.ypc_userdata = &jlu;
        ypc.ypc_ignore_unused = true;
        ypc.ypc_alt_callbacks.yajl_start_array = json_array_start;
        ypc.ypc_alt_callbacks.yajl_start_map = json_array_start;
        ypc.ypc_alt_callbacks.yajl_end_array = nullptr;
        ypc.ypc_alt_callbacks.yajl_end_map = nullptr;
        jlu.jlu_format = this;
        jlu.jlu_base_line = &ll;
        jlu.jlu_line_value = sbr.get_data();
        jlu.jlu_line_size = sbr.length();
        jlu.jlu_handle = handle;
        if (yajl_parse(handle, line_data, sbr.length()) == yajl_status_ok
            && yajl_complete_parse(handle) == yajl_status_ok)
        {
            if (ll.get_time() == 0) {
                if (this->lf_specialized) {
                    ll.set_ignore(true);
                    dst.emplace_back(ll);
                    return log_format::SCAN_MATCH;
                }

                log_debug("no match! %.*s", sbr.length(), line_data);
                return log_format::SCAN_NO_MATCH;
            }

            jlu.jlu_sub_line_count += this->jlf_line_format_init_count;
            for (int lpc = 0; lpc < jlu.jlu_sub_line_count; lpc++) {
                ll.set_sub_offset(lpc);
                if (lpc > 0) {
                    ll.set_level((log_level_t) (ll.get_level_and_flags()
                                                | LEVEL_CONTINUED));
                }
                dst.emplace_back(ll);
            }
        } else {
            unsigned char* msg;
            int line_count = 2;

            msg = yajl_get_error(
                handle, 1, (const unsigned char*) sbr.get_data(), sbr.length());
            if (msg != nullptr) {
                auto msg_frag = string_fragment::from_c_str(msg);
                log_debug("Unable to parse line at offset %d: %s",
                          li.li_file_range.fr_offset,
                          msg);
                line_count = msg_frag.count('\n') + 1;
                yajl_free_error(handle, msg);
            }
            if (!this->lf_specialized) {
                return log_format::SCAN_NO_MATCH;
            }
            for (int lpc = 0; lpc < line_count; lpc++) {
                log_level_t level = LEVEL_INVALID;

                ll.set_time(dst.back().get_timeval());
                if (lpc > 0) {
                    level = (log_level_t) (level | LEVEL_CONTINUED);
                }
                ll.set_level(level);
                ll.set_sub_offset(lpc);
                dst.emplace_back(ll);
            }
        }

        return log_format::SCAN_MATCH;
    }

    int curr_fmt = -1, orig_lock = this->last_pattern_index();
    int pat_index = orig_lock;
    auto line_sf = sbr.to_string_fragment();

    while (::next_format(this->elf_pattern_order, curr_fmt, pat_index)) {
        static thread_local auto md = lnav::pcre2pp::match_data::unitialized();

        auto* fpat = this->elf_pattern_order[curr_fmt].get();
        auto* pat = fpat->p_pcre.pp_value.get();

        if (fpat->p_module_format) {
            continue;
        }

        auto match_res = pat->capture_from(line_sf)
                             .into(md)
                             .matches(PCRE2_NO_UTF_CHECK)
                             .ignore_error();
        if (!match_res) {
            if (!this->lf_pattern_locks.empty() && pat_index != -1) {
                curr_fmt = -1;
                pat_index = -1;
            }
            continue;
        }

        auto ts = md[fpat->p_timestamp_field_index];
        auto time_cap = md[fpat->p_time_field_index];
        auto level_cap = md[fpat->p_level_field_index];
        auto mod_cap = md[fpat->p_module_field_index];
        auto opid_cap = md[fpat->p_opid_field_index];
        auto body_cap = md[fpat->p_body_field_index];
        const char* last;
        struct exttm log_time_tm;
        struct timeval log_tv;
        uint8_t mod_index = 0, opid = 0;
        char combined_datetime_buf[512];

        if (ts && time_cap) {
            auto ts_str_len = snprintf(combined_datetime_buf,
                                       sizeof(combined_datetime_buf),
                                       "%.*sT%.*s",
                                       ts->length(),
                                       ts->data(),
                                       time_cap->length(),
                                       time_cap->data());
            ts = string_fragment::from_bytes(combined_datetime_buf, ts_str_len);
        }

        if ((last = this->lf_date_time.scan(ts->data(),
                                            ts->length(),
                                            this->get_timestamp_formats(),
                                            &log_time_tm,
                                            log_tv))
            == nullptr)
        {
            this->lf_date_time.unlock();
            if ((last = this->lf_date_time.scan(ts->data(),
                                                ts->length(),
                                                this->get_timestamp_formats(),
                                                &log_time_tm,
                                                log_tv))
                == nullptr)
            {
                continue;
            }
        }

        auto level = this->convert_level(
            level_cap.value_or(string_fragment::invalid()), &sbc);

        this->lf_timestamp_flags = log_time_tm.et_flags;

        if (!((log_time_tm.et_flags & ETF_DAY_SET)
              && (log_time_tm.et_flags & ETF_MONTH_SET)
              && (log_time_tm.et_flags & ETF_YEAR_SET)))
        {
            this->check_for_new_year(dst, log_time_tm, log_tv);
        }

        if (opid_cap && !opid_cap->empty()) {
            {
                auto opid_iter = sbc.sbc_opids.find(opid_cap.value());

                if (opid_iter == sbc.sbc_opids.end()) {
                    auto opid_copy = opid_cap->to_owned(sbc.sbc_allocator);
                    auto otr = opid_time_range{log_tv, log_tv};
                    sbc.sbc_opids.emplace(opid_copy, otr);
                } else {
                    opid_iter->second.otr_end = log_tv;
                }
            }
            opid = hash_str(opid_cap->data(), opid_cap->length());
        }

        if (mod_cap) {
            intern_string_t mod_name = intern_string::lookup(mod_cap.value());
            auto mod_iter = MODULE_FORMATS.find(mod_name);

            if (mod_iter == MODULE_FORMATS.end()) {
                mod_index = this->module_scan(body_cap.value(), mod_name);
                mod_iter = MODULE_FORMATS.find(mod_name);
            } else if (mod_iter->second.mf_mod_format) {
                mod_index = mod_iter->second.mf_mod_format->lf_mod_index;
            }

            if (mod_index && level_cap && body_cap) {
                auto mod_elf = std::dynamic_pointer_cast<external_log_format>(
                    mod_iter->second.mf_mod_format);

                if (mod_elf) {
                    static thread_local auto mod_md
                        = lnav::pcre2pp::match_data::unitialized();

                    shared_buffer_ref body_ref;

                    body_cap->trim();

                    int mod_pat_index = mod_elf->last_pattern_index();
                    auto& mod_pat = *mod_elf->elf_pattern_order[mod_pat_index];
                    auto match_res = mod_pat.p_pcre.pp_value
                                         ->capture_from(body_cap.value())
                                         .into(mod_md)
                                         .matches(PCRE2_NO_UTF_CHECK)
                                         .ignore_error();
                    if (match_res) {
                        auto mod_level_cap
                            = mod_md[mod_pat.p_level_field_index];

                        level = mod_elf->convert_level(
                            mod_level_cap.value_or(string_fragment::invalid()),
                            &sbc);
                    }
                }
            }
        }

        for (auto value_index : fpat->p_numeric_value_indexes) {
            const indexed_value_def& ivd = fpat->p_value_by_index[value_index];
            const value_def& vd = *ivd.ivd_value_def;
            auto num_cap = md[ivd.ivd_index];

            if (num_cap && num_cap->is_valid()) {
                const struct scaling_factor* scaling = nullptr;

                if (ivd.ivd_unit_field_index >= 0) {
                    auto unit_cap = md[ivd.ivd_unit_field_index];

                    if (unit_cap && unit_cap->is_valid()) {
                        intern_string_t unit_val
                            = intern_string::lookup(unit_cap.value());

                        auto unit_iter = vd.vd_unit_scaling.find(unit_val);
                        if (unit_iter != vd.vd_unit_scaling.end()) {
                            const auto& sf = unit_iter->second;

                            scaling = &sf;
                        }
                    }
                }

                auto scan_res
                    = scn::scan_value<double>(num_cap->to_string_view());
                if (scan_res) {
                    auto dvalue = scan_res.value();
                    if (scaling != nullptr) {
                        scaling->scale(dvalue);
                    }
                    this->lf_value_stats[vd.vd_values_index].add_value(dvalue);
                }
            }
        }

        dst.emplace_back(
            li.li_file_range.fr_offset, log_tv, level, mod_index, opid);

        if (orig_lock != curr_fmt) {
            uint32_t lock_line;

            log_debug("%zu: changing pattern lock %d -> %d",
                      dst.size() - 1,
                      orig_lock,
                      curr_fmt);
            if (this->lf_pattern_locks.empty()) {
                lock_line = 0;
            } else {
                lock_line = dst.size() - 1;
            }
            this->lf_pattern_locks.emplace_back(lock_line, curr_fmt);
        }
        return log_format::SCAN_MATCH;
    }

    if (this->lf_specialized && !this->lf_multiline) {
        auto& last_line = dst.back();

        log_debug("invalid line %d %d", dst.size(), li.li_file_range.fr_offset);
        dst.emplace_back(li.li_file_range.fr_offset,
                         last_line.get_timeval(),
                         log_level_t::LEVEL_INVALID);

        return log_format::SCAN_MATCH;
    }

    return log_format::SCAN_NO_MATCH;
}

uint8_t
external_log_format::module_scan(string_fragment body_cap,
                                 const intern_string_t& mod_name)
{
    uint8_t mod_index;
    body_cap.trim();
    auto& ext_fmts = GRAPH_ORDERED_FORMATS;
    module_format mf;

    for (auto& elf : ext_fmts) {
        int curr_fmt = -1, fmt_lock = -1;

        while (::next_format(elf->elf_pattern_order, curr_fmt, fmt_lock)) {
            static thread_local auto md
                = lnav::pcre2pp::match_data::unitialized();

            auto& fpat = elf->elf_pattern_order[curr_fmt];
            auto& pat = fpat->p_pcre;

            if (!fpat->p_module_format) {
                continue;
            }

            auto match_res = pat.pp_value->capture_from(body_cap)
                                 .into(md)
                                 .matches(PCRE2_NO_UTF_CHECK)
                                 .ignore_error();
            if (!match_res) {
                continue;
            }

            log_debug("%s:module format found -- %s (%d)",
                      mod_name.get(),
                      elf->get_name().get(),
                      elf->lf_mod_index);

            mod_index = elf->lf_mod_index;
            mf.mf_mod_format = elf->specialized(curr_fmt);
            MODULE_FORMATS[mod_name] = mf;

            return mod_index;
        }
    }

    MODULE_FORMATS[mod_name] = mf;

    return 0;
}

void
external_log_format::annotate(uint64_t line_number,
                              string_attrs_t& sa,
                              logline_value_vector& values,
                              bool annotate_module) const
{
    static thread_local auto md = lnav::pcre2pp::match_data::unitialized();

    auto& line = values.lvv_sbr;
    struct line_range lr;

    line.erase_ansi();
    if (this->elf_type != elf_type_t::ELF_TYPE_TEXT) {
        values = this->jlf_line_values;
        sa = this->jlf_line_attrs;
        return;
    }

    if (line.empty()) {
        return;
    }

    values.lvv_values.reserve(this->elf_value_defs.size());

    int pat_index = this->pattern_index_for_line(line_number);
    auto& pat = *this->elf_pattern_order[pat_index];

    sa.reserve(pat.p_pcre.pp_value->get_capture_count());
    auto match_res
        = pat.p_pcre.pp_value->capture_from(line.to_string_fragment())
              .into(md)
              .matches(PCRE2_NO_UTF_CHECK)
              .ignore_error();
    if (!match_res) {
        // A continued line still needs a body.
        lr.lr_start = 0;
        lr.lr_end = line.length();
        sa.emplace_back(lr, SA_BODY.value());
        if (!this->lf_multiline) {
            auto len
                = pat.p_pcre.pp_value->match_partial(line.to_string_fragment());
            sa.emplace_back(
                line_range{(int) len, -1},
                SA_INVALID.value("Log line does not match any pattern"));
        }
        return;
    }

    nonstd::optional<string_fragment> module_cap;
    if (!pat.p_module_format) {
        auto ts_cap = md[pat.p_timestamp_field_index];
        if (ts_cap) {
            sa.emplace_back(to_line_range(ts_cap.value()),
                            logline::L_TIMESTAMP.value());
        }

        if (pat.p_module_field_index != -1) {
            module_cap = md[pat.p_module_field_index];
            if (module_cap) {
                sa.emplace_back(to_line_range(module_cap.value()),
                                logline::L_MODULE.value());
            }
        }

        auto opid_cap = md[pat.p_opid_field_index];
        if (opid_cap) {
            sa.emplace_back(to_line_range(opid_cap.value()),
                            logline::L_OPID.value());
        }
    }

    auto body_cap = md[pat.p_body_field_index];

    for (size_t lpc = 0; lpc < pat.p_value_by_index.size(); lpc++) {
        const indexed_value_def& ivd = pat.p_value_by_index[lpc];
        const struct scaling_factor* scaling = nullptr;
        auto cap = md[ivd.ivd_index];
        const auto& vd = *ivd.ivd_value_def;

        if (ivd.ivd_unit_field_index >= 0) {
            auto unit_cap = md[ivd.ivd_unit_field_index];

            if (unit_cap) {
                intern_string_t unit_val
                    = intern_string::lookup(unit_cap.value());
                auto unit_iter = vd.vd_unit_scaling.find(unit_val);
                if (unit_iter != vd.vd_unit_scaling.end()) {
                    const struct scaling_factor& sf = unit_iter->second;

                    scaling = &sf;
                }
            }
        }

        if (cap) {
            values.lvv_values.emplace_back(
                vd.vd_meta, line, to_line_range(cap.value()));
            values.lvv_values.back().apply_scaling(scaling);
        } else {
            values.lvv_values.emplace_back(vd.vd_meta);
        }
        if (pat.p_module_format) {
            values.lvv_values.back().lv_meta.lvm_from_module = true;
        }
    }

    bool did_mod_annotate_body = false;
    if (annotate_module && module_cap && body_cap && body_cap->is_valid()) {
        intern_string_t mod_name = intern_string::lookup(module_cap.value());
        auto mod_iter = MODULE_FORMATS.find(mod_name);

        if (mod_iter != MODULE_FORMATS.end()
            && mod_iter->second.mf_mod_format != nullptr)
        {
            auto& mf = mod_iter->second;

            body_cap->trim();
            auto narrow_res
                = line.narrow(body_cap->sf_begin, body_cap->length());
            auto pre_mod_values_size = values.lvv_values.size();
            auto pre_mod_sa_size = sa.size();
            mf.mf_mod_format->annotate(line_number, sa, values, false);
            for (size_t lpc = pre_mod_values_size;
                 lpc < values.lvv_values.size();
                 lpc++)
            {
                values.lvv_values[lpc].lv_origin.shift(0, body_cap->sf_begin);
            }
            for (size_t lpc = pre_mod_sa_size; lpc < sa.size(); lpc++) {
                sa[lpc].sa_range.shift(0, body_cap->sf_begin);
            }
            line.widen(narrow_res);
            did_mod_annotate_body = true;
        }
    }
    if (!did_mod_annotate_body) {
        if (body_cap && body_cap->is_valid()) {
            lr = to_line_range(body_cap.value());
        } else {
            lr.lr_start = line.length();
            lr.lr_end = line.length();
        }
        sa.emplace_back(lr, SA_BODY.value());
    }
}

void
external_log_format::rewrite(exec_context& ec,
                             shared_buffer_ref& line,
                             string_attrs_t& sa,
                             std::string& value_out)
{
    std::vector<logline_value>::iterator shift_iter;
    auto& values = *ec.ec_line_values;

    value_out.assign(line.get_data(), line.length());

    for (auto iter = values.lvv_values.begin(); iter != values.lvv_values.end();
         ++iter)
    {
        if (!iter->lv_origin.is_valid()) {
            log_debug("not rewriting value with invalid origin -- %s",
                      iter->lv_meta.lvm_name.get());
            continue;
        }

        auto vd_iter = this->elf_value_defs.find(iter->lv_meta.lvm_name);
        if (vd_iter == this->elf_value_defs.end()) {
            log_debug("not rewriting undefined value -- %s",
                      iter->lv_meta.lvm_name.get());
            continue;
        }

        const auto& vd = *vd_iter->second;

        if (vd.vd_rewriter.empty()) {
            continue;
        }

        auto _sg = ec.enter_source(
            vd_iter->second->vd_rewrite_src_name, 1, vd.vd_rewriter);
        std::string field_value;

        auto exec_res = execute_any(ec, vd.vd_rewriter);
        if (exec_res.isOk()) {
            field_value = exec_res.unwrap();
        } else {
            field_value = exec_res.unwrapErr().to_attr_line().get_string();
        }
        auto adj_origin
            = iter->origin_in_full_msg(value_out.c_str(), value_out.length());

        value_out.erase(adj_origin.lr_start, adj_origin.length());

        int32_t shift_amount
            = ((int32_t) field_value.length()) - adj_origin.length();
        value_out.insert(adj_origin.lr_start, field_value);
        for (shift_iter = values.lvv_values.begin();
             shift_iter != values.lvv_values.end();
             ++shift_iter)
        {
            shift_iter->lv_origin.shift(adj_origin.lr_start, shift_amount);
        }
        shift_string_attrs(sa, adj_origin.lr_start, shift_amount);
    }
}

static int
read_json_field(yajlpp_parse_context* ypc, const unsigned char* str, size_t len)
{
    json_log_userdata* jlu = (json_log_userdata*) ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path();
    struct exttm tm_out;
    struct timeval tv_out;

    if (jlu->jlu_format->lf_timestamp_field == field_name) {
        jlu->jlu_format->lf_date_time.scan(
            (const char*) str,
            len,
            jlu->jlu_format->get_timestamp_formats(),
            &tm_out,
            tv_out);
        // Leave off the machine oriented flag since we convert it anyhow
        jlu->jlu_format->lf_timestamp_flags
            = tm_out.et_flags & ~ETF_MACHINE_ORIENTED;
        jlu->jlu_base_line->set_time(tv_out);
    } else if (jlu->jlu_format->elf_level_pointer.pp_value != nullptr) {
        if (jlu->jlu_format->elf_level_pointer.pp_value
                ->find_in(field_name.to_string_fragment(), PCRE2_NO_UTF_CHECK)
                .ignore_error()
                .has_value())
        {
            jlu->jlu_base_line->set_level(jlu->jlu_format->convert_level(
                string_fragment::from_bytes(str, len), jlu->jlu_batch_context));
        }
    }
    if (jlu->jlu_format->elf_level_field == field_name) {
        jlu->jlu_base_line->set_level(jlu->jlu_format->convert_level(
            string_fragment::from_bytes(str, len), jlu->jlu_batch_context));
    }
    if (jlu->jlu_format->elf_opid_field == field_name) {
        uint8_t opid = hash_str((const char*) str, len);
        jlu->jlu_base_line->set_opid(opid);
    }

    jlu->jlu_sub_line_count += jlu->jlu_format->value_line_count(
        field_name, ypc->is_level(1), str, len);

    return 1;
}

static int
rewrite_json_field(yajlpp_parse_context* ypc,
                   const unsigned char* str,
                   size_t len)
{
    static const intern_string_t body_name = intern_string::lookup("body", -1);
    json_log_userdata* jlu = (json_log_userdata*) ypc->ypc_userdata;
    const intern_string_t field_name = ypc->get_path();

    if (jlu->jlu_format->lf_timestamp_field == field_name) {
        char time_buf[64];

        // TODO add a timeval kind to logline_value
        if (jlu->jlu_line->is_time_skewed()) {
            struct timeval tv;
            struct exttm tm;

            jlu->jlu_format->lf_date_time.scan(
                (const char*) str,
                len,
                jlu->jlu_format->get_timestamp_formats(),
                &tm,
                tv);
            sql_strftime(time_buf, sizeof(time_buf), tv, 'T');
        } else {
            sql_strftime(
                time_buf, sizeof(time_buf), jlu->jlu_line->get_timeval(), 'T');
        }
        jlu->jlu_format->jlf_line_values.lvv_values.emplace_back(
            jlu->jlu_format->get_value_meta(field_name,
                                            value_kind_t::VALUE_TEXT),
            std::string{time_buf});
    } else if (jlu->jlu_shared_buffer.contains((const char*) str)) {
        auto str_offset = (int) ((const char*) str - jlu->jlu_line_value);
        if (field_name == jlu->jlu_format->elf_body_field) {
            jlu->jlu_format->jlf_line_values.lvv_values.emplace_back(
                jlu->jlu_format->get_value_meta(body_name,
                                                value_kind_t::VALUE_TEXT),
                string_fragment::from_byte_range(
                    jlu->jlu_shared_buffer.get_data(),
                    str_offset,
                    str_offset + len));
        }
        if (!ypc->is_level(1) && !jlu->jlu_format->has_value_def(field_name)) {
            return 1;
        }

        jlu->jlu_format->jlf_line_values.lvv_values.emplace_back(
            jlu->jlu_format->get_value_meta(field_name,
                                            value_kind_t::VALUE_TEXT),
            string_fragment::from_byte_range(jlu->jlu_shared_buffer.get_data(),
                                             str_offset,
                                             str_offset + len));
    } else {
        if (field_name == jlu->jlu_format->elf_body_field) {
            jlu->jlu_format->jlf_line_values.lvv_values.emplace_back(
                jlu->jlu_format->get_value_meta(body_name,
                                                value_kind_t::VALUE_TEXT),
                std::string{(const char*) str, len});
        }
        if (!ypc->is_level(1) && !jlu->jlu_format->has_value_def(field_name)) {
            return 1;
        }

        jlu->jlu_format->jlf_line_values.lvv_values.emplace_back(
            jlu->jlu_format->get_value_meta(field_name,
                                            value_kind_t::VALUE_TEXT),
            std::string{(const char*) str, len});
    }

    return 1;
}

void
external_log_format::get_subline(const logline& ll,
                                 shared_buffer_ref& sbr,
                                 bool full_message)
{
    if (this->elf_type == elf_type_t::ELF_TYPE_TEXT) {
        return;
    }

    if (this->jlf_cached_offset != ll.get_offset()
        || this->jlf_cached_full != full_message)
    {
        auto& ypc = *(this->jlf_parse_context);
        yajl_handle handle = this->jlf_yajl_handle.get();
        json_log_userdata jlu(sbr, nullptr);

        this->jlf_share_manager.invalidate_refs();
        this->jlf_cached_line.clear();
        this->jlf_line_values.clear();
        this->jlf_line_offsets.clear();
        this->jlf_line_attrs.clear();

        auto line_frag = sbr.to_string_fragment();

        if (!line_frag.startswith("{")) {
            this->jlf_cached_line.resize(line_frag.length());
            memcpy(this->jlf_cached_line.data(),
                   line_frag.data(),
                   line_frag.length());
            this->jlf_line_values.clear();
            sbr.share(this->jlf_share_manager,
                      &this->jlf_cached_line[0],
                      this->jlf_cached_line.size());
            this->jlf_line_values.lvv_sbr = sbr;
            this->jlf_line_attrs.emplace_back(
                line_range{0, -1},
                SA_INVALID.value(fmt::format(
                    FMT_STRING("line at offset {} is not a JSON-line"),
                    ll.get_offset())));
            return;
        }

        yajl_reset(handle);
        ypc.set_static_handler(json_log_rewrite_handlers.jpc_children[0]);
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

        yajl_status parse_status = yajl_parse(
            handle, (const unsigned char*) sbr.get_data(), sbr.length());
        if (parse_status != yajl_status_ok
            || yajl_complete_parse(handle) != yajl_status_ok)
        {
            unsigned char* msg;
            std::string full_msg;

            msg = yajl_get_error(
                handle, 1, (const unsigned char*) sbr.get_data(), sbr.length());
            if (msg != nullptr) {
                full_msg = fmt::format(
                    FMT_STRING("[offset: {}] {}\n{}"),
                    ll.get_offset(),
                    fmt::string_view{sbr.get_data(), sbr.length()},
                    reinterpret_cast<char*>(msg));
                yajl_free_error(handle, msg);
            }

            this->jlf_cached_line.resize(full_msg.size());
            memcpy(
                this->jlf_cached_line.data(), full_msg.data(), full_msg.size());
            this->jlf_line_values.clear();
            this->jlf_line_attrs.emplace_back(
                line_range{0, -1},
                SA_INVALID.value("JSON line failed to parse"));
        } else {
            std::vector<logline_value>::iterator lv_iter;
            bool used_values[this->jlf_line_values.lvv_values.size()];
            struct line_range lr;

            memset(used_values, 0, sizeof(used_values));

            for (lv_iter = this->jlf_line_values.lvv_values.begin();
                 lv_iter != this->jlf_line_values.lvv_values.end();
                 ++lv_iter)
            {
                lv_iter->lv_meta.lvm_format = this;
            }

            int sub_offset = 1 + this->jlf_line_format_init_count;
            for (const auto& jfe : this->jlf_line_format) {
                static const intern_string_t ts_field
                    = intern_string::lookup("__timestamp__", -1);
                static const intern_string_t level_field
                    = intern_string::lookup("__level__");
                size_t begin_size = this->jlf_cached_line.size();

                switch (jfe.jfe_type) {
                    case json_log_field::CONSTANT:
                        this->json_append_to_cache(
                            jfe.jfe_default_value.c_str(),
                            jfe.jfe_default_value.size());
                        break;
                    case json_log_field::VARIABLE:
                        lv_iter = find_if(
                            this->jlf_line_values.lvv_values.begin(),
                            this->jlf_line_values.lvv_values.end(),
                            logline_value_cmp(&jfe.jfe_value.pp_value));
                        if (lv_iter != this->jlf_line_values.lvv_values.end()) {
                            auto str = lv_iter->to_string();
                            size_t nl_pos = str.find('\n');

                            lr.lr_start = this->jlf_cached_line.size();

                            lv_iter->lv_meta.lvm_hidden
                                = lv_iter->lv_meta.lvm_user_hidden;
                            if ((int) str.size() > jfe.jfe_max_width) {
                                switch (jfe.jfe_overflow) {
                                    case json_format_element::overflow_t::
                                        ABBREV: {
                                        this->json_append_to_cache(str.c_str(),
                                                                   str.size());
                                        size_t new_size = abbreviate_str(
                                            &this->jlf_cached_line[lr.lr_start],
                                            str.size(),
                                            jfe.jfe_max_width);

                                        this->jlf_cached_line.resize(
                                            lr.lr_start + new_size);
                                        break;
                                    }
                                    case json_format_element::overflow_t::
                                        TRUNCATE: {
                                        this->json_append_to_cache(
                                            str.c_str(), jfe.jfe_max_width);
                                        break;
                                    }
                                    case json_format_element::overflow_t::
                                        DOTDOT: {
                                        size_t middle
                                            = (jfe.jfe_max_width / 2) - 1;
                                        this->json_append_to_cache(str.c_str(),
                                                                   middle);
                                        this->json_append_to_cache("..", 2);
                                        size_t rest
                                            = (jfe.jfe_max_width - middle - 2);
                                        this->json_append_to_cache(
                                            str.c_str() + str.size() - rest,
                                            rest);
                                        break;
                                    }
                                }
                            } else {
                                sub_offset
                                    += count(str.begin(), str.end(), '\n');
                                this->json_append(jfe, str.c_str(), str.size());
                            }

                            if (nl_pos == std::string::npos || full_message) {
                                lr.lr_end = this->jlf_cached_line.size();
                            } else {
                                lr.lr_end = lr.lr_start + nl_pos;
                            }

                            if (lv_iter->lv_meta.lvm_name
                                == this->lf_timestamp_field)
                            {
                                this->jlf_line_attrs.emplace_back(
                                    lr, logline::L_TIMESTAMP.value());
                            } else if (lv_iter->lv_meta.lvm_name
                                       == this->elf_body_field)
                            {
                                this->jlf_line_attrs.emplace_back(
                                    lr, SA_BODY.value());
                            } else if (lv_iter->lv_meta.lvm_name
                                       == this->elf_opid_field)
                            {
                                this->jlf_line_attrs.emplace_back(
                                    lr, logline::L_OPID.value());
                            }
                            lv_iter->lv_origin = lr;
                            used_values[distance(
                                this->jlf_line_values.lvv_values.begin(),
                                lv_iter)]
                                = true;
                        } else if (jfe.jfe_value.pp_value == ts_field) {
                            struct line_range lr;
                            ssize_t ts_len;
                            char ts[64];

                            if (jfe.jfe_ts_format.empty()) {
                                ts_len = sql_strftime(
                                    ts, sizeof(ts), ll.get_timeval(), 'T');
                            } else {
                                struct exttm et;

                                ll.to_exttm(et);
                                ts_len = ftime_fmt(ts,
                                                   sizeof(ts),
                                                   jfe.jfe_ts_format.c_str(),
                                                   et);
                            }
                            lr.lr_start = this->jlf_cached_line.size();
                            this->json_append_to_cache(ts, ts_len);
                            lr.lr_end = this->jlf_cached_line.size();
                            this->jlf_line_attrs.emplace_back(
                                lr, logline::L_TIMESTAMP.value());

                            lv_iter = find_if(
                                this->jlf_line_values.lvv_values.begin(),
                                this->jlf_line_values.lvv_values.end(),
                                logline_value_cmp(&this->lf_timestamp_field));
                            if (lv_iter
                                != this->jlf_line_values.lvv_values.end())
                            {
                                used_values[distance(
                                    this->jlf_line_values.lvv_values.begin(),
                                    lv_iter)]
                                    = true;
                            }
                        } else if (jfe.jfe_value.pp_value == level_field
                                   || jfe.jfe_value.pp_value
                                       == this->elf_level_field)
                        {
                            this->json_append(jfe, ll.get_level_name(), -1);
                        } else {
                            this->json_append(jfe,
                                              jfe.jfe_default_value.c_str(),
                                              jfe.jfe_default_value.size());
                        }

                        switch (jfe.jfe_text_transform) {
                            case external_log_format::json_format_element::
                                transform_t::NONE:
                                break;
                            case external_log_format::json_format_element::
                                transform_t::UPPERCASE:
                                for (size_t cindex = begin_size;
                                     cindex < this->jlf_cached_line.size();
                                     cindex++)
                                {
                                    this->jlf_cached_line[cindex] = toupper(
                                        this->jlf_cached_line[cindex]);
                                }
                                break;
                            case external_log_format::json_format_element::
                                transform_t::LOWERCASE:
                                for (size_t cindex = begin_size;
                                     cindex < this->jlf_cached_line.size();
                                     cindex++)
                                {
                                    this->jlf_cached_line[cindex] = tolower(
                                        this->jlf_cached_line[cindex]);
                                }
                                break;
                            case external_log_format::json_format_element::
                                transform_t::CAPITALIZE:
                                for (size_t cindex = begin_size;
                                     cindex < begin_size + 1;
                                     cindex++)
                                {
                                    this->jlf_cached_line[cindex] = toupper(
                                        this->jlf_cached_line[cindex]);
                                }
                                for (size_t cindex = begin_size + 1;
                                     cindex < this->jlf_cached_line.size();
                                     cindex++)
                                {
                                    this->jlf_cached_line[cindex] = tolower(
                                        this->jlf_cached_line[cindex]);
                                }
                                break;
                        }
                        break;
                }
            }
            this->json_append_to_cache("\n", 1);

            for (size_t lpc = 0; lpc < this->jlf_line_values.lvv_values.size();
                 lpc++)
            {
                static const intern_string_t body_name
                    = intern_string::lookup("body", -1);
                auto& lv = this->jlf_line_values.lvv_values[lpc];

                if (lv.lv_meta.lvm_hidden || used_values[lpc]
                    || body_name == lv.lv_meta.lvm_name)
                {
                    continue;
                }

                const std::string str = lv.to_string();
                size_t curr_pos = 0, nl_pos, line_len = -1;

                lv.lv_sub_offset = sub_offset;
                lv.lv_origin.lr_start = 2 + lv.lv_meta.lvm_name.size() + 2;
                do {
                    nl_pos = str.find('\n', curr_pos);
                    if (nl_pos != std::string::npos) {
                        line_len = nl_pos - curr_pos;
                    } else {
                        line_len = str.size() - curr_pos;
                    }
                    this->json_append_to_cache("  ", 2);
                    this->json_append_to_cache(lv.lv_meta.lvm_name.get(),
                                               lv.lv_meta.lvm_name.size());
                    this->json_append_to_cache(": ", 2);
                    this->json_append_to_cache(&str.c_str()[curr_pos],
                                               line_len);
                    this->json_append_to_cache("\n", 1);
                    curr_pos = nl_pos + 1;
                    sub_offset += 1;
                } while (nl_pos != std::string::npos && nl_pos < str.size());
            }
        }

        this->jlf_line_offsets.push_back(0);
        for (size_t lpc = 0; lpc < this->jlf_cached_line.size(); lpc++) {
            if (this->jlf_cached_line[lpc] == '\n') {
                this->jlf_line_offsets.push_back(lpc + 1);
            }
        }
        this->jlf_line_offsets.push_back(this->jlf_cached_line.size());
        this->jlf_cached_offset = ll.get_offset();
        this->jlf_cached_full = full_message;
    }

    off_t this_off = 0, next_off = 0;

    if (!this->jlf_line_offsets.empty()
        && ll.get_sub_offset() < this->jlf_line_offsets.size())
    {
        require(ll.get_sub_offset() < this->jlf_line_offsets.size());

        this_off = this->jlf_line_offsets[ll.get_sub_offset()];
        if ((ll.get_sub_offset() + 1) < (int) this->jlf_line_offsets.size()) {
            next_off = this->jlf_line_offsets[ll.get_sub_offset() + 1];
        } else {
            next_off = this->jlf_cached_line.size();
        }
        if (next_off > 0 && this->jlf_cached_line[next_off - 1] == '\n'
            && this_off != next_off)
        {
            next_off -= 1;
        }
    }

    if (full_message) {
        sbr.share(this->jlf_share_manager,
                  &this->jlf_cached_line[0],
                  this->jlf_cached_line.size());
    } else {
        sbr.share(this->jlf_share_manager,
                  this->jlf_cached_line.data() + this_off,
                  next_off - this_off);
    }
    this->jlf_line_values.lvv_sbr = sbr;
}

void
external_log_format::build(std::vector<lnav::console::user_message>& errors)
{
    if (!this->lf_timestamp_field.empty()) {
        auto& vd = this->elf_value_defs[this->lf_timestamp_field];
        if (vd.get() == nullptr) {
            vd = std::make_shared<external_log_format::value_def>(
                this->lf_timestamp_field, value_kind_t::VALUE_TEXT, -1, this);
        }
        vd->vd_meta.lvm_name = this->lf_timestamp_field;
        vd->vd_meta.lvm_kind = value_kind_t::VALUE_TEXT;
        vd->vd_internal = true;
    }
    if (startswith(this->elf_level_field.get(), "/")) {
        this->elf_level_field
            = intern_string::lookup(this->elf_level_field.get() + 1);
    }
    if (!this->elf_level_field.empty()
        && this->elf_value_defs.find(this->elf_level_field)
            == this->elf_value_defs.end())
    {
        auto& vd = this->elf_value_defs[this->elf_level_field];
        if (vd.get() == nullptr) {
            vd = std::make_shared<external_log_format::value_def>(
                this->elf_level_field, value_kind_t::VALUE_TEXT, -1, this);
        }
        vd->vd_meta.lvm_name = this->elf_level_field;
        vd->vd_meta.lvm_kind = value_kind_t::VALUE_TEXT;
        vd->vd_internal = true;
    }
    if (!this->elf_body_field.empty()) {
        auto& vd = this->elf_value_defs[this->elf_body_field];
        if (vd.get() == nullptr) {
            vd = std::make_shared<external_log_format::value_def>(
                this->elf_body_field, value_kind_t::VALUE_TEXT, -1, this);
        }
        vd->vd_meta.lvm_name = this->elf_body_field;
        vd->vd_meta.lvm_kind = value_kind_t::VALUE_TEXT;
        vd->vd_internal = true;
    }

    if (!this->lf_timestamp_format.empty()) {
        this->lf_timestamp_format.push_back(nullptr);
    }
    for (auto iter = this->elf_patterns.begin();
         iter != this->elf_patterns.end();
         ++iter)
    {
        pattern& pat = *iter->second;

        if (pat.p_pcre.pp_value == nullptr) {
            continue;
        }

        if (pat.p_module_format) {
            this->elf_has_module_format = true;
        }

        for (auto named_cap : pat.p_pcre.pp_value->get_named_captures()) {
            const intern_string_t name
                = intern_string::lookup(named_cap.get_name());

            if (name == this->lf_timestamp_field) {
                pat.p_timestamp_field_index = named_cap.get_index();
            }
            if (name == this->lf_time_field) {
                pat.p_time_field_index = named_cap.get_index();
            }
            if (name == this->elf_level_field) {
                pat.p_level_field_index = named_cap.get_index();
            }
            if (name == this->elf_module_id_field) {
                pat.p_module_field_index = named_cap.get_index();
            }
            if (name == this->elf_opid_field) {
                pat.p_opid_field_index = named_cap.get_index();
            }
            if (name == this->elf_body_field) {
                pat.p_body_field_index = named_cap.get_index();
            }

            auto value_iter = this->elf_value_defs.find(name);
            if (value_iter != this->elf_value_defs.end()) {
                auto vd = value_iter->second;
                indexed_value_def ivd;

                ivd.ivd_index = named_cap.get_index();
                if (!vd->vd_unit_field.empty()) {
                    ivd.ivd_unit_field_index = pat.p_pcre.pp_value->name_index(
                        vd->vd_unit_field.get());
                } else {
                    ivd.ivd_unit_field_index = -1;
                }
                if (!vd->vd_internal && vd->vd_meta.lvm_column == -1) {
                    vd->vd_meta.lvm_column = this->elf_column_count++;
                }
                ivd.ivd_value_def = vd;
                pat.p_value_by_index.push_back(ivd);
            }
        }

        stable_sort(pat.p_value_by_index.begin(), pat.p_value_by_index.end());

        for (int lpc = 0; lpc < (int) pat.p_value_by_index.size(); lpc++) {
            auto& ivd = pat.p_value_by_index[lpc];
            auto vd = ivd.ivd_value_def;

            if (!vd->vd_foreign_key && !vd->vd_meta.lvm_identifier) {
                switch (vd->vd_meta.lvm_kind) {
                    case value_kind_t::VALUE_INTEGER:
                    case value_kind_t::VALUE_FLOAT:
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
        if (!this->elf_module_id_field.empty()
            && pat.p_module_field_index == -1)
        {
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

    if (this->elf_type != elf_type_t::ELF_TYPE_TEXT) {
        if (!this->elf_patterns.empty()) {
            errors.emplace_back(
                lnav::console::user_message::error(
                    attr_line_t()
                        .append_quoted(
                            lnav::roles::symbol(this->elf_name.to_string()))
                        .append(" is not a valid log format"))
                    .with_reason("structured logs cannot have regexes")
                    .with_snippets(this->get_snippets()));
        }
        if (this->elf_type == elf_type_t::ELF_TYPE_JSON) {
            this->jlf_parse_context
                = std::make_shared<yajlpp_parse_context>(this->elf_name);
            this->jlf_yajl_handle.reset(
                yajl_alloc(&this->jlf_parse_context->ypc_callbacks,
                           nullptr,
                           this->jlf_parse_context.get()),
                yajl_handle_deleter());
            yajl_config(
                this->jlf_yajl_handle.get(), yajl_dont_validate_strings, 1);
        }

    } else {
        if (this->elf_patterns.empty()) {
            errors.emplace_back(lnav::console::user_message::error(
                                    attr_line_t()
                                        .append_quoted(lnav::roles::symbol(
                                            this->elf_name.to_string()))
                                        .append(" is not a valid log format"))
                                    .with_reason("no regexes specified")
                                    .with_snippets(this->get_snippets()));
        }
    }

    stable_sort(this->elf_level_pairs.begin(), this->elf_level_pairs.end());

    for (auto& vd : this->elf_value_def_order) {
        std::vector<std::string>::iterator act_iter;

        vd->vd_meta.lvm_format = this;
        if (!vd->vd_internal && vd->vd_meta.lvm_column == -1) {
            vd->vd_meta.lvm_column = this->elf_column_count++;
        }

        if (vd->vd_meta.lvm_kind == value_kind_t::VALUE_UNKNOWN) {
            vd->vd_meta.lvm_kind = value_kind_t::VALUE_TEXT;
        }

        if (this->elf_type == elf_type_t::ELF_TYPE_TEXT) {
            std::set<std::string> available_captures;

            bool found_in_pattern = false;
            for (const auto& pat : this->elf_patterns) {
                if (pat.second->p_pcre.pp_value == nullptr) {
                    continue;
                }

                auto cap_index = pat.second->p_pcre.pp_value->name_index(
                    vd->vd_meta.lvm_name.get());
                if (cap_index >= 0) {
                    found_in_pattern = true;
                    break;
                }

                for (auto named_cap :
                     pat.second->p_pcre.pp_value->get_named_captures())
                {
                    available_captures.insert(named_cap.get_name().to_string());
                }
            }
            if (!found_in_pattern) {
                auto notes
                    = attr_line_t("the following captures are available:\n  ")
                          .join(available_captures,
                                VC_ROLE.value(role_t::VCR_SYMBOL),
                                ", ");
                errors.emplace_back(
                    lnav::console::user_message::warning(
                        attr_line_t("invalid value ")
                            .append_quoted(lnav::roles::symbol(
                                fmt::format(FMT_STRING("/{}/value/{}"),
                                            this->elf_name,
                                            vd->vd_meta.lvm_name.get()))))
                        .with_reason(
                            attr_line_t("no patterns have a capture named ")
                                .append_quoted(vd->vd_meta.lvm_name.get()))
                        .with_note(notes)
                        .with_snippets(this->get_snippets())
                        .with_help("values are populated from captures in "
                                   "patterns, so at least one pattern must "
                                   "have a capture with this value name"));
            }
        }

        for (act_iter = vd->vd_action_list.begin();
             act_iter != vd->vd_action_list.end();
             ++act_iter)
        {
            if (this->lf_action_defs.find(*act_iter)
                == this->lf_action_defs.end())
            {
#if 0
                errors.push_back("error:" + this->elf_name.to_string() + ":"
                                 + vd->vd_meta.lvm_name.get()
                                 + ": cannot find action -- " + (*act_iter));
#endif
            }
        }

        vd->set_rewrite_src_name();
    }

    for (const auto& td_pair : this->lf_tag_defs) {
        const auto& td = td_pair.second;

        if (td->ftd_pattern.pp_value == nullptr
            || td->ftd_pattern.pp_value->get_pattern().empty())
        {
            errors.emplace_back(
                lnav::console::user_message::error(
                    attr_line_t("invalid tag definition ")
                        .append_quoted(lnav::roles::symbol(
                            fmt::format(FMT_STRING("/{}/tags/{}"),
                                        this->elf_name,
                                        td_pair.first))))
                    .with_reason(
                        "tag definitions must have a non-empty pattern")
                    .with_snippets(this->get_snippets()));
        }
    }

    if (this->elf_type == elf_type_t::ELF_TYPE_TEXT
        && this->elf_samples.empty())
    {
        errors.emplace_back(
            lnav::console::user_message::error(
                attr_line_t()
                    .append_quoted(
                        lnav::roles::symbol(this->elf_name.to_string()))
                    .append(" is not a valid log format"))
                .with_reason("log message samples must be included in a format "
                             "definition")
                .with_snippets(this->get_snippets()));
    }

    if (!this->lf_subsecond_field.empty()
        && !this->lf_subsecond_unit.has_value())
    {
        errors.emplace_back(
            lnav::console::user_message::error(
                attr_line_t()
                    .append_quoted(
                        lnav::roles::symbol(this->elf_name.to_string()))
                    .append(" is not a valid log format"))
                .with_reason(attr_line_t()
                                 .append_quoted("subsecond-unit"_symbol)
                                 .append(" must be set when ")
                                 .append_quoted("subsecond-field"_symbol)
                                 .append(" is used"))
                .with_snippets(this->get_snippets()));
    }

    for (size_t sample_index = 0; sample_index < this->elf_samples.size();
         sample_index += 1)
    {
        auto& elf_sample = this->elf_samples[sample_index];
        auto sample_lines
            = string_fragment(elf_sample.s_line.pp_value).split_lines();
        bool found = false;

        for (auto pat_iter = this->elf_pattern_order.begin();
             pat_iter != this->elf_pattern_order.end();
             ++pat_iter)
        {
            auto& pat = *(*pat_iter);

            if (!pat.p_pcre.pp_value) {
                continue;
            }

            auto md = pat.p_pcre.pp_value->create_match_data();
            auto match_res = pat.p_pcre.pp_value->capture_from(sample_lines[0])
                                 .into(md)
                                 .matches()
                                 .ignore_error();
            if (!match_res) {
                continue;
            }
            found = true;

            if (pat.p_module_format) {
                continue;
            }

            elf_sample.s_matched_regexes.insert(pat.p_name.to_string());
            pat.p_matched_samples.insert(sample_index);

            if (pat.p_pcre.pp_value->name_index(this->lf_timestamp_field.get())
                < 0)
            {
                attr_line_t notes;
                bool first_note = true;

                if (pat.p_pcre.pp_value->get_capture_count() > 0) {
                    notes.append("the following captures are available:\n  ");
                }
                for (auto named_cap : pat.p_pcre.pp_value->get_named_captures())
                {
                    if (!first_note) {
                        notes.append(", ");
                    }
                    notes.append(
                        lnav::roles::symbol(named_cap.get_name().to_string()));
                    first_note = false;
                }
                errors.emplace_back(
                    lnav::console::user_message::error(
                        attr_line_t("invalid value for property ")
                            .append_quoted(lnav::roles::symbol(
                                fmt::format(FMT_STRING("/{}/timestamp-field"),
                                            this->elf_name))))
                        .with_reason(
                            attr_line_t()
                                .append_quoted(this->lf_timestamp_field)
                                .append(" was not found in the pattern at ")
                                .append(lnav::roles::symbol(pat.p_config_path)))
                        .with_note(notes)
                        .with_snippets(this->get_snippets()));
                continue;
            }

            const auto ts_cap = md[pat.p_timestamp_field_index];
            const auto level_cap = md[pat.p_level_field_index];
            const char* const* custom_formats = this->get_timestamp_formats();
            date_time_scanner dts;
            struct timeval tv;
            struct exttm tm;

            if (ts_cap && ts_cap->sf_begin == 0) {
                pat.p_timestamp_end = ts_cap->sf_end;
            }
            if (ts_cap
                && dts.scan(ts_cap->data(),
                            ts_cap->length(),
                            custom_formats,
                            &tm,
                            tv)
                    == nullptr)
            {
                attr_line_t notes;

                if (custom_formats == nullptr) {
                    notes.append("the following built-in formats were tried:");
                    for (int lpc = 0; PTIMEC_FORMATS[lpc].pf_fmt != nullptr;
                         lpc++)
                    {
                        off_t off = 0;

                        PTIMEC_FORMATS[lpc].pf_func(
                            &tm, ts_cap->data(), off, ts_cap->length());
                        notes.append("\n  ")
                            .append(ts_cap.value())
                            .append("\n")
                            .append(2 + off, ' ')
                            .append("^ "_snippet_border)
                            .append_quoted(
                                lnav::roles::symbol(PTIMEC_FORMATS[lpc].pf_fmt))
                            .append(" matched up to here"_snippet_border);
                    }
                } else {
                    notes.append("the following custom formats were tried:");
                    for (int lpc = 0; custom_formats[lpc] != nullptr; lpc++) {
                        off_t off = 0;

                        ptime_fmt(custom_formats[lpc],
                                  &tm,
                                  ts_cap->data(),
                                  off,
                                  ts_cap->length());
                        notes.append("\n  ")
                            .append(ts_cap.value())
                            .append("\n")
                            .append(2 + off, ' ')
                            .append("^ "_snippet_border)
                            .append_quoted(
                                lnav::roles::symbol(custom_formats[lpc]))
                            .append(" matched up to here"_snippet_border);
                    }
                }

                errors.emplace_back(
                    lnav::console::user_message::error(
                        attr_line_t("invalid sample log message: ")
                            .append(lnav::to_json(elf_sample.s_line.pp_value)))
                        .with_reason(attr_line_t("unrecognized timestamp -- ")
                                         .append(ts_cap.value()))
                        .with_snippet(elf_sample.s_line.to_snippet())
                        .with_note(notes)
                        .with_help(attr_line_t("If the timestamp format is not "
                                               "supported by default, you can "
                                               "add a custom format with the ")
                                       .append_quoted("timestamp-format"_symbol)
                                       .append(" property")));
            }

            auto level = this->convert_level(
                level_cap.value_or(string_fragment::invalid()), nullptr);

            if (elf_sample.s_level != LEVEL_UNKNOWN
                && elf_sample.s_level != level)
            {
                attr_line_t note_al;

                note_al.append("matched regex = ")
                    .append(lnav::roles::symbol(pat.p_name.to_string()))
                    .append("\n")
                    .append("captured level = ")
                    .append_quoted(level_cap->to_string());
                errors.emplace_back(
                    lnav::console::user_message::error(
                        attr_line_t("invalid sample log message: ")
                            .append(lnav::to_json(elf_sample.s_line.pp_value)))
                        .with_reason(attr_line_t()
                                         .append_quoted(lnav::roles::symbol(
                                             level_names[level]))
                                         .append(" does not match the expected "
                                                 "level of ")
                                         .append_quoted(lnav::roles::symbol(
                                             level_names[elf_sample.s_level])))
                        .with_snippet(elf_sample.s_line.to_snippet())
                        .with_note(note_al));
            }

            {
                auto full_match_res
                    = pat.p_pcre.pp_value
                          ->capture_from(elf_sample.s_line.pp_value)
                          .into(md)
                          .matches()
                          .ignore_error();
                if (!full_match_res) {
                    attr_line_t regex_al = pat.p_pcre.pp_value->get_pattern();
                    lnav::snippets::regex_highlighter(
                        regex_al, -1, line_range{0, (int) regex_al.length()});
                    errors.emplace_back(
                        lnav::console::user_message::error(
                            attr_line_t("invalid pattern: ")
                                .append_quoted(lnav::roles::symbol(
                                    pat.p_name.to_string())))
                            .with_reason("pattern does not match entire "
                                         "multiline sample message")
                            .with_snippet(elf_sample.s_line.to_snippet())
                            .with_note(attr_line_t()
                                           .append(lnav::roles::symbol(
                                               pat.p_name.to_string()))
                                           .append(" = ")
                                           .append(regex_al))
                            .with_help(
                                attr_line_t("use ").append_quoted(".*").append(
                                    " to match new-lines")));
                } else if (static_cast<size_t>(full_match_res->f_all.length())
                           != elf_sample.s_line.pp_value.length())
                {
                    attr_line_t regex_al = pat.p_pcre.pp_value->get_pattern();
                    lnav::snippets::regex_highlighter(
                        regex_al, -1, line_range{0, (int) regex_al.length()});
                    auto match_length
                        = static_cast<size_t>(full_match_res->f_all.length());
                    attr_line_t sample_al = elf_sample.s_line.pp_value;
                    sample_al.append("\n")
                        .append(match_length, ' ')
                        .append("^ matched up to here"_error)
                        .with_attr_for_all(
                            VC_ROLE.value(role_t::VCR_QUOTED_CODE));
                    auto sample_snippet = lnav::console::snippet::from(
                        elf_sample.s_line.pp_location, sample_al);
                    errors.emplace_back(
                        lnav::console::user_message::error(
                            attr_line_t("invalid pattern: ")
                                .append_quoted(lnav::roles::symbol(
                                    pat.p_name.to_string())))
                            .with_reason("pattern does not match entire "
                                         "message")
                            .with_snippet(sample_snippet)
                            .with_note(attr_line_t()
                                           .append(lnav::roles::symbol(
                                               pat.p_name.to_string()))
                                           .append(" = ")
                                           .append(regex_al))
                            .with_help("update the regular expression to fully "
                                       "capture the sample message"));
                }
            }
        }

        if (!found && !this->elf_pattern_order.empty()) {
            std::vector<std::pair<ssize_t, intern_string_t>> partial_indexes;
            attr_line_t notes;
            size_t max_name_width = 0;

            for (const auto& pat_iter : this->elf_pattern_order) {
                auto& pat = *pat_iter;

                if (!pat.p_pcre.pp_value) {
                    continue;
                }

                partial_indexes.emplace_back(
                    pat.p_pcre.pp_value->match_partial(sample_lines[0]),
                    pat.p_name);
                max_name_width = std::max(max_name_width, pat.p_name.size());
            }
            for (const auto& line_frag : sample_lines) {
                auto src_line = attr_line_t(line_frag.to_string());
                if (!line_frag.endswith("\n")) {
                    src_line.append("\n");
                }
                src_line.with_attr_for_all(
                    VC_ROLE.value(role_t::VCR_QUOTED_CODE));
                notes.append("   ").append(src_line);
                for (auto& part_pair : partial_indexes) {
                    if (part_pair.first >= 0
                        && part_pair.first < line_frag.length())
                    {
                        notes.append("   ")
                            .append(part_pair.first, ' ')
                            .append("^ "_snippet_border)
                            .append(lnav::roles::symbol(
                                part_pair.second.to_string()))
                            .append(" matched up to here"_snippet_border)
                            .append("\n");
                    }
                    part_pair.first -= line_frag.length();
                }
            }
            notes.add_header(
                "the following shows how each pattern matched this sample:\n");

            attr_line_t regex_note;
            for (const auto& pat_iter : this->elf_pattern_order) {
                if (!pat_iter->p_pcre.pp_value) {
                    regex_note
                        .append(
                            lnav::roles::symbol(fmt::format(FMT_STRING("{:{}}"),
                                                            pat_iter->p_name,
                                                            max_name_width)))
                        .append(" is invalid");
                    continue;
                }

                attr_line_t regex_al = pat_iter->p_pcre.pp_value->get_pattern();
                lnav::snippets::regex_highlighter(
                    regex_al, -1, line_range{0, (int) regex_al.length()});

                regex_note
                    .append(lnav::roles::symbol(fmt::format(
                        FMT_STRING("{:{}}"), pat_iter->p_name, max_name_width)))
                    .append(" = ")
                    .append_quoted(regex_al)
                    .append("\n");
            }

            errors.emplace_back(
                lnav::console::user_message::error(
                    attr_line_t("invalid sample log message: ")
                        .append(lnav::to_json(elf_sample.s_line.pp_value)))
                    .with_reason("sample does not match any patterns")
                    .with_snippet(elf_sample.s_line.to_snippet())
                    .with_note(notes.rtrim())
                    .with_note(regex_note));
        }
    }

    if (!this->elf_samples.empty()) {
        for (const auto& elf_sample : this->elf_samples) {
            if (elf_sample.s_matched_regexes.size() <= 1) {
                continue;
            }

            errors.emplace_back(
                lnav::console::user_message::warning(
                    attr_line_t("invalid log format: ")
                        .append_quoted(
                            lnav::roles::symbol(this->elf_name.to_string())))
                    .with_reason(
                        attr_line_t(
                            "sample is matched by more than one regex: ")
                            .join(elf_sample.s_matched_regexes,
                                  VC_ROLE.value(role_t::VCR_SYMBOL),
                                  ", "))
                    .with_snippet(lnav::console::snippet::from(
                        elf_sample.s_line.pp_location,
                        attr_line_t().append(lnav::roles::quoted_code(
                            elf_sample.s_line.pp_value))))
                    .with_help("log format regexes must match a single type "
                               "of log message"));
        }

        for (const auto& pat : this->elf_pattern_order) {
            if (pat->p_module_format) {
                continue;
            }

            if (pat->p_matched_samples.empty()) {
                errors.emplace_back(
                    lnav::console::user_message::warning(
                        attr_line_t("invalid pattern: ")
                            .append_quoted(
                                lnav::roles::symbol(pat->p_config_path)))
                        .with_reason("pattern does not match any samples")
                        .with_snippet(lnav::console::snippet::from(
                            pat->p_pcre.pp_location, ""))
                        .with_help(
                            "every pattern should have at least one sample "
                            "that it matches"));
            }
        }
    }

    for (auto& elf_value_def : this->elf_value_defs) {
        if (elf_value_def.second->vd_foreign_key
            || elf_value_def.second->vd_meta.lvm_identifier)
        {
            continue;
        }

        switch (elf_value_def.second->vd_meta.lvm_kind) {
            case value_kind_t::VALUE_INTEGER:
            case value_kind_t::VALUE_FLOAT:
                elf_value_def.second->vd_values_index
                    = this->elf_numeric_value_defs.size();
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
         ++iter, format_index++)
    {
        static const intern_string_t ts
            = intern_string::lookup("__timestamp__");
        static const intern_string_t level_field
            = intern_string::lookup("__level__");
        json_format_element& jfe = *iter;

        if (startswith(jfe.jfe_value.pp_value.get(), "/")) {
            jfe.jfe_value.pp_value
                = intern_string::lookup(jfe.jfe_value.pp_value.get() + 1);
        }
        if (!jfe.jfe_ts_format.empty()) {
            if (!jfe.jfe_value.pp_value.empty() && jfe.jfe_value.pp_value != ts)
            {
                log_warning(
                    "%s:line-format[%d]:ignoring field '%s' since "
                    "timestamp-format was used",
                    this->elf_name.get(),
                    format_index,
                    jfe.jfe_value.pp_value.get());
            }
            jfe.jfe_value.pp_value = ts;
        }

        switch (jfe.jfe_type) {
            case json_log_field::VARIABLE: {
                auto vd_iter
                    = this->elf_value_defs.find(jfe.jfe_value.pp_value);
                if (jfe.jfe_value.pp_value == ts) {
                    this->elf_value_defs[this->lf_timestamp_field]
                        ->vd_meta.lvm_hidden
                        = true;
                } else if (jfe.jfe_value.pp_value == level_field) {
                    this->elf_value_defs[this->elf_level_field]
                        ->vd_meta.lvm_hidden
                        = true;
                } else if (vd_iter == this->elf_value_defs.end()) {
                    errors.emplace_back(
                        lnav::console::user_message::error(
                            attr_line_t("invalid line format element ")
                                .append_quoted(lnav::roles::symbol(fmt::format(
                                    FMT_STRING("/{}/line-format/{}/field"),
                                    this->elf_name,
                                    format_index))))
                            .with_reason(
                                attr_line_t()
                                    .append_quoted(jfe.jfe_value.pp_value)
                                    .append(" is not a defined value"))
                            .with_snippet(jfe.jfe_value.to_snippet()));
                }
                break;
            }
            case json_log_field::CONSTANT:
                this->jlf_line_format_init_count
                    += std::count(jfe.jfe_default_value.begin(),
                                  jfe.jfe_default_value.end(),
                                  '\n');
                break;
            default:
                break;
        }
    }

    for (auto& hd_pair : this->elf_highlighter_patterns) {
        external_log_format::highlighter_def& hd = hd_pair.second;
        auto fg = styling::color_unit::make_empty();
        auto bg = styling::color_unit::make_empty();
        text_attrs attrs;

        if (!hd.hd_color.pp_value.empty()) {
            fg = styling::color_unit::from_str(hd.hd_color.pp_value)
                     .unwrapOrElse([&](const auto& msg) {
                         errors.emplace_back(
                             lnav::console::user_message::error(
                                 attr_line_t()
                                     .append_quoted(hd.hd_color.pp_value)
                                     .append(" is not a valid color value for "
                                             "property ")
                                     .append_quoted(lnav::roles::symbol(
                                         hd.hd_color.pp_path.to_string())))
                                 .with_reason(msg)
                                 .with_snippet(hd.hd_color.to_snippet()));
                         return styling::color_unit::make_empty();
                     });
        }

        if (!hd.hd_background_color.pp_value.empty()) {
            bg = styling::color_unit::from_str(hd.hd_background_color.pp_value)
                     .unwrapOrElse([&](const auto& msg) {
                         errors.emplace_back(
                             lnav::console::user_message::error(
                                 attr_line_t()
                                     .append_quoted(
                                         hd.hd_background_color.pp_value)
                                     .append(" is not a valid color value for "
                                             "property ")
                                     .append_quoted(lnav::roles::symbol(
                                         hd.hd_background_color.pp_path
                                             .to_string())))
                                 .with_reason(msg)
                                 .with_snippet(
                                     hd.hd_background_color.to_snippet()));
                         return styling::color_unit::make_empty();
                     });
        }

        if (hd.hd_underline) {
            attrs.ta_attrs |= A_UNDERLINE;
        }
        if (hd.hd_blink) {
            attrs.ta_attrs |= A_BLINK;
        }

        if (hd.hd_pattern.pp_value != nullptr) {
            this->lf_highlighters.emplace_back(hd.hd_pattern.pp_value);
            this->lf_highlighters.back()
                .with_name(hd_pair.first.to_string())
                .with_format_name(this->elf_name)
                .with_color(fg, bg)
                .with_attrs(attrs);
        }
    }
}

void
external_log_format::register_vtabs(
    log_vtab_manager* vtab_manager,
    std::vector<lnav::console::user_message>& errors)
{
    for (auto& elf_search_table : this->elf_search_tables) {
        if (elf_search_table.second.std_pattern.pp_value == nullptr) {
            continue;
        }

        auto lst = std::make_shared<log_search_table>(
            elf_search_table.second.std_pattern.pp_value,
            elf_search_table.first);
        lst->lst_format = this;
        lst->lst_log_path_glob = elf_search_table.second.std_glob;
        if (elf_search_table.second.std_level != LEVEL_UNKNOWN) {
            lst->lst_log_level = elf_search_table.second.std_level;
        }
        auto errmsg = vtab_manager->register_vtab(lst);
        if (!errmsg.empty()) {
#if 0
            errors.push_back("error:" + this->elf_name.to_string() + ":"
                             + search_iter->first.to_string()
                             + ":unable to register table -- " + errmsg);
#endif
        }
    }
}

bool
external_log_format::match_samples(const std::vector<sample>& samples) const
{
    for (const auto& sample_iter : samples) {
        for (const auto& pat_iter : this->elf_pattern_order) {
            auto& pat = *pat_iter;

            if (!pat.p_pcre.pp_value) {
                continue;
            }

            if (pat.p_pcre.pp_value->find_in(sample_iter.s_line.pp_value)
                    .ignore_error())
            {
                return true;
            }
        }
    }

    return false;
}

class external_log_table : public log_format_vtab_impl {
public:
    explicit external_log_table(const external_log_format& elf)
        : log_format_vtab_impl(elf), elt_format(elf)
    {
    }

    void get_columns(std::vector<vtab_column>& cols) const override
    {
        const auto& elf = this->elt_format;

        cols.resize(elf.elf_column_count);
        for (const auto& vd : elf.elf_value_def_order) {
            auto type_pair = log_vtab_impl::logline_value_to_sqlite_type(
                vd->vd_meta.lvm_kind);

            if (vd->vd_meta.lvm_column == -1) {
                continue;
            }

            require(0 <= vd->vd_meta.lvm_column
                    && vd->vd_meta.lvm_column < elf.elf_column_count);

            cols[vd->vd_meta.lvm_column].vc_name = vd->vd_meta.lvm_name.get();
            cols[vd->vd_meta.lvm_column].vc_type = type_pair.first;
            cols[vd->vd_meta.lvm_column].vc_subtype = type_pair.second;
            cols[vd->vd_meta.lvm_column].vc_collator = vd->vd_collate;
            cols[vd->vd_meta.lvm_column].vc_comment = vd->vd_description;
        }
    }

    void get_foreign_keys(std::vector<std::string>& keys_inout) const override
    {
        log_vtab_impl::get_foreign_keys(keys_inout);

        for (const auto& elf_value_def : this->elt_format.elf_value_defs) {
            if (elf_value_def.second->vd_foreign_key) {
                keys_inout.emplace_back(elf_value_def.first.to_string());
            }
        }
    }

    bool next(log_cursor& lc, logfile_sub_source& lss) override
    {
        if (lc.is_eof()) {
            return true;
        }

        content_line_t cl(lss.at(lc.lc_curr_line));
        auto* lf = lss.find_file_ptr(cl);
        auto lf_iter = lf->begin() + cl;
        uint8_t mod_id = lf_iter->get_module_id();

        if (lf_iter->is_continued()) {
            return false;
        }

        this->elt_module_format.mf_mod_format = nullptr;
        if (lf->get_format_name() == this->lfvi_format.get_name()) {
            return true;
        } else if (mod_id && mod_id == this->lfvi_format.lf_mod_index) {
            auto format = lf->get_format();

            return lf->read_line(lf_iter)
                .map([this, format, cl](auto line) {
                    logline_value_vector values;
                    struct line_range mod_name_range;
                    intern_string_t mod_name;

                    this->vi_attrs.clear();
                    values.lvv_sbr = line;
                    format->annotate(cl, this->vi_attrs, values, false);
                    this->elt_container_body
                        = find_string_attr_range(this->vi_attrs, &SA_BODY);
                    if (!this->elt_container_body.is_valid()) {
                        return false;
                    }
                    this->elt_container_body.ltrim(line.get_data());
                    mod_name_range = find_string_attr_range(this->vi_attrs,
                                                            &logline::L_MODULE);
                    if (!mod_name_range.is_valid()) {
                        return false;
                    }
                    mod_name = intern_string::lookup(
                        &line.get_data()[mod_name_range.lr_start],
                        mod_name_range.length());
                    this->vi_attrs.clear();
                    this->elt_module_format
                        = external_log_format::MODULE_FORMATS[mod_name];
                    if (!this->elt_module_format.mf_mod_format) {
                        return false;
                    }
                    return this->elt_module_format.mf_mod_format->get_name()
                        == this->lfvi_format.get_name();
                })
                .unwrapOr(false);
        }

        return false;
    }

    void extract(logfile* lf,
                 uint64_t line_number,
                 logline_value_vector& values) override
    {
        auto& line = values.lvv_sbr;
        auto format = lf->get_format();

        if (this->elt_module_format.mf_mod_format != nullptr) {
            shared_buffer_ref body_ref;

            body_ref.subset(line,
                            this->elt_container_body.lr_start,
                            this->elt_container_body.length());
            this->vi_attrs.clear();
            auto narrow_res
                = values.lvv_sbr.narrow(this->elt_container_body.lr_start,
                                        this->elt_container_body.length());
            values.lvv_values.clear();
            this->elt_module_format.mf_mod_format->annotate(
                line_number, this->vi_attrs, values, false);
            values.lvv_sbr.widen(narrow_res);
        } else {
            this->vi_attrs.clear();
            format->annotate(line_number, this->vi_attrs, values, false);
        }
    }

    const external_log_format& elt_format;
    module_format elt_module_format;
    struct line_range elt_container_body;
};

std::shared_ptr<log_vtab_impl>
external_log_format::get_vtab_impl() const
{
    return std::make_shared<external_log_table>(*this);
}

std::shared_ptr<log_format>
external_log_format::specialized(int fmt_lock)
{
    auto retval = std::make_shared<external_log_format>(*this);

    retval->lf_specialized = true;
    this->lf_pattern_locks.clear();
    if (fmt_lock != -1) {
        retval->lf_pattern_locks.emplace_back(0, fmt_lock);
    }

    if (this->elf_type == elf_type_t::ELF_TYPE_JSON) {
        this->jlf_parse_context
            = std::make_shared<yajlpp_parse_context>(this->elf_name);
        this->jlf_yajl_handle.reset(
            yajl_alloc(&this->jlf_parse_context->ypc_callbacks,
                       nullptr,
                       this->jlf_parse_context.get()),
            yajl_handle_deleter());
        yajl_config(this->jlf_yajl_handle.get(), yajl_dont_validate_strings, 1);
        this->jlf_cached_line.reserve(16 * 1024);
    }

    this->lf_value_stats.clear();
    this->lf_value_stats.resize(this->elf_numeric_value_defs.size());

    return retval;
}

bool
external_log_format::match_name(const std::string& filename)
{
    if (this->elf_filename_pcre.pp_value == nullptr) {
        return true;
    }

    return this->elf_filename_pcre.pp_value->find_in(filename)
        .ignore_error()
        .has_value();
}

bool
external_log_format::match_mime_type(const file_format_t ff) const
{
    if (ff == file_format_t::UNKNOWN && this->elf_mime_types.empty()) {
        return true;
    }

    return this->elf_mime_types.count(ff) == 1;
}

long
external_log_format::value_line_count(const intern_string_t ist,
                                      bool top_level,
                                      const unsigned char* str,
                                      ssize_t len) const
{
    const auto iter = this->elf_value_defs.find(ist);
    long line_count
        = (str != nullptr) ? std::count(&str[0], &str[len], '\n') + 1 : 1;

    if (iter == this->elf_value_defs.end()) {
        return (this->jlf_hide_extra || !top_level) ? 0 : line_count;
    }

    if (iter->second->vd_meta.lvm_hidden) {
        return 0;
    }

    if (std::find_if(this->jlf_line_format.begin(),
                     this->jlf_line_format.end(),
                     json_field_cmp(json_log_field::VARIABLE, ist))
        != this->jlf_line_format.end())
    {
        return line_count - 1;
    }

    return line_count;
}

log_level_t
external_log_format::convert_level(string_fragment sf,
                                   scan_batch_context* sbc) const
{
    log_level_t retval = LEVEL_INFO;

    if (sf.is_valid()) {
        if (sbc != nullptr && sbc->sbc_cached_level_count > 0) {
            auto cached_level_iter
                = std::find(std::begin(sbc->sbc_cached_level_strings),
                            std::begin(sbc->sbc_cached_level_strings)
                                + sbc->sbc_cached_level_count,
                            sf);
            if (cached_level_iter
                != std::begin(sbc->sbc_cached_level_strings)
                    + sbc->sbc_cached_level_count)
            {
                auto cache_index
                    = std::distance(std::begin(sbc->sbc_cached_level_strings),
                                    cached_level_iter);
                if (cache_index != 0) {
                    std::swap(sbc->sbc_cached_level_strings[cache_index],
                              sbc->sbc_cached_level_strings[0]);
                    std::swap(sbc->sbc_cached_level_values[cache_index],
                              sbc->sbc_cached_level_values[0]);
                }
                return sbc->sbc_cached_level_values[0];
            }
        }

        if (this->elf_level_patterns.empty()) {
            retval = string2level(sf.data(), sf.length());
        } else {
            for (const auto& elf_level_pattern : this->elf_level_patterns) {
                if (elf_level_pattern.second.lp_pcre.pp_value
                        ->find_in(sf, PCRE2_NO_UTF_CHECK)
                        .ignore_error()
                        .has_value())
                {
                    retval = elf_level_pattern.first;
                    break;
                }
            }
        }

        if (sbc != nullptr && sf.length() < 10) {
            size_t cache_index;

            if (sbc->sbc_cached_level_count == 4) {
                cache_index = sbc->sbc_cached_level_count - 1;
            } else {
                cache_index = sbc->sbc_cached_level_count;
                sbc->sbc_cached_level_count += 1;
            }
            sbc->sbc_cached_level_strings[cache_index] = sf.to_string();
            sbc->sbc_cached_level_values[cache_index] = retval;
        }
    }

    return retval;
}

logline_value_meta
external_log_format::get_value_meta(intern_string_t field_name,
                                    value_kind_t kind)
{
    auto iter = this->elf_value_defs.find(field_name);

    if (iter == this->elf_value_defs.end()) {
        auto retval = logline_value_meta(field_name, kind, -1, this);

        retval.lvm_hidden = this->jlf_hide_extra;
        return retval;
    }

    auto lvm = iter->second->vd_meta;

    lvm.lvm_kind = kind;
    return lvm;
}

void
external_log_format::json_append(
    const external_log_format::json_format_element& jfe,
    const char* value,
    ssize_t len)
{
    if (len == -1) {
        len = strlen(value);
    }
    if (jfe.jfe_align == json_format_element::align_t::RIGHT) {
        if (len < jfe.jfe_min_width) {
            this->json_append_to_cache(jfe.jfe_min_width - len);
        }
    }
    this->json_append_to_cache(value, len);
    if (jfe.jfe_align == json_format_element::align_t::LEFT) {
        if (len < jfe.jfe_min_width) {
            this->json_append_to_cache(jfe.jfe_min_width - len);
        }
    }
}

intern_string_t
external_log_format::get_pattern_name(uint64_t line_number) const
{
    if (this->elf_type != elf_type_t::ELF_TYPE_TEXT) {
        static auto structured = intern_string::lookup("structured");

        return structured;
    }
    int pat_index = this->pattern_index_for_line(line_number);
    return this->elf_pattern_order[pat_index]->p_name;
}

int
log_format::pattern_index_for_line(uint64_t line_number) const
{
    auto iter = lower_bound(this->lf_pattern_locks.cbegin(),
                            this->lf_pattern_locks.cend(),
                            line_number,
                            [](const pattern_for_lines& pfl, uint32_t line) {
                                return pfl.pfl_line < line;
                            });

    if (iter == this->lf_pattern_locks.end() || iter->pfl_line != line_number) {
        --iter;
    }

    return iter->pfl_pat_index;
}

std::string
log_format::get_pattern_path(uint64_t line_number) const
{
    int pat_index = this->pattern_index_for_line(line_number);
    return fmt::format(FMT_STRING("builtin ({})"), pat_index);
}

intern_string_t
log_format::get_pattern_name(uint64_t line_number) const
{
    char pat_str[128];

    int pat_index = this->pattern_index_for_line(line_number);
    snprintf(pat_str, sizeof(pat_str), "builtin (%d)", pat_index);
    return intern_string::lookup(pat_str);
}

std::shared_ptr<log_format>
log_format::find_root_format(const char* name)
{
    auto& fmts = get_root_formats();
    for (auto& lf : fmts) {
        if (lf->get_name() == name) {
            return lf;
        }
    }
    return nullptr;
}

log_format::pattern_for_lines::pattern_for_lines(uint32_t pfl_line,
                                                 uint32_t pfl_pat_index)
    : pfl_line(pfl_line), pfl_pat_index(pfl_pat_index)
{
}

void
logline_value_stats::merge(const logline_value_stats& other)
{
    if (other.lvs_count == 0) {
        return;
    }

    require(other.lvs_min_value <= other.lvs_max_value);

    if (other.lvs_min_value < this->lvs_min_value) {
        this->lvs_min_value = other.lvs_min_value;
    }
    if (other.lvs_max_value > this->lvs_max_value) {
        this->lvs_max_value = other.lvs_max_value;
    }
    this->lvs_count += other.lvs_count;
    this->lvs_total += other.lvs_total;

    ensure(this->lvs_count >= 0);
    ensure(this->lvs_min_value <= this->lvs_max_value);
}

void
logline_value_stats::add_value(double value)
{
    if (value < this->lvs_min_value) {
        this->lvs_min_value = value;
    }
    if (value > this->lvs_max_value) {
        this->lvs_max_value = value;
    }
    this->lvs_count += 1;
    this->lvs_total += value;
}

std::vector<logline_value_meta>
external_log_format::get_value_metadata() const
{
    std::vector<logline_value_meta> retval;

    for (const auto& vd : this->elf_value_def_order) {
        retval.emplace_back(vd->vd_meta);
    }

    return retval;
}

bool
format_tag_def::path_restriction::matches(const char* fn) const
{
    return fnmatch(this->p_glob.c_str(), fn, 0) == 0;
}

/* XXX */
#include "log_format_impls.cc"
