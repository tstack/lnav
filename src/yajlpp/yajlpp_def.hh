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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file yajlpp_def.hh
 */

#ifndef yajlpp_def_hh
#define yajlpp_def_hh

#include <chrono>

#include "config.h"
#include "mapbox/variant.hpp"
#include "relative_time.hh"
#include "view_curses.hh"
#include "yajlpp.hh"

#define FOR_FIELD(T, FIELD) for_field<T, decltype(T ::FIELD), &T ::FIELD>()

inline intern_string_t&
assign(intern_string_t& lhs, const string_fragment& rhs)
{
    lhs = intern_string::lookup(rhs.data(), rhs.length());

    return lhs;
}

inline std::string&
assign(std::string& lhs, const string_fragment& rhs)
{
    lhs.assign(rhs.data(), rhs.length());

    return lhs;
}

template<template<typename...> class Container>
inline Container<std::string>&
assign(Container<std::string>& lhs, const string_fragment& rhs)
{
    lhs.emplace_back(rhs.data(), rhs.length());

    return lhs;
}

struct json_null_t {
    bool operator==(const json_null_t& other) const { return true; }
};

using json_any_t
    = mapbox::util::variant<json_null_t, bool, int64_t, double, std::string>;

struct json_path_container;

struct json_path_handler : public json_path_handler_base {
    template<typename P>
    json_path_handler(P path, int (*null_func)(yajlpp_parse_context*))
        : json_path_handler_base(path)
    {
        this->jph_callbacks.yajl_null = (int (*)(void*)) null_func;
    }

    template<typename P>
    json_path_handler(P path, int (*bool_func)(yajlpp_parse_context*, int))
        : json_path_handler_base(path)
    {
        this->jph_callbacks.yajl_boolean = (int (*)(void*, int)) bool_func;
    }

    template<typename P>
    json_path_handler(P path, int (*int_func)(yajlpp_parse_context*, long long))
        : json_path_handler_base(path)
    {
        this->jph_callbacks.yajl_integer = (int (*)(void*, long long)) int_func;
    }

    template<typename P>
    json_path_handler(P path, int (*double_func)(yajlpp_parse_context*, double))
        : json_path_handler_base(path)
    {
        this->jph_callbacks.yajl_double = (int (*)(void*, double)) double_func;
    }

    template<typename P>
    json_path_handler(P path,
                      int (*str_func)(yajlpp_parse_context*,
                                      const unsigned char*,
                                      size_t))
        : json_path_handler_base(path)
    {
        this->jph_callbacks.yajl_string
            = (int (*)(void*, const unsigned char*, size_t)) str_func;
    }

    template<typename P>
    json_path_handler(P path) : json_path_handler_base(path)
    {
    }

    json_path_handler(const std::string& path, const pcrepp& re)
        : json_path_handler_base(path, re)
    {
    }

    json_path_handler(const std::string& path,
                      const std::shared_ptr<pcrepp>& re)
        : json_path_handler_base(path, re)
    {
    }

    json_path_handler& add_cb(int (*null_func)(yajlpp_parse_context*))
    {
        this->jph_callbacks.yajl_null = (int (*)(void*)) null_func;
        return *this;
    }

    json_path_handler& add_cb(int (*bool_func)(yajlpp_parse_context*, int))
    {
        this->jph_callbacks.yajl_boolean = (int (*)(void*, int)) bool_func;
        return *this;
    }

    json_path_handler& add_cb(int (*int_func)(yajlpp_parse_context*, long long))
    {
        this->jph_callbacks.yajl_integer = (int (*)(void*, long long)) int_func;
        return *this;
    }

    json_path_handler& add_cb(int (*double_func)(yajlpp_parse_context*, double))
    {
        this->jph_callbacks.yajl_double = (int (*)(void*, double)) double_func;
        return *this;
    }

    json_path_handler& add_cb(int (*str_func)(yajlpp_parse_context*,
                                              const unsigned char*,
                                              size_t))
    {
        this->jph_callbacks.yajl_string
            = (int (*)(void*, const unsigned char*, size_t)) str_func;
        return *this;
    }

    json_path_handler& with_synopsis(const char* synopsis)
    {
        this->jph_synopsis = synopsis;
        return *this;
    }

    json_path_handler& with_description(const char* description)
    {
        this->jph_description = description;
        return *this;
    }

    json_path_handler& with_min_length(size_t len)
    {
        this->jph_min_length = len;
        return *this;
    }

    json_path_handler& with_max_length(size_t len)
    {
        this->jph_max_length = len;
        return *this;
    }

    json_path_handler& with_enum_values(const enum_value_t values[])
    {
        this->jph_enum_values = values;
        return *this;
    }

    json_path_handler& with_pattern(const char* re)
    {
        this->jph_pattern_re = re;
        this->jph_pattern = std::make_shared<pcrepp>(re);
        return *this;
    }

    json_path_handler& with_min_value(long long val)
    {
        this->jph_min_value = val;
        return *this;
    }

    template<typename R, typename T>
    json_path_handler& with_obj_provider(
        R* (*provider)(const yajlpp_provider_context& pc, T* root))
    {
        this->jph_obj_provider
            = [provider](const yajlpp_provider_context& ypc, void* root) {
                  return (R*) provider(ypc, (T*) root);
              };
        return *this;
    }

