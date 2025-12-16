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

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sys/types.h>
#include <time.h>

#include "ArenaAlloc/arenaalloc.h"
#include "base/file_range.hh"
#include "base/intern_string.hh"
#include "base/log_level_enum.hh"
#include "base/map_util.hh"
#include "base/small_string_map.hh"
#include "base/string_attr_type.hh"
#include "base/time_util.hh"
#include "byte_array.hh"
#include "digestible/digestible.h"
#include "log_level.hh"
#include "pcrepp/pcre2pp.hh"
#include "robin_hood/robin_hood.h"
#include "shared_buffer.hh"
#include "yajlpp/yajlpp.hh"

class log_format;

enum class timestamp_point_of_reference_t : uint8_t {
    send,
    start,
};

struct log_level_stats {
    uint32_t lls_error_count{0};
    uint32_t lls_warning_count{0};
    uint32_t lls_total_count{0};

    log_level_stats& operator|=(const log_level_stats& rhs);
    void update_msg_count(log_level_t lvl, int32_t amount = 1);
};

struct log_op_description {
    std::optional<size_t> lod_index;
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

    void clear();

    void close_sub_ops(const string_fragment& subid);

    bool operator<(const opid_time_range& rhs) const
    {
        return this->otr_range < rhs.otr_range;
    }

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

    log_opid_map::iterator insert_op(ArenaAlloc::Alloc<char>& alloc,
                                     const string_fragment& opid,
                                     const std::chrono::microseconds& us,
                                     timestamp_point_of_reference_t poref,
                                     std::chrono::microseconds duration
                                     = std::chrono::microseconds(0));

    opid_sub_time_range* sub_op_in_use(ArenaAlloc::Alloc<char>& alloc,
                                       log_opid_map::iterator& op_iter,
                                       const string_fragment& subid,
                                       const std::chrono::microseconds& us,
                                       log_level_t level);
};

struct thread_id_time_range {
    time_range titr_range;
    log_level_stats titr_level_stats;

    void clear();

    bool operator<(const thread_id_time_range& rhs) const
    {
        return this->titr_range < rhs.titr_range;
    }

    thread_id_time_range& operator|=(const thread_id_time_range& rhs);
};

using log_thread_id_map
    = robin_hood::unordered_map<string_fragment,
                                thread_id_time_range,
                                frag_hasher,
                                std::equal_to<string_fragment>>;

struct log_thread_id_state {
    log_thread_id_map ltis_tid_ranges;

    log_thread_id_map::iterator insert_tid(ArenaAlloc::Alloc<char>& alloc,
                                           const string_fragment& tid,
                                           const std::chrono::microseconds& us);
};

struct logline_value_stats {
    void merge(const logline_value_stats& other);

    void add_value(double value);

    int64_t lvs_width{0};
    int64_t lvs_count{0};
    double lvs_total{0};
    double lvs_min_value{std::numeric_limits<double>::max()};
    double lvs_max_value{-std::numeric_limits<double>::max()};
    digestible::tdigest<double> lvs_tdigest{200};
};

struct pattern_for_lines {
    pattern_for_lines(uint32_t pfl_line, uint32_t pfl_pat_index);

    uint32_t pfl_line;
    int pfl_pat_index;
};

struct pattern_locks {
    std::vector<pattern_for_lines> pl_lines;

    bool empty() const { return this->pl_lines.empty(); }

    int pattern_index_for_line(uint64_t line_number) const;

    int last_pattern_index() const
    {
        if (this->pl_lines.empty()) {
            return -1;
        }

        return this->pl_lines.back().pfl_pat_index;
    }
};

struct scan_batch_context {
    ArenaAlloc::Alloc<char>& sbc_allocator;
    pattern_locks& sbc_pattern_locks;
    std::vector<logline_value_stats> sbc_value_stats;
    log_opid_state sbc_opids;
    log_thread_id_state sbc_tids;
    lnav::small_string_map sbc_level_cache;
};

struct log_format_file_state {
    const std::vector<logline_value_stats>& lffs_value_stats;
    const pattern_locks& lffs_pattern_locks;
};

