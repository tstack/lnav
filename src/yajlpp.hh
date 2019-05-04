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
#include <memory>
#include <set>
#include <stack>
#include <utility>
#include <vector>
#include <string>
#include <algorithm>
#include <functional>

#include "optional.hpp"
#include "pcrepp.hh"
#include "json_ptr.hh"
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

class yajlpp_gen_context;
class yajlpp_parse_context;

struct yajlpp_provider_context {
    pcre_extractor ypc_extractor;
    int ypc_index;

    template<typename T>
    intern_string_t get_substr_i(T name) const {
        pcre_context::iterator cap = this->ypc_extractor.pe_context[name];
        char path[cap->length() + 1];
        size_t len = json_ptr::decode(path, this->ypc_extractor.pe_input.get_substr_start(cap), cap->length());

        return intern_string::lookup(path, len);
    };

    template<typename T>
    std::string get_substr(T name) const {
        pcre_context::iterator cap = this->ypc_extractor.pe_context[name];
        char path[cap->length() + 1];
        size_t len = json_ptr::decode(path, this->ypc_extractor.pe_input.get_substr_start(cap), cap->length());

        return std::string(path, len);
    };
};

class yajlpp_error : public std::exception {
public:
    yajlpp_error(yajl_handle handle, const char *json, size_t len) {
        auto_mem<unsigned char> yajl_msg;

        yajl_msg = yajl_get_error(handle, 1, (const unsigned char *) json, len);
        this->msg = (const char *) yajl_msg.in();
    }

    ~yajlpp_error() override {

    }

    const char *what() const noexcept override {
        return this->msg.c_str();
    }

private:
    std::string msg;
};

struct json_path_handler_base {
    struct enum_value_t {
        template<typename T>
        enum_value_t(const char *name, T value)
            : first(name),
              second((unsigned int) value) {
        }

        const char *first;
        unsigned int second;
    };

    static const enum_value_t ENUM_TERMINATOR;

    json_path_handler_base(const char *path)
            : jph_path(path),
              jph_regex(path, PCRE_ANCHORED),
              jph_gen_callback(NULL),
              jph_field_getter(nullptr),
              jph_obj_provider(NULL),
              jph_path_provider(NULL),
              jph_synopsis(""),
              jph_description(""),
              jph_children(NULL),
              jph_kv_pair(false),
              jph_min_length(0),
              jph_max_length(INT_MAX),
              jph_enum_values(NULL),
              jph_min_value(LLONG_MIN),
              jph_optional_wrapper(false)
    {
        memset(&this->jph_callbacks, 0, sizeof(this->jph_callbacks));
    };

    nonstd::optional<int> to_enum_value(const string_fragment &sf) const {
        for (int lpc = 0; this->jph_enum_values[lpc].first; lpc++) {
            const enum_value_t &ev = this->jph_enum_values[lpc];

            if (sf == ev.first) {
                return ev.second;
            }
        }

        return nonstd::nullopt;
    };

    virtual yajl_gen_status gen(yajlpp_gen_context &ygc, yajl_gen handle) const;
    virtual void walk(
        const std::function<void(const json_path_handler_base &,
                                 const std::string &,
                                 void *)> &cb,
        void *root = nullptr,
        const std::string &base = "") const;

    const char *   jph_path;
    pcrepp         jph_regex;
    yajl_callbacks jph_callbacks;
    yajl_gen_status (*jph_gen_callback)(yajlpp_gen_context &, const json_path_handler_base &, yajl_gen);
    void           (*jph_validator)(yajlpp_parse_context &ypc,
                                    const json_path_handler_base &jph);
    void *(*jph_field_getter)(void *root, nonstd::optional<std::string> name);
    void *(*jph_obj_provider)(const yajlpp_provider_context &pe, void *root);
    void (*jph_path_provider)(void *root, std::vector<std::string> &paths_out);
    const char *   jph_synopsis;
    const char *   jph_description;
    json_path_handler_base *jph_children;
    bool           jph_kv_pair;
    std::shared_ptr<pcrepp> jph_pattern;
    const char * jph_pattern_re{nullptr};
    std::function<void(const string_fragment &)> jph_string_validator;
    size_t         jph_min_length;
    size_t         jph_max_length;
    const enum_value_t  *jph_enum_values;
    long long      jph_min_value;
    bool           jph_optional_wrapper;
};

struct json_path_handler;

struct source_location {
    source_location()
        : sl_source(intern_string::lookup("unknown")),
          sl_line_number(-1) {
    }

    source_location(intern_string_t source, int line)
        : sl_source(source), sl_line_number(line) {};

    intern_string_t sl_source;
    int sl_line_number;
};

class yajlpp_parse_context {
public:
    typedef void (*error_reporter_t)(const yajlpp_parse_context &ypc,
                                     lnav_log_level_t level,
                                     const char *msg);

    yajlpp_parse_context(std::string source,
                         struct json_path_handler *handlers = nullptr)
        : ypc_source(std::move(source)),
          ypc_handlers(handlers)
    {
        this->ypc_path.reserve(4096);
        this->ypc_path.push_back('\0');
        this->ypc_callbacks = DEFAULT_CALLBACKS;
        memset(&this->ypc_alt_callbacks, 0, sizeof(this->ypc_alt_callbacks));
    };

