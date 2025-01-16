/**
 * Copyright (c) 2013-2016, Timothy Stack
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
 * @file log_format_loader.cc
 */

#include <map>
#include <string>

#include "log_format_loader.hh"

#include <glob.h>
#include <libgen.h>
#include <sys/stat.h>

#include "base/auto_fd.hh"
#include "base/from_trait.hh"
#include "base/fs_util.hh"
#include "base/paths.hh"
#include "base/string_util.hh"
#include "bin2c.hh"
#include "builtin-scripts.h"
#include "builtin-sh-scripts.h"
#include "config.h"
#include "default-formats.h"
#include "file_format.hh"
#include "fmt/format.h"
#include "lnav_config.hh"
#include "log_format_ext.hh"
#include "sql_execute.hh"
#include "sql_util.hh"
#include "yajlpp/yajlpp.hh"
#include "yajlpp/yajlpp_def.hh"

static void extract_metadata(string_fragment, struct script_metadata& meta_out);

using log_formats_map_t
    = std::map<intern_string_t, std::shared_ptr<external_log_format>>;

using namespace lnav::roles::literals;

static auto intern_lifetime = intern_string::get_table_lifetime();
static log_formats_map_t LOG_FORMATS;

struct loader_userdata {
    yajlpp_parse_context* ud_parse_context{nullptr};
    std::string ud_file_schema;
    std::filesystem::path ud_format_path;
    std::vector<intern_string_t>* ud_format_names{nullptr};
    std::vector<lnav::console::user_message>* ud_errors{nullptr};
};

static external_log_format*
ensure_format(const yajlpp_provider_context& ypc, loader_userdata* ud)
{
    const intern_string_t name = ypc.get_substr_i(0);
    auto* formats = ud->ud_format_names;
    auto* retval = LOG_FORMATS[name].get();
    if (retval == nullptr) {
        LOG_FORMATS[name] = std::make_shared<external_log_format>(name);
        retval = LOG_FORMATS[name].get();
        log_debug("Loading format -- %s", name.get());
    }
    retval->elf_source_path.insert(ud->ud_format_path.parent_path().string());

    if (find(formats->begin(), formats->end(), name) == formats->end()) {
        formats->push_back(name);
    }

    if (!ud->ud_format_path.empty()) {
        const intern_string_t i_src_path
            = intern_string::lookup(ud->ud_format_path.string());
        auto srcs_iter = retval->elf_format_sources.find(i_src_path);
        if (srcs_iter == retval->elf_format_sources.end()) {
            retval->elf_format_source_order.emplace_back(ud->ud_format_path);
            retval->elf_format_sources[i_src_path]
                = ud->ud_parse_context->get_line_number();
        }
    }

    if (ud->ud_format_path.empty()) {
        retval->elf_builtin_format = true;
    }

    return retval;
}

static external_log_format::pattern*
pattern_provider(const yajlpp_provider_context& ypc, external_log_format* elf)
{
    auto regex_name = ypc.get_substr(0);
    auto& pat = elf->elf_patterns[regex_name];

    if (pat.get() == nullptr) {
        pat = std::make_shared<external_log_format::pattern>();
    }

    if (pat->p_config_path.empty()) {
        pat->p_name = intern_string::lookup(regex_name);
        pat->p_config_path = fmt::format(
            FMT_STRING("/{}/regex/{}"), elf->get_name(), regex_name);
    }

    return pat.get();
}

static external_log_format::value_def*
value_def_provider(const yajlpp_provider_context& ypc, external_log_format* elf)
{
    const intern_string_t value_name = ypc.get_substr_i(0);

    auto iter = elf->elf_value_defs.find(value_name);
    std::shared_ptr<external_log_format::value_def> retval;

    if (iter == elf->elf_value_defs.end()) {
        retval = std::make_shared<external_log_format::value_def>(
            value_name,
            value_kind_t::VALUE_TEXT,
            logline_value_meta::external_column{},
            elf);
        elf->elf_value_defs[value_name] = retval;
        elf->elf_value_def_order.emplace_back(retval);
    } else {
        retval = iter->second;
    }

    return retval.get();
}

static format_tag_def*
format_tag_def_provider(const yajlpp_provider_context& ypc,
                        external_log_format* elf)
{
    const intern_string_t tag_name = ypc.get_substr_i(0);

    auto iter = elf->lf_tag_defs.find(tag_name);
    std::shared_ptr<format_tag_def> retval;

    if (iter == elf->lf_tag_defs.end()) {
        auto tag_with_hash = fmt::format(FMT_STRING("#{}"), tag_name);
        retval = std::make_shared<format_tag_def>(tag_with_hash);
        elf->lf_tag_defs[tag_name] = retval;
    } else {
        retval = iter->second;
    }

    return retval.get();
}

static format_partition_def*
format_partition_def_provider(const yajlpp_provider_context& ypc,
                              external_log_format* elf)
{
    const intern_string_t partition_name = ypc.get_substr_i(0);

    auto iter = elf->lf_partition_defs.find(partition_name);
    std::shared_ptr<format_partition_def> retval;

    if (iter == elf->lf_partition_defs.end()) {
        retval = std::make_shared<format_partition_def>(
            partition_name.to_string());
        elf->lf_partition_defs[partition_name] = retval;
    } else {
        retval = iter->second;
    }

    return retval.get();
}

static scaling_factor*
scaling_factor_provider(const yajlpp_provider_context& ypc,
                        external_log_format::value_def* value_def)
{
    auto scale_name = ypc.get_substr_i(0);
    auto& retval = value_def->vd_unit_scaling[scale_name];

    return &retval;
}

static external_log_format::json_format_element&
ensure_json_format_element(external_log_format* elf, int index)
{
    elf->jlf_line_format.resize(index + 1);

    return elf->jlf_line_format[index];
}

static external_log_format::json_format_element*
line_format_provider(const yajlpp_provider_context& ypc,
                     external_log_format* elf)
{
    auto& jfe = ensure_json_format_element(elf, ypc.ypc_index);

    jfe.jfe_type = external_log_format::json_log_field::VARIABLE;

    return &jfe;
}

static int
read_format_bool(yajlpp_parse_context* ypc, int val)
{
    auto elf = (external_log_format*) ypc->ypc_obj_stack.top();
    auto field_name = ypc->get_path_fragment(1);

    if (field_name == "convert-to-local-time") {
        elf->lf_date_time.dts_local_time = val;
    } else if (field_name == "json") {
        if (val) {
            elf->elf_type = external_log_format::elf_type_t::ELF_TYPE_JSON;
        }
    }

    return 1;
}

static int
read_format_double(yajlpp_parse_context* ypc, double val)
{
    auto elf = (external_log_format*) ypc->ypc_obj_stack.top();
    auto field_name = ypc->get_path_fragment(1);

    if (field_name == "timestamp-divisor") {
        if (val <= 0) {
            ypc->report_error(
                lnav::console::user_message::error(
                    attr_line_t()
                        .append_quoted(fmt::to_string(val))
                        .append(" is not a valid value for ")
                        .append_quoted(lnav::roles::symbol(
                            ypc->get_full_path().to_string())))
                    .with_reason("value cannot be less than or equal to zero")
                    .with_snippet(ypc->get_snippet())
                    .with_help(ypc->ypc_current_handler->get_help_text(ypc)));
        }
        elf->elf_timestamp_divisor = val;
    }

    return 1;
}

