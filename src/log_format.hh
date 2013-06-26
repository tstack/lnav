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

#include <assert.h>
#include <time.h>
#include <stdint.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <sys/types.h>

#include <string>
#include <vector>
#include <memory>

#include "pcrepp.hh"
#include "byte_array.hh"
#include "view_curses.hh"

/**
 * Convert the time stored in a 'tm' struct into epoch time.
 *
 * @param t The 'tm' structure to convert to epoch time.
 * @return The given time in seconds since the epoch.
 */
time_t tm2sec(const struct tm *t);

class logfile_filter {
public:
    typedef enum {
        MAYBE,
        INCLUDE,
        EXCLUDE,

        LFT__MAX,

        LFT__MASK = (MAYBE|INCLUDE|EXCLUDE)
    } type_t;

    logfile_filter(type_t type, std::string id)
        : lf_enabled(true),
          lf_type(type),
          lf_id(id) { };
    virtual ~logfile_filter() { };

    type_t get_type(void) const { return this->lf_type; };
    std::string get_id(void) const { return this->lf_id; };

    bool is_enabled(void) { return this->lf_enabled; };
    void enable(void) { this->lf_enabled = true; };
    void disable(void) { this->lf_enabled = false; };

    virtual bool matches(std::string line) = 0;

    virtual std::string to_command(void) = 0;

protected:
    bool        lf_enabled;
    type_t      lf_type;
    std::string lf_id;
};

typedef std::vector<logfile_filter *> filter_stack_t;

/**
 * Metadata for a single line in a log file.
 */
class logline {
public:

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

        LEVEL_MULTILINE = 0x40,  /*< Start of a multiline entry. */
        LEVEL_CONTINUED = 0x80,  /*< Continuation of multiline entry. */

        /** Mask of flags for the level field. */
        LEVEL__FLAGS    = (LEVEL_MULTILINE | LEVEL_CONTINUED)
    } level_t;

    static const char *level_names[LEVEL__MAX];

    static level_t string2level(const char *levelstr, bool exact = false);

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
            level_t l,
            uint8_t m = 0)
        : ll_offset(off),
          ll_time(t),
          ll_millis(millis),
          ll_level(l),
          ll_filter_state(logfile_filter::MAYBE)
    {
        memset(this->ll_schema, 0, sizeof(this->ll_schema));
    };

    /** @return The offset of the line in the file. */
    off_t get_offset() const { return this->ll_offset; };

    /** @return The timestamp for the line. */
    time_t get_time() const { return this->ll_time; };

    void set_time(time_t t) { this->ll_time = t; };

    /** @return The millisecond timestamp for the line. */
    uint16_t get_millis() const { return this->ll_millis; };

    void set_millis(uint16_t m) { this->ll_millis = m; }

    void set_multiline(void) { this->ll_level |= LEVEL_MULTILINE; };

    /** @param l The logging level. */
    void set_level(level_t l) { this->ll_level = l; };

    /** @return The logging level. */
    level_t get_level() const { return (level_t)(this->ll_level & 0xff); };

    const char *get_level_name() const
    {
        return level_names[this->ll_level & 0x0f];
    };

    uint8_t get_filter_generation(void) const {
        return this->ll_filter_state >> 2;
    };

    logfile_filter::type_t get_filter_state(void) const {
        return (logfile_filter::type_t)(this->ll_filter_state &
                                        logfile_filter::LFT__MASK);
    };

    void set_filter_state(uint8_t generation, logfile_filter::type_t filter) {
        this->ll_filter_state = (generation << 2) | filter;
    };

    /**
     * @return  True if there is a schema value set for this log line.
     */
    bool has_schema(void) const
    {
        return this->ll_schema[0] != 0 ||
               this->ll_schema[1] != 0 ||
               this->ll_schema[2] != 0 ||
               this->ll_schema[3] != 0;
    };

    /**
     * Set the "schema" for this log line.  The schema ID is used to match log
     * lines that have a similar format when generating the logline table.  The
     * schema is set lazily so that startup is faster.
     *
     * @param ba The SHA-1 hash of the constant parts of this log line.
     */
    void set_schema(const byte_array<20> &ba)
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
    bool match_schema(const byte_array<20> &ba) const
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

private:
    off_t    ll_offset;
    time_t   ll_time;
    uint16_t ll_millis;
    uint8_t  ll_level;
    uint8_t  ll_filter_state;
    char     ll_schema[4];
};

class logline_value {
public:
    enum kind_t {
        VALUE_TEXT,
        VALUE_INTEGER,
        VALUE_FLOAT,
    };

    logline_value(std::string name, int64_t i)
        : lv_name(name), lv_kind(VALUE_INTEGER), lv_number(i) { };
    logline_value(std::string name, double i)
        : lv_name(name), lv_kind(VALUE_FLOAT), lv_number(i) { };
    logline_value(std::string name, std::string s)
        : lv_name(name), lv_kind(VALUE_TEXT), lv_string(s) { };
    logline_value(std::string name, kind_t kind, std::string s)
        : lv_name(name), lv_kind(kind)
    {
        switch (kind) {
        case VALUE_TEXT:
            this->lv_string = s;
            break;

        case VALUE_INTEGER:
            sscanf(s.c_str(), "%" PRId64 "", &this->lv_number.i);
            break;

        case VALUE_FLOAT:
            sscanf(s.c_str(), "%lf", &this->lv_number.d);
            break;
        }
    };