extern const string_attr_type<void> L_PREFIX;
extern const string_attr_type<void> L_TIMESTAMP;
extern const string_attr_type<void> L_LEVEL;
extern const string_attr_type<std::shared_ptr<logfile>> L_FILE;
extern const string_attr_type<bookmark_metadata*> L_PARTITION;
extern const string_attr_type<void> L_OPID;
extern const string_attr_type<bookmark_metadata*> L_META;

/**
 * Metadata for a single line in a log file.
 */
class logline {
public:
    /**
     * Construct a logline object with the given values.
     *
     * @param off The offset of the line in the file.
     * @param t The timestamp for the line.
     * @param millis The millisecond timestamp for the line.
     * @param l The logging level.
     */
    logline(file_off_t off, std::chrono::microseconds t, log_level_t lev)
        : ll_time(t), ll_offset(off), ll_sub_offset(0), ll_valid_utf(1),
          ll_has_ansi(false), ll_ignore(false), ll_continued(false),
          ll_time_skew(false), ll_mark(false), ll_meta_mark(0), ll_expr_mark(0),
          ll_level(lev)
    {
    }

    /** @return The offset of the line in the file. */
    file_off_t get_offset() const { return this->ll_offset; }

    uint16_t get_sub_offset() const { return this->ll_sub_offset; }

    logline& set_sub_offset(uint16_t suboff)
    {
        this->ll_sub_offset = suboff;
        return *this;
    }

    template<typename S>
    S get_time() const
    {
        return std::chrono::duration_cast<S>(this->ll_time);
    }

    template<typename S>
    S get_subsecond_time() const
    {
        static constexpr auto ONE_SEC = std::chrono::seconds(1);

        return std::chrono::duration_cast<S>(this->ll_time % ONE_SEC);
    }

    void to_exttm(struct exttm& tm_out) const
    {
        const auto secs = static_cast<time_t>(
            this->get_time<std::chrono::seconds>().count());

        gmtime_r(&secs, &tm_out.et_tm);
        tm_out.et_nsec
            = this->get_subsecond_time<std::chrono::nanoseconds>().count();
    }

    template<typename T>
    void set_time(T t)
    {
        this->ll_time
            = std::chrono::duration_cast<std::chrono::microseconds>(t);
    }

    timeval get_timeval() const
    {
        return timeval{
            static_cast<decltype(timeval::tv_sec)>(
                this->get_time<std::chrono::seconds>().count()),
            static_cast<decltype(timeval::tv_usec)>(
                this->get_subsecond_time<std::chrono::microseconds>().count()),
        };
    }

    void set_time(const timeval& tv) { this->ll_time = to_us(tv); }

    template<typename T>
    void set_subsecond_time(T sub)
    {
        this->ll_time
            += std::chrono::duration_cast<std::chrono::microseconds>(sub);
    }

    logline& set_ignore(bool val)
    {
        this->ll_ignore = val;
        return *this;
    }

    bool is_ignored() const { return this->ll_ignore; }

    logline& set_mark(bool val)
    {
        this->ll_mark = val;
        return *this;
    }

    bool is_marked() const { return this->ll_mark; }

    logline& set_meta_mark(bool val)
    {
        this->ll_meta_mark = val;
        return *this;
    }

    bool is_meta_marked() const { return this->ll_meta_mark; }

    logline& set_expr_mark(bool val)
    {
        this->ll_expr_mark = val;
        return *this;
    }

    bool is_expr_marked() const { return this->ll_expr_mark; }

    logline& set_time_skew(bool val)
    {
        this->ll_time_skew = val;
        return *this;
    }

    bool is_time_skewed() const { return this->ll_time_skew; }

    logline& set_valid_utf(bool v)
    {
        this->ll_valid_utf = v;
        return *this;
    }

    bool is_valid_utf() const { return this->ll_valid_utf; }

    logline& set_has_ansi(bool v)
    {
        this->ll_has_ansi = v;
        return *this;
    }

    bool has_ansi() const { return this->ll_has_ansi; }

    /** @param l The logging level. */
    void set_level(log_level_t l) { this->ll_level = l; };

    log_level_t get_msg_level() const { return log_level_t{this->ll_level}; }