static int
read_format_int(yajlpp_parse_context* ypc, long long val)
{
    auto elf = (external_log_format*) ypc->ypc_obj_stack.top();
    auto field_name = ypc->get_path_fragment(1);

    if (field_name == "timestamp-divisor") {
        if (val <= 0) {
            ypc->report_error(
                lnav::console::user_message::error(
                    attr_line_t()
                        .append_quoted(fmt::to_string(val))
                        .append(" is not a valid value for ")
                        .append_quoted(lnav::roles::symbol(
                            ypc->get_full_path().to_string())))
                    .with_reason("value cannot be less than or equal to zero")
                    .with_snippet(ypc->get_snippet())
                    .with_help(ypc->ypc_current_handler->get_help_text(ypc)));
        }
        elf->elf_timestamp_divisor = val;
    }

    return 1;
}

static int
read_format_field(yajlpp_parse_context* ypc,
                  const unsigned char* str,
                  size_t len,
                  yajl_string_props_t*)
{
    auto elf = (external_log_format*) ypc->ypc_obj_stack.top();
    auto leading_slash = len > 0 && str[0] == '/';
    auto value = std::string((const char*) (leading_slash ? str + 1 : str),
                             leading_slash ? len - 1 : len);
    auto field_name = ypc->get_path_fragment(1);

    if (field_name == "timestamp-format") {
        elf->lf_timestamp_format.push_back(intern_string::lookup(value)->get());
    } else if (field_name == "module-field") {
        elf->elf_module_id_field = intern_string::lookup(value);
        elf->elf_container = true;
    }

    return 1;
}

static int
read_levels(yajlpp_parse_context* ypc, const unsigned char* str, size_t len, yajl_string_props_t*)
{
    auto elf = (external_log_format*) ypc->ypc_obj_stack.top();
    auto regex = std::string((const char*) str, len);
    auto level_name_or_number = ypc->get_path_fragment(2);
    log_level_t level = string2level(level_name_or_number.c_str());
    auto value_frag = string_fragment::from_bytes(str, len);

    elf->elf_level_patterns[level].lp_pcre.pp_path = ypc->get_full_path();
    auto compile_res = lnav::pcre2pp::code::from(value_frag);
    if (compile_res.isErr()) {
        static const intern_string_t PATTERN_SRC
            = intern_string::lookup("pattern");
        auto ce = compile_res.unwrapErr();
        ypc->ypc_current_handler->report_error(
            ypc,
            value_frag.to_string(),
            lnav::console::to_user_message(PATTERN_SRC, ce));
    } else {
        elf->elf_level_patterns[level].lp_pcre.pp_value
            = compile_res.unwrap().to_shared();
    }

    return 1;
}

static int
read_level_int(yajlpp_parse_context* ypc, long long val)
{
    auto elf = (external_log_format*) ypc->ypc_obj_stack.top();
    auto level_name_or_number = ypc->get_path_fragment(2);
    log_level_t level = string2level(level_name_or_number.c_str());

    elf->elf_level_pairs.emplace_back(val, level);

    return 1;
}

static int
read_action_def(yajlpp_parse_context* ypc, const unsigned char* str, size_t len, yajl_string_props_t*)
{
    auto elf = (external_log_format*) ypc->ypc_obj_stack.top();
    auto action_name = ypc->get_path_fragment(2);
    auto field_name = ypc->get_path_fragment(3);
    auto val = std::string((const char*) str, len);

    elf->lf_action_defs[action_name].ad_name = action_name;
    if (field_name == "label") {
        elf->lf_action_defs[action_name].ad_label = val;
    }

    return 1;
}

static int
read_action_bool(yajlpp_parse_context* ypc, int val)
{
    auto elf = (external_log_format*) ypc->ypc_obj_stack.top();
    auto action_name = ypc->get_path_fragment(2);
    auto field_name = ypc->get_path_fragment(3);

    elf->lf_action_defs[action_name].ad_capture_output = val;

    return 1;
}

static int
read_action_cmd(yajlpp_parse_context* ypc, const unsigned char* str, size_t len, yajl_string_props_t*)
{
    auto elf = (external_log_format*) ypc->ypc_obj_stack.top();
    auto action_name = ypc->get_path_fragment(2);
    auto field_name = ypc->get_path_fragment(3);
    auto val = std::string((const char*) str, len);

    elf->lf_action_defs[action_name].ad_name = action_name;
    elf->lf_action_defs[action_name].ad_cmdline.push_back(val);

    return 1;
}

static external_log_format::sample&
ensure_sample(external_log_format* elf, int index)
{
    elf->elf_samples.resize(index + 1);

    return elf->elf_samples[index];
}

static external_log_format::sample*
sample_provider(const yajlpp_provider_context& ypc, external_log_format* elf)
{
    auto& sample = ensure_sample(elf, ypc.ypc_index);

    return &sample;
}

static int
read_json_constant(yajlpp_parse_context* ypc,
                   const unsigned char* str,
                   size_t len,
                   yajl_string_props_t*)
{
    auto val = std::string((const char*) str, len);
    auto elf = (external_log_format*) ypc->ypc_obj_stack.top();

    ypc->ypc_array_index.back() += 1;
    auto& jfe = ensure_json_format_element(elf, ypc->ypc_array_index.back());
    jfe.jfe_type = external_log_format::json_log_field::CONSTANT;
    jfe.jfe_default_value = val;

    return 1;
}

static const struct json_path_container pattern_handlers = {
    yajlpp::property_handler("pattern")
        .with_synopsis("<message-regex>")
        .with_description(
            "The regular expression to match a log message and capture fields.")
        .with_min_length(1)
        .for_field(&external_log_format::pattern::p_pcre),
    yajlpp::property_handler("module-format")
        .with_synopsis("<bool>")
        .with_description(
            "If true, this pattern will only be used to parse message bodies "
            "of container formats, like syslog")
        .for_field(&external_log_format::pattern::p_module_format),
};

static const json_path_handler_base::enum_value_t SUBSECOND_UNIT_ENUM[] = {
    {"milli", log_format::subsecond_unit::milli},
    {"micro", log_format::subsecond_unit::micro},
    {"nano", log_format::subsecond_unit::nano},

    json_path_handler_base::ENUM_TERMINATOR,
};

static const json_path_handler_base::enum_value_t ALIGN_ENUM[] = {
    {"left", external_log_format::json_format_element::align_t::LEFT},
    {"right", external_log_format::json_format_element::align_t::RIGHT},

    json_path_handler_base::ENUM_TERMINATOR,
};

static const json_path_handler_base::enum_value_t OVERFLOW_ENUM[] = {
    {"abbrev", external_log_format::json_format_element::overflow_t::ABBREV},
    {"truncate",
     external_log_format::json_format_element::overflow_t::TRUNCATE},
    {"dot-dot", external_log_format::json_format_element::overflow_t::DOTDOT},
    {"last-word",
     external_log_format::json_format_element::overflow_t::LASTWORD},

    json_path_handler_base::ENUM_TERMINATOR,
};

static const json_path_handler_base::enum_value_t TRANSFORM_ENUM[] = {
    {"none", external_log_format::json_format_element::transform_t::NONE},
    {"uppercase",
     external_log_format::json_format_element::transform_t::UPPERCASE},
    {"lowercase",
     external_log_format::json_format_element::transform_t::LOWERCASE},
    {"capitalize",
     external_log_format::json_format_element::transform_t::CAPITALIZE},

    json_path_handler_base::ENUM_TERMINATOR,
};

