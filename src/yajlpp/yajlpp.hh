/**
 * Copyright (c) 2013-2019, Timothy Stack
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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file yajlpp.hh
 */

#ifndef yajlpp_hh
#define yajlpp_hh

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stack>
#include <string>
#include <utility>
#include <vector>

#include <limits.h>
#include <stdarg.h>
#include <string.h>

#include "base/file_range.hh"
#include "base/intern_string.hh"
#include "base/lnav.console.hh"
#include "base/lnav.console.into.hh"
#include "base/lnav_log.hh"
#include "base/opt_util.hh"
#include "json_ptr.hh"
#include "pcrepp/pcre2pp.hh"
#include "relative_time.hh"
#include "yajl/api/yajl_gen.h"
#include "yajl/api/yajl_parse.h"

inline yajl_gen_status
yajl_gen_pstring(yajl_gen hand, const char* str, size_t len)
{
    if (len == (size_t) -1) {
        len = strlen(str);
    }
    return yajl_gen_string(hand, (const unsigned char*) str, len);
}

inline yajl_gen_status
yajl_gen_string(yajl_gen hand, const std::string& str)
{
    return yajl_gen_string(
        hand, (const unsigned char*) str.c_str(), str.length());
}

yajl_gen_status yajl_gen_tree(yajl_gen hand, yajl_val val);

void yajl_cleanup_tree(yajl_val val);

template<typename T>
struct positioned_property {
    intern_string_t pp_path;
    source_location pp_location;
    T pp_value;

    lnav::console::snippet to_snippet() const
    {
        return lnav::console::snippet::from(this->pp_location, "");
    }
};

template<typename T, typename... Types>
struct factory_container : public positioned_property<std::shared_ptr<T>> {
    template<Types... DefaultArgs>
    struct with_default_args : public positioned_property<std::shared_ptr<T>> {
        template<typename... Args>
        static Result<with_default_args, lnav::console::user_message> from(
            intern_string_t src, source_location loc, Args... args)
        {
            auto from_res = T::from(args..., DefaultArgs...);

            if (from_res.isOk()) {
                with_default_args retval;

                retval.pp_path = src;
                retval.pp_location = loc;
                retval.pp_value = from_res.unwrap().to_shared();
                return Ok(retval);
            }

            return Err(
                lnav::console::to_user_message(src, from_res.unwrapErr()));
        }

        std::string to_string() const
        {
            if (this->pp_value != nullptr) {
                return this->pp_value->to_string();
            }
            return "";
        }
    };

    template<typename... Args>
    static Result<factory_container, lnav::console::user_message> from(
        intern_string_t src, source_location loc, Args... args)
    {
        auto from_res = T::from(args...);

        if (from_res.isOk()) {
            factory_container retval;

            retval.pp_path = src;
            retval.pp_location = loc;
            retval.pp_value = from_res.unwrap().to_shared();
            return Ok(retval);
        }

        return Err(lnav::console::to_user_message(src, from_res.unwrapErr()));
    }

    std::string to_string() const
    {
        if (this->pp_value != nullptr) {
            return this->pp_value->to_string();
        }
        return "";
    }
};

class yajlpp_gen_context;
class yajlpp_parse_context;

struct yajlpp_provider_context {
    lnav::pcre2pp::match_data* ypc_extractor;
    size_t ypc_index{0};
    yajlpp_parse_context* ypc_parse_context;

    static constexpr size_t nindex = static_cast<size_t>(-1);

    template<typename T>
    intern_string_t get_substr_i(T&& name) const
    {
        auto cap = (*this->ypc_extractor)[std::forward<T>(name)].value();
        char path[cap.length() + 1];
        size_t len = json_ptr::decode(path, cap.data(), cap.length());

        return intern_string::lookup(path, len);
    }

    template<typename T>
    std::string get_substr(T&& name) const
    {
        auto cap = (*this->ypc_extractor)[std::forward<T>(name)].value();
        char path[cap.length() + 1];
        size_t len = json_ptr::decode(path, cap.data(), cap.length());

        return {path, len};
    }
};