    template<typename R>
    json_path_handler& with_size_provider(size_t (*provider)(const R* root))
    {
        this->jph_size_provider
            = [provider](const void* root) { return provider((R*) root); };
        return *this;
    }

    template<typename T>
    json_path_handler& with_path_provider(
        void (*provider)(T* root, std::vector<std::string>& paths_out))
    {
        this->jph_path_provider
            = [provider](void* root, std::vector<std::string>& paths_out) {
                  provider((T*) root, paths_out);
              };
        return *this;
    }

    template<typename T>
    json_path_handler& with_obj_deleter(
        void (*provider)(const yajlpp_provider_context& pc, T* root))
    {
        this->jph_obj_deleter
            = [provider](const yajlpp_provider_context& ypc, void* root) {
                  provider(ypc, (T*) root);
              };
        return *this;
    }

    template<typename T, typename MEM_T, MEM_T T::*MEM>
    static void* get_field_lvalue_cb(void* root,
                                     nonstd::optional<std::string> name)
    {
        auto obj = (T*) root;
        auto& mem = obj->*MEM;

        return &mem;
    }

    template<typename T, typename STR_T, STR_T T::*STR>
    static int string_field_cb(yajlpp_parse_context* ypc,
                               const unsigned char* str,
                               size_t len)
    {
        auto handler = ypc->ypc_current_handler;

        if (ypc->ypc_locations) {
            (*ypc->ypc_locations)[ypc->get_full_path()]
                = source_location{ypc->ypc_source, ypc->get_line_number()};
        }

        assign(ypc->get_lvalue(ypc->get_obj_member<T, STR_T, STR>()),
               string_fragment(str, 0, len));
        handler->jph_validator(*ypc, *handler);

        return 1;
    }

    template<typename T, typename ENUM_T, ENUM_T T::*ENUM>
    static int enum_field_cb(yajlpp_parse_context* ypc,
                             const unsigned char* str,
                             size_t len)
    {
        auto obj = (T*) ypc->ypc_obj_stack.top();
        auto handler = ypc->ypc_current_handler;
        auto res = handler->to_enum_value(string_fragment(str, 0, len));

        if (res) {
            obj->*ENUM = (ENUM_T) res.value();
        } else {
            handler->report_enum_error(ypc,
                                       std::string((const char*) str, len));
        }

        return 1;
    }

    static int bool_field_cb(yajlpp_parse_context* ypc, int val)
    {
        return ypc->ypc_current_handler->jph_bool_cb(ypc, val);
    }

    static int str_field_cb2(yajlpp_parse_context* ypc,
                             const unsigned char* str,
                             size_t len)
    {
        return ypc->ypc_current_handler->jph_str_cb(ypc, str, len);
    }

    static int int_field_cb(yajlpp_parse_context* ypc, long long val)
    {
        return ypc->ypc_current_handler->jph_integer_cb(ypc, val);
    }

    template<typename T, typename NUM_T, NUM_T T::*NUM>
    static int num_field_cb(yajlpp_parse_context* ypc, long long num)
    {
        auto obj = (T*) ypc->ypc_obj_stack.top();

        obj->*NUM = num;

        return 1;
    }

    template<typename T, typename NUM_T, NUM_T T::*NUM>
    static int decimal_field_cb(yajlpp_parse_context* ypc, double num)
    {
        auto obj = (T*) ypc->ypc_obj_stack.top();

        obj->*NUM = num;

        return 1;
    }

    template<typename T, typename STR_T, STR_T T::*STR>
    static void string_field_validator(yajlpp_parse_context& ypc,
                                       const json_path_handler_base& jph)
    {
        auto& field_ptr = ypc.get_rvalue(ypc.get_obj_member<T, STR_T, STR>());

        if (jph.jph_pattern) {
            auto sf = to_string_fragment(field_ptr);
            pcre_input pi(sf);
            pcre_context_static<30> pc;

            if (!jph.jph_pattern->match(pc, pi)) {
                jph.report_pattern_error(&ypc, sf.to_string());
            }
        }
        if (field_ptr.empty() && jph.jph_min_length > 0) {
            ypc.report_error(
                lnav::console::user_message::error(
                    attr_line_t("invalid value for option ")
                        .template append_quoted(lnav::roles::symbol(
                            ypc.get_full_path().to_string())))
                    .with_reason("empty values are not allowed")
                    .with_snippet(ypc.get_snippet())
                    .with_help(jph.get_help_text(&ypc)));
        } else if (field_ptr.size() < jph.jph_min_length) {
            ypc.report_error(
                lnav::console::user_message::error(
                    attr_line_t()
                        .template append_quoted(field_ptr)
                        .append(" is not a valid value for option ")
                        .append_quoted(lnav::roles::symbol(
                            ypc.get_full_path().to_string())))
                    .with_reason(attr_line_t("value must be at least ")
                                     .append(lnav::roles::number(
                                         fmt::to_string(jph.jph_min_length)))
                                     .append(" characters long"))
                    .with_snippet(ypc.get_snippet())
                    .with_help(jph.get_help_text(&ypc)));
        }
    }