static const struct json_path_container line_format_handlers = {
    yajlpp::property_handler("field")
        .with_synopsis("<field-name>")
        .with_description(
            "The name of the field to substitute at this position")
        .for_field(&external_log_format::json_format_element::jfe_value),

    yajlpp::property_handler("default-value")
        .with_synopsis("<string>")
        .with_description(
            "The default value for this position if the field is null")
        .for_field(
            &external_log_format::json_format_element::jfe_default_value),

    yajlpp::property_handler("timestamp-format")
        .with_synopsis("<string>")
        .with_min_length(1)
        .with_description("The strftime(3) format for this field")
        .for_field(&external_log_format::json_format_element::jfe_ts_format),

    yajlpp::property_handler("min-width")
        .with_min_value(0)
        .with_synopsis("<size>")
        .with_description("The minimum width of the field")
        .for_field(&external_log_format::json_format_element::jfe_min_width),

    yajlpp::property_handler("auto-width")
        .with_description("Automatically detect the necessary width of the "
                          "field based on the observed values")
        .for_field(&external_log_format::json_format_element::jfe_auto_width),

    yajlpp::property_handler("max-width")
        .with_min_value(0)
        .with_synopsis("<size>")
        .with_description("The maximum width of the field")
        .for_field(&external_log_format::json_format_element::jfe_max_width),

    yajlpp::property_handler("align")
        .with_synopsis("left|right")
        .with_description(
            "Align the text in the column to the left or right side")
        .with_enum_values(ALIGN_ENUM)
        .for_field(&external_log_format::json_format_element::jfe_align),

    yajlpp::property_handler("overflow")
        .with_synopsis("abbrev|truncate|dot-dot")
        .with_description("Overflow style")
        .with_enum_values(OVERFLOW_ENUM)
        .for_field(&external_log_format::json_format_element::jfe_overflow),

    yajlpp::property_handler("text-transform")
        .with_synopsis("none|uppercase|lowercase|capitalize")
        .with_description("Text transformation")
        .with_enum_values(TRANSFORM_ENUM)
        .for_field(
            &external_log_format::json_format_element::jfe_text_transform),

    yajlpp::property_handler("prefix")
        .with_synopsis("<str>")
        .with_description("Text to prepend to the value")
        .for_field(&external_log_format::json_format_element::jfe_prefix),

    yajlpp::property_handler("suffix")
        .with_synopsis("<str>")
        .with_description("Text to append to the value")
        .for_field(&external_log_format::json_format_element::jfe_suffix),
};

static const json_path_handler_base::enum_value_t KIND_ENUM[] = {
    {"string", value_kind_t::VALUE_TEXT},
    {"integer", value_kind_t::VALUE_INTEGER},
    {"float", value_kind_t::VALUE_FLOAT},
    {"boolean", value_kind_t::VALUE_BOOLEAN},
    {"json", value_kind_t::VALUE_JSON},
    {"struct", value_kind_t::VALUE_STRUCT},
    {"quoted", value_kind_t::VALUE_QUOTED},
    {"xml", value_kind_t::VALUE_XML},

    json_path_handler_base::ENUM_TERMINATOR,
};

static const json_path_handler_base::enum_value_t SCALE_OP_ENUM[] = {
    {"identity", scale_op_t::SO_IDENTITY},
    {"multiply", scale_op_t::SO_MULTIPLY},
    {"divide", scale_op_t::SO_DIVIDE},

    json_path_handler_base::ENUM_TERMINATOR,
};

static const struct json_path_container scaling_factor_handlers = {
    yajlpp::property_handler("op")
        .with_enum_values(SCALE_OP_ENUM)
        .for_field(&scaling_factor::sf_op),

    yajlpp::property_handler("value").for_field(&scaling_factor::sf_value),
};

static const struct json_path_container scale_handlers = {
    yajlpp::pattern_property_handler("(?<scale>[^/]+)")
        .with_obj_provider(scaling_factor_provider)
        .with_children(scaling_factor_handlers),
};

static const struct json_path_container unit_handlers = {
    yajlpp::property_handler("field")
        .with_synopsis("<field-name>")
        .with_description(
            "The name of the field that contains the units for this field")
        .for_field(&external_log_format::value_def::vd_unit_field),

    yajlpp::property_handler("scaling-factor")
        .with_description("Transforms the numeric value by the given factor")
        .with_children(scale_handlers),
};

static const struct json_path_container value_def_handlers = {
    yajlpp::property_handler("kind")
        .with_synopsis("<data-type>")
        .with_description("The type of data in the field")
        .with_enum_values(KIND_ENUM)
        .for_field(&external_log_format::value_def::vd_meta,
                   &logline_value_meta::lvm_kind),

    yajlpp::property_handler("collate")
        .with_synopsis("<function>")
        .with_description("The collating function to use for this column")
        .for_field(&external_log_format::value_def::vd_collate),

    yajlpp::property_handler("unit")
        .with_description("Unit definitions for this field")
        .with_children(unit_handlers),

    yajlpp::property_handler("identifier")
        .with_synopsis("<bool>")
        .with_description("Indicates whether or not this field contains an "
                          "identifier that should be highlighted")
        .for_field(&external_log_format::value_def::vd_meta,
                   &logline_value_meta::lvm_identifier),

    yajlpp::property_handler("foreign-key")
        .with_synopsis("<bool>")
        .with_description("Indicates whether or not this field should be "
                          "treated as a foreign key for row in another table")
        .for_field(&external_log_format::value_def::vd_meta,
                   &logline_value_meta::lvm_foreign_key),

    yajlpp::property_handler("hidden")
        .with_synopsis("<bool>")
        .with_description(
            "Indicates whether or not this field should be hidden")
        .for_field(&external_log_format::value_def::vd_meta,
                   &logline_value_meta::lvm_hidden),

    yajlpp::property_handler("action-list#")
        .with_synopsis("<string>")
        .with_description("Actions to execute when this field is clicked on")
        .for_field(&external_log_format::value_def::vd_action_list),

    yajlpp::property_handler("rewriter")
        .with_synopsis("<command>")
        .with_description(
            "A command that will rewrite this field when pretty-printing")
        .for_field(&external_log_format::value_def::vd_rewriter)
        .with_example(";SELECT :sc_status || ' (' || (SELECT message FROM "
                      "http_status_codes WHERE status = :sc_status) || ') '"),

    yajlpp::property_handler("description")
        .with_synopsis("<string>")
        .with_description("A description of the field")
        .for_field(&external_log_format::value_def::vd_description),
};

static const struct json_path_container highlighter_def_handlers = {
    yajlpp::property_handler("pattern")
        .with_synopsis("<regex>")
        .with_description(
            "A regular expression to highlight in logs of this format.")
        .for_field(&external_log_format::highlighter_def::hd_pattern),

    yajlpp::property_handler("color")
        .with_synopsis("#<hex>|<name>")
        .with_description("The color to use when highlighting this pattern.")
        .for_field(&external_log_format::highlighter_def::hd_color),

    yajlpp::property_handler("background-color")
        .with_synopsis("#<hex>|<name>")
        .with_description(
            "The background color to use when highlighting this pattern.")
        .for_field(&external_log_format::highlighter_def::hd_background_color),

    yajlpp::property_handler("underline")
        .with_synopsis("<enabled>")
        .with_description("Highlight this pattern with an underline.")
        .for_field(&external_log_format::highlighter_def::hd_underline),

    yajlpp::property_handler("blink")
        .with_synopsis("<enabled>")
        .with_description("Highlight this pattern by blinking.")
        .for_field(&external_log_format::highlighter_def::hd_blink),
};

