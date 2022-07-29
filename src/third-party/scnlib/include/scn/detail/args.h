// Copyright 2017 Elias Kosunen
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// This file is a part of scnlib:
//     https://github.com/eliaskosunen/scnlib

#ifndef SCN_DETAIL_ARGS_H
#define SCN_DETAIL_ARGS_H

#include "../reader/common.h"
#include "../util/array.h"

SCN_GCC_PUSH
SCN_GCC_IGNORE("-Wnoexcept")
#include <string>
SCN_GCC_POP

namespace scn {
    SCN_BEGIN_NAMESPACE

    /**
     * Allows reading an rvalue.
     * Stores an rvalue and returns an lvalue reference to it via `operator()`.
     * Create one with \ref temp.
     */
    template <typename T>
    struct temporary {
        temporary(T&& val) : value(SCN_MOVE(val)) {}

        T& operator()() && noexcept
        {
            return value;
        }

        T value;
    };
    /**
     * Factory function for \ref temporary.
     *
     * Canonical use case is with \ref scn::span:
     * \code{.cpp}
     * std::vector<char> buffer(32, '\0');
     * auto result = scn::scan("123", "{}", scn::temp(scn::make_span(buffer)));
     * // buffer == "123"
     * \endcode
     */
    template <typename T,
              typename std::enable_if<
                  !std::is_lvalue_reference<T>::value>::type* = nullptr>
    temporary<T> temp(T&& val)
    {
        return {SCN_FWD(val)};
    }

    namespace detail {
        enum type {
            none_type = 0,
            // signed integer
            schar_type,
            short_type,
            int_type,
            long_type,
            long_long_type,
            // unsigned integer
            uchar_type,
            ushort_type,
            uint_type,
            ulong_type,
            ulong_long_type,
            // other integral types
            bool_type,
            char_type,
            code_point_type,
            last_integer_type = code_point_type,
            // floats
            float_type,
            double_type,
            long_double_type,
            last_numeric_type = long_double_type,
            // other
            buffer_type,
            string_type,
            string_view_type,

            custom_type,
            last_type = custom_type
        };

        constexpr bool is_integral(type t) noexcept
        {
            return t > none_type && t <= last_integer_type;
        }
        constexpr bool is_arithmetic(type t) noexcept
        {
            return t > none_type && t <= last_numeric_type;
        }

        struct custom_value {
            // using scan_type = error (*)(void*, Context&, ParseCtx&);

            void* value;
            void (*scan)();
        };

        template <typename Context, typename ParseCtx, typename T>
        error scan_custom_arg(void* arg, Context& ctx, ParseCtx& pctx) noexcept
        {
            return visitor_boilerplate<scanner<T>>(*static_cast<T*>(arg), ctx,
                                                   pctx);
        }

        struct monostate {
        };

        template <typename Ctx>
        struct ctx_tag {
        };
        template <typename ParseCtx>
        struct parse_ctx_tag {
        };

        class value {
        public:
            constexpr value() noexcept : m_empty{} {}

            template <typename T>
            explicit SCN_CONSTEXPR14 value(T& val) noexcept
                : m_value(std::addressof(val))
            {
            }

            template <typename Ctx, typename ParseCtx, typename T>
            value(ctx_tag<Ctx>, parse_ctx_tag<ParseCtx>, T& val) noexcept
                : m_custom(
                      custom_value{std::addressof(val),
                                   reinterpret_cast<void (*)()>(
                                       &scan_custom_arg<Ctx, ParseCtx, T>)})
            {
            }

            template <typename T>
            SCN_CONSTEXPR14 T& get_as() noexcept
            {
                return *static_cast<T*>(m_value);
            }
            template <typename T>
            constexpr const T& get_as() const noexcept
            {
                return *static_cast<const T*>(m_value);
            }

            SCN_CONSTEXPR14 custom_value& get_custom() noexcept
            {
                return m_custom;
            }
            SCN_NODISCARD constexpr const custom_value& get_custom()
                const noexcept
            {
                return m_custom;
            }

        private:
            union {
                monostate m_empty;
                void* m_value;
                custom_value m_custom;
            };
        };

        template <typename CharT, typename T, type Type>
        struct init {
            T* val;
            static const type type_tag = Type;

            constexpr init(T& v) : val(std::addressof(v)) {}
            template <typename Ctx, typename ParseCtx>
            SCN_CONSTEXPR14 value get()
            {
                SCN_EXPECT(val != nullptr);
                return value{*val};
            }
        };
        template <typename CharT, typename T>
        struct init<CharT, T, custom_type> {
            T* val;
            static const type type_tag = custom_type;