    template<typename T, typename NUM_T, NUM_T T::*NUM>
    static void number_field_validator(yajlpp_parse_context& ypc,
                                       const json_path_handler_base& jph)
    {
        auto& field_ptr = ypc.get_rvalue(ypc.get_obj_member<T, NUM_T, NUM>());

        if (field_ptr < jph.jph_min_value) {
            jph.report_min_value_error(&ypc, field_ptr);
        }
    }

    template<typename T, typename R, R T::*FIELD>
    static yajl_gen_status field_gen(yajlpp_gen_context& ygc,
                                     const json_path_handler_base& jph,
                                     yajl_gen handle)
    {
        auto def_obj = (T*) (ygc.ygc_default_stack.empty()
                                 ? nullptr
                                 : ygc.ygc_default_stack.top());
        auto obj = (T*) ygc.ygc_obj_stack.top();

        if (def_obj != nullptr && def_obj->*FIELD == obj->*FIELD) {
            return yajl_gen_status_ok;
        }

        if (ygc.ygc_depth) {
            yajl_gen_string(handle, jph.jph_property);
        }

        yajlpp_generator gen(handle);

        return gen(obj->*FIELD);
    }

    template<typename T, typename R, R T::*FIELD>
    static yajl_gen_status map_field_gen(yajlpp_gen_context& ygc,
                                         const json_path_handler_base& jph,
                                         yajl_gen handle)
    {
        const auto def_container = (T*) (ygc.ygc_default_stack.empty()
                                             ? nullptr
                                             : ygc.ygc_default_stack.top());
        auto container = (T*) ygc.ygc_obj_stack.top();
        auto& obj = container->*FIELD;
        yajl_gen_status rc;

        for (const auto& pair : obj) {
            if (def_container != nullptr) {
                auto& def_obj = def_container->*FIELD;
                auto iter = def_obj.find(pair.first);

                if (iter != def_obj.end() && iter->second == pair.second) {
                    continue;
                }
            }

            if ((rc = yajl_gen_string(handle, pair.first))
                != yajl_gen_status_ok) {
                return rc;
            }
            if ((rc = yajl_gen_string(handle, pair.second))
                != yajl_gen_status_ok) {
                return rc;
            }
        }

        return yajl_gen_status_ok;
    }

    template<typename T, typename STR_T, std::string T::*STR>
    json_path_handler& for_field()
    {
        this->add_cb(string_field_cb<T, STR_T, STR>);
        this->jph_gen_callback = field_gen<T, STR_T, STR>;
        this->jph_validator = string_field_validator<T, STR_T, STR>;
        this->jph_field_getter = get_field_lvalue_cb<T, STR_T, STR>;

        return *this;
    }

    template<typename T,
             typename STR_T,
             std::map<std::string, std::string> T::*STR>
    json_path_handler& for_field()
    {
        this->add_cb(string_field_cb<T, STR_T, STR>);
        this->jph_gen_callback = map_field_gen<T, STR_T, STR>;
        this->jph_validator = string_field_validator<T, STR_T, STR>;

        return *this;
    }

    template<typename T,
             typename STR_T,
             std::map<std::string, std::vector<std::string>> T::*STR>
    json_path_handler& for_field()
    {
        this->add_cb(string_field_cb<T, STR_T, STR>);
        this->jph_validator = string_field_validator<T, STR_T, STR>;

        return *this;
    }

    template<typename T, typename STR_T, std::vector<std::string> T::*STR>
    json_path_handler& for_field()
    {
        this->add_cb(string_field_cb<T, STR_T, STR>);
        this->jph_gen_callback = field_gen<T, STR_T, STR>;
        this->jph_validator = string_field_validator<T, STR_T, STR>;

        return *this;
    }

    template<typename T, typename STR_T, intern_string_t T::*STR>
    json_path_handler& for_field()
    {
        this->add_cb(string_field_cb<T, intern_string_t, STR>);
        this->jph_gen_callback = field_gen<T, intern_string_t, STR>;
        this->jph_validator = string_field_validator<T, intern_string_t, STR>;

        return *this;
    }

    template<typename T, typename BOOL_T, bool T::*BOOL>
    json_path_handler& for_field()
    {
        this->add_cb(bool_field_cb);
        this->jph_bool_cb = [&](yajlpp_parse_context* ypc, int val) {
            auto obj = (T*) ypc->ypc_obj_stack.top();

            obj->*BOOL = static_cast<bool>(val);

            return 1;
        };
        this->jph_gen_callback = field_gen<T, bool, BOOL>;

        return *this;
    }

    template<typename T, typename U>
    static inline U& get_field(T& input, U(T::*field))
    {
        return input.*field;
    }

    template<typename T, typename U, typename... V>
    static inline auto get_field(T& input, U(T::*field), V... args)
        -> decltype(get_field(input.*field, args...))
    {
        return get_field(input.*field, args...);
    }

    template<typename T, typename U, typename... V>
    static inline auto get_field(void* input, U(T::*field), V... args)
        -> decltype(get_field(*((T*) input), field, args...))
    {
        return get_field(*((T*) input), field, args...);
    }