static const json_path_handler_base::enum_value_t LEVEL_ENUM[] = {
    {level_names[LEVEL_TRACE], LEVEL_TRACE},
    {level_names[LEVEL_DEBUG5], LEVEL_DEBUG5},
    {level_names[LEVEL_DEBUG4], LEVEL_DEBUG4},
    {level_names[LEVEL_DEBUG3], LEVEL_DEBUG3},
    {level_names[LEVEL_DEBUG2], LEVEL_DEBUG2},
    {level_names[LEVEL_DEBUG], LEVEL_DEBUG},
    {level_names[LEVEL_INFO], LEVEL_INFO},
    {level_names[LEVEL_STATS], LEVEL_STATS},
    {level_names[LEVEL_NOTICE], LEVEL_NOTICE},
    {level_names[LEVEL_WARNING], LEVEL_WARNING},
    {level_names[LEVEL_ERROR], LEVEL_ERROR},
    {level_names[LEVEL_CRITICAL], LEVEL_CRITICAL},
    {level_names[LEVEL_FATAL], LEVEL_FATAL},

    json_path_handler_base::ENUM_TERMINATOR,
};

static const struct json_path_container sample_handlers = {
    yajlpp::property_handler("description")
        .with_synopsis("<text>")
        .with_description("A description of this sample.")
        .for_field(&external_log_format::sample::s_description),
    yajlpp::property_handler("line")
        .with_synopsis("<log-line>")
        .with_description(
            "A sample log line that should match a pattern in this format.")
        .for_field(&external_log_format::sample::s_line),

    yajlpp::property_handler("level")
        .with_enum_values(LEVEL_ENUM)
        .with_description("The expected level for this sample log line.")
        .for_field(&external_log_format::sample::s_level),
};

static const json_path_handler_base::enum_value_t TYPE_ENUM[] = {
    {"text", external_log_format::elf_type_t::ELF_TYPE_TEXT},
    {"json", external_log_format::elf_type_t::ELF_TYPE_JSON},
    {"csv", external_log_format::elf_type_t::ELF_TYPE_CSV},

    json_path_handler_base::ENUM_TERMINATOR,
};

static const struct json_path_container regex_handlers = {
    yajlpp::pattern_property_handler(R"((?<pattern_name>[^/]+))")
        .with_description("The set of patterns used to match log messages")
        .with_obj_provider(pattern_provider)
        .with_children(pattern_handlers),
};

static const struct json_path_container level_handlers = {
    yajlpp::pattern_property_handler("(?<level>trace|debug[2345]?|info|stats|"
                                     "notice|warning|error|critical|fatal)")
        .add_cb(read_levels)
        .add_cb(read_level_int)
        .with_synopsis("<pattern|integer>")
        .with_description("The regular expression used to match the log text "
                          "for this level.  "
                          "For JSON logs with numeric levels, this should be "
                          "the number for the corresponding level."),
};

static const struct json_path_container value_handlers = {
    yajlpp::pattern_property_handler("(?<value_name>[^/]+)")
        .with_description(
            "The set of values captured by the log message patterns")
        .with_obj_provider(value_def_provider)
        .with_children(value_def_handlers),
};

static const struct json_path_container tag_path_handlers = {
    yajlpp::property_handler("glob")
        .with_synopsis("<glob>")
        .with_description("The glob to match against file paths")
        .with_example("*/system.log*")
        .for_field(&format_tag_def::path_restriction::p_glob),
};

static const struct json_path_container format_tag_def_handlers = {
    yajlpp::property_handler("paths#")
        .with_description("Restrict tagging to the given paths")
        .for_field(&format_tag_def::ftd_paths)
        .with_children(tag_path_handlers),
    yajlpp::property_handler("pattern")
        .with_synopsis("<regex>")
        .with_description("The regular expression to match against the body of "
                          "the log message")
        .with_example("\\w+ is down")
        .for_field(&format_tag_def::ftd_pattern),
    yajlpp::property_handler("description")
        .with_synopsis("<string>")
        .with_description("A description of this tag")
        .for_field(&format_tag_def::ftd_description),
    json_path_handler("level")
        .with_synopsis("<log-level>")
        .with_description("Constrain hits to log messages with this level")
        .with_enum_values(LEVEL_ENUM)
        .for_field(&format_tag_def::ftd_level),
};

static const struct json_path_container tag_handlers = {
    yajlpp::pattern_property_handler(R"((?<tag_name>[\w:;\._\-]+))")
        .with_description("The name of the tag to apply")
        .with_obj_provider(format_tag_def_provider)
        .with_children(format_tag_def_handlers),
};

static const struct json_path_container format_partition_def_handlers = {
    yajlpp::property_handler("paths#")
        .with_description("Restrict partitioning to the given paths")
        .for_field(&format_partition_def::fpd_paths)
        .with_children(tag_path_handlers),
    yajlpp::property_handler("pattern")
        .with_synopsis("<regex>")
        .with_description("The regular expression to match against the body of "
                          "the log message")
        .with_example("\\w+ is down")
        .for_field(&format_partition_def::fpd_pattern),
    yajlpp::property_handler("description")
        .with_synopsis("<string>")
        .with_description("A description of this partition")
        .for_field(&format_partition_def::fpd_description),
    json_path_handler("level")
        .with_synopsis("<log-level>")
        .with_description("Constrain hits to log messages with this level")
        .with_enum_values(LEVEL_ENUM)
        .for_field(&format_partition_def::fpd_level),
};

static const struct json_path_container partition_handlers = {
    yajlpp::pattern_property_handler(R"((?<partition_type>[\w:;\._\-]+))")
        .with_description("The type of partition to apply")
        .with_obj_provider(format_partition_def_provider)
        .with_children(format_partition_def_handlers),
};

static const struct json_path_container highlight_handlers = {
    yajlpp::pattern_property_handler(R"((?<highlight_name>[^/]+))")
        .with_description("The definition of a highlight")
        .with_obj_provider<external_log_format::highlighter_def,
                           external_log_format>(
            [](const yajlpp_provider_context& ypc, external_log_format* root) {
                auto* retval
                    = &(root->elf_highlighter_patterns[ypc.get_substr_i(0)]);

                return retval;
            })
        .with_children(highlighter_def_handlers),
};

static const struct json_path_container action_def_handlers = {
    json_path_handler("label", read_action_def),
    json_path_handler("capture-output", read_action_bool),
    json_path_handler("cmd#", read_action_cmd),
};

static const struct json_path_container action_handlers = {
    json_path_handler(
        lnav::pcre2pp::code::from_const("(?<action_name>\\w+)").to_shared(),
        read_action_def)
        .with_children(action_def_handlers),
};

static const struct json_path_container search_table_def_handlers = {
    json_path_handler("pattern")
        .with_synopsis("<regex>")
        .with_description("The regular expression for this search table.")
        .for_field(&external_log_format::search_table_def::std_pattern),
    json_path_handler("glob")
        .with_synopsis("<glob>")
        .with_description("Glob pattern used to constrain hits to messages "
                          "that match the given pattern.")
        .for_field(&external_log_format::search_table_def::std_glob),
    json_path_handler("level")
        .with_synopsis("<log-level>")
        .with_description("Constrain hits to log messages with this level")
        .with_enum_values(LEVEL_ENUM)
        .for_field(&external_log_format::search_table_def::std_level),
};

