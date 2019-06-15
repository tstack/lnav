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
 * @file log_format.hh
 */

#ifndef __log_format_hh
#define __log_format_hh

#include <time.h>
#include <sys/time.h>
#include <stdint.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <sys/types.h>

#include <set>
#include <list>
#include <string>
#include <vector>
#include <limits>
#include <memory>
#include <sstream>

#include "optional.hpp"
#include "pcrepp/pcrepp.hh"
#include "yajlpp/yajlpp.hh"
#include "base/lnav_log.hh"
#include "lnav_util.hh"
#include "byte_array.hh"
#include "view_curses.hh"
#include "intern_string.hh"
#include "shared_buffer.hh"
#include "highlighter.hh"
#include "log_level.hh"

struct sqlite3;
class logfile;
class log_format;
class log_vtab_manager;
struct exec_context;

/**
 * Metadata for a single line in a log file.
 */
class logline {
public:
    static string_attr_type L_PREFIX;
    static string_attr_type L_TIMESTAMP;
    static string_attr_type L_FILE;
    static string_attr_type L_PARTITION;
    static string_attr_type L_MODULE;
    static string_attr_type L_OPID;
    static string_attr_type L_META;

    /**
     * Construct a logline object with the given values.
     *
     * @param off The offset of the line in the file.
     * @param t The timestamp for the line.
     * @param millis The millisecond timestamp for the line.
     * @param l The logging level.
     */
    logline(off_t off,
            time_t t,
            uint16_t millis,
            log_level_t l,
            uint8_t mod = 0,
            uint8_t opid = 0)
        : ll_offset(off),
          ll_time(t),
          ll_millis(millis),
          ll_opid(opid),
          ll_sub_offset(0),
          ll_valid_utf(1),
          ll_level(l),
          ll_module_id(mod)
    {
        memset(this->ll_schema, 0, sizeof(this->ll_schema));
    };

    logline(off_t off,
            const struct timeval &tv,
            log_level_t l,
            uint8_t mod = 0,
            uint8_t opid = 0)
        : ll_offset(off),
          ll_opid(opid),
          ll_sub_offset(0),
          ll_valid_utf(1),
          ll_level(l),
          ll_module_id(mod)
    {
        this->set_time(tv);
        memset(this->ll_schema, 0, sizeof(this->ll_schema));
    };

    /** @return The offset of the line in the file. */
    off_t get_offset() const { return this->ll_offset; };

    uint16_t get_sub_offset() const { return this->ll_sub_offset; };

    void set_sub_offset(uint16_t suboff) { this->ll_sub_offset = suboff; };

    /** @return The timestamp for the line. */
    time_t get_time() const { return this->ll_time; };

    void to_exttm(struct exttm &tm_out) const {
        tm_out.et_tm = *gmtime(&this->ll_time);
        tm_out.et_nsec = this->ll_millis * 1000 * 1000;
    };

    void set_time(time_t t) { this->ll_time = t; };

    /** @return The millisecond timestamp for the line. */
    uint16_t get_millis() const { return this->ll_millis; };

    void set_millis(uint16_t m) { this->ll_millis = m; };

    uint64_t get_time_in_millis() const {
        return (this->ll_time * 1000ULL + (uint64_t) this->ll_millis);
    };

    struct timeval get_timeval() const {
        struct timeval retval = { this->ll_time, this->ll_millis * 1000 };

        return retval;
    };

    void set_time(const struct timeval &tv) {
        this->ll_time = tv.tv_sec;
        this->ll_millis = tv.tv_usec / 1000;
    };

    void set_mark(bool val) {
        if (val) {
            this->ll_level |= LEVEL_MARK;
        }
        else {
            this->ll_level &= ~LEVEL_MARK;
        }
    };

    bool is_marked(void) const { return this->ll_level & LEVEL_MARK; };

    void set_time_skew(bool val) {
        if (val) {
            this->ll_level |= LEVEL_TIME_SKEW;
        }
        else {
            this->ll_level &= ~LEVEL_TIME_SKEW;
        }
    };

    bool is_time_skewed() const {
        return this->ll_level & LEVEL_TIME_SKEW;
    };

    void set_valid_utf(bool v) {
        this->ll_valid_utf = v;
    }

    bool is_valid_utf() const {
        return this->ll_valid_utf;
    }

    /** @param l The logging level. */
    void set_level(log_level_t l) { this->ll_level = l; };

    /** @return The logging level. */
    log_level_t get_level_and_flags() const {
        return (log_level_t)this->ll_level;
    };

