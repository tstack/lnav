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
 * @file log_format_ext.hh
 */

#ifndef lnav_log_format_ext_hh
#define lnav_log_format_ext_hh

#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/separated_string.hh"
#include "hasher.hh"
#include "log_format.hh"
#include "log_search_table_fwd.hh"
#include "styling.hh"
#include "yajlpp/yajlpp.hh"

class external_log_format : public log_format {
public:
    struct highlighter_def {
        factory_container<lnav::pcre2pp::code> hd_pattern;
        positioned_property<intern_string_t> hd_field;
        style_config hd_base_style;
        std::map<intern_string_t, style_config> hd_capture_styles;
    };

    struct value_def {
        value_def(intern_string_t name,
                  value_kind_t kind,
                  logline_value_meta::column_t col,
                  log_format* format)
            : vd_meta(name, kind, col, format)
        {
        }

        void set_rewrite_src_name()
        {
            this->vd_rewrite_src_name = intern_string::lookup(
                fmt::format(FMT_STRING("{}:{}"),
                            this->vd_meta.lvm_format.value()->get_name(),
                            this->vd_meta.lvm_name));
        }

        logline_value_meta vd_meta;
        std::string vd_collate;
        intern_string_t vd_unit_field;
        std::map<const intern_string_t, scaling_factor> vd_unit_scaling;
        bool vd_internal{false};
        std::vector<std::string> vd_action_list;
        std::string vd_rewriter;
        std::string vd_description;
        intern_string_t vd_rewrite_src_name;
        std::optional<size_t> vd_line_format_index;
        bool vd_is_desc_field{false};
        /**
         * Whether this value names one of the fields the opid description
         * hashes together, which is what the tabular scan checks per cell.
         * Narrower than `vd_is_desc_field`, which also covers the subid
         * description fields.
         */
        bool vd_is_opid_desc_field{false};
        std::map<const intern_string_t, highlighter_def>
            vd_highlighter_patterns;
    };

    struct indexed_value_def {
        indexed_value_def(int index = -1,
                          int unit_index = -1,
                          std::shared_ptr<value_def> vd = nullptr)
            : ivd_index(index), ivd_unit_field_index(unit_index),
              ivd_value_def(std::move(vd))
        {
        }

        int ivd_index;
        int ivd_unit_field_index;
        std::shared_ptr<value_def> ivd_value_def;

        bool operator<(const indexed_value_def& rhs) const
        {
            return this->ivd_index < rhs.ivd_index;
        }
    };

    struct pattern {
        intern_string_t p_name;
        std::string p_config_path;
        factory_container<lnav::pcre2pp::code,
                          int>::with_default_args<PCRE2_DOTALL>
            p_pcre;
        std::vector<indexed_value_def> p_value_by_index;
        std::map<intern_string_t, int> p_value_name_to_index;
        std::vector<int> p_numeric_value_indexes;
        int p_timestamp_field_index{-1};
        int p_time_field_index{-1};
        int p_level_field_index{-1};
        int p_opid_field_index{-1};
        int p_subid_field_index{-1};
        int p_body_field_index{-1};
        int p_thread_id_field_index{-1};
        int p_src_file_field_index{-1};
        int p_src_line_field_index{-1};
        int p_duration_field_index{-1};
        int p_start_timestamp_field_index{-1};
        int p_timestamp_end{-1};
        std::vector<int> p_opid_description_field_indexes;
        std::set<size_t> p_matched_samples;
        /**
         * For a name used by more than one group, the lowest group number
         * paired with the other group numbers with that name.
         */
        std::vector<std::pair<int, std::vector<int>>> p_dup_captures;

        void coalesce_dups(lnav::pcre2pp::match_data& md) const
        {
            for (const auto& [dst, srcs] : this->p_dup_captures) {
                for (const auto src : srcs) {
                    md.coalesce(dst, src);
                }
            }
        }
    };

    struct level_pattern {
        factory_container<lnav::pcre2pp::code> lp_pcre;
    };

    external_log_format(const intern_string_t name)
        : elf_level_field(intern_string::lookup("level", -1)),
          elf_body_field(intern_string::lookup("body", -1)),
          jlf_yajl_handle(nullptr, yajl_handle_deleter()), elf_name(name)
    {
        this->jlf_line_offsets.reserve(128);
    }

    const intern_string_t get_name() const override { return this->elf_name; }

    match_name_result match_name(const std::string& filename) override;

