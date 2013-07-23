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
 * @file yajlpp.hh
 */

#ifndef _yajlpp_hh
#define _yajlpp_hh

#include <string.h>
#include <stdarg.h>

#include <vector>
#include <string>

#include "pcrepp.hh"

#include "yajl/api/yajl_parse.h"
#include "yajl/api/yajl_gen.h"

inline
yajl_gen_status yajl_gen_pstring(yajl_gen hand, const char *str, size_t len)
{
    if (len == (size_t)-1) {
        len = strlen(str);
    }
    return yajl_gen_string(hand, (const unsigned char *)str, len);
}

inline
yajl_gen_status yajl_gen_string(yajl_gen hand, const std::string &str)
{
    return yajl_gen_string(hand,
                           (const unsigned char *)str.c_str(),
                           str.length());
}

struct json_path_handler_base {
    json_path_handler_base(const char *path) : jph_path(path), jph_regex(path)
    {
        memset(&this->jph_callbacks, 0, sizeof(this->jph_callbacks));
    };

    const char *   jph_path;
    pcrepp         jph_regex;
    yajl_callbacks jph_callbacks;
};

class yajlpp_parse_context;

struct json_path_handler : public json_path_handler_base {
    json_path_handler(const char *path, int(*null_func)(yajlpp_parse_context *))
        : json_path_handler_base(path)
    {
        this->jph_callbacks.yajl_null = (int (*)(void *))null_func;
    };

    json_path_handler(const char *path, int(*bool_func)(yajlpp_parse_context *, int))
        : json_path_handler_base(path)
    {
        this->jph_callbacks.yajl_boolean = (int (*)(void *, int))bool_func;
    }

    json_path_handler(const char *path, int(*int_func)(yajlpp_parse_context *, long long))
        : json_path_handler_base(path)
    {
        this->jph_callbacks.yajl_integer = (int (*)(void *, long long))int_func;
    }

    json_path_handler(const char *path, int(*double_func)(yajlpp_parse_context *, double))
        : json_path_handler_base(path)
    {
        this->jph_callbacks.yajl_double = (int (*)(void *, double))double_func;
    }

    json_path_handler(const char *path,
                      int(*str_func)(yajlpp_parse_context *, const unsigned char *, size_t))
        : json_path_handler_base(path)
    {
        this->jph_callbacks.yajl_string = (int (*)(void *, const unsigned char *, size_t))str_func;
    }

    json_path_handler() : json_path_handler_base("") {};
};

class yajlpp_parse_context {
public:

    struct json_path_element {
        json_path_element(int index = 0) : jpe_index(index) { };
        json_path_element(const std::string &name)
            : jpe_name(name), jpe_index(0) {};
        json_path_element(const unsigned char *name)
            : jpe_name((const char *)name), jpe_index(0) {};

        void        set_name(const unsigned char *name, size_t len)
        {
            this->jpe_name = std::string((const char *)name, len);
        };

        std::string jpe_name;
        int         jpe_index;
    };

    yajlpp_parse_context(std::string source,
                         struct json_path_handler *handlers)
        : ypc_source(source), ypc_handlers(handlers)
    {
        this->ypc_callbacks = DEFAULT_CALLBACKS;
    };

    std::string get_path_fragment(int offset) const
    {
        size_t start, end;

        if (offset < 0) {
            offset = this->ypc_path_index_stack.size() + offset;
        }
        start = this->ypc_path_index_stack[offset] + 1;
        if ((offset + 1) < (int)this->ypc_path_index_stack.size()) {
            end = this->ypc_path_index_stack[offset + 1];
        }
        else{
            end = std::string::npos;
        }
        return this->ypc_path.substr(start, end - start);
    };

    const std::string ypc_source;
    struct json_path_handler *ypc_handlers;
    void *                  ypc_userdata;
    yajl_callbacks          ypc_callbacks;
    std::string             ypc_path;
    std::vector<size_t>     ypc_path_index_stack;
    std::vector<int>        ypc_array_index;
    pcre_context_static<30> ypc_pcre_context;

private:
    static const yajl_callbacks DEFAULT_CALLBACKS;

    void update_callbacks(void);

    static int map_start(void *ctx);
    static int map_key(void *ctx, const unsigned char *key, size_t len);
    static int map_end(void *ctx);
    static int array_start(void *ctx);
    static int array_end(void *ctx);
    static int handle_unused(void *ctx);
};

class yajlpp_generator {
public:
    yajlpp_generator(yajl_gen handle) : yg_handle(handle) { };

    void operator()(const std::string &str)
    {
        yajl_gen_string(this->yg_handle, str);
    };

    void operator()(long long value)
    {
        yajl_gen_integer(this->yg_handle, value);
    };

private:
    yajl_gen yg_handle;
};

class yajlpp_container_base {
public:
    yajlpp_container_base(yajl_gen handle)
        : gen(handle), ycb_handle(handle) {};

    void operator()(const std::string &str)
    {
        yajl_gen_string(this->ycb_handle, str);
    };

    void operator()(long long value)
    {
        yajl_gen_integer(this->ycb_handle, value);
    };

    yajlpp_generator gen;

protected:
    yajl_gen ycb_handle;
};

class yajlpp_map : public yajlpp_container_base {
public:
    yajlpp_map(yajl_gen handle) : yajlpp_container_base(handle)
    {
        yajl_gen_map_open(handle);
    };

    ~yajlpp_map() { yajl_gen_map_close(this->ycb_handle); };
};

class yajlpp_array : public yajlpp_container_base {
public:
    yajlpp_array(yajl_gen handle) : yajlpp_container_base(handle)
    {
        yajl_gen_array_open(handle);
    };

    ~yajlpp_array() { yajl_gen_array_close(this->ycb_handle); };
};
#endif