class yajlpp_error : public std::exception {
public:
    yajlpp_error(yajl_handle handle, const char* json, size_t len) noexcept
    {
        auto_mem<unsigned char> yajl_msg;

        yajl_msg = yajl_get_error(
            handle, 1, reinterpret_cast<const unsigned char*>(json), len);
        this->ye_msg = reinterpret_cast<const char*>(yajl_msg.in());
    }

    const char* what() const noexcept override { return this->ye_msg.c_str(); }

private:
    std::string ye_msg;
};

struct json_path_container;

struct json_path_handler_base {
    struct enum_value_t {
        template<typename T>
        enum_value_t(const char* name, T value)
            : first(name), second((unsigned int) value)
        {
        }

        const char* first;
        int second;
    };

    static const enum_value_t ENUM_TERMINATOR;

    explicit json_path_handler_base(const std::string& property);

    explicit json_path_handler_base(
        const std::shared_ptr<const lnav::pcre2pp::code>& property_re);

    json_path_handler_base(
        std::string property,
        const std::shared_ptr<const lnav::pcre2pp::code>& property_re);

    bool is_array() const { return this->jph_is_array; }

    std::optional<int> to_enum_value(const string_fragment& sf) const;
    const char* to_enum_string(int value) const;

    template<typename T>
    std::enable_if_t<!detail::is_optional<T>::value, const char*>
    to_enum_string(T value) const
    {
        return this->to_enum_string((int) value);
    }

    template<typename T>
    std::enable_if_t<detail::is_optional<T>::value, const char*> to_enum_string(
        T value) const
    {
        return this->to_enum_string((int) value.value());
    }

    yajl_gen_status gen(yajlpp_gen_context& ygc, yajl_gen handle) const;
    yajl_gen_status gen_schema(yajlpp_gen_context& ygc) const;
    yajl_gen_status gen_schema_type(yajlpp_gen_context& ygc) const;
    void walk(const std::function<void(const json_path_handler_base&,
                                       const std::string&,
                                       const void*)>& cb,
              void* root = nullptr,
              const std::string& base = "/") const;

    enum class schema_type_t : std::uint32_t {
        ANY,
        BOOLEAN,
        INTEGER,
        NUMBER,
        STRING,
        ARRAY,
        OBJECT,
    };

    std::vector<schema_type_t> get_types() const;

    std::string jph_property;
    std::shared_ptr<const lnav::pcre2pp::code> jph_regex;
    yajl_callbacks jph_callbacks{};
    std::function<yajl_gen_status(
        yajlpp_gen_context&, const json_path_handler_base&, yajl_gen)>
        jph_gen_callback;
    std::function<void(yajlpp_parse_context& ypc,
                       const json_path_handler_base& jph)>
        jph_validator;
    std::function<const void*(void* root, std::optional<std::string> name)>
        jph_field_getter;
    std::function<void*(const yajlpp_provider_context& pe, void* root)>
        jph_obj_provider;
    std::function<void(void* root, std::vector<std::string>& paths_out)>
        jph_path_provider;
    std::function<void(const yajlpp_provider_context& pe, void* root)>
        jph_obj_deleter;
    std::function<size_t(void* root)> jph_size_provider;
    const char* jph_synopsis{""};
    const char* jph_description{""};
    const json_path_container* jph_children{nullptr};
    std::shared_ptr<const lnav::pcre2pp::code> jph_pattern;
    const char* jph_pattern_re{nullptr};
    std::function<void(const string_fragment&)> jph_string_validator;
    size_t jph_min_length{0};
    size_t jph_max_length{INT_MAX};
    const enum_value_t* jph_enum_values{nullptr};
    long long jph_min_value{LLONG_MIN};
    bool jph_optional_wrapper{false};
    bool jph_is_array;
    bool jph_is_pattern_property{false};
    std::vector<std::string> jph_examples;

