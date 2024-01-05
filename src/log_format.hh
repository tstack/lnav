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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file log_format.hh
 */

#ifndef log_format_hh
#define log_format_hh

#include <stdint.h>
#include <sys/time.h>
#include <time.h>
#define __STDC_FORMAT_MACROS
#include <limits>
#include <list>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <inttypes.h>
#include <sys/types.h>

#include "base/date_time_scanner.hh"
#include "base/intern_string.hh"
#include "base/lnav_log.hh"
#include "file_format.hh"
#include "highlighter.hh"
#include "line_buffer.hh"
#include "log_format_fwd.hh"
#include "log_level.hh"
#include "optional.hpp"
#include "pcrepp/pcre2pp.hh"
#include "shared_buffer.hh"

struct sqlite3;
class logfile;
class log_vtab_manager;
struct exec_context;

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
                       const nonstd::optional<log_format*>& format
                       = nonstd::nullopt)
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

    logline_value_meta& with_struct_name(intern_string_t name)
    {
        this->lvm_struct_name = name;
        return *this;
    }

    intern_string_t lvm_name;
    value_kind_t lvm_kind;
    column_t lvm_column{external_column{}};
    nonstd::optional<size_t> lvm_values_index;
    bool lvm_identifier{false};
    bool lvm_hidden{false};
    nonstd::optional<bool> lvm_user_hidden;
    bool lvm_from_module{false};
    intern_string_t lvm_struct_name;
    nonstd::optional<log_format*> lvm_format;
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
                  struct line_range origin);

    void apply_scaling(const scaling_factor* sf)
    {
        if (sf != nullptr) {
            switch (this->lv_meta.lvm_kind) {
                case value_kind_t::VALUE_INTEGER:
                    sf->scale(this->lv_value.i);
                    break;
                case value_kind_t::VALUE_FLOAT:
                    sf->scale(this->lv_value.d);
                    break;
                default:
                    break;
            }
        }
    }

    std::string to_string() const;

    const char* text_value() const
    {
        if (this->lv_str) {
            return this->lv_str->c_str();
        }
        if (this->lv_frag.empty()) {
            if (this->lv_intern_string.empty()) {
                return "";
            }
            return this->lv_intern_string.get();
        }
        return this->lv_frag.data();
    }

    size_t text_length() const
    {
        if (this->lv_str) {
            return this->lv_str->size();
        }
        if (this->lv_frag.empty()) {
            return this->lv_intern_string.size();
        }
        return this->lv_frag.length();
    }

    struct line_range origin_in_full_msg(const char* msg, ssize_t len) const;

    logline_value_meta lv_meta;
    union value_u {
        int64_t i;
        double d;

        value_u() : i(0) {}
        value_u(int64_t i) : i(i) {}
        value_u(double d) : d(d) {}
    } lv_value;
    nonstd::optional<std::string> lv_str;
    string_fragment lv_frag;
    int lv_sub_offset{0};
    intern_string_t lv_intern_string;
    struct line_range lv_origin;
};

struct logline_value_vector {
    void clear()
    {
        this->lvv_values.clear();
        this->lvv_sbr.disown();
        this->lvv_opid_value = nonstd::nullopt;
    }

    shared_buffer_ref lvv_sbr;
    std::vector<logline_value> lvv_values;
    nonstd::optional<std::string> lvv_opid_value;
};

struct logline_value_stats {
    logline_value_stats() { this->clear(); }

    void clear()
    {
        this->lvs_width = 0;
        this->lvs_count = 0;
        this->lvs_total = 0;
        this->lvs_min_value = std::numeric_limits<double>::max();
        this->lvs_max_value = -std::numeric_limits<double>::max();
    }

    void merge(const logline_value_stats& other);

    void add_value(double value);

    int64_t lvs_width;
    int64_t lvs_count;
    double lvs_total;
    double lvs_min_value;
    double lvs_max_value;
};

struct logline_value_cmp {
    explicit logline_value_cmp(
        const intern_string_t* name = nullptr,
        nonstd::optional<logline_value_meta::column_t> col = nonstd::nullopt)
        : lvc_name(name), lvc_column(col)
    {
    }

