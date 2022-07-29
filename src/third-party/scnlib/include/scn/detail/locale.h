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

#ifndef SCN_DETAIL_LOCALE_H
#define SCN_DETAIL_LOCALE_H

#include "../unicode/unicode.h"
#include "../util/array.h"
#include "../util/string_view.h"
#include "../util/unique_ptr.h"

#include <cwchar>
#include <string>

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace detail {
        constexpr bool has_zero(uint64_t v)
        {
            return (v - UINT64_C(0x0101010101010101)) & ~v &
                   UINT64_C(0x8080808080808080);
        }

        template <typename CharT>
        CharT ascii_widen(char ch);
        template <>
        constexpr char ascii_widen(char ch)
        {
            return ch;
        }
        template <>
        constexpr wchar_t ascii_widen(char ch)
        {
            return static_cast<wchar_t>(ch);
        }

        // Hand write to avoid C locales and thus noticeable performance losses
        inline bool is_space(char ch) noexcept
        {
            static constexpr detail::array<bool, 256> lookup = {
                {false, false, false, false, false, false, false, false, false,
                 true,  true,  true,  true,  true,  false, false, false, false,
                 false, false, false, false, false, false, false, false, false,
                 false, false, false, false, false, true,  false, false, false,
                 false, false, false, false, false, false, false, false, false,
                 false, false, false, false, false, false, false, false, false,
                 false, false, false, false, false, false, false, false, false,
                 false, false, false, false, false, false, false, false, false,
                 false, false, false, false, false, false, false, false, false,
                 false, false, false, false, false, false, false, false, false,
                 false, false, false, false, false, false, false, false, false,
                 false, false, false, false, false, false, false, false, false,
                 false, false, false, false, false, false, false, false, false,
                 false, false, false, false, false, false, false, false, false,
                 false, false, false, false, false, false, false, false, false,
                 false, false, false, false, false, false, false, false, false,
                 false, false, false, false, false, false, false, false, false,
                 false, false, false, false, false, false, false, false, false,
                 false, false, false, false, false, false, false, false, false,
                 false, false, false, false, false, false, false, false, false,
                 false, false, false, false, false, false, false, false, false,
                 false, false, false, false, false, false, false, false, false,
                 false, false, false, false, false, false, false, false, false,
                 false, false, false, false, false, false, false, false, false,
                 false, false, false, false, false, false, false, false, false,
                 false, false, false, false, false, false, false, false, false,
                 false, false, false, false, false, false, false, false, false,
                 false, false, false, false, false, false, false, false, false,
                 false, false, false, false}};
            return lookup[static_cast<size_t>(static_cast<unsigned char>(ch))];
        }
        constexpr inline bool is_space(wchar_t ch) noexcept
        {
            return ch == 0x20 || (ch >= 0x09 && ch <= 0x0d);
        }
        constexpr inline bool is_space(code_point cp) noexcept
        {
            return cp == 0x20 || (cp >= 0x09 && cp <= 0x0d);
        }

        constexpr inline bool is_digit(char ch) noexcept
        {
            return ch >= '0' && ch <= '9';
        }
        constexpr inline bool is_digit(wchar_t ch) noexcept
        {
            return ch >= L'0' && ch <= L'9';
        }
        constexpr inline bool is_digit(code_point cp) noexcept
        {
            return cp >= '0' && cp <= '9';
        }

        template <typename CharT>
        struct locale_defaults;
        template <>
        struct locale_defaults<char> {
            static constexpr string_view truename()
            {
                return {"true"};
            }
            static constexpr string_view falsename()
            {
                return {"false"};
            }
            static constexpr char decimal_point() noexcept
            {
                return '.';
            }
            static constexpr char thousands_separator() noexcept
            {
                return ',';
            }
        };
        template <>
        struct locale_defaults<wchar_t> {
            static constexpr wstring_view truename()
            {
                return {L"true"};
            }
            static constexpr wstring_view falsename()
            {
                return {L"false"};
            }
            static constexpr wchar_t decimal_point() noexcept
            {
                return L'.';
            }
            static constexpr wchar_t thousands_separator() noexcept
            {
                return L',';
            }
        };
    }  // namespace detail

    SCN_CLANG_PUSH_IGNORE_UNDEFINED_TEMPLATE

    SCN_CLANG_PUSH
    SCN_CLANG_IGNORE("-Wpadded")

    // scn::scan:
    //   - no L flag -> use hard-coded defaults, akin to "C"
    //     locale_ref.default() -> default_locale_ref
    //   - L flag -> use global C++ locale
    //     locale_ref.localized() -> custom_locale_ref (global C++)
    // scn::scan_localized:
    //   - no L flag -> use hard-coded defaults, akin to "C"
    //     locale_ref.default() -> default_locale_ref
    //   - L flag -> use given C++ locale
    //     locale_ref.localized() -> custom_locale_ref (given locale)

    namespace detail {
        // constexpr locale
        template <typename CharT, typename SV, typename Def>
        struct basic_static_locale_ref_base {
            using char_type = CharT;
            using string_view_type = SV;
            using defaults = Def;

            static constexpr bool is_static = true;

            constexpr basic_static_locale_ref_base() = default;

            static constexpr bool is_space(char_type ch)
            {
                return detail::is_space(ch);
            }
            static constexpr bool is_digit(char_type ch)
            {
                return detail::is_digit(ch);
            }

            static SCN_CONSTEXPR14 bool is_space(span<const char_type> ch)
            {
                SCN_EXPECT(ch.size() >= 1);
                return detail::is_space(ch[0]);
            }
            static SCN_CONSTEXPR14 bool is_digit(span<const char_type> ch)
            {
                SCN_EXPECT(ch.size() >= 1);
                return detail::is_digit(ch[0]);
            }

            static constexpr char_type decimal_point()
            {
                return defaults::decimal_point();
            }
            static constexpr char_type thousands_separator()
            {
                return defaults::thousands_separator();
            }

            static constexpr string_view_type truename()
            {
                return defaults::truename();
            }
            static constexpr string_view_type falsename()
            {
                return defaults::falsename();
            }
        };
        template <typename CharT>
        struct basic_static_locale_ref
            : basic_static_locale_ref_base<CharT,
                                           basic_string_view<CharT>,
                                           locale_defaults<CharT>> {
        };
        template <>
        struct basic_static_locale_ref<code_point>
            : basic_static_locale_ref_base<code_point,
                                           string_view,
                                           locale_defaults<char>> {
        };

        // base class
        template <typename CharT>
        class basic_locale_ref_impl_base {
        public:
            using char_type = CharT;
            using string_type = std::basic_string<char_type>;
            using string_view_type = basic_string_view<char_type>;

            static constexpr bool is_static = false;

            basic_locale_ref_impl_base() = default;

            basic_locale_ref_impl_base(const basic_locale_ref_impl_base&) =
                default;
            basic_locale_ref_impl_base(basic_locale_ref_impl_base&&) = default;
            basic_locale_ref_impl_base& operator=(
                const basic_locale_ref_impl_base&) = default;
            basic_locale_ref_impl_base& operator=(
                basic_locale_ref_impl_base&&) = default;

#define SCN_DEFINE_LOCALE_REF_CTYPE(f)          \
    bool is_##f(char_type ch) const             \
    {                                           \
        return do_is_##f(ch);                   \
    }                                           \
    bool is_##f(span<const char_type> ch) const \
    {                                           \
        return do_is_##f(ch);                   \
    }

            SCN_DEFINE_LOCALE_REF_CTYPE(space)
            SCN_DEFINE_LOCALE_REF_CTYPE(digit)
            // SCN_DEFINE_LOCALE_REF_CTYPE(alnum)
            // SCN_DEFINE_LOCALE_REF_CTYPE(alpha)
            // SCN_DEFINE_LOCALE_REF_CTYPE(blank)
            // SCN_DEFINE_LOCALE_REF_CTYPE(cntrl)
            // SCN_DEFINE_LOCALE_REF_CTYPE(graph)
            // SCN_DEFINE_LOCALE_REF_CTYPE(lower)
            // SCN_DEFINE_LOCALE_REF_CTYPE(print)
            // SCN_DEFINE_LOCALE_REF_CTYPE(punct)
            // SCN_DEFINE_LOCALE_REF_CTYPE(upper)
            // SCN_DEFINE_LOCALE_REF_CTYPE(xdigit)
#undef SCN_DEFINE_LOCALE_REF_CTYPE

            char_type decimal_point() const
            {
                return do_decimal_point();
            }
            char_type thousands_separator() const
            {
                return do_thousands_separator();
            }

            string_view_type truename() const
            {
                return do_truename();
            }
            string_view_type falsename() const
            {
                return do_falsename();
            }

        protected:
            ~basic_locale_ref_impl_base() = default;

        private:
#define SCN_DECLARE_LOCALE_REF_CTYPE_DO(f)       \
    virtual bool do_is_##f(char_type) const = 0; \
    virtual bool do_is_##f(span<const char_type>) const = 0;
            SCN_DECLARE_LOCALE_REF_CTYPE_DO(space)
            SCN_DECLARE_LOCALE_REF_CTYPE_DO(digit)
            // SCN_DECLARE_LOCALE_REF_CTYPE_DO(alnum)
            // SCN_DECLARE_LOCALE_REF_CTYPE_DO(alpha)
            // SCN_DECLARE_LOCALE_REF_CTYPE_DO(blank)
            // SCN_DECLARE_LOCALE_REF_CTYPE_DO(cntrl)
            // SCN_DECLARE_LOCALE_REF_CTYPE_DO(graph)
            // SCN_DECLARE_LOCALE_REF_CTYPE_DO(lower)
            // SCN_DECLARE_LOCALE_REF_CTYPE_DO(print)
            // SCN_DECLARE_LOCALE_REF_CTYPE_DO(punct)
            // SCN_DECLARE_LOCALE_REF_CTYPE_DO(upper)
            // SCN_DECLARE_LOCALE_REF_CTYPE_DO(xdigit)
#undef SCN_DECLARE_LOCALE_REF_CTYPE_DO

            virtual char_type do_decimal_point() const = 0;
            virtual char_type do_thousands_separator() const = 0;
            virtual string_view_type do_truename() const = 0;
            virtual string_view_type do_falsename() const = 0;
        };

        // hardcoded "C", using static_locale_ref
        template <typename CharT>
        class basic_default_locale_ref final
            : public basic_locale_ref_impl_base<CharT> {
            using base = basic_locale_ref_impl_base<CharT>;

        public:
            using char_type = typename base::char_type;
            using string_view_type = typename base::string_view_type;

            basic_default_locale_ref() = default;

        private:
            using static_type = basic_static_locale_ref<char_type>;

            bool do_is_space(char_type ch) const override
            {
                return static_type::is_space(ch);
            }
            bool do_is_digit(char_type ch) const override
            {
                return static_type::is_digit(ch);
            }

            bool do_is_space(span<const char_type> ch) const override
            {
                return static_type::is_space(ch);
            }
            bool do_is_digit(span<const char_type> ch) const override
            {
                return static_type::is_digit(ch);
            }

            char_type do_decimal_point() const override
            {
                return static_type::decimal_point();
            }
            char_type do_thousands_separator() const override
            {
                return static_type::thousands_separator();
            }
            string_view_type do_truename() const override
            {
                return static_type::truename();
            }
            string_view_type do_falsename() const override
            {
                return static_type::falsename();
            }
        };

        // custom
        template <typename CharT>
        class basic_custom_locale_ref final
            : public basic_locale_ref_impl_base<CharT> {
            using base = basic_locale_ref_impl_base<CharT>;

        public:
            using char_type = typename base::char_type;
            using string_type = typename base::string_type;
            using string_view_type = typename base::string_view_type;

            basic_custom_locale_ref();
            basic_custom_locale_ref(const void* locale);

            basic_custom_locale_ref(const basic_custom_locale_ref&) = delete;
            basic_custom_locale_ref& operator=(const basic_custom_locale_ref&) =
                delete;

            basic_custom_locale_ref(basic_custom_locale_ref&&);
            basic_custom_locale_ref& operator=(basic_custom_locale_ref&&);

            ~basic_custom_locale_ref();

            static basic_custom_locale_ref make_classic();

            const void* get_locale() const
            {
                return m_locale;
            }

            void convert_to_global();
            void convert_to_classic();

            // narrow: locale multibyte -> locale wide
            // wide: identity
            error convert_to_wide(const CharT* from_begin,
                                  const CharT* from_end,
                                  const CharT*& from_next,
                                  wchar_t* to_begin,
                                  wchar_t* to_end,
                                  wchar_t*& to_next) const;
            expected<wchar_t> convert_to_wide(const CharT* from_begin,
                                              const CharT* from_end) const;

#define SCN_DEFINE_CUSTOM_LOCALE_CTYPE(f)     \
    bool is_##f(char_type) const;             \
    bool is_##f(span<const char_type>) const; \
    bool is_##f(code_point) const;
            SCN_DEFINE_CUSTOM_LOCALE_CTYPE(alnum)
            SCN_DEFINE_CUSTOM_LOCALE_CTYPE(alpha)
            SCN_DEFINE_CUSTOM_LOCALE_CTYPE(blank)
            SCN_DEFINE_CUSTOM_LOCALE_CTYPE(cntrl)
            SCN_DEFINE_CUSTOM_LOCALE_CTYPE(graph)
            SCN_DEFINE_CUSTOM_LOCALE_CTYPE(lower)
            SCN_DEFINE_CUSTOM_LOCALE_CTYPE(print)
            SCN_DEFINE_CUSTOM_LOCALE_CTYPE(punct)
            SCN_DEFINE_CUSTOM_LOCALE_CTYPE(upper)
            SCN_DEFINE_CUSTOM_LOCALE_CTYPE(xdigit)
#undef SCN_DEFINE_CUSTOM_LOCALE_CTYPE

            bool is_space(code_point) const;
            using base::is_space;

            bool is_digit(code_point) const;
            using base::is_digit;

            template <typename T>
            expected<std::ptrdiff_t> read_num(T& val,
                                              const string_type& buf,
                                              int base) const;

        private:
            SCN_CLANG_PUSH_IGNORE_UNDEFINED_TEMPLATE
            bool do_is_space(char_type ch) const override;
            bool do_is_digit(char_type ch) const override;

            bool do_is_space(span<const char_type> ch) const override;
            bool do_is_digit(span<const char_type> ch) const override;
            SCN_CLANG_POP_IGNORE_UNDEFINED_TEMPLATE

            char_type do_decimal_point() const override;
            char_type do_thousands_separator() const override;
            string_view_type do_truename() const override;
            string_view_type do_falsename() const override;

            void _initialize();

            const void* m_locale{nullptr};
            void* m_data{nullptr};
        };
    }  // namespace detail

    template <typename CharT>
    class basic_locale_ref {
    public:
        using char_type = CharT;
        using impl_base = detail::basic_locale_ref_impl_base<char_type>;
        using static_type = detail::basic_static_locale_ref<char_type>;
        using default_type = detail::basic_default_locale_ref<char_type>;
        using custom_type = detail::basic_custom_locale_ref<char_type>;

        // default
        constexpr basic_locale_ref() = default;
        // nullptr = global
        constexpr basic_locale_ref(const void* p) : m_payload(p) {}

        basic_locale_ref clone() const
        {
            return {m_payload};
        }

        constexpr bool has_custom() const
        {
            return m_payload != nullptr;
        }

        // hardcoded "C", constexpr, should be preferred whenever possible
        constexpr static_type get_static() const
        {
            return {};
        }

        // hardcoded "C", not constexpr
        default_type& get_default()
        {
            return m_default;
        }
        const default_type& get_default() const
        {
            return m_default;
        }

        // global locale or given locale
        custom_type& get_localized()
        {
            _construct_custom();
            return *m_custom;
        }
        const custom_type& get_localized() const
        {
            _construct_custom();
            return *m_custom;
        }

        custom_type make_localized_classic() const
        {
            return custom_type::make_classic();
        }

        custom_type* get_localized_unsafe()
        {
            return m_custom.get();
        }
        const custom_type* get_localized_unsafe() const
        {
            return m_custom.get();
        }

        // virtual interface
        impl_base& get(bool localized)
        {
            if (localized) {
                return get_localized();
            }
            return get_default();
        }
        const impl_base& get(bool localized) const
        {
            if (localized) {
                return get_localized();
            }
            return get_default();
        }

        void prepare_localized() const
        {
            _construct_custom();
        }
        void reset_locale(const void* payload)
        {
            m_custom.reset();
            m_payload = payload;
            _construct_custom();
        }

    private:
        void _construct_custom() const
        {
            if (m_custom) {
                // already constructed
                return;
            }
            SCN_CLANG_PUSH_IGNORE_UNDEFINED_TEMPLATE
            m_custom = detail::make_unique<custom_type>(m_payload);
            SCN_CLANG_POP_IGNORE_UNDEFINED_TEMPLATE
        }

        mutable detail::unique_ptr<custom_type> m_custom{nullptr};
        const void* m_payload{nullptr};
        default_type m_default{};
    };

    template <typename CharT, typename Locale>
    basic_locale_ref<CharT> make_locale_ref(const Locale& loc)
    {
        return {std::addressof(loc)};
    }
    template <typename CharT>
    basic_locale_ref<CharT> make_default_locale_ref()
    {
        return {};
    }

    using locale_ref = basic_locale_ref<char>;
    using wlocale_ref = basic_locale_ref<wchar_t>;

    SCN_CLANG_POP // -Wpadded
    SCN_CLANG_POP_IGNORE_UNDEFINED_TEMPLATE
    SCN_END_NAMESPACE
}  // namespace scn

#if defined(SCN_HEADER_ONLY) && SCN_HEADER_ONLY && !defined(SCN_LOCALE_CPP)
#include "locale.cpp"
#endif

#endif  // SCN_DETAIL_LOCALE_H