    std::function<int(yajlpp_parse_context*)> jph_null_cb;
    std::function<int(yajlpp_parse_context*, int)> jph_bool_cb;
    std::function<int(yajlpp_parse_context*, long long)> jph_integer_cb;
    std::function<int(yajlpp_parse_context*, double)> jph_double_cb;
    std::function<int(yajlpp_parse_context*, const string_fragment& sf)>
        jph_str_cb;

    void validate_string(yajlpp_parse_context& ypc, string_fragment sf) const;

    void report_pattern_error(yajlpp_parse_context* ypc,
                              const std::string& value_str) const;
    void report_min_value_error(yajlpp_parse_context* ypc,
                                long long value) const;
    void report_duration_error(yajlpp_parse_context* ypc,
                               const std::string& value_str,
                               const relative_time::parse_error& pe) const;
    void report_enum_error(yajlpp_parse_context* ypc,
                           const std::string& value_str) const;
    void report_error(yajlpp_parse_context* ypc,
                      const std::string& value_str,
                      lnav::console::user_message um) const;
    void report_tz_error(yajlpp_parse_context* ypc,
                         const std::string& value_str,
                         const char* msg) const;

    attr_line_t get_help_text(const std::string& full_path) const;
    attr_line_t get_help_text(yajlpp_parse_context* ypc) const;
};

struct json_path_handler;

class yajlpp_parse_context {
public:
    using error_reporter_t = std::function<void(
        const yajlpp_parse_context& ypc, const lnav::console::user_message&)>;

    yajlpp_parse_context(intern_string_t source,
                         const struct json_path_container* handlers = nullptr);

    const char* get_path_fragment(int offset,
                                  char* frag_in,
                                  size_t& len_out) const;

    intern_string_t get_path_fragment_i(int offset) const
    {
        char fragbuf[this->ypc_path.size()];
        const char* frag;
        size_t len;

        frag = this->get_path_fragment(offset, fragbuf, len);
        return intern_string::lookup(frag, len);
    }

    std::string get_path_fragment(int offset) const
    {
        char fragbuf[this->ypc_path.size()];
        const char* frag;
        size_t len;

        frag = this->get_path_fragment(offset, fragbuf, len);
        return std::string(frag, len);
    }

    const intern_string_t get_path() const;

    const intern_string_t get_full_path() const;

    bool is_level(size_t level) const
    {
        return this->ypc_path_index_stack.size() == level;
    }

    yajlpp_parse_context& set_path(const std::string& path);

    void reset(const struct json_path_container* handlers);

    void set_static_handler(const struct json_path_handler_base& jph);

    template<typename T>
    yajlpp_parse_context& with_obj(T& obj)
    {
        this->ypc_obj_stack.push(&obj);
        return *this;
    }

    yajlpp_parse_context& with_handle(yajl_handle handle)
    {
        this->ypc_handle = handle;
        return *this;
    }

    yajlpp_parse_context& with_error_reporter(error_reporter_t err)
    {
        this->ypc_error_reporter = err;
        return *this;
    }

    yajlpp_parse_context& with_ignore_unused(bool ignore)
    {
        this->ypc_ignore_unused = ignore;
        return *this;
    }

    yajl_status parse(const unsigned char* jsonText, size_t jsonTextLen);

    yajl_status parse(const string_fragment& sf)
    {
        return this->parse((const unsigned char*) sf.data(), sf.length());
    }

    int get_line_number() const;

    yajl_status complete_parse();

    bool parse_doc(const string_fragment& sf);

    void report_error(const lnav::console::user_message& msg) const
    {
        if (this->ypc_error_reporter) {
            this->ypc_error_reporter(*this, msg);
        }
    }

    lnav::console::snippet get_snippet() const;

    template<typename T>
    std::vector<T>& get_lvalue(std::map<std::string, std::vector<T>>& value)
    {
        return value[this->get_path_fragment(-2)];
    }

    template<typename T>
    T& get_lvalue(std::map<std::string, T>& value)
    {
        return value[this->get_path_fragment(-1)];
    }

    template<typename T>
    T& get_lvalue(T& lvalue)
    {
        return lvalue;
    }

