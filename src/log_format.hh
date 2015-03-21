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
#include <string>
#include <vector>
#include <memory>
#include <sstream>

#include "pcrepp.hh"
#include "yajlpp.hh"
#include "lnav_log.hh"
#include "lnav_util.hh"
#include "byte_array.hh"
#include "view_curses.hh"
#include "intern_string.hh"
#include "shared_buffer.hh"

/**
 * Metadata for a single line in a log file.
 */
class logline {
public:
    static string_attr_type L_PREFIX;
    static string_attr_type L_TIMESTAMP;
    static string_attr_type L_FILE;
    static string_attr_type L_PARTITION;

    /**
     * The logging level identifiers for a line(s).
     */
    typedef enum {
        LEVEL_UNKNOWN,
        LEVEL_TRACE,
        LEVEL_DEBUG,
        LEVEL_INFO,
        LEVEL_WARNING,
        LEVEL_ERROR,
        LEVEL_CRITICAL,
        LEVEL_FATAL,

        LEVEL__MAX,

        LEVEL_MARK      = 0x40,  /*< Bookmarked line. */
        LEVEL_CONTINUED = 0x80,  /*< Continuation of multiline entry. */

        /** Mask of flags for the level field. */
        LEVEL__FLAGS    = (LEVEL_MARK | LEVEL_CONTINUED)
    } level_t;

    static const char *level_names[LEVEL__MAX + 1];

    static level_t string2level(const char *levelstr, ssize_t len = -1, bool exact = false);

    static level_t abbrev2level(const char *levelstr, ssize_t len = -1);

    static int levelcmp(const char *l1, ssize_t l1_len,
        const char *l2, ssize_t l2_len);

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
            level_t l)
        : ll_offset(off),
          ll_time(t),
          ll_millis(millis),
          ll_sub_offset(0),
          ll_level(l)
    {
        memset(this->ll_schema, 0, sizeof(this->ll_schema));
    };

    logline(off_t off,
            const struct timeval &tv,
            level_t l,
            uint8_t m = 0)
        : ll_offset(off),
          ll_sub_offset(0),
          ll_level(l)
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

    /** @param l The logging level. */
    void set_level(level_t l) { this->ll_level = l; };

    /** @return The logging level. */
    level_t get_level() const { return (level_t)(this->ll_level & 0xff); };

    level_t get_msg_level() const {
        return (level_t)(this->ll_level & ~LEVEL__FLAGS);
    };

    const char *get_level_name() const
    {
        return level_names[this->ll_level & 0x0f];
    };

    bool is_continued(void) const {
        return this->get_level() & LEVEL_CONTINUED;
    };

    /**
     * @return  True if there is a schema value set for this log line.
     */
    bool has_schema(void) const
    {
        return (this->ll_schema[0] != 0 ||
                this->ll_schema[1] != 0 ||
                this->ll_schema[2] != 0);
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
        return this->ll_time < rhs.ll_time ||
               (this->ll_time == rhs.ll_time &&
                this->ll_millis < rhs.ll_millis);
    };

    bool operator<(const time_t &rhs) const { return this->ll_time < rhs; };

    bool operator<(const struct timeval &rhs) const {
        return ((this->ll_time < rhs.tv_sec) ||
                (this->ll_millis < (rhs.tv_usec / 1000)));
    };

private:
    off_t    ll_offset;
    time_t   ll_time;
    uint16_t ll_millis;
    uint16_t ll_sub_offset;
    uint8_t  ll_level;
    char     ll_schema[3];
};

enum scale_op_t {
    SO_IDENTITY,
    SO_MULTIPLY,
    SO_DIVIDE
};

struct scaling_factor {
    scaling_factor() : sf_op(SO_IDENTITY), sf_value(1) { };

