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

#include "yajlpp.hh"
#include "lnav_config.hh"
#include "log_format.hh"
#include "auto_fd.hh"
#include "sql_util.hh"
#include "format-text-files.hh"
#include "default-log-formats-json.hh"

#include "log_format_loader.hh"

#ifndef SYSCONFDIR
#define SYSCONFDIR "/usr/etc"
#endif

using namespace std;

static void extract_metadata(const char *contents, size_t len, struct script_metadata &meta_out);

typedef map<intern_string_t, external_log_format *> log_formats_map_t;

static log_formats_map_t LOG_FORMATS;

struct userdata {
    std::string ud_format_path;
    vector<intern_string_t> *ud_format_names;
    std::vector<std::string> *ud_errors;
};

static external_log_format *ensure_format(const yajlpp_provider_context &ypc, userdata *ud)
{
    const intern_string_t name = ypc.get_substr_i(0);
    vector<intern_string_t> *formats = ud->ud_format_names;
    external_log_format *retval;

    retval = LOG_FORMATS[name];
    if (retval == NULL) {
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

    auto &retval = elf->elf_value_defs[value_name];

    if (retval.get() == nullptr) {
        retval = make_shared<external_log_format::value_def>();
    }

    retval->vd_name = value_name;

    return retval.get();
}

static scaling_factor *scaling_factor_provider(const yajlpp_provider_context &ypc, external_log_format::value_def *value_def)
{
    string scale_spec = ypc.get_substr(0);

    const intern_string_t scale_name = intern_string::lookup(scale_spec.substr(1));
    scaling_factor &retval = value_def->vd_unit_scaling[scale_name];

    if (scale_spec[0] == '/') {
        retval.sf_op = SO_DIVIDE;
    }
    else if (scale_spec[0] == '*') {
        retval.sf_op = SO_MULTIPLY;
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
    int index = ypc.ypc_index;
    external_log_format::json_format_element &jfe = ensure_json_format_element(elf, index);

    jfe.jfe_type = external_log_format::JLF_VARIABLE;

    return &jfe;
}

static int read_format_bool(yajlpp_parse_context *ypc, int val)
{
    external_log_format *elf = (external_log_format *)ypc->ypc_obj_stack.top();
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
    external_log_format *elf = (external_log_format *)ypc->ypc_obj_stack.top();
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
    external_log_format *elf = (external_log_format *)ypc->ypc_obj_stack.top();
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
    external_log_format *elf = (external_log_format *)ypc->ypc_obj_stack.top();
    string value = string((const char *)str, len);
    string field_name = ypc->get_path_fragment(1);

    if (field_name == "file-pattern")
        elf->elf_file_pattern = value;
    else if (field_name == "level-field")
        elf->elf_level_field = intern_string::lookup(value);
    else if (field_name == "timestamp-field")
        elf->lf_timestamp_field = intern_string::lookup(value);
    else if (field_name == "body-field")
        elf->elf_body_field = intern_string::lookup(value);
    else if (field_name == "timestamp-format")
        elf->lf_timestamp_format.push_back(intern_string::lookup(value)->get());
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
    external_log_format *elf = (external_log_format *)ypc->ypc_obj_stack.top();
    string regex = string((const char *)str, len);
    string level_name_or_number = ypc->get_path_fragment(2);
    logline::level_t level = logline::string2level(level_name_or_number.c_str());
    elf->elf_level_patterns[level].lp_regex = regex;

    return 1;
}

static int read_level_int(yajlpp_parse_context *ypc, long long val)
{
    external_log_format *elf = (external_log_format *)ypc->ypc_obj_stack.top();
    string level_name_or_number = ypc->get_path_fragment(2);
    logline::level_t level = logline::string2level(level_name_or_number.c_str());

    elf->elf_level_pairs.push_back(make_pair(val, level));

    return 1;
}

static int read_action_def(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    external_log_format *elf = (external_log_format *)ypc->ypc_obj_stack.top();
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
    external_log_format *elf = (external_log_format *)ypc->ypc_obj_stack.top();
    string action_name = ypc->get_path_fragment(2);
    string field_name = ypc->get_path_fragment(3);

    elf->lf_action_defs[action_name].ad_capture_output = val;

    return 1;
}

static int read_action_cmd(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    external_log_format *elf = (external_log_format *)ypc->ypc_obj_stack.top();
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
    external_log_format *elf = (external_log_format *)ypc->ypc_obj_stack.top();
    string val = string((const char *)str, len);

    ypc->ypc_array_index.back() += 1;

    int index = ypc->ypc_array_index.back();
    external_log_format::json_format_element &jfe = ensure_json_format_element(elf, index);

    jfe.jfe_type = external_log_format::JLF_CONSTANT;
    jfe.jfe_default_value = val;

    return 1;
}

static int create_search_table(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    external_log_format *elf = (external_log_format *)ypc->ypc_obj_stack.top();
    const intern_string_t table_name = ypc->get_path_fragment_i(2);
    string regex = string((const char *) str, len);

    elf->elf_search_tables.push_back(make_pair(table_name, regex));

    return 1;
}


static struct json_path_handler pattern_handlers[] = {
    json_path_handler("pattern")
        .with_synopsis("<message-regex>")
        .with_description(
            "The regular expression to match a log message and capture fields.")
        .with_min_length(1)
        .for_field(&nullobj<external_log_format::pattern>()->p_string),
    json_path_handler("module-format")
        .with_synopsis("<bool>")
        .with_description(
            "If true, this pattern will only be used to parse message bodies "
                "of container formats, like syslog")
        .for_field(&nullobj<external_log_format::pattern>()->p_module_format),

    json_path_handler()
};

static const json_path_handler_base::enum_value_t ALIGN_ENUM[] = {
    make_pair("left", external_log_format::json_format_element::LEFT),
    make_pair("right", external_log_format::json_format_element::RIGHT),

    json_path_handler_base::ENUM_TERMINATOR
};

static const json_path_handler_base::enum_value_t OVERFLOW_ENUM[] = {
    make_pair("abbrev", external_log_format::json_format_element::ABBREV),
    make_pair("truncate", external_log_format::json_format_element::TRUNCATE),
    make_pair("dot-dot", external_log_format::json_format_element::DOTDOT),

    json_path_handler_base::ENUM_TERMINATOR
};

static struct json_path_handler line_format_handlers[] = {
    json_path_handler("field")
        .with_synopsis("<field-name>")
        .with_description("The name of the field to substitute at this position")
        .with_min_length(1)
        .for_field(&nullobj<external_log_format::json_format_element>()->jfe_value),

    json_path_handler("default-value")
        .with_synopsis("<string>")
        .with_description("The default value for this position if the field is null")
        .for_field(&nullobj<external_log_format::json_format_element>()->jfe_default_value),

    json_path_handler("timestamp-format")
        .with_synopsis("<string>")
        .with_min_length(1)
        .with_description("The strftime(3) format for this field")
        .for_field(&nullobj<external_log_format::json_format_element>()->jfe_ts_format),

    json_path_handler("min-width")
        .with_min_value(0)
        .with_synopsis("<size>")
        .with_description("The minimum width of the field")
        .for_field(&nullobj<external_log_format::json_format_element>()->jfe_min_width),

    json_path_handler("max-width")
        .with_min_value(0)
        .with_synopsis("<size>")
        .with_description("The maximum width of the field")
        .for_field(&nullobj<external_log_format::json_format_element>()->jfe_max_width),

    json_path_handler("align")
        .with_synopsis("left|right")
        .with_description("Align the text in the column to the left or right side")
        .with_enum_values(ALIGN_ENUM)
        .for_enum(&nullobj<external_log_format::json_format_element>()->jfe_align),

    json_path_handler("overflow")
        .with_synopsis("abbrev|truncate|dot-dot")
        .with_description("Overflow style")
        .with_enum_values(OVERFLOW_ENUM)
        .for_enum(&nullobj<external_log_format::json_format_element>()->jfe_overflow),

    json_path_handler()
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

static struct json_path_handler unit_handlers[] = {
    json_path_handler("field")
        .with_synopsis("<field-name>")
        .with_description("The name of the field that contains the units for this field")
        .for_field(&nullobj<external_log_format::value_def>()->vd_unit_field),

    json_path_handler("scaling-factor/(?<scale>.*)$")
        .with_synopsis("[*,/]<unit>")
        .with_obj_provider(scaling_factor_provider)
        .for_field(&nullobj<scaling_factor>()->sf_value),

    json_path_handler()
};

static struct json_path_handler value_def_handlers[] = {
    json_path_handler("kind")
        .with_synopsis("string|integer|float|boolean|json|quoted")
        .with_description("The type of data in the field")
        .with_enum_values(KIND_ENUM)
        .for_enum(&nullobj<external_log_format::value_def>()->vd_kind),

    json_path_handler("collate")
        .with_synopsis("<function>")
        .with_description("The collating function to use for this column")
        .for_field(&nullobj<external_log_format::value_def>()->vd_collate),

    json_path_handler("unit/")
        .with_description("Unit definitions for this field")
        .with_children(unit_handlers),

    json_path_handler("identifier")
        .with_synopsis("<bool>")
        .with_description("Indicates whether or not this field contains an identifier that should be highlighted")
        .for_field(&nullobj<external_log_format::value_def>()->vd_identifier),

    json_path_handler("foreign-key")
        .with_synopsis("<bool>")
        .with_description("Indicates whether or not this field should be treated as a foreign key for row in another table")
        .for_field(&nullobj<external_log_format::value_def>()->vd_foreign_key),

    json_path_handler("hidden")
        .with_synopsis("<bool>")
        .with_description("Indicates whether or not this field should be hidden")
        .for_field(&nullobj<external_log_format::value_def>()->vd_hidden),

    json_path_handler("action-list#")
        .with_synopsis("<string>")
        .with_description("Actions to execute when this field is clicked on")
        .for_field(&nullobj<external_log_format::value_def>()->vd_action_list),

    json_path_handler("rewriter")
        .with_synopsis("<command>")
        .with_description("A command that will rewrite this field when pretty-printing")
        .for_field(&nullobj<external_log_format::value_def>()->vd_rewriter),

    json_path_handler("description")
        .with_synopsis("<string>")
        .with_description("A description of the field")
        .for_field(&nullobj<external_log_format::value_def>()->vd_description),

    json_path_handler()
};

static struct json_path_handler highlighter_def_handlers[] = {
    json_path_handler("pattern")
        .with_synopsis("<regex>")
        .with_description("A regular expression to highlight in logs of this format.")
        .for_field(&nullobj<external_log_format::highlighter_def>()->hd_pattern),

    json_path_handler("color")
        .with_synopsis("#<hex>|<name>")
        .with_description("The color to use when highlighting this pattern.")
        .for_field(&nullobj<external_log_format::highlighter_def>()->hd_color),

    json_path_handler("background-color")
        .with_synopsis("#<hex>|<name>")
        .with_description("The background color to use when highlighting this pattern.")
        .for_field(&nullobj<external_log_format::highlighter_def>()->hd_background_color),

    json_path_handler("underline")
        .with_synopsis("<enabled>")
        .with_description("Highlight this pattern with an underline.")
        .for_field(&nullobj<external_log_format::highlighter_def>()->hd_underline),

    json_path_handler("blink")
        .with_synopsis("<enabled>")
        .with_description("Highlight this pattern by blinking.")
        .for_field(&nullobj<external_log_format::highlighter_def>()->hd_blink),

    json_path_handler()
};

static const json_path_handler_base::enum_value_t LEVEL_ENUM[] = {
    make_pair(logline::level_names[logline::LEVEL_TRACE], logline::LEVEL_TRACE),
    make_pair(logline::level_names[logline::LEVEL_DEBUG5], logline::LEVEL_DEBUG5),
    make_pair(logline::level_names[logline::LEVEL_DEBUG4], logline::LEVEL_DEBUG4),
    make_pair(logline::level_names[logline::LEVEL_DEBUG3], logline::LEVEL_DEBUG3),
    make_pair(logline::level_names[logline::LEVEL_DEBUG2], logline::LEVEL_DEBUG2),
    make_pair(logline::level_names[logline::LEVEL_DEBUG], logline::LEVEL_DEBUG),
    make_pair(logline::level_names[logline::LEVEL_INFO], logline::LEVEL_INFO),
    make_pair(logline::level_names[logline::LEVEL_STATS], logline::LEVEL_STATS),
    make_pair(logline::level_names[logline::LEVEL_WARNING], logline::LEVEL_WARNING),
    make_pair(logline::level_names[logline::LEVEL_ERROR], logline::LEVEL_ERROR),
    make_pair(logline::level_names[logline::LEVEL_CRITICAL], logline::LEVEL_CRITICAL),
    make_pair(logline::level_names[logline::LEVEL_FATAL], logline::LEVEL_FATAL),

    json_path_handler_base::ENUM_TERMINATOR
};

struct json_path_handler sample_handlers[] = {
    json_path_handler("line")
        .with_synopsis("<log-line>")
        .with_description("A sample log line that should match a pattern in this format.")
        .for_field(&nullobj<external_log_format::sample>()->s_line),

    json_path_handler("level")
        .with_enum_values(LEVEL_ENUM)
        .with_description("The expected level for this sample log line.")
        .for_enum(&nullobj<external_log_format::sample>()->s_level),

    json_path_handler()
};

static const json_path_handler_base::enum_value_t TYPE_ENUM[] = {
    make_pair("text", external_log_format::elf_type_t::ELF_TYPE_TEXT),
    make_pair("json", external_log_format::elf_type_t::ELF_TYPE_JSON),
    make_pair("csv", external_log_format::elf_type_t::ELF_TYPE_CSV),

    json_path_handler_base::ENUM_TERMINATOR
};

struct json_path_handler format_handlers[] = {
    json_path_handler("regex/(?<pattern_name>[^/]+)/")
        .with_obj_provider(pattern_provider)
        .with_children(pattern_handlers),

    // TODO convert the rest of these
    json_path_handler("(json|convert-to-local-time|"
        "hide-extra|multiline)", read_format_bool),
    json_path_handler("timestamp-divisor", read_format_double)
        .add_cb(read_format_int)
        .with_synopsis("<number>")
        .with_description("The value to divide a numeric timestamp by in a JSON log."),
    json_path_handler("(file-pattern|level-field|timestamp-field|"
                              "body-field|url|url#|title|description|"
                              "timestamp-format#|module-field|opid-field)$",
                      read_format_field),
    json_path_handler("ordered-by-time")
        .with_synopsis("<bool>")
        .with_description("Indicates that the order of messages in the file is time-based.")
        .for_field(&nullobj<external_log_format>()->lf_time_ordered),
    json_path_handler("level/"
                      "(trace|debug\\d*|info|stats|warning|error|critical|fatal)")
        .add_cb(read_levels)
        .add_cb(read_level_int)
        .with_synopsis("<pattern|integer>")
        .with_description("The regular expression used to match the log text for this level.  "
                              "For JSON logs with numeric levels, this should be the number for the corresponding level."),

    json_path_handler("value/(?<value_name>[^/]+)/")
        .with_obj_provider(value_def_provider)
        .with_children(value_def_handlers),

    json_path_handler("action/(?<action_name>[^/]+)/label", read_action_def),
    json_path_handler("action/(?<action_name>[^/]+)/capture-output", read_action_bool),
    json_path_handler("action/(?<action_name>[^/]+)/cmd#", read_action_cmd),
    json_path_handler("sample#/")
        .with_obj_provider(sample_provider)
        .with_children(sample_handlers),

    json_path_handler("line-format#/")
        .with_obj_provider(line_format_provider)
        .with_children(line_format_handlers),
    json_path_handler("line-format#", read_json_constant),

    json_path_handler("search-table/.+/pattern", create_search_table)
        .with_synopsis("<regex>")
        .with_description("The regular expression for this search table."),

    json_path_handler("highlights/(?<highlight_name>[^/]+)/")
        .with_obj_provider<external_log_format::highlighter_def, external_log_format>([](const yajlpp_provider_context &ypc, external_log_format *root) {
            return &(root->elf_highlighter_patterns[ypc.get_substr_i(0)]);
        })
        .with_children(highlighter_def_handlers),

    json_path_handler("file-type")
        .with_synopsis("The type of file that contains the log messages")
        .with_enum_values(TYPE_ENUM)
        .for_enum(&nullobj<external_log_format>()->elf_type),

    json_path_handler()
};

struct json_path_handler root_format_handler[] = {
    json_path_handler("/(?<format_name>\\w+)/")
        .with_description("The definition of a log file format.")
        .with_obj_provider(ensure_format)
        .with_children(format_handlers),

    json_path_handler()
};

static void write_sample_file(void)
{
    string sample_path = dotlnav_path("formats/default/default-formats.json.sample");
    auto_fd sample_fd;

    if ((sample_fd = open(sample_path.c_str(),
                          O_WRONLY|O_TRUNC|O_CREAT,
                          0644)) == -1 ||
        (write(sample_fd.get(),
               default_log_formats_json,
               strlen(default_log_formats_json)) == -1)) {
        perror("error: unable to write default format file");
    }

    string sh_path = dotlnav_path("formats/default/dump-pid.sh");
    auto_fd sh_fd;

    if ((sh_fd = open(sh_path.c_str(), O_WRONLY|O_TRUNC|O_CREAT, 0755)) == -1 ||
        write(sh_fd.get(), dump_pid_sh, strlen(dump_pid_sh)) == -1) {
        perror("error: unable to write default text file");
    }

    static const char *SCRIPTS[] = {
            partition_by_boot_lnav,
            dhclient_summary_lnav,
            lnav_pop_view_lnav,
            search_for_lnav,
            NULL
    };


    for (int lpc = 0; SCRIPTS[lpc]; lpc++) {
        struct script_metadata meta;
        const char *script_content = SCRIPTS[lpc];
        string script_path;
        auto_fd script_fd;
        char path[2048];
        size_t script_len;
        struct stat st;

        script_len = strlen(script_content);
        extract_metadata(script_content, script_len, meta);
        snprintf(path, sizeof(path), "formats/default/%s.lnav", meta.sm_name.c_str());
        script_path = dotlnav_path(path);
        if (stat(script_path.c_str(), &st) == 0 && (size_t) st.st_size == script_len) {
            // Assume it's the right contents and move on...
            continue;
        }
        if ((script_fd = open(script_path.c_str(), O_WRONLY|O_TRUNC|O_CREAT, 0755)) == -1 ||
            write(script_fd.get(), script_content, script_len) == -1) {
            perror("error: unable to write default text file");
        }
    }
}

static void format_error_reporter(const yajlpp_parse_context &ypc, const char *msg)
{
    struct userdata *ud = (userdata *) ypc.ypc_userdata;

    ud->ud_errors->push_back(msg);
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
    yajlpp_parse_context ypc(filename, root_format_handler);
    ypc.ypc_userdata = &ud;
    ypc.with_obj(ud);
    if ((fd = open(filename.c_str(), O_RDONLY)) == -1) {
        char errmsg[1024];

        snprintf(errmsg, sizeof(errmsg),
                 "error:unable to open format file '%s' -- %s",
                 filename.c_str(),
                 strerror(errno));
        errors.push_back(errmsg);
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
                errors.push_back(
                    "error:" +
                    filename +
                    ":" +
                    to_string(ypc.get_line_number()) +
                    ":invalid json -- " +
                    string((char *)yajl_get_error(handle, 1, (unsigned char *)buffer, rc)));
                break;
            }
            offset += rc;
        }
        if (rc == 0) {
            if (ypc.complete_parse() != yajl_status_ok) {
                errors.push_back(
                    "error:" +
                    filename +
                    ":" +
                    to_string(ypc.get_line_number()) +
                    ":invalid json -- " +
                    string((char *)yajl_get_error(handle, 0, NULL, 0)));
            }
        }
    }

    return retval;
}

static void load_from_path(const string &path, std::vector<string> &errors)
{
    string format_path = path + "/formats/*/*.json";
    static_root_mem<glob_t, globfree> gl;

    log_info("loading formats from path: %s", format_path.c_str());
    if (glob(format_path.c_str(), 0, NULL, gl.inout()) == 0) {
        for (int lpc = 0; lpc < (int)gl->gl_pathc; lpc++) {
            string filename(gl->gl_pathv[lpc]);
            vector<intern_string_t> format_list;

            format_list = load_format_file(filename, errors);
            if (format_list.empty()) {
                log_warning("Empty format file: %s", filename.c_str());
            }
            else {
                for (vector<intern_string_t>::iterator iter = format_list.begin();
                        iter != format_list.end();
                        ++iter) {
                    log_info("  found format: %s", iter->get());
                }
            }
        }
    }
}

void load_formats(const std::vector<std::string> &extra_paths,
                  std::vector<std::string> &errors)
{
    string default_source = string(dotlnav_path("formats") + "/default/");
    yajlpp_parse_context ypc_builtin(default_source, root_format_handler);
    std::vector<intern_string_t> retval;
    struct userdata ud;
    yajl_handle handle;

    write_sample_file();

    log_debug("Loading default formats");
    handle = yajl_alloc(&ypc_builtin.ypc_callbacks, NULL, &ypc_builtin);
    ud.ud_format_names = &retval;
    ud.ud_errors = &errors;
    ypc_builtin
        .with_obj(ud)
        .with_handle(handle)
        .with_error_reporter(format_error_reporter)
        .ypc_userdata = &ud;
    yajl_config(handle, yajl_allow_comments, 1);
    if (ypc_builtin.parse((const unsigned char *)default_log_formats_json,
                          strlen(default_log_formats_json)) != yajl_status_ok) {
        errors.push_back("builtin: invalid json -- " +
            string((char *)yajl_get_error(handle, 1, (unsigned char *)default_log_formats_json, strlen(default_log_formats_json))));
    }
    ypc_builtin.complete_parse();
    yajl_free(handle);

    load_from_path("/etc/lnav", errors);
    load_from_path(SYSCONFDIR "/lnav", errors);
    load_from_path(dotlnav_path(""), errors);

    for (vector<string>::const_iterator path_iter = extra_paths.begin();
         path_iter != extra_paths.end();
         ++path_iter) {
        load_from_path(*path_iter, errors);
    }

    if (!errors.empty()) {
        return;
    }

    uint8_t mod_counter = 0;

    vector<external_log_format *> alpha_ordered_formats;
    for (map<intern_string_t, external_log_format *>::iterator iter = LOG_FORMATS.begin();
         iter != LOG_FORMATS.end();
         ++iter) {
        external_log_format *elf = iter->second;
        elf->build(errors);

        if (elf->elf_has_module_format) {
            mod_counter += 1;
            elf->lf_mod_index = mod_counter;
        }

        for (map<intern_string_t, external_log_format *>::iterator check_iter = LOG_FORMATS.begin();
             check_iter != LOG_FORMATS.end();
             ++check_iter) {
            if (iter->first == check_iter->first) {
                continue;
            }

            external_log_format *check_elf = check_iter->second;
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

        for (vector<external_log_format *>::iterator iter = alpha_ordered_formats.begin();
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
            for (vector<external_log_format *>::iterator iter = alpha_ordered_formats.begin();
                 iter != alpha_ordered_formats.end();
                 ++iter) {
                external_log_format *elf = *iter;

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

        for (vector<external_log_format *>::iterator iter = alpha_ordered_formats.begin();
             iter != alpha_ordered_formats.end();
             ++iter) {
            external_log_format *elf = *iter;
            for (vector<intern_string_t>::iterator pop_iter = popped_formats.begin();
                    pop_iter != popped_formats.end();
                    ++pop_iter) {
                elf->elf_collision.remove(*pop_iter);
            }
        }
    }

    log_info("Format order:")
    for (vector<external_log_format *>::iterator iter = graph_ordered_formats.begin();
            iter != graph_ordered_formats.end();
            ++iter) {
        log_info("  %s", (*iter)->get_name().get());
    }

    vector<log_format *> &roots = log_format::get_root_formats();
    roots.insert(roots.begin(), graph_ordered_formats.begin(), graph_ordered_formats.end());
}

static void exec_sql_in_path(sqlite3 *db, const string &path, std::vector<string> &errors)
{
    string format_path = path + "/formats/*/*.sql";
    static_root_mem<glob_t, globfree> gl;

    log_info("executing SQL files in path: %s", format_path.c_str());
    if (glob(format_path.c_str(), 0, NULL, gl.inout()) == 0) {
        for (int lpc = 0; lpc < (int)gl->gl_pathc; lpc++) {
            string filename(gl->gl_pathv[lpc]);
            string content;

            if (read_file(filename.c_str(), content)) {
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
                       const std::vector<std::string> &extra_paths,
                       std::vector<std::string> &errors)
{
    exec_sql_in_path(db, "/etc/lnav", errors);
    exec_sql_in_path(db, SYSCONFDIR "/lnav", errors);
    exec_sql_in_path(db, dotlnav_path(""), errors);

    for (vector<string>::const_iterator path_iter = extra_paths.begin();
         path_iter != extra_paths.end();
         ++path_iter) {
        exec_sql_in_path(db, *path_iter, errors);
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

    if (stat(meta_inout.sm_path.c_str(), &st) == -1) {
        log_warning("unable to open script -- %s", meta_inout.sm_path.c_str());
    } else if (!S_ISREG(st.st_mode)) {
        log_warning("not a regular file -- %s", meta_inout.sm_path.c_str());
    } else if ((fp = fopen(meta_inout.sm_path.c_str(), "r")) != NULL) {
        size_t len;

        len = fread(buffer, 1, sizeof(buffer), fp.in());
        extract_metadata(buffer, len, meta_inout);
    }
}

static void find_format_in_path(const string &path,
                                map<string, vector<script_metadata> > &scripts)
{
    string format_path = path + "/formats/*/*.lnav";
    static_root_mem<glob_t, globfree> gl;

    log_debug("Searching for script in path: %s", format_path.c_str());
    if (glob(format_path.c_str(), 0, NULL, gl.inout()) == 0) {
        for (int lpc = 0; lpc < (int)gl->gl_pathc; lpc++) {
            const char *filename = basename(gl->gl_pathv[lpc]);
            string script_name = string(filename, strlen(filename) - 5);
            struct script_metadata meta;

            meta.sm_path = gl->gl_pathv[lpc];
            meta.sm_name = script_name;
            extract_metadata_from_file(meta);
            scripts[script_name].push_back(meta);

            log_debug("  found script: %s", meta.sm_path.c_str());
        }
    }
}

void find_format_scripts(const vector<string> &extra_paths,
                         map<string, vector<script_metadata> > &scripts)
{
    find_format_in_path("/etc/lnav", scripts);
    find_format_in_path(SYSCONFDIR "/lnav", scripts);
    find_format_in_path(dotlnav_path(""), scripts);

    for (vector<string>::const_iterator path_iter = extra_paths.begin();
         path_iter != extra_paths.end();
         ++path_iter) {
        find_format_in_path(*path_iter, scripts);
    }
}

void load_format_vtabs(log_vtab_manager *vtab_manager,
                       std::vector<std::string> &errors)
{
    map<intern_string_t, external_log_format *> &root_formats = LOG_FORMATS;

    for (map<intern_string_t, external_log_format *>::iterator iter = root_formats.begin();
         iter != root_formats.end();
         ++iter) {
        iter->second->register_vtabs(vtab_manager, errors);
    }
}