    template<typename T>
    T& get_rvalue(std::map<std::string, std::vector<T>>& value)
    {
        return value[this->get_path_fragment(-2)].back();
    }

    template<typename T>
    T& get_rvalue(std::map<std::string, T>& value)
    {
        return value[this->get_path_fragment(-1)];
    }

    template<typename T>
    T& get_rvalue(std::vector<T>& value)
    {
        return value.back();
    }

    template<typename T>
    T& get_rvalue(T& lvalue)
    {
        return lvalue;
    }

    template<typename T, typename MEM_T, MEM_T T::*MEM>
    auto& get_obj_member()
    {
        auto obj = (T*) this->ypc_obj_stack.top();

        return obj->*MEM;
    }

    void fill_in_source()
    {
        if (this->ypc_locations != nullptr) {
            (*this->ypc_locations)[this->get_full_path()] = source_location{
                this->ypc_source,
                this->get_line_number(),
            };
        }
    }

    const intern_string_t ypc_source;
    int ypc_line_number{1};
    const struct json_path_container* ypc_handlers;
    std::stack<void*> ypc_obj_stack;
    void* ypc_userdata{nullptr};
    yajl_handle ypc_handle{nullptr};
    const unsigned char* ypc_json_text{nullptr};
    size_t ypc_json_text_len{0};
    size_t ypc_total_consumed{0};
    yajl_callbacks ypc_callbacks;
    yajl_callbacks ypc_alt_callbacks;
    std::vector<char> ypc_path;
    std::vector<size_t> ypc_path_index_stack;
    std::vector<size_t> ypc_array_index;
    std::vector<const json_path_handler_base*> ypc_handler_stack;
    size_t ypc_array_handler_count{0};
    bool ypc_ignore_unused{false};
    const struct json_path_container* ypc_sibling_handlers{nullptr};
    const struct json_path_handler_base* ypc_current_handler{nullptr};
    std::set<std::string> ypc_active_paths;
    error_reporter_t ypc_error_reporter{nullptr};
    std::map<intern_string_t, source_location>* ypc_locations{nullptr};

    void update_callbacks(const json_path_container* handlers = nullptr,
                          int child_start = 0);

private:
    static const yajl_callbacks DEFAULT_CALLBACKS;

    static int map_start(void* ctx);
    static int map_key(void* ctx, const unsigned char* key, size_t len);
    static int map_end(void* ctx);
    static int array_start(void* ctx);
    static int array_end(void* ctx);
    static int handle_unused(void* ctx);
    static int handle_unused_or_delete(void* ctx);
};

class yajlpp_generator {
public:
    yajlpp_generator(yajl_gen handle) : yg_handle(handle) {}

    yajl_gen_status operator()(const std::string& str)
    {
        return yajl_gen_string(this->yg_handle, str);
    }

    yajl_gen_status operator()(const char* str)
    {
        return yajl_gen_string(
            this->yg_handle, (const unsigned char*) str, strlen(str));
    }

    yajl_gen_status operator()(const char* str, size_t len)
    {
        return yajl_gen_string(
            this->yg_handle, (const unsigned char*) str, len);
    }

    yajl_gen_status operator()(const intern_string_t& str)
    {
        return yajl_gen_string(
            this->yg_handle, (const unsigned char*) str.get(), str.size());
    }

    yajl_gen_status operator()(const string_fragment& str)
    {
        return yajl_gen_string(
            this->yg_handle, (const unsigned char*) str.data(), str.length());
    }

    yajl_gen_status operator()(bool value)
    {
        return yajl_gen_bool(this->yg_handle, value);
    }

    yajl_gen_status operator()(double value)
    {
        return yajl_gen_double(this->yg_handle, value);
    }

    template<typename T>
    yajl_gen_status operator()(
        T value,
        typename std::enable_if<std::is_integral<T>::value
                                && !std::is_same<T, bool>::value>::type* dummy
        = 0)
    {
        return yajl_gen_integer(this->yg_handle, value);
    }

