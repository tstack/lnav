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

static map<intern_string_t, external_log_format *> LOG_FORMATS;

struct userdata {
    std::string ud_format_path;
    vector<intern_string_t> *ud_format_names;
};

static external_log_format *ensure_format(yajlpp_parse_context *ypc)
{
    const intern_string_t name = ypc->get_path_fragment_i(0);
    struct userdata *ud = (userdata *) ypc->ypc_userdata;
    vector<intern_string_t> *formats = ud->ud_format_names;
    external_log_format *retval;

    retval = LOG_FORMATS[name];
    if (retval == NULL) {
        LOG_FORMATS[name] = retval = new external_log_format(name);
        log_debug("Loading format -- %s", name.get());
    }
    retval->elf_source_path.insert(ypc->ypc_source.substr(0, ypc->ypc_source.rfind('/')));

    if (find(formats->begin(), formats->end(), name) == formats->end()) {
        formats->push_back(name);
    }

    if (ud->ud_format_path.empty()) {
        retval->elf_builtin_format = true;
    }

    return retval;
}

static external_log_format::pattern *pattern_provider(yajlpp_parse_context &ypc, void *root)
{
    external_log_format *elf = ensure_format(&ypc);
    string regex_name = ypc.get_path_fragment(2);

    struct external_log_format::pattern &pat = elf->elf_patterns[regex_name];

    if (pat.p_config_path.empty()) {
        string full_path = ypc.get_path().to_string();
        pat.p_config_path = full_path.substr(0, full_path.rfind('/'));
    }

    return &pat;
}

static int read_format_bool(yajlpp_parse_context *ypc, int val)
{
    external_log_format *elf = ensure_format(ypc);
    string field_name = ypc->get_path_fragment(1);

    if (field_name == "convert-to-local-time")
        elf->lf_date_time.dts_local_time = val;
    else if (field_name == "json")
        elf->jlf_json = val;
    else if (field_name == "hide-extra")
        elf->jlf_hide_extra = val;
    else if (field_name == "multiline")
        elf->elf_multiline = val;

    return 1;
}

static int read_format_double(yajlpp_parse_context *ypc, double val)
{
    external_log_format *elf = ensure_format(ypc);
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
    external_log_format *elf = ensure_format(ypc);
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
    external_log_format *elf = ensure_format(ypc);
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
    external_log_format *elf = ensure_format(ypc);
    string regex = string((const char *)str, len);
    string level_name_or_number = ypc->get_path_fragment(2);
    logline::level_t level = logline::string2level(level_name_or_number.c_str());

    elf->elf_level_patterns[level].lp_regex = regex;

    return 1;
}

static int read_level_int(yajlpp_parse_context *ypc, long long val)
{
    external_log_format *elf = ensure_format(ypc);
    string level_name_or_number = ypc->get_path_fragment(2);
    logline::level_t level = logline::string2level(level_name_or_number.c_str());

    elf->elf_level_pairs.push_back(make_pair(val, level));

    return 1;
}

static int read_value_def(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    external_log_format *elf = ensure_format(ypc);
    const intern_string_t value_name = ypc->get_path_fragment_i(2);
    string field_name = ypc->get_path_fragment(3);
    string val = string((const char *)str, len);

    elf->elf_value_defs[value_name].vd_name = value_name;
    if (field_name == "kind") {
        logline_value::kind_t kind;

        if ((kind = logline_value::string2kind(val.c_str())) ==
            logline_value::VALUE_UNKNOWN) {
            fprintf(stderr, "error: unknown value kind %s\n", val.c_str());
            return 0;
        }
        elf->elf_value_defs[value_name].vd_kind = kind;
    }
    else if (field_name == "unit" && ypc->get_path_fragment(4) == "field") {
        elf->elf_value_defs[value_name].vd_unit_field = intern_string::lookup(val);
    }
    else if (field_name == "collate") {
        elf->elf_value_defs[value_name].vd_collate = val;
    }

    return 1;
}

static int read_value_action(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    external_log_format *elf = ensure_format(ypc);
    const intern_string_t value_name = ypc->get_path_fragment_i(2);
    string field_name = ypc->get_path_fragment(3);
    string val = string((const char *)str, len);

    elf->elf_value_defs[value_name].vd_action_list.push_back(val);

    return 1;
}