            constexpr init(T& v) : val(std::addressof(v)) {}
            template <typename Ctx, typename ParseCtx>
            SCN_CONSTEXPR14 value get()
            {
                SCN_EXPECT(val != nullptr);
                return {ctx_tag<Ctx>{}, parse_ctx_tag<ParseCtx>{}, *val};
            }
        };

        template <typename Context,
                  typename ParseCtx,
                  typename T,
                  typename CharT = typename Context::char_type>
        SCN_CONSTEXPR14 basic_arg<CharT> make_arg(T& value) noexcept;

#define SCN_MAKE_VALUE(Tag, Type)                                         \
    template <typename CharT>                                             \
    constexpr init<CharT, Type, Tag> make_value(Type& val,                \
                                                priority_tag<1>) noexcept \
    {                                                                     \
        return val;                                                       \
    }

        SCN_MAKE_VALUE(schar_type, signed char)
        SCN_MAKE_VALUE(short_type, short)
        SCN_MAKE_VALUE(int_type, int)
        SCN_MAKE_VALUE(long_type, long)
        SCN_MAKE_VALUE(long_long_type, long long)

        SCN_MAKE_VALUE(uchar_type, unsigned char)
        SCN_MAKE_VALUE(ushort_type, unsigned short)
        SCN_MAKE_VALUE(uint_type, unsigned)
        SCN_MAKE_VALUE(ulong_type, unsigned long)
        SCN_MAKE_VALUE(ulong_long_type, unsigned long long)

        SCN_MAKE_VALUE(bool_type, bool)
        SCN_MAKE_VALUE(code_point_type, code_point)

        SCN_MAKE_VALUE(float_type, float)
        SCN_MAKE_VALUE(double_type, double)
        SCN_MAKE_VALUE(long_double_type, long double)

        SCN_MAKE_VALUE(buffer_type, span<CharT>)
        SCN_MAKE_VALUE(string_type, std::basic_string<CharT>)
        SCN_MAKE_VALUE(string_view_type, basic_string_view<CharT>)

        template <typename CharT>
        constexpr init<CharT, CharT, char_type> make_value(
            CharT& val,
            priority_tag<1>) noexcept
        {
            return val;
        }

        template <typename CharT, typename T>
        constexpr inline auto make_value(T& val, priority_tag<0>) noexcept
            -> init<CharT, T, custom_type>
        {
            return val;
        }

        enum : size_t {
            packed_arg_bitsize = 5,
            packed_arg_mask = (1 << packed_arg_bitsize) - 1,
            max_packed_args = (sizeof(size_t) * 8 - 1) / packed_arg_bitsize,
            is_unpacked_bit = size_t{1} << (sizeof(size_t) * 8ull - 1ull)
        };
    }  // namespace detail

    SCN_CLANG_PUSH
    SCN_CLANG_IGNORE("-Wpadded")

    /// Type-erased scanning argument.
    template <typename CharT>
    class SCN_TRIVIAL_ABI basic_arg {
    public:
        using char_type = CharT;

        class handle {
        public:
            explicit handle(detail::custom_value custom) : m_custom(custom) {}

            template <typename Context, typename ParseCtx>
            error scan(Context& ctx, ParseCtx& pctx)
            {
                return reinterpret_cast<error (*)(void*, Context&, ParseCtx&)>(
                    m_custom.scan)(m_custom.value, ctx, pctx);
            }

        private:
            detail::custom_value m_custom;
        };

        constexpr basic_arg() = default;

        constexpr explicit operator bool() const noexcept
        {
            return m_type != detail::none_type;
        }

        SCN_NODISCARD constexpr detail::type type() const noexcept
        {
            return type;
        }
        SCN_NODISCARD constexpr bool is_integral() const noexcept
        {
            return detail::is_integral(m_type);
        }
        SCN_NODISCARD constexpr bool is_arithmetic() const noexcept
        {
            return detail::is_arithmetic(m_type);
        }

    private:
        constexpr basic_arg(detail::value v, detail::type t) noexcept
            : m_value(v), m_type(t)
        {
        }

        template <typename Ctx, typename ParseCtx, typename T, typename C>
        friend SCN_CONSTEXPR14 basic_arg<C> detail::make_arg(T& value) noexcept;

        template <typename C, typename Visitor>
        friend SCN_CONSTEXPR14 error visit_arg(Visitor&& vis,
                                               basic_arg<C>& arg);

        friend class basic_args<CharT>;

        detail::value m_value;
        detail::type m_type{detail::none_type};
    };

    SCN_CLANG_POP

