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

#include "base/date_time_scanner.hh"
#include "base/time_util.hh"
#include "config.h"
#include "mapbox/variant.hpp"
#include "relative_time.hh"
#include "yajlpp.hh"

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
                      int (*number_func)(yajlpp_parse_context*,
                                         const char* numberVal,
                                         size_t numberLen))
        : json_path_handler_base(path)
    {
        this->jph_callbacks.yajl_number
            = (int (*)(void*, const char*, size_t)) number_func;
    }

    template<typename P>
    json_path_handler(P path) : json_path_handler_base(path)
    {
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

    json_path_handler(const std::string& path,
                      const std::shared_ptr<const lnav::pcre2pp::code>& re)
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

    json_path_handler& add_cb(int (*number_func)(yajlpp_parse_context*,
                                                 const char*,
                                                 size_t))
    {
        this->jph_callbacks.yajl_number
            = (int (*)(void*, const char*, size_t)) number_func;
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

    template<typename T, std::size_t N>
    json_path_handler& with_pattern(const T (&re)[N])
    {
        this->jph_pattern_re = re;
        this->jph_pattern = lnav::pcre2pp::code::from_const(re).to_shared();
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

    static int null_field_cb(yajlpp_parse_context* ypc)
    {
        ypc->fill_in_source();
        return ypc->ypc_current_handler->jph_null_cb(ypc);
    }

    static int bool_field_cb(yajlpp_parse_context* ypc, int val)
    {
        ypc->fill_in_source();
        return ypc->ypc_current_handler->jph_bool_cb(ypc, val);
    }

    static int str_field_cb2(yajlpp_parse_context* ypc,
                             const unsigned char* str,
                             size_t len)
    {
        ypc->fill_in_source();
        return ypc->ypc_current_handler->jph_str_cb(
            ypc, string_fragment::from_bytes(str, len));
    }

    static int int_field_cb(yajlpp_parse_context* ypc, long long val)
    {
        ypc->fill_in_source();
        return ypc->ypc_current_handler->jph_integer_cb(ypc, val);
    }

    static int dbl_field_cb(yajlpp_parse_context* ypc, double val)
    {
        ypc->fill_in_source();
        return ypc->ypc_current_handler->jph_double_cb(ypc, val);
    }

    template<typename T, typename U>
    static inline U& get_field(T& input, std::shared_ptr<U>(T::*field))
    {
        auto& ptr = input.*field;

        if (ptr.get() == nullptr) {
            ptr = std::make_shared<U>();
        }

        return *ptr;
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
        using value_type = typename LastIsEnum<Args...>::value_type;
        static constexpr bool value = LastIsEnum<Args...>::value;
    };

    template<typename T, typename U>
    struct LastIsEnum<U T::*> {
        using value_type = U;

        static constexpr bool value = std::is_enum_v<U>;
    };

    template<typename T, typename U>
    struct LastIsEnum<std::optional<U> T::*> {
        using value_type = U;

        static constexpr bool value = std::is_enum_v<U>;
    };

    template<typename T, typename... Args>
    struct LastIsInteger {
        static constexpr bool value = LastIsInteger<Args...>::value;
    };

    template<typename T, typename U>
    struct LastIsInteger<U T::*> {
        static constexpr bool value
            = std::is_integral_v<U> && !std::is_same_v<U, bool>;
    };

    template<typename T, typename U>
    struct LastIsInteger<std::optional<U> T::*> {
        static constexpr bool value
            = std::is_integral_v<U> && !std::is_same_v<U, bool>;
    };

    template<typename T, typename... Args>
    struct LastIsFloat {
        static constexpr bool value = LastIsFloat<Args...>::value;
    };

    template<typename T, typename U>
    struct LastIsFloat<U T::*> {
        static constexpr bool value = std::is_same_v<U, double>;
    };

    template<typename T, typename U>
    struct LastIsFloat<std::optional<U> T::*> {
        static constexpr bool value = std::is_same_v<U, double>;
    };

    template<typename T, typename... Args>
    struct LastIsVector {
        using value_type = typename LastIsVector<Args...>::value_type;
        static constexpr bool value = LastIsVector<Args...>::value;
    };

    template<typename T, typename U>
    struct LastIsVector<std::vector<U> T::*> {
        using value_type = U;
        static constexpr bool value = true;
    };

    template<typename T, typename U>
    struct LastIsVector<std::shared_ptr<std::vector<U>> T::*> {
        using value_type = U;
        static constexpr bool value = true;
    };

    template<typename T, typename U>
    struct LastIsVector<U T::*> {
        using value_type = void;
        static constexpr bool value = false;
    };

    template<typename T, typename... Args>
    struct LastIsIntegerVector {
        using value_type = typename LastIsIntegerVector<Args...>::value_type;
        static constexpr bool value = LastIsIntegerVector<Args...>::value;
    };

    template<typename T, typename U>
    struct LastIsIntegerVector<std::vector<U> T::*> {
        using value_type = U;
        static constexpr bool value
            = std::is_integral_v<U> && !std::is_same_v<U, bool>;
    };

    template<typename T, typename U>
    struct LastIsIntegerVector<U T::*> {
        using value_type = void;
        static constexpr bool value = false;
    };

    template<typename T, typename... Args>
    struct LastIsMap {
        static constexpr bool is_ptr = LastIsMap<Args...>::is_ptr;
        using key_type = typename LastIsMap<Args...>::key_type;
        using value_type = typename LastIsMap<Args...>::value_type;
        static constexpr bool value = LastIsMap<Args...>::value;
    };

    template<typename T, typename K, typename U>
    struct LastIsMap<std::shared_ptr<std::map<K, U>> T::*> {
        static constexpr bool is_ptr = true;
        using key_type = K;
        using value_type = U;
        static constexpr bool value = true;
    };

    template<typename T, typename K, typename U>
    struct LastIsMap<std::map<K, U> T::*> {
        static constexpr bool is_ptr = false;
        using key_type = K;
        using value_type = U;
        static constexpr bool value = true;
    };

    template<typename T, typename U>
    struct LastIsMap<U T::*> {
        static constexpr bool is_ptr = false;
        using key_type = void;
        using value_type = void;
        static constexpr bool value = false;
    };

    template<typename T>
    static bool is_field_set(const std::optional<T>& field)
    {
        return field.has_value();
    }

    template<typename T>
    static bool is_field_set(const T&)
    {
        return true;
    }

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
            = [args...](void* root, std::optional<std::string> name) {
                  return (void*) &json_path_handler::get_field(root, args...);
              };

        return *this;
    }

    template<
        typename... Args,
        std::enable_if_t<LastIs<std::vector<std::string>, Args...>::value, bool>
        = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const string_fragment& value_str) {
            auto* obj = ypc->ypc_obj_stack.top();
            const auto* jph = ypc->ypc_current_handler;

            jph->validate_string(*ypc, value_str);
            json_path_handler::get_field(obj, args...)
                .emplace_back(value_str.to_string());

            return 1;
        };
        return *this;
    }

    template<typename... Args,
             std::enable_if_t<LastIsIntegerVector<Args...>::value, bool> = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(int_field_cb);
        this->jph_integer_cb
            = [args...](yajlpp_parse_context* ypc, long long val) {
                  const auto* jph = ypc->ypc_current_handler;
                  auto* obj = ypc->ypc_obj_stack.top();

                  if (val < jph->jph_min_value) {
                      jph->report_min_value_error(ypc, val);
                      return 1;
                  }

                  json_path_handler::get_field(obj, args...).emplace_back(val);

                  return 1;
              };
        return *this;
    }

    template<typename... Args,
             std::enable_if_t<LastIsVector<Args...>::value, bool> = true,
             std::enable_if_t<
                 !std::is_same_v<typename LastIsVector<Args...>::value_type,
                                 std::string>
                     && !LastIsIntegerVector<Args...>::value,
                 bool>
             = true>
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

    template<typename T, typename U>
    json_path_handler& for_child(positioned_property<U>(T::*field))
    {
        this->jph_obj_provider
            = [field](const yajlpp_provider_context& ypc, void* root) -> void* {
            auto& child = json_path_handler::get_field(root, field);

            if (ypc.ypc_parse_context != nullptr && child.pp_path.empty()) {
                child.pp_path = ypc.ypc_parse_context->get_full_path();
            }
            return &child.pp_value;
        };
        this->jph_field_getter
            = [field](void* root, std::optional<std::string> name) {
                  return (void*) &json_path_handler::get_field(root, field);
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
                 bool>
             = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const string_fragment& value_str) {
            auto* obj = ypc->ypc_obj_stack.top();
            auto key = ypc->get_path_fragment(-1);

            json_path_handler::get_field(obj, args...)[key]
                = value_str.to_string();

            return 1;
        };
        this->jph_path_provider =
            [args...](void* root, std::vector<std::string>& paths_out) {
                const auto& field = json_path_handler::get_field(root, args...);

                for (const auto& pair : field) {
                    paths_out.emplace_back(pair.first);
                }
            };
        this->jph_field_getter
            = [args...](void* root,
                        std::optional<std::string> name) -> const void* {
            const auto& field = json_path_handler::get_field(root, args...);
            if (!name) {
                return &field;
            }

            auto iter = field.find(name.value());
            if (iter == field.end()) {
                return nullptr;
            }
            return (void*) &iter->second;
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

    template<
        typename... Args,
        std::enable_if_t<LastIsMap<Args...>::value, bool> = true,
        std::enable_if_t<std::is_same_v<intern_string_t,
                                        typename LastIsMap<Args...>::key_type>,
                         bool>
        = true>
    json_path_handler& for_field(Args... args)
    {
        this->jph_path_provider =
            [args...](void* root, std::vector<std::string>& paths_out) {
                const auto& field = json_path_handler::get_field(root, args...);

                for (const auto& pair : field) {
                    paths_out.emplace_back(std::to_string(pair.first));
                }
            };
        this->jph_field_getter
            = [args...](void* root,
                        std::optional<std::string> name) -> const void* {
            const auto& field = json_path_handler::get_field(root, args...);
            if (!name) {
                return &field;
            }

            if constexpr (std::is_same_v<typename LastIsMap<Args...>::key_type,
                                         intern_string_t>)
            {
                auto iter = field.find(intern_string::lookup(name.value()));
                if (iter == field.end()) {
                    return nullptr;
                }
                return (void*) &iter->second;
            } else {
                auto iter = field.find(name.value());
                if (iter == field.end()) {
                    return nullptr;
                }
                return (void*) &iter->second;
            }
        };
        this->jph_obj_provider
            = [args...](const yajlpp_provider_context& ypc, void* root) {
                  auto& field = json_path_handler::get_field(root, args...);

                  return &(field[ypc.get_substr_i(0)]);
              };
        return *this;
    }

    template<
        typename... Args,
        std::enable_if_t<LastIsMap<Args...>::value, bool> = true,
        std::enable_if_t<
            std::is_same_v<std::string, typename LastIsMap<Args...>::key_type>,
            bool>
        = true,
        std::enable_if_t<
            !std::is_same_v<json_any_t, typename LastIsMap<Args...>::value_type>
                && !std::is_same_v<std::string,
                                   typename LastIsMap<Args...>::value_type>
                && !std::is_same_v<std::optional<std::string>,
                                   typename LastIsMap<Args...>::value_type>,
            bool>
        = true>
    json_path_handler& for_field(Args... args)
    {
        this->jph_path_provider =
            [args...](void* root, std::vector<std::string>& paths_out) {
                const auto& field = json_path_handler::get_field(root, args...);

                for (const auto& pair : field) {
                    paths_out.emplace_back(pair.first);
                }
            };
        this->jph_obj_provider
            = [args...](const yajlpp_provider_context& ypc, void* root) {
                  auto& field = json_path_handler::get_field(root, args...);
                  auto key = ypc.get_substr(0);

                  return &(field[key]);
              };
        return *this;
    }

    template<typename... Args,
             std::enable_if_t<
                 LastIs<std::map<std::string, std::optional<std::string>>,
                        Args...>::value,
                 bool>
             = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const string_fragment& value_str) {
            auto obj = ypc->ypc_obj_stack.top();
            auto key = ypc->get_path_fragment(-1);

            json_path_handler::get_field(obj, args...)[key]
                = value_str.to_string();

            return 1;
        };
        this->add_cb(null_field_cb);
        this->jph_null_cb = [args...](yajlpp_parse_context* ypc) {
            auto* obj = ypc->ypc_obj_stack.top();
            auto key = ypc->get_path_fragment(-1);

            json_path_handler::get_field(obj, args...)[key] = std::nullopt;

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
                 bool>
             = true>
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
                                     const string_fragment& value_str) {
            auto* obj = ypc->ypc_obj_stack.top();
            auto key = ypc->get_path_fragment(-1);

            json_path_handler::get_field(obj, args...)[key]
                = value_str.to_string();

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
                                     const string_fragment& value_str) {
            auto* obj = ypc->ypc_obj_stack.top();
            const auto* jph = ypc->ypc_current_handler;

            jph->validate_string(*ypc, value_str);
            json_path_handler::get_field(obj, args...) = value_str.to_string();

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
            = [args...](void* root, std::optional<std::string> name) {
                  return (void*) &json_path_handler::get_field(root, args...);
              };
        return *this;
    }

    template<typename... Args,
             std::enable_if_t<LastIs<timeval, Args...>::value, bool> = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const string_fragment& value_str) {
            auto* obj = ypc->ypc_obj_stack.top();
            const auto* jph = ypc->ypc_current_handler;

            date_time_scanner dts;
            timeval tv{};
            exttm tm;

            if (dts.scan(value_str.data(), value_str.length(), nullptr, &tm, tv)
                == nullptr)
            {
                ypc->report_error(lnav::console::user_message::error(
                                      attr_line_t("unrecognized timestamp ")
                                          .append_quoted(value_str))
                                      .with_snippet(ypc->get_snippet())
                                      .with_help(jph->get_help_text(ypc)));
            } else {
                json_path_handler::get_field(obj, args...) = tv;
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
            char buf[64];

            auto buf_len = lnav::strftime_rfc3339(
                buf, sizeof(buf), field.tv_sec, field.tv_usec, 'T');

            return gen(string_fragment::from_bytes(buf, buf_len));
        };
        this->jph_field_getter
            = [args...](void* root, std::optional<std::string> name) {
                  return (void*) &json_path_handler::get_field(root, args...);
              };
        return *this;
    }

    template<
        typename... Args,
        std::enable_if_t<LastIs<std::optional<std::string>, Args...>::value,
                         bool>
        = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const string_fragment& value_str) {
            auto* obj = ypc->ypc_obj_stack.top();
            const auto* jph = ypc->ypc_current_handler;

            jph->validate_string(*ypc, value_str);
            json_path_handler::get_field(obj, args...) = value_str.to_string();

            return 1;
        };
        this->add_cb(null_field_cb);
        this->jph_null_cb = [args...](yajlpp_parse_context* ypc) {
            auto* obj = ypc->ypc_obj_stack.top();

            json_path_handler::get_field(obj, args...) = std::nullopt;

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
            = [args...](void* root, std::optional<std::string> name) {
                  return (void*) &json_path_handler::get_field(root, args...);
              };
        return *this;
    }

    template<typename... Args,
             std::enable_if_t<
                 LastIs<positioned_property<std::string>, Args...>::value,
                 bool>
             = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const string_fragment& value_str) {
            auto* obj = ypc->ypc_obj_stack.top();
            const auto* jph = ypc->ypc_current_handler;

            jph->validate_string(*ypc, value_str);
            auto& field = json_path_handler::get_field(obj, args...);

            field.pp_path = ypc->get_full_path();
            field.pp_location.sl_source = ypc->ypc_source;
            field.pp_location.sl_line_number = ypc->get_line_number();
            field.pp_value = value_str.to_string();

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
        this->jph_field_getter
            = [args...](void* root, std::optional<std::string> name) {
                  return (void*) &json_path_handler::get_field(root, args...);
              };
        return *this;
    }

    template<typename... Args,
             std::enable_if_t<LastIs<intern_string_t, Args...>::value, bool>
             = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const string_fragment& value_str) {
            auto* obj = ypc->ypc_obj_stack.top();
            const auto* jph = ypc->ypc_current_handler;

            jph->validate_string(*ypc, value_str);
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

    template<
        typename... Args,
        std::enable_if_t<
            LastIs<positioned_property<const date::time_zone*>, Args...>::value,
            bool>
        = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const string_fragment& value_str) {
            auto* obj = ypc->ypc_obj_stack.top();
            const auto* jph = ypc->ypc_current_handler;

            try {
                const auto* tz
                    = date::get_tzdb().locate_zone(value_str.to_string());
                auto& field = json_path_handler::get_field(obj, args...);
                field.pp_path = ypc->get_full_path();
                field.pp_location.sl_source = ypc->ypc_source;
                field.pp_location.sl_line_number = ypc->get_line_number();
                field.pp_value = tz;
            } catch (const std::runtime_error& e) {
                jph->report_tz_error(ypc, value_str.to_string(), e.what());
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

                if (field.pp_value == field_def.pp_value) {
                    return yajl_gen_status_ok;
                }
            }

            if (ygc.ygc_depth) {
                yajl_gen_string(handle, jph.jph_property);
            }

            yajlpp_generator gen(handle);

            return gen(field.pp_value->name());
        };
        return *this;
    }

    template<typename... Args,
             std::enable_if_t<
                 LastIs<positioned_property<intern_string_t>, Args...>::value,
                 bool>
             = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const string_fragment& value_str) {
            auto* obj = ypc->ypc_obj_stack.top();
            const auto* jph = ypc->ypc_current_handler;

            jph->validate_string(*ypc, value_str);
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

    template<typename>
    struct int_ {
        using type = int;
    };
    template<
        typename C,
        typename T,
        typename int_<decltype(T::from(
            intern_string_t{}, source_location{}, string_fragment{}))>::type
        = 0,
        typename... Args>
    json_path_handler& for_field(Args... args, T C::*ptr_arg)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args..., ptr_arg](
                               yajlpp_parse_context* ypc,
                               const string_fragment& value_frag) {
            auto* obj = ypc->ypc_obj_stack.top();
            const auto* jph = ypc->ypc_current_handler;
            auto loc = source_location{ypc->ypc_source, ypc->get_line_number()};

            auto from_res = T::from(ypc->get_full_path(), loc, value_frag);
            if (from_res.isErr()) {
                jph->report_error(
                    ypc, value_frag.to_string(), from_res.unwrapErr());
            } else {
                json_path_handler::get_field(obj, args..., ptr_arg)
                    = from_res.unwrap();
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

                      if (field.pp_value == field_def.pp_value) {
                          return yajl_gen_status_ok;
                      }
                  }

                  if (ygc.ygc_depth) {
                      yajl_gen_string(handle, jph.jph_property);
                  }

                  yajlpp_generator gen(handle);

                  return gen(field.to_string());
              };
        this->jph_field_getter
            = [args..., ptr_arg](void* root, std::optional<std::string> name) {
                  return &json_path_handler::get_field(root, args..., ptr_arg);
              };
        return *this;
    }

    template<typename... Args,
             std::enable_if_t<LastIsInteger<Args...>::value, bool> = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(int_field_cb);
        this->jph_integer_cb
            = [args...](yajlpp_parse_context* ypc, long long val) {
                  const auto* jph = ypc->ypc_current_handler;
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

            if (!is_field_set(field)) {
                return yajl_gen_status_ok;
            }

            if (ygc.ygc_depth) {
                yajl_gen_string(handle, jph.jph_property);
            }

            yajlpp_generator gen(handle);

            return gen(field);
        };
        this->jph_field_getter
            = [args...](void* root, std::optional<std::string> name) {
                  return (void*) &json_path_handler::get_field(root, args...);
              };

        return *this;
    }

    template<typename... Args,
             std::enable_if_t<LastIsFloat<Args...>::value, bool> = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(dbl_field_cb);
        this->jph_double_cb = [args...](yajlpp_parse_context* ypc, double val) {
            const auto* jph = ypc->ypc_current_handler;
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

            if (!is_field_set(field)) {
                return yajl_gen_status_ok;
            }

            if (ygc.ygc_depth) {
                yajl_gen_string(handle, jph.jph_property);
            }

            yajlpp_generator gen(handle);

            return gen(field);
        };
        this->jph_field_getter
            = [args...](void* root, std::optional<std::string> name) {
                  return (void*) &json_path_handler::get_field(root, args...);
              };

        return *this;
    }

    template<
        typename... Args,
        std::enable_if_t<LastIs<std::chrono::seconds, Args...>::value, bool>
        = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const string_fragment& value_str) {
            auto* obj = ypc->ypc_obj_stack.top();
            const auto* handler = ypc->ypc_current_handler;
            auto parse_res = relative_time::from_str(value_str);

            if (parse_res.isErr()) {
                auto parse_error = parse_res.unwrapErr();

                handler->report_duration_error(
                    ypc, value_str.to_string(), parse_error);
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
            = [args...](void* root, std::optional<std::string> name) {
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
                                     const string_fragment& value_str) {
            auto* obj = ypc->ypc_obj_stack.top();
            const auto* handler = ypc->ypc_current_handler;
            auto res = handler->to_enum_value(value_str);

            if (res) {
                json_path_handler::get_field(obj, args...)
                    = (typename LastIsEnum<Args...>::value_type) res.value();
            } else {
                handler->report_enum_error(ypc, value_str.to_string());
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

            if (!is_field_set(field)) {
                return yajl_gen_status_ok;
            }

            if (ygc.ygc_depth) {
                yajl_gen_string(handle, jph.jph_property);
            }

            yajlpp_generator gen(handle);

            return gen(jph.to_enum_string(field));
        };
        this->jph_field_getter
            = [args...](void* root, std::optional<std::string> name) {
                  return (void*) &json_path_handler::get_field(root, args...);
              };

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

    Result<void, std::vector<lnav::console::user_message>> consume(
        const string_fragment& json)
    {
        if (this->yp_parse_context.parse(json) == yajl_status_ok) {
            if (this->yp_errors.empty()) {
                return Ok();
            }
        }

        return Err(std::move(this->yp_errors));
    }

    Result<T, std::vector<lnav::console::user_message>> complete()
    {
        if (this->yp_parse_context.complete_parse() == yajl_status_ok) {
            return Ok(std::move(this->yp_obj));
        }

        return Err(std::move(this->yp_errors));
    }

    Result<T, std::vector<lnav::console::user_message>> of(
        const string_fragment& json)
    {
        if (this->yp_parse_context.parse_doc(json)) {
            if (this->yp_errors.empty()) {
                return Ok(std::move(this->yp_obj));
            }
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
struct typed_json_path_container;

template<typename T>
struct yajlpp_formatter {
    const T& yf_obj;
    const typed_json_path_container<T>& yf_container;
    yajlpp_gen yf_gen;

    template<typename... Args>
    yajlpp_formatter<T> with_config(Args... args) &&
    {
        yajl_gen_config(this->yf_gen.get_handle(), args...);

        return std::move(*this);
    }

    std::string to_string() &&
    {
        yajlpp_gen_context ygc(this->yf_gen, this->yf_container);
        ygc.template with_obj(this->yf_obj);
        ygc.ygc_depth = 1;
        ygc.gen();

        return this->yf_gen.to_string_fragment().to_string();
    }
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

    yajlpp_formatter<T> formatter_for(const T& obj) const
    {
        return yajlpp_formatter<T>{obj, *this};
    }

    std::string to_string(const T& obj) const
    {
        yajlpp_gen gen;
        yajlpp_gen_context ygc(gen, *this);
        ygc.template with_obj(obj);
        ygc.ygc_depth = 1;
        ygc.gen();

        return gen.to_string_fragment().to_string();
    }

    json_string to_json_string(const T& obj) const
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

template<typename T, std::size_t N>
inline json_path_handler
pattern_property_handler(const T (&path)[N])
{
    return {lnav::pcre2pp::code::from_const(path, PCRE2_ANCHORED).to_shared()};
}

}  // namespace yajlpp

#endif
