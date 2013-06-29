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

#include <map>
#include <string>

#include "yajlpp.hh"
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

static int read_format_regex(void *ctx, const unsigned char *str, size_t len)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;
    external_log_format *elf = ensure_format(ypc->get_path_fragment(0));
    string value = string((const char *)str, len);

    elf->elf_patterns.insert(elf->elf_patterns.begin(), value);

    return 1;
}

static int read_format_field(void *ctx, const unsigned char *str, size_t len)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;
    external_log_format *elf = ensure_format(ypc->get_path_fragment(0));
    string value = string((const char *)str, len);
    string field_name = ypc->get_path_fragment(1);

    if (field_name == "level-field")
        elf->elf_level_field = value;

    return 1;
}

static int read_levels(void *ctx, const unsigned char *str, size_t len)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;
    external_log_format *elf = ensure_format(ypc->get_path_fragment(0));
    string regex = string((const char *)str, len);
    logline::level_t level = logline::string2level(ypc->get_path_fragment(2).c_str());

    elf->elf_level_patterns[level].lp_regex = regex;

    return 1;
}

static int read_value_def(void *ctx, const unsigned char *str, size_t len)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;
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

    return 1;
}

static int read_value_ident(void *ctx, int val)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;
    external_log_format *elf = ensure_format(ypc->get_path_fragment(0));
    string value_name = ypc->get_path_fragment(2);

    elf->elf_value_defs[value_name].vd_identifier = val;

    return 1;
}

static struct json_path_handler format_handlers[] = {
    json_path_handler("/\\w+/regex#", read_format_regex),
    json_path_handler("/\\w+/(level-field)", read_format_field),
    json_path_handler("/\\w+/level/"
                      "(trace|debug|info|warning|error|critical|fatal)",
                      read_levels),
    json_path_handler("/\\w+/value/\\w+/(kind)", read_value_def),
    json_path_handler("/\\w+/value/\\w+/identifier", read_value_ident),

    json_path_handler()
};

void load_formats(std::vector<std::string> &errors)
{
    yajlpp_parse_context ypc(format_handlers);
    std::vector<std::string> retval;
    yajl_handle handle;

    handle = yajl_alloc(&ypc.ypc_callbacks, NULL, &ypc);
    yajl_parse(handle,
               (const unsigned char *)default_log_formats_json,
               strlen(default_log_formats_json));
    yajl_complete_parse(handle);
    yajl_free(handle);

    for (map<string, external_log_format *>::iterator iter = LOG_FORMATS.begin();
         iter != LOG_FORMATS.end();
         ++iter) {
        iter->second->build(errors);

        if (errors.empty()) {
            log_format::get_root_formats().insert(log_format::get_root_formats().begin(), iter->second);
        }
    }
}
