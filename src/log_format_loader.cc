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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file log_format_loader.cc
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <libgen.h>
#include <sys/stat.h>

#include <map>
#include <string>
#include <fstream>

#include "fmt/format.h"

#include "yajlpp/yajlpp.hh"
#include "yajlpp/yajlpp_def.hh"
#include "lnav_config.hh"
#include "log_format.hh"
#include "auto_fd.hh"
#include "sql_util.hh"
#include "builtin-scripts.h"
#include "builtin-sh-scripts.h"
#include "default-formats.h"

#include "log_format_loader.hh"
#include "bin2c.h"

using namespace std;

static void extract_metadata(const char *contents, size_t len, struct script_metadata &meta_out);

typedef map<intern_string_t, external_log_format *> log_formats_map_t;

static log_formats_map_t LOG_FORMATS;

struct userdata {
    std::string ud_format_path;
    vector<intern_string_t> *ud_format_names{nullptr};
    std::vector<std::string> *ud_errors{nullptr};
};

static external_log_format *ensure_format(const yajlpp_provider_context &ypc, userdata *ud)
{
    const intern_string_t name = ypc.get_substr_i(0);
    vector<intern_string_t> *formats = ud->ud_format_names;
    external_log_format *retval;

    retval = LOG_FORMATS[name];
    if (retval == nullptr) {
        LOG_FORMATS[name] = retval = new external_log_format(name);
        log_debug("Loading format -- %s", name.get());
    }
    retval->elf_source_path.insert(ud->ud_format_path.substr(0, ud->ud_format_path.rfind('/')));

    if (find(formats->begin(), formats->end(), name) == formats->end()) {
        formats->push_back(name);
    }

    if (ud->ud_format_path.empty()) {
        retval->elf_builtin_format = true;
    }

    return retval;
}

static external_log_format::pattern *pattern_provider(const yajlpp_provider_context &ypc, external_log_format *elf)
{
    string regex_name = ypc.get_substr(0);
    auto &pat = elf->elf_patterns[regex_name];

    if (pat.get() == nullptr) {
        pat = make_shared<external_log_format::pattern>();
    }

    if (pat->p_config_path.empty()) {
        pat->p_config_path = elf->get_name().to_string() + "/regex/" + regex_name;
    }

    return pat.get();
}

static external_log_format::value_def *value_def_provider(const yajlpp_provider_context &ypc, external_log_format *elf)
{
    const intern_string_t value_name = ypc.get_substr_i(0);

    auto iter = elf->elf_value_defs.find(value_name);
    shared_ptr<external_log_format::value_def> retval;

    if (iter == elf->elf_value_defs.end()) {
        retval = make_shared<external_log_format::value_def>();
        retval->vd_name = value_name;
        elf->elf_value_defs[value_name] = retval;
        elf->elf_value_def_order.emplace_back(retval);
    } else {
        retval = iter->second;
    }

    return retval.get();
}

static scaling_factor *scaling_factor_provider(const yajlpp_provider_context &ypc, external_log_format::value_def *value_def)
{
    string scale_spec = ypc.get_substr(0);

    const intern_string_t scale_name = intern_string::lookup(scale_spec.substr(1));
    scaling_factor &retval = value_def->vd_unit_scaling[scale_name];

    if (scale_spec[0] == '/') {
        retval.sf_op = scale_op_t::SO_DIVIDE;
    }
    else if (scale_spec[0] == '*') {
        retval.sf_op = scale_op_t::SO_MULTIPLY;
    }

    return &retval;
}

static external_log_format::json_format_element &
ensure_json_format_element(external_log_format *elf, int index)
{
    elf->jlf_line_format.resize(index + 1);

    return elf->jlf_line_format[index];
}

static external_log_format::json_format_element *line_format_provider(
    const yajlpp_provider_context &ypc, external_log_format *elf)
{
    auto &jfe = ensure_json_format_element(elf, ypc.ypc_index);

    jfe.jfe_type = external_log_format::JLF_VARIABLE;

    return &jfe;
}

static int read_format_bool(yajlpp_parse_context *ypc, int val)
{
    auto elf = (external_log_format *) ypc->ypc_obj_stack.top();
    string field_name = ypc->get_path_fragment(1);

    if (field_name == "convert-to-local-time")
        elf->lf_date_time.dts_local_time = val;
    else if (field_name == "json") {
        if (val) {
            elf->elf_type = external_log_format::ELF_TYPE_JSON;
        }
    }
    else if (field_name == "hide-extra")
        elf->jlf_hide_extra = val;
    else if (field_name == "multiline")
        elf->elf_multiline = val;

    return 1;
}

static int read_format_double(yajlpp_parse_context *ypc, double val)
{
    auto elf = (external_log_format *) ypc->ypc_obj_stack.top();
    string field_name = ypc->get_path_fragment(1);

    if (field_name == "timestamp-divisor") {
        if (val <= 0) {
            fprintf(stderr, "error:%s: timestamp-divisor cannot be less "
                "than or equal to zero\n",
                ypc->get_path_fragment(0).c_str());
            return 0;
        }
        elf->elf_timestamp_divisor = val;
    }

    return 1;
}

