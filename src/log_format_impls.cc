/**
 * Copyright (c) 2007-2017, Timothy Stack
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
 *
 * @file log_format_impls.cc
 */

#include <algorithm>
#include <utility>

#include "log_format.hh"

#include <stdio.h>

#include "base/injector.bind.hh"
#include "base/opt_util.hh"
#include "config.h"
#include "formats/logfmt/logfmt.parser.hh"
#include "log_vtab_impl.hh"
#include "sql_util.hh"
#include "yajlpp/yajlpp.hh"

class generic_log_format : public log_format {
    static pcre_format* get_pcre_log_formats()
    {
        static pcre_format log_fmt[] = {
            pcre_format(
                "^(?:\\*\\*\\*\\s+)?(?<timestamp>@[0-9a-zA-Z]{16,24})(.*)"),
            pcre_format(
                "^(?:\\*\\*\\*\\s+)?(?<timestamp>[\\dTZ: +/\\-,\\.-]+)([^:]+)"),
            pcre_format(
                "^(?:\\*\\*\\*\\s+)?(?<timestamp>[\\w:+/\\.-]+) \\[\\w (.*)"),
            pcre_format("^(?:\\*\\*\\*\\s+)?(?<timestamp>[\\w:,/\\.-]+) (.*)"),
            pcre_format(
                "^(?:\\*\\*\\*\\s+)?(?<timestamp>[\\w:,/\\.-]+) - (.*)"),
            pcre_format(
                "^(?:\\*\\*\\*\\s+)?(?<timestamp>[\\w: \\.,/-]+) - (.*)"),
            pcre_format("^(?:\\*\\*\\*\\s+)?(?<timestamp>[\\w: "
                        "\\.,/-]+)\\[[^\\]]+\\](.*)"),
            pcre_format("^(?:\\*\\*\\*\\s+)?(?<timestamp>[\\w: \\.,/-]+) (.*)"),

            pcre_format(
                R"(^(?:\*\*\*\s+)?\[(?<timestamp>[\w: \.,+/-]+)\]\s*(\w+):?)"),
            pcre_format(
                "^(?:\\*\\*\\*\\s+)?\\[(?<timestamp>[\\w: \\.,+/-]+)\\] (.*)"),
            pcre_format("^(?:\\*\\*\\*\\s+)?\\[(?<timestamp>[\\w: "
                        "\\.,+/-]+)\\] \\[(\\w+)\\]"),
            pcre_format("^(?:\\*\\*\\*\\s+)?\\[(?<timestamp>[\\w: "
                        "\\.,+/-]+)\\] \\w+ (.*)"),
            pcre_format("^(?:\\*\\*\\*\\s+)?\\[(?<timestamp>[\\w: ,+/-]+)\\] "
                        "\\(\\d+\\) (.*)"),

            pcre_format(),
        };

        return log_fmt;
    }

    std::string get_pattern_regex(uint64_t line_number) const override
    {
        int pat_index = this->pattern_index_for_line(line_number);
        return get_pcre_log_formats()[pat_index].name;
    }

    const intern_string_t get_name() const override
    {
        return intern_string::lookup("generic_log");
    }

    scan_result_t scan(logfile& lf,
                       std::vector<logline>& dst,
                       const line_info& li,
                       shared_buffer_ref& sbr,
                       scan_batch_context& sbc) override
    {
        struct exttm log_time;
        struct timeval log_tv;
        string_fragment ts;
        nonstd::optional<string_fragment> level;
        const char* last_pos;

        if ((last_pos = this->log_scanf(dst.size(),
                                        sbr.to_string_fragment(),
                                        get_pcre_log_formats(),
                                        nullptr,
                                        &log_time,
                                        &log_tv,

                                        &ts,
                                        &level))
            != nullptr)
        {
            log_level_t level_val = log_level_t::LEVEL_UNKNOWN;
            if (level) {
                level_val = string2level(level->data(), level->length());
            }

            if (!((log_time.et_flags & ETF_DAY_SET)
                  && (log_time.et_flags & ETF_MONTH_SET)
                  && (log_time.et_flags & ETF_YEAR_SET)))
            {
                this->check_for_new_year(dst, log_time, log_tv);
            }

            dst.emplace_back(li.li_file_range.fr_offset, log_tv, level_val);
            return SCAN_MATCH;
        }

        return SCAN_NO_MATCH;
    }

    void annotate(uint64_t line_number,
                  string_attrs_t& sa,
                  logline_value_vector& values,
                  bool annotate_module) const override
    {
        auto& line = values.lvv_sbr;
        int pat_index = this->pattern_index_for_line(line_number);
        auto& fmt = get_pcre_log_formats()[pat_index];
        int prefix_len = 0;
        auto md = fmt.pcre->create_match_data();
        auto match_res = fmt.pcre->capture_from(line.to_string_fragment())
                             .into(md)
                             .matches(PCRE2_NO_UTF_CHECK)
                             .ignore_error();
        if (!match_res) {
            return;
        }

        auto lr = to_line_range(md[fmt.pf_timestamp_index].value());
        sa.emplace_back(lr, logline::L_TIMESTAMP.value());

        prefix_len = lr.lr_end;
        auto level_cap = md[2];
        if (level_cap) {
            if (string2level(level_cap->data(), level_cap->length(), true)
                != LEVEL_UNKNOWN)
            {
                prefix_len = level_cap->sf_end;
            }
        }

        lr.lr_start = 0;
        lr.lr_end = prefix_len;
        sa.emplace_back(lr, logline::L_PREFIX.value());

        lr.lr_start = prefix_len;
        lr.lr_end = line.length();
        sa.emplace_back(lr, SA_BODY.value());
    }

    std::shared_ptr<log_format> specialized(int fmt_lock) override
    {
        auto retval = std::make_shared<generic_log_format>(*this);

        retval->lf_specialized = true;
        return retval;
    }
};

std::string
from_escaped_string(const char* str, size_t len)
{
    std::string retval;

    for (size_t lpc = 0; lpc < len; lpc++) {
        switch (str[lpc]) {
            case '\\':
                if ((lpc + 3) < len && str[lpc + 1] == 'x') {
                    int ch;

                    if (sscanf(&str[lpc + 2], "%2x", &ch) == 1) {
                        retval.append(1, (char) ch & 0xff);
                        lpc += 3;
                    }
                }
                break;
            default:
                retval.append(1, str[lpc]);
                break;
        }
    }

    return retval;
}