    template<typename R, typename T, typename... Args>
    struct LastIs {
        static constexpr bool value = LastIs<R, Args...>::value;
    };

    template<typename R, typename T>
    struct LastIs<R, T> {
        static constexpr bool value = false;
    };

    template<typename R, typename T>
    struct LastIs<R, R T::*> {
        static constexpr bool value = true;
    };

    template<typename T, typename... Args>
    struct LastIsEnum {
        static constexpr bool value = LastIsEnum<Args...>::value;
    };

    template<typename T, typename U>
    struct LastIsEnum<U T::*> {
        static constexpr bool value = std::is_enum<U>::value;
    };

    template<typename T, typename... Args>
    struct LastIsNumber {
        static constexpr bool value = LastIsNumber<Args...>::value;
    };

    template<typename T, typename U>
    struct LastIsNumber<U T::*> {
        static constexpr bool value
            = std::is_integral<U>::value && !std::is_same<U, bool>::value;
    };

    template<typename T, typename... Args>
    struct LastIsVector {
        static constexpr bool value = LastIsVector<Args...>::value;
    };

    template<typename T, typename U>
    struct LastIsVector<std::vector<U> T::*> {
        static constexpr bool value = true;
    };

    template<typename T, typename U>
    struct LastIsVector<U T::*> {
        static constexpr bool value = false;
    };

    template<typename... Args,
             std::enable_if_t<LastIs<bool, Args...>::value, bool> = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(bool_field_cb);
        this->jph_bool_cb = [args...](yajlpp_parse_context* ypc, int val) {
            auto* obj = ypc->ypc_obj_stack.top();

            json_path_handler::get_field(obj, args...) = static_cast<bool>(val);

            return 1;
        };
        this->jph_gen_callback = [args...](yajlpp_gen_context& ygc,
                                           const json_path_handler_base& jph,
                                           yajl_gen handle) {
            const auto& field = json_path_handler::get_field(
                ygc.ygc_obj_stack.top(), args...);

            if (!ygc.ygc_default_stack.empty()) {
                const auto& field_def = json_path_handler::get_field(
                    ygc.ygc_default_stack.top(), args...);

                if (field == field_def) {
                    return yajl_gen_status_ok;
                }
            }

            if (ygc.ygc_depth) {
                yajl_gen_string(handle, jph.jph_property);
            }

            yajlpp_generator gen(handle);

            return gen(field);
        };
        this->jph_field_getter
            = [args...](void* root, nonstd::optional<std::string> name) {
                  return (void*) &json_path_handler::get_field(root, args...);
              };