    log_level_t get_msg_level() const {
        return (log_level_t)(this->ll_level & ~LEVEL__FLAGS);
    };

    const char *get_level_name() const
    {
        return level_names[this->ll_level & ~LEVEL__FLAGS];
    };

    bool is_continued() const {
        return this->ll_level & LEVEL_CONTINUED;
    };

    uint8_t get_module_id() const {
        return this->ll_module_id;
    };

    void set_opid(uint8_t opid) {
        this->ll_opid = opid;
    };

    uint8_t get_opid() const {
        return this->ll_opid;
    };

    /**
     * @return  True if there is a schema value set for this log line.
     */
    bool has_schema(void) const
    {
        return (this->ll_schema[0] != 0 ||
                this->ll_schema[1] != 0);
    };

    /**
     * Set the "schema" for this log line.  The schema ID is used to match log
     * lines that have a similar format when generating the logline table.  The
     * schema is set lazily so that startup is faster.
     *
     * @param ba The SHA-1 hash of the constant parts of this log line.
     */
    void set_schema(const byte_array<2, uint64_t> &ba)
    {
        memcpy(this->ll_schema, ba.in(), sizeof(this->ll_schema));
    };

    char get_schema() const {
        return this->ll_schema[0];
    };

    /**
     * Perform a partial match of the given schema against this log line.
     * Storing the full schema is not practical, so we just keep the first four
     * bytes.
     *
     * @param  ba The SHA-1 hash of the constant parts of a log line.
     * @return    True if the first four bytes of the given schema match the
     *   schema stored in this log line.
     */
    bool match_schema(const byte_array<2, uint64_t> &ba) const
    {
        return memcmp(this->ll_schema, ba.in(), sizeof(this->ll_schema)) == 0;
    }

    /**
     * Compare loglines based on their timestamp.
     */
    bool operator<(const logline &rhs) const
    {
        return (this->ll_time < rhs.ll_time) ||
               (this->ll_time == rhs.ll_time &&
                this->ll_millis < rhs.ll_millis) ||
               (this->ll_time == rhs.ll_time &&
                this->ll_millis == rhs.ll_millis &&
                this->ll_offset < rhs.ll_offset) ||
               (this->ll_time == rhs.ll_time &&
                this->ll_millis == rhs.ll_millis &&
                this->ll_offset == rhs.ll_offset &&
                this->ll_sub_offset < rhs.ll_sub_offset);
    };

    bool operator<(const time_t &rhs) const { return this->ll_time < rhs; };

    bool operator<(const struct timeval &rhs) const {
        return ((this->ll_time < rhs.tv_sec) ||
                ((this->ll_time == rhs.tv_sec) &&
                 (this->ll_millis < (rhs.tv_usec / 1000))));
    };

    bool operator<=(const struct timeval &rhs) const {
        return ((this->ll_time < rhs.tv_sec) ||
                ((this->ll_time == rhs.tv_sec) &&
                 (this->ll_millis <= (rhs.tv_usec / 1000))));
    };
private:
    off_t    ll_offset;
    time_t   ll_time;
    unsigned int ll_millis : 10;
    unsigned int ll_opid : 6;
    unsigned int ll_sub_offset : 15;
    unsigned int ll_valid_utf : 1;
    uint8_t  ll_level;
    uint8_t  ll_module_id;
    char     ll_schema[2];
};

enum class scale_op_t {
    SO_IDENTITY,
    SO_MULTIPLY,
    SO_DIVIDE
};

struct scaling_factor {
    scaling_factor() : sf_op(scale_op_t::SO_IDENTITY), sf_value(1) { };

    template<typename T>
    void scale(T &val) const {
        switch (this->sf_op) {
        case scale_op_t::SO_IDENTITY:
            break;
        case scale_op_t::SO_DIVIDE:
            val = val / (T)this->sf_value;
            break;
        case scale_op_t::SO_MULTIPLY:
            val = val * (T)this->sf_value;
            break;
        }
    }

    scale_op_t sf_op;
    double sf_value;
};

class logline_value {
public:
    enum kind_t {
        VALUE_UNKNOWN = -1,
        VALUE_NULL,
        VALUE_TEXT,
        VALUE_INTEGER,
        VALUE_FLOAT,
        VALUE_BOOLEAN,
        VALUE_JSON,
        VALUE_STRUCT,
        VALUE_QUOTED,
        VALUE_TIMESTAMP,

        VALUE__MAX
    };

