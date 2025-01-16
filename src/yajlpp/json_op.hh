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
 * @file json_op.hh
 */

#ifndef json_op_hh
#define json_op_hh

#include <string>

#include <sys/types.h>

#include "yajl/api/yajl_parse.h"
#include "yajlpp/json_ptr.hh"

class json_op {
    static int handle_null(void* ctx);
    static int handle_boolean(void* ctx, int boolVal);
    static int handle_number(void* ctx,
                             const char* numberVal,
                             size_t numberLen);
    static int handle_string(void* ctx,
                             const unsigned char* stringVal,
                             size_t len,
                             yajl_string_props_t* props);
    static int handle_start_map(void* ctx);
    static int handle_map_key(void* ctx, const unsigned char* key, size_t len);
    static int handle_end_map(void* ctx);
    static int handle_start_array(void* ctx);
    static int handle_end_array(void* ctx);

public:
    const static yajl_callbacks gen_callbacks;
    const static yajl_callbacks ptr_callbacks;

    explicit json_op(const json_ptr& ptr)
        : jo_ptr(ptr), jo_ptr_callbacks(gen_callbacks)
    {
    }

    bool check_index(bool primitive = true)
    {
        return this->jo_ptr.at_index(
            this->jo_depth, this->jo_array_index, primitive);
    }

    int jo_depth{0};
    int jo_array_index{-1};

    json_ptr jo_ptr;
    yajl_callbacks jo_ptr_callbacks;
    void* jo_ptr_data{nullptr};
    std::string jo_ptr_error;
    int jo_ptr_error_code{0};
};

#endif