static int read_format_int(yajlpp_parse_context *ypc, long long val)
{
    auto elf = (external_log_format *) ypc->ypc_obj_stack.top();
    string field_name = ypc->get_path_fragment(1);

    if (field_name == "timestamp-divisor") {
        if (val <= 0) {
            fprintf(stderr, "error:%s: timestamp-divisor cannot be less "
                "than or equal to zero\n",
                ypc->get_path_fragment(0).c_str());
            return 0;
        }
        elf->elf_timestamp_divisor = val;
    }

    return 1;
}

static int read_format_field(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    auto elf = (external_log_format *) ypc->ypc_obj_stack.top();
    string value = string((const char *)str, len);
    string field_name = ypc->get_path_fragment(1);

    if (field_name == "file-pattern") {
        elf->elf_file_pattern = value;
    }
    else if (field_name == "level-field") {
        elf->elf_level_field = intern_string::lookup(value);
    }
    else if (field_name == "timestamp-field") {
        elf->lf_timestamp_field = intern_string::lookup(value);
    }
    else if (field_name == "body-field") {
        elf->elf_body_field = intern_string::lookup(value);
    }
    else if (field_name == "timestamp-format") {
        elf->lf_timestamp_format.push_back(intern_string::lookup(value)->get());
    }
    else if (field_name == "module-field") {
        elf->elf_module_id_field = intern_string::lookup(value);
        elf->elf_container = true;
    }
    else if (field_name == "opid-field") {
        elf->elf_opid_field = intern_string::lookup(value);
    }

    return 1;
}

static int read_levels(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    auto elf = (external_log_format *) ypc->ypc_obj_stack.top();
    string regex = string((const char *)str, len);
    string level_name_or_number = ypc->get_path_fragment(2);
    log_level_t level = string2level(level_name_or_number.c_str());
    elf->elf_level_patterns[level].lp_regex = regex;

    return 1;
}

static int read_level_int(yajlpp_parse_context *ypc, long long val)
{
    auto elf = (external_log_format *) ypc->ypc_obj_stack.top();
    string level_name_or_number = ypc->get_path_fragment(2);
    log_level_t level = string2level(level_name_or_number.c_str());

    elf->elf_level_pairs.emplace_back(val, level);

    return 1;
}

static int read_action_def(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    auto elf = (external_log_format *) ypc->ypc_obj_stack.top();
    string action_name = ypc->get_path_fragment(2);
    string field_name = ypc->get_path_fragment(3);
    string val = string((const char *)str, len);

    elf->lf_action_defs[action_name].ad_name = action_name;
    if (field_name == "label")
        elf->lf_action_defs[action_name].ad_label = val;

    return 1;
}

static int read_action_bool(yajlpp_parse_context *ypc, int val)
{
    auto elf = (external_log_format *) ypc->ypc_obj_stack.top();
    string action_name = ypc->get_path_fragment(2);
    string field_name = ypc->get_path_fragment(3);

    elf->lf_action_defs[action_name].ad_capture_output = val;

    return 1;
}

static int read_action_cmd(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    auto elf = (external_log_format *) ypc->ypc_obj_stack.top();
    string action_name = ypc->get_path_fragment(2);
    string field_name = ypc->get_path_fragment(3);
    string val = string((const char *)str, len);

    elf->lf_action_defs[action_name].ad_name = action_name;
    elf->lf_action_defs[action_name].ad_cmdline.push_back(val);

    return 1;
}

static external_log_format::sample &ensure_sample(external_log_format *elf,
                                                  int index)
{
    elf->elf_samples.resize(index + 1);

    return elf->elf_samples[index];
}

static external_log_format::sample *sample_provider(const yajlpp_provider_context &ypc, external_log_format *elf)
{
    external_log_format::sample &sample = ensure_sample(elf, ypc.ypc_index);

    return &sample;
}

static int read_json_constant(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    auto val = string((const char *) str, len);
    auto elf = (external_log_format *) ypc->ypc_obj_stack.top();

    ypc->ypc_array_index.back() += 1;
    auto &jfe = ensure_json_format_element(elf, ypc->ypc_array_index.back());
    jfe.jfe_type = external_log_format::JLF_CONSTANT;
    jfe.jfe_default_value = val;

    return 1;
}

static int create_search_table(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    auto elf = (external_log_format *) ypc->ypc_obj_stack.top();
    const intern_string_t table_name = ypc->get_path_fragment_i(2);
    string regex = string((const char *) str, len);

    elf->elf_search_tables.emplace_back(table_name, regex);

    return 1;
}


