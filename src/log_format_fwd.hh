/**
 * Copyright (c) 2020, Timothy Stack
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
 * @file log_format_fwd.hh
 */

#ifndef lnav_log_format_fwd_hh
#define lnav_log_format_fwd_hh

#include <utility>

#include <sys/types.h>

#include "ArenaAlloc/arenaalloc.h"
#include "base/file_range.hh"
#include "base/map_util.hh"
#include "base/string_attr_type.hh"
#include "byte_array.hh"
#include "log_level.hh"
#include "pcrepp/pcre2pp.hh"
#include "robin_hood/robin_hood.h"
#include "yajlpp/yajlpp.hh"

class log_format;

struct log_level_stats {
    uint32_t lls_error_count{0};
    uint32_t lls_warning_count{0};
    uint32_t lls_total_count{0};

    log_level_stats& operator|=(const log_level_stats& rhs);
    void update_msg_count(log_level_t lvl);
};

struct log_op_description {
    std::optional<intern_string_t> lod_id;
    lnav::map::small<size_t, std::string> lod_elements;

    log_op_description& operator|=(const log_op_description& rhs);
};

struct opid_sub_time_range {
    string_fragment ostr_subid;
    time_range ostr_range;
    bool ostr_open{true};
    log_level_stats ostr_level_stats;
    std::string ostr_description;

    bool operator<(const opid_sub_time_range& rhs) const
    {
        return this->ostr_range < rhs.ostr_range;
    }
};

struct opid_time_range {
    time_range otr_range;
    log_level_stats otr_level_stats;
    log_op_description otr_description;
    std::vector<opid_sub_time_range> otr_sub_ops;

    void close_sub_ops(const string_fragment& subid);

    opid_time_range& operator|=(const opid_time_range& rhs);
};

using log_opid_map = robin_hood::unordered_map<string_fragment,
                                               opid_time_range,
                                               frag_hasher,
                                               std::equal_to<string_fragment>>;

using sub_opid_map = robin_hood::unordered_map<string_fragment,
                                               string_fragment,
                                               frag_hasher,
                                               std::equal_to<string_fragment>>;

struct log_opid_state {
    log_opid_map los_opid_ranges;
    sub_opid_map los_sub_in_use;

    opid_sub_time_range* sub_op_in_use(ArenaAlloc::Alloc<char>& alloc,
                                       log_opid_map::iterator& op_iter,
                                       const string_fragment& subid,
                                       const timeval& tv,
                                       log_level_t level);
};

struct scan_batch_context {
    ArenaAlloc::Alloc<char>& sbc_allocator;
    log_opid_state sbc_opids;
    std::string sbc_cached_level_strings[4];
    log_level_t sbc_cached_level_values[4];
    size_t sbc_cached_level_count{0};
};

/**
 * Metadata for a single line in a log file.
 */
class logline {
public:
    static string_attr_type<void> L_PREFIX;
    static string_attr_type<void> L_TIMESTAMP;
    static string_attr_type<std::shared_ptr<logfile>> L_FILE;
    static string_attr_type<bookmark_metadata*> L_PARTITION;
    static string_attr_type<void> L_MODULE;
    static string_attr_type<void> L_OPID;
    static string_attr_type<bookmark_metadata*> L_META;

    /**
     * Construct a logline object with the given values.
     *
     * @param off The offset of the line in the file.
     * @param t The timestamp for the line.
     * @param millis The millisecond timestamp for the line.
     * @param l The logging level.
     */
    logline(file_off_t off,
            time_t t,
            uint16_t millis,
            log_level_t lev,
            uint8_t mod = 0,
            uint8_t opid = 0)
        : ll_offset(off), ll_has_ansi(false), ll_time(t), ll_millis(millis),
          ll_opid(opid), ll_sub_offset(0), ll_valid_utf(1), ll_level(lev),
          ll_module_id(mod), ll_meta_mark(0), ll_expr_mark(0)
    {
        memset(this->ll_schema, 0, sizeof(this->ll_schema));
    }