        return *this;
    }

    template<typename... Args,
             std::enable_if_t<LastIsVector<Args...>::value, bool> = true>
    json_path_handler& for_field(Args... args)
    {
        this->jph_obj_provider
            = [args...](const yajlpp_provider_context& ypc, void* root) {
                  auto& vec = json_path_handler::get_field(root, args...);

                  if (ypc.ypc_index >= vec.size()) {
                      vec.resize(ypc.ypc_index + 1);
                  }

                  return &vec[ypc.ypc_index];
              };
        this->jph_size_provider = [args...](void* root) {
            auto& vec = json_path_handler::get_field(root, args...);

            return vec.size();
        };

        return *this;
    }

    template<typename... Args>
    json_path_handler& for_child(Args... args)
    {
        this->jph_obj_provider = [args...](const yajlpp_provider_context& ypc,
                                           void* root) -> void* {
            auto& child = json_path_handler::get_field(root, args...);

            return &child;
        };

        return *this;
    }

    template<typename... Args,
             std::enable_if_t<
                 LastIs<std::map<std::string, std::string>, Args...>::value,
                 bool> = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const unsigned char* str,
                                     size_t len) {
            auto obj = ypc->ypc_obj_stack.top();
            auto key = ypc->get_path_fragment(-1);

            json_path_handler::get_field(obj, args...)[key]
                = std::string((const char*) str, len);

            return 1;
        };
        this->jph_gen_callback = [args...](yajlpp_gen_context& ygc,
                                           const json_path_handler_base& jph,
                                           yajl_gen handle) {
            const auto& field = json_path_handler::get_field(
                ygc.ygc_obj_stack.top(), args...);

            if (!ygc.ygc_default_stack.empty()) {
                const auto& field_def = json_path_handler::get_field(
                    ygc.ygc_default_stack.top(), args...);

                if (field == field_def) {
                    return yajl_gen_status_ok;
                }
            }

            {
                yajlpp_generator gen(handle);

                for (const auto& pair : field) {
                    gen(pair.first);
                    gen(pair.second);
                }
            }

            return yajl_gen_status_ok;
        };
        return *this;
    }

    template<typename... Args,
             std::enable_if_t<
                 LastIs<std::map<std::string, json_any_t>, Args...>::value,
                 bool> = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(bool_field_cb);
        this->jph_bool_cb = [args...](yajlpp_parse_context* ypc, int val) {
            auto* obj = ypc->ypc_obj_stack.top();
            auto key = ypc->get_path_fragment(-1);

            json_path_handler::get_field(obj, args...)[key] = val ? true
                                                                  : false;

            return 1;
        };
        this->add_cb(int_field_cb);
        this->jph_integer_cb
            = [args...](yajlpp_parse_context* ypc, long long val) {
                  auto* obj = ypc->ypc_obj_stack.top();
                  auto key = ypc->get_path_fragment(-1);

                  json_path_handler::get_field(obj, args...)[key]
                      = static_cast<int64_t>(val);

                  return 1;
              };
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const unsigned char* str,
                                     size_t len) {
            auto* obj = ypc->ypc_obj_stack.top();
            auto key = ypc->get_path_fragment(-1);

            json_path_handler::get_field(obj, args...)[key]
                = std::string((const char*) str, len);

            return 1;
        };
        this->jph_gen_callback = [args...](yajlpp_gen_context& ygc,
                                           const json_path_handler_base& jph,
                                           yajl_gen handle) {
            const auto& field = json_path_handler::get_field(
                ygc.ygc_obj_stack.top(), args...);

            if (!ygc.ygc_default_stack.empty()) {
                const auto& field_def = json_path_handler::get_field(
                    ygc.ygc_default_stack.top(), args...);

                if (field == field_def) {
                    return yajl_gen_status_ok;
                }
            }

            {
                yajlpp_generator gen(handle);

                for (const auto& pair : field) {
                    gen(pair.first);
                    pair.second.match([&gen](json_null_t v) { gen(); },
                                      [&gen](bool v) { gen(v); },
                                      [&gen](int64_t v) { gen(v); },
                                      [&gen](double v) { gen(v); },
                                      [&gen](const std::string& v) { gen(v); });
                }
            }

            return yajl_gen_status_ok;
        };
        return *this;
    }

    template<typename... Args,
             std::enable_if_t<LastIs<std::string, Args...>::value, bool> = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const unsigned char* str,
                                     size_t len) {
            auto obj = ypc->ypc_obj_stack.top();
            auto value_str = std::string((const char*) str, len);
            auto jph = ypc->ypc_current_handler;

            if (jph->jph_pattern) {
                pcre_input pi(value_str);
                pcre_context_static<30> pc;

                if (!jph->jph_pattern->match(pc, pi)) {
                    jph->report_pattern_error(ypc, value_str);
                }
            }

            json_path_handler::get_field(obj, args...) = std::move(value_str);

            return 1;
        };
        this->jph_gen_callback = [args...](yajlpp_gen_context& ygc,
                                           const json_path_handler_base& jph,
                                           yajl_gen handle) {
            const auto& field = json_path_handler::get_field(
                ygc.ygc_obj_stack.top(), args...);

            if (!ygc.ygc_default_stack.empty()) {
                const auto& field_def = json_path_handler::get_field(
                    ygc.ygc_default_stack.top(), args...);

                if (field == field_def) {
                    return yajl_gen_status_ok;
                }
            }

            if (ygc.ygc_depth) {
                yajl_gen_string(handle, jph.jph_property);
            }

            yajlpp_generator gen(handle);

            return gen(field);
        };
        this->jph_field_getter
            = [args...](void* root, nonstd::optional<std::string> name) {
                  return (void*) &json_path_handler::get_field(root, args...);
              };
        return *this;
    }

    template<
        typename... Args,
        std::enable_if_t<LastIs<nonstd::optional<std::string>, Args...>::value,
                         bool> = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const unsigned char* str,
                                     size_t len) {
            auto obj = ypc->ypc_obj_stack.top();
            auto value_str = std::string((const char*) str, len);
            auto jph = ypc->ypc_current_handler;

            if (jph->jph_pattern) {
                pcre_input pi(value_str);
                pcre_context_static<30> pc;

                if (!jph->jph_pattern->match(pc, pi)) {
                    jph->report_pattern_error(ypc, value_str);
                }
            }

            json_path_handler::get_field(obj, args...) = std::move(value_str);

            return 1;
        };
        this->jph_gen_callback = [args...](yajlpp_gen_context& ygc,
                                           const json_path_handler_base& jph,
                                           yajl_gen handle) {
            const auto& field = json_path_handler::get_field(
                ygc.ygc_obj_stack.top(), args...);

            if (!ygc.ygc_default_stack.empty()) {
                const auto& field_def = json_path_handler::get_field(
                    ygc.ygc_default_stack.top(), args...);

                if (field == field_def) {
                    return yajl_gen_status_ok;
                }
            }

            if (!field) {
                return yajl_gen_status_ok;
            }

            if (ygc.ygc_depth) {
                yajl_gen_string(handle, jph.jph_property);
            }

            yajlpp_generator gen(handle);

            return gen(field.value());
        };
        this->jph_field_getter
            = [args...](void* root, nonstd::optional<std::string> name) {
                  return (void*) &json_path_handler::get_field(root, args...);
              };
        return *this;
    }

    template<typename... Args,
             std::enable_if_t<
                 LastIs<positioned_property<std::string>, Args...>::value,
                 bool> = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const unsigned char* str,
                                     size_t len) {
            auto obj = ypc->ypc_obj_stack.top();
            auto value_str = std::string((const char*) str, len);
            auto jph = ypc->ypc_current_handler;

            if (jph->jph_pattern) {
                pcre_input pi(value_str);
                pcre_context_static<30> pc;

                if (!jph->jph_pattern->match(pc, pi)) {
                    jph->report_pattern_error(ypc, value_str);
                }
            }

            auto& field = json_path_handler::get_field(obj, args...);

            field.pp_path = ypc->get_full_path();
            field.pp_location.sl_source = ypc->ypc_source;
            field.pp_location.sl_line_number = ypc->get_line_number();
            field.pp_value = std::move(value_str);

            return 1;
        };
        this->jph_gen_callback = [args...](yajlpp_gen_context& ygc,
                                           const json_path_handler_base& jph,
                                           yajl_gen handle) {
            const auto& field = json_path_handler::get_field(
                ygc.ygc_obj_stack.top(), args...);

            if (!ygc.ygc_default_stack.empty()) {
                const auto& field_def = json_path_handler::get_field(
                    ygc.ygc_default_stack.top(), args...);

                if (field.pp_value == field_def.pp_value) {
                    return yajl_gen_status_ok;
                }
            }

            if (ygc.ygc_depth) {
                yajl_gen_string(handle, jph.jph_property);
            }

            yajlpp_generator gen(handle);

            return gen(field.pp_value);
        };
        return *this;
    }

    template<
        typename... Args,
        std::enable_if_t<LastIs<intern_string_t, Args...>::value, bool> = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const unsigned char* str,
                                     size_t len) {
            auto obj = ypc->ypc_obj_stack.top();
            auto value_str = std::string((const char*) str, len);
            auto jph = ypc->ypc_current_handler;

            if (jph->jph_pattern) {
                pcre_input pi(value_str);
                pcre_context_static<30> pc;

                if (!jph->jph_pattern->match(pc, pi)) {
                    jph->report_pattern_error(ypc, value_str);
                }
            }

            json_path_handler::get_field(obj, args...)
                = intern_string::lookup(value_str);

            return 1;
        };
        this->jph_gen_callback = [args...](yajlpp_gen_context& ygc,
                                           const json_path_handler_base& jph,
                                           yajl_gen handle) {
            const auto& field = json_path_handler::get_field(
                ygc.ygc_obj_stack.top(), args...);

            if (!ygc.ygc_default_stack.empty()) {
                const auto& field_def = json_path_handler::get_field(
                    ygc.ygc_default_stack.top(), args...);

                if (field == field_def) {
                    return yajl_gen_status_ok;
                }
            }

            if (ygc.ygc_depth) {
                yajl_gen_string(handle, jph.jph_property);
            }

            yajlpp_generator gen(handle);

            return gen(field);
        };
        return *this;
    }

    template<typename... Args,
             std::enable_if_t<
                 LastIs<positioned_property<intern_string_t>, Args...>::value,
                 bool> = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const unsigned char* str,
                                     size_t len) {
            auto obj = ypc->ypc_obj_stack.top();
            auto value_str = std::string((const char*) str, len);
            auto jph = ypc->ypc_current_handler;

            if (jph->jph_pattern) {
                pcre_input pi(value_str);
                pcre_context_static<30> pc;

                if (!jph->jph_pattern->match(pc, pi)) {
                    jph->report_pattern_error(ypc, value_str);
                }
            }

            auto& field = json_path_handler::get_field(obj, args...);
            field.pp_path = ypc->get_full_path();
            field.pp_location.sl_source = ypc->ypc_source;
            field.pp_location.sl_line_number = ypc->get_line_number();
            field.pp_value = intern_string::lookup(value_str);

            return 1;
        };
        this->jph_gen_callback = [args...](yajlpp_gen_context& ygc,
                                           const json_path_handler_base& jph,
                                           yajl_gen handle) {
            const auto& field = json_path_handler::get_field(
                ygc.ygc_obj_stack.top(), args...);

            if (!ygc.ygc_default_stack.empty()) {
                const auto& field_def = json_path_handler::get_field(
                    ygc.ygc_default_stack.top(), args...);

                if (field.pp_value == field_def.pp_value) {
                    return yajl_gen_status_ok;
                }
            }

            if (ygc.ygc_depth) {
                yajl_gen_string(handle, jph.jph_property);
            }

            yajlpp_generator gen(handle);

            return gen(field.pp_value);
        };
        return *this;
    }

    template<typename C, typename T, typename... Args>
    json_path_handler& for_field(Args... args, std::shared_ptr<T> C::*ptr_arg)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args..., ptr_arg](yajlpp_parse_context* ypc,
                                              const unsigned char* str,
                                              size_t len) {
            auto obj = ypc->ypc_obj_stack.top();
            auto value_str = std::string((const char*) str, len);
            auto jph = ypc->ypc_current_handler;

            try {
                auto re = std::make_shared<T>(value_str);
                json_path_handler::get_field(obj, args..., ptr_arg)
                    = std::move(re);
            } catch (const pcrepp::error& e) {
                pcrepp::compile_error ce;

                ce.ce_msg = e.what();
                ce.ce_offset = e.e_offset;
                jph->report_regex_value_error(ypc, value_str, ce);
            }

            return 1;
        };
        this->jph_gen_callback
            = [args..., ptr_arg](yajlpp_gen_context& ygc,
                                 const json_path_handler_base& jph,
                                 yajl_gen handle) {
                  const auto& field = json_path_handler::get_field(
                      ygc.ygc_obj_stack.top(), args..., ptr_arg);

                  if (!ygc.ygc_default_stack.empty()) {
                      const auto& field_def = json_path_handler::get_field(
                          ygc.ygc_default_stack.top(), args..., ptr_arg);

                      if (field == field_def) {
                          return yajl_gen_status_ok;
                      }
                  }

                  if (ygc.ygc_depth) {
                      yajl_gen_string(handle, jph.jph_property);
                  }

                  yajlpp_generator gen(handle);

                  return gen(field->get_pattern());
              };
        return *this;
    }

    template<typename... Args,
             std::enable_if_t<LastIsNumber<Args...>::value, bool> = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(int_field_cb);
        this->jph_integer_cb
            = [args...](yajlpp_parse_context* ypc, long long val) {
                  auto jph = ypc->ypc_current_handler;
                  auto* obj = ypc->ypc_obj_stack.top();

                  if (val < jph->jph_min_value) {
                      jph->report_min_value_error(ypc, val);
                      return 1;
                  }

                  json_path_handler::get_field(obj, args...) = val;

                  return 1;
              };
        this->jph_gen_callback = [args...](yajlpp_gen_context& ygc,
                                           const json_path_handler_base& jph,
                                           yajl_gen handle) {
            const auto& field = json_path_handler::get_field(
                ygc.ygc_obj_stack.top(), args...);

            if (!ygc.ygc_default_stack.empty()) {
                const auto& field_def = json_path_handler::get_field(
                    ygc.ygc_default_stack.top(), args...);

                if (field == field_def) {
                    return yajl_gen_status_ok;
                }
            }

            if (ygc.ygc_depth) {
                yajl_gen_string(handle, jph.jph_property);
            }

            yajlpp_generator gen(handle);

            return gen(field);
        };
        this->jph_field_getter
            = [args...](void* root, nonstd::optional<std::string> name) {
                  return (void*) &json_path_handler::get_field(root, args...);
              };

        return *this;
    }

    template<typename... Args,
             std::enable_if_t<LastIs<std::chrono::seconds, Args...>::value,
                              bool> = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const unsigned char* str,
                                     size_t len) {
            auto obj = ypc->ypc_obj_stack.top();
            auto handler = ypc->ypc_current_handler;
            auto parse_res = relative_time::from_str((const char*) str, len);

            if (parse_res.isErr()) {
                auto parse_error = parse_res.unwrapErr();
                auto value_str = std::string((const char*) str, len);

                handler->report_duration_error(ypc, value_str, parse_error);
                return 1;
            }

            json_path_handler::get_field(obj, args...) = std::chrono::seconds(
                parse_res.template unwrap().to_timeval().tv_sec);

            return 1;
        };
        this->jph_gen_callback = [args...](yajlpp_gen_context& ygc,
                                           const json_path_handler_base& jph,
                                           yajl_gen handle) {
            const auto& field = json_path_handler::get_field(
                ygc.ygc_obj_stack.top(), args...);

            if (!ygc.ygc_default_stack.empty()) {
                const auto& field_def = json_path_handler::get_field(
                    ygc.ygc_default_stack.top(), args...);

                if (field == field_def) {
                    return yajl_gen_status_ok;
                }
            }

            if (ygc.ygc_depth) {
                yajl_gen_string(handle, jph.jph_property);
            }

            yajlpp_generator gen(handle);

            return gen(relative_time::from_timeval(
                           {static_cast<time_t>(field.count()), 0})
                           .to_string());
        };
        this->jph_field_getter
            = [args...](void* root, nonstd::optional<std::string> name) {
                  return (void*) &json_path_handler::get_field(root, args...);
              };
        return *this;
    }

    template<typename... Args,
             std::enable_if_t<LastIsEnum<Args...>::value, bool> = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const unsigned char* str,
                                     size_t len) {
            auto obj = ypc->ypc_obj_stack.top();
            auto handler = ypc->ypc_current_handler;
            auto res = handler->to_enum_value(string_fragment(str, 0, len));

            if (res) {
                json_path_handler::get_field(obj, args...)
                    = (decltype(json_path_handler::get_field(
                        obj, args...))) res.value();
            } else {
                handler->report_enum_error(ypc,
                                           std::string((const char*) str, len));
            }

            return 1;
        };
        this->jph_gen_callback = [args...](yajlpp_gen_context& ygc,
                                           const json_path_handler_base& jph,
                                           yajl_gen handle) {
            const auto& field = json_path_handler::get_field(
                ygc.ygc_obj_stack.top(), args...);

            if (!ygc.ygc_default_stack.empty()) {
                const auto& field_def = json_path_handler::get_field(
                    ygc.ygc_default_stack.top(), args...);

                if (field == field_def) {
                    return yajl_gen_status_ok;
                }
            }

            if (ygc.ygc_depth) {
                yajl_gen_string(handle, jph.jph_property);
            }

            yajlpp_generator gen(handle);

            return gen(jph.to_enum_string((int) field));
        };
        this->jph_field_getter
            = [args...](void* root, nonstd::optional<std::string> name) {
                  return (void*) &json_path_handler::get_field(root, args...);
              };

        return *this;
    };

    template<typename T, typename NUM_T, NUM_T T::*NUM>
    json_path_handler& for_field(
        typename std::enable_if<std::is_integral<NUM_T>::value
                                && !std::is_same<NUM_T, bool>::value>::type*
            dummy
        = 0)
    {
        this->add_cb(num_field_cb<T, NUM_T, NUM>);
        this->jph_validator = number_field_validator<T, NUM_T, NUM>;
        return *this;
    }

    template<typename T, typename NUM_T, double T::*NUM>
    json_path_handler& for_field()
    {
        this->add_cb(decimal_field_cb<T, NUM_T, NUM>);
        this->jph_validator = number_field_validator<T, NUM_T, NUM>;
        return *this;
    }

    template<typename T, typename ENUM_T, ENUM_T T::*ENUM>
    json_path_handler& for_field(
        typename std::enable_if<std::is_enum<ENUM_T>::value>::type* dummy = 0)
    {
        this->add_cb(enum_field_cb<T, ENUM_T, ENUM>);
        return *this;
    }

    json_path_handler& with_children(const json_path_container& container);

    json_path_handler& with_example(const std::string& example)
    {
        this->jph_examples.emplace_back(example);
        return *this;
    }
};

