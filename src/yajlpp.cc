/**
 * Copyright (c) 2015, Timothy Stack
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
#include "yajl/api/yajl_parse.h"

using namespace std;

const json_path_handler_base::enum_value_t json_path_handler_base::ENUM_TERMINATOR =
    make_pair((const char *) NULL, 0);

static char *resolve_root(yajlpp_parse_context *ypc)
{
    const json_path_handler_base *jph = ypc->ypc_current_handler;

    ptrdiff_t offset = (char *) jph->jph_simple_offset - (char *) NULL;
    char *retval = (char *) ypc->ypc_simple_data;

    if (jph->jph_obj_provider != NULL) {
        retval = (char *) jph->jph_obj_provider(*ypc, (void *) retval);
    }

    return retval + offset;
}

int yajlpp_static_string(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    char *root_ptr = resolve_root(ypc);
    string *field_ptr = (string *) root_ptr;

    (*field_ptr) = string((const char *) str, len);

    yajlpp_validator_for_string(*ypc, *ypc->ypc_current_handler);

    return 1;
}

int yajlpp_static_intern_string(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    char *root_ptr = resolve_root(ypc);
    intern_string_t *field_ptr = (intern_string_t *) root_ptr;

    (*field_ptr) = intern_string::lookup((const char *) str, len);

    yajlpp_validator_for_intern_string(*ypc, *ypc->ypc_current_handler);

    return 1;
}

int yajlpp_static_enum(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    char *root_ptr = resolve_root(ypc);
    int *field_ptr = (int *) root_ptr;
    const json_path_handler_base &jph = *ypc->ypc_current_handler;
    bool found = false;

    for (int lpc = 0; jph.jph_enum_values[lpc].first; lpc++) {
        const json_path_handler::enum_value_t &ev = jph.jph_enum_values[lpc];

        if (len == strlen(ev.first) && strncmp((const char *) str, ev.first, len) == 0) {
            *field_ptr = ev.second;
            found = true;
            break;
        }
    }

    if (!found) {
        ypc->report_error("error:%s:line %d\n  "
                              "Invalid value, '%.*s', for option:",
                          ypc->ypc_source.c_str(),
                          ypc->get_line_number(),
                          len,
                          str);

        ypc->report_error("    %s %s -- %s\n",
                          &ypc->ypc_path[0],
                          jph.jph_synopsis,
                          jph.jph_description);
        ypc->report_error("  Allowed values: ");
        for (int lpc = 0; jph.jph_enum_values[lpc].first; lpc++) {
            const json_path_handler::enum_value_t &ev = jph.jph_enum_values[lpc];

            ypc->report_error("    %s\n", ev.first);
        }
    }

    return 1;
}

yajl_gen_status yajlpp_static_gen_string(yajlpp_gen_context &ygc,
                                         const json_path_handler_base &jph,
                                         yajl_gen handle)
{
    string *default_field_ptr = resolve_simple_object<string>(ygc.ygc_default_data, jph.jph_simple_offset);
    string *field_ptr = resolve_simple_object<string>(ygc.ygc_simple_data, jph.jph_simple_offset);

    if (ygc.ygc_default_data != NULL && (*default_field_ptr == *field_ptr)) {
        return yajl_gen_status_ok;
    }

    if (ygc.ygc_depth) {
        yajl_gen_string(handle, jph.jph_path);
    }
    return yajl_gen_string(handle, *field_ptr);
}

void yajlpp_validator_for_string(yajlpp_parse_context &ypc,
                                 const json_path_handler_base &jph)
{
    string *field_ptr = (string *) resolve_root(&ypc);
    char buffer[1024];

    if (field_ptr->empty() && jph.jph_min_length > 0) {
        ypc.report_error("value must not be empty");
    }
    else if (field_ptr->size() < jph.jph_min_length) {
        snprintf(buffer, sizeof(buffer),
                 "value must be at least %lu characters long",
                 jph.jph_min_length);
        ypc.report_error(buffer);
    }
}

void yajlpp_validator_for_intern_string(yajlpp_parse_context &ypc,
                                        const json_path_handler_base &jph)
{
    intern_string_t *field_ptr = (intern_string_t *) resolve_root(&ypc);
    char buffer[1024];

    if (field_ptr->empty() && jph.jph_min_length > 0) {
        ypc.report_error("value must not be empty");
    }
    else if (field_ptr->size() < jph.jph_min_length) {
        snprintf(buffer, sizeof(buffer),
                 "value must be at least %lu characters long",
                 jph.jph_min_length);
        ypc.report_error(buffer);
    }
}

void yajlpp_validator_for_int(yajlpp_parse_context &ypc,
                              const json_path_handler_base &jph)
{
    long long *field_ptr = (long long int *) resolve_root(&ypc);
    char buffer[1024];

    if (*field_ptr < jph.jph_min_value) {
        snprintf(buffer, sizeof(buffer),
                 "value must be greater than %lld",
                 jph.jph_min_value);
        ypc.report_error(buffer);
    }
}

int yajlpp_static_number(yajlpp_parse_context *ypc, long long num)
{
    char *root_ptr = resolve_root(ypc);
    long long *field_ptr = (long long *) root_ptr;

    *field_ptr = num;

    return 1;
}

int yajlpp_static_bool(yajlpp_parse_context *ypc, int val)
{
    char *root_ptr = resolve_root(ypc);
    bool *field_ptr = (bool *) root_ptr;

    *field_ptr = val;

    return 1;
}

yajl_gen_status yajlpp_static_gen_bool(yajlpp_gen_context &ygc,
                                       const json_path_handler_base &jph,
                                       yajl_gen handle)
{
    bool *default_field_ptr = resolve_simple_object<bool>(ygc.ygc_default_data, jph.jph_simple_offset);
    bool *field_ptr = resolve_simple_object<bool>(ygc.ygc_simple_data, jph.jph_simple_offset);

    if (ygc.ygc_default_data != NULL && (*default_field_ptr == *field_ptr)) {
        return yajl_gen_status_ok;
    }

    if (ygc.ygc_depth) {
        yajl_gen_string(handle, jph.jph_path);
    }
    return yajl_gen_bool(handle, *field_ptr);
}

yajl_gen_status json_path_handler_base::gen(yajlpp_gen_context &ygc, yajl_gen handle) const
{
    if (this->jph_children) {
        int start = this->jph_path[0] == '^' ? 1 : 0;
        int start_depth = ygc.ygc_depth;

        for (int lpc = start; this->jph_path[lpc]; lpc++) {
            if (this->jph_path[lpc] == '/') {
                if (lpc > start) {
                    yajl_gen_pstring(handle,
                                     &this->jph_path[start],
                                     lpc - start);
                    yajl_gen_map_open(handle);
                    ygc.ygc_depth += 1;
                }
                start = lpc + 1;
            }
        }

        for (int lpc = 0; this->jph_children[lpc].jph_path[0]; lpc++) {
            json_path_handler_base &jph = this->jph_children[lpc];
            yajl_gen_status status = jph.gen(ygc, handle);

            if (status != yajl_gen_status_ok) {
                return status;
            }
        }

        while (ygc.ygc_depth > start_depth) {
            yajl_gen_map_close(handle);
            ygc.ygc_depth -= 1;
        }
    }
    else if (this->jph_gen_callback != NULL) {
        return this->jph_gen_callback(ygc, *this, handle);
    }

    return yajl_gen_status_ok;
};

int yajlpp_parse_context::map_start(void *ctx)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;
    int retval = 1;

    ypc->ypc_path_index_stack.push_back(ypc->ypc_path.size() - 1);

    if (ypc->ypc_path.size() > 1 &&
        ypc->ypc_path[ypc->ypc_path.size() - 2] == '#') {
        ypc->ypc_array_index.back() += 1;
    }

    if (ypc->ypc_alt_callbacks.yajl_start_map != NULL) {
        retval = ypc->ypc_alt_callbacks.yajl_start_map(ypc);
    }

    return retval;
}

int yajlpp_parse_context::map_key(void *ctx,
                                  const unsigned char *key,
                                  size_t len)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;
    int start, retval = 1;

    ypc->ypc_path.resize(ypc->ypc_path_index_stack.back());
    ypc->ypc_path.push_back('/');
    start = ypc->ypc_path.size();
    ypc->ypc_path.resize(ypc->ypc_path.size() + len);
    memcpy(&ypc->ypc_path[start], key, len);
    ypc->ypc_path.push_back('\0');

    if (ypc->ypc_alt_callbacks.yajl_map_key != NULL) {
        retval = ypc->ypc_alt_callbacks.yajl_map_key(ctx, key, len);
    }

    if (ypc->ypc_handlers != NULL) {
        ypc->update_callbacks();
    }
    return retval;
}

void yajlpp_parse_context::update_callbacks(const json_path_handler_base *orig_handlers, int child_start)
{
    const json_path_handler_base *handlers = orig_handlers;

    this->ypc_current_handler = NULL;

    if (this->ypc_handlers == NULL) {
        return;
    }

    pcre_input pi(&this->ypc_path[0], 0, this->ypc_path.size() - 1);

    this->ypc_callbacks = DEFAULT_CALLBACKS;

    if (handlers == NULL) {
        handlers = this->ypc_handlers;
    }

    if (!this->ypc_active_paths.empty()) {
        string curr_path(&this->ypc_path[0], this->ypc_path.size() - 1);

        if (this->ypc_active_paths.find(curr_path) ==
            this->ypc_active_paths.end()) {
            return;
        }
    }

    for (int lpc = 0; handlers[lpc].jph_path[0]; lpc++) {
        const json_path_handler_base &jph = handlers[lpc];

        pi.reset(&this->ypc_path[child_start],
                 0,
                 this->ypc_path.size() - 1 - child_start);
        if (jph.jph_regex.match(this->ypc_pcre_context, pi)) {
            pcre_context::capture_t *cap = this->ypc_pcre_context.all();

            if (jph.jph_children) {
                if (this->ypc_path[cap->c_end - 1] != '/') {
                    continue;
                }

                this->update_callbacks(jph.jph_children, cap->c_end);
            }
            else {
                if (child_start + cap->c_end != this->ypc_path.size() - 1) {
                    continue;
                }

                this->ypc_current_handler = &jph;
            }

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

    ypc->ypc_path.resize(ypc->ypc_path_index_stack.back());
    ypc->ypc_path.push_back('\0');
    ypc->ypc_path_index_stack.pop_back();

    if (ypc->ypc_alt_callbacks.yajl_end_map != NULL) {
        retval = ypc->ypc_alt_callbacks.yajl_end_map(ctx);
    }

    ypc->update_callbacks();
    return retval;
}

int yajlpp_parse_context::array_start(void *ctx)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;
    int retval = 1;

    ypc->ypc_path_index_stack.push_back(ypc->ypc_path.size() - 1);
    ypc->ypc_path[ypc->ypc_path.size() - 1] = '#';
    ypc->ypc_path.push_back('\0');
    ypc->ypc_array_index.push_back(-1);

    if (ypc->ypc_alt_callbacks.yajl_start_array != NULL) {
        retval = ypc->ypc_alt_callbacks.yajl_start_array(ctx);
    }

    ypc->update_callbacks();

    return retval;
}

int yajlpp_parse_context::array_end(void *ctx)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;
    int retval = 1;

    ypc->ypc_path.resize(ypc->ypc_path_index_stack.back());
    ypc->ypc_path.push_back('\0');
    ypc->ypc_path_index_stack.pop_back();
    ypc->ypc_array_index.pop_back();

    if (ypc->ypc_alt_callbacks.yajl_end_array != NULL) {
        retval = ypc->ypc_alt_callbacks.yajl_end_array(ctx);
    }

    ypc->update_callbacks();

    return retval;
}

int yajlpp_parse_context::handle_unused(void *ctx)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;

    if (ypc->ypc_ignore_unused) {
        return 1;
    }

    const json_path_handler_base *handler = ypc->ypc_current_handler;

    int line_number = ypc->get_line_number();

    if (handler != NULL && strlen(handler->jph_synopsis) > 0 &&
        strlen(handler->jph_description) > 0) {

        fprintf(stderr,
                "warning:%s:line %d\n  unexpected data for path -- \n",
                ypc->ypc_source.c_str(),
                line_number);

        fprintf(stderr, "    %s %s -- %s\n",
                &ypc->ypc_path[0],
                handler->jph_synopsis,
                handler->jph_description
        );
    }
    else {
        fprintf(stderr,
                "warning:%s:line %d\n  unexpected path -- \n",
                ypc->ypc_source.c_str(),
                line_number);

        fprintf(stderr, "    %s\n", &ypc->ypc_path[0]);
    }

    if (ypc->ypc_callbacks.yajl_boolean != (int (*)(void *, int))yajlpp_parse_context::handle_unused ||
        ypc->ypc_callbacks.yajl_integer != (int (*)(void *, long long))yajlpp_parse_context::handle_unused ||
        ypc->ypc_callbacks.yajl_double != (int (*)(void *, double))yajlpp_parse_context::handle_unused ||
        ypc->ypc_callbacks.yajl_string != (int (*)(void *, const unsigned char *, size_t))yajlpp_parse_context::handle_unused) {
        fprintf(stderr, "  expecting one of the following data types --\n");
    }

    if (ypc->ypc_callbacks.yajl_boolean != (int (*)(void *, int))yajlpp_parse_context::handle_unused) {
        fprintf(stderr, "    boolean\n");
    }
    if (ypc->ypc_callbacks.yajl_integer != (int (*)(void *, long long))yajlpp_parse_context::handle_unused) {
        fprintf(stderr, "    integer\n");
    }
    if (ypc->ypc_callbacks.yajl_double != (int (*)(void *, double))yajlpp_parse_context::handle_unused) {
        fprintf(stderr, "    float\n");
    }
    if (ypc->ypc_callbacks.yajl_string != (int (*)(void *, const unsigned char *, size_t))yajlpp_parse_context::handle_unused) {
        fprintf(stderr, "    string\n");
    }

    if (handler == NULL) {
        fprintf(stderr, "  accepted paths --\n");
        for (int lpc = 0; ypc->ypc_handlers[lpc].jph_path[0]; lpc++) {
            fprintf(stderr, "    %s\n",
                    ypc->ypc_handlers[lpc].jph_path);
        }
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
