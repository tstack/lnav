/**
 * Copyright (c) 2014, Timothy Stack
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
 * THIS SOFTWARE IS PROVIDED BY TIMOTHY STACK AND CONTRIBUTORS ''AS IS'' AND ANY
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
 * @file drive_json_op.cc
 */

#include <errno.h>
#include <stdlib.h>

#include "base/lnav_log.hh"
#include "config.h"
#include "yajl/api/yajl_gen.h"
#include "yajlpp/json_op.hh"

static void
printer(void* ctx, const char* numberVal, size_t numberLen)
{
    log_perror(write(STDOUT_FILENO, numberVal, numberLen));
}

static int
handle_start_map(void* ctx)
{
    json_op* jo = (json_op*) ctx;
    yajl_gen gen = (yajl_gen) jo->jo_ptr_data;

    yajl_gen_map_open(gen);

    return 1;
}

static int
handle_map_key(void* ctx,
               const unsigned char* key,
               size_t len,
               yajl_string_props_t* props)
{
    json_op* jo = (json_op*) ctx;
    yajl_gen gen = (yajl_gen) jo->jo_ptr_data;

    yajl_gen_string(gen, key, len);

    return 1;
}

static int
handle_end_map(void* ctx)
{
    json_op* jo = (json_op*) ctx;
    yajl_gen gen = (yajl_gen) jo->jo_ptr_data;

    yajl_gen_map_close(gen);

    return 1;
}

static int
handle_null(void* ctx)
{
    json_op* jo = (json_op*) ctx;
    yajl_gen gen = (yajl_gen) jo->jo_ptr_data;

    yajl_gen_null(gen);

    return 1;
}

static int
handle_boolean(void* ctx, int boolVal)
{
    json_op* jo = (json_op*) ctx;
    yajl_gen gen = (yajl_gen) jo->jo_ptr_data;

    yajl_gen_bool(gen, boolVal);

    return 1;
}

static int
handle_number(void* ctx, const char* numberVal, size_t numberLen)
{
    json_op* jo = (json_op*) ctx;
    yajl_gen gen = (yajl_gen) jo->jo_ptr_data;

    yajl_gen_number(gen, numberVal, numberLen);

    return 1;
}

static int
handle_string(void* ctx,
              const unsigned char* stringVal,
              size_t len,
              yajl_string_props_t*)
{
    json_op* jo = (json_op*) ctx;
    yajl_gen gen = (yajl_gen) jo->jo_ptr_data;

    yajl_gen_string(gen, stringVal, len);

    return 1;
}

static int
handle_start_array(void* ctx)
{
    json_op* jo = (json_op*) ctx;
    yajl_gen gen = (yajl_gen) jo->jo_ptr_data;

    yajl_gen_array_open(gen);

    return 1;
}

static int
handle_end_array(void* ctx)
{
    json_op* jo = (json_op*) ctx;
    yajl_gen gen = (yajl_gen) jo->jo_ptr_data;

    yajl_gen_array_close(gen);

    return 1;
}

int
main(int argc, char* argv[])
{
    int retval = EXIT_SUCCESS;

    log_argv(argc, argv);

    if (argc != 3) {
        fprintf(stderr, "error: expecting operation and json-pointer\n");
        retval = EXIT_FAILURE;
    } else if (strcmp(argv[1], "get") == 0) {
        unsigned char buffer[1024];
        json_ptr jptr(argv[2]);
        json_op jo(jptr);
        yajl_handle handle;
        yajl_status status;
        yajl_gen gen;
        ssize_t rc;

        gen = yajl_gen_alloc(nullptr);
        yajl_gen_config(gen, yajl_gen_print_callback, printer, nullptr);
        yajl_gen_config(gen, yajl_gen_beautify, true);

        jo.jo_ptr_callbacks.yajl_start_map = handle_start_map;
        jo.jo_ptr_callbacks.yajl_map_key = handle_map_key;
        jo.jo_ptr_callbacks.yajl_end_map = handle_end_map;
        jo.jo_ptr_callbacks.yajl_start_array = handle_start_array;
        jo.jo_ptr_callbacks.yajl_end_array = handle_end_array;
        jo.jo_ptr_callbacks.yajl_null = handle_null;
        jo.jo_ptr_callbacks.yajl_boolean = handle_boolean;
        jo.jo_ptr_callbacks.yajl_number = handle_number;
        jo.jo_ptr_callbacks.yajl_string = handle_string;
        jo.jo_ptr_data = gen;

        handle = yajl_alloc(&json_op::ptr_callbacks, nullptr, &jo);
        while ((rc = read(STDIN_FILENO, buffer, sizeof(buffer))) > 0) {
            status = yajl_parse(handle, buffer, rc);
            if (status == yajl_status_error) {
                auto msg = yajl_get_error(handle, 1, buffer, rc);

                fprintf(stderr, "error:cannot parse JSON input -- %s\n", msg);
                retval = EXIT_FAILURE;
                yajl_free_error(handle, msg);
                break;
            } else if (status == yajl_status_client_canceled) {
                fprintf(stderr, "client cancel\n");
                break;
            }
        }
        status = yajl_complete_parse(handle);
        if (status == yajl_status_error) {
            auto msg = yajl_get_error(handle, 1, buffer, rc);
            fprintf(stderr, "error:cannot parse JSON input -- %s\n", msg);
            yajl_free_error(handle, msg);
            retval = EXIT_FAILURE;
        } else if (status == yajl_status_client_canceled) {
            fprintf(stderr, "client cancel\n");
        }
        yajl_free(handle);
    } else {
        fprintf(stderr, "error: unknown operation -- %s\n", argv[1]);
        retval = EXIT_FAILURE;
    }

    return retval;
}