nonstd::optional<const char*>
lnav_strnstr(const char* s, const char* find, size_t slen)
{
    char c, sc;
    size_t len;

    if ((c = *find++) != '\0') {
        len = strlen(find);
        do {
            do {
                if (slen < 1 || (sc = *s) == '\0') {
                    return nonstd::nullopt;
                }
                --slen;
                ++s;
            } while (sc != c);
            if (len > slen) {
                return nonstd::nullopt;
            }
        } while (strncmp(s, find, len) != 0);
        s--;
    }
    return s;
}

struct separated_string {
    const char* ss_str;
    size_t ss_len;
    const char* ss_separator;
    size_t ss_separator_len;

    separated_string(const char* str, size_t len)
        : ss_str(str), ss_len(len), ss_separator(","),
          ss_separator_len(strlen(this->ss_separator))
    {
    }

    separated_string& with_separator(const char* sep)
    {
        this->ss_separator = sep;
        this->ss_separator_len = strlen(sep);
        return *this;
    }

    struct iterator {
        const separated_string& i_parent;
        const char* i_pos;
        const char* i_next_pos;
        size_t i_index;

        iterator(const separated_string& ss, const char* pos)
            : i_parent(ss), i_pos(pos), i_next_pos(pos), i_index(0)
        {
            this->update();
        }

        void update()
        {
            const separated_string& ss = this->i_parent;
            auto next_field
                = lnav_strnstr(this->i_pos,
                               ss.ss_separator,
                               ss.ss_len - (this->i_pos - ss.ss_str));
            if (next_field) {
                this->i_next_pos = next_field.value() + ss.ss_separator_len;
            } else {
                this->i_next_pos = ss.ss_str + ss.ss_len;
            }
        }

        iterator& operator++()
        {
            this->i_pos = this->i_next_pos;
            this->update();
            this->i_index += 1;

            return *this;
        }

        string_fragment operator*()
        {
            const auto& ss = this->i_parent;
            int end;

            if (this->i_next_pos < (ss.ss_str + ss.ss_len)) {
                end = this->i_next_pos - ss.ss_str - ss.ss_separator_len;
            } else {
                end = this->i_next_pos - ss.ss_str;
            }
            return string_fragment::from_byte_range(
                ss.ss_str, this->i_pos - ss.ss_str, end);
        }

        bool operator==(const iterator& other) const
        {
            return (&this->i_parent == &other.i_parent)
                && (this->i_pos == other.i_pos);
        }

        bool operator!=(const iterator& other) const
        {
            return !(*this == other);
        }

        size_t index() const { return this->i_index; }
    };

    iterator begin() { return {*this, this->ss_str}; }

    iterator end() { return {*this, this->ss_str + this->ss_len}; }
};

class bro_log_format : public log_format {
public:
    struct field_def {
        logline_value_meta fd_meta;
        std::string fd_collator;
        nonstd::optional<size_t> fd_numeric_index;

        explicit field_def(const intern_string_t name,
                           int col,
                           log_format* format)
            : fd_meta(name, value_kind_t::VALUE_TEXT, col, format)
        {
        }

        field_def& with_kind(value_kind_t kind,
                             bool identifier = false,
                             const std::string& collator = "")
        {
            this->fd_meta.lvm_kind = kind;
            this->fd_meta.lvm_identifier = identifier;
            this->fd_collator = collator;
            return *this;
        }

        field_def& with_numeric_index(size_t index)
        {
            this->fd_numeric_index = index;
            return *this;
        }
    };

    bro_log_format()
    {
        this->lf_is_self_describing = true;
        this->lf_time_ordered = false;
    }

    const intern_string_t get_name() const override
    {
        static const intern_string_t name(intern_string::lookup("bro"));

        return this->blf_format_name.empty() ? name : this->blf_format_name;
    }

    void clear() override
    {
        this->log_format::clear();
        this->blf_format_name.clear();
        this->blf_field_defs.clear();
    }