struct json_path_container {
    json_path_container(std::initializer_list<json_path_handler> children)
        : jpc_children(children)
    {
    }

    json_path_container& with_definition_id(const std::string& id)
    {
        this->jpc_definition_id = id;
        return *this;
    }

    json_path_container& with_schema_id(const std::string& id)
    {
        this->jpc_schema_id = id;
        return *this;
    }

    json_path_container& with_description(std::string desc)
    {
        this->jpc_description = std::move(desc);
        return *this;
    }

    void gen_schema(yajlpp_gen_context& ygc) const;

    void gen_properties(yajlpp_gen_context& ygc) const;

    std::string jpc_schema_id;
    std::string jpc_definition_id;
    std::string jpc_description;
    std::vector<json_path_handler> jpc_children;
};

template<typename T>
class yajlpp_parser {
public:
    yajlpp_parser(intern_string_t src, const json_path_container* container)
        : yp_parse_context(src, container)
    {
        this->yp_handle = yajl_alloc(&this->yp_parse_context.ypc_callbacks,
                                     nullptr,
                                     &this->yp_parse_context);
        this->yp_parse_context.with_handle(this->yp_handle);
        this->yp_parse_context.template with_obj(this->yp_obj);
        this->yp_parse_context.ypc_userdata = this;
        this->yp_parse_context.with_error_reporter(
            [](const auto& ypc, const auto& um) {
                auto* yp = static_cast<yajlpp_parser<T>*>(ypc.ypc_userdata);

                yp->yp_errors.template emplace_back(um);
            });
    }