    template<typename T>
    void scale(T &val) const {
        switch (this->sf_op) {
        case SO_IDENTITY:
            break;
        case SO_DIVIDE:
            val = val / (T)this->sf_value;
            break;
        case SO_MULTIPLY:
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
        VALUE_QUOTED,

        VALUE__MAX
    };

    static const char *value_names[VALUE__MAX];
    static kind_t string2kind(const char *kindstr);

    logline_value(const intern_string_t name)
        : lv_name(name), lv_kind(VALUE_NULL), lv_identifier(), lv_column(-1) { };
    logline_value(const intern_string_t name, bool b)
        : lv_name(name),
          lv_kind(VALUE_BOOLEAN),
          lv_number((int64_t)(b ? 1 : 0)),
          lv_identifier(),
          lv_column(-1) { };
    logline_value(const intern_string_t name, int64_t i)
        : lv_name(name), lv_kind(VALUE_INTEGER), lv_number(i), lv_identifier(), lv_column(-1) { };
    logline_value(const intern_string_t name, double i)
        : lv_name(name), lv_kind(VALUE_FLOAT), lv_number(i), lv_identifier(), lv_column(-1) { };
    logline_value(const intern_string_t name, shared_buffer_ref &sbr)
        : lv_name(name), lv_kind(VALUE_TEXT), lv_sbr(sbr),
          lv_identifier(), lv_column(-1) {
    };
    logline_value(const intern_string_t name, kind_t kind, shared_buffer_ref &sbr,
                  bool ident=false, const scaling_factor *scaling=NULL,
                  int col=-1, int start=-1, int end=-1)
        : lv_name(name), lv_kind(kind),
          lv_identifier(ident), lv_column(col),
          lv_origin(start, end)
    {
        if (sbr.get_data() == NULL) {
            this->lv_kind = kind = VALUE_NULL;
        }

        switch (kind) {
        case VALUE_JSON:
        case VALUE_TEXT:
        case VALUE_QUOTED:
            this->lv_sbr = sbr;
            break;

        case VALUE_NULL:
            break;

        case VALUE_INTEGER:
            strtonum(this->lv_number.i, sbr.get_data(), sbr.length());
            if (scaling != NULL) {
                scaling->scale(this->lv_number.i);
            }
            break;

        case VALUE_FLOAT: {
            char scan_value[sbr.length() + 1];

            memcpy(scan_value, sbr.get_data(), sbr.length());
            scan_value[sbr.length()] = '\0';
            this->lv_number.d = strtod(scan_value, NULL);
            if (scaling != NULL) {
                scaling->scale(this->lv_number.d);
            }
            break;
        }

        case VALUE_BOOLEAN:
            if (strncmp(sbr.get_data(), "true", sbr.length()) == 0 ||
                strncmp(sbr.get_data(), "yes", sbr.length()) == 0) {
                this->lv_number.i = 1;
            }
            else {
                this->lv_number.i = 0;
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
        case VALUE_TEXT:
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
            snprintf(buffer, sizeof(buffer), "%" PRId64, this->lv_number.i);
            break;

        case VALUE_FLOAT:
            snprintf(buffer, sizeof(buffer), "%lf", this->lv_number.d);
            break;

        case VALUE_BOOLEAN:
            if (this->lv_number.i) {
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

    intern_string_t lv_name;
    kind_t      lv_kind;
    union value_u {
        int64_t i;
        double  d;

        value_u() : i(0) { };
        value_u(int64_t i) : i(i) { };
        value_u(double d) : d(d) { };
    }           lv_number;
    shared_buffer_ref lv_sbr;
    bool lv_identifier;
    int lv_column;
    struct line_range lv_origin;
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

    log_format() : lf_fmt_lock(-1),
                   lf_timestamp_field(intern_string::lookup("timestamp", -1)) {
    };
    virtual ~log_format() { };

    virtual void clear(void)
    {
        this->lf_fmt_lock      = -1;
        this->lf_date_time.clear();
    };

    /**
     * Get the name of this log format.
     *
     * @return The log format name.
     */
    virtual std::string get_name(void) const = 0;

    virtual bool match_name(const std::string &filename) { return true; };

    /**
     * Scan a log line to see if it matches this log format.
     *
     * @param dst The vector of loglines that the formatter should append to
     *   if it detected a match.
     * @param offset The offset in the file where this line is located.
     * @param prefix The contents of the line.
     * @param len The length of the prefix string.
     */
    virtual bool scan(std::vector<logline> &dst,
                      off_t offset,
                      shared_buffer_ref &sbr) = 0;

    /**
     * Remove redundant data from the log line string.
     *
     * XXX We should probably also add some attributes to the line here, so we
     * can highlight things like the date.
     *
     * @param line The log line to edit.
     */
    virtual void scrub(std::string &line) { };

    virtual void annotate(shared_buffer_ref &sbr,
                          string_attrs_t &sa,
                          std::vector<logline_value> &values) const
    { };

    virtual std::auto_ptr<log_format> specialized(void) = 0;

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

    void check_for_new_year(std::vector<logline> &dst,
        const struct timeval &log_tv);

    date_time_scanner lf_date_time;
    int lf_fmt_lock;
    intern_string_t lf_timestamp_field;
    int lf_timestamp_field_index;
    std::map<std::string, action_def> lf_action_defs;
protected:
    static std::vector<log_format *> lf_root_formats;

    struct pcre_format {
        const char *name;
        pcrepp pcre;
    };

    static bool next_format(pcre_format *fmt, int &index, int &locked_index);

    const char *log_scanf(const char *line,
                          size_t len,
                          pcre_format *fmt,
                          const char *time_fmt[],
                          struct exttm *tm_out,
                          struct timeval *tv_out,
                          ...);
};

class external_log_format : public log_format {

public:
    struct sample {
        std::string s_line;
        logline::level_t s_level;
    };

    struct value_def {
        value_def() :
            vd_index(-1),
            vd_kind(logline_value::VALUE_UNKNOWN),
            vd_identifier(false),
            vd_foreign_key(false),
            vd_unit_field_index(-1),
            vd_column(-1),
            vd_hidden(false) {

        };

        intern_string_t vd_name;
        int vd_index;
        logline_value::kind_t vd_kind;
        std::string vd_collate;
        bool vd_identifier;
        bool vd_foreign_key;
        intern_string_t vd_unit_field;
        int vd_unit_field_index;
        std::map<std::string, scaling_factor> vd_unit_scaling;
        int vd_column;
        bool vd_hidden;
        std::vector<std::string> vd_action_list;

        bool operator<(const value_def &rhs) const {
            return this->vd_index < rhs.vd_index;
        };
    };

    struct pattern {
        pattern() : p_pcre(NULL) { };

        std::string p_string;
        std::vector<std::string> p_before_pattern;
        pcrepp *p_pcre;
        std::vector<value_def> p_value_by_index;
    };

    struct level_pattern {
        level_pattern() : lp_pcre(NULL) { };
        
        std::string lp_regex;
        pcrepp *lp_pcre;
    };

    external_log_format(const std::string &name)
        : elf_file_pattern(".*"),
          elf_filename_pcre(NULL),
          elf_column_count(0),
          elf_timestamp_divisor(1.0),
          elf_body_field(intern_string::lookup("body", -1)),
          jlf_json(false),
          jlf_cached_offset(-1),
          jlf_yajl_handle(yajl_free),
          elf_name(name) {
            this->jlf_line_offsets.reserve(128);
        };

    std::string get_name(void) const {
        return this->elf_name;
    };

    bool match_name(const std::string &filename) {
        pcre_context_static<10> pc;
        pcre_input pi(filename);

        return this->elf_filename_pcre->match(pc, pi);
    };

    bool scan(std::vector<logline> &dst,
              off_t offset,
              shared_buffer_ref &sbr);
    
    void annotate(shared_buffer_ref &line,
                  string_attrs_t &sa,
                  std::vector<logline_value> &values) const;

    void build(std::vector<std::string> &errors);

    std::auto_ptr<log_format> specialized() {
        external_log_format *elf = new external_log_format(*this);
        std::auto_ptr<log_format> retval((log_format *)elf);

        if (this->jlf_json) {
            this->jlf_parse_context.reset(new yajlpp_parse_context(this->elf_name));
            this->jlf_yajl_handle.reset(yajl_alloc(
                    &this->jlf_parse_context->ypc_callbacks,
                    NULL,
                    this->jlf_parse_context.get()));
            yajl_config(this->jlf_yajl_handle.in(), yajl_dont_validate_strings, 1);
            this->jlf_cached_line.reserve(16 * 1024);
        }
        else if (this->lf_fmt_lock != -1) {
            pcrepp *pat = this->elf_pattern_order[this->lf_fmt_lock]->p_pcre;

            retval->lf_timestamp_field_index = pat->name_index(
                    this->lf_timestamp_field.to_string());
            if (this->elf_level_field.empty()) {
                this->elf_level_field_index = -1;
            }
            else {
                elf->elf_level_field_index = pat->name_index(elf->elf_level_field.to_string());
            }
            if (this->elf_body_field.empty()) {
                this->elf_body_field_index = -1;
            }
            else {
                elf->elf_body_field_index = pat->name_index(elf->elf_body_field.to_string());
            }
        }

        return retval;
    };

    void get_subline(const logline &ll, shared_buffer_ref &sbr, bool full_message);

    log_vtab_impl *get_vtab_impl(void) const;

    const std::vector<std::string> *get_actions(const logline_value &lv) const {
        std::map<const intern_string_t, value_def>::const_iterator iter;
        const std::vector<std::string> *retval = NULL;

        iter = this->elf_value_defs.find(lv.lv_name);
        if (iter != this->elf_value_defs.end()) {
            retval = &iter->second.vd_action_list;
        }

        return retval;
    };

    const std::set<std::string> get_source_path() const {
        return this->elf_source_path;
    };

    std::set<std::string> elf_source_path;
    std::string elf_file_pattern;
    pcrepp *elf_filename_pcre;
    std::map<std::string, pattern> elf_patterns;
    std::vector<pattern *> elf_pattern_order;
    std::vector<sample> elf_samples;
    std::map<const intern_string_t, value_def> elf_value_defs;
    int elf_column_count;
    double elf_timestamp_divisor;
    intern_string_t elf_level_field;
    int elf_level_field_index;
    intern_string_t elf_body_field;
    int elf_body_field_index;
    std::map<logline::level_t, level_pattern> elf_level_patterns;

    enum json_log_field {
        JLF_CONSTANT,
        JLF_VARIABLE
    };

    struct json_format_element {
        json_format_element()
            : jfe_type(JLF_CONSTANT), jfe_default_value("-"), jfe_min_width(0)
        { };

        json_log_field jfe_type;
        intern_string_t jfe_value;
        std::string jfe_default_value;
        int jfe_min_width;
    };

    void json_append_to_cache(const char *value, size_t len) {
        size_t old_size = this->jlf_cached_line.size();
        this->jlf_cached_line.resize(old_size + len);
        memcpy(&this->jlf_cached_line[old_size], value, len);
    };

    bool jlf_json;
    std::vector<json_format_element> jlf_line_format;
    std::vector<logline_value> jlf_line_values;

    off_t jlf_cached_offset;
    std::vector<off_t> jlf_line_offsets;
    shared_buffer jlf_share_manager;
    std::vector<char> jlf_cached_line;
    string_attrs_t jlf_line_attrs;
    std::auto_ptr<yajlpp_parse_context> jlf_parse_context;
    auto_mem<yajl_handle_t> jlf_yajl_handle;
private:
    const std::string elf_name;

};

#endif