    const char *get_path_fragment(int offset, char *frag_in, size_t &len_out) const {
        const char *retval;
        size_t start, end;

        if (offset < 0) {
            offset = this->ypc_path_index_stack.size() + offset;
        }
        start = this->ypc_path_index_stack[offset] + 1;
        if ((offset + 1) < (int)this->ypc_path_index_stack.size()) {
            end = this->ypc_path_index_stack[offset + 1];
        }
        else {
            end = this->ypc_path.size() - 1;
        }
        if (this->ypc_handlers) {
            len_out = json_ptr::decode(frag_in, &this->ypc_path[start], end - start);
            retval = frag_in;
        }
        else {
            retval = &this->ypc_path[start];
            len_out = end - start;
        }

        return retval;
    }

    const intern_string_t get_path_fragment_i(int offset) const {
        char fragbuf[this->ypc_path.size()];
        const char *frag;
        size_t len;

        frag = this->get_path_fragment(offset, fragbuf, len);
        return intern_string::lookup(frag, len);
    };

    std::string get_path_fragment(int offset) const {
        char fragbuf[this->ypc_path.size()];
        const char *frag;
        size_t len;

        frag = this->get_path_fragment(offset, fragbuf, len);
        return std::string(frag, len);
    };

    const intern_string_t get_path() const {
        return intern_string::lookup(&this->ypc_path[1],
                                     this->ypc_path.size() - 2);
    };

    const intern_string_t get_full_path() const {
        return intern_string::lookup(&this->ypc_path[0],
                                     this->ypc_path.size() - 1);
    };

    bool is_level(size_t level) const {
        return this->ypc_path_index_stack.size() == level;
    };

    yajlpp_parse_context &set_path(const std::string &path) {
        this->ypc_path.resize(path.size() + 1);
        std::copy(path.begin(), path.end(), this->ypc_path.begin());
        this->ypc_path[path.size()] = '\0';
        for (size_t lpc = 0; lpc < path.size(); lpc++) {
            switch (path[lpc]) {
                case '/':
                    this->ypc_path_index_stack.push_back(lpc);
                    break;
            }
        }
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
        this->ypc_sibling_handlers = nullptr;
        this->ypc_current_handler = nullptr;
        while (!this->ypc_obj_stack.empty()) {
            this->ypc_obj_stack.pop();
        }
    }

    void set_static_handler(struct json_path_handler_base &jph) {
        this->ypc_path.clear();
        this->ypc_path.push_back('\0');
        this->ypc_path_index_stack.clear();
        this->ypc_array_index.clear();
        if (jph.jph_callbacks.yajl_null != nullptr)
            this->ypc_callbacks.yajl_null = jph.jph_callbacks.yajl_null;
        if (jph.jph_callbacks.yajl_boolean != nullptr)
            this->ypc_callbacks.yajl_boolean = jph.jph_callbacks.yajl_boolean;
        if (jph.jph_callbacks.yajl_integer != nullptr)
            this->ypc_callbacks.yajl_integer = jph.jph_callbacks.yajl_integer;
        if (jph.jph_callbacks.yajl_double != nullptr)
            this->ypc_callbacks.yajl_double = jph.jph_callbacks.yajl_double;
        if (jph.jph_callbacks.yajl_string != nullptr)
            this->ypc_callbacks.yajl_string = jph.jph_callbacks.yajl_string;
    }

    template<typename T>
    yajlpp_parse_context &with_obj(T &obj) {
        this->ypc_obj_stack.push(&obj);
        return *this;
    };

    yajlpp_parse_context &with_handle(yajl_handle handle) {
        this->ypc_handle = handle;
        return *this;
    };

    yajlpp_parse_context &with_error_reporter(error_reporter_t err) {
        this->ypc_error_reporter = err;
        return *this;
    }

    yajlpp_parse_context &with_ignore_unused(bool ignore) {
        this->ypc_ignore_unused = ignore;
        return *this;
    }

    yajl_status parse(const unsigned char *jsonText, size_t jsonTextLen);;

    int get_line_number() const {
        if (this->ypc_handle != NULL && this->ypc_json_text) {
            size_t consumed = yajl_get_bytes_consumed(this->ypc_handle);
            long current_count = std::count(&this->ypc_json_text[0],
                                            &this->ypc_json_text[consumed],
                                            '\n');

            return this->ypc_line_number + current_count;
        }
        else {
            return this->ypc_line_number;
        }
    };

    yajl_status complete_parse();;

    void report_error(lnav_log_level_t level, const char *format, ...) {
        va_list args;

        va_start(args, format);
        if (this->ypc_error_reporter) {
            char buffer[1024];

            vsnprintf(buffer, sizeof(buffer), format, args);

            this->ypc_error_reporter(*this, level, buffer);
        }
        va_end(args);
    }

    template<typename T>
    std::vector<T> &get_lvalue(std::map<std::string, std::vector<T>> &value) {
        return value[this->get_path_fragment(-2)];
    };