    yajlpp_parser& with_ignore_unused(bool value)
    {
        this->yp_parse_context.with_ignore_unused(value);

        return *this;
    }

    Result<T, std::vector<lnav::console::user_message>> of(
        const string_fragment& json)
    {
        if (this->yp_parse_context.parse_doc(json)) {
            return Ok(std::move(this->yp_obj));
        }

        return Err(std::move(this->yp_errors));
    }

private:
    yajlpp_parse_context yp_parse_context;
    auto_mem<yajl_handle_t> yp_handle{yajl_free};
    std::vector<lnav::console::user_message> yp_errors;
    T yp_obj;
};

template<typename T>
struct typed_json_path_container : public json_path_container {
    typed_json_path_container(std::initializer_list<json_path_handler> children)
        : json_path_container(children)
    {
    }

    typed_json_path_container<T>& with_schema_id2(const std::string& id)
    {
        this->jpc_schema_id = id;
        return *this;
    }

    typed_json_path_container<T>& with_description2(std::string desc)
    {
        this->jpc_description = std::move(desc);
        return *this;
    }

    yajlpp_parser<T> parser_for(intern_string_t src) const
    {
        return yajlpp_parser<T>{src, this};
    }

    std::string to_string(T& obj) const
    {
        yajlpp_gen gen;
        yajlpp_gen_context ygc(gen, *this);
        ygc.template with_obj(obj);
        ygc.ygc_depth = 1;
        ygc.gen();

        return gen.to_string_fragment().to_string();
    }

    json_string to_json_string(T& obj) const
    {
        yajlpp_gen gen;
        yajlpp_gen_context ygc(gen, *this);
        ygc.template with_obj(obj);
        ygc.ygc_depth = 1;
        ygc.gen();

        return json_string{gen.get_handle()};
    }
};

namespace yajlpp {
inline json_path_handler
property_handler(const std::string& path)
{
    return {path};
}

inline json_path_handler
pattern_property_handler(const std::string& path)
{
    return {pcrepp(path)};
}
}  // namespace yajlpp

#endif