    scan_result_t test_line(
        sample_t& sample,
        std::vector<lnav::console::user_message>& msgs) override;

    scan_result_t scan(logfile& lf,
                       std::vector<logline>& dst,
                       const line_info& offset,
                       shared_buffer_ref& sbr,
                       scan_batch_context& sbc) override;

    bool scan_for_partial(const log_format_file_state& lffs,
                          shared_buffer_ref& sbr,
                          size_t& len_out) const override;

    void annotate(logfile* lf,
                  uint64_t line_number,
                  string_attrs_t& sa,
                  logline_value_vector& values) const override;

    void rewrite(exec_context& ec,
                 shared_buffer_ref& line,
                 string_attrs_t& sa,
                 std::string& value_out) override;

    void build(std::vector<lnav::console::user_message>& errors);

    void register_vtabs(log_vtab_manager* vtab_manager,
                        std::vector<lnav::console::user_message>& errors);

    bool match_samples(const std::vector<sample_t>& samples) const;

    bool apply_hide_field(const intern_string_t field_name,
                          bool val) override;

    std::map<intern_string_t, logline_value_meta> get_field_states() override;

    std::shared_ptr<log_format> specialized(scan_batch_context& sbc,
                                            int fmt_lock) override;

    std::optional<size_t> stats_index_for_value(
        const intern_string_t& name) const override;

    void get_subline(const log_format_file_state& lffs,
                     const logline& ll,
                     size_t sub_line,
                     shared_buffer_ref& sbr,
                     subline_options opts) override;

    bool has_cached_subline(const logline& ll,
                            subline_options opts) const override;

    std::shared_ptr<log_vtab_impl> get_vtab_impl() const override;

    const std::vector<std::string>* get_actions(
        const logline_value& lv) const override
    {
        const std::vector<std::string>* retval = nullptr;

        const auto iter = this->elf_value_defs.find(lv.lv_meta.lvm_name);
        if (iter != this->elf_value_defs.end()) {
            retval = &iter->second->vd_action_list;
        }

        return retval;
    }

    bool format_changed() override;

    std::set<std::string> get_source_path() const override
    {
        return this->elf_source_path;
    }

    std::vector<logline_value_meta> get_value_metadata() const override;

    enum class json_log_field {
        CONSTANT,
        VARIABLE
    };

    struct json_format_element {
        enum class align_t {
            NONE,
            LEFT,
            RIGHT,
        };

        enum class overflow_t {
            ABBREV,
            TRUNCATE,
            DOTDOT,
            LASTWORD,
        };

        enum class transform_t {
            NONE,
            UPPERCASE,
            LOWERCASE,
            CAPITALIZE,
        };

        json_log_field jfe_type{json_log_field::CONSTANT};
        positioned_property<intern_string_t> jfe_value;
        std::string jfe_default_value{"-"};
        size_t jfe_default_value_line_feeds{0};
        bool jfe_use_default_for_empty{false};
        long long jfe_min_width{0};
        bool jfe_auto_width{false};
        long long jfe_max_width{LLONG_MAX};
        align_t jfe_align{align_t::NONE};
        overflow_t jfe_overflow{overflow_t::ABBREV};
        transform_t jfe_text_transform{transform_t::NONE};
        std::string jfe_ts_format;
        std::string jfe_prefix;
        size_t jfe_prefix_line_feeds{0};
        std::string jfe_suffix;
        size_t jfe_suffix_line_feeds{0};
    };

    struct json_field_cmp {
        json_field_cmp(json_log_field type, const intern_string_t name)
            : jfc_type(type), jfc_field_name(name)
        {
        }

        bool operator()(const json_format_element& jfe) const
        {
            return (this->jfc_type == jfe.jfe_type
                    && this->jfc_field_name == jfe.jfe_value.pp_value);
        }

        json_log_field jfc_type;
        const intern_string_t jfc_field_name;
    };

    struct value_line_count_result {
        size_t vlcr_count{1};
        size_t vlcr_line_format_count{0};
        bool vlcr_has_ansi{false};
        bool vlcr_valid_utf{true};
        std::optional<size_t> vlcr_line_format_index;
    };

    value_line_count_result value_line_count(scan_batch_context& sbc,
                                             const value_def* vd,
                                             bool top_level,
                                             std::optional<double> val,
                                             const unsigned char* str,
                                             ssize_t len,
                                             yajl_string_props_t* props);

    bool has_value_def(const intern_string_t ist) const
    {
        const auto iter = this->elf_value_defs.find(ist);

        return iter != this->elf_value_defs.end();
    }