    const std::string to_string()
    {
        char buffer[128];

        switch (this->lv_kind) {
        case VALUE_TEXT:
            return this->lv_string;

        case VALUE_INTEGER:
            snprintf(buffer, sizeof(buffer), "%" PRId64, this->lv_number.i);
            break;

        case VALUE_FLOAT:
            snprintf(buffer, sizeof(buffer), "%lf", this->lv_number.d);
            break;
        }

        return std::string(buffer);
    };

    std::string lv_name;
    kind_t      lv_kind;
    union value_u {
        int64_t i;
        double  d;

        value_u() : i(0) { };
        value_u(int64_t i) : i(i) { };
        value_u(double d) : d(d) { };
    }           lv_number;
    std::string lv_string;
};

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

    log_format() : lf_fmt_lock(-1), lf_time_fmt_lock(-1), lf_time_fmt_len(-1) {
    };
    virtual ~log_format() { };

    virtual void clear(void)
    {
        this->lf_fmt_lock      = -1;
        this->lf_time_fmt_lock = -1;
        this->lf_time_fmt_len = -1;
    };

    /**
     * Get the name of this log format.
     *
     * @return The log format name.
     */
    virtual std::string get_name(void) = 0;

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
                      char *prefix,
                      int len) = 0;

    /**
     * Remove redundant data from the log line string.
     *
     * XXX We should probably also add some attributes to the line here, so we
     * can highlight things like the date.
     *
     * @param line The log line to edit.
     */
    virtual void scrub(std::string &line) { };

    virtual void annotate(const std::string &line,
                          string_attrs_t &sa,
                          std::vector<logline_value> &values) const
    { };

    virtual std::auto_ptr<log_format> specialized(void) = 0;

protected:
    static std::vector<log_format *> lf_root_formats;

    const char *log_scanf(const char *line,
                          const char *fmt[],
                          int expected_matches,
                          const char *time_fmt[],
                          char *time_dest,
                          struct tm *tm_out,
                          time_t &time_out,
                          ...);

    const char *time_scanf(const char *time_dest,
                           const char *time_fmt[],
                           struct tm *tm_out,
                           time_t &time_out);

    int lf_fmt_lock;
    int lf_time_fmt_lock;
    int lf_time_fmt_len;
};

class external_log_format : public log_format {

public:
    struct sample {
        std::string s_line;
        logline::level_t s_level;
    };

    struct value_def {
        std::string vd_name;
        logline_value::kind_t vd_kind;
    };

    struct level_pattern {
        std::string lp_regex;
        pcrepp *lp_pcre;
    };

    external_log_format(const std::string &name) : elf_name(name) { };

    std::string get_name(void) {
        return this->elf_name;
    };

    bool scan(std::vector<logline> &dst,
              off_t offset,
              char *prefix,
              int len) {
        pcre_input pi(prefix, 0, len);
        pcre_context_static<30> pc;
        bool retval = false;

        if (this->elf_pcre->match(pc, pi)) {
            pcre_context::capture_t *ts = pc["timestamp"];
            pcre_context::capture_t *level_cap = pc[this->elf_level_field];
            const char *ts_str = pi.get_substr_start(ts);
            const char *last;
            time_t line_time;
            struct tm log_time;
            uint16_t millis = 0;
            logline::level_t level = logline::LEVEL_INFO;

            if ((last = this->time_scanf(ts_str,
                                         NULL,
                                         &log_time,
                                         line_time)) == NULL) {
                return false;
            }

            /* Try to pull out the milliseconds value. */
            if (last[0] == ',' || last[0] == '.') {
                int subsec_len = 0;

                sscanf(last + 1, "%hd%n", &millis, &subsec_len);
                if (millis >= 1000) {
                    millis = 0;
                }
            }

            if (level_cap != NULL && level_cap->c_begin != -1) {
                pcre_context_static<30> pc_level;
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

            dst.push_back(logline(offset,
                          line_time,
                          millis,
                          level));

            retval = true;
        }

        return retval;
    };

    void annotate(const std::string &line,
                  string_attrs_t &sa,
                  std::vector<logline_value> &values) const
    {
        pcre_context_static<30> pc;
        pcre_input pi(line);
        struct line_range lr;
        pcre_context::capture_t *cap;

        if (!this->elf_pcre->match(pc, pi))
            return;

        cap = pc["timestamp"];
        lr.lr_start = cap->c_begin;
        lr.lr_end = cap->c_end;
        sa[lr].insert(make_string_attr("timestamp", 0));

        cap = pc["body"];
        lr.lr_start = cap->c_begin;
        lr.lr_end = cap->c_end;
        sa[lr].insert(make_string_attr("body", 0));
        
        for (std::vector<value_def>::const_iterator iter =
                 this->elf_value_defs.begin();
             iter != this->elf_value_defs.end();
             ++iter) {
            cap = pc[iter->vd_name];

            values.push_back(logline_value(iter->vd_name,
                                           iter->vd_kind,
                                           pi.get_substr(cap)));
        }
    }

    void build(void) {
        this->elf_pcre = new pcrepp(this->elf_regex.c_str());
        for (std::map<logline::level_t, level_pattern>::iterator iter = this->elf_level_patterns.begin();
             iter != this->elf_level_patterns.end();
             ++iter) {
            iter->second.lp_pcre = new pcrepp(iter->second.lp_regex.c_str());
        }
    };

    std::auto_ptr<log_format> specialized() {
        std::auto_ptr<log_format> retval((log_format *)
                                         new external_log_format(*this));

        return retval;
    };

    std::string elf_regex;
    pcrepp *elf_pcre;
    std::vector<sample> elf_samples;
    std::vector<value_def> elf_value_defs;
    std::string elf_level_field;
    std::map<logline::level_t, level_pattern> elf_level_patterns;

private:
    const std::string elf_name;

};

#endif