    template<typename T>
    T &get_lvalue(std::map<std::string, T> &value) {
        return value[this->get_path_fragment(-1)];
    };

    template<typename T>
    T &get_lvalue(T &lvalue) {
        return lvalue;
    };

    template<typename T>
    T &get_rvalue(std::map<std::string, std::vector<T>> &value) {
        return value[this->get_path_fragment(-2)].back();
    };

    template<typename T>
    T &get_rvalue(std::map<std::string, T> &value) {
        return value[this->get_path_fragment(-1)];
    };

    template<typename T>
    T &get_rvalue(std::vector<T> &value) {
        return value.back();
    };

    template<typename T>
    T &get_rvalue(T &lvalue) {
        return lvalue;
    };

    template<typename T, typename MEM_T, MEM_T T::*MEM>
    auto &get_obj_member() {
        auto obj = (T *) this->ypc_obj_stack.top();

        return obj->*MEM;
    };

    const std::string ypc_source;
    int ypc_line_number{1};
    struct json_path_handler *ypc_handlers;
    std::stack<void *> ypc_obj_stack;
    void *                  ypc_userdata{nullptr};
    yajl_handle             ypc_handle{nullptr};
    const unsigned char *   ypc_json_text{nullptr};
    yajl_callbacks          ypc_callbacks;
    yajl_callbacks          ypc_alt_callbacks;
    std::vector<char>       ypc_path;
    std::vector<size_t>     ypc_path_index_stack;
    std::vector<int>        ypc_array_index;
    std::vector<const json_path_handler_base *> ypc_handler_stack;
    pcre_context_static<30> ypc_pcre_context;
    bool                    ypc_ignore_unused{false};
    const struct json_path_handler_base *ypc_sibling_handlers{nullptr};
    const struct json_path_handler_base *ypc_current_handler{nullptr};
    std::set<std::string>   ypc_active_paths;
    error_reporter_t ypc_error_reporter{nullptr};
    std::map<intern_string_t, source_location> *ypc_locations{nullptr};

    void update_callbacks(const json_path_handler_base *handlers = NULL,
                          int child_start = 0);
private:
    static const yajl_callbacks DEFAULT_CALLBACKS;

    int index_for_provider() const {
        return this->ypc_array_index.empty() ? -1 : this->ypc_array_index.back();
    };

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

    yajl_gen_status operator()(const std::string &str)
    {
        return yajl_gen_string(this->yg_handle, str);
    };

    yajl_gen_status operator()(const char *str)
    {
        return yajl_gen_string(this->yg_handle, (const unsigned char *)str, strlen(str));
    };

    yajl_gen_status operator()(const char *str, size_t len)
    {
        return yajl_gen_string(this->yg_handle, (const unsigned char *)str, len);
    };

    yajl_gen_status operator()(const intern_string_t &str)
    {
        return yajl_gen_string(this->yg_handle, (const unsigned char *)str.get(), str.size());
    };

    yajl_gen_status operator()(bool value)
    {
        return yajl_gen_bool(this->yg_handle, value);
    };

    template<typename T>
    yajl_gen_status operator()(T value,
                               typename std::enable_if<std::is_integral<T>::value &&
                                                       !std::is_same<T, bool>::value>::type* dummy = 0)
    {
        return yajl_gen_integer(this->yg_handle, value);
    };

    template<typename T>
    yajl_gen_status operator()(const T &container,
                               typename std::enable_if<!std::is_integral<T>::value>::type* dummy = 0)
    {
        yajl_gen_array_open(this->yg_handle);
        for (auto elem : container) {
            yajl_gen_status rc = (*this)(elem);

            if (rc != yajl_gen_status_ok) {
                return rc;
            }
        }

        yajl_gen_array_close(this->yg_handle);

        return yajl_gen_status_ok;
    };

    yajl_gen_status operator()()
    {
        return yajl_gen_null(this->yg_handle);
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
              ygc_handlers(handlers)
    {
    };

    template<typename T>
    yajlpp_gen_context &with_default_obj(T &obj) {
        this->ygc_default_stack.push(&obj);
        return *this;
    };

    template<typename T>
    yajlpp_gen_context &with_obj(T &obj) {
        this->ygc_obj_stack.push(&obj);
        return *this;
    };

    yajlpp_gen_context &with_context(yajlpp_parse_context &ypc);

    void gen();

    yajl_gen ygc_handle;
    int ygc_depth;
    std::stack<void *> ygc_default_stack;
    std::stack<void *> ygc_obj_stack;
    std::string ygc_base_name;
    json_path_handler *ygc_handlers;
};

class yajlpp_gen {
public:
    yajlpp_gen() : yg_handle(yajl_gen_free) {
        this->yg_handle = yajl_gen_alloc(NULL);
    };

    operator yajl_gen_t *() {
        return this->yg_handle.in();
    };

    string_fragment to_string_fragment() {
        const unsigned char *buf;
        size_t len;

        yajl_gen_get_buf(this->yg_handle.in(), &buf, &len);

        return string_fragment((const char *) buf, 0, len);
    };

private:
    auto_mem<yajl_gen_t> yg_handle;
};

#endif