static int read_value_bool(yajlpp_parse_context *ypc, int val)
{
    external_log_format *elf = ensure_format(ypc);
    const intern_string_t value_name = ypc->get_path_fragment_i(2);
    string key_name = ypc->get_path_fragment(3);

    if (key_name == "identifier")
        elf->elf_value_defs[value_name].vd_identifier = val;
    else if (key_name == "foreign-key")
        elf->elf_value_defs[value_name].vd_foreign_key = val;
    else if (key_name == "hidden")
        elf->elf_value_defs[value_name].vd_hidden = val;

    return 1;
}

static int read_action_def(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    external_log_format *elf = ensure_format(ypc);
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
    external_log_format *elf = ensure_format(ypc);
    string action_name = ypc->get_path_fragment(2);
    string field_name = ypc->get_path_fragment(3);

    elf->lf_action_defs[action_name].ad_capture_output = val;

    return 1;
}

static int read_action_cmd(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    external_log_format *elf = ensure_format(ypc);
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

static int read_scaling(yajlpp_parse_context *ypc, double val)
{
    external_log_format *elf = ensure_format(ypc);
    const intern_string_t value_name = ypc->get_path_fragment_i(2);
    string scale_spec = ypc->get_path_fragment(5);

    if (scale_spec.empty()) {
        fprintf(stderr,
                "error:%s:%s: scaling factor field cannot be empty\n",
                ypc->get_path_fragment(0).c_str(),
                value_name.get());
        return 0;
    }

    const intern_string_t scale_name = intern_string::lookup(scale_spec.substr(1));
    struct scaling_factor &sf = elf->elf_value_defs[value_name].vd_unit_scaling[scale_name];

    if (scale_spec[0] == '/') {
        sf.sf_op = SO_DIVIDE;
    }
    else if (scale_spec[0] == '*') {
        sf.sf_op = SO_MULTIPLY;
    }
    else {
        fprintf(stderr,
                "error:%s:%s: scaling factor field must start with '/' or '*'\n",
                ypc->get_path_fragment(0).c_str(),
                value_name.get());
        return 0;
    }

    sf.sf_value = val;

    return 1;
}

static int read_sample_line(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    external_log_format *elf = ensure_format(ypc);
    string val = string((const char *)str, len);
    int index = ypc->ypc_array_index.back();
    external_log_format::sample &sample = ensure_sample(elf, index);

    sample.s_line = val;

    return 1;
}

static external_log_format::json_format_element &
    ensure_json_format_element(external_log_format *elf, int index)
{
    elf->jlf_line_format.resize(index + 1);

    return elf->jlf_line_format[index];
}

static int read_json_constant(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    external_log_format *elf = ensure_format(ypc);
    string val = string((const char *)str, len);

    ypc->ypc_array_index.back() += 1;

    int index = ypc->ypc_array_index.back();
    external_log_format::json_format_element &jfe = ensure_json_format_element(elf, index);

    jfe.jfe_type = external_log_format::JLF_CONSTANT;
    jfe.jfe_default_value = val;

    return 1;
}

static int read_json_variable(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    external_log_format *elf = ensure_format(ypc);
    string val = string((const char *)str, len);
    int index = ypc->ypc_array_index.back();
    external_log_format::json_format_element &jfe = ensure_json_format_element(elf, index);
    string field_name = ypc->get_path_fragment(3);

    jfe.jfe_type = external_log_format::JLF_VARIABLE;
    if (field_name == "field") {
        jfe.jfe_value = intern_string::lookup(val);
    }
    else if (field_name == "default-value") {
        jfe.jfe_default_value = val;
    }

    return 1;
}

static int read_json_variable_num(yajlpp_parse_context *ypc, long long val)
{
    external_log_format *elf = ensure_format(ypc);
    int index = ypc->ypc_array_index.back();
    external_log_format::json_format_element &jfe = ensure_json_format_element(elf, index);
    string field_name = ypc->get_path_fragment(2);

    jfe.jfe_type = external_log_format::JLF_VARIABLE;
    jfe.jfe_min_width = val;

    return 1;
}

static int create_search_table(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    external_log_format *elf = ensure_format(ypc);
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

static struct json_path_handler format_handlers[] = {
    json_path_handler("/\\w+/regex/[^/]+/")
        .with_obj_provider(pattern_provider)
        .with_children(pattern_handlers),

    // TODO convert the rest of these
    json_path_handler("/\\w+/(json|convert-to-local-time|"
        "hide-extra|multiline)", read_format_bool),
    json_path_handler("/\\w+/timestamp-divisor", read_format_double)
        .add_cb(read_format_int),
    json_path_handler("/\\w+/(file-pattern|level-field|timestamp-field|"
                              "body-field|url|url#|title|description|"
                              "timestamp-format#|module-field|opid-field)$",
                      read_format_field),
    json_path_handler("/\\w+/level/"
                      "(trace|debug\\d*|info|stats|warning|error|critical|fatal)")
        .add_cb(read_levels)
        .add_cb(read_level_int),
    json_path_handler("/\\w+/value/.+/(kind|collate|unit/field)$", read_value_def),
    json_path_handler("/\\w+/value/.+/(identifier|foreign-key|hidden)$", read_value_bool),
    json_path_handler("/\\w+/value/.+/unit/scaling-factor/.*$",
        read_scaling),
    json_path_handler("/\\w+/value/.+/action-list#", read_value_action),
    json_path_handler("/\\w+/action/[^/]+/label", read_action_def),
    json_path_handler("/\\w+/action/[^/]+/capture-output", read_action_bool),
    json_path_handler("/\\w+/action/[^/]+/cmd#", read_action_cmd),
    json_path_handler("/\\w+/sample#/line", read_sample_line),
    json_path_handler("/\\w+/line-format#/(field|default-value)", read_json_variable),
    json_path_handler("/\\w+/line-format#/min-width", read_json_variable_num),
    json_path_handler("/\\w+/line-format#", read_json_constant),

    json_path_handler("/\\w+/search-table/.+/pattern", create_search_table)
        .with_synopsis("<regex>")
        .with_description("The regular expression for this search table."),

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

std::vector<intern_string_t> load_format_file(const string &filename, std::vector<string> &errors)
{
    std::vector<intern_string_t> retval;
    struct userdata ud;
    auto_fd fd;

    log_info("loading formats from file: %s", filename.c_str());
    ud.ud_format_path = filename;
    ud.ud_format_names = &retval;
    yajlpp_parse_context ypc(filename, format_handlers);
    ypc.ypc_userdata = &ud;
    if ((fd = open(filename.c_str(), O_RDONLY)) == -1) {
        char errmsg[1024];

        snprintf(errmsg, sizeof(errmsg),
                "error: unable to open format file -- %s",
                filename.c_str());
        errors.push_back(errmsg);
    }
    else {
        auto_mem<yajl_handle_t> handle(yajl_free);
        char buffer[2048];
        off_t offset = 0;
        ssize_t rc = -1;

        handle = yajl_alloc(&ypc.ypc_callbacks, NULL, &ypc);
        yajl_config(handle, yajl_allow_comments, 1);
        while (true) {
            rc = read(fd, buffer, sizeof(buffer));
            if (rc == 0) {
                break;
            }
            else if (rc == -1) {
                errors.push_back(filename +
                        ":unable to read file -- " +
                        string(strerror(errno)));
                break;
            }
            if (offset == 0 && (rc > 2) &&
                    (buffer[0] == '#') && (buffer[1] == '!')) {
                // Turn it into a JavaScript comment.
                buffer[0] = buffer[1] = '/';
            }
            if (yajl_parse(handle, (const unsigned char *)buffer, rc) != yajl_status_ok) {
                errors.push_back(filename +
                        ": invalid json -- " +
                        string((char *)yajl_get_error(handle, 1, (unsigned char *)buffer, rc)));
                break;
            }
            offset += rc;
        }
        if (rc == 0) {
            if (yajl_complete_parse(handle) != yajl_status_ok) {
                errors.push_back(filename +
                        ": invalid json -- " +
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
    yajlpp_parse_context ypc_builtin(default_source, format_handlers);
    std::vector<intern_string_t> retval;
    struct userdata ud;
    yajl_handle handle;

    write_sample_file();

    log_debug("Loading default formats");
    handle = yajl_alloc(&ypc_builtin.ypc_callbacks, NULL, &ypc_builtin);
    ud.ud_format_names = &retval;
    ypc_builtin
        .with_handle(handle)
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
                errors.push_back("Unable to read file: " + filename);
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

    if ((fp = fopen(meta_inout.sm_path.c_str(), "r")) != NULL) {
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
