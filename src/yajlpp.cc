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
 * @file yajlpp.cc
 */

#include "config.h"

#include "yajlpp.hh"

int yajlpp_parse_context::map_start(void *ctx)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;
    int retval = 1;

    ypc->ypc_path_index_stack.push_back(ypc->ypc_path.length());

    if (ypc->ypc_path[ypc->ypc_path.size() - 1] == '#') {
        ypc->ypc_array_index.back() += 1;
    }

    if (ypc->ypc_alt_callbacks.yajl_start_map != NULL)
        retval = ypc->ypc_alt_callbacks.yajl_start_map(ypc);

    return retval;
}

int yajlpp_parse_context::map_key(void *ctx,
                                  const unsigned char *key,
                                  size_t len)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;
    int retval = 1;

    ypc->ypc_path  = ypc->ypc_path.substr(0, ypc->ypc_path_index_stack.back());
    ypc->ypc_path += "/" + std::string((const char *)key, len);

    if (ypc->ypc_alt_callbacks.yajl_map_key != NULL)
        retval = ypc->ypc_alt_callbacks.yajl_map_key(ctx, key, len);

    ypc->update_callbacks();
    return retval;
}

void yajlpp_parse_context::update_callbacks(void)
{
    pcre_input pi(this->ypc_path);

    this->ypc_callbacks = DEFAULT_CALLBACKS;

    for (int lpc = 0; this->ypc_handlers[lpc].jph_path[0]; lpc++) {
        const json_path_handler &jph = this->ypc_handlers[lpc];

        pi.reset(this->ypc_path);

        if (jph.jph_regex.match(this->ypc_pcre_context, pi)) {
            if (jph.jph_callbacks.yajl_null != NULL)
                this->ypc_callbacks.yajl_null = jph.jph_callbacks.yajl_null;
            if (jph.jph_callbacks.yajl_boolean != NULL)
                this->ypc_callbacks.yajl_boolean = jph.jph_callbacks.yajl_boolean;
            if (jph.jph_callbacks.yajl_integer != NULL)
                this->ypc_callbacks.yajl_integer = jph.jph_callbacks.yajl_integer;
            if (jph.jph_callbacks.yajl_double != NULL)
                this->ypc_callbacks.yajl_double = jph.jph_callbacks.yajl_double;
            if (jph.jph_callbacks.yajl_string != NULL)
                this->ypc_callbacks.yajl_string = jph.jph_callbacks.yajl_string;
        }
    }
}

int yajlpp_parse_context::map_end(void *ctx)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;
    int retval = 1;

    ypc->ypc_path = ypc->ypc_path.substr(0, ypc->ypc_path_index_stack.back());
    ypc->ypc_path_index_stack.pop_back();

    if (ypc->ypc_alt_callbacks.yajl_end_map != NULL)
        retval = ypc->ypc_alt_callbacks.yajl_end_map(ctx);

    ypc->update_callbacks();
    return retval;
}

int yajlpp_parse_context::array_start(void *ctx)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;
    int retval = 1;

    ypc->ypc_path_index_stack.push_back(ypc->ypc_path.length());
    ypc->ypc_path += "#";
    ypc->ypc_array_index.push_back(-1);

    if (ypc->ypc_alt_callbacks.yajl_start_array != NULL)
        retval = ypc->ypc_alt_callbacks.yajl_start_array(ctx);

    ypc->update_callbacks();

    return retval;
}

int yajlpp_parse_context::array_end(void *ctx)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;
    int retval = 1;

    ypc->ypc_path = ypc->ypc_path.substr(0, ypc->ypc_path_index_stack.back());
    ypc->ypc_path_index_stack.pop_back();
    ypc->ypc_array_index.pop_back();

    if (ypc->ypc_alt_callbacks.yajl_end_array != NULL)
        retval = ypc->ypc_alt_callbacks.yajl_end_array(ctx);

    ypc->update_callbacks();

    return retval;
}

int yajlpp_parse_context::handle_unused(void *ctx)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;

    if (ypc->ypc_ignore_unused)
        return 1;

    fprintf(stderr, "warning:%s:%s:unexpected data, expecting one of the following data types --\n",
        ypc->ypc_source.c_str(),
        ypc->ypc_path.c_str());
    if (ypc->ypc_callbacks.yajl_boolean != (int (*)(void *, int))yajlpp_parse_context::handle_unused) {
        fprintf(stderr, "warning:%s:%s:  boolean\n",
                ypc->ypc_source.c_str(), ypc->ypc_path.c_str());
    }
    if (ypc->ypc_callbacks.yajl_integer != (int (*)(void *, long long))yajlpp_parse_context::handle_unused) {
        fprintf(stderr, "warning:%s:%s:  integer\n",
                ypc->ypc_source.c_str(), ypc->ypc_path.c_str());
    }
    if (ypc->ypc_callbacks.yajl_double != (int (*)(void *, double))yajlpp_parse_context::handle_unused) {
        fprintf(stderr, "warning:%s:%s:  float\n",
                ypc->ypc_source.c_str(), ypc->ypc_path.c_str());
    }
    if (ypc->ypc_callbacks.yajl_string != (int (*)(void *, const unsigned char *, size_t))yajlpp_parse_context::handle_unused) {
        fprintf(stderr, "warning:%s:%s:  string\n",
                ypc->ypc_source.c_str(), ypc->ypc_path.c_str());
    }

    fprintf(stderr, "warning:%s:%s:accepted paths --\n",
            ypc->ypc_source.c_str(), ypc->ypc_path.c_str());
    for (int lpc = 0; ypc->ypc_handlers[lpc].jph_path[0]; lpc++) {
        fprintf(stderr, "warning:%s:%s:  %s\n",
            ypc->ypc_source.c_str(),
            ypc->ypc_path.c_str(),
            ypc->ypc_handlers[lpc].jph_path);
    }

    return 1;
}

const yajl_callbacks yajlpp_parse_context::DEFAULT_CALLBACKS = {
    yajlpp_parse_context::handle_unused,
    (int (*)(void *, int))yajlpp_parse_context::handle_unused,
    (int (*)(void *, long long))yajlpp_parse_context::handle_unused,
    (int (*)(void *, double))yajlpp_parse_context::handle_unused,
    NULL,
    (int (*)(void *, const unsigned char *, size_t))
    yajlpp_parse_context::handle_unused,
    yajlpp_parse_context::map_start,
    yajlpp_parse_context::map_key,
    yajlpp_parse_context::map_end,
    yajlpp_parse_context::array_start,
    yajlpp_parse_context::array_end,
};