    std::string get_pattern_path(const pattern_locks& pl,
                                 uint64_t line_number) const override
    {
        if (this->lf_file_type != file_type_t::TEXT) {
            return "structured";
        }
        auto pat_index = pl.pattern_index_for_line(line_number);
        return this->elf_pattern_order[pat_index]->p_config_path;
    }

    intern_string_t get_pattern_name(const pattern_locks& pl,
                                     uint64_t line_number) const override;

    std::string get_pattern_regex(const pattern_locks& pl,
                                  uint64_t line_number) const override;

    bool pattern_has_level(int pat_index) const override;

    log_level_t convert_level(string_fragment str,
                              scan_batch_context* sbc) const;

    static std::vector<std::shared_ptr<external_log_format>>
        GRAPH_ORDERED_FORMATS;

    std::set<std::string> elf_source_path;
    std::vector<std::filesystem::path> elf_format_source_order;
    std::map<intern_string_t, int> elf_format_sources;
    std::list<intern_string_t> elf_collision;
    factory_container<lnav::pcre2pp::code> elf_filename_pcre;
    std::map<std::string, std::shared_ptr<pattern>> elf_patterns;
    std::vector<std::shared_ptr<pattern>> elf_pattern_order;
    std::vector<sample_t> elf_samples;
    std::unordered_map<const intern_string_t, std::shared_ptr<value_def>>
        elf_value_defs;

    struct value_defs_state {
        size_t vds_generation{0};
    };

    std::shared_ptr<value_defs_state> elf_value_defs_state{
        std::make_shared<value_defs_state>()};
    value_defs_state elf_specialized_value_defs_state;

    std::vector<std::shared_ptr<value_def>> elf_value_def_order;
    robin_hood::unordered_map<string_fragment,
                              value_def*,
                              frag_hasher,
                              std::equal_to<string_fragment>,
                              50>
        elf_value_def_frag_map;
    std::vector<std::pair<string_fragment, value_def*>>
        elf_value_def_read_order;
    ArenaAlloc::Alloc<char> elf_allocator{4096};
    std::vector<std::shared_ptr<value_def>> elf_numeric_value_defs;
    size_t elf_column_count{0};
    double elf_timestamp_divisor{1.0};
    intern_string_t elf_level_field;
    factory_container<lnav::pcre2pp::code> elf_level_pointer;
    intern_string_t elf_body_field;
    intern_string_t elf_opid_field;
    intern_string_t elf_subid_field;
    intern_string_t elf_thread_id_field;
    intern_string_t elf_src_file_field;
    intern_string_t elf_src_line_field;
    intern_string_t elf_src_loc_field;
    intern_string_t elf_duration_field;
    double elf_duration_divisor{1.0};
    std::map<log_level_t, level_pattern> elf_level_patterns;
    std::vector<std::pair<int64_t, log_level_t>> elf_level_pairs;
    bool elf_builtin_format{false};

    struct header_exprs {
        std::map<std::string, std::string> he_exprs;
    };

    struct header {
        header_exprs h_exprs;
        size_t h_size{32};
    };

    struct converter {
        std::string c_type;
        header c_header;
        positioned_property<std::string> c_command;
    };

    converter elf_converter;

    using search_table_pcre2pp
        = factory_container<lnav::pcre2pp::code, int>::with_default_args<
            log_search_table_ns::PATTERN_OPTIONS>;

    struct search_table_def {
        search_table_pcre2pp std_pattern;
        std::string std_glob;
        log_level_t std_level{LEVEL_UNKNOWN};
    };

    std::map<intern_string_t, search_table_def> elf_search_tables;
    std::map<const intern_string_t, highlighter_def> elf_highlighter_patterns;

    /**
     * For a format whose lines are each one metric sample: the fields that
     * hold the metric's name and the sample's value.  Both are empty for any
     * other format.
     */
    struct metric_def {
        intern_string_t md_name_field;
        intern_string_t md_value_field;
        /** The field that says what kind of metric a sample is of. */
        intern_string_t md_kind_field;
        /**
         * The values of md_kind_field for a counter, whose samples are
         * running totals.  Every other sample is of a gauge.
         */
        std::vector<std::string> md_counter_kinds;
        std::vector<metric_display_def> md_display;

        bool is_defined() const
        {
            return !this->md_name_field.empty()
                || !this->md_value_field.empty()
                || !this->md_kind_field.empty()
                || !this->md_counter_kinds.empty()
                || !this->md_display.empty();
        }