    logline_value(const intern_string_t name)
        : lv_name(name), lv_kind(VALUE_NULL), lv_identifier(), lv_column(-1),
          lv_hidden(false), lv_sub_offset(0), lv_from_module(false), lv_format(NULL) { };
    logline_value(const intern_string_t name, bool b)
        : lv_name(name),
          lv_kind(VALUE_BOOLEAN),
          lv_value((int64_t)(b ? 1 : 0)),
          lv_identifier(),
          lv_column(-1),
          lv_hidden(false), lv_sub_offset(0),
          lv_from_module(false), lv_format(NULL) { };
    logline_value(const intern_string_t name, int64_t i)
        : lv_name(name), lv_kind(VALUE_INTEGER), lv_value(i), lv_identifier(), lv_column(-1),
          lv_hidden(false), lv_sub_offset(0), lv_from_module(false), lv_format(NULL) { };
    logline_value(const intern_string_t name, double i)
        : lv_name(name), lv_kind(VALUE_FLOAT), lv_value(i), lv_identifier(), lv_column(-1),
          lv_hidden(false), lv_sub_offset(0), lv_from_module(false), lv_format(NULL) { };
    logline_value(const intern_string_t name, shared_buffer_ref &sbr, int column = -1)
        : lv_name(name), lv_kind(VALUE_TEXT), lv_sbr(sbr),
          lv_identifier(), lv_column(column),
          lv_hidden(false), lv_sub_offset(0), lv_from_module(false), lv_format(NULL) {
    };
    logline_value(const intern_string_t name, const intern_string_t val, int column = -1)
            : lv_name(name), lv_kind(VALUE_TEXT), lv_intern_string(val), lv_identifier(),
              lv_column(column), lv_hidden(false), lv_sub_offset(0), lv_from_module(false), lv_format(NULL) {

    };
    logline_value(const intern_string_t name, kind_t kind, shared_buffer_ref &sbr,
                  bool ident=false, const scaling_factor *scaling=NULL,
                  int col=-1, int start=-1, int end=-1, bool from_module=false,
                  const log_format *format=NULL)
        : lv_name(name), lv_kind(kind),
          lv_identifier(ident), lv_column(col), lv_hidden(false), lv_sub_offset(0),
          lv_origin(start, end),
          lv_from_module(from_module),
          lv_format(format)
    {
        if (sbr.get_data() == NULL) {
            this->lv_kind = kind = VALUE_NULL;
        }

        switch (kind) {
        case VALUE_JSON:
        case VALUE_STRUCT:
        case VALUE_TEXT:
        case VALUE_QUOTED:
        case VALUE_TIMESTAMP:
            this->lv_sbr = sbr;
            break;

        case VALUE_NULL:
            break;

        case VALUE_INTEGER:
            strtonum(this->lv_value.i, sbr.get_data(), sbr.length());
            if (scaling != NULL) {
                scaling->scale(this->lv_value.i);
            }
            break;

        case VALUE_FLOAT: {
            char scan_value[sbr.length() + 1];

            memcpy(scan_value, sbr.get_data(), sbr.length());
            scan_value[sbr.length()] = '\0';
            this->lv_value.d = strtod(scan_value, NULL);
            if (scaling != NULL) {
                scaling->scale(this->lv_value.d);
            }
            break;
        }

        case VALUE_BOOLEAN:
            if (strncmp(sbr.get_data(), "true", sbr.length()) == 0 ||
                strncmp(sbr.get_data(), "yes", sbr.length()) == 0) {
                this->lv_value.i = 1;
            }
            else {
                this->lv_value.i = 0;
            }
            break;

        case VALUE_UNKNOWN:
        case VALUE__MAX:
            ensure(0);
            break;
        }
    };

    const std::string to_string() const
    {
        char buffer[128];

        switch (this->lv_kind) {
        case VALUE_NULL:
            return "null";

        case VALUE_JSON:
        case VALUE_STRUCT:
        case VALUE_TEXT:
        case VALUE_TIMESTAMP:
            if (this->lv_sbr.empty()) {
                return this->lv_intern_string.to_string();
            }
            return std::string(this->lv_sbr.get_data(), this->lv_sbr.length());

        case VALUE_QUOTED:
            if (this->lv_sbr.length() == 0) {
                return "";
            } else {
                switch (this->lv_sbr.get_data()[0]) {
                case '\'':
                case '"': {
                    char unquoted_str[this->lv_sbr.length()];
                    size_t unquoted_len;

                    unquoted_len = unquote(unquoted_str, this->lv_sbr.get_data(),
                        this->lv_sbr.length());
                    return std::string(unquoted_str, unquoted_len);
                }
                default:
                    return std::string(this->lv_sbr.get_data(), this->lv_sbr.length());
                }
            }

        case VALUE_INTEGER:
            snprintf(buffer, sizeof(buffer), "%" PRId64, this->lv_value.i);
            break;

        case VALUE_FLOAT:
            snprintf(buffer, sizeof(buffer), "%lf", this->lv_value.d);
            break;

        case VALUE_BOOLEAN:
            if (this->lv_value.i) {
                return "true";
            }
            else {
                return "false";
            }
            break;
        case VALUE_UNKNOWN:
        case VALUE__MAX:
            ensure(0);
            break;
        }

        return std::string(buffer);
    };

