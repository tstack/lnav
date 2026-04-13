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
#include <filesystem>

#include "base/date_time_scanner.hh"
#include "base/lnav.resolver.hh"
#include "base/map_util.hh"
#include "base/relative_time.hh"
#include "base/time_util.hh"
#include "config.h"
#include "mapbox/variant.hpp"
#include "yajlpp.hh"

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
                                      size_t,
                                      yajl_string_props_t*))
        : json_path_handler_base(path)
    {
        this->jph_callbacks.yajl_string = (int (*)(
            void*, const unsigned char*, size_t, yajl_string_props_t*))
            str_func;
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
                                              size_t,
                                              yajl_string_props_t*))
    {
        this->jph_callbacks.yajl_string = (int (*)(
            void*, const unsigned char*, size_t, yajl_string_props_t*))
            str_func;
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

    json_path_handler& with_const(string_fragment value)
    {
        this->jph_const_str = value;
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
                             size_t len,
                             yajl_string_props_t* props)
    {
        ypc->fill_in_source();
        return ypc->ypc_current_handler->jph_str_cb(
            ypc, string_fragment::from_bytes(str, len), props);
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
    static inline U& get_field(T& input, std::shared_ptr<U>(T::* field))
    {
        auto& ptr = input.*field;

        if (ptr.get() == nullptr) {
            ptr = std::make_shared<U>();
        }

        return *ptr;
    }

    template<typename T, typename U>
    static inline U& get_field(T& input, U(T::* field))
    {
        return input.*field;
    }

    template<typename T, typename U, typename... V>
    static inline auto get_field(T& input, U(T::* field), V... args)
        -> decltype(get_field(input.*field, args...))
    {
        return get_field(input.*field, args...);
    }

    template<typename T, typename U, typename... V>
    static inline auto get_field(void* input, U(T::* field), V... args)
        -> decltype(get_field(*((T*) input), field, args...))
    {
        return get_field(*((T*) input), field, args...);
    }

    // ----------------------------------------------------------------
    // Unified field-classification machinery.
    //
    // The `for_field` overload set dispatches on the last argument in
    // its parameter pack, which is always a member-pointer `U C::*`.
    // `FieldTraits<F>` does that classification once: it peels any
    // outer `std::optional<>` and `std::shared_ptr<>` from `U`, then
    // exposes both the raw pointee (`raw_type`) and the unwrapped leaf
    // (`inner_type`/`inner_kind`/`value_type`/`key_type`), along with
    // `is_optional`/`is_shared_ptr` flags.
    //
    // `FieldKind<Args...>` is the alias used by SFINAE predicates: it
    // applies `FieldTraits` to the last entry in `Args...`.
    // ----------------------------------------------------------------

    enum class field_kind {
        none,
        boolean,
        integer,
        floating,
        string,
        enumeration,
        vector,
        integer_vector,
        string_vector,
        map,
    };

    template<typename... Args>
    struct LastArg;

    template<typename T>
    struct LastArg<T> {
        using type = T;
    };

    template<typename T, typename... Rest>
    struct LastArg<T, Rest...> {
        using type = typename LastArg<Rest...>::type;
    };

    template<typename T>
    struct StripOptional {
        using type = T;
        static constexpr bool value = false;
    };
    template<typename T>
    struct StripOptional<std::optional<T>> {
        using type = T;
        static constexpr bool value = true;
    };

    template<typename T>
    struct StripSharedPtr {
        using type = T;
        static constexpr bool value = false;
    };
    template<typename T>
    struct StripSharedPtr<std::shared_ptr<T>> {
        using type = T;
        static constexpr bool value = true;
    };

    template<typename T>
    struct VectorInner {
        using type = void;
        static constexpr bool value = false;
    };
    template<typename T>
    struct VectorInner<std::vector<T>> {
        using type = T;
        static constexpr bool value = true;
    };

    template<typename T>
    struct MapInner {
        using key_type = void;
        using value_type = void;
        static constexpr bool value = false;
    };
    template<typename K, typename V>
    struct MapInner<std::map<K, V>> {
        using key_type = K;
        using value_type = V;
        static constexpr bool value = true;
    };
    template<typename K, typename V>
    struct MapInner<lnav::map::small<K, V>> {
        using key_type = K;
        using value_type = V;
        static constexpr bool value = true;
    };

    template<typename F>
    struct FieldTraits {
        using raw_type = void;
        using inner_type = void;
        using value_type = void;
        using key_type = void;
        static constexpr field_kind inner_kind = field_kind::none;
        static constexpr bool is_optional = false;
        static constexpr bool is_shared_ptr = false;
        static constexpr bool is_member_ptr = false;
    };

    template<typename C, typename U>
    struct FieldTraits<U C::*> {
    private:
        using opt = StripOptional<U>;
        using inner_opt = typename opt::type;
        using sptr = StripSharedPtr<inner_opt>;

        static constexpr field_kind compute_kind()
        {
            using inner = typename sptr::type;
            if constexpr (std::is_same_v<inner, bool>) {
                return field_kind::boolean;
            } else if constexpr (VectorInner<inner>::value) {
                using vt = typename VectorInner<inner>::type;
                if constexpr (std::is_same_v<vt, std::string>) {
                    return field_kind::string_vector;
                } else if constexpr (std::is_integral_v<
                                         vt> && !std::is_same_v<vt, bool>) {
                    return field_kind::integer_vector;
                } else {
                    return field_kind::vector;
                }
            } else if constexpr (MapInner<inner>::value) {
                return field_kind::map;
            } else if constexpr (std::is_enum_v<inner>) {
                return field_kind::enumeration;
            } else if constexpr (std::is_same_v<inner, double>) {
                return field_kind::floating;
            } else if constexpr (std::is_integral_v<
                                     inner> && !std::is_same_v<inner, bool>) {
                return field_kind::integer;
            } else if constexpr (std::is_same_v<inner, std::string>) {
                return field_kind::string;
            } else {
                return field_kind::none;
            }
        }

    public:
        // Raw pointee (with any optional<>/shared_ptr<> wrappers intact),
        // matching what `LastIs<R, ...>` historically tested.
        using raw_type = U;
        // Inner type after peeling std::optional<> and std::shared_ptr<>;
        // this is what the kind classification operates on, mirroring
        // `LastIsInteger`/`LastIsFloat`/`LastIsEnum`/`LastIsVector`/`LastIsMap`.
        using inner_type = typename sptr::type;

        static constexpr field_kind inner_kind = compute_kind();
        static constexpr bool is_optional = opt::value;
        static constexpr bool is_shared_ptr = sptr::value;
        static constexpr bool is_member_ptr = true;

        using value_type = std::conditional_t<
            VectorInner<inner_type>::value,
            typename VectorInner<inner_type>::type,
            std::conditional_t<MapInner<inner_type>::value,
                               typename MapInner<inner_type>::value_type,
                               inner_type>>;
        using key_type = typename MapInner<inner_type>::key_type;
    };

    // Convenience alias: `FieldKind<Args...>` is the FieldTraits for
    // the last (leaf) argument in the pack.
    template<typename... Args>
    using FieldKind = FieldTraits<typename LastArg<Args...>::type>;

    // Named predicates used by the `for_field` overloads' SFINAE
    // gates.  They keep the overload signatures one-liners so the
    // dispatch table is easy to scan.
    template<typename R, typename... Args>
    inline static constexpr bool raw_field_is_v
        = std::is_same_v<R, typename FieldKind<Args...>::raw_type>;

    template<field_kind Kind, typename... Args>
    inline static constexpr bool inner_kind_is_v
        = (FieldKind<Args...>::inner_kind == Kind);

    // Convenience composite predicates for the cases that need extra
    // qualifiers.
    template<typename... Args>
    inline static constexpr bool is_string_or_path_field_v
        = raw_field_is_v<std::string, Args...>
        || raw_field_is_v<std::filesystem::path, Args...>;

    template<typename... Args>
    inline static constexpr bool is_plain_vector_field_v
        = (FieldKind<Args...>::inner_kind == field_kind::vector)
        && !FieldKind<Args...>::is_optional;

    template<typename... Args>
    inline static constexpr bool is_integer_vector_field_v
        = (FieldKind<Args...>::inner_kind == field_kind::integer_vector)
        && !FieldKind<Args...>::is_optional;

    template<typename... Args>
    inline static constexpr bool is_intern_string_keyed_map_field_v
        = (FieldKind<Args...>::inner_kind == field_kind::map)
        && !FieldKind<Args...>::is_optional
        && std::is_same_v<intern_string_t,
                          typename FieldKind<Args...>::key_type>;

    template<typename... Args>
    inline static constexpr bool is_generic_string_keyed_map_field_v
        = (FieldKind<Args...>::inner_kind == field_kind::map)
        && !FieldKind<Args...>::is_optional
        && std::is_same_v<std::string,
                          typename FieldKind<Args...>::key_type>
        && !std::is_same_v<scoped_value_t,
                           typename FieldKind<Args...>::value_type>
        && !std::is_same_v<std::string,
                           typename FieldKind<Args...>::value_type>
        && !std::is_same_v<std::optional<std::string>,
                           typename FieldKind<Args...>::value_type>;

    // Build a `jph_gen_callback` lambda that handles the boilerplate
    // shared by virtually every overload below: fetch the live field,
    // skip emission if it equals the default, and (when `EmitKey` is
    // true) write the property name before delegating to `emit` for
    // the value.  Container or conditional-emit overloads that need to
    // control the key themselves pass `EmitKey = false` and use the
    // `ygc`/`jph` arguments to decide.
    template<bool EmitKey = true, typename Emit, typename... Args>
    static auto make_gen_callback(Emit emit, Args... args)
    {
        return [args..., emit = std::move(emit)](
                   yajlpp_gen_context& ygc,
                   const json_path_handler_base& jph,
                   yajl_gen handle) -> yajl_gen_status {
            const auto& field
                = json_path_handler::get_field(ygc.ygc_obj_stack.top(), args...);
            if (!ygc.ygc_default_stack.empty()) {
                const auto& field_def = json_path_handler::get_field(
                    ygc.ygc_default_stack.top(), args...);
                if (field == field_def) {
                    return yajl_gen_status_ok;
                }
            }
            if constexpr (EmitKey) {
                if (ygc.ygc_depth) {
                    yajl_gen_string(handle, jph.jph_property);
                }
            }
            return emit(field, ygc, jph, handle);
        };
    }

    // Same shape as `make_gen_callback`, but populates `jph_field_getter`
    // for the common case where the getter just hands back a pointer to
    // the field that `args...` walks to.
    template<typename... Args>
    void install_field_getter(Args... args)
    {
        this->jph_field_getter
            = [args...](void* root, std::optional<std::string>) {
                  return (void*) &json_path_handler::get_field(root, args...);
              };
    }

    // Wire up `jph_null_cb` so that an explicit JSON `null` clears an
    // `std::optional<>` field to `std::nullopt`.  Scalar overloads
    // (integer, float, enum) call this when the leaf field's traits
    // report `is_optional == true`.
    template<typename... Args>
    void install_optional_null_cb(Args... args)
    {
        this->add_cb(null_field_cb);
        this->jph_null_cb = [args...](yajlpp_parse_context* ypc) {
            auto* obj = ypc->ypc_obj_stack.top();
            json_path_handler::get_field(obj, args...) = std::nullopt;
            return 1;
        };
    }

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
             std::enable_if_t<
                 raw_field_is_v<bool, Args...>,
                 bool>
             = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(bool_field_cb);
        this->jph_bool_cb = [args...](yajlpp_parse_context* ypc, int val) {
            auto* obj = ypc->ypc_obj_stack.top();

            json_path_handler::get_field(obj, args...) = static_cast<bool>(val);

            return 1;
        };
        this->jph_gen_callback = make_gen_callback(
            [](const auto& field, const auto&, const auto&, yajl_gen handle) {
                return yajl_gen_bool(handle, field);
            },
            args...);
        install_field_getter(args...);

        return *this;
    }

    template<
        typename... Args,
        std::enable_if_t<raw_field_is_v<std::vector<std::string>, Args...>,
                         bool>
        = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const string_fragment& value_str,
                                     yajl_string_props_t* props) {
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
             std::enable_if_t<
                 is_integer_vector_field_v<Args...>,
                 bool>
             = true>
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
             std::enable_if_t<
                 is_plain_vector_field_v<Args...>,
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
    json_path_handler& for_child(positioned_property<U>(T::* field))
    {
        this->jph_obj_provider
            = [field](const yajlpp_provider_context& ypc, void* root) -> void* {
            auto& child = json_path_handler::get_field(root, field);

            if (ypc.ypc_parse_context != nullptr && child.pp_path.empty()) {
                child.pp_path = ypc.ypc_parse_context->get_full_path();
            }
            return &child.pp_value;
        };
        install_field_getter(field);

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
                 raw_field_is_v<std::map<std::string, std::string>, Args...>,
                 bool>
             = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const string_fragment& value_str,
                                     yajl_string_props_t* props) {
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
        this->jph_gen_callback = make_gen_callback<false>(
            [](const auto& field, const auto&, const auto&, yajl_gen handle) {
                yajlpp_generator gen(handle);
                for (const auto& pair : field) {
                    gen(pair.first);
                    gen(pair.second);
                }
                return yajl_gen_status_ok;
            },
            args...);
        return *this;
    }

    template<
        typename... Args,
        std::enable_if_t<
            is_intern_string_keyed_map_field_v<Args...>,
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

            if constexpr (std::is_same_v<typename FieldKind<Args...>::key_type,
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
        std::enable_if_t<
            is_generic_string_keyed_map_field_v<Args...>,
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
                 raw_field_is_v<std::map<std::string, std::optional<std::string>>, Args...>,
                 bool>
             = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const string_fragment& value_str,
                                     yajl_string_props_t*) {
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
        this->jph_gen_callback = make_gen_callback<false>(
            [](const auto& field, const auto&, const auto&, yajl_gen handle) {
                yajlpp_generator gen(handle);
                for (const auto& pair : field) {
                    gen(pair.first);
                    gen(pair.second);
                }
                return yajl_gen_status_ok;
            },
            args...);
        return *this;
    }

    template<typename... Args,
             std::enable_if_t<
                 raw_field_is_v<std::map<std::string, scoped_value_t>, Args...>,
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
                                     const string_fragment& value_str,
                                     yajl_string_props_t*) {
            auto* obj = ypc->ypc_obj_stack.top();
            auto key = ypc->get_path_fragment(-1);

            json_path_handler::get_field(obj, args...)[key]
                = value_str.to_string();

            return 1;
        };
        this->jph_gen_callback = make_gen_callback<false>(
            [](const auto& field, const auto&, const auto&, yajl_gen handle) {
                yajlpp_generator gen(handle);
                for (const auto& pair : field) {
                    gen(pair.first);
                    pair.second.match(
                        [&gen](null_value_t v) { gen(); },
                        [&gen](bool v) { gen(v); },
                        [&gen](int64_t v) { gen(v); },
                        [&gen](double v) { gen(v); },
                        [&gen](const std::string& v) { gen(v); },
                        [&](const string_fragment& v) { gen(v); });
                }
                return yajl_gen_status_ok;
            },
            args...);
        return *this;
    }

    template<
        typename... Args,
        std::enable_if_t<
            is_string_or_path_field_v<Args...>,
            bool>
        = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(null_field_cb);
        this->jph_null_cb = [args...](yajlpp_parse_context* ypc) {
            auto* obj = ypc->ypc_obj_stack.top();

            json_path_handler::get_field(obj, args...).clear();

            return 1;
        };
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const string_fragment& value_str,
                                     yajl_string_props_t*) {
            auto* obj = ypc->ypc_obj_stack.top();
            const auto* jph = ypc->ypc_current_handler;

            jph->validate_string(*ypc, value_str);
            json_path_handler::get_field(obj, args...) = value_str.to_string();

            return 1;
        };
        this->jph_gen_callback = make_gen_callback(
            [](const auto& field, const auto&, const auto&, yajl_gen handle) {
                return yajl_gen_string(handle, field);
            },
            args...);
        install_field_getter(args...);
        return *this;
    }

    template<typename... Args,
             std::enable_if_t<
                 raw_field_is_v<string_fragment, Args...>,
                 bool>
             = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [](yajlpp_parse_context* ypc,
                              const string_fragment& value_str,
                              yajl_string_props_t*) {
            const auto* jph = ypc->ypc_current_handler;

            jph->validate_string(*ypc, value_str);
            return 1;
        };
        this->jph_gen_callback = make_gen_callback(
            [](const auto& field, const auto&, const auto&, yajl_gen handle) {
                return yajl_gen_string(handle, field);
            },
            args...);
        install_field_getter(args...);
        return *this;
    }

    template<typename... Args,
             std::enable_if_t<
                 raw_field_is_v<timeval, Args...>,
                 bool>
             = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const string_fragment& value_str,
                                     yajl_string_props_t*) {
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
        this->jph_gen_callback = make_gen_callback(
            [](const auto& field, const auto&, const auto&, yajl_gen handle) {
                yajlpp_generator gen(handle);
                char buf[64];
                auto buf_len = lnav::strftime_rfc3339(
                    buf, sizeof(buf), to_us(field), 'T');
                return gen(string_fragment::from_bytes(buf, buf_len));
            },
            args...);
        install_field_getter(args...);
        return *this;
    }

    template<
        typename... Args,
        std::enable_if_t<
            raw_field_is_v<std::optional<std::string>, Args...>,
            bool>
        = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const string_fragment& value_str,
                                     yajl_string_props_t*) {
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
        // Skip emission entirely (no key, no value) when the
        // optional is empty — hence the `<false>` so we control key
        // emission ourselves.
        this->jph_gen_callback = make_gen_callback<false>(
            [](const auto& field, const auto& ygc, const auto& jph,
               yajl_gen handle) -> yajl_gen_status {
                if (!field) {
                    return yajl_gen_status_ok;
                }
                if (ygc.ygc_depth) {
                    yajl_gen_string(handle, jph.jph_property);
                }
                return yajl_gen_string(handle, field.value());
            },
            args...);
        install_field_getter(args...);
        return *this;
    }

    template<typename... Args,
             std::enable_if_t<
                 raw_field_is_v<positioned_property<std::string>, Args...>,
                 bool>
             = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const string_fragment& value_str,
                                     yajl_string_props_t*) {
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
        // positioned_property compares its `pp_value` only (not the
        // whole struct) when deciding whether to skip emission, so we
        // open-code the default check.
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
            return yajl_gen_string(handle, field.pp_value);
        };
        install_field_getter(args...);
        return *this;
    }

    template<typename... Args,
             std::enable_if_t<
                 raw_field_is_v<intern_string_t, Args...>,
                 bool>
             = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const string_fragment& value_str,
                                     yajl_string_props_t*) {
            auto* obj = ypc->ypc_obj_stack.top();
            const auto* jph = ypc->ypc_current_handler;

            jph->validate_string(*ypc, value_str);
            json_path_handler::get_field(obj, args...)
                = intern_string::lookup(value_str);

            return 1;
        };
        this->jph_gen_callback = make_gen_callback(
            [](const auto& field, const auto&, const auto&, yajl_gen handle) {
                yajlpp_generator gen(handle);
                return gen(field);
            },
            args...);
        return *this;
    }

    template<
        typename... Args,
        std::enable_if_t<
            raw_field_is_v<positioned_property<const date::time_zone*>, Args...>,
            bool>
        = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const string_fragment& value_str,
                                     yajl_string_props_t*) {
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
                 raw_field_is_v<positioned_property<intern_string_t>, Args...>,
                 bool>
             = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const string_fragment& value_str,
                                     yajl_string_props_t*) {
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
    json_path_handler& for_field(Args... args, T C::* ptr_arg)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args..., ptr_arg](yajlpp_parse_context* ypc,
                                              const string_fragment& value_frag,
                                              yajl_string_props_t*) {
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
             std::enable_if_t<
                 inner_kind_is_v<field_kind::integer, Args...>,
                 bool>
             = true>
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
        if constexpr (FieldKind<Args...>::is_optional) {
            install_optional_null_cb(args...);
        }
        this->jph_gen_callback = make_gen_callback<false>(
            [](const auto& field, const auto& ygc, const auto& jph,
               yajl_gen handle) {
                if (!is_field_set(field)) {
                    return yajl_gen_status_ok;
                }
                if (ygc.ygc_depth) {
                    yajl_gen_string(handle, jph.jph_property);
                }
                yajlpp_generator gen(handle);
                return gen(field);
            },
            args...);
        install_field_getter(args...);
        return *this;
    }

    template<typename... Args,
             std::enable_if_t<
                 inner_kind_is_v<field_kind::floating, Args...>,
                 bool>
             = true>
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
        if constexpr (FieldKind<Args...>::is_optional) {
            install_optional_null_cb(args...);
        }
        this->jph_gen_callback = make_gen_callback<false>(
            [](const auto& field, const auto& ygc, const auto& jph,
               yajl_gen handle) {
                if (!is_field_set(field)) {
                    return yajl_gen_status_ok;
                }
                if (ygc.ygc_depth) {
                    yajl_gen_string(handle, jph.jph_property);
                }
                yajlpp_generator gen(handle);
                return gen(field);
            },
            args...);
        install_field_getter(args...);
        return *this;
    }

    template<
        typename... Args,
        std::enable_if_t<raw_field_is_v<std::chrono::seconds, Args...>,
                         bool>
        = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const string_fragment& value_str,
                                     yajl_string_props_t*) {
            auto* obj = ypc->ypc_obj_stack.top();
            const auto* handler = ypc->ypc_current_handler;
            auto parse_res = relative_time::from_str(value_str);

            if (parse_res.isErr()) {
                auto parse_error = parse_res.unwrapErr();

                handler->report_duration_error(
                    ypc, value_str.to_string(), parse_error);
                return 1;
            }

            json_path_handler::get_field(obj, args...)
                = std::chrono::seconds(parse_res.unwrap().to_timeval().tv_sec);

            return 1;
        };
        this->jph_gen_callback = make_gen_callback(
            [](const auto& field, const auto&, const auto&, yajl_gen handle) {
                yajlpp_generator gen(handle);
                return gen(relative_time::from_timeval(
                               {static_cast<time_t>(field.count()), 0})
                               .to_string());
            },
            args...);
        install_field_getter(args...);
        return *this;
    }

    template<typename... Args,
             std::enable_if_t<
                 inner_kind_is_v<field_kind::enumeration, Args...>,
                 bool>
             = true>
    json_path_handler& for_field(Args... args)
    {
        this->add_cb(str_field_cb2);
        this->jph_str_cb = [args...](yajlpp_parse_context* ypc,
                                     const string_fragment& value_str,
                                     yajl_string_props_t*) {
            auto* obj = ypc->ypc_obj_stack.top();
            const auto* handler = ypc->ypc_current_handler;
            auto res = handler->to_enum_value(value_str);

            if (res) {
                json_path_handler::get_field(obj, args...)
                    = (typename FieldKind<Args...>::value_type) res.value();
            } else {
                handler->report_enum_error(ypc, value_str.to_string());
            }

            return 1;
        };
        if constexpr (FieldKind<Args...>::is_optional) {
            install_optional_null_cb(args...);
        }
        this->jph_gen_callback = make_gen_callback<false>(
            [](const auto& field, const auto& ygc, const auto& jph,
               yajl_gen handle) {
                if (!is_field_set(field)) {
                    return yajl_gen_status_ok;
                }
                if (ygc.ygc_depth) {
                    yajl_gen_string(handle, jph.jph_property);
                }
                return yajl_gen_string(handle, jph.to_enum_string(field));
            },
            args...);
        install_field_getter(args...);
        return *this;
    }

    json_path_handler& with_children(const json_path_container& container);

    json_path_handler& with_example(const string_fragment example)
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

    json_path_container& with_schema_id(string_fragment id)
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

    string_fragment jpc_schema_id;
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
        this->yp_parse_context.with_obj(this->yp_obj);
        this->yp_parse_context.ypc_userdata = this;
        this->yp_parse_context.with_error_reporter(
            [](const auto& ypc, const auto& um) {
                auto* yp = static_cast<yajlpp_parser<T>*>(ypc.ypc_userdata);

                yp->yp_errors.emplace_back(um);
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
        string_fragment_producer& json)
    {
        if (this->yp_parse_context.parse_doc(json)) {
            if (this->yp_errors.empty()) {
                return Ok(std::move(this->yp_obj));
            }
        }

        return Err(std::move(this->yp_errors));
    }

    Result<T, std::vector<lnav::console::user_message>> of(string_fragment json)
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
        ygc.with_obj(this->yf_obj);
        ygc.ygc_depth = 1;
        ygc.gen();

        return this->yf_gen.to_string_fragment().to_string();
    }
};

template<typename T>
struct typed_json_path_container : json_path_container {
    typed_json_path_container(std::initializer_list<json_path_handler> children)
        : json_path_container(children)
    {
    }

    typed_json_path_container<T>& with_schema_id2(const string_fragment id)
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
        ygc.with_obj(obj);
        ygc.ygc_depth = 1;
        ygc.gen();

        return gen.to_string_fragment().to_string();
    }

    json_string to_json_string(const T& obj) const
    {
        yajlpp_gen gen;
        yajlpp_gen_context ygc(gen, *this);
        ygc.with_obj(obj);
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
json_path_handler
pattern_property_handler(const T (&path)[N])
{
    return {lnav::pcre2pp::code::from_const(path, PCRE2_ANCHORED).to_shared()};
}

}  // namespace yajlpp

#endif
