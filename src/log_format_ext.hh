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

#include <unordered_map>

#include "log_format.hh"
#include "log_search_table_fwd.hh"
#include "yajlpp/yajlpp.hh"

class module_format;

class external_log_format : public log_format {
public:
    struct sample {
        positioned_property<std::string> s_line;
        std::string s_description;
        log_level_t s_level{LEVEL_UNKNOWN};
        std::set<std::string> s_matched_regexes;
    };

    struct value_def {
        value_def(intern_string_t name,
                  value_kind_t kind,
                  int col,
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
        bool vd_foreign_key{false};
        intern_string_t vd_unit_field;
        std::map<const intern_string_t, scaling_factor> vd_unit_scaling;
        bool vd_internal{false};
        std::vector<std::string> vd_action_list;
        std::string vd_rewriter;
        std::string vd_description;
        intern_string_t vd_rewrite_src_name;
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
        std::vector<int> p_numeric_value_indexes;
        int p_timestamp_field_index{-1};
        int p_time_field_index{-1};
        int p_level_field_index{-1};
        int p_module_field_index{-1};
        int p_opid_field_index{-1};
        int p_body_field_index{-1};
        int p_timestamp_end{-1};
        bool p_module_format{false};
        std::set<size_t> p_matched_samples;
    };

    struct level_pattern {
        factory_container<lnav::pcre2pp::code> lp_pcre;
    };

    struct yajl_handle_deleter {
        void operator()(yajl_handle handle) const
        {
            if (handle != nullptr) {
                yajl_free(handle);
            }
        }
    };

    external_log_format(const intern_string_t name)
        : elf_level_field(intern_string::lookup("level", -1)),
          elf_body_field(intern_string::lookup("body", -1)),
          jlf_yajl_handle(nullptr, yajl_handle_deleter()), elf_name(name)
    {
        this->jlf_line_offsets.reserve(128);
    }

    const intern_string_t get_name() const { return this->elf_name; }

    bool match_name(const std::string& filename);

    bool match_mime_type(const file_format_t ff) const;

    scan_result_t scan(logfile& lf,
                       std::vector<logline>& dst,
                       const line_info& offset,
                       shared_buffer_ref& sbr,
                       scan_batch_context& sbc);

    bool scan_for_partial(shared_buffer_ref& sbr, size_t& len_out) const;

    void annotate(uint64_t line_number,
                  string_attrs_t& sa,
                  logline_value_vector& values,
                  bool annotate_module = true) const;

    void rewrite(exec_context& ec,
                 shared_buffer_ref& line,
                 string_attrs_t& sa,
                 std::string& value_out);

    void build(std::vector<lnav::console::user_message>& errors);

    void register_vtabs(log_vtab_manager* vtab_manager,
                        std::vector<lnav::console::user_message>& errors);

    bool match_samples(const std::vector<sample>& samples) const;

    bool hide_field(const intern_string_t field_name, bool val)
    {
        auto vd_iter = this->elf_value_defs.find(field_name);

        if (vd_iter == this->elf_value_defs.end()) {
            return false;
        }

        vd_iter->second->vd_meta.lvm_user_hidden = val;
        return true;
    }

    std::shared_ptr<log_format> specialized(int fmt_lock);

    const logline_value_stats* stats_for_value(
        const intern_string_t& name) const
    {
        auto iter = this->elf_value_defs.find(name);
        if (iter != this->elf_value_defs.end()
            && iter->second->vd_meta.lvm_values_index)
        {
            return &this->lf_value_stats[iter->second->vd_meta.lvm_values_index
                                             .value()];
        }

        return nullptr;
    }

    void get_subline(const logline& ll,
                     shared_buffer_ref& sbr,
                     bool full_message);

    std::shared_ptr<log_vtab_impl> get_vtab_impl() const;

    const std::vector<std::string>* get_actions(const logline_value& lv) const
    {
        const std::vector<std::string>* retval = nullptr;

        const auto iter = this->elf_value_defs.find(lv.lv_meta.lvm_name);
        if (iter != this->elf_value_defs.end()) {
            retval = &iter->second->vd_action_list;
        }

        return retval;
    }

    std::set<std::string> get_source_path() const
    {
        return this->elf_source_path;
    }

    std::vector<logline_value_meta> get_value_metadata() const;

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
        long long jfe_min_width{0};
        bool jfe_auto_width{false};
        long long jfe_max_width{LLONG_MAX};
        align_t jfe_align{align_t::NONE};
        overflow_t jfe_overflow{overflow_t::ABBREV};
        transform_t jfe_text_transform{transform_t::NONE};
        std::string jfe_ts_format;
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

    struct highlighter_def {
        factory_container<lnav::pcre2pp::code> hd_pattern;
        positioned_property<std::string> hd_color;
        positioned_property<std::string> hd_background_color;
        bool hd_underline{false};
        bool hd_blink{false};
    };