        /** True if the format shows any entry of md_display by default. */
        bool has_shown_entry() const
        {
            for (const auto& mdd : this->md_display) {
                if (!mdd.mdd_hidden && !mdd.is_pattern()) {
                    return true;
                }
            }
            return false;
        }

        /** What md_display says about how a metric's values are shown. */
        struct display_settings {
            metric_range ds_range;
            intern_string_t ds_unit_suffix;
            std::optional<double> ds_unit_divisor;
        };

        /**
         * Each setting is from the first entry of md_display with a name
         * that matches the metric and has it, or else the first entry with
         * a pattern that does.
         */
        display_settings display_settings_for(string_fragment name) const
        {
            display_settings retval;
            auto take = [&retval](const metric_display_def& mdd) {
                if (!retval.ds_range.mr_min) {
                    retval.ds_range.mr_min = mdd.mdd_min;
                }
                if (!retval.ds_range.mr_max) {
                    retval.ds_range.mr_max = mdd.mdd_max;
                }
                if (retval.ds_unit_suffix.empty()) {
                    retval.ds_unit_suffix = mdd.mdd_unit_suffix;
                }
                if (!retval.ds_unit_divisor) {
                    retval.ds_unit_divisor = mdd.mdd_unit_divisor;
                }
            };
            for (const auto pattern : {false, true}) {
                for (const auto& mdd : this->md_display) {
                    if (mdd.is_pattern() == pattern && mdd.matches(name)) {
                        take(mdd);
                    }
                }
            }
            return retval;
        }

        bool is_counter_kind(string_fragment kind) const
        {
            for (const auto& ck : this->md_counter_kinds) {
                if (kind == ck) {
                    return true;
                }
            }
            return false;
        }
    };

    metric_def elf_metric;

    /**
     * What the user changed about the metrics shown on the line, with
     * :hide-fields and :show-fields.  Shared with the specialized copies.
     */
    struct metric_display_state {
        /** The user's choice for each entry of md_display, if any. */
        std::vector<std::optional<bool>> mds_entry_hidden;
        /** Metrics the user asked to see that md_display doesn't have. */
        std::vector<metric_display_def> mds_extra;
        /** Metrics the user hid when there is no md_display. */
        std::vector<metric_display_def> mds_hidden;
        /**
         * The entries of md_display that aren't hidden by the format or the
         * user, then mds_extra.
         */
        std::vector<metric_display_def> mds_shown;
        /** Bumped by update_metric_display(). */
        size_t mds_generation{0};
    };

    std::shared_ptr<metric_display_state> elf_metric_display_state{
        std::make_shared<metric_display_state>()};

    /** Rebuilds mds_shown after a change. */
    void update_metric_display();

    bool is_metric_hidden(string_fragment name) const override;

    bool is_metric_shown(string_fragment name) const override;

    size_t get_metric_display_generation() const override
    {
        return this->elf_metric_display_state->mds_generation;
    }

    metric_range get_metric_range(string_fragment name) const override;

    /**
     * Gives the meta of a sample of the named metric the unit that its
     * md_display entry has, if any.
     */
    void apply_metric_unit(logline_value_meta& meta) const;

    void reset_user_field_state() override;

    const std::vector<logline_value>& get_metric_values(
        const logline_value_vector& values,
        std::vector<logline_value>& scratch) const override;

    size_t get_metric_value_count() const override;

    bool metrics_are_columns() const override
    {
        return !this->elf_metric.is_defined();
    }

    bool is_metric_sample_field(intern_string_t name) const override
    {
        return this->elf_metric.is_defined()
            && (name == this->elf_metric.md_name_field
                || name == this->elf_metric.md_value_field
                || name == this->elf_metric.md_kind_field);
    }

    std::optional<logline_value_meta> get_metric_meta(
        intern_string_t name) const override;

    const std::vector<metric_display_def>* get_metric_display() const override
    {
        // With nothing to show by default, the line shows the metrics that
        // vary instead, along with any the user shows (is_metric_shown()).
        if (!this->elf_metric.has_shown_entry()) {
            return nullptr;
        }
        return &this->elf_metric_display_state->mds_shown;
    }

    scan_result_t scan_json(std::vector<logline>& dst,
                            const line_info& li,
                            shared_buffer_ref& sbr,
                            scan_batch_context& sbc);