    const string_fragment& get_level_name() const
    {
        return level_names[this->ll_level];
    }

    bool is_message() const { return !this->ll_ignore && !this->ll_continued; }

    logline& set_continued(bool val)
    {
        this->ll_continued = val;
        return *this;
    }

    bool is_continued() const { return this->ll_continued; }

    /**
     * Compare loglines based on their timestamp.
     */
    bool operator<(const logline& rhs) const
    {
        return (this->ll_time < rhs.ll_time)
            || (this->ll_time == rhs.ll_time && this->ll_offset < rhs.ll_offset)
            || (this->ll_time == rhs.ll_time && this->ll_offset == rhs.ll_offset
                && this->ll_sub_offset < rhs.ll_sub_offset);
    }

    bool operator<(const std::chrono::microseconds& rhs) const
    {
        return this->ll_time < rhs;
    }

    bool operator<(const timeval& rhs) const
    {
        return this->get_timeval() < rhs;
    }

    bool operator<=(const timeval& rhs) const
    {
        return this->get_timeval() <= rhs;
    }

    void set_schema_computed(bool val) { this->ll_has_schema = val; }

    bool has_schema() const { return this->ll_has_schema; }

    void merge_bloom_bits(uint64_t bloom_bits)
    {
        this->ll_bloom_bits |= bloom_bits;
    }

    bool match_bloom_bits(uint64_t bloom_bits) const
    {
        return (this->ll_bloom_bits & bloom_bits) == bloom_bits;
    }

    static constexpr size_t BLOOM_BITS_SIZE = 56;

private:
    std::chrono::microseconds ll_time;
    file_off_t ll_offset : 44;
    unsigned int ll_sub_offset : 15;
    uint8_t ll_valid_utf : 1;
    uint8_t ll_has_ansi : 1;
    uint8_t ll_ignore : 1;
    uint8_t ll_continued : 1;
    uint8_t ll_time_skew : 1;
    uint64_t ll_bloom_bits : 56;
    uint8_t ll_mark : 1;
    uint8_t ll_meta_mark : 1;
    uint8_t ll_expr_mark : 1;
    uint8_t ll_has_schema : 1;
    uint8_t ll_level : 4;
};

static_assert(sizeof(logline) == 24);

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

struct subline_options {
    friend bool operator==(const subline_options& lhs,
                           const subline_options& rhs)
    {
        return lhs.full_message == rhs.full_message
            && lhs.hash_hack == rhs.hash_hack
            && lhs.scrub_invalid_utf8 == rhs.scrub_invalid_utf8;
    }

    friend bool operator!=(const subline_options& lhs,
                           const subline_options& rhs)
    {
        return !(lhs == rhs);
    }

    bool full_message{false};
    bool hash_hack{false};
    bool scrub_invalid_utf8{true};
};

enum class value_kind_t : int {
    VALUE_UNKNOWN = -1,
    VALUE_NULL,
    VALUE_TEXT,
    VALUE_INTEGER,
    VALUE_FLOAT,
    VALUE_BOOLEAN,
    VALUE_JSON,
    VALUE_STRUCT,
    VALUE_QUOTED,
    VALUE_W3C_QUOTED,
    VALUE_TIMESTAMP,
    VALUE_XML,

    VALUE__MAX
};

enum class scale_op_t {
    SO_IDENTITY,
    SO_MULTIPLY,
    SO_DIVIDE
};

struct scaling_factor {
    template<typename T>
    void scale(T& val) const
    {
        switch (this->sf_op) {
            case scale_op_t::SO_IDENTITY:
                break;
            case scale_op_t::SO_DIVIDE:
                val = val / (T) this->sf_value;
                break;
            case scale_op_t::SO_MULTIPLY:
                val = val * (T) this->sf_value;
                break;
        }
    }

    scale_op_t sf_op{scale_op_t::SO_IDENTITY};
    double sf_value{1};
};

enum class chart_type_t {
    none,
    hist,
    spectro,
};

struct logline_value_meta {
    struct internal_column {
        bool operator==(const internal_column&) const { return true; }
    };
    struct external_column {
        bool operator==(const external_column&) const { return true; }
    };
    struct table_column {
        size_t value;