    template <typename CharT, typename Visitor>
    SCN_CONSTEXPR14 error visit_arg(Visitor&& vis, basic_arg<CharT>& arg)
    {
        switch (arg.m_type) {
            case detail::none_type:
                break;

            case detail::schar_type:
                return vis(arg.m_value.template get_as<signed char>());
            case detail::short_type:
                return vis(arg.m_value.template get_as<short>());
            case detail::int_type:
                return vis(arg.m_value.template get_as<int>());
            case detail::long_type:
                return vis(arg.m_value.template get_as<long>());
            case detail::long_long_type:
                return vis(arg.m_value.template get_as<long long>());

            case detail::uchar_type:
                return vis(arg.m_value.template get_as<unsigned char>());
            case detail::ushort_type:
                return vis(arg.m_value.template get_as<unsigned short>());
            case detail::uint_type:
                return vis(arg.m_value.template get_as<unsigned int>());
            case detail::ulong_type:
                return vis(arg.m_value.template get_as<unsigned long>());
            case detail::ulong_long_type:
                return vis(arg.m_value.template get_as<unsigned long long>());

            case detail::bool_type:
                return vis(arg.m_value.template get_as<bool>());
            case detail::char_type:
                return vis(arg.m_value.template get_as<CharT>());
            case detail::code_point_type:
                return vis(arg.m_value.template get_as<code_point>());

            case detail::float_type:
                return vis(arg.m_value.template get_as<float>());
            case detail::double_type:
                return vis(arg.m_value.template get_as<double>());
            case detail::long_double_type:
                return vis(arg.m_value.template get_as<long double>());

            case detail::buffer_type:
                return vis(arg.m_value.template get_as<span<CharT>>());
            case detail::string_type:
                return vis(
                    arg.m_value.template get_as<std::basic_string<CharT>>());
            case detail::string_view_type:
                return vis(
                    arg.m_value.template get_as<basic_string_view<CharT>>());

            case detail::custom_type:
                return vis(typename basic_arg<CharT>::handle(
                    arg.m_value.get_custom()));

                SCN_CLANG_PUSH
                SCN_CLANG_IGNORE("-Wcovered-switch-default")
            default:
                return vis(detail::monostate{});
                SCN_CLANG_POP
        }
        SCN_UNREACHABLE;
    }

    namespace detail {
        template <typename CharT, typename T>
        struct get_type {
            using value_type = decltype(make_value<CharT>(
                SCN_DECLVAL(typename std::remove_reference<
                            typename std::remove_cv<T>::type>::type&),
                SCN_DECLVAL(priority_tag<1>)));
            static const type value = value_type::type_tag;
        };

        template <typename CharT>
        constexpr size_t get_types()
        {
            return 0;
        }
        template <typename CharT, typename Arg, typename... Args>
        constexpr size_t get_types()
        {
            return static_cast<size_t>(get_type<CharT, Arg>::value) |
                   (get_types<CharT, Args...>() << 5);
        }

        template <typename Context,
                  typename ParseCtx,
                  typename T,
                  typename CharT>
        SCN_CONSTEXPR14 basic_arg<CharT> make_arg(T& value) noexcept
        {
            basic_arg<CharT> arg;
            arg.m_type = get_type<CharT, T>::value;
            arg.m_value = make_value<CharT>(value, priority_tag<1>{})
                              .template get<Context, ParseCtx>();
            return arg;
        }

        template <bool Packed,
                  typename Context,
                  typename ParseCtx,
                  typename T,
                  typename CharT = typename Context::char_type>
        inline auto make_arg(T& v) ->
            typename std::enable_if<Packed, value>::type
        {
            return make_value<CharT>(v, priority_tag<1>{})
                .template get<Context, ParseCtx>();
        }
        template <bool Packed, typename Context, typename ParseCtx, typename T>
        inline auto make_arg(T& v) -> typename std::
            enable_if<!Packed, basic_arg<typename Context::char_type>>::type
        {
            return make_arg<Context, ParseCtx>(v);
        }
    }  // namespace detail

    template <typename CharT, typename... Args>
    class arg_store {
        static constexpr const size_t num_args = sizeof...(Args);
        static const bool is_packed = num_args < detail::max_packed_args;

        friend class basic_args<CharT>;

        static constexpr size_t get_types()
        {
            return is_packed ? detail::get_types<CharT, Args...>()
                             : detail::is_unpacked_bit | num_args;
        }

    public:
        static constexpr size_t types = get_types();
        using arg_type = basic_arg<CharT>;