    scan_result_t scan_int(std::vector<logline>& dst,
                           const line_info& li,
                           shared_buffer_ref& sbr)
    {
        static const intern_string_t STATUS_CODE
            = intern_string::lookup("bro_status_code");
        static const intern_string_t TS = intern_string::lookup("bro_ts");
        static const intern_string_t UID = intern_string::lookup("bro_uid");

        separated_string ss(sbr.get_data(), sbr.length());
        struct timeval tv;
        struct exttm tm;
        bool found_ts = false;
        log_level_t level = LEVEL_INFO;
        uint8_t opid = 0;

        ss.with_separator(this->blf_separator.get());

        for (auto iter = ss.begin(); iter != ss.end(); ++iter) {
            if (iter.index() == 0 && *iter == "#close") {
                return SCAN_MATCH;
            }

            if (iter.index() >= this->blf_field_defs.size()) {
                break;
            }

            const auto& fd = this->blf_field_defs[iter.index()];

            if (TS == fd.fd_meta.lvm_name) {
                string_fragment sf = *iter;

                if (this->lf_date_time.scan(
                        sf.data(), sf.length(), nullptr, &tm, tv))
                {
                    this->lf_timestamp_flags = tm.et_flags;
                    found_ts = true;
                }
            } else if (STATUS_CODE == fd.fd_meta.lvm_name) {
                string_fragment sf = *iter;

                if (!sf.empty() && sf[0] >= '4') {
                    level = LEVEL_ERROR;
                }
            } else if (UID == fd.fd_meta.lvm_name) {
                string_fragment sf = *iter;

                opid = hash_str(sf.data(), sf.length());
            }

            if (fd.fd_numeric_index) {
                switch (fd.fd_meta.lvm_kind) {
                    case value_kind_t::VALUE_INTEGER:
                    case value_kind_t::VALUE_FLOAT: {
                        string_fragment sf = *iter;
                        char field_copy[sf.length() + 1];
                        double val;

                        if (sscanf(sf.to_string(field_copy), "%lf", &val) == 1)
                        {
                            this->lf_value_stats[fd.fd_numeric_index.value()]
                                .add_value(val);
                        }
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        if (found_ts) {
            if (!this->lf_specialized) {
                for (auto& ll : dst) {
                    ll.set_ignore(true);
                }
            }
            dst.emplace_back(li.li_file_range.fr_offset, tv, level, 0, opid);
            return SCAN_MATCH;
        }
        return SCAN_NO_MATCH;
    }

    scan_result_t scan(logfile& lf,
                       std::vector<logline>& dst,
                       const line_info& li,
                       shared_buffer_ref& sbr,
                       scan_batch_context& sbc) override
    {
        static const auto SEP_RE
            = lnav::pcre2pp::code::from_const(R"(^#separator\s+(.+))");

        if (!this->blf_format_name.empty()) {
            return this->scan_int(dst, li, sbr);
        }

        if (dst.empty() || dst.size() > 20 || sbr.empty()
            || sbr.get_data()[0] == '#')
        {
            return SCAN_NO_MATCH;
        }

        auto line_iter = dst.begin();
        auto read_result = lf.read_line(line_iter);

        if (read_result.isErr()) {
            return SCAN_NO_MATCH;
        }

        auto line = read_result.unwrap();
        auto md = SEP_RE.create_match_data();

        auto match_res = SEP_RE.capture_from(line.to_string_fragment())
                             .into(md)
                             .matches(PCRE2_NO_UTF_CHECK)
                             .ignore_error();
        if (!match_res) {
            return SCAN_NO_MATCH;
        }

        this->clear();

        auto sep = from_escaped_string(md[1]->data(), md[1]->length());
        this->blf_separator = intern_string::lookup(sep);

        for (++line_iter; line_iter != dst.end(); ++line_iter) {
            auto next_read_result = lf.read_line(line_iter);

            if (next_read_result.isErr()) {
                return SCAN_NO_MATCH;
            }

            line = next_read_result.unwrap();
            separated_string ss(line.get_data(), line.length());

            ss.with_separator(this->blf_separator.get());
            auto iter = ss.begin();

            string_fragment directive = *iter;

            if (directive.empty() || directive[0] != '#') {
                continue;
            }

            ++iter;
            if (iter == ss.end()) {
                continue;
            }

            if (directive == "#set_separator") {
                this->blf_set_separator = intern_string::lookup(*iter);
            } else if (directive == "#empty_field") {
                this->blf_empty_field = intern_string::lookup(*iter);
            } else if (directive == "#unset_field") {
                this->blf_unset_field = intern_string::lookup(*iter);
            } else if (directive == "#path") {
                auto full_name = fmt::format(FMT_STRING("bro_{}_log"), *iter);
                this->blf_format_name = intern_string::lookup(full_name);
            } else if (directive == "#fields" && this->blf_field_defs.empty()) {
                do {
                    this->blf_field_defs.emplace_back(
                        intern_string::lookup("bro_" + sql_safe_ident(*iter)),
                        this->blf_field_defs.size(),
                        this);
                    ++iter;
                } while (iter != ss.end());
            } else if (directive == "#types") {
                static const char* KNOWN_IDS[] = {
                    "bro_conn_uids",
                    "bro_fuid",
                    "bro_host",
                    "bro_info_code",
                    "bro_method",
                    "bro_mime_type",
                    "bro_orig_fuids",
                    "bro_parent_fuid",
                    "bro_proto",
                    "bro_referrer",
                    "bro_resp_fuids",
                    "bro_service",
                    "bro_status_code",
                    "bro_uid",
                    "bro_uri",
                    "bro_user_agent",
                    "bro_username",
                };

                int numeric_count = 0;

                do {
                    string_fragment field_type = *iter;
                    auto& fd = this->blf_field_defs[iter.index() - 1];

                    if (field_type == "time") {
                        fd.with_kind(value_kind_t::VALUE_TIMESTAMP);
                    } else if (field_type == "string") {
                        bool ident = std::binary_search(std::begin(KNOWN_IDS),
                                                        std::end(KNOWN_IDS),
                                                        fd.fd_meta.lvm_name);
                        fd.with_kind(value_kind_t::VALUE_TEXT, ident);
                    } else if (field_type == "count") {
                        bool ident = std::binary_search(std::begin(KNOWN_IDS),
                                                        std::end(KNOWN_IDS),
                                                        fd.fd_meta.lvm_name);
                        fd.with_kind(value_kind_t::VALUE_INTEGER, ident)
                            .with_numeric_index(numeric_count);
                        numeric_count += 1;
                    } else if (field_type == "bool") {
                        fd.with_kind(value_kind_t::VALUE_BOOLEAN);
                    } else if (field_type == "addr") {
                        fd.with_kind(
                            value_kind_t::VALUE_TEXT, true, "ipaddress");
                    } else if (field_type == "port") {
                        fd.with_kind(value_kind_t::VALUE_INTEGER, true);
                    } else if (field_type == "interval") {
                        fd.with_kind(value_kind_t::VALUE_FLOAT)
                            .with_numeric_index(numeric_count);
                        numeric_count += 1;
                    }

                    ++iter;
                } while (iter != ss.end());

                this->lf_value_stats.resize(numeric_count);
            }
        }

        if (!this->blf_format_name.empty() && !this->blf_separator.empty()
            && !this->blf_field_defs.empty())
        {
            dst.clear();
            return this->scan_int(dst, li, sbr);
        }

        this->blf_format_name.clear();
        this->lf_value_stats.clear();

        return SCAN_NO_MATCH;
    }

    void annotate(uint64_t line_number,
                  string_attrs_t& sa,
                  logline_value_vector& values,
                  bool annotate_module) const override
    {
        static const intern_string_t TS = intern_string::lookup("bro_ts");
        static const intern_string_t UID = intern_string::lookup("bro_uid");

        auto& sbr = values.lvv_sbr;
        separated_string ss(sbr.get_data(), sbr.length());

        ss.with_separator(this->blf_separator.get());

        for (auto iter = ss.begin(); iter != ss.end(); ++iter) {
            if (iter.index() >= this->blf_field_defs.size()) {
                return;
            }

            const field_def& fd = this->blf_field_defs[iter.index()];
            string_fragment sf = *iter;

            if (sf == this->blf_empty_field) {
                sf.clear();
            } else if (sf == this->blf_unset_field) {
                sf.invalidate();
            }

            auto lr = line_range(sf.sf_begin, sf.sf_end);

            if (fd.fd_meta.lvm_name == TS) {
                sa.emplace_back(lr, logline::L_TIMESTAMP.value());
            } else if (fd.fd_meta.lvm_name == UID) {
                sa.emplace_back(lr, logline::L_OPID.value());
            }

            if (lr.is_valid()) {
                values.lvv_values.emplace_back(fd.fd_meta, sbr, lr);
            } else {
                values.lvv_values.emplace_back(fd.fd_meta);
            }
        }
    }

    const logline_value_stats* stats_for_value(
        const intern_string_t& name) const override
    {
        const logline_value_stats* retval = nullptr;

        for (const auto& blf_field_def : this->blf_field_defs) {
            if (blf_field_def.fd_meta.lvm_name == name) {
                if (!blf_field_def.fd_numeric_index) {
                    break;
                }
                retval = &this->lf_value_stats[blf_field_def.fd_numeric_index
                                                   .value()];
                break;
            }
        }

        return retval;
    }

    bool hide_field(const intern_string_t field_name, bool val) override
    {
        auto fd_iter
            = std::find_if(this->blf_field_defs.begin(),
                           this->blf_field_defs.end(),
                           [field_name](const field_def& elem) {
                               return elem.fd_meta.lvm_name == field_name;
                           });
        if (fd_iter == this->blf_field_defs.end()) {
            return false;
        }

        fd_iter->fd_meta.lvm_user_hidden = val;
        return true;
    }

    std::shared_ptr<log_format> specialized(int fmt_lock = -1) override
    {
        auto retval = std::make_shared<bro_log_format>(*this);

        retval->lf_specialized = true;
        return retval;
    }

    class bro_log_table : public log_format_vtab_impl {
    public:
        explicit bro_log_table(const bro_log_format& format)
            : log_format_vtab_impl(format), blt_format(format)
        {
        }

        void get_columns(std::vector<vtab_column>& cols) const override
        {
            for (const auto& fd : this->blt_format.blf_field_defs) {
                std::pair<int, unsigned int> type_pair
                    = log_vtab_impl::logline_value_to_sqlite_type(
                        fd.fd_meta.lvm_kind);

                cols.emplace_back(fd.fd_meta.lvm_name.to_string(),
                                  type_pair.first,
                                  fd.fd_collator,
                                  false,
                                  "",
                                  type_pair.second);
            }
        }

        void get_foreign_keys(
            std::vector<std::string>& keys_inout) const override
        {
            this->log_vtab_impl::get_foreign_keys(keys_inout);

            for (const auto& fd : this->blt_format.blf_field_defs) {
                if (fd.fd_meta.lvm_identifier) {
                    keys_inout.push_back(fd.fd_meta.lvm_name.to_string());
                }
            }
        }

        const bro_log_format& blt_format;
    };

    static std::map<intern_string_t, std::shared_ptr<bro_log_table>>&
    get_tables()
    {
        static std::map<intern_string_t, std::shared_ptr<bro_log_table>> retval;

        return retval;
    }

    std::shared_ptr<log_vtab_impl> get_vtab_impl() const override
    {
        if (this->blf_format_name.empty()) {
            return nullptr;
        }

        std::shared_ptr<bro_log_table> retval = nullptr;

        auto& tables = get_tables();
        auto iter = tables.find(this->blf_format_name);
        if (iter == tables.end()) {
            retval = std::make_shared<bro_log_table>(*this);
            tables[this->blf_format_name] = retval;
        }

        return retval;
    }

    void get_subline(const logline& ll,
                     shared_buffer_ref& sbr,
                     bool full_message) override
    {
    }

    intern_string_t blf_format_name;
    intern_string_t blf_separator;
    intern_string_t blf_set_separator;
    intern_string_t blf_empty_field;
    intern_string_t blf_unset_field;
    std::vector<field_def> blf_field_defs;
};

struct ws_separated_string {
    const char* ss_str;
    size_t ss_len;

    explicit ws_separated_string(const char* str = nullptr, size_t len = -1)
        : ss_str(str), ss_len(len)
    {
    }

    struct iterator {
        enum class state_t {
            NORMAL,
            QUOTED,
        };

        const ws_separated_string& i_parent;
        const char* i_pos;
        const char* i_next_pos;
        size_t i_index{0};
        state_t i_state{state_t::NORMAL};

        iterator(const ws_separated_string& ss, const char* pos)
            : i_parent(ss), i_pos(pos), i_next_pos(pos)
        {
            this->update();
        }

        void update()
        {
            const auto& ss = this->i_parent;
            bool done = false;

            while (!done && this->i_next_pos < (ss.ss_str + ss.ss_len)) {
                switch (this->i_state) {
                    case state_t::NORMAL:
                        if (*this->i_next_pos == '"') {
                            this->i_state = state_t::QUOTED;
                        } else if (isspace(*this->i_next_pos)) {
                            done = true;
                        }
                        break;
                    case state_t::QUOTED:
                        if (*this->i_next_pos == '"') {
                            this->i_state = state_t::NORMAL;
                        }
                        break;
                }
                if (!done) {
                    this->i_next_pos += 1;
                }
            }
        }

        iterator& operator++()
        {
            const auto& ss = this->i_parent;

            this->i_pos = this->i_next_pos;
            while (this->i_pos < (ss.ss_str + ss.ss_len)
                   && isspace(*this->i_pos))
            {
                this->i_pos += 1;
                this->i_next_pos += 1;
            }
            this->update();
            this->i_index += 1;

            return *this;
        }

        string_fragment operator*()
        {
            const auto& ss = this->i_parent;
            int end = this->i_next_pos - ss.ss_str;

            return string_fragment(ss.ss_str, this->i_pos - ss.ss_str, end);
        }

        bool operator==(const iterator& other) const
        {
            return (&this->i_parent == &other.i_parent)
                && (this->i_pos == other.i_pos);
        }

        bool operator!=(const iterator& other) const
        {
            return !(*this == other);
        }

        size_t index() const { return this->i_index; }
    };

    iterator begin() { return {*this, this->ss_str}; }

    iterator end() { return {*this, this->ss_str + this->ss_len}; }
};

class w3c_log_format : public log_format {
public:
    struct field_def {
        const intern_string_t fd_name;
        logline_value_meta fd_meta;
        std::string fd_collator;
        nonstd::optional<size_t> fd_numeric_index;

        explicit field_def(const intern_string_t name)
            : fd_name(name), fd_meta(intern_string::lookup(sql_safe_ident(
                                         name.to_string_fragment())),
                                     value_kind_t::VALUE_TEXT)
        {
        }

        field_def(const intern_string_t name, logline_value_meta meta)
            : fd_name(name), fd_meta(meta)
        {
        }

        field_def(int col,
                  const char* name,
                  value_kind_t kind,
                  bool ident = false,
                  std::string coll = "")
            : fd_name(intern_string::lookup(name)),
              fd_meta(
                  intern_string::lookup(sql_safe_ident(string_fragment(name))),
                  kind,
                  col),
              fd_collator(std::move(coll))
        {
            this->fd_meta.lvm_identifier = ident;
        }

        field_def& with_kind(value_kind_t kind,
                             bool identifier = false,
                             const std::string& collator = "")
        {
            this->fd_meta.lvm_kind = kind;
            this->fd_meta.lvm_identifier = identifier;
            this->fd_collator = collator;
            return *this;
        }

        field_def& with_numeric_index(int index)
        {
            this->fd_numeric_index = index;
            return *this;
        }
    };

    struct field_to_struct_t {
        field_to_struct_t(const char* prefix, const char* struct_name)
            : fs_prefix(prefix),
              fs_struct_name(intern_string::lookup(struct_name))
        {
        }

        const char* fs_prefix;
        intern_string_t fs_struct_name;
    };

    static const std::vector<field_def> KNOWN_FIELDS;
    const static std::vector<field_to_struct_t> KNOWN_STRUCT_FIELDS;

    w3c_log_format()
    {
        this->lf_is_self_describing = true;
        this->lf_time_ordered = false;
    }

    const intern_string_t get_name() const override
    {
        static const intern_string_t name(intern_string::lookup("w3c"));

        return this->wlf_format_name.empty() ? name : this->wlf_format_name;
    }

    void clear() override
    {
        this->log_format::clear();
        this->wlf_time_scanner.clear();
        this->wlf_format_name.clear();
        this->wlf_field_defs.clear();
    }

    scan_result_t scan_int(std::vector<logline>& dst,
                           const line_info& li,
                           shared_buffer_ref& sbr)
    {
        static const intern_string_t F_DATE = intern_string::lookup("date");
        static const intern_string_t F_DATE_LOCAL
            = intern_string::lookup("date-local");
        static const intern_string_t F_DATE_UTC
            = intern_string::lookup("date-UTC");
        static const intern_string_t F_TIME = intern_string::lookup("time");
        static const intern_string_t F_TIME_LOCAL
            = intern_string::lookup("time-local");
        static const intern_string_t F_TIME_UTC
            = intern_string::lookup("time-UTC");
        static const intern_string_t F_STATUS_CODE
            = intern_string::lookup("sc-status");

        ws_separated_string ss(sbr.get_data(), sbr.length());
        struct timeval date_tv {
            0, 0
        }, time_tv{0, 0};
        struct exttm date_tm, time_tm;
        bool found_date = false, found_time = false;
        log_level_t level = LEVEL_INFO;

        for (auto iter = ss.begin(); iter != ss.end(); ++iter) {
            if (iter.index() >= this->wlf_field_defs.size()) {
                level = LEVEL_INVALID;
                break;
            }

            const field_def& fd = this->wlf_field_defs[iter.index()];
            string_fragment sf = *iter;

            if (sf.startswith("#")) {
                if (sf == "#Date:") {
                    auto sbr_sf_opt
                        = sbr.to_string_fragment().consume_n(sf.length());

                    if (sbr_sf_opt) {
                        auto sbr_sf = sbr_sf_opt.value().trim();
                        date_time_scanner dts;
                        struct exttm tm;
                        struct timeval tv;

                        if (dts.scan(sbr_sf.data(),
                                     sbr_sf.length(),
                                     nullptr,
                                     &tm,
                                     tv))
                        {
                            this->lf_date_time.set_base_time(tv.tv_sec,
                                                             tm.et_tm);
                            this->wlf_time_scanner.set_base_time(tv.tv_sec,
                                                                 tm.et_tm);
                        }
                    }
                }
                dst.emplace_back(
                    li.li_file_range.fr_offset, 0, 0, LEVEL_IGNORE, 0);
                return SCAN_MATCH;
            }

            sf = sf.trim("\" \t");
            if (F_DATE == fd.fd_name || F_DATE_LOCAL == fd.fd_name
                || F_DATE_UTC == fd.fd_name)
            {
                if (this->lf_date_time.scan(
                        sf.data(), sf.length(), nullptr, &date_tm, date_tv))
                {
                    this->lf_timestamp_flags |= date_tm.et_flags;
                    found_date = true;
                }
            } else if (F_TIME == fd.fd_name || F_TIME_LOCAL == fd.fd_name
                       || F_TIME_UTC == fd.fd_name)
            {
                if (this->wlf_time_scanner.scan(
                        sf.data(), sf.length(), nullptr, &time_tm, time_tv))
                {
                    this->lf_timestamp_flags |= time_tm.et_flags;
                    found_time = true;
                }
            } else if (F_STATUS_CODE == fd.fd_name) {
                if (!sf.empty() && sf[0] >= '4') {
                    level = LEVEL_ERROR;
                }
            }

            if (fd.fd_numeric_index) {
                switch (fd.fd_meta.lvm_kind) {
                    case value_kind_t::VALUE_INTEGER:
                    case value_kind_t::VALUE_FLOAT: {
                        char field_copy[sf.length() + 1];
                        double val;

                        if (sscanf(sf.to_string(field_copy), "%lf", &val) == 1)
                        {
                            this->lf_value_stats[fd.fd_numeric_index.value()]
                                .add_value(val);
                        }
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        if (found_time) {
            struct exttm tm = time_tm;
            struct timeval tv;

            if (found_date) {
                tm.et_tm.tm_year = date_tm.et_tm.tm_year;
                tm.et_tm.tm_mday = date_tm.et_tm.tm_mday;
                tm.et_tm.tm_mon = date_tm.et_tm.tm_mon;
                tm.et_tm.tm_wday = date_tm.et_tm.tm_wday;
                tm.et_tm.tm_yday = date_tm.et_tm.tm_yday;
            }

            tv.tv_sec = tm2sec(&tm.et_tm);
            tv.tv_usec = tm.et_nsec / 1000;

            if (!this->lf_specialized) {
                for (auto& ll : dst) {
                    ll.set_ignore(true);
                }
            }
            dst.emplace_back(li.li_file_range.fr_offset, tv, level, 0);
            return SCAN_MATCH;
        }

        return SCAN_NO_MATCH;
    }

    scan_result_t scan(logfile& lf,
                       std::vector<logline>& dst,
                       const line_info& li,
                       shared_buffer_ref& sbr,
                       scan_batch_context& sbc) override
    {
        static const auto* W3C_LOG_NAME = intern_string::lookup("w3c_log");
        static const auto* X_FIELDS_NAME = intern_string::lookup("x_fields");
        static auto X_FIELDS_IDX = 0;

        if (li.li_partial) {
            return SCAN_INCOMPLETE;
        }

        if (!this->wlf_format_name.empty()) {
            return this->scan_int(dst, li, sbr);
        }

        if (dst.empty() || dst.size() > 20 || sbr.empty()
            || sbr.get_data()[0] == '#')
        {
            return SCAN_NO_MATCH;
        }

        this->clear();

        for (auto line_iter = dst.begin(); line_iter != dst.end(); ++line_iter)
        {
            auto next_read_result = lf.read_line(line_iter);

            if (next_read_result.isErr()) {
                return SCAN_NO_MATCH;
            }

            auto line = next_read_result.unwrap();
            ws_separated_string ss(line.get_data(), line.length());
            auto iter = ss.begin();

            string_fragment directive = *iter;

            if (directive.empty() || directive[0] != '#') {
                continue;
            }

            ++iter;
            if (iter == ss.end()) {
                continue;
            }

            if (directive == "#Date:") {
                date_time_scanner dts;
                struct exttm tm;
                struct timeval tv;

                if (dts.scan(line.get_data_at(directive.length() + 1),
                             line.length() - directive.length() - 1,
                             nullptr,
                             &tm,
                             tv))
                {
                    this->lf_date_time.set_base_time(tv.tv_sec, tm.et_tm);
                    this->wlf_time_scanner.set_base_time(tv.tv_sec, tm.et_tm);
                }
            } else if (directive == "#Fields:" && this->wlf_field_defs.empty())
            {
                int numeric_count = 0;

                do {
                    auto sf = (*iter).trim(")");

                    auto field_iter = std::find_if(
                        begin(KNOWN_FIELDS),
                        end(KNOWN_FIELDS),
                        [&sf](auto elem) { return sf == elem.fd_name; });
                    if (field_iter != end(KNOWN_FIELDS)) {
                        this->wlf_field_defs.emplace_back(*field_iter);
                    } else if (sf == "date" || sf == "time") {
                        this->wlf_field_defs.emplace_back(
                            intern_string::lookup(sf));
                    } else {
                        const auto fs_iter = std::find_if(
                            begin(KNOWN_STRUCT_FIELDS),
                            end(KNOWN_STRUCT_FIELDS),
                            [&sf](auto elem) {
                                return sf.startswith(elem.fs_prefix);
                            });
                        if (fs_iter != end(KNOWN_STRUCT_FIELDS)) {
                            auto field_name
                                = intern_string::lookup(sf.substr(3));
                            this->wlf_field_defs.emplace_back(
                                field_name,
                                logline_value_meta(
                                    field_name,
                                    value_kind_t::VALUE_TEXT,
                                    KNOWN_FIELDS.size() + 1
                                        + std::distance(
                                            begin(KNOWN_STRUCT_FIELDS),
                                            fs_iter),
                                    this)
                                    .with_struct_name(fs_iter->fs_struct_name));
                        } else {
                            auto field_name = intern_string::lookup(sf);
                            this->wlf_field_defs.emplace_back(
                                field_name,
                                logline_value_meta(
                                    field_name,
                                    value_kind_t::VALUE_TEXT,
                                    KNOWN_FIELDS.size() + X_FIELDS_IDX,
                                    this)
                                    .with_struct_name(X_FIELDS_NAME));
                        }
                    }
                    auto& fd = this->wlf_field_defs.back();
                    fd.fd_meta.lvm_format = nonstd::make_optional(this);
                    switch (fd.fd_meta.lvm_kind) {
                        case value_kind_t::VALUE_FLOAT:
                        case value_kind_t::VALUE_INTEGER:
                            fd.with_numeric_index(numeric_count);
                            numeric_count += 1;
                            break;
                        default:
                            break;
                    }

                    ++iter;
                } while (iter != ss.end());

                this->wlf_format_name = W3C_LOG_NAME;
                this->lf_value_stats.resize(numeric_count);
            }
        }

        if (!this->wlf_format_name.empty() && !this->wlf_field_defs.empty()) {
            return this->scan_int(dst, li, sbr);
        }

        this->wlf_format_name.clear();
        this->lf_value_stats.clear();

        return SCAN_NO_MATCH;
    }

    void annotate(uint64_t line_number,
                  string_attrs_t& sa,
                  logline_value_vector& values,
                  bool annotate_module) const override
    {
        auto& sbr = values.lvv_sbr;
        ws_separated_string ss(sbr.get_data(), sbr.length());

        for (auto iter = ss.begin(); iter != ss.end(); ++iter) {
            string_fragment sf = *iter;

            if (iter.index() >= this->wlf_field_defs.size()) {
                sa.emplace_back(line_range{sf.sf_begin, -1},
                                SA_INVALID.value("extra fields detected"));
                return;
            }

            const field_def& fd = this->wlf_field_defs[iter.index()];

            if (sf == "-") {
                sf.invalidate();
            }

            auto lr = line_range(sf.sf_begin, sf.sf_end);

            if (lr.is_valid()) {
                values.lvv_values.emplace_back(fd.fd_meta, sbr, lr);
                if (sf.startswith("\"")) {
                    auto& meta = values.lvv_values.back().lv_meta;

                    if (meta.lvm_kind == value_kind_t::VALUE_TEXT) {
                        meta.lvm_kind = value_kind_t::VALUE_W3C_QUOTED;
                    } else {
                        meta.lvm_kind = value_kind_t::VALUE_NULL;
                    }
                }
            } else {
                values.lvv_values.emplace_back(fd.fd_meta);
            }
        }
    }

    const logline_value_stats* stats_for_value(
        const intern_string_t& name) const override
    {
        const logline_value_stats* retval = nullptr;

        for (const auto& wlf_field_def : this->wlf_field_defs) {
            if (wlf_field_def.fd_meta.lvm_name == name) {
                if (!wlf_field_def.fd_numeric_index) {
                    break;
                }
                retval = &this->lf_value_stats[wlf_field_def.fd_numeric_index
                                                   .value()];
                break;
            }
        }

        return retval;
    }

    bool hide_field(const intern_string_t field_name, bool val) override
    {
        auto fd_iter
            = std::find_if(this->wlf_field_defs.begin(),
                           this->wlf_field_defs.end(),
                           [field_name](const field_def& elem) {
                               return elem.fd_meta.lvm_name == field_name;
                           });
        if (fd_iter == this->wlf_field_defs.end()) {
            return false;
        }

        fd_iter->fd_meta.lvm_user_hidden = val;
        return true;
    }

    std::shared_ptr<log_format> specialized(int fmt_lock = -1) override
    {
        auto retval = std::make_shared<w3c_log_format>(*this);

        retval->lf_specialized = true;
        return retval;
    }

    class w3c_log_table : public log_format_vtab_impl {
    public:
        explicit w3c_log_table(const w3c_log_format& format)
            : log_format_vtab_impl(format), wlt_format(format)
        {
        }

        void get_columns(std::vector<vtab_column>& cols) const override
        {
            for (const auto& fd : KNOWN_FIELDS) {
                auto type_pair = log_vtab_impl::logline_value_to_sqlite_type(
                    fd.fd_meta.lvm_kind);

                cols.emplace_back(fd.fd_meta.lvm_name.to_string(),
                                  type_pair.first,
                                  fd.fd_collator,
                                  false,
                                  "",
                                  type_pair.second);
            }
            cols.emplace_back("x_fields");
            cols.back().with_comment(
                "A JSON-object that contains fields that are not first-class "
                "columns");
            for (const auto& fs : KNOWN_STRUCT_FIELDS) {
                cols.emplace_back(fs.fs_struct_name.to_string());
            }
        };

        void get_foreign_keys(
            std::vector<std::string>& keys_inout) const override
        {
            this->log_vtab_impl::get_foreign_keys(keys_inout);

            for (const auto& fd : KNOWN_FIELDS) {
                if (fd.fd_meta.lvm_identifier) {
                    keys_inout.push_back(fd.fd_meta.lvm_name.to_string());
                }
            }
        }

        const w3c_log_format& wlt_format;
    };

    static std::map<intern_string_t, std::shared_ptr<w3c_log_table>>&
    get_tables()
    {
        static std::map<intern_string_t, std::shared_ptr<w3c_log_table>> retval;

        return retval;
    }

    std::shared_ptr<log_vtab_impl> get_vtab_impl() const override
    {
        if (this->wlf_format_name.empty()) {
            return nullptr;
        }

        std::shared_ptr<w3c_log_table> retval = nullptr;

        auto& tables = get_tables();
        auto iter = tables.find(this->wlf_format_name);
        if (iter == tables.end()) {
            retval = std::make_shared<w3c_log_table>(*this);
            tables[this->wlf_format_name] = retval;
        }

        return retval;
    }

    void get_subline(const logline& ll,
                     shared_buffer_ref& sbr,
                     bool full_message) override
    {
    }

    date_time_scanner wlf_time_scanner;
    intern_string_t wlf_format_name;
    std::vector<field_def> wlf_field_defs;
};

static int KNOWN_FIELD_INDEX = 0;
const std::vector<w3c_log_format::field_def> w3c_log_format::KNOWN_FIELDS = {
    {
        KNOWN_FIELD_INDEX++,
        "cs-method",
        value_kind_t::VALUE_TEXT,
        true,
    },
    {
        KNOWN_FIELD_INDEX++,
        "c-ip",
        value_kind_t::VALUE_TEXT,
        true,
        "ipaddress",
    },
    {
        KNOWN_FIELD_INDEX++,
        "cs-bytes",
        value_kind_t::VALUE_INTEGER,
        false,
    },
    {
        KNOWN_FIELD_INDEX++,
        "cs-host",
        value_kind_t::VALUE_TEXT,
        true,
    },
    {
        KNOWN_FIELD_INDEX++,
        "cs-uri-stem",
        value_kind_t::VALUE_TEXT,
        true,
        "naturalnocase",
    },
    {
        KNOWN_FIELD_INDEX++,
        "cs-uri-query",
        value_kind_t::VALUE_TEXT,
        false,
    },
    {
        KNOWN_FIELD_INDEX++,
        "cs-username",
        value_kind_t::VALUE_TEXT,
        false,
    },
    {
        KNOWN_FIELD_INDEX++,
        "cs-version",
        value_kind_t::VALUE_TEXT,
        true,
    },
    {
        KNOWN_FIELD_INDEX++,
        "s-ip",
        value_kind_t::VALUE_TEXT,
        true,
        "ipaddress",
    },
    {
        KNOWN_FIELD_INDEX++,
        "s-port",
        value_kind_t::VALUE_INTEGER,
        true,
    },
    {
        KNOWN_FIELD_INDEX++,
        "s-computername",
        value_kind_t::VALUE_TEXT,
        true,
    },
    {
        KNOWN_FIELD_INDEX++,
        "s-sitename",
        value_kind_t::VALUE_TEXT,
        true,
    },
    {
        KNOWN_FIELD_INDEX++,
        "sc-bytes",
        value_kind_t::VALUE_INTEGER,
        false,
    },
    {
        KNOWN_FIELD_INDEX++,
        "sc-status",
        value_kind_t::VALUE_INTEGER,
        false,
    },
    {
        KNOWN_FIELD_INDEX++,
        "sc-substatus",
        value_kind_t::VALUE_INTEGER,
        false,
    },
    {
        KNOWN_FIELD_INDEX++,
        "time-taken",
        value_kind_t::VALUE_FLOAT,
        false,
    },
};

const std::vector<w3c_log_format::field_to_struct_t>
    w3c_log_format::KNOWN_STRUCT_FIELDS = {
        {"cs(", "cs_headers"},
        {"sc(", "sc_headers"},
        {"rs(", "rs_headers"},
        {"sr(", "sr_headers"},
};

struct logfmt_pair_handler {
    explicit logfmt_pair_handler(date_time_scanner& dts) : lph_dt_scanner(dts)
    {
    }

    bool process_value(const string_fragment& value_frag)
    {
        if (this->lph_key_frag == "time" || this->lph_key_frag == "ts") {
            if (!this->lph_dt_scanner.scan(value_frag.data(),
                                           value_frag.length(),
                                           nullptr,
                                           &this->lph_time_tm,
                                           this->lph_tv))
            {
                return false;
            }
            this->lph_found_time = true;
        } else if (this->lph_key_frag == "level") {
            this->lph_level
                = string2level(value_frag.data(), value_frag.length());
        }
        return true;
    }

    date_time_scanner& lph_dt_scanner;
    bool lph_found_time{false};
    struct exttm lph_time_tm {};
    struct timeval lph_tv {
        0, 0
    };
    log_level_t lph_level{log_level_t::LEVEL_INFO};
    string_fragment lph_key_frag{""};
};

class logfmt_format : public log_format {
public:
    const intern_string_t get_name() const override
    {
        const static auto NAME = intern_string::lookup("logfmt_log");

        return NAME;
    }

    class logfmt_log_table : public log_format_vtab_impl {
    public:
        logfmt_log_table(const log_format& format)
            : log_format_vtab_impl(format)
        {
        }

        void get_columns(std::vector<vtab_column>& cols) const override
        {
            static const auto FIELDS = std::string("fields");

            cols.emplace_back(FIELDS);
        }
    };

    std::shared_ptr<log_vtab_impl> get_vtab_impl() const override
    {
        static auto retval = std::make_shared<logfmt_log_table>(*this);

        return retval;
    }

    scan_result_t scan(logfile& lf,
                       std::vector<logline>& dst,
                       const line_info& li,
                       shared_buffer_ref& sbr,
                       scan_batch_context& sbc) override
    {
        auto p = logfmt::parser(sbr.to_string_fragment());
        scan_result_t retval = scan_result_t::SCAN_NO_MATCH;
        bool done = false;
        logfmt_pair_handler lph(this->lf_date_time);

        while (!done) {
            auto parse_result = p.step();

            done = parse_result.match(
                [](const logfmt::parser::end_of_input&) { return true; },
                [&lph](const logfmt::parser::kvpair& kvp) {
                    lph.lph_key_frag = kvp.first;

                    return kvp.second.match(
                        [](const logfmt::parser::bool_value& bv) {
                            return false;
                        },
                        [&lph](const logfmt::parser::float_value& fv) {
                            return lph.process_value(fv.fv_str_value);
                        },
                        [&lph](const logfmt::parser::int_value& iv) {
                            return lph.process_value(iv.iv_str_value);
                        },
                        [&lph](const logfmt::parser::quoted_value& qv) {
                            auto_mem<yajl_handle_t> handle(yajl_free);
                            yajl_callbacks cb;

                            memset(&cb, 0, sizeof(cb));
                            handle = yajl_alloc(&cb, nullptr, &lph);
                            cb.yajl_string = +[](void* ctx,
                                                 const unsigned char* str,
                                                 size_t len) -> int {
                                auto& lph = *((logfmt_pair_handler*) ctx);
                                string_fragment value_frag{str, 0, (int) len};

                                return lph.process_value(value_frag);
                            };

                            if (yajl_parse(
                                    handle,
                                    (const unsigned char*) qv.qv_value.data(),
                                    qv.qv_value.length())
                                    != yajl_status_ok
                                || yajl_complete_parse(handle)
                                    != yajl_status_ok)
                            {
                                log_debug("json parsing failed");
                                string_fragment unq_frag{
                                    qv.qv_value.sf_string,
                                    qv.qv_value.sf_begin + 1,
                                    qv.qv_value.sf_end - 1,
                                };

                                return lph.process_value(unq_frag);
                            }

                            return false;
                        },
                        [&lph](const logfmt::parser::unquoted_value& uv) {
                            return lph.process_value(uv.uv_value);
                        });
                },
                [](const logfmt::parser::error& err) {
                    // log_error("logfmt parse error: %s", err.e_msg.c_str());
                    return true;
                });
        }

        if (lph.lph_found_time) {
            dst.emplace_back(
                li.li_file_range.fr_offset, lph.lph_tv, lph.lph_level);
            retval = scan_result_t::SCAN_MATCH;
        }

        return retval;
    }

    void annotate(uint64_t line_number,
                  string_attrs_t& sa,
                  logline_value_vector& values,
                  bool annotate_module) const override
    {
        static const auto FIELDS_NAME = intern_string::lookup("fields");

        auto& sbr = values.lvv_sbr;
        auto p = logfmt::parser(sbr.to_string_fragment());
        bool done = false;

        while (!done) {
            auto parse_result = p.step();

            done = parse_result.match(
                [](const logfmt::parser::end_of_input&) { return true; },
                [this, &sa, &values](const logfmt::parser::kvpair& kvp) {
                    auto value_frag = kvp.second.match(
                        [this, &kvp, &values](
                            const logfmt::parser::bool_value& bv) {
                            auto lvm = logline_value_meta{intern_string::lookup(
                                                              kvp.first),
                                                          value_kind_t::
                                                              VALUE_INTEGER,
                                                          0,
                                                          (log_format*) this}
                                           .with_struct_name(FIELDS_NAME);
                            values.lvv_values.emplace_back(lvm, bv.bv_value);

                            return bv.bv_str_value;
                        },
                        [this, &kvp, &values](
                            const logfmt::parser::int_value& iv) {
                            auto lvm = logline_value_meta{intern_string::lookup(
                                                              kvp.first),
                                                          value_kind_t::
                                                              VALUE_INTEGER,
                                                          0,
                                                          (log_format*) this}
                                           .with_struct_name(FIELDS_NAME);
                            values.lvv_values.emplace_back(lvm, iv.iv_value);

                            return iv.iv_str_value;
                        },
                        [this, &kvp, &values](
                            const logfmt::parser::float_value& fv) {
                            auto lvm = logline_value_meta{intern_string::lookup(
                                                              kvp.first),
                                                          value_kind_t::
                                                              VALUE_INTEGER,
                                                          0,
                                                          (log_format*) this}
                                           .with_struct_name(FIELDS_NAME);
                            values.lvv_values.emplace_back(lvm, fv.fv_value);

                            return fv.fv_str_value;
                        },
                        [](const logfmt::parser::quoted_value& qv) {
                            return qv.qv_value;
                        },
                        [](const logfmt::parser::unquoted_value& uv) {
                            return uv.uv_value;
                        });
                    auto value_lr
                        = line_range{value_frag.sf_begin, value_frag.sf_end};

                    if (kvp.first == "time" || kvp.first == "ts") {
                        sa.emplace_back(value_lr, logline::L_TIMESTAMP.value());
                    } else if (kvp.first == "level") {
                    } else if (kvp.first == "msg") {
                        sa.emplace_back(value_lr, SA_BODY.value());
                    } else if (!kvp.second.is<logfmt::parser::int_value>()
                               && !kvp.second.is<logfmt::parser::bool_value>())
                    {
                        auto lvm
                            = logline_value_meta{intern_string::lookup(
                                                     kvp.first),
                                                 value_frag.startswith("\"")
                                                     ? value_kind_t::VALUE_JSON
                                                     : value_kind_t::VALUE_TEXT,
                                                 0,
                                                 (log_format*) this}
                                  .with_struct_name(FIELDS_NAME);
                        values.lvv_values.emplace_back(lvm, value_frag);
                    }

                    return false;
                },
                [line_number, &sbr](const logfmt::parser::error& err) {
                    log_error("bad line %.*s", sbr.length(), sbr.get_data());
                    log_error("%lld:logfmt parse error: %s",
                              line_number,
                              err.e_msg.c_str());
                    return true;
                });
        }
    }

    std::shared_ptr<log_format> specialized(int fmt_lock) override
    {
        auto retval = std::make_shared<logfmt_format>(*this);

        retval->lf_specialized = true;
        return retval;
    }
};

static auto format_binder = injector::bind_multiple<log_format>()
                                .add<logfmt_format>()
                                .add<bro_log_format>()
                                .add<w3c_log_format>()
                                .add<generic_log_format>();