static struct json_path_container pattern_handlers = {
    json_path_handler("pattern")
        .with_synopsis("<message-regex>")
        .with_description(
            "The regular expression to match a log message and capture fields.")
        .with_min_length(1)
        .FOR_FIELD(external_log_format::pattern, p_string),
    json_path_handler("module-format")
        .with_synopsis("<bool>")
        .with_description(
            "If true, this pattern will only be used to parse message bodies "
                "of container formats, like syslog")
        .FOR_FIELD(external_log_format::pattern, p_module_format)
};

static const json_path_handler_base::enum_value_t ALIGN_ENUM[] = {
    { "left", external_log_format::json_format_element::align_t::LEFT },
    { "right", external_log_format::json_format_element::align_t::RIGHT },

    json_path_handler_base::ENUM_TERMINATOR
};

static const json_path_handler_base::enum_value_t OVERFLOW_ENUM[] = {
    { "abbrev", external_log_format::json_format_element::overflow_t::ABBREV },
    { "truncate", external_log_format::json_format_element::overflow_t::TRUNCATE },
    { "dot-dot", external_log_format::json_format_element::overflow_t::DOTDOT },

    json_path_handler_base::ENUM_TERMINATOR
};

static const json_path_handler_base::enum_value_t TRANSFORM_ENUM[] = {
    { "none", external_log_format::json_format_element::transform_t::NONE },
    { "uppercase", external_log_format::json_format_element::transform_t::UPPERCASE },
    { "lowercase", external_log_format::json_format_element::transform_t::LOWERCASE },
    { "capitalize", external_log_format::json_format_element::transform_t::CAPITALIZE },

    json_path_handler_base::ENUM_TERMINATOR
};

static struct json_path_container line_format_handlers = {
    json_path_handler("field")
        .with_synopsis("<field-name>")
        .with_description("The name of the field to substitute at this position")
        .with_min_length(1)
        .FOR_FIELD(external_log_format::json_format_element, jfe_value),

    json_path_handler("default-value")
        .with_synopsis("<string>")
        .with_description("The default value for this position if the field is null")
        .FOR_FIELD(external_log_format::json_format_element, jfe_default_value),

    json_path_handler("timestamp-format")
        .with_synopsis("<string>")
        .with_min_length(1)
        .with_description("The strftime(3) format for this field")
        .FOR_FIELD(external_log_format::json_format_element, jfe_ts_format),

    json_path_handler("min-width")
        .with_min_value(0)
        .with_synopsis("<size>")
        .with_description("The minimum width of the field")
        .FOR_FIELD(external_log_format::json_format_element, jfe_min_width),

    json_path_handler("max-width")
        .with_min_value(0)
        .with_synopsis("<size>")
        .with_description("The maximum width of the field")
        .FOR_FIELD(external_log_format::json_format_element, jfe_max_width),

    json_path_handler("align")
        .with_synopsis("left|right")
        .with_description("Align the text in the column to the left or right side")
        .with_enum_values(ALIGN_ENUM)
        .FOR_FIELD(external_log_format::json_format_element, jfe_align),

    json_path_handler("overflow")
        .with_synopsis("abbrev|truncate|dot-dot")
        .with_description("Overflow style")
        .with_enum_values(OVERFLOW_ENUM)
        .FOR_FIELD(external_log_format::json_format_element, jfe_overflow),

    json_path_handler("text-transform")
        .with_synopsis("none|uppercase|lowercase|capitalize")
        .with_description("Text transformation")
        .with_enum_values(TRANSFORM_ENUM)
        .FOR_FIELD(external_log_format::json_format_element, jfe_text_transform)
};

static const json_path_handler_base::enum_value_t KIND_ENUM[] = {
    {"string", logline_value::VALUE_TEXT},
    {"integer", logline_value::VALUE_INTEGER},
    {"float", logline_value::VALUE_FLOAT},
    {"boolean", logline_value::VALUE_BOOLEAN},
    {"json", logline_value::VALUE_JSON},
    {"struct", logline_value::VALUE_STRUCT},
    {"quoted", logline_value::VALUE_QUOTED},

    json_path_handler_base::ENUM_TERMINATOR
};

static struct json_path_container scale_handlers = {
    json_path_handler(pcrepp("(?<scale>.*)"))
        .with_synopsis("[*,/]<unit>")
        .FOR_FIELD(scaling_factor, sf_value)
};

static struct json_path_container unit_handlers = {
    json_path_handler("field")
        .with_synopsis("<field-name>")
        .with_description("The name of the field that contains the units for this field")
        .FOR_FIELD(external_log_format::value_def, vd_unit_field),

    json_path_handler("scaling-factor")
        .with_description("Transforms the numeric value by the given factor")
        .with_obj_provider(scaling_factor_provider)
        .with_children(scale_handlers),
};

static struct json_path_container value_def_handlers = {
    json_path_handler("kind")
        .with_synopsis("string|integer|float|boolean|json|quoted")
        .with_description("The type of data in the field")
        .with_enum_values(KIND_ENUM)
        .FOR_FIELD(external_log_format::value_def, vd_kind),

    json_path_handler("collate")
        .with_synopsis("<function>")
        .with_description("The collating function to use for this column")
        .FOR_FIELD(external_log_format::value_def, vd_collate),