    const char *text_value() const {
        if (this->lv_sbr.empty()) {
            if (this->lv_intern_string.empty()) {
                return "";
            }
            return this->lv_intern_string.get();
        }
        return this->lv_sbr.get_data();
    };

    const size_t text_length() const {
        if (this->lv_sbr.empty()) {
            return this->lv_intern_string.size();
        }
        return this->lv_sbr.length();
    }

    struct line_range origin_in_full_msg(const char *msg, size_t len) const;;

    intern_string_t lv_name;
    kind_t      lv_kind;
    union value_u {
        int64_t i;
        double  d;

        value_u() : i(0) { };
        value_u(int64_t i) : i(i) { };
        value_u(double d) : d(d) { };
    } lv_value;
    shared_buffer_ref lv_sbr;
    intern_string_t lv_intern_string;
    bool lv_identifier;
    int lv_column;
    bool lv_hidden;
    bool lv_user_hidden;
    int lv_sub_offset;
    struct line_range lv_origin;
    bool lv_from_module;
    const log_format *lv_format;
};

struct logline_value_stats {

    logline_value_stats() {
        this->clear();
    };

    void clear() {
        this->lvs_count = 0;
        this->lvs_total = 0;
        this->lvs_min_value = std::numeric_limits<double>::max();
        this->lvs_max_value = -std::numeric_limits<double>::max();
    };

    void merge(const logline_value_stats &other) {
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
    };

    void add_value(double value) {
        if (value < this->lvs_min_value) {
            this->lvs_min_value = value;
        }
        if (value > this->lvs_max_value) {
            this->lvs_max_value = value;
        }
        this->lvs_count += 1;
        this->lvs_total += value;
    };

    int64_t lvs_count;
    double lvs_total;
    double lvs_min_value;
    double lvs_max_value;
};

struct logline_value_cmp {
    logline_value_cmp(const intern_string_t *name = NULL, int col = -1)
        : lvc_name(name), lvc_column(col) {

    };

    bool operator()(const logline_value &lv) {
        bool retval = true;

        if (this->lvc_name != NULL) {
            retval = retval && ((*this->lvc_name) == lv.lv_name);
        }
        if (this->lvc_column != -1) {
            retval = retval && (this->lvc_column == lv.lv_column);
        }

        return retval;
    };

    const intern_string_t *lvc_name;
    int lvc_column;
};

class log_vtab_impl;

/**
 * Base class for implementations of log format parsers.
 */
class log_format {
public:

    /**
     * @return The collection of builtin log formats.
     */
    static std::vector<log_format *> &get_root_formats(void);

    /**
     * Template used to register log formats during initialization.
     */
    template<class T>
    class register_root_format {
public:
        register_root_format()
        {
            static T format;

            log_format::lf_root_formats.push_back(&format);
        };
    };

    static log_format *find_root_format(const char *name) {
        std::vector<log_format *> &fmts = get_root_formats();
        for (std::vector<log_format *>::iterator iter = fmts.begin();
             iter != fmts.end();
             ++iter) {
            log_format *lf = *iter;
            if (lf->get_name() == name) {
                return lf;
            }
        }
        return NULL;
    }

    struct action_def {
        std::string ad_name;
        std::string ad_label;
        std::vector<std::string> ad_cmdline;
        bool ad_capture_output;

        action_def() : ad_capture_output(false) { };

        bool operator<(const action_def &rhs) const {
            return this->ad_name < rhs.ad_name;
        };
    };

    log_format() : lf_mod_index(0),
                   lf_timestamp_field(intern_string::lookup("timestamp", -1)),
                   lf_timestamp_flags(0),
                   lf_is_self_describing(false),
                   lf_time_ordered(true) {
    };

    virtual ~log_format() { };

    virtual void clear()
    {
        this->lf_pattern_locks.clear();
        this->lf_date_time.clear();
    };