    scan_result_t scan_tabular(logfile& lf,
                               std::vector<logline>& dst,
                               const line_info& li,
                               shared_buffer_ref& sbr,
                               scan_batch_context& sbc);

    enum class timestamp_outcome {
        ok,  // log_tv & log_time_tm populated; lf_timestamp_flags updated
        relock_mismatch,  // unlock + rescan succeeded, but the flag set
                          // changed; caller may want to bail
        no_parse,  // unable to parse, even after unlock + rescan
    };

    timestamp_outcome ingest_timestamp(string_fragment ts_sf,
                                       const logfile* lf,
                                       std::vector<logline>& dst,
                                       exttm& log_time_tm,
                                       timeval& log_tv,
                                       scan_batch_context& sbc);

    log_opid_map::iterator record_opid(const string_key& opid_cap,
                                       std::chrono::microseconds duration,
                                       std::chrono::microseconds log_us,
                                       log_level_t level,
                                       scan_batch_context& sbc);

    struct line_finalize_inputs {
        std::optional<string_fragment> lfi_opid_cap;
        std::optional<string_fragment> lfi_tid_cap;
        std::optional<string_fragment> lfi_duration_cap;
        std::optional<string_fragment> lfi_start_ts_cap;
        std::optional<string_fragment> lfi_src_file_cap;
        std::optional<string_fragment> lfi_src_line_cap;
        /**
         * The opid description fields, hashed as the scan walks them, so that
         * no container is needed to hold them.  Only meaningful when
         * `lfi_has_opid_desc` is set.
         */
        hasher lfi_opid_desc_hash;
        bool lfi_has_opid_desc{false};
        string_fragment lfi_line_sf;
        // Scratch buffer for a synthesized opid; `lfi_opid_cap` is pointed
        // at this when `finalize_line` synthesizes one.  Caller's struct
        // must outlive the iter returned by `finalize_line`.
        char lfi_synth_opid_buf[hasher::STRING_SIZE];
        bool lfi_terminated{true};
    };

    /**
     * Process the captured fields in a line and update various tables/stats.
     *
     * @param new_line The logline to finalize
     * @param in The collection of captures to process
     * @param sbc The logfile's batch context
     * @return The iterator for the inserted opid.
     */
    std::optional<log_opid_map::iterator> finalize_line(
        logline& new_line, line_finalize_inputs& in, scan_batch_context& sbc);

    void update_op_description(const std::vector<opid_descriptors*>& desc_def,
                               log_op_description& lod,
                               const pattern* fpat,
                               const lnav::pcre2pp::match_data& md);

    void update_op_description(const std::vector<opid_descriptors*>& desc_def,
                               log_op_description& lod,
                               const desc_cap_map& desc_caps);

    void json_append_to_cache(const char* value, ssize_t len)
    {
        if (len <= 0) {
            return;
        }

        this->jlf_attr_line.al_string.append(value, len);
    }

    void json_append_to_cache(const string_fragment& sf)
    {
        this->json_append_to_cache(sf.data(), sf.length());
    }

    void json_append_to_cache(ssize_t len)
    {
        if (len <= 0) {
            return;
        }
        this->jlf_attr_line.al_string.append(len, ' ');
    }

    void json_append(const log_format_file_state& sbc,
                     const json_format_element& jfe,
                     const value_def* vd,
                     const string_fragment& sf);

    logline_value_meta get_value_meta(intern_string_t field_name,
                                      value_kind_t kind) const;

    logline_value_meta get_value_meta(yajlpp_parse_context* ypc,
                                      const value_def* vd,
                                      value_kind_t kind);

    std::vector<lnav::console::snippet> get_snippets() const;

    bool jlf_hide_extra{false};
    std::vector<json_format_element> jlf_line_format;
    int jlf_line_format_init_count{0};
    logline_value_vector jlf_line_values;

    off_t jlf_cached_offset{-1};
    /**
     * Whether the input the cached rendering was made from was valid UTF-8:
     * all of the message's lines, as read, not just its first.  The
     * rendering keeps valid UTF-8 valid (escapes decode to valid characters,
     * overflow cuts on character boundaries, transforms touch only ASCII), so
     * this is what its buffers say.
     */
    bool jlf_cached_valid_utf{true};
    line_range jlf_cached_sub_range;
    subline_options jlf_cached_opts{};
    std::vector<off_t> jlf_line_offsets;
    std::vector<bool> jlf_used_values;
    attr_line_t jlf_attr_line;
    std::shared_ptr<yajlpp_parse_context> jlf_parse_context;
    std::shared_ptr<yajl_handle_t> jlf_yajl_handle;
    shared_buffer jlf_share_manager;