    bool operator()(const logline_value& lv) const
    {
        bool retval = true;

        if (this->lvc_name != nullptr) {
            retval = retval && ((*this->lvc_name) == lv.lv_meta.lvm_name);
        }
        if (this->lvc_column) {
            retval
                = retval && (this->lvc_column.value() == lv.lv_meta.lvm_column);
        }

        return retval;
    }

    const intern_string_t* lvc_name;
    nonstd::optional<logline_value_meta::column_t> lvc_column;
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
    static std::vector<std::shared_ptr<log_format>>& get_root_formats();

    static std::shared_ptr<log_format> find_root_format(const char* name);

    struct action_def {
        std::string ad_name;
        std::string ad_label;
        std::vector<std::string> ad_cmdline;
        bool ad_capture_output{false};

        bool operator<(const action_def& rhs) const
        {
            return this->ad_name < rhs.ad_name;
        }
    };

    virtual ~log_format() = default;

    virtual void clear()
    {
        this->lf_pattern_locks.clear();
        this->lf_date_time.clear();
        this->lf_time_scanner.clear();
    }

    /**
     * Get the name of this log format.
     *
     * @return The log format name.
     */
    virtual const intern_string_t get_name() const = 0;

    virtual bool match_name(const std::string& filename) { return true; }

    struct scan_match {
        uint32_t sm_quality;
    };

    struct scan_no_match {
        const char* snm_reason{nullptr};
    };

    struct scan_incomplete {};

    using scan_result_t
        = mapbox::util::variant<scan_match, scan_no_match, scan_incomplete>;

    /**
     * Scan a log line to see if it matches this log format.
     *
     * @param dst The vector of loglines that the formatter should append to
     *   if it detected a match.
     * @param offset The offset in the file where this line is located.
     * @param prefix The contents of the line.
     * @param len The length of the prefix string.
     */
    virtual scan_result_t scan(logfile& lf,
                               std::vector<logline>& dst,
                               const line_info& li,
                               shared_buffer_ref& sbr,
                               scan_batch_context& sbc)
        = 0;

    virtual bool scan_for_partial(shared_buffer_ref& sbr, size_t& len_out) const
    {
        return false;
    }

    /**
     * Remove redundant data from the log line string.
     *
     * XXX We should probably also add some attributes to the line here, so we
     * can highlight things like the date.
     *
     * @param line The log line to edit.
     */
    virtual void scrub(std::string& line) {}

    virtual void annotate(uint64_t line_number,
                          string_attrs_t& sa,
                          logline_value_vector& values,
                          bool annotate_module = true) const
    {
    }

    virtual void rewrite(exec_context& ec,
                         shared_buffer_ref& line,
                         string_attrs_t& sa,
                         std::string& value_out)
    {
        value_out.assign(line.get_data(), line.length());
    }

    virtual const logline_value_stats* stats_for_value(
        const intern_string_t& name) const
    {
        return nullptr;
    }

    virtual std::shared_ptr<log_format> specialized(int fmt_lock = -1) = 0;

    virtual std::shared_ptr<log_vtab_impl> get_vtab_impl() const
    {
        return nullptr;
    }

    virtual void get_subline(const logline& ll,
                             shared_buffer_ref& sbr,
                             bool full_message = false)
    {
    }

    virtual const std::vector<std::string>* get_actions(
        const logline_value& lv) const
    {
        return nullptr;
    }

    virtual std::set<std::string> get_source_path() const
    {
        std::set<std::string> retval;

        retval.insert("default");

        return retval;
    }

    virtual bool hide_field(const intern_string_t field_name, bool val)
    {
        return false;
    }

    virtual std::map<intern_string_t, logline_value_meta> get_field_states()
    {
        return {};
    }

    const char* const* get_timestamp_formats() const
    {
        if (this->lf_timestamp_format.empty()) {
            return nullptr;
        }

        return &this->lf_timestamp_format[0];
    }

    void check_for_new_year(std::vector<logline>& dst,
                            exttm log_tv,
                            timeval timeval1);

    virtual std::string get_pattern_path(uint64_t line_number) const;

    virtual intern_string_t get_pattern_name(uint64_t line_number) const;

    virtual std::string get_pattern_regex(uint64_t line_number) const
    {
        return "";
    }

    virtual std::vector<logline_value_meta> get_value_metadata() const
    {
        return {};
    }

    virtual bool format_changed() { return false; }