    /**
     * Get the name of this log format.
     *
     * @return The log format name.
     */
    virtual intern_string_t get_name(void) const = 0;

    virtual bool match_name(const std::string &filename) { return true; };

    enum scan_result_t {
        SCAN_MATCH,
        SCAN_NO_MATCH,
        SCAN_INCOMPLETE,
    };

    /**
     * Scan a log line to see if it matches this log format.
     *
     * @param dst The vector of loglines that the formatter should append to
     *   if it detected a match.
     * @param offset The offset in the file where this line is located.
     * @param prefix The contents of the line.
     * @param len The length of the prefix string.
     */
    virtual scan_result_t scan(logfile &lf,
                               std::vector<logline> &dst,
                               off_t offset,
                               shared_buffer_ref &sbr) = 0;

    virtual bool scan_for_partial(shared_buffer_ref &sbr, size_t &len_out) {
        return false;
    };

    /**
     * Remove redundant data from the log line string.
     *
     * XXX We should probably also add some attributes to the line here, so we
     * can highlight things like the date.
     *
     * @param line The log line to edit.
     */
    virtual void scrub(std::string &line) { };

    virtual void
    annotate(uint64_t line_number, shared_buffer_ref &sbr, string_attrs_t &sa,
                 std::vector<logline_value> &values, bool annotate_module = true) const
    { };

    virtual void rewrite(exec_context &ec,
                         shared_buffer_ref &line,
                         string_attrs_t &sa,
                         std::string &value_out) {
        value_out.assign(line.get_data(), line.length());
    };

    virtual const logline_value_stats *stats_for_value(const intern_string_t &name) const {
        return NULL;
    };

    virtual std::unique_ptr<log_format> specialized(int fmt_lock = -1) = 0;

    virtual log_vtab_impl *get_vtab_impl(void) const {
        return NULL;
    };

    virtual void get_subline(const logline &ll, shared_buffer_ref &sbr, bool full_message = false) {
    };

    virtual const std::vector<std::string> *get_actions(const logline_value &lv) const {
        return NULL;
    };

    virtual const std::set<std::string> get_source_path() const {
        std::set<std::string> retval;

        retval.insert("default");

        return retval;
    };

    virtual bool hide_field(const intern_string_t field_name, bool val) {
        return false;
    };

    const char * const *get_timestamp_formats() const {
        if (this->lf_timestamp_format.empty()) {
            return NULL;
        }

        return &this->lf_timestamp_format[0];
    };

    void check_for_new_year(std::vector<logline> &dst, exttm log_tv,
                            timeval timeval1);

    virtual std::string get_pattern_name(uint64_t line_number) const {
        int pat_index = this->pattern_index_for_line(line_number);
        char name[32];
        snprintf(name, sizeof(name), "builtin (%d)", pat_index);
        return name;
    };

    virtual std::string get_pattern_regex(uint64_t line_number) const {
        return "";
    };

    struct pattern_for_lines {
        pattern_for_lines(uint32_t pfl_line, uint32_t pfl_pat_index);

        uint32_t pfl_line;
        int pfl_pat_index;
    };

    int last_pattern_index() const {
        if (this->lf_pattern_locks.empty()) {
            return -1;
        }

        return this->lf_pattern_locks.back().pfl_pat_index;
    }

    int pattern_index_for_line(uint64_t line_number) const;

    uint8_t lf_mod_index;
    date_time_scanner lf_date_time;
    std::vector<pattern_for_lines> lf_pattern_locks;
    intern_string_t lf_timestamp_field;
    std::vector<const char *> lf_timestamp_format;
    int lf_timestamp_flags;
    std::map<std::string, action_def> lf_action_defs;
    std::vector<logline_value_stats> lf_value_stats;
    std::vector<highlighter> lf_highlighters;
    bool lf_is_self_describing;
    bool lf_time_ordered;
protected:
    static std::vector<log_format *> lf_root_formats;

    struct pcre_format {
        pcre_format(const char *regex) : name(regex), pcre(regex) {
            this->pf_timestamp_index = this->pcre.name_index("timestamp");
        };

        pcre_format() : name(NULL), pcre("") { };

        const char *name;
        pcrepp pcre;
        int pf_timestamp_index{-1};
    };

    static bool next_format(pcre_format *fmt, int &index, int &locked_index);

    const char *log_scanf(uint32_t line_number,
                          const char *line,
                          size_t len,
                          pcre_format *fmt,
                          const char *time_fmt[],
                          struct exttm *tm_out,
                          struct timeval *tv_out,
                          ...);
};

