/**
 * Copyright (c) 2013, Timothy Stack
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

#include <map>
#include <string>

#include "yajlpp.hh"
#include "lnav_config.hh"
#include "log_format.hh"
#include "default-log-formats-json.hh"

using namespace std;

static map<string, external_log_format *> LOG_FORMATS;

static external_log_format *ensure_format(const std::string &name)
{
    external_log_format *retval;

    retval = LOG_FORMATS[name];
    if (retval == NULL) {
        LOG_FORMATS[name] = retval = new external_log_format(name);
    }

    return retval;
}

static int read_format_regex(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    external_log_format *elf = ensure_format(ypc->get_path_fragment(0));
    string value = string((const char *)str, len);

    elf->elf_patterns.insert(elf->elf_patterns.begin(), value);

    return 1;
}

static int read_format_bool(yajlpp_parse_context *ypc, int val)
{
    external_log_format *elf = ensure_format(ypc->get_path_fragment(0));
    string field_name = ypc->get_path_fragment(1);

    if (field_name == "local-time")
        elf->lf_date_time.dts_local_time = val;

    return 1;
}

static int read_format_field(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    external_log_format *elf = ensure_format(ypc->get_path_fragment(0));
    string value = string((const char *)str, len);
    string field_name = ypc->get_path_fragment(1);

    if (field_name == "level-field")
        elf->elf_level_field = value;

    return 1;
}

static int read_levels(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    external_log_format *elf = ensure_format(ypc->get_path_fragment(0));
    string regex = string((const char *)str, len);
    logline::level_t level = logline::string2level(ypc->get_path_fragment(2).c_str());

    elf->elf_level_patterns[level].lp_regex = regex;

    return 1;
}

static int read_value_def(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    external_log_format *elf = ensure_format(ypc->get_path_fragment(0));
    string value_name = ypc->get_path_fragment(2);
    string field_name = ypc->get_path_fragment(3);
    string val = string((const char *)str, len);

    elf->elf_value_defs[value_name].vd_name = value_name;
    if (field_name == "kind") {
        logline_value::kind_t kind;

        if ((kind = logline_value::string2kind(val.c_str())) ==
            logline_value::VALUE_UNKNOWN) {
            fprintf(stderr, "unknown value kind %s\n", val.c_str());
            return 0;
        }
        elf->elf_value_defs[value_name].vd_kind = kind;
    }
    else if (field_name == "unit" && ypc->get_path_fragment(4) == "field") {
        elf->elf_value_defs[value_name].vd_unit_field = val;
    }
    else if (field_name == "collate") {
        elf->elf_value_defs[value_name].vd_collate = val;
    }

    return 1;
}

static int read_value_bool(yajlpp_parse_context *ypc, int val)
{
    external_log_format *elf = ensure_format(ypc->get_path_fragment(0));
    string value_name = ypc->get_path_fragment(2);
    string key_name = ypc->get_path_fragment(3);

    if (key_name == "identifier")
        elf->elf_value_defs[value_name].vd_identifier = val;
    else if (key_name == "foreign-key")
        elf->elf_value_defs[value_name].vd_foreign_key = val;

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
    external_log_format *elf = ensure_format(ypc->get_path_fragment(0));
    string value_name = ypc->get_path_fragment(2);
    string scale_name = ypc->get_path_fragment(5);

    if (scale_name.empty()) {
        fprintf(stderr,
                "error:%s:%s: scaling factor field cannot be empty\n",
                ypc->get_path_fragment(0).c_str(),
                value_name.c_str());
        return 0;
    }

    struct scaling_factor &sf = elf->elf_value_defs[value_name].vd_unit_scaling[scale_name.substr(1)];

    if (scale_name[0] == '/') {
        sf.sf_op = SO_DIVIDE;
    }
    else if (scale_name[0] == '*') {
        sf.sf_op = SO_MULTIPLY;
    }
    else {
        fprintf(stderr,
                "error:%s:%s: scaling factor field must start with '/' or '*'\n",
                ypc->get_path_fragment(0).c_str(),
                value_name.c_str());
        return 0;
    }

    sf.sf_value = val;

    return 1;
}

static int read_sample_line(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    external_log_format *elf = ensure_format(ypc->get_path_fragment(0));
    string val = string((const char *)str, len);
    int index = ypc->ypc_array_index.back();
    external_log_format::sample &sample = ensure_sample(elf, index);

    sample.s_line = val;

    return 1;
}

static struct json_path_handler format_handlers[] = {
    json_path_handler("/\\w+/regex#", read_format_regex),
    json_path_handler("/\\w+/local-time", read_format_bool),
    json_path_handler("/\\w+/(level-field|url|title|description)", read_format_field),
    json_path_handler("/\\w+/level/"
                      "(trace|debug|info|warning|error|critical|fatal)",
                      read_levels),
    json_path_handler("/\\w+/value/\\w+/(kind|collate|unit/field)", read_value_def),
    json_path_handler("/\\w+/value/\\w+/(identifier|foreign-key)", read_value_bool),
    json_path_handler("/\\w+/value/\\w+/unit/scaling-factor/.*",
        read_scaling),
    json_path_handler("/\\w+/sample#/line", read_sample_line),

    json_path_handler()
};

void load_formats(std::vector<std::string> &errors)
{
    yajlpp_parse_context ypc_builtin("builtin", format_handlers);
    std::vector<std::string> retval;
    yajl_handle handle;

    handle = yajl_alloc(&ypc_builtin.ypc_callbacks, NULL, &ypc_builtin);
    yajl_parse(handle,
               (const unsigned char *)default_log_formats_json,
               strlen(default_log_formats_json));
    yajl_complete_parse(handle);
    yajl_free(handle);

    string format_path = dotlnav_path("formats/*.json");
    static_root_mem<glob_t, globfree> gl;

    if (glob(format_path.c_str(), GLOB_NOCHECK, NULL, gl.inout()) == 0) {
        for (int lpc = 0; lpc < (int)gl->gl_pathc; lpc++) {
            string filename(gl->gl_pathv[lpc]);
            int fd;

            yajlpp_parse_context ypc(filename, format_handlers);
            if ((fd = open(gl->gl_pathv[lpc], O_RDONLY)) != -1) {
                char buffer[2048];
                int rc = -1;

                handle = yajl_alloc(&ypc.ypc_callbacks, NULL, &ypc);
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
                    if (yajl_parse(handle, (const unsigned char *)buffer, rc) != yajl_status_ok) {
                        errors.push_back(filename +
                            ": invalid json -- " +
                            string((char *)yajl_get_error(handle, 1, (unsigned char *)buffer, rc)));
                        break;
                    }
                }
                if (rc == 0) {
                    if (yajl_complete_parse(handle) != yajl_status_ok) {
                        errors.push_back(filename +
                            ": invalid json -- " +
                            string((char *)yajl_get_error(handle, 0, NULL, 0)));
                    }
                }
                yajl_free(handle);
            }
        }
    }

    for (map<string, external_log_format *>::iterator iter = LOG_FORMATS.begin();
         iter != LOG_FORMATS.end();
         ++iter) {
        iter->second->build(errors);

        if (errors.empty()) {
            log_format::get_root_formats().insert(log_format::get_root_formats().begin(), iter->second);
        }
    }
}