    struct pattern_for_lines {
        pattern_for_lines(uint32_t pfl_line, uint32_t pfl_pat_index);

        uint32_t pfl_line;
        int pfl_pat_index;
    };

    int last_pattern_index() const
    {
        if (this->lf_pattern_locks.empty()) {
            return -1;
        }

        return this->lf_pattern_locks.back().pfl_pat_index;
    }

    int pattern_index_for_line(uint64_t line_number) const;

    bool operator<(const log_format& rhs) const
    {
        return this->get_name() < rhs.get_name();
    }

    static bool name_lt(const std::shared_ptr<const log_format>& lhs,
                        const std::shared_ptr<const log_format>& rhs)
    {
        return intern_string_t::case_lt(lhs->get_name(), rhs->get_name());
    }

    enum class subsecond_unit {
        milli,
        micro,
        nano,
    };

    std::string lf_description;
    uint8_t lf_mod_index{0};
    bool lf_multiline{true};
    bool lf_structured{false};
    date_time_scanner lf_date_time;
    date_time_scanner lf_time_scanner;
    std::vector<pattern_for_lines> lf_pattern_locks;
    intern_string_t lf_timestamp_field{intern_string::lookup("timestamp", -1)};
    intern_string_t lf_subsecond_field;
    nonstd::optional<subsecond_unit> lf_subsecond_unit;
    intern_string_t lf_time_field;
    std::vector<const char*> lf_timestamp_format;
    unsigned int lf_timestamp_flags{0};
    std::map<std::string, action_def> lf_action_defs;
    std::vector<logline_value_stats> lf_value_stats;
    std::vector<highlighter> lf_highlighters;
    bool lf_is_self_describing{false};
    bool lf_time_ordered{true};
    bool lf_specialized{false};
    nonstd::optional<int64_t> lf_max_unrecognized_lines;
    std::map<const intern_string_t, std::shared_ptr<format_tag_def>>
        lf_tag_defs;

    struct opid_descriptor {
        positioned_property<intern_string_t> od_field;
        factory_container<lnav::pcre2pp::code> od_extractor;
        std::string od_prefix{" "};
        std::string od_suffix;
        std::string od_joiner{", "};

        nonstd::optional<std::string> matches(const string_fragment& sf) const;
    };

    struct opid_descriptors {
        std::shared_ptr<std::vector<opid_descriptor>> od_descriptors;

        std::string to_string(
            const lnav::map::small<size_t, std::string>& lod) const;
    };

    std::shared_ptr<std::map<intern_string_t, opid_descriptors>>
        lf_opid_description_def{
            std::make_shared<std::map<intern_string_t, opid_descriptors>>()};

    std::shared_ptr<std::map<intern_string_t, opid_descriptors>>
        lf_subid_description_def{
            std::make_shared<std::map<intern_string_t, opid_descriptors>>()};

    ArenaAlloc::Alloc<char> lf_desc_allocator{2 * 1024};

    using desc_field_set
        = robin_hood::unordered_set<intern_string_t,
                                    intern_hasher,
                                    std::equal_to<intern_string_t>>;

    desc_field_set lf_desc_fields;

    using desc_cap_map
        = robin_hood::unordered_map<intern_string_t,
                                    string_fragment,
                                    intern_hasher,
                                    std::equal_to<intern_string_t>>;
    desc_cap_map lf_desc_captures;

protected:
    static std::vector<std::shared_ptr<log_format>> lf_root_formats;

    struct pcre_format {
        template<typename T, std::size_t N>
        explicit pcre_format(const T (&regex)[N])
            : name(regex),
              pcre(lnav::pcre2pp::code::from_const(regex).to_shared()),
              pf_timestamp_index(this->pcre->name_index("timestamp"))
        {
        }

        pcre_format() = default;

        const char* name{nullptr};
        std::shared_ptr<lnav::pcre2pp::code> pcre;
        int pf_timestamp_index{-1};
    };

    static bool next_format(const pcre_format* fmt,
                            int& index,
                            int& locked_index);

    const char* log_scanf(uint32_t line_number,
                          string_fragment line,
                          const pcre_format* fmt,
                          const char* time_fmt[],
                          struct exttm* tm_out,
                          struct timeval* tv_out,

                          string_fragment* ts_out,
                          nonstd::optional<string_fragment>* level_out);
};

#endif