class module_format;

class external_log_format : public log_format {

public:
    struct sample {
        sample() : s_level(LEVEL_UNKNOWN) {};

        std::string s_line;
        log_level_t s_level;
    };

    struct value_def {
        value_def() :
            vd_kind(logline_value::VALUE_UNKNOWN),
            vd_identifier(false),
            vd_foreign_key(false),
            vd_column(-1),
            vd_values_index(-1),
            vd_hidden(false),
            vd_user_hidden(false),
            vd_internal(false) {

        };

        intern_string_t vd_name;
        logline_value::kind_t vd_kind;
        std::string vd_collate;
        bool vd_identifier;
        bool vd_foreign_key;
        intern_string_t vd_unit_field;
        std::map<const intern_string_t, scaling_factor> vd_unit_scaling;
        int vd_column;
        ssize_t vd_values_index;
        bool vd_hidden;
        bool vd_user_hidden;
        bool vd_internal;
        std::vector<std::string> vd_action_list;
        std::string vd_rewriter;
        std::string vd_description;
    };

    struct indexed_value_def {
        indexed_value_def(int index = -1, int unit_index = -1, value_def *vd = NULL)
            : ivd_index(index),
              ivd_unit_field_index(unit_index),
              ivd_value_def(vd) {
        }

        int ivd_index;
        int ivd_unit_field_index;
        const struct value_def *ivd_value_def;

        bool operator<(const indexed_value_def &rhs) const {
            return this->ivd_index < rhs.ivd_index;
        }
    };

    struct pattern {
        pattern() : p_pcre(NULL),
                    p_timestamp_field_index(-1),
                    p_level_field_index(-1),
                    p_module_field_index(-1),
                    p_opid_field_index(-1),
                    p_body_field_index(-1),
                    p_timestamp_end(-1),
                    p_module_format(false) {

        };

        std::string p_config_path;
        std::string p_string;
        pcrepp *p_pcre;
        std::vector<indexed_value_def> p_value_by_index;
        std::vector<int> p_numeric_value_indexes;
        int p_timestamp_field_index;
        int p_level_field_index;
        int p_module_field_index;
        int p_opid_field_index;
        int p_body_field_index;
        int p_timestamp_end;
        bool p_module_format;
    };

    struct level_pattern {
        level_pattern() : lp_pcre(NULL) { };
        
        std::string lp_regex;
        pcrepp *lp_pcre;
    };

    external_log_format(const intern_string_t name)
        : elf_file_pattern(".*"),
          elf_filename_pcre(NULL),
          elf_column_count(0),
          elf_timestamp_divisor(1.0),
          elf_level_field(intern_string::lookup("level", -1)),
          elf_body_field(intern_string::lookup("body", -1)),
          elf_multiline(true),
          elf_container(false),
          elf_has_module_format(false),
          elf_builtin_format(false),
          elf_type(ELF_TYPE_TEXT),
          jlf_hide_extra(false),
          jlf_cached_offset(-1),
          jlf_yajl_handle(yajl_free),
          elf_name(name) {
            this->jlf_line_offsets.reserve(128);
        };

    intern_string_t get_name(void) const {
        return this->elf_name;
    };

    bool match_name(const std::string &filename) {
        pcre_context_static<10> pc;
        pcre_input pi(filename);

        return this->elf_filename_pcre->match(pc, pi);
    };

    scan_result_t scan(logfile &lf,
                       std::vector<logline> &dst,
                       off_t offset,
                       shared_buffer_ref &sbr);

    bool scan_for_partial(shared_buffer_ref &sbr, size_t &len_out);

    void annotate(uint64_t line_number, shared_buffer_ref &line, string_attrs_t &sa,
                      std::vector<logline_value> &values, bool annotate_module = true) const;

    void rewrite(exec_context &ec,
                 shared_buffer_ref &line,
                 string_attrs_t &sa,
                 std::string &value_out);

    void build(std::vector<std::string> &errors);

    void register_vtabs(log_vtab_manager *vtab_manager,
                        std::vector<std::string> &errors);

    bool match_samples(const std::vector<sample> &samples) const;

    bool hide_field(const intern_string_t field_name, bool val) {
        auto vd_iter = this->elf_value_defs.find(field_name);

        if (vd_iter == this->elf_value_defs.end()) {
            return false;
        }

        vd_iter->second->vd_user_hidden = val;
        return true;
    };