    json_path_handler("unit")
        .with_description("Unit definitions for this field")
        .with_children(unit_handlers),

    json_path_handler("identifier")
        .with_synopsis("<bool>")
        .with_description("Indicates whether or not this field contains an identifier that should be highlighted")
        .FOR_FIELD(external_log_format::value_def, vd_identifier),

    json_path_handler("foreign-key")
        .with_synopsis("<bool>")
        .with_description("Indicates whether or not this field should be treated as a foreign key for row in another table")
        .FOR_FIELD(external_log_format::value_def, vd_foreign_key),

    json_path_handler("hidden")
        .with_synopsis("<bool>")
        .with_description("Indicates whether or not this field should be hidden")
        .FOR_FIELD(external_log_format::value_def, vd_hidden),

    json_path_handler("action-list#")
        .with_synopsis("<string>")
        .with_description("Actions to execute when this field is clicked on")
        .FOR_FIELD(external_log_format::value_def, vd_action_list),

    json_path_handler("rewriter")
        .with_synopsis("<command>")
        .with_description("A command that will rewrite this field when pretty-printing")
        .FOR_FIELD(external_log_format::value_def, vd_rewriter),

    json_path_handler("description")
        .with_synopsis("<string>")
        .with_description("A description of the field")
        .FOR_FIELD(external_log_format::value_def, vd_description)
};

static struct json_path_container highlighter_def_handlers = {
    json_path_handler("pattern")
        .with_synopsis("<regex>")
        .with_description("A regular expression to highlight in logs of this format.")
        .FOR_FIELD(external_log_format::highlighter_def, hd_pattern),

    json_path_handler("color")
        .with_synopsis("#<hex>|<name>")
        .with_description("The color to use when highlighting this pattern.")
        .FOR_FIELD(external_log_format::highlighter_def, hd_color),

    json_path_handler("background-color")
        .with_synopsis("#<hex>|<name>")
        .with_description("The background color to use when highlighting this pattern.")
        .FOR_FIELD(external_log_format::highlighter_def, hd_background_color),

    json_path_handler("underline")
        .with_synopsis("<enabled>")
        .with_description("Highlight this pattern with an underline.")
        .FOR_FIELD(external_log_format::highlighter_def, hd_underline),

    json_path_handler("blink")
        .with_synopsis("<enabled>")
        .with_description("Highlight this pattern by blinking.")
        .FOR_FIELD(external_log_format::highlighter_def, hd_blink)
};

static const json_path_handler_base::enum_value_t LEVEL_ENUM[] = {
    { level_names[LEVEL_TRACE], LEVEL_TRACE },
    { level_names[LEVEL_DEBUG5], LEVEL_DEBUG5 },
    { level_names[LEVEL_DEBUG4], LEVEL_DEBUG4 },
    { level_names[LEVEL_DEBUG3], LEVEL_DEBUG3 },
    { level_names[LEVEL_DEBUG2], LEVEL_DEBUG2 },
    { level_names[LEVEL_DEBUG], LEVEL_DEBUG },
    { level_names[LEVEL_INFO], LEVEL_INFO },
    { level_names[LEVEL_STATS], LEVEL_STATS },
    { level_names[LEVEL_NOTICE], LEVEL_NOTICE },
    { level_names[LEVEL_WARNING], LEVEL_WARNING },
    { level_names[LEVEL_ERROR], LEVEL_ERROR },
    { level_names[LEVEL_CRITICAL], LEVEL_CRITICAL },
    { level_names[LEVEL_FATAL], LEVEL_FATAL },

    json_path_handler_base::ENUM_TERMINATOR
};

static struct json_path_container sample_handlers = {
    json_path_handler("line")
        .with_synopsis("<log-line>")
        .with_description("A sample log line that should match a pattern in this format.")
        .FOR_FIELD(external_log_format::sample, s_line),

    json_path_handler("level")
        .with_enum_values(LEVEL_ENUM)
        .with_description("The expected level for this sample log line.")
        .FOR_FIELD(external_log_format::sample, s_level)
};

static const json_path_handler_base::enum_value_t TYPE_ENUM[] = {
    { "text", external_log_format::elf_type_t::ELF_TYPE_TEXT },
    { "json", external_log_format::elf_type_t::ELF_TYPE_JSON },
    { "csv", external_log_format::elf_type_t::ELF_TYPE_CSV },

    json_path_handler_base::ENUM_TERMINATOR
};

static struct json_path_container regex_handlers = {
    json_path_handler(pcrepp(R"((?<pattern_name>[^/]+))"))
        .with_description("The set of patterns used to match log messages")
        .with_obj_provider(pattern_provider)
        .with_children(pattern_handlers),
};

static struct json_path_container level_handlers = {
    json_path_handler(
        pcrepp("(?<level>trace|debug[2345]?|info|stats|notice|warning|error|critical|fatal)"))
        .add_cb(read_levels)
        .add_cb(read_level_int)
        .with_synopsis("<pattern|integer>")
        .with_description("The regular expression used to match the log text for this level.  "
                          "For JSON logs with numeric levels, this should be the number for the corresponding level.")
};