        using value_type =
            typename std::conditional<is_packed, detail::value, arg_type>::type;
        static constexpr size_t data_size =
            num_args + (is_packed && num_args != 0 ? 0 : 1);

        template <typename Ctx, typename ParseCtx>
        SCN_CONSTEXPR14 arg_store(detail::ctx_tag<Ctx>,
                                  detail::parse_ctx_tag<ParseCtx>,
                                  Args&... a) noexcept
            : m_data{{detail::make_arg<is_packed, Ctx, ParseCtx>(a)...}}
        {
        }

        SCN_CONSTEXPR14 span<value_type> data() noexcept
        {
            return make_span(m_data.data(),
                             static_cast<std::ptrdiff_t>(m_data.size()));
        }

    private:
        detail::array<value_type, data_size> m_data;
    };

    template <typename Context, typename ParseCtx, typename... Args>
    arg_store<typename Context::char_type, Args...> make_args(Args&... args)
    {
        return {detail::ctx_tag<Context>(), detail::parse_ctx_tag<ParseCtx>(),
                args...};
    }
    template <typename WrappedRange,
              typename Format,
              typename... Args,
              typename CharT = typename WrappedRange::char_type>
    arg_store<CharT, Args...> make_args_for(WrappedRange&,
                                            Format,
                                            Args&... args)
    {
        using context_type = basic_context<WrappedRange>;
        using parse_context_type =
            typename detail::parse_context_template_for_format<
                Format>::template type<typename context_type::char_type>;
        return {detail::ctx_tag<context_type>(),
                detail::parse_ctx_tag<parse_context_type>(), args...};
    }

    template <typename CharT>
    class basic_args {
    public:
        using arg_type = basic_arg<CharT>;

        constexpr basic_args() noexcept = default;

        template <typename... Args>
        SCN_CONSTEXPR14 basic_args(arg_store<CharT, Args...>& store) noexcept
            : m_types(store.types)
        {
            set_data(store.m_data.data());
        }

        SCN_CONSTEXPR14 basic_args(span<arg_type> args) noexcept
            : m_types(detail::is_unpacked_bit | args.size())
        {
            set_data(args.data());
        }

        SCN_CONSTEXPR14 arg_type get(std::ptrdiff_t i) const noexcept
        {
            return do_get(i);
        }

        SCN_NODISCARD SCN_CONSTEXPR14 bool check_id(
            std::ptrdiff_t i) const noexcept
        {
            if (!is_packed()) {
                return static_cast<size_t>(i) <
                       (m_types &
                        ~static_cast<size_t>(detail::is_unpacked_bit));
            }
            return type(i) != detail::none_type;
        }

        SCN_NODISCARD constexpr size_t max_size() const noexcept
        {
            return is_packed()
                       ? static_cast<size_t>(detail::max_packed_args)
                       : m_types &
                             ~static_cast<size_t>(detail::is_unpacked_bit);
        }

    private:
        size_t m_types{0};
        union {
            detail::value* m_values;
            arg_type* m_args;
        };

        SCN_NODISCARD constexpr bool is_packed() const noexcept
        {
            return (m_types & detail::is_unpacked_bit) == 0;
        }

        SCN_NODISCARD SCN_CONSTEXPR14 typename detail::type type(
            std::ptrdiff_t i) const noexcept
        {
            size_t shift = static_cast<size_t>(i) * detail::packed_arg_bitsize;
            return static_cast<typename detail::type>(
                (static_cast<size_t>(m_types) >> shift) &
                detail::packed_arg_mask);
        }

        SCN_CONSTEXPR14 void set_data(detail::value* values) noexcept
        {
            m_values = values;
        }
        SCN_CONSTEXPR14 void set_data(arg_type* args) noexcept
        {
            m_args = args;
        }

        SCN_CONSTEXPR14 arg_type do_get(std::ptrdiff_t i) const noexcept
        {
            SCN_EXPECT(i >= 0);

            arg_type arg;
            if (!is_packed()) {
                auto num_args = static_cast<std::ptrdiff_t>(max_size());
                if (SCN_LIKELY(i < num_args)) {
                    arg = m_args[i];
                }
                return arg;
            }

            SCN_EXPECT(m_values);
            if (SCN_UNLIKELY(
                    i > static_cast<std::ptrdiff_t>(detail::max_packed_args))) {
                return arg;
            }

            arg.m_type = type(i);
            if (arg.m_type == detail::none_type) {
                return arg;
            }
            arg.m_value = m_values[i];
            return arg;
        }
    };

    SCN_END_NAMESPACE
}  // namespace scn

#endif  // SCN_DETAIL_ARGS_H