        bool operator==(const table_column& rhs) const
        {
            return this->value == rhs.value;
        }
    };

    using column_t
        = mapbox::util::variant<internal_column, external_column, table_column>;

    logline_value_meta(intern_string_t name,
                       value_kind_t kind,
                       column_t col = external_column{},
                       const std::optional<log_format*>& format = std::nullopt)
        : lvm_name(name), lvm_kind(kind), lvm_column(col), lvm_format(format)
    {
    }

    bool is_hidden() const
    {
        if (this->lvm_user_hidden) {
            return this->lvm_user_hidden.value();
        }
        return this->lvm_hidden;
    }

    bool is_numeric() const;

    logline_value_meta& with_struct_name(intern_string_t name)
    {
        this->lvm_struct_name = name;
        return *this;
    }

    chart_type_t to_chart_type() const;

    intern_string_t lvm_name;
    value_kind_t lvm_kind;
    column_t lvm_column{external_column{}};
    std::optional<size_t> lvm_values_index;
    bool lvm_identifier{false};
    bool lvm_foreign_key{false};
    bool lvm_hidden{false};
    std::optional<bool> lvm_user_hidden;
    intern_string_t lvm_struct_name;
    std::optional<log_format*> lvm_format;
};

class logline_value {
public:
    logline_value(logline_value_meta lvm) : lv_meta(std::move(lvm))
    {
        this->lv_meta.lvm_kind = value_kind_t::VALUE_NULL;
    }

    logline_value(logline_value_meta lvm, bool b)
        : lv_meta(std::move(lvm)), lv_value((int64_t) (b ? 1 : 0))
    {
        this->lv_meta.lvm_kind = value_kind_t::VALUE_BOOLEAN;
    }

    logline_value(logline_value_meta lvm, int64_t i)
        : lv_meta(std::move(lvm)), lv_value(i)
    {
        this->lv_meta.lvm_kind = value_kind_t::VALUE_INTEGER;
    }

    logline_value(logline_value_meta lvm, double i)
        : lv_meta(std::move(lvm)), lv_value(i)
    {
        this->lv_meta.lvm_kind = value_kind_t::VALUE_FLOAT;
    }

    logline_value(logline_value_meta lvm, string_fragment frag)
        : lv_meta(std::move(lvm)), lv_frag(frag)
    {
    }

    logline_value(logline_value_meta lvm, const intern_string_t val)
        : lv_meta(std::move(lvm)), lv_intern_string(val)
    {
    }

    logline_value(logline_value_meta lvm, std::string val)
        : lv_meta(std::move(lvm)), lv_str(std::move(val))
    {
    }

    logline_value(logline_value_meta lvm,
                  shared_buffer_ref& sbr,
                  line_range origin);

    void apply_scaling(const scaling_factor* sf);

    std::string to_string() const;

    string_fragment to_string_fragment(ArenaAlloc::Alloc<char>& alloc) const;

    const char* text_value() const;

    size_t text_length() const;

    string_fragment text_value_fragment() const;

    line_range origin_in_full_msg(const char* msg, ssize_t len) const;

    logline_value_meta lv_meta;
    union value_u {
        int64_t i;
        double d;

        value_u() : i(0) {}
        value_u(int64_t i) : i(i) {}
        value_u(double d) : d(d) {}
    } lv_value;
    std::optional<std::string> lv_str;
    string_fragment lv_frag;
    int lv_sub_offset{0};
    intern_string_t lv_intern_string;
    line_range lv_origin;
};

struct logline_value_vector {
    enum class opid_provenance : uint8_t {
        none,
        file,
        user,
    };

    void clear();

    logline_value_vector() = default;

    logline_value_vector(const logline_value_vector& other);

    logline_value_vector& operator=(const logline_value_vector& other);

    shared_buffer_ref lvv_sbr;
    std::vector<logline_value> lvv_values;
    std::optional<std::string> lvv_opid_value;
    opid_provenance lvv_opid_provenance{opid_provenance::none};
    std::optional<std::string> lvv_thread_id_value;
};

#endif