static struct json_path_container value_handlers = {
    json_path_handler(pcrepp("(?<value_name>[^/]+)"))
        .with_description("The set of values captured by the log message patterns")
        .with_obj_provider(value_def_provider)
        .with_children(value_def_handlers)
};

static struct json_path_container highlight_handlers = {
    json_path_handler(pcrepp(R"((?<highlight_name>[^/]+))"))
        .with_description("The definition of a highlight")
        .with_obj_provider<external_log_format::highlighter_def, external_log_format>([](const yajlpp_provider_context &ypc, external_log_format *root) {
            return &(root->elf_highlighter_patterns[ypc.get_substr_i(0)]);
        })
        .with_children(highlighter_def_handlers)
};

static struct json_path_container action_def_handlers = {
    json_path_handler("label", read_action_def),
    json_path_handler("capture-output", read_action_bool),
    json_path_handler("cmd#", read_action_cmd)
};

static struct json_path_container action_handlers = {
    json_path_handler(pcrepp("(?<action_name>\\w+)"), read_action_def)
        .with_children(action_def_handlers)
};

static struct json_path_container search_table_def_handlers = {
    json_path_handler("pattern", create_search_table)
        .with_synopsis("<regex>")
        .with_description("The regular expression for this search table."),
};

static struct json_path_container search_table_handlers = {
    json_path_handler(pcrepp("\\w+"))
        .with_description("The set of search tables to be automatically defined")
        .with_children(search_table_def_handlers)
};

struct json_path_container format_handlers = {
    json_path_handler("regex")
        .with_description("The set of regular expressions used to match log messages")
        .with_children(regex_handlers),

    json_path_handler("json", read_format_bool)
        .with_description(R"(Indicates that log files are JSON-encoded (deprecated, use "file-type": "json"))"),
    json_path_handler("convert-to-local-time", read_format_bool)
        .with_description("Indicates that displayed timestamps should automatically be converted to local time"),
    json_path_handler("hide-extra", read_format_bool)
        .with_description("Specifies whether extra values in JSON logs should be displayed"),
    json_path_handler("multiline", read_format_bool)
        .with_description("Indicates that log messages can span multiple lines"),
    json_path_handler("timestamp-divisor", read_format_double)
        .add_cb(read_format_int)
        .with_synopsis("<number>")
        .with_description("The value to divide a numeric timestamp by in a JSON log."),
    json_path_handler("file-pattern", read_format_field)
        .with_description("A regular expression that restricts this format to log files with a matching name"),
    json_path_handler("level-field", read_format_field)
        .with_description("The name of the level field in the log message pattern"),
    json_path_handler("timestamp-field", read_format_field)
        .with_description("The name of the timestamp field in the log message pattern"),
    json_path_handler("body-field", read_format_field)
        .with_description("The name of the body field in the log message pattern"),
    json_path_handler("url", pcrepp("url#?"))
        .add_cb(read_format_field)
        .with_description("A URL with more information about this log format"),
    json_path_handler("title", read_format_field)
        .with_description("The human-readable name for this log format"),
    json_path_handler("description", read_format_field)
        .with_description("A longer description of this log format"),
    json_path_handler("timestamp-format#", read_format_field)
        .with_description("An array of strptime(3)-like timestamp formats"),
    json_path_handler("module-field", read_format_field)
        .with_description("The name of the module field in the log message pattern"),
    json_path_handler("opid-field", read_format_field)
        .with_description("The name of the operation-id field in the log message pattern"),
    json_path_handler("ordered-by-time")
        .with_synopsis("<bool>")
        .with_description("Indicates that the order of messages in the file is time-based.")
        .FOR_FIELD(log_format, lf_time_ordered),
    json_path_handler("level")
        .with_description("The map of level names to patterns or integer values")
        .with_children(level_handlers),

    json_path_handler("value")
        .with_description("The set of value definitions")
        .with_children(value_handlers),

    json_path_handler("action")
        .with_children(action_handlers),
    json_path_handler("sample#")
        .with_description("An array of sample log messages to be tested against the log message patterns")
        .with_obj_provider(sample_provider)
        .with_children(sample_handlers),

    json_path_handler("line-format#")
        .with_description("The display format for JSON-encoded log messages")
        .with_obj_provider(line_format_provider)
        .add_cb(read_json_constant)
        .with_children(line_format_handlers),
    json_path_handler("search-table", create_search_table)
        .with_description("Search tables to automatically define for this log format")
        .with_children(search_table_handlers),

    json_path_handler("highlights")
        .with_description("The set of highlight definitions")
        .with_children(highlight_handlers),

    json_path_handler("file-type")
        .with_synopsis("text|json|csv")
        .with_description("The type of file that contains the log messages")
        .with_enum_values(TYPE_ENUM)
        .FOR_FIELD(external_log_format, elf_type)
};

