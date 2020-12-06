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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file log_format_ext.hh
 */

#ifndef lnav_log_format_ext_hh
#define lnav_log_format_ext_hh

#include <unordered_map>

#include "log_format.hh"
#include "yajlpp/yajlpp.hh"

class module_format;

class external_log_format : public log_format {

public:
    struct sample {
        sample() : s_level(LEVEL_UNKNOWN) {};

        std::string s_line;
        log_level_t s_level;
    };

    struct value_def {
        intern_string_t vd_name;
        logline_value::kind_t vd_kind{logline_value::VALUE_UNKNOWN};
        std::string vd_collate;
        bool vd_identifier{false};
        bool vd_foreign_key{false};
        intern_string_t vd_unit_field;
        std::map<const intern_string_t, scaling_factor> vd_unit_scaling;
        int vd_column{-1};
        ssize_t vd_values_index{-1};
        bool vd_hidden{false};
        bool vd_user_hidden{false};
        bool vd_internal{false};
        std::vector<std::string> vd_action_list;
        std::string vd_rewriter;
        std::string vd_description;
    };

    struct indexed_value_def {
        indexed_value_def(int index = -1,
                          int unit_index = -1,
                          std::shared_ptr<value_def> vd = nullptr)
            : ivd_index(index),
              ivd_unit_field_index(unit_index),
              ivd_value_def(std::move(vd)) {
        }

        int ivd_index;
        int ivd_unit_field_index;
        std::shared_ptr<value_def> ivd_value_def;

        bool operator<(const indexed_value_def &rhs) const {
            return this->ivd_index < rhs.ivd_index;
        }
    };

    struct pattern {
        std::string p_config_path;
        std::string p_string;
        std::unique_ptr<pcrepp> p_pcre;
        std::vector<indexed_value_def> p_value_by_index;
        std::vector<int> p_numeric_value_indexes;
        int p_timestamp_field_index{-1};
        int p_level_field_index{-1};
        int p_module_field_index{-1};
        int p_opid_field_index{-1};
        int p_body_field_index{-1};
        int p_timestamp_end{-1};
        bool p_module_format{false};
    };

    struct level_pattern {
        std::string lp_regex;
        std::shared_ptr<pcrepp> lp_pcre;
    };

    external_log_format(const intern_string_t name)
        : elf_file_pattern(".*"),
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

    const intern_string_t get_name() const {
        return this->elf_name;
    };

    bool match_name(const std::string &filename) {
        pcre_context_static<10> pc;
        pcre_input pi(filename);

        return this->elf_filename_pcre->match(pc, pi);
    };

    scan_result_t scan(logfile &lf,
                       std::vector<logline> &dst,
                       const line_info &offset,
                       shared_buffer_ref &sbr);

    bool scan_for_partial(shared_buffer_ref &sbr, size_t &len_out) const;

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

    std::shared_ptr<log_format> specialized(int fmt_lock);

    const logline_value_stats *stats_for_value(const intern_string_t &name) const {
        const logline_value_stats *retval = nullptr;

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

    std::shared_ptr<log_vtab_impl> get_vtab_impl() const;

    const std::vector<std::string> *get_actions(const logline_value &lv) const {
        const std::vector<std::string> *retval = nullptr;

        const auto iter = this->elf_value_defs.find(lv.lv_name);
        if (iter != this->elf_value_defs.end()) {
            retval = &iter->second->vd_action_list;
        }

        return retval;
    };

    std::set<std::string> get_source_path() const {
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
                          const unsigned char *str = nullptr,
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
    static std::vector<std::shared_ptr<external_log_format>> GRAPH_ORDERED_FORMATS;

    std::set<std::string> elf_source_path;
    std::list<intern_string_t> elf_collision;
    std::string elf_file_pattern;
    std::shared_ptr<pcrepp> elf_filename_pcre;
    std::map<std::string, std::shared_ptr<pattern>> elf_patterns;
    std::vector<std::shared_ptr<pattern>> elf_pattern_order;
    std::vector<sample> elf_samples;
    std::unordered_map<const intern_string_t, std::shared_ptr<value_def>>
        elf_value_defs;
    std::vector<std::shared_ptr<value_def>> elf_value_def_order;
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
        memcpy(&(this->jlf_cached_line[old_size]), value, len);
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
    std::shared_ptr<log_format> mf_mod_format;
};

#endif