static const struct json_path_container search_table_handlers = {
    yajlpp::pattern_property_handler("(?<table_name>\\w+)")
        .with_description(
            "The set of search tables to be automatically defined")
        .with_obj_provider<external_log_format::search_table_def,
                           external_log_format>(
            [](const yajlpp_provider_context& ypc, external_log_format* root) {
                auto* retval = &(root->elf_search_tables[ypc.get_substr_i(0)]);

                return retval;
            })
        .with_children(search_table_def_handlers),
};

static const struct json_path_container header_expr_handlers = {
    yajlpp::pattern_property_handler(R"((?<header_expr_name>\w+))")
        .with_description("SQLite expression")
        .for_field(&external_log_format::header_exprs::he_exprs),
};

static const struct json_path_container header_handlers = {
    yajlpp::property_handler("expr")
        .with_description("The expressions used to check if a file header "
                          "matches this file format")
        .for_child(&external_log_format::header::h_exprs)
        .with_children(header_expr_handlers),
    yajlpp::property_handler("size")
        .with_description("The minimum size required for this header type")
        .for_field(&external_log_format::header::h_size),
};

static const struct json_path_container converter_handlers = {
    yajlpp::property_handler("type")
        .with_description("The MIME type")
        .for_field(&external_log_format::converter::c_type),
    yajlpp::property_handler("header")
        .with_description("File header detection definitions")
        .for_child(&external_log_format::converter::c_header)
        .with_children(header_handlers),
    yajlpp::property_handler("command")
        .with_description("The script used to convert the file")
        .with_pattern(R"([\w\.\-]+)")
        .for_field(&external_log_format::converter::c_command),
};

static const struct json_path_container opid_descriptor_handlers = {
    yajlpp::property_handler("field")
        .with_synopsis("<name>")
        .with_description("The field to include in the operation description")
        .for_field(&log_format::opid_descriptor::od_field),
    yajlpp::property_handler("extractor")
        .with_synopsis("<regex>")
        .with_description(
            "The regex used to extract content for the operation description")
        .for_field(&log_format::opid_descriptor::od_extractor),
    yajlpp::property_handler("prefix")
        .with_description(
            "A string to prepend to this field in the description")
        .for_field(&log_format::opid_descriptor::od_prefix),
    yajlpp::property_handler("suffix")
        .with_description("A string to append to this field in the description")
        .for_field(&log_format::opid_descriptor::od_suffix),
    yajlpp::property_handler("joiner")
        .with_description("A string to insert between instances of this field "
                          "when the field is found more than once")
        .for_field(&log_format::opid_descriptor::od_joiner),
};

static const struct json_path_container opid_description_format_handlers = {
    yajlpp::property_handler("format#")
        .with_description("Defines the elements of this operation description")
        .for_field(&log_format::opid_descriptors::od_descriptors)
        .with_children(opid_descriptor_handlers),
};

static const struct json_path_container opid_description_handlers = {
    yajlpp::pattern_property_handler(R"((?<opid_descriptor>[\w\.\-]+))")
        .with_description("A type of description for this operation")
        .for_field(&log_format::lf_opid_description_def)
        .with_children(opid_description_format_handlers),
};

static const struct json_path_container subid_description_handlers = {
    yajlpp::pattern_property_handler(R"((?<subid_descriptor>[\w\.\-]+))")
        .with_description("A type of description for this sub-operation")
        .for_field(&log_format::lf_subid_description_def)
        .with_children(opid_description_format_handlers),
};

static const struct json_path_container opid_handlers = {
    yajlpp::property_handler("subid")
        .with_description("The field that holds the ID for a sub-operation")
        .for_field(&external_log_format::elf_subid_field),
    yajlpp::property_handler("description")
        .with_description(
            "Define how to construct a description of an operation")
        .with_children(opid_description_handlers),
    yajlpp::property_handler("sub-description")
        .with_description(
            "Define how to construct a description of a sub-operation")
        .with_children(subid_description_handlers),
};

const struct json_path_container format_handlers = {
    yajlpp::property_handler("regex")
        .with_description(
            "The set of regular expressions used to match log messages")
        .with_children(regex_handlers),

    json_path_handler("json", read_format_bool)
        .with_description(
            R"(Indicates that log files are JSON-encoded (deprecated, use "file-type": "json"))"),
    json_path_handler("convert-to-local-time")
        .with_description("Indicates that displayed timestamps should "
                          "automatically be converted to local time")
        .for_field(&external_log_format::lf_date_time,
                   &date_time_scanner::dts_local_time),
    json_path_handler("hide-extra")
        .with_description(
            "Specifies whether extra values in JSON logs should be displayed")
        .for_field(&external_log_format::jlf_hide_extra),
    json_path_handler("multiline")
        .with_description("Indicates that log messages can span multiple lines")
        .for_field(&log_format::lf_multiline),
    json_path_handler("timestamp-divisor", read_format_double)
        .add_cb(read_format_int)
        .with_synopsis("<number>")
        .with_description(
            "The value to divide a numeric timestamp by in a JSON log."),
    json_path_handler("file-pattern")
        .with_description("A regular expression that restricts this format to "
                          "log files with a matching name")
        .for_field(&external_log_format::elf_filename_pcre),
    json_path_handler("converter")
        .with_description("Describes how the file format can be detected and "
                          "converted to a log that can be understood by lnav")
        .for_child(&external_log_format::elf_converter)
        .with_children(converter_handlers),
    json_path_handler("level-field")
        .with_description(
            "The name of the level field in the log message pattern")
        .for_field(&external_log_format::elf_level_field),
    json_path_handler("level-pointer")
        .with_description("A regular-expression that matches the JSON-pointer "
                          "of the level property")
        .for_field(&external_log_format::elf_level_pointer),
    json_path_handler("timestamp-field")
        .with_description(
            "The name of the timestamp field in the log message pattern")
        .for_field(&log_format::lf_timestamp_field),
    json_path_handler("subsecond-field")
        .with_description("The path to the property in a JSON-lines log "
                          "message that contains the sub-second time value")
        .for_field(&log_format::lf_subsecond_field),
    json_path_handler("subsecond-units")
        .with_description("The units of the subsecond-field property value")
        .with_enum_values(SUBSECOND_UNIT_ENUM)
        .for_field(&log_format::lf_subsecond_unit),
    json_path_handler("time-field")
        .with_description(
            "The name of the time field in the log message pattern.  This "
            "field should only be specified if the timestamp field only "
            "contains a date.")
        .for_field(&log_format::lf_time_field),
    json_path_handler("body-field")
        .with_description(
            "The name of the body field in the log message pattern")
        .for_field(&external_log_format::elf_body_field),
    json_path_handler("url",
                      lnav::pcre2pp::code::from_const("^url#?").to_shared())
        .add_cb(read_format_field)
        .with_description("A URL with more information about this log format"),
    json_path_handler("title", read_format_field)
        .with_description("The human-readable name for this log format"),
    json_path_handler("description")
        .with_description("A longer description of this log format")
        .for_field(&external_log_format::lf_description),
    json_path_handler("timestamp-format#", read_format_field)
        .with_description("An array of strptime(3)-like timestamp formats"),
    json_path_handler("module-field", read_format_field)
        .with_description(
            "The name of the module field in the log message pattern"),
    json_path_handler("opid-field")
        .with_description(
            "The name of the operation-id field in the log message pattern")
        .for_field(&external_log_format::elf_opid_field),
    yajlpp::property_handler("opid")
        .with_description("Definitions related to operations found in logs")
        .with_children(opid_handlers),
    yajlpp::property_handler("ordered-by-time")
        .with_synopsis("<bool>")
        .with_description(
            "Indicates that the order of messages in the file is time-based.")
        .for_field(&log_format::lf_time_ordered),
    yajlpp::property_handler("level")
        .with_description(
            "The map of level names to patterns or integer values")
        .with_children(level_handlers),

    yajlpp::property_handler("value")
        .with_description("The set of value definitions")
        .with_children(value_handlers),

    yajlpp::property_handler("tags")
        .with_description("The tags to automatically apply to log messages")
        .with_children(tag_handlers),

    yajlpp::property_handler("partitions")
        .with_description(
            "The partitions to automatically apply to log messages")
        .with_children(partition_handlers),

    yajlpp::property_handler("action").with_children(action_handlers),
    yajlpp::property_handler("sample#")
        .with_description("An array of sample log messages to be tested "
                          "against the log message patterns")
        .with_obj_provider(sample_provider)
        .with_children(sample_handlers),

    yajlpp::property_handler("line-format#")
        .with_description("The display format for JSON-encoded log messages")
        .with_obj_provider(line_format_provider)
        .add_cb(read_json_constant)
        .with_children(line_format_handlers),
    json_path_handler("search-table")
        .with_description(
            "Search tables to automatically define for this log format")
        .with_children(search_table_handlers),

    yajlpp::property_handler("highlights")
        .with_description("The set of highlight definitions")
        .with_children(highlight_handlers),

    yajlpp::property_handler("file-type")
        .with_synopsis("text|json|csv")
        .with_description("The type of file that contains the log messages")
        .with_enum_values(TYPE_ENUM)
        .for_field(&external_log_format::elf_type),

    yajlpp::property_handler("max-unrecognized-lines")
        .with_synopsis("<lines>")
        .with_description("The maximum number of lines in a file to use when "
                          "detecting the format")
        .with_min_value(1)
        .for_field(&log_format::lf_max_unrecognized_lines),
};