    long value_line_count(const intern_string_t ist,
                          bool top_level,
                          nonstd::optional<double> val = nonstd::nullopt,
                          const unsigned char* str = nullptr,
                          ssize_t len = -1);

    bool has_value_def(const intern_string_t ist) const
    {
        const auto iter = this->elf_value_defs.find(ist);

        return iter != this->elf_value_defs.end();
    }

    std::string get_pattern_path(uint64_t line_number) const
    {
        if (this->elf_type != elf_type_t::ELF_TYPE_TEXT) {
            return "structured";
        }
        int pat_index = this->pattern_index_for_line(line_number);
        return this->elf_pattern_order[pat_index]->p_config_path;
    }

    intern_string_t get_pattern_name(uint64_t line_number) const;

    std::string get_pattern_regex(uint64_t line_number) const
    {
        if (this->elf_type != elf_type_t::ELF_TYPE_TEXT) {
            return "";
        }
        int pat_index = this->pattern_index_for_line(line_number);
        return this->elf_pattern_order[pat_index]
            ->p_pcre.pp_value->get_pattern();
    }

    log_level_t convert_level(string_fragment str,
                              scan_batch_context* sbc) const;

    using mod_map_t = std::map<intern_string_t, module_format>;
    static mod_map_t MODULE_FORMATS;
    static std::vector<std::shared_ptr<external_log_format>>
        GRAPH_ORDERED_FORMATS;

    std::set<std::string> elf_source_path;
    std::vector<ghc::filesystem::path> elf_format_source_order;
    std::map<intern_string_t, int> elf_format_sources;
    std::list<intern_string_t> elf_collision;
    std::set<file_format_t> elf_mime_types;
    factory_container<lnav::pcre2pp::code> elf_filename_pcre;
    std::map<std::string, std::shared_ptr<pattern>> elf_patterns;
    std::vector<std::shared_ptr<pattern>> elf_pattern_order;
    std::vector<sample> elf_samples;
    std::unordered_map<const intern_string_t, std::shared_ptr<value_def>>
        elf_value_defs;
    std::vector<std::shared_ptr<value_def>> elf_value_def_order;
    std::vector<std::shared_ptr<value_def>> elf_numeric_value_defs;
    int elf_column_count{0};
    double elf_timestamp_divisor{1.0};
    intern_string_t elf_level_field;
    factory_container<lnav::pcre2pp::code> elf_level_pointer;
    intern_string_t elf_body_field;
    intern_string_t elf_module_id_field;
    intern_string_t elf_opid_field;
    std::map<log_level_t, level_pattern> elf_level_patterns;
    std::vector<std::pair<int64_t, log_level_t>> elf_level_pairs;
    bool elf_container{false};
    bool elf_has_module_format{false};
    bool elf_builtin_format{false};

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

    enum class elf_type_t {
        ELF_TYPE_TEXT,
        ELF_TYPE_JSON,
        ELF_TYPE_CSV,
    };

    elf_type_t elf_type{elf_type_t::ELF_TYPE_TEXT};

    void json_append_to_cache(const char* value, ssize_t len)
    {
        if (len <= 0) {
            return;
        }

        size_t old_size = this->jlf_cached_line.size();
        if (len == -1) {
            len = strlen(value);
        }
        this->jlf_cached_line.resize(old_size + len);
        memcpy(&(this->jlf_cached_line[old_size]), value, len);
    }

    void json_append_to_cache(ssize_t len)
    {
        if (len <= 0) {
            return;
        }
        size_t old_size = this->jlf_cached_line.size();
        this->jlf_cached_line.resize(old_size + len);
        memset(&this->jlf_cached_line[old_size], ' ', len);
    }

    void json_append(const json_format_element& jfe,
                     const value_def* vd,
                     const char* value,
                     ssize_t len);

    logline_value_meta get_value_meta(intern_string_t field_name,
                                      value_kind_t kind);

    std::vector<lnav::console::snippet> get_snippets() const;

    bool jlf_hide_extra{false};
    std::vector<json_format_element> jlf_line_format;
    int jlf_line_format_init_count{0};
    shared_buffer jlf_share_manager;
    logline_value_vector jlf_line_values;

    off_t jlf_cached_offset{-1};
    line_range jlf_cached_sub_range;
    bool jlf_cached_full{false};
    std::vector<off_t> jlf_line_offsets;
    std::vector<char> jlf_cached_line;
    string_attrs_t jlf_line_attrs;
    std::shared_ptr<yajlpp_parse_context> jlf_parse_context;
    std::shared_ptr<yajl_handle_t> jlf_yajl_handle;

private:
    const intern_string_t elf_name;

    static uint8_t module_scan(string_fragment body_cap,
                               const intern_string_t& mod_name);
};

class module_format {
public:
    std::shared_ptr<log_format> mf_mod_format;
};

#endif
