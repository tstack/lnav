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
#include <limits.h>

#include <map>
#include <set>
#include <vector>
#include <string>
#include <algorithm>

#include "pcrepp.hh"
#include "intern_string.hh"

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

template<typename T>
inline
T *nullobj()
{
    return (T *) NULL;
}

template<typename T>
inline
T *resolve_simple_object(void *base, void *offset)
{
    ptrdiff_t ptr_offset = (char *) offset - (char *) NULL;
    char *root_ptr = (char *) base;

    return (T *) (root_ptr + ptr_offset);
}

struct json_path_handler;
class yajlpp_gen_context;
class json_schema_validator;
class yajlpp_parse_context;

struct json_path_handler_base {
    json_path_handler_base(const char *path)
            : jph_path(path),
              jph_regex(path, PCRE_ANCHORED),
              jph_gen_callback(NULL),
              jph_obj_provider(NULL),
              jph_synopsis(""),
              jph_description(""),
              jph_simple_offset(NULL),
              jph_children(NULL),
              jph_min_length(0),
              jph_max_length(INT_MAX)
    {
        memset(&this->jph_callbacks, 0, sizeof(this->jph_callbacks));
    };

    virtual yajl_gen_status gen(yajlpp_gen_context &ygc, yajl_gen handle) const;

    const char *   jph_path;
    pcrepp         jph_regex;
    yajl_callbacks jph_callbacks;
    yajl_gen_status (*jph_gen_callback)(yajlpp_gen_context &, const json_path_handler_base &, yajl_gen);
    void           (*jph_validator)(json_schema_validator &validator,
                                    const std::string &path,
                                    const json_path_handler_base &jph);
    void *(*jph_obj_provider)(yajlpp_parse_context &ypc, void *root);
    const char *   jph_synopsis;
    const char *   jph_description;
    void *         jph_simple_offset;
    json_path_handler_base *jph_children;
    size_t         jph_min_length;
    size_t         jph_max_length;
};

int yajlpp_static_string(yajlpp_parse_context *, const unsigned char *, size_t);
int yajlpp_static_intern_string(yajlpp_parse_context *, const unsigned char *, size_t);
yajl_gen_status yajlpp_static_gen_string(yajlpp_gen_context &ygc,
                                         const json_path_handler_base &,
                                         yajl_gen);
void yajlpp_validator_for_string(json_schema_validator &validator,
                                 const std::string &path,
                                 const json_path_handler_base &jph);

int yajlpp_static_number(yajlpp_parse_context *, long long);

int yajlpp_static_bool(yajlpp_parse_context *, int);
yajl_gen_status yajlpp_static_gen_bool(yajlpp_gen_context &ygc,
                                       const json_path_handler_base &,
                                       yajl_gen);

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

    json_path_handler(const char *path) : json_path_handler_base(path) { };

    json_path_handler() : json_path_handler_base("") {};

    json_path_handler &add_cb(int(*null_func)(yajlpp_parse_context *)) {
        this->jph_callbacks.yajl_null = (int (*)(void *))null_func;
        return *this;
    };

    json_path_handler &add_cb(int(*bool_func)(yajlpp_parse_context *, int))
    {
        this->jph_callbacks.yajl_boolean = (int (*)(void *, int))bool_func;
        return *this;
    }

    json_path_handler &add_cb(int(*int_func)(yajlpp_parse_context *, long long))
    {
        this->jph_callbacks.yajl_integer = (int (*)(void *, long long))int_func;
        return *this;
    }

    json_path_handler &add_cb(int(*double_func)(yajlpp_parse_context *, double))
    {
        this->jph_callbacks.yajl_double = (int (*)(void *, double))double_func;
        return *this;
    }

    json_path_handler &add_cb(int(*str_func)(yajlpp_parse_context *, const unsigned char *, size_t))
    {
        this->jph_callbacks.yajl_string = (int (*)(void *, const unsigned char *, size_t))str_func;
        return *this;
    }

    json_path_handler &with_synopsis(const char *synopsis) {
        this->jph_synopsis = synopsis;
        return *this;
    }

    json_path_handler &with_description(const char *description) {
        this->jph_description = description;
        return *this;
    }

    json_path_handler &with_min_length(size_t len) {
        this->jph_min_length = len;
        return *this;
    }

    json_path_handler &with_max_length(size_t len) {
        this->jph_max_length = len;
        return *this;
    }

    template<typename R, typename T>
    json_path_handler &with_obj_provider(R *(*provider)(yajlpp_parse_context &ypc, T *root)) {
        this->jph_obj_provider = (void *(*)(yajlpp_parse_context &, void *)) provider;
        return *this;
    };

    json_path_handler &for_field(std::string *field) {
        this->add_cb(yajlpp_static_string);
        this->jph_simple_offset = field;
        this->jph_gen_callback = yajlpp_static_gen_string;
        this->jph_validator = yajlpp_validator_for_string;
        return *this;
    };

    json_path_handler &for_field(intern_string_t *field) {
        this->add_cb(yajlpp_static_intern_string);
        this->jph_simple_offset = field;
        this->jph_gen_callback = yajlpp_static_gen_string;
        this->jph_validator = yajlpp_validator_for_string;
        return *this;
    };

    json_path_handler &for_field(long long *field) {
        this->add_cb(yajlpp_static_number);
        this->jph_simple_offset = field;
        return *this;
    };

    json_path_handler &for_field(bool *field) {
        this->add_cb(yajlpp_static_bool);
        this->jph_simple_offset = field;
        this->jph_gen_callback = yajlpp_static_gen_bool;
        return *this;
    };

    json_path_handler &with_children(json_path_handler *children) {
        for (int lpc = 0; children[lpc].jph_path[0]; lpc++) {
            json_path_handler &child = children[lpc];

            if (child.jph_obj_provider == NULL) {
                child.jph_obj_provider = this->jph_obj_provider;
            }
        }
        this->jph_children = children;
        return *this;
    };
};