    /**
     * Scratch a JSON scan parses into.  A specialized copy belongs to one
     * file and parks its values on the format for the render pass to read
     * back; a root is shared by every file being probed against it, so a
     * candidate parses into a per-thread instance of this instead, which
     * nothing reads back.
     */
    struct json_scan_scratch {
        logline_value_vector jss_line_values;
        desc_cap_map jss_desc_captures;
        ArenaAlloc::Alloc<char> jss_desc_allocator{2 * 1024};
        std::shared_ptr<yajlpp_parse_context> jss_parse_context;
        std::shared_ptr<yajl_handle_t> jss_yajl_handle{
            nullptr, yajl_handle_deleter()};
        /**
         * The column order a probed line presents, and the names it points
         * at.  Reset at the top of every probe: the memo is positional and
         * keyed only on the field name, so carrying it from one candidate
         * format to the next would hand back the wrong value_def.
         */
        std::vector<std::pair<string_fragment, value_def*>> jss_read_order;
        ArenaAlloc::Alloc<char> jss_field_allocator{4096};

        /**
         * Build the parser on first use.  Nothing in it survives a line --
         * scan_json() resets the handle and rebinds the handler and the
         * userdata every call -- so one serves every candidate format, and
         * building it once per thread rather than once per probed line is
         * what keeps detection cheap.
         */
        void ensure_parser();
    };

    /**
     * What an external format works out from the file it is reading: the
     * shape of a tabular header, and the column order it presents.  Held
     * here while the format is only a candidate, because detection runs
     * every file against the same shared root.  @see format_scan_state
     */
    struct external_scan_state : format_scan_state {
        std::vector<std::pair<string_fragment, value_def*>>
            ess_value_def_read_order;
        file_ssize_t ess_header_end{0};
        char ess_separator{','};
        size_t ess_extra_count{0};
    };

    std::unique_ptr<format_scan_state> make_scan_state() const override
    {
        return std::make_unique<external_scan_state>();
    }

    void adopt_scan_state(format_scan_state& fss) override;

    file_ssize_t tlf_header_end{0};
    char tlf_separator{','};
    size_t tlf_extra_count{0};
    std::optional<separated_string::resume_state> tlf_suspended_state;
    size_t tlf_sub_lines{0};

private:
    const intern_string_t elf_name;

    void rewrite_tabular_subline(const log_format_file_state& lffs,
                                 const logline& ll,
                                 shared_buffer_ref& sbr,
                                 subline_options opts);

    void compute_subline_offsets();

    void share_rewritten_subline(const logline& ll,
                                 size_t sub_line,
                                 shared_buffer_ref& sbr,
                                 subline_options opts);

    void apply_text_transform(size_t begin_size,
                              json_format_element::transform_t transform);

    bool emit_overflow(const log_format_file_state& lffs,
                       const json_format_element& jfe,
                       const value_def* vd,
                       std::string& str);

    void emit_detail_block(const std::vector<bool>& used_values,
                           int& sub_offset);

    void render_line_format(const log_format_file_state& lffs,
                            const logline& ll,
                            subline_options opts,
                            const exttm* ts_extra_bits,
                            std::vector<bool>& used_values,
                            int& sub_offset);

    /**
     * Where the annotation side accumulates the opid description fields for a
     * row.  The scan side hashes the same cells through line_finalize_inputs;
     * the two ids have to come out equal, since the log view finds an op's
     * lines by matching log_opid against the all_opids key.
     */
    struct opid_desc_accum {
        hasher oda_hash;
        bool oda_any{false};
        bool oda_has_duration{false};
        bool oda_has_start_ts{false};
    };

    void process_csv_cell(logline_value_vector& values,
                          string_attrs_t* sa,
                          const separated_string::iterator& it,
                          shared_buffer_ref& sbr,
                          opid_desc_accum* opid_desc = nullptr) const;

    /**
     * Set the synthesized opid on a tabular row, from the description fields
     * process_csv_cell() just gathered, or from the whole row when the format
     * synthesizes off a duration instead.  Mirrors finalize_line().
     *
     * @param row_sf The row with ANSI escapes erased -- the same bytes
     *               finalize_line() hashes.
     */
    void synthesize_tabular_opid(logline_value_vector& values,
                                 opid_desc_accum& opid_desc,
                                 string_fragment row_sf) const;
};

#endif