    logline(file_off_t off,
            const struct timeval& tv,
            log_level_t lev,
            uint8_t mod = 0,
            uint8_t opid = 0)
        : ll_offset(off), ll_has_ansi(false), ll_opid(opid), ll_sub_offset(0),
          ll_valid_utf(1), ll_level(lev), ll_module_id(mod), ll_meta_mark(0),
          ll_expr_mark(0)
    {
        this->set_time(tv);
        memset(this->ll_schema, 0, sizeof(this->ll_schema));
    }

    /** @return The offset of the line in the file. */
    file_off_t get_offset() const { return this->ll_offset; }

    uint16_t get_sub_offset() const { return this->ll_sub_offset; }

    void set_sub_offset(uint16_t suboff) { this->ll_sub_offset = suboff; }

    /** @return The timestamp for the line. */
    time_t get_time() const { return this->ll_time; }

    void to_exttm(struct exttm& tm_out) const
    {
        tm_out.et_tm = *gmtime(&this->ll_time);
        tm_out.et_nsec = this->ll_millis * 1000 * 1000;
    }

    void set_time(time_t t) { this->ll_time = t; }

    /** @return The millisecond timestamp for the line. */
    uint16_t get_millis() const { return this->ll_millis; }

    void set_millis(uint16_t m) { this->ll_millis = m; }

    uint64_t get_time_in_millis() const
    {
        return (this->ll_time * 1000ULL + (uint64_t) this->ll_millis);
    }

    struct timeval get_timeval() const
    {
        struct timeval retval = {this->ll_time, this->ll_millis * 1000};

        return retval;
    }

    void set_time(const struct timeval& tv)
    {
        this->ll_time = tv.tv_sec;
        this->ll_millis = tv.tv_usec / 1000;
    }

    void set_ignore(bool val)
    {
        if (val) {
            this->ll_level |= LEVEL_IGNORE;
        } else {
            this->ll_level &= ~LEVEL_IGNORE;
        }
    }

    bool is_ignored() const { return this->ll_level & LEVEL_IGNORE; }

    void set_mark(bool val)
    {
        if (val) {
            this->ll_level |= LEVEL_MARK;
        } else {
            this->ll_level &= ~LEVEL_MARK;
        }
    }

    bool is_marked() const { return this->ll_level & LEVEL_MARK; }

    void set_meta_mark(bool val) { this->ll_meta_mark = val; }

    bool is_meta_marked() const { return this->ll_meta_mark; }

    void set_expr_mark(bool val) { this->ll_expr_mark = val; }

    bool is_expr_marked() const { return this->ll_expr_mark; }

    void set_time_skew(bool val)
    {
        if (val) {
            this->ll_level |= LEVEL_TIME_SKEW;
        } else {
            this->ll_level &= ~LEVEL_TIME_SKEW;
        }
    }

    bool is_time_skewed() const { return this->ll_level & LEVEL_TIME_SKEW; }

    void set_valid_utf(bool v) { this->ll_valid_utf = v; }

    bool is_valid_utf() const { return this->ll_valid_utf; }

    void set_has_ansi(bool v) { this->ll_has_ansi = v; }

    bool has_ansi() const { return this->ll_has_ansi; }

    /** @param l The logging level. */
    void set_level(log_level_t l) { this->ll_level = l; };

    /** @return The logging level. */
    log_level_t get_level_and_flags() const
    {
        return (log_level_t) this->ll_level;
    }

    log_level_t get_msg_level() const
    {
        return (log_level_t) (this->ll_level & ~LEVEL__FLAGS);
    }

    const char* get_level_name() const
    {
        return level_names[this->ll_level & ~LEVEL__FLAGS];
    }

    bool is_message() const
    {
        return (this->ll_level & (LEVEL_IGNORE | LEVEL_CONTINUED)) == 0;
    }

    bool is_continued() const { return this->ll_level & LEVEL_CONTINUED; }

    uint8_t get_module_id() const { return this->ll_module_id; }

    void set_opid(uint8_t opid) { this->ll_opid = opid; }

    uint8_t get_opid() const { return this->ll_opid; }