static vector<string> SUPPORTED_FORMAT_SCHEMAS = {
    "https://lnav.org/schemas/format-v1.schema.json",
};

static int read_id(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    auto file_id = string((const char *) str, len);

    if (find(SUPPORTED_FORMAT_SCHEMAS.begin(),
             SUPPORTED_FORMAT_SCHEMAS.end(),
             file_id) == SUPPORTED_FORMAT_SCHEMAS.end()) {
        fprintf(stderr, "%s:%d: error: unsupported format $schema -- %s\n",
                ypc->ypc_source.c_str(), ypc->get_line_number(), file_id.c_str());
        return 0;
    }

    return 1;
}

struct json_path_container root_format_handler = json_path_container {
    json_path_handler("$schema", read_id)
        .with_synopsis("The URI of the schema for this file")
        .with_description("Specifies the type of this file"),

    json_path_handler(pcrepp("(?<format_name>\\w+)"))
        .with_description("The definition of a log file format.")
        .with_obj_provider(ensure_format)
        .with_children(format_handlers)
}
    .with_schema_id(SUPPORTED_FORMAT_SCHEMAS.back());

static void write_sample_file()
{
    for (int lpc = 0; lnav_format_json[lpc].bsf_name; lpc++) {
        auto &bsf = lnav_format_json[lpc];
        auto sample_path = dotlnav_path() /
            fmt::format("formats/default/{}.sample", bsf.bsf_name);
        auto_fd sample_fd;

        if ((sample_fd = openp(sample_path,
                               O_WRONLY | O_TRUNC | O_CREAT,
                               0644)) == -1 ||
            (write(sample_fd.get(), bsf.bsf_data, bsf.bsf_size) == -1)) {
            perror("error: unable to write default format file");
        }
    }

    for (int lpc = 0; lnav_sh_scripts[lpc].bsf_name; lpc++) {
        struct bin_src_file &bsf = lnav_sh_scripts[lpc];
        auto sh_path = dotlnav_path() / fmt::format("formats/default/{}", bsf.bsf_name);
        auto_fd sh_fd;

        if ((sh_fd = openp(sh_path, O_WRONLY|O_TRUNC|O_CREAT, 0755)) == -1 ||
            write(sh_fd.get(), bsf.bsf_data, strlen((const char *) bsf.bsf_data)) == -1) {
            perror("error: unable to write default text file");
        }
    }

    for (int lpc = 0; lnav_scripts[lpc].bsf_name; lpc++) {
        struct script_metadata meta;
        struct bin_src_file &bsf = lnav_scripts[lpc];
        const char *script_content = reinterpret_cast<const char *>(bsf.bsf_data);
        auto_fd script_fd;
        char path[2048];
        size_t script_len;
        struct stat st;

        script_len = strlen(script_content);
        extract_metadata(script_content, script_len, meta);
        snprintf(path, sizeof(path), "formats/default/%s.lnav", meta.sm_name.c_str());
        auto script_path = dotlnav_path() / path;
        if (statp(script_path, &st) == 0 && (size_t) st.st_size == script_len) {
            // Assume it's the right contents and move on...
            continue;
        }
        if ((script_fd = openp(script_path, O_WRONLY|O_TRUNC|O_CREAT, 0755)) == -1 ||
            write(script_fd.get(), script_content, script_len) == -1) {
            perror("error: unable to write default text file");
        }
    }
}

static void format_error_reporter(const yajlpp_parse_context &ypc,
                                  lnav_log_level_t level,
                                  const char *msg)
{
    if (level >= lnav_log_level_t::ERROR) {
        struct userdata *ud = (userdata *) ypc.ypc_userdata;

        ud->ud_errors->push_back(msg);
    } else {
        fprintf(stderr, "warning:%s\n",  msg);
    }
}

std::vector<intern_string_t> load_format_file(const string &filename, std::vector<string> &errors)
{
    std::vector<intern_string_t> retval;
    struct userdata ud;
    auto_fd fd;

    log_info("loading formats from file: %s", filename.c_str());
    ud.ud_format_path = filename;
    ud.ud_format_names = &retval;
    ud.ud_errors = &errors;
    yajlpp_parse_context ypc(filename, &root_format_handler);
    ypc.ypc_userdata = &ud;
    ypc.with_obj(ud);
    if ((fd = open(filename.c_str(), O_RDONLY)) == -1) {
        char errmsg[1024];

        snprintf(errmsg, sizeof(errmsg),
                 "error:unable to open format file '%s' -- %s",
                 filename.c_str(),
                 strerror(errno));
        errors.emplace_back(errmsg);
    }
    else {
        auto_mem<yajl_handle_t> handle(yajl_free);
        char buffer[2048];
        off_t offset = 0;
        ssize_t rc = -1;

        handle = yajl_alloc(&ypc.ypc_callbacks, NULL, &ypc);
        ypc.with_handle(handle)
            .with_error_reporter(format_error_reporter);
        yajl_config(handle, yajl_allow_comments, 1);
        while (true) {
            rc = read(fd, buffer, sizeof(buffer));
            if (rc == 0) {
                break;
            }
            else if (rc == -1) {
                errors.push_back(
                    "error:" +
                    filename +
                    ":unable to read file -- " +
                    string(strerror(errno)));
                break;
            }
            if (offset == 0 && (rc > 2) &&
                    (buffer[0] == '#') && (buffer[1] == '!')) {
                // Turn it into a JavaScript comment.
                buffer[0] = buffer[1] = '/';
            }
            if (ypc.parse((const unsigned char *)buffer, rc) != yajl_status_ok) {
                break;
            }
            offset += rc;
        }
        if (rc == 0) {
            ypc.complete_parse();
        }
    }

    return retval;
}