static int
read_id(yajlpp_parse_context* ypc, const unsigned char* str, size_t len, yajl_string_props_t*)
{
    auto* ud = static_cast<loader_userdata*>(ypc->ypc_userdata);
    auto file_id = std::string((const char*) str, len);

    ud->ud_file_schema = file_id;
    if (SUPPORTED_FORMAT_SCHEMAS.find(file_id)
        == SUPPORTED_FORMAT_SCHEMAS.end())
    {
        const auto* handler = ypc->ypc_current_handler;
        attr_line_t notes{"expecting one of the following $schema values:"};

        for (const auto& schema : SUPPORTED_FORMAT_SCHEMAS) {
            notes.append("\n").append(
                lnav::roles::symbol(fmt::format(FMT_STRING("  {}"), schema)));
        }
        ypc->report_error(
            lnav::console::user_message::error(
                attr_line_t("'")
                    .append(lnav::roles::symbol(file_id))
                    .append("' is not a supported log format $schema version"))
                .with_snippet(ypc->get_snippet())
                .with_note(notes)
                .with_help(handler->get_help_text(ypc)));
    }

    return 1;
}

const struct json_path_container root_format_handler = json_path_container{
    json_path_handler("$schema", read_id)
        .with_synopsis("The URI of the schema for this file")
        .with_description("Specifies the type of this file"),

    yajlpp::pattern_property_handler("(?<format_name>\\w+)")
        .with_description("The definition of a log file format.")
        .with_obj_provider(ensure_format)
        .with_children(format_handlers),
}
    .with_schema_id(DEFAULT_FORMAT_SCHEMA);

static void
write_sample_file()
{
    for (const auto& bsf : lnav_format_json) {
        auto sample_path = lnav::paths::dotlnav()
            / fmt::format(FMT_STRING("formats/default/{}.sample"),
                          bsf.get_name());

        auto stat_res = lnav::filesystem::stat_file(sample_path);
        if (stat_res.isOk()) {
            auto st = stat_res.unwrap();
            if (st.st_mtime >= lnav::filesystem::self_mtime()) {
                log_debug("skipping writing sample: %s (mtimes %d >= %d)",
                          bsf.get_name(),
                          st.st_mtime,
                          lnav::filesystem::self_mtime());
                continue;
            }
            log_debug("sample file needs to be updated: %s", bsf.get_name());
        } else {
            log_debug("sample file does not exist: %s", bsf.get_name());
        }

        auto sfp = bsf.to_string_fragment_producer();
        auto write_res = lnav::filesystem::write_file(
            sample_path,
            *sfp,
            {lnav::filesystem::write_file_options::read_only});

        if (write_res.isErr()) {
            auto msg = write_res.unwrapErr();
            fprintf(stderr,
                    "error:unable to write default format file: %s -- %s\n",
                    sample_path.c_str(),
                    msg.c_str());
        }
    }

    for (const auto& bsf : lnav_sh_scripts) {
        auto sh_path = lnav::paths::dotlnav()
            / fmt::format(FMT_STRING("formats/default/{}"), bsf.get_name());
        auto stat_res = lnav::filesystem::stat_file(sh_path);
        if (stat_res.isOk()) {
            auto st = stat_res.unwrap();
            if (st.st_mtime >= lnav::filesystem::self_mtime()) {
                continue;
            }
        }

        auto sfp = bsf.to_string_fragment_producer();
        auto write_res = lnav::filesystem::write_file(
            sh_path,
            *sfp,
            {
                lnav::filesystem::write_file_options::executable,
                lnav::filesystem::write_file_options::read_only,
            });

        if (write_res.isErr()) {
            auto msg = write_res.unwrapErr();
            fprintf(stderr,
                    "error:unable to write default text file: %s -- %s\n",
                    sh_path.c_str(),
                    msg.c_str());
        }
    }

    for (const auto& bsf : lnav_scripts) {
        script_metadata meta;
        auto sf = bsf.to_string_fragment_producer()->to_string();

        meta.sm_name = bsf.get_name();
        extract_metadata(sf, meta);
        auto path
            = fmt::format(FMT_STRING("formats/default/{}.lnav"), meta.sm_name);
        auto script_path = lnav::paths::dotlnav() / path;
        auto stat_res = lnav::filesystem::stat_file(script_path);
        if (stat_res.isOk()) {
            auto st = stat_res.unwrap();
            if (st.st_mtime >= lnav::filesystem::self_mtime()) {
                continue;
            }
        }

        auto write_res = lnav::filesystem::write_file(
            script_path,
            sf,
            {
                lnav::filesystem::write_file_options::executable,
                lnav::filesystem::write_file_options::read_only,
            });
        if (write_res.isErr()) {
            fprintf(stderr,
                    "error:unable to write default script file: %s -- %s\n",
                    script_path.c_str(),
                    strerror(errno));
        }
    }
}

