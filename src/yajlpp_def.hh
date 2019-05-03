/**
 * Copyright (c) 2018, Timothy Stack
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
 * @file yajlpp_def.hh
 */

#ifndef __yajlpp_def_hh
#define __yajlpp_def_hh

#include "yajlpp.hh"

#define FOR_FIELD(T, FIELD) \
    for_field<T, decltype(T :: FIELD), & T :: FIELD>()

inline intern_string_t &assign(intern_string_t &lhs, const string_fragment &rhs) {
    lhs = intern_string::lookup(rhs.data(), rhs.length());

    return lhs;
}

inline std::string &assign(std::string &lhs, const string_fragment &rhs) {
    lhs.assign(rhs.data(), rhs.length());

    return lhs;
}

template<template<typename ...> class Container>
inline Container<std::string> &assign(Container<std::string> &lhs, const string_fragment &rhs) {
    lhs.emplace_back(rhs.data(), rhs.length());

    return lhs;
}

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

    json_path_handler &with_enum_values(const enum_value_t values[]) {
        this->jph_enum_values = values;
        return *this;
    }

    json_path_handler &with_pattern(const char *re) {
        this->jph_pattern_re = re;
        this->jph_pattern = std::make_shared<pcrepp>(re);
        return *this;
    };

    json_path_handler &with_string_validator(std::function<void(const string_fragment &)> val) {
        this->jph_string_validator = val;
        return *this;
    };

    json_path_handler &with_min_value(long long val) {
        this->jph_min_value = val;
        return *this;
    }

    template<typename R, typename T>
    json_path_handler &with_obj_provider(R *(*provider)(const yajlpp_provider_context &pc, T *root)) {
        this->jph_obj_provider = (void *(*)(const yajlpp_provider_context &, void *)) provider;
        return *this;
    };

    template<typename T>
    json_path_handler &with_path_provider(void (*provider)(T *root, std::vector<std::string> &paths_out)) {
        this->jph_path_provider = (void (*)(void *, std::vector<std::string> &)) provider;
        return *this;
    }

    template<typename T, typename MEM_T, MEM_T T::*MEM>
    static void *get_field_lvalue_cb(void *root, nonstd::optional<std::string> name) {
        auto obj = (T *) root;
        auto &mem = obj->*MEM;

        return &mem;
    };

    template<typename T, typename STR_T, STR_T T::*STR>
    static int string_field_cb(yajlpp_parse_context *ypc, const unsigned char *str, size_t len) {
        auto handler = ypc->ypc_current_handler;

        if (ypc->ypc_locations) {
            intern_string_t src = intern_string::lookup(ypc->ypc_source);

            (*ypc->ypc_locations)[ypc->get_full_path()] = {
                src, ypc->get_line_number() };
        }

        assign(ypc->get_lvalue(ypc->get_obj_member<T, STR_T, STR>()), string_fragment(str, 0, len));
        handler->jph_validator(*ypc, *handler);

        return 1;
    };

    template<typename T, typename ENUM_T, ENUM_T T::*ENUM>
    static int enum_field_cb(yajlpp_parse_context *ypc, const unsigned char *str, size_t len) {
        auto obj = (T *) ypc->ypc_obj_stack.top();
        auto handler = ypc->ypc_current_handler;
        auto res = handler->to_enum_value(string_fragment(str, 0, len));

        if (res) {
            obj->*ENUM = (ENUM_T) res.value();
        } else {
            ypc->report_error(LOG_LEVEL_ERROR,
                              "error:%s:line %d\n  "
                              "Invalid value, '%.*s', for option:",
                              ypc->ypc_source.c_str(),
                              ypc->get_line_number(),
                              len,
                              str);

            ypc->report_error(LOG_LEVEL_ERROR,
                              "    %s %s -- %s\n",
                              &ypc->ypc_path[0],
                              handler->jph_synopsis,
                              handler->jph_description);
            ypc->report_error(LOG_LEVEL_ERROR,
                              "  Allowed values: ");
            for (int lpc = 0; handler->jph_enum_values[lpc].first; lpc++) {
                const json_path_handler::enum_value_t &ev = handler->jph_enum_values[lpc];

                ypc->report_error(LOG_LEVEL_ERROR, "    %s\n", ev.first);
            }
        }

        return 1;
    };

    template<typename T, bool T::*BOOL>
    static int bool_field_cb(yajlpp_parse_context *ypc, int val) {
        auto obj = (T *) ypc->ypc_obj_stack.top();

        obj->*BOOL = static_cast<bool>(val);

        return 1;
    };

    template<typename T, typename NUM_T, NUM_T T::*NUM>
    static int num_field_cb(yajlpp_parse_context *ypc, long long num)
    {
        auto obj = (T *) ypc->ypc_obj_stack.top();

        obj->*NUM = num;

        return 1;
    }

    template<typename T, typename NUM_T, NUM_T T::*NUM>
    static int decimal_field_cb(yajlpp_parse_context *ypc, double num)
    {
        auto obj = (T *) ypc->ypc_obj_stack.top();

        obj->*NUM = num;

        return 1;
    }

    template<typename T, typename STR_T, STR_T T::*STR>
    static void string_field_validator(yajlpp_parse_context &ypc, const json_path_handler_base &jph) {
        auto &field_ptr = ypc.get_rvalue(ypc.get_obj_member<T, STR_T, STR>());

        if (jph.jph_pattern) {
            string_fragment sf = to_string_fragment(field_ptr);
            pcre_input pi(sf);
            pcre_context_static<30> pc;

            if (!jph.jph_pattern->match(pc, pi)) {
                ypc.report_error(LOG_LEVEL_ERROR,
                                 "Value does not match pattern: %s",
                                 jph.jph_pattern_re);
            }
        }
        if (jph.jph_string_validator) {
            try {
                jph.jph_string_validator(to_string_fragment(field_ptr));
            } catch (const std::exception &e) {
                ypc.report_error(LOG_LEVEL_ERROR,
                                 "%s",
                                 e.what());
            }
        }
        if (field_ptr.empty() && jph.jph_min_length > 0) {
            ypc.report_error(LOG_LEVEL_ERROR, "value must not be empty");
        } else if (field_ptr.size() < jph.jph_min_length) {
            ypc.report_error(LOG_LEVEL_ERROR, "value must be at least %lu characters long",
                             jph.jph_min_length);
        }
    };

    template<typename T, typename NUM_T, NUM_T T::*NUM>
    static void number_field_validator(yajlpp_parse_context &ypc, const json_path_handler_base &jph) {
        auto &field_ptr = ypc.get_rvalue(ypc.get_obj_member<T, NUM_T, NUM>());

        if (field_ptr < jph.jph_min_value) {
            ypc.report_error(LOG_LEVEL_ERROR,
                             "value must be greater than %lld",
                             jph.jph_min_value);
        }
    }

    template<typename T, typename R, R T::*FIELD>
    static yajl_gen_status field_gen(yajlpp_gen_context &ygc,
                                     const json_path_handler_base &jph,
                                     yajl_gen handle) {
        auto def_obj = (T *) (ygc.ygc_default_stack.empty() ? nullptr : ygc.ygc_default_stack.top());
        auto obj = (T *) ygc.ygc_obj_stack.top();

        if (def_obj != nullptr && def_obj->*FIELD == obj->*FIELD) {
            return yajl_gen_status_ok;
        }

        if (ygc.ygc_depth) {
            yajl_gen_string(handle, jph.jph_path);
        }

        yajlpp_generator gen(handle);

        return gen(obj->*FIELD);
    };

    static yajl_gen_status map_field_gen(yajlpp_gen_context &ygc,
                                         const json_path_handler_base &jph,
                                         yajl_gen handle)
    {
        const auto def_obj = (std::map<std::string, std::string> *) (
            ygc.ygc_default_stack.empty() ? nullptr
                                          : ygc.ygc_default_stack.top());
        auto obj = (std::map<std::string, std::string> *) ygc.ygc_obj_stack.top();
        yajl_gen_status rc;

        for (auto &pair : *obj) {
            if (def_obj != nullptr) {
                auto iter = def_obj->find(pair.first);

                if (iter != def_obj->end() && iter->second == pair.second) {
                    continue;
                }
            }

            if ((rc = yajl_gen_string(handle, pair.first)) !=
                yajl_gen_status_ok) {
                return rc;
            }
            if ((rc = yajl_gen_string(handle, pair.second)) !=
                yajl_gen_status_ok) {
                return rc;
            }
        }

        return yajl_gen_status_ok;
    };

    template<typename T, typename STR_T, std::string T::*STR>
    json_path_handler &for_field() {
        this->add_cb(string_field_cb<T, STR_T, STR>);
        this->jph_gen_callback = field_gen<T, STR_T, STR>;
        this->jph_validator = string_field_validator<T, STR_T, STR>;
        this->jph_field_getter = get_field_lvalue_cb<T, STR_T, STR>;

        return *this;
    };

    template<typename T, typename STR_T, std::map<std::string, std::string> T::*STR>
    json_path_handler &for_field() {
        this->add_cb(string_field_cb<T, STR_T, STR>);
        this->jph_gen_callback = map_field_gen;
        this->jph_validator = string_field_validator<T, STR_T, STR>;

        return *this;
    };

    template<typename T, typename STR_T, std::map<std::string, std::vector<std::string>> T::*STR>
    json_path_handler &for_field() {
        this->add_cb(string_field_cb<T, STR_T, STR>);
        this->jph_validator = string_field_validator<T, STR_T, STR>;

        return *this;
    };

    template<typename T, typename STR_T, std::vector<std::string> T::*STR>
    json_path_handler &for_field() {
        this->add_cb(string_field_cb<T, STR_T, STR>);
        this->jph_gen_callback = field_gen<T, STR_T, STR>;
        this->jph_validator = string_field_validator<T, STR_T, STR>;

        return *this;
    };

    template<typename T, typename STR_T, intern_string_t T::*STR>
    json_path_handler &for_field() {
        this->add_cb(string_field_cb<T, intern_string_t, STR>);
        this->jph_gen_callback = field_gen<T, intern_string_t, STR>;
        this->jph_validator = string_field_validator<T, intern_string_t, STR>;

        return *this;
    };

    template<typename T, typename BOOL_T, bool T::*BOOL>
    json_path_handler &for_field() {
        this->add_cb(bool_field_cb<T, BOOL>);
        this->jph_gen_callback = field_gen<T, bool, BOOL>;

        return *this;
    };

    template<typename T, typename NUM_T, NUM_T T::*NUM>
    json_path_handler &for_field(typename std::enable_if<std::is_integral<NUM_T>::value &&
                                                         !std::is_same<NUM_T, bool>::value>::type* dummy = 0) {
        this->add_cb(num_field_cb<T, NUM_T, NUM>);
        this->jph_validator = number_field_validator<T, NUM_T, NUM>;
        return *this;
    };

    template<typename T, typename NUM_T, double T::*NUM>
    json_path_handler &for_field() {
        this->add_cb(decimal_field_cb<T, NUM_T, NUM>);
        this->jph_validator = number_field_validator<T, NUM_T, NUM>;
        return *this;
    };

    template<typename T, typename ENUM_T, ENUM_T T::*ENUM>
    json_path_handler &for_field(typename std::enable_if<std::is_enum<ENUM_T>::value>::type* dummy = 0) {
        this->add_cb(enum_field_cb<T, ENUM_T, ENUM>);
        return *this;
    };

    json_path_handler &with_children(json_path_handler *children) {
        require(this->jph_path[strlen(this->jph_path) - 1] == '/');

        this->jph_children = children;
        return *this;
    };
};

#endif