    std::unique_ptr<log_format> specialized(int fmt_lock) {
        external_log_format *elf = new external_log_format(*this);
        std::unique_ptr<log_format> retval(elf);

        this->lf_pattern_locks.clear();
        if (fmt_lock != -1) {
            elf->lf_pattern_locks.emplace_back(0, fmt_lock);
        }

        if (this->elf_type == ELF_TYPE_JSON) {
            this->jlf_parse_context.reset(new yajlpp_parse_context(this->elf_name.to_string()));
            this->jlf_yajl_handle.reset(yajl_alloc(
                    &this->jlf_parse_context->ypc_callbacks,
                    NULL,
                    this->jlf_parse_context.get()));
            yajl_config(this->jlf_yajl_handle.in(), yajl_dont_validate_strings, 1);
            this->jlf_cached_line.reserve(16 * 1024);
        }

        this->lf_value_stats.clear();
        this->lf_value_stats.resize(this->elf_numeric_value_defs.size());

        return retval;
    };

    const logline_value_stats *stats_for_value(const intern_string_t &name) const {
        const logline_value_stats *retval = NULL;

        for (size_t lpc = 0; lpc < this->elf_numeric_value_defs.size(); lpc++) {
            value_def &vd = *this->elf_numeric_value_defs[lpc];

            if (vd.vd_name == name) {
                retval = &this->lf_value_stats[lpc];
                break;
            }
        }

        return retval;
    };

    void get_subline(const logline &ll, shared_buffer_ref &sbr, bool full_message);

    log_vtab_impl *get_vtab_impl(void) const;

    const std::vector<std::string> *get_actions(const logline_value &lv) const {
        const std::vector<std::string> *retval = NULL;

        const auto iter = this->elf_value_defs.find(lv.lv_name);
        if (iter != this->elf_value_defs.end()) {
            retval = &iter->second->vd_action_list;
        }

        return retval;
    };

    const std::set<std::string> get_source_path() const {
        return this->elf_source_path;
    };

    enum json_log_field {
        JLF_CONSTANT,
        JLF_VARIABLE
    };

    struct json_format_element {
        enum class align_t {
            LEFT,
            RIGHT,
        };

        enum class overflow_t {
            ABBREV,
            TRUNCATE,
            DOTDOT,
        };

        enum class transform_t {
            NONE,
            UPPERCASE,
            LOWERCASE,
            CAPITALIZE,
        };

        json_format_element()
            : jfe_type(JLF_CONSTANT), jfe_default_value("-"), jfe_min_width(0),
              jfe_max_width(LLONG_MAX), jfe_align(align_t::LEFT),
              jfe_overflow(overflow_t::ABBREV),
              jfe_text_transform(transform_t::NONE)
        { };

        json_log_field jfe_type;
        intern_string_t jfe_value;
        std::string jfe_default_value;
        long long jfe_min_width;
        long long jfe_max_width;
        align_t jfe_align;
        overflow_t jfe_overflow;
        transform_t jfe_text_transform;
        std::string jfe_ts_format;
    };

    struct json_field_cmp {
        json_field_cmp(json_log_field type,
                       const intern_string_t name)
            : jfc_type(type), jfc_field_name(name) {
        };

        bool operator()(const json_format_element &jfe) const {
            return (this->jfc_type == jfe.jfe_type &&
                    this->jfc_field_name == jfe.jfe_value);
        };

        json_log_field jfc_type;
        const intern_string_t jfc_field_name;
    };

    struct highlighter_def {
        highlighter_def() : hd_underline(false), hd_blink(false) {
        }

        std::string hd_pattern;
        std::string hd_color;
        std::string hd_background_color;
        bool hd_underline;
        bool hd_blink;
    };

    long value_line_count(const intern_string_t ist,
                          bool top_level,
                          const unsigned char *str = NULL,
                          ssize_t len = -1) const {
        const auto iter = this->elf_value_defs.find(ist);
        long line_count = (str != NULL) ? std::count(&str[0], &str[len], '\n') + 1 : 1;

        if (iter == this->elf_value_defs.end()) {
            return (this->jlf_hide_extra || !top_level) ? 0 : line_count;
        }

        if (iter->second->vd_hidden) {
            return 0;
        }

        if (std::find_if(this->jlf_line_format.begin(),
                         this->jlf_line_format.end(),
                         json_field_cmp(JLF_VARIABLE, ist)) !=
            this->jlf_line_format.end()) {
            return line_count - 1;
        }

        return line_count;
    };

    bool has_value_def(const intern_string_t ist) const {
        const auto iter = this->elf_value_defs.find(ist);

        return iter != this->elf_value_defs.end();
    };