    bool match_opid_hash(unsigned long hash) const
    {
        struct {
            unsigned int value : 6;
        } reduced = {
            (unsigned int) hash,
        };

        return this->ll_opid == reduced.value;
    }

    /**
     * @return  True if there is a schema value set for this log line.
     */
    bool has_schema() const
    {
        return (this->ll_schema[0] != 0 || this->ll_schema[1] != 0);
    }

    /**
     * Set the "schema" for this log line.  The schema ID is used to match log
     * lines that have a similar format when generating the logline table.  The
     * schema is set lazily so that startup is faster.
     *
     * @param ba The SHA-1 hash of the constant parts of this log line.
     */
    void set_schema(const byte_array<2, uint64_t>& ba)
    {
        memcpy(this->ll_schema, ba.in(), sizeof(this->ll_schema));
    }

    char get_schema() const { return this->ll_schema[0]; }

    /**
     * Perform a partial match of the given schema against this log line.
     * Storing the full schema is not practical, so we just keep the first four
     * bytes.
     *
     * @param  ba The SHA-1 hash of the constant parts of a log line.
     * @return    True if the first four bytes of the given schema match the
     *   schema stored in this log line.
     */
    bool match_schema(const byte_array<2, uint64_t>& ba) const
    {
        return memcmp(this->ll_schema, ba.in(), sizeof(this->ll_schema)) == 0;
    }

    /**
     * Compare loglines based on their timestamp.
     */
    bool operator<(const logline& rhs) const
    {
        return (this->ll_time < rhs.ll_time)
            || (this->ll_time == rhs.ll_time && this->ll_millis < rhs.ll_millis)
            || (this->ll_time == rhs.ll_time && this->ll_millis == rhs.ll_millis
                && this->ll_offset < rhs.ll_offset)
            || (this->ll_time == rhs.ll_time && this->ll_millis == rhs.ll_millis
                && this->ll_offset == rhs.ll_offset
                && this->ll_sub_offset < rhs.ll_sub_offset);
    }

    bool operator<(const time_t& rhs) const { return this->ll_time < rhs; }

    bool operator<(const struct timeval& rhs) const
    {
        return ((this->ll_time < rhs.tv_sec)
                || ((this->ll_time == rhs.tv_sec)
                    && (this->ll_millis < (rhs.tv_usec / 1000))));
    }

    bool operator<=(const struct timeval& rhs) const
    {
        return ((this->ll_time < rhs.tv_sec)
                || ((this->ll_time == rhs.tv_sec)
                    && (this->ll_millis <= (rhs.tv_usec / 1000))));
    }

private:
    file_off_t ll_offset : 63;
    uint8_t ll_has_ansi : 1;
    time_t ll_time;
    unsigned int ll_millis : 10;
    unsigned int ll_opid : 6;
    unsigned int ll_sub_offset : 15;
    unsigned int ll_valid_utf : 1;
    uint8_t ll_level;
    uint8_t ll_module_id : 6;
    uint8_t ll_meta_mark : 1;
    uint8_t ll_expr_mark : 1;
    char ll_schema[2];
};

struct format_tag_def {
    explicit format_tag_def(std::string name) : ftd_name(std::move(name)) {}

    struct path_restriction {
        std::string p_glob;

        bool matches(const char* fn) const;
    };

    std::string ftd_name;
    std::string ftd_description;
    std::vector<path_restriction> ftd_paths;
    factory_container<lnav::pcre2pp::code, int>::with_default_args<PCRE2_DOTALL>
        ftd_pattern;
    log_level_t ftd_level{LEVEL_UNKNOWN};
};

struct format_partition_def {
    explicit format_partition_def(std::string name) : fpd_name(std::move(name))
    {
    }

    struct path_restriction {
        std::string p_glob;

        bool matches(const char* fn) const;
    };

    std::string fpd_name;
    std::string fpd_description;
    std::vector<path_restriction> fpd_paths;
    factory_container<lnav::pcre2pp::code, int>::with_default_args<PCRE2_DOTALL>
        fpd_pattern;
    log_level_t fpd_level{LEVEL_UNKNOWN};
};

#endif