static void load_from_path(const filesystem::path &path, std::vector<string> &errors)
{
    auto format_path = path / "formats/*/*.json";
    static_root_mem<glob_t, globfree> gl;

    log_info("loading formats from path: %s", format_path.str().c_str());
    if (glob(format_path.str().c_str(), 0, nullptr, gl.inout()) == 0) {
        for (int lpc = 0; lpc < (int)gl->gl_pathc; lpc++) {
            const char *base = basename(gl->gl_pathv[lpc]);

            if (startswith(base, "config.")) {
                continue;
            }

            string filename(gl->gl_pathv[lpc]);
            vector<intern_string_t> format_list;

            format_list = load_format_file(filename, errors);
            if (format_list.empty()) {
                log_warning("Empty format file: %s", filename.c_str());
            }
            else {
                for (auto iter = format_list.begin();
                     iter != format_list.end();
                     ++iter) {
                    log_info("  found format: %s", iter->get());
                }
            }
        }
    }
}

void load_formats(const std::vector<filesystem::path> &extra_paths,
                  std::vector<std::string> &errors)
{
    auto default_source = dotlnav_path() / "default";
    yajlpp_parse_context ypc_builtin(default_source.str(), &root_format_handler);
    std::vector<intern_string_t> retval;
    struct userdata ud;
    yajl_handle handle;

    write_sample_file();

    log_debug("Loading default formats");
    for (int lpc = 0; lnav_format_json[lpc].bsf_name; lpc++) {
        auto &bsf = lnav_format_json[lpc];
        handle = yajl_alloc(&ypc_builtin.ypc_callbacks, nullptr, &ypc_builtin);
        ud.ud_format_names = &retval;
        ud.ud_errors = &errors;
        ypc_builtin
            .with_obj(ud)
            .with_handle(handle)
            .with_error_reporter(format_error_reporter)
            .ypc_userdata = &ud;
        yajl_config(handle, yajl_allow_comments, 1);
        if (ypc_builtin.parse(bsf.bsf_data, bsf.bsf_size) != yajl_status_ok) {
            errors.push_back("builtin: invalid json -- " +
                             string((char *) yajl_get_error(handle, 1,
                                                            bsf.bsf_data,
                                                            bsf.bsf_size)));
        }
        ypc_builtin.complete_parse();
        yajl_free(handle);
    }

    for (const auto & extra_path : extra_paths) {
        load_from_path(extra_path, errors);
    }

    if (!errors.empty()) {
        return;
    }

    uint8_t mod_counter = 0;

    vector<external_log_format *> alpha_ordered_formats;
    for (auto iter = LOG_FORMATS.begin(); iter != LOG_FORMATS.end(); ++iter) {
        external_log_format *elf = iter->second;
        elf->build(errors);

        if (elf->elf_has_module_format) {
            mod_counter += 1;
            elf->lf_mod_index = mod_counter;
        }

        for (auto & check_iter : LOG_FORMATS) {
            if (iter->first == check_iter.first) {
                continue;
            }

            external_log_format *check_elf = check_iter.second;
            if (elf->match_samples(check_elf->elf_samples)) {
                log_warning("Format collision, format '%s' matches sample from '%s'",
                        elf->get_name().get(),
                        check_elf->get_name().get());
                elf->elf_collision.push_back(check_elf->get_name());
            }
        }

        if (errors.empty()) {
            alpha_ordered_formats.push_back(elf);
        }
    }

    vector<external_log_format *> &graph_ordered_formats =
            external_log_format::GRAPH_ORDERED_FORMATS;

    while (!alpha_ordered_formats.empty()) {
        vector<intern_string_t> popped_formats;

        for (auto iter = alpha_ordered_formats.begin();
             iter != alpha_ordered_formats.end();) {
            external_log_format *elf = *iter;
            if (elf->elf_collision.empty()) {
                iter = alpha_ordered_formats.erase(iter);
                popped_formats.push_back(elf->get_name());
                graph_ordered_formats.push_back(elf);
            }
            else {
                ++iter;
            }
        }

        if (popped_formats.empty() && !alpha_ordered_formats.empty()) {
            bool broke_cycle = false;

            log_warning("Detected a cycle...");
            for (auto elf : alpha_ordered_formats) {
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

        for (auto elf : alpha_ordered_formats) {
            for (auto & popped_format : popped_formats) {
                elf->elf_collision.remove(popped_format);
            }
        }
    }

    log_info("Format order:")
    for (auto & graph_ordered_format : graph_ordered_formats) {
        log_info("  %s", graph_ordered_format->get_name().get());
    }

    vector<log_format *> &roots = log_format::get_root_formats();
    roots.insert(roots.begin(), graph_ordered_formats.begin(), graph_ordered_formats.end());
}

static void exec_sql_in_path(sqlite3 *db, const filesystem::path &path, std::vector<string> &errors)
{
    auto format_path = path / "formats/*/*.sql";
    static_root_mem<glob_t, globfree> gl;

    log_info("executing SQL files in path: %s", format_path.str().c_str());
    if (glob(format_path.str().c_str(), 0, nullptr, gl.inout()) == 0) {
        for (int lpc = 0; lpc < (int)gl->gl_pathc; lpc++) {
            string filename(gl->gl_pathv[lpc]);
            string content;

            if (read_file(filename, content)) {
                log_info("Executing SQL file: %s", filename.c_str());
                sql_execute_script(db, filename.c_str(), content.c_str(), errors);
            }
            else {
                errors.push_back(
                    "error:unable to read file '" +
                    filename +
                    "' -- " +
                    string(strerror(errno)));
            }
        }
    }
}

void load_format_extra(sqlite3 *db,
                       const std::vector<filesystem::path> &extra_paths,
                       std::vector<std::string> &errors)
{
    for (const auto & extra_path : extra_paths) {
        exec_sql_in_path(db, extra_path, errors);
    }
}

static void extract_metadata(const char *contents, size_t len, struct script_metadata &meta_out)
{
    static const pcrepp SYNO_RE("^#\\s+@synopsis:(.*)$", PCRE_MULTILINE);
    static const pcrepp DESC_RE("^#\\s+@description:(.*)$", PCRE_MULTILINE);

    pcre_input pi(contents, 0, len);
    pcre_context_static<16> pc;

    pi.reset(contents, 0, len);
    if (SYNO_RE.match(pc, pi)) {
        meta_out.sm_synopsis = trim(pi.get_substr(pc[0]));
    }
    pi.reset(contents, 0, len);
    if (DESC_RE.match(pc, pi)) {
        meta_out.sm_description = trim(pi.get_substr(pc[0]));
    }

    if (!meta_out.sm_synopsis.empty()) {
        size_t space = meta_out.sm_synopsis.find(' ');

        if (space == string::npos) {
            space = meta_out.sm_synopsis.size();
        }
        meta_out.sm_name = meta_out.sm_synopsis.substr(0, space);
    }
}

void extract_metadata_from_file(struct script_metadata &meta_inout)
{
    char buffer[8 * 1024];
    auto_mem<FILE> fp(fclose);
    struct stat st;

    if (statp(meta_inout.sm_path, &st) == -1) {
        log_warning("unable to open script -- %s", meta_inout.sm_path.str().c_str());
    } else if (!S_ISREG(st.st_mode)) {
        log_warning("not a regular file -- %s", meta_inout.sm_path.str().c_str());
    } else if ((fp = fopen(meta_inout.sm_path.str().c_str(), "r")) != NULL) {
        size_t len;

        len = fread(buffer, 1, sizeof(buffer), fp.in());
        extract_metadata(buffer, len, meta_inout);
    }
}

static void find_format_in_path(const filesystem::path &path,
                                map<string, vector<script_metadata> > &scripts)
{
    auto format_path = path / "formats/*/*.lnav";
    static_root_mem<glob_t, globfree> gl;

    log_debug("Searching for script in path: %s", format_path.str().c_str());
    if (glob(format_path.str().c_str(), 0, nullptr, gl.inout()) == 0) {
        for (int lpc = 0; lpc < (int)gl->gl_pathc; lpc++) {
            const char *filename = basename(gl->gl_pathv[lpc]);
            string script_name = string(filename, strlen(filename) - 5);
            struct script_metadata meta;

            meta.sm_path = gl->gl_pathv[lpc];
            meta.sm_name = script_name;
            extract_metadata_from_file(meta);
            scripts[script_name].push_back(meta);

            log_debug("  found script: %s", meta.sm_path.str().c_str());
        }
    }
}

void find_format_scripts(const vector<filesystem::path> &extra_paths,
                         map<string, vector<script_metadata> > &scripts)
{
    for (const auto &extra_path : extra_paths) {
        find_format_in_path(extra_path, scripts);
    }
}

void load_format_vtabs(log_vtab_manager *vtab_manager,
                       std::vector<std::string> &errors)
{
    map<intern_string_t, external_log_format *> &root_formats = LOG_FORMATS;

    for (auto & root_format : root_formats) {
        root_format.second->register_vtabs(vtab_manager, errors);
    }
}