static void
format_error_reporter(const yajlpp_parse_context& ypc,
                      const lnav::console::user_message& msg)
{
    auto* ud = (loader_userdata*) ypc.ypc_userdata;

    ud->ud_errors->emplace_back(msg);
}

std::vector<intern_string_t>
load_format_file(const std::filesystem::path& filename,
                 std::vector<lnav::console::user_message>& errors)
{
    std::vector<intern_string_t> retval;
    loader_userdata ud;
    auto_fd fd;

    log_info("loading formats from file: %s", filename.c_str());
    yajlpp_parse_context ypc(intern_string::lookup(filename.string()),
                             &root_format_handler);
    ud.ud_parse_context = &ypc;
    ud.ud_format_path = filename;
    ud.ud_format_names = &retval;
    ud.ud_errors = &errors;
    ypc.ypc_userdata = &ud;
    ypc.with_obj(ud);
    if ((fd = lnav::filesystem::openp(filename, O_RDONLY)) == -1) {
        errors.emplace_back(
            lnav::console::user_message::error(
                attr_line_t("unable to open format file: ")
                    .append(lnav::roles::file(filename.string())))
                .with_errno_reason());
    } else {
        auto_mem<yajl_handle_t> handle(yajl_free);
        char buffer[2048];
        off_t offset = 0;
        ssize_t rc = -1;

        handle = yajl_alloc(&ypc.ypc_callbacks, nullptr, &ypc);
        ypc.with_handle(handle).with_error_reporter(format_error_reporter);
        yajl_config(handle, yajl_allow_comments, 1);
        while (true) {
            rc = read(fd, buffer, sizeof(buffer));
            if (rc == 0) {
                break;
            }
            if (rc == -1) {
                errors.emplace_back(
                    lnav::console::user_message::error(
                        attr_line_t("unable to read format file: ")
                            .append(lnav::roles::file(filename.string())))
                        .with_errno_reason());
                break;
            }
            if (offset == 0 && (rc > 2) && (buffer[0] == '#')
                && (buffer[1] == '!'))
            {
                // Turn it into a JavaScript comment.
                buffer[0] = buffer[1] = '/';
            }
            if (ypc.parse((const unsigned char*) buffer, rc) != yajl_status_ok)
            {
                break;
            }
            offset += rc;
        }
        if (rc == 0) {
            ypc.complete_parse();
        }

        if (ud.ud_file_schema.empty()) {
            static const auto SCHEMA_LINE
                = attr_line_t()
                      .append(
                          fmt::format(FMT_STRING("    \"$schema\": \"{}\","),
                                      *SUPPORTED_FORMAT_SCHEMAS.begin()))
                      .with_attr_for_all(VC_ROLE.value(role_t::VCR_QUOTED_CODE))
                      .move();

            errors.emplace_back(
                lnav::console::user_message::warning(
                    attr_line_t("format file is missing ")
                        .append_quoted("$schema"_symbol)
                        .append(" property"))
                    .with_snippet(lnav::console::snippet::from(
                        intern_string::lookup(filename.string()), ""))
                    .with_note("the schema specifies the supported format "
                               "version and can be used with editors to "
                               "automatically validate the file")
                    .with_help(attr_line_t("add the following property to the "
                                           "top-level JSON object:\n")
                                   .append(SCHEMA_LINE)));
        }
    }

    return retval;
}

static void
load_from_path(const std::filesystem::path& path,
               std::vector<lnav::console::user_message>& errors)
{
    auto format_path = path / "formats/*/*.json";
    static_root_mem<glob_t, globfree> gl;

    log_info("loading formats from path: %s", format_path.c_str());
    if (glob(format_path.c_str(), 0, nullptr, gl.inout()) == 0) {
        for (int lpc = 0; lpc < (int) gl->gl_pathc; lpc++) {
            auto filepath = std::filesystem::path(gl->gl_pathv[lpc]);

            if (startswith(filepath.filename().string(), "config.")) {
                log_info("  not loading config as format: %s",
                         filepath.c_str());
                continue;
            }

            auto format_list = load_format_file(filepath, errors);
            if (format_list.empty()) {
                log_warning("Empty format file: %s", filepath.c_str());
            } else {
                log_info("contents of format file '%s':", filepath.c_str());
                for (auto iter = format_list.begin(); iter != format_list.end();
                     ++iter)
                {
                    log_info("  found format: %s", iter->get());
                }
            }
        }
    }
}

void
load_formats(const std::vector<std::filesystem::path>& extra_paths,
             std::vector<lnav::console::user_message>& errors)
{
    auto default_source = lnav::paths::dotlnav() / "default";
    std::vector<intern_string_t> retval;
    struct loader_userdata ud;
    yajl_handle handle;

    write_sample_file();

    log_debug("Loading default formats");
    for (const auto& bsf : lnav_format_json) {
        yajlpp_parse_context ypc_builtin(intern_string::lookup(bsf.get_name()),
                                         &root_format_handler);
        handle = yajl_alloc(&ypc_builtin.ypc_callbacks, nullptr, &ypc_builtin);
        ud.ud_parse_context = &ypc_builtin;
        ud.ud_format_names = &retval;
        ud.ud_errors = &errors;
        ypc_builtin.with_obj(ud)
            .with_handle(handle)
            .with_error_reporter(format_error_reporter)
            .ypc_userdata
            = &ud;
        yajl_config(handle, yajl_allow_comments, 1);
        auto sf = bsf.to_string_fragment_producer();
        ypc_builtin.parse(*sf);
        yajl_free(handle);
    }

    for (const auto& extra_path : extra_paths) {
        load_from_path(extra_path, errors);
    }

    uint8_t mod_counter = 0;

    std::vector<std::shared_ptr<external_log_format>> alpha_ordered_formats;
    for (auto iter = LOG_FORMATS.begin(); iter != LOG_FORMATS.end(); ++iter) {
        auto& elf = iter->second;
        elf->build(errors);

        if (elf->elf_has_module_format) {
            mod_counter += 1;
            elf->lf_mod_index = mod_counter;
        }

        for (auto& check_iter : LOG_FORMATS) {
            if (iter->first == check_iter.first) {
                continue;
            }

            auto& check_elf = check_iter.second;
            if (elf->match_samples(check_elf->elf_samples)) {
                log_warning(
                    "Format collision, format '%s' matches sample from '%s'",
                    elf->get_name().get(),
                    check_elf->get_name().get());
                elf->elf_collision.push_back(check_elf->get_name());
            }
        }

        alpha_ordered_formats.push_back(elf);
    }

    auto& graph_ordered_formats = external_log_format::GRAPH_ORDERED_FORMATS;

    while (!alpha_ordered_formats.empty()) {
        std::vector<intern_string_t> popped_formats;

        for (auto iter = alpha_ordered_formats.begin();
             iter != alpha_ordered_formats.end();)
        {
            auto elf = *iter;
            if (elf->elf_collision.empty()) {
                iter = alpha_ordered_formats.erase(iter);
                popped_formats.push_back(elf->get_name());
                graph_ordered_formats.push_back(elf);
            } else {
                ++iter;
            }
        }

        if (popped_formats.empty() && !alpha_ordered_formats.empty()) {
            bool broke_cycle = false;

            log_warning("Detected a cycle...");
            for (const auto& elf : alpha_ordered_formats) {
                if (elf->elf_builtin_format) {
                    log_warning("  Skipping builtin format -- %s",
                                elf->get_name().get());
                } else {
                    log_warning("  Breaking cycle by picking -- %s",
                                elf->get_name().get());
                    elf->elf_collision.clear();
                    broke_cycle = true;
                    break;
                }
            }
            if (!broke_cycle) {
                alpha_ordered_formats.front()->elf_collision.clear();
            }
        }

        for (const auto& elf : alpha_ordered_formats) {
            for (auto& popped_format : popped_formats) {
                elf->elf_collision.remove(popped_format);
            }
        }
    }

    log_info("Format order:") for (auto& graph_ordered_format :
                                   graph_ordered_formats)
    {
        log_info("  %s", graph_ordered_format->get_name().get());
    }

    auto& roots = log_format::get_root_formats();
    auto iter = std::find_if(roots.begin(), roots.end(), [](const auto& elem) {
        return elem->get_name() == "generic_log";
    });
    roots.insert(
        iter, graph_ordered_formats.begin(), graph_ordered_formats.end());
}