class yajlpp_parse_context {
public:
    yajlpp_parse_context(const std::string &source,
                         struct json_path_handler *handlers = NULL)
        : ypc_source(source),
          ypc_line_number(1),
          ypc_handlers(handlers),
          ypc_userdata(NULL),
          ypc_simple_data(NULL),
          ypc_handle(NULL),
          ypc_json_text(NULL),
          ypc_ignore_unused(false),
          ypc_current_handler(NULL)
    {
        this->ypc_path.reserve(4096);
        this->ypc_path.push_back('\0');
        this->ypc_callbacks = DEFAULT_CALLBACKS;
        memset(&this->ypc_alt_callbacks, 0, sizeof(this->ypc_alt_callbacks));
    };

    void get_path_fragment(int offset, const char **frag, size_t &len_out) const {
        size_t start, end;

        if (offset < 0) {
            offset = this->ypc_path_index_stack.size() + offset;
        }
        start = this->ypc_path_index_stack[offset] + 1;
        if ((offset + 1) < (int)this->ypc_path_index_stack.size()) {
            end = this->ypc_path_index_stack[offset + 1];
        }
        else{
            end = this->ypc_path.size() - 1;
        }
        *frag = &this->ypc_path[start];
        len_out = end - start;
    }

    const intern_string_t get_path_fragment_i(int offset) const {
        const char *frag;
        size_t len;

        this->get_path_fragment(offset, &frag, len);
        return intern_string::lookup(frag, len);
    };

    std::string get_path_fragment(int offset) const
    {
        const char *frag;
        size_t len;

        this->get_path_fragment(offset, &frag, len);
        return std::string(frag, len);
    };

    const intern_string_t get_path() const {
        return intern_string::lookup(&this->ypc_path[1],
                                     this->ypc_path.size() - 2);
    };

    bool is_level(size_t level) const {
        return this->ypc_path_index_stack.size() == level;
    };

    yajlpp_parse_context &set_path(const std::string &path) {
        this->ypc_path.resize(path.size() + 1);
        std::copy(path.begin(), path.end(), this->ypc_path.begin());
        this->ypc_path[path.size()] = '\0';
        return *this;
    }

    void reset(struct json_path_handler *handlers) {
        this->ypc_handlers = handlers;
        this->ypc_path.clear();
        this->ypc_path.push_back('\0');
        this->ypc_path_index_stack.clear();
        this->ypc_array_index.clear();
        this->ypc_callbacks = DEFAULT_CALLBACKS;
        memset(&this->ypc_alt_callbacks, 0, sizeof(this->ypc_alt_callbacks));
    }