    std::string get_pattern_name(uint64_t line_number) const {
        if (this->elf_type != ELF_TYPE_TEXT) {
            return "structured";
        }
        int pat_index = this->pattern_index_for_line(line_number);
        return this->elf_pattern_order[pat_index]->p_config_path;
    }

    std::string get_pattern_regex(uint64_t line_number) const {
        if (this->elf_type != ELF_TYPE_TEXT) {
            return "";
        }
        int pat_index = this->pattern_index_for_line(line_number);
        return this->elf_pattern_order[pat_index]->p_string;
    }

    log_level_t convert_level(const pcre_input &pi, pcre_context::capture_t *level_cap) const {
        log_level_t retval = LEVEL_INFO;

        if (level_cap != nullptr && level_cap->is_valid()) {
            pcre_context_static<128> pc_level;
            pcre_input pi_level(pi.get_substr_start(level_cap),
                                0,
                                level_cap->length());

            if (this->elf_level_patterns.empty()) {
                retval = string2level(pi_level.get_string(), level_cap->length());
            } else {
                for (const auto &elf_level_pattern : this->elf_level_patterns) {
                    if (elf_level_pattern.second.lp_pcre->match(pc_level, pi_level)) {
                        retval = elf_level_pattern.first;
                        break;
                    }
                }
            }
        }

        return retval;
    }

    typedef std::map<intern_string_t, module_format> mod_map_t;
    static mod_map_t MODULE_FORMATS;
    static std::vector<external_log_format *> GRAPH_ORDERED_FORMATS;

    std::set<std::string> elf_source_path;
    std::list<intern_string_t> elf_collision;
    std::string elf_file_pattern;
    pcrepp *elf_filename_pcre;
    std::map<std::string, std::shared_ptr<pattern>> elf_patterns;
    std::vector<std::shared_ptr<pattern>> elf_pattern_order;
    std::vector<sample> elf_samples;
    std::map<const intern_string_t, std::shared_ptr<value_def>> elf_value_defs;
    std::vector<std::shared_ptr<value_def>> elf_numeric_value_defs;
    int elf_column_count;
    double elf_timestamp_divisor;
    intern_string_t elf_level_field;
    intern_string_t elf_body_field;
    intern_string_t elf_module_id_field;
    intern_string_t elf_opid_field;
    std::map<log_level_t, level_pattern> elf_level_patterns;
    std::vector<std::pair<int64_t, log_level_t> > elf_level_pairs;
    bool elf_multiline;
    bool elf_container;
    bool elf_has_module_format;
    bool elf_builtin_format;
    std::vector<std::pair<intern_string_t, std::string> > elf_search_tables;
    std::map<const intern_string_t, highlighter_def> elf_highlighter_patterns;

    enum elf_type_t {
        ELF_TYPE_TEXT,
        ELF_TYPE_JSON,
        ELF_TYPE_CSV,
    };

    elf_type_t elf_type;

    void json_append_to_cache(const char *value, ssize_t len) {
        size_t old_size = this->jlf_cached_line.size();
        if (len == -1) {
            len = strlen(value);
        }
        this->jlf_cached_line.resize(old_size + len);
        memcpy(&(this->jlf_cached_line.data()[old_size]), value, len);
    };

    void json_append_to_cache(ssize_t len) {
        size_t old_size = this->jlf_cached_line.size();
        this->jlf_cached_line.resize(old_size + len);
        memset(&this->jlf_cached_line[old_size], ' ', len);
    };

    void json_append(const json_format_element &jfe, const char *value, ssize_t len) {
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
    };

    bool jlf_hide_extra;
    std::vector<json_format_element> jlf_line_format;
    int jlf_line_format_init_count{0};
    std::vector<logline_value> jlf_line_values;

    off_t jlf_cached_offset;
    bool jlf_cached_full{false};
    std::vector<off_t> jlf_line_offsets;
    shared_buffer jlf_share_manager;
    std::vector<char> jlf_cached_line;
    string_attrs_t jlf_line_attrs;
    std::shared_ptr<yajlpp_parse_context> jlf_parse_context;
    auto_mem<yajl_handle_t> jlf_yajl_handle;
private:
    const intern_string_t elf_name;

    static uint8_t module_scan(const pcre_input &pi,
                               pcre_context::capture_t *body_cap,
                               const intern_string_t &mod_name);
};

class module_format {

public:
    module_format() : mf_mod_format(NULL) {

    };

    external_log_format *mf_mod_format;
};

#endif