static void
exec_sql_in_path(sqlite3* db,
                 const std::map<std::string, scoped_value_t>& global_vars,
                 const std::filesystem::path& path,
                 std::vector<lnav::console::user_message>& errors)
{
    auto format_path = path / "formats/*/*.sql";
    static_root_mem<glob_t, globfree> gl;

    log_info("executing SQL files in path: %s", format_path.c_str());
    if (glob(format_path.c_str(), 0, nullptr, gl.inout()) == 0) {
        for (int lpc = 0; lpc < (int) gl->gl_pathc; lpc++) {
            auto filename = std::filesystem::path(gl->gl_pathv[lpc]);
            auto read_res = lnav::filesystem::read_file(filename);

            if (read_res.isOk()) {
                log_info("Executing SQL file: %s", filename.c_str());
                auto content = read_res.unwrap();

                sql_execute_script(
                    db, global_vars, filename.c_str(), content.c_str(), errors);
            } else {
                errors.emplace_back(
                    lnav::console::user_message::error(
                        attr_line_t("unable to read format file: ")
                            .append(lnav::roles::file(filename.string())))
                        .with_reason(read_res.unwrapErr()));
            }
        }
    }
}

void
load_format_extra(sqlite3* db,
                  const std::map<std::string, scoped_value_t>& global_vars,
                  const std::vector<std::filesystem::path>& extra_paths,
                  std::vector<lnav::console::user_message>& errors)
{
    for (const auto& extra_path : extra_paths) {
        exec_sql_in_path(db, global_vars, extra_path, errors);
    }
}

static void
extract_metadata(string_fragment contents, script_metadata& meta_out)
{
    static const auto SYNO_RE = lnav::pcre2pp::code::from_const(
        "^#\\s+@synopsis:(.*)$", PCRE2_MULTILINE);
    static const auto DESC_RE = lnav::pcre2pp::code::from_const(
        "^#\\s+@description:(.*)$", PCRE2_MULTILINE);
    static const auto OUTPUT_FORMAT_RE = lnav::pcre2pp::code::from_const(
        "^#\\s+@output-format:\\s+(.*)$", PCRE2_MULTILINE);

    auto syno_md = SYNO_RE.create_match_data();
    auto syno_match_res
        = SYNO_RE.capture_from(contents).into(syno_md).matches().ignore_error();
    if (syno_match_res) {
        meta_out.sm_synopsis = syno_md[1]->trim().to_string();
    }
    auto desc_md = DESC_RE.create_match_data();
    auto desc_match_res
        = DESC_RE.capture_from(contents).into(desc_md).matches().ignore_error();
    if (desc_match_res) {
        meta_out.sm_description = desc_md[1]->trim().to_string();
    }

    auto out_format_md = OUTPUT_FORMAT_RE.create_match_data();
    auto out_format_res = OUTPUT_FORMAT_RE.capture_from(contents)
                              .into(out_format_md)
                              .matches()
                              .ignore_error();
    if (out_format_res) {
        auto out_format_frag = out_format_md[1]->trim();
        auto from_res = from<text_format_t>(out_format_frag);
        if (from_res.isErr()) {
            log_error("%s (%s): invalid @output-format '%.*s'",
                      meta_out.sm_name.c_str(),
                      meta_out.sm_path.c_str(),
                      out_format_frag.length(),
                      out_format_frag.data());
        } else {
            meta_out.sm_output_format = from_res.unwrap();
            log_info("%s (%s): setting output format to %d",
                     meta_out.sm_name.c_str(),
                     meta_out.sm_path.c_str(),
                     meta_out.sm_output_format);
        }
    }

    if (!meta_out.sm_synopsis.empty()) {
        size_t space = meta_out.sm_synopsis.find(' ');

        if (space == std::string::npos) {
            space = meta_out.sm_synopsis.size();
        }
        meta_out.sm_name = meta_out.sm_synopsis.substr(0, space);
    }
}

void
extract_metadata_from_file(struct script_metadata& meta_inout)
{
    auto stat_res = lnav::filesystem::stat_file(meta_inout.sm_path);
    if (stat_res.isErr()) {
        log_warning("unable to open script: %s -- %s",
                    meta_inout.sm_path.c_str(),
                    stat_res.unwrapErr().c_str());
        return;
    }

    auto st = stat_res.unwrap();
    if (!S_ISREG(st.st_mode)) {
        log_warning("script is not a regular file -- %s",
                    meta_inout.sm_path.c_str());
        return;
    }

    auto open_res = lnav::filesystem::open_file(meta_inout.sm_path, O_RDONLY);
    if (open_res.isErr()) {
        log_warning("unable to open script file: %s -- %s",
                    meta_inout.sm_path.c_str(),
                    open_res.unwrapErr().c_str());
        return;
    }

    auto fd = open_res.unwrap();
    char buffer[8 * 1024];
    auto rc = read(fd, buffer, sizeof(buffer));
    if (rc > 0) {
        extract_metadata(string_fragment::from_bytes(buffer, rc), meta_inout);
    }
}

static void
find_format_in_path(const std::filesystem::path& path,
                    available_scripts& scripts)
{
    for (const auto& format_path :
         {path / "formats/*/*.lnav", path / "configs/*/*.lnav"})
    {
        static_root_mem<glob_t, globfree> gl;

        log_debug("Searching for script in path: %s", format_path.c_str());
        if (glob(format_path.c_str(), 0, nullptr, gl.inout()) == 0) {
            for (size_t lpc = 0; lpc < gl->gl_pathc; lpc++) {
                const char* filename = basename(gl->gl_pathv[lpc]);
                auto script_name = std::string(filename, strlen(filename) - 5);
                struct script_metadata meta;

                meta.sm_path = gl->gl_pathv[lpc];
                meta.sm_name = script_name;
                extract_metadata_from_file(meta);
                scripts.as_scripts[script_name].push_back(meta);

                log_info("  found script: %s", meta.sm_path.c_str());
            }
        }
    }
}

void
find_format_scripts(const std::vector<std::filesystem::path>& extra_paths,
                    available_scripts& scripts)
{
    for (const auto& extra_path : extra_paths) {
        find_format_in_path(extra_path, scripts);
    }
}

void
load_format_vtabs(log_vtab_manager* vtab_manager,
                  std::vector<lnav::console::user_message>& errors)
{
    auto& root_formats = LOG_FORMATS;

    for (auto& root_format : root_formats) {
        root_format.second->register_vtabs(vtab_manager, errors);
    }
}