    void set_static_handler(struct json_path_handler &jph) {
        this->ypc_path.clear();
        this->ypc_path.push_back('\0');
        this->ypc_path_index_stack.clear();
        this->ypc_array_index.clear();
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

    template<typename T>
    yajlpp_parse_context &with_obj(T &obj) {
        this->ypc_simple_data = &obj;
        return *this;
    };

    yajlpp_parse_context &with_handle(yajl_handle handle) {
        this->ypc_handle = handle;
        return *this;
    };

    yajl_status parse(const unsigned char *jsonText, size_t jsonTextLen) {
        this->ypc_json_text = jsonText;

        yajl_status retval = yajl_parse(this->ypc_handle, jsonText, jsonTextLen);

        if (retval == yajl_status_ok) {
            size_t consumed = yajl_get_bytes_consumed(this->ypc_handle);

            this->ypc_line_number += std::count(&jsonText[0], &jsonText[consumed], '\n');
        }

        this->ypc_json_text = NULL;
        return retval;
    };

    yajl_status complete_parse() {
        return yajl_complete_parse(this->ypc_handle);
    };

    const std::string ypc_source;
    int ypc_line_number;
    struct json_path_handler *ypc_handlers;
    void *                  ypc_userdata;
    void *                  ypc_simple_data;
    yajl_handle             ypc_handle;
    const unsigned char *   ypc_json_text;
    yajl_callbacks          ypc_callbacks;
    yajl_callbacks          ypc_alt_callbacks;
    std::vector<char>       ypc_path;
    std::vector<size_t>     ypc_path_index_stack;
    std::vector<int>        ypc_array_index;
    pcre_context_static<30> ypc_pcre_context;
    bool                    ypc_ignore_unused;
    const struct json_path_handler_base *ypc_current_handler;
    std::set<std::string>   ypc_active_paths;

    void update_callbacks(const json_path_handler_base *handlers = NULL,
                          int child_start = 0);
private:
    static const yajl_callbacks DEFAULT_CALLBACKS;


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

    void operator()(const char *str)
    {
        yajl_gen_string(this->yg_handle, (const unsigned char *)str, strlen(str));
    };

    void operator()(const char *str, size_t len)
    {
        yajl_gen_string(this->yg_handle, (const unsigned char *)str, len);
    };

    void operator()(long long value)
    {
        yajl_gen_integer(this->yg_handle, value);
    };

    void operator()(bool value)
    {
        yajl_gen_bool(this->yg_handle, value);
    };

    void operator()()
    {
        yajl_gen_null(this->yg_handle);
    };
private:
    yajl_gen yg_handle;
};

class yajlpp_container_base {
public:
    yajlpp_container_base(yajl_gen handle)
        : gen(handle), ycb_handle(handle) {};

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

class yajlpp_gen_context {
public:
    yajlpp_gen_context(yajl_gen handle, json_path_handler *handlers)
            : ygc_handle(handle),
              ygc_depth(0),
              ygc_default_data(NULL),
              ygc_simple_data(NULL),
              ygc_handlers(handlers)
    {
    };

    template<typename T>
    yajlpp_gen_context &with_default_obj(T &obj) {
        this->ygc_default_data = &obj;
        return *this;
    };

    template<typename T>
    yajlpp_gen_context &with_obj(T &obj) {
        this->ygc_simple_data = &obj;
        return *this;
    };

    void gen() {
        yajlpp_map root(this->ygc_handle);

        for (int lpc = 0; this->ygc_handlers[lpc].jph_path[0]; lpc++) {
            json_path_handler &jph = this->ygc_handlers[lpc];

            jph.gen(*this, this->ygc_handle);
        }
    };

    yajl_gen ygc_handle;
    int ygc_depth;
    void *ygc_default_data;
    void *ygc_simple_data;
    json_path_handler *ygc_handlers;
    std::vector<std::string> ygc_path;
};

class json_schema {
public:
    json_schema(const json_path_handler_base *handlers)
            : js_handlers(handlers) {
        this->populate("", handlers);
    };

    void populate(const std::string &parent_path,
                  const json_path_handler_base *handlers) {
        for (int lpc = 0; handlers[lpc].jph_path[0]; lpc++) {
            const json_path_handler_base &jph = handlers[lpc];

            if (jph.jph_children) {
                this->populate(parent_path + jph.jph_path, jph.jph_children);
            }
            else {
                std::string option = parent_path + jph.jph_path;

                this->js_path_to_handler[option] = &jph;
            }
        }
    }

    typedef std::map<std::string, const json_path_handler_base *> path_to_handler_t;

    const json_path_handler_base *js_handlers;
    path_to_handler_t js_path_to_handler;
};

class json_schema_validator {
public:

    typedef std::map<std::string, std::vector<std::string> > error_map_t;

    json_schema_validator(const json_schema &schema, void *obj = NULL)
            : jsv_schema(schema), jsv_simple_data(obj) {

    };

    json_schema_validator &check_path(const std::string &path) {
        json_schema::path_to_handler_t::const_iterator iter;

        iter = this->jsv_schema.js_path_to_handler.find(path);
        if (iter->second->jph_validator) {
            iter->second->jph_validator(*this, path, *iter->second);
        }

        return *this;
    };

    void print(FILE *out) const {
        error_map_t::const_iterator iter;

        for (iter = this->jsv_errors.begin();
             iter != this->jsv_errors.end();
             ++iter) {
            const std::string &path = iter->first;
            const json_path_handler_base *jph = this->jsv_schema.js_path_to_handler.find(path)->second;
            const std::vector<std::string> &error_list = iter->second;

            fprintf(out,
                    "error: Invalid configuration option -- %s (%s)\n",
                    path.c_str(),
                    jph->jph_description);
            for (size_t lpc = 0; lpc < error_list.size(); lpc++) {
                fprintf(out, "error:   %s\n", error_list[lpc].c_str());
            }
            fprintf(out, "error: Use the following lnav command to fix the configuration:\n");
            fprintf(out, "error:   :config %s %s\n", path.c_str(), jph->jph_synopsis);
        }
    };

    const json_schema &jsv_schema;
    void *jsv_simple_data;
    error_map_t jsv_errors;
};

#endif
