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

    ypc->ypc_path_index_stack.push_back(ypc->ypc_path.length());
    return 1;
}

int yajlpp_parse_context::map_key(void *ctx,
                                  const unsigned char *key,
                                  size_t len)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;

    ypc->ypc_path = ypc->ypc_path.substr(0, ypc->ypc_path_index_stack.back());
    ypc->ypc_path += "/" + std::string((const char *)key, len);

    ypc->update_callbacks();
    return 1;
}

void yajlpp_parse_context::update_callbacks(void)
{
    pcre_input pi(this->ypc_path);
    bool found = false;

    this->ypc_callbacks = DEFAULT_CALLBACKS;

    for (int lpc = 0; this->ypc_handlers[lpc].jph_path[0]; lpc++) {
        const json_path_handler &jph = this->ypc_handlers[lpc];

        if (jph.jph_regex.match(this->ypc_pcre_context, pi)) {
            this->ypc_callbacks.yajl_null = jph.jph_callbacks.yajl_null;
            this->ypc_callbacks.yajl_boolean = jph.jph_callbacks.yajl_boolean;
            this->ypc_callbacks.yajl_integer = jph.jph_callbacks.yajl_integer;
            this->ypc_callbacks.yajl_double = jph.jph_callbacks.yajl_double;
            this->ypc_callbacks.yajl_string = jph.jph_callbacks.yajl_string;
            found = true;
            break;
        }
    }
}

int yajlpp_parse_context::map_end(void *ctx)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;

    ypc->ypc_path = ypc->ypc_path.substr(0, ypc->ypc_path_index_stack.back());
    ypc->ypc_path_index_stack.pop_back();

    ypc->update_callbacks();
    return 1;
}

int yajlpp_parse_context::array_start(void *ctx)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;

    ypc->ypc_path_index_stack.push_back(ypc->ypc_path.length());
    ypc->ypc_path += "#";

    ypc->update_callbacks();

    return 1;
}

int yajlpp_parse_context::array_end(void *ctx)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;

    ypc->ypc_path = ypc->ypc_path.substr(0, ypc->ypc_path_index_stack.back());
    ypc->ypc_path_index_stack.pop_back();

    ypc->update_callbacks();

    return 1;
}

const yajl_callbacks yajlpp_parse_context::DEFAULT_CALLBACKS = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    yajlpp_parse_context::map_start,
    yajlpp_parse_context::map_key,
    yajlpp_parse_context::map_end,
    yajlpp_parse_context::array_start,
    yajlpp_parse_context::array_end,
};