    template<typename T>
    yajl_gen_status operator()(std::optional<T> value)
    {
        if (!value.has_value()) {
            return yajl_gen_status_ok;
        }

        return (*this)(value.value());
    }

    template<typename T>
    yajl_gen_status operator()(
        const T& container,
        typename std::enable_if<!std::is_integral<T>::value>::type* dummy = 0)
    {
        yajl_gen_array_open(this->yg_handle);
        for (const auto& elem : container) {
            yajl_gen_status rc = (*this)(elem);

            if (rc != yajl_gen_status_ok) {
                return rc;
            }
        }

        yajl_gen_array_close(this->yg_handle);

        return yajl_gen_status_ok;
    }

    yajl_gen_status operator()() { return yajl_gen_null(this->yg_handle); }

private:
    yajl_gen yg_handle;
};

class yajlpp_container_base {
public:
    yajlpp_container_base(yajl_gen handle) : gen(handle), ycb_handle(handle) {}

    yajlpp_generator gen;

protected:
    yajl_gen ycb_handle;
};

class yajlpp_map : public yajlpp_container_base {
public:
    yajlpp_map(yajl_gen handle) : yajlpp_container_base(handle)
    {
        yajl_gen_map_open(handle);
    }

    ~yajlpp_map() { yajl_gen_map_close(this->ycb_handle); }
};

class yajlpp_array : public yajlpp_container_base {
public:
    yajlpp_array(yajl_gen handle) : yajlpp_container_base(handle)
    {
        yajl_gen_array_open(handle);
    }

    ~yajlpp_array() { yajl_gen_array_close(this->ycb_handle); }
};

class yajlpp_gen_context {
public:
    yajlpp_gen_context(yajl_gen handle,
                       const struct json_path_container& handlers)
        : ygc_handle(handle), ygc_depth(0), ygc_handlers(&handlers)
    {
    }

    template<typename T>
    yajlpp_gen_context& with_default_obj(T& obj)
    {
        this->ygc_default_stack.push(&obj);
        return *this;
    }

    template<typename T>
    yajlpp_gen_context& with_obj(const T& obj)
    {
        this->ygc_obj_stack.push((void*) &obj);
        return *this;
    }

    yajlpp_gen_context& with_context(yajlpp_parse_context& ypc);

    void gen();

    void gen_schema(const json_path_container* handlers = nullptr);

    yajl_gen ygc_handle;
    int ygc_depth;
    std::stack<void*> ygc_default_stack;
    std::stack<void*> ygc_obj_stack;
    std::vector<std::string> ygc_path;
    const json_path_container* ygc_handlers;
    std::map<std::string, const json_path_container*> ygc_schema_definitions;
};

class yajlpp_gen {
public:
    yajlpp_gen() : yg_handle(yajl_gen_free)
    {
        this->yg_handle = yajl_gen_alloc(nullptr);
    }

    yajl_gen get_handle() const { return this->yg_handle.in(); }

    operator yajl_gen() { return this->yg_handle.in(); }

    string_fragment to_string_fragment();

private:
    auto_mem<yajl_gen_t> yg_handle;
};

struct json_string {
    explicit json_string(yajl_gen_t* gen)
    {
        const unsigned char* buf;

        yajl_gen_get_buf(gen, &buf, &this->js_len);

        this->js_content = (const unsigned char*) malloc(this->js_len);
        memcpy((void*) this->js_content.in(), buf, this->js_len);
    }

    explicit json_string(auto_buffer&& buf)
    {
        auto buf_pair = buf.release();

        this->js_content = (const unsigned char*) buf_pair.first;
        this->js_len = buf_pair.second;
    }

    string_fragment to_string_fragment() const
    {
        return string_fragment::from_bytes(this->js_content, this->js_len);
    }

    auto_mem<const unsigned char> js_content;
    size_t js_len{0};
};

void dump_schema_to(const json_path_container& jpc, const char* internals_dir);

namespace yajlpp {

auto_mem<yajl_handle_t> alloc_handle(const yajl_callbacks* cb, void* cu);

}  // namespace yajlpp

#endif
