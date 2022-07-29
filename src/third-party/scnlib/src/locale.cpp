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

#if defined(SCN_HEADER_ONLY) && SCN_HEADER_ONLY
#define SCN_LOCALE_CPP
#endif

#include <scn/detail/locale.h>
#include <scn/util/math.h>

#include <cctype>
#include <cmath>
#include <cwchar>
#include <iomanip>
#include <locale>
#include <sstream>

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace detail {
        template <typename CharT>
        struct locale_data {
            using char_type = CharT;
            using string_type = std::basic_string<char_type>;

            std::locale global_locale{std::locale()};
            std::locale classic_locale{std::locale::classic()};

            string_type truename{};
            string_type falsename{};
            char_type decimal_point{};
            char_type thousands_separator{};
        };

        template <typename CharT>
        const std::locale& to_locale(const basic_custom_locale_ref<CharT>& l)
        {
            SCN_EXPECT(l.get_locale());
            return *static_cast<const std::locale*>(l.get_locale());
        }

        // Buggy on gcc 5 and 6
        SCN_GCC_PUSH
        SCN_GCC_IGNORE("-Wmaybe-uninitialized")

        template <typename CharT>
        basic_custom_locale_ref<CharT>::basic_custom_locale_ref()
        {
            auto data = new locale_data<CharT>{};
            m_data = data;
            m_locale = &data->global_locale;
            _initialize();
        }
        template <typename CharT>
        basic_custom_locale_ref<CharT>::basic_custom_locale_ref(
            const void* locale)
            : m_locale(locale)
        {
            auto data = new locale_data<CharT>{};
            m_data = data;
            if (!locale) {
                m_locale = &data->global_locale;
            }
            _initialize();
        }

        SCN_GCC_POP

        template <typename CharT>
        void basic_custom_locale_ref<CharT>::_initialize()
        {
            const auto& facet =
                std::use_facet<std::numpunct<CharT>>(to_locale(*this));

            auto& data = *static_cast<locale_data<CharT>*>(m_data);
            data.truename = facet.truename();
            data.falsename = facet.falsename();
            data.decimal_point = facet.decimal_point();
            data.thousands_separator = facet.thousands_sep();
        }

        template <typename CharT>
        basic_custom_locale_ref<CharT>::basic_custom_locale_ref(
            basic_custom_locale_ref&& o)
        {
            m_data = o.m_data;
            m_locale = o.m_locale;

            o.m_data = nullptr;
            o.m_locale = nullptr;

            _initialize();
        }
        template <typename CharT>
        auto basic_custom_locale_ref<CharT>::operator=(
            basic_custom_locale_ref&& o) -> basic_custom_locale_ref&
        {
            delete static_cast<locale_data<CharT>*>(m_data);

            m_data = o.m_data;
            m_locale = o.m_locale;

            o.m_data = nullptr;
            o.m_locale = nullptr;

            _initialize();

            return *this;
        }

        template <typename CharT>
        basic_custom_locale_ref<CharT>::~basic_custom_locale_ref()
        {
            delete static_cast<locale_data<CharT>*>(m_data);
        }

        template <typename CharT>
        auto basic_custom_locale_ref<CharT>::make_classic()
            -> basic_custom_locale_ref
        {
            basic_custom_locale_ref loc{};
            loc.convert_to_classic();
            return loc;
        }

        template <typename CharT>
        void basic_custom_locale_ref<CharT>::convert_to_classic()
        {
            m_locale =
                &static_cast<locale_data<CharT>*>(m_data)->classic_locale;
        }
        template <typename CharT>
        void basic_custom_locale_ref<CharT>::convert_to_global()
        {
            SCN_EXPECT(m_data);
            m_locale = &static_cast<locale_data<CharT>*>(m_data)->global_locale;
        }

        template <typename CharT>
        bool basic_custom_locale_ref<CharT>::do_is_space(char_type ch) const
        {
            return std::isspace(ch, to_locale(*this));
        }
        template <typename CharT>
        bool basic_custom_locale_ref<CharT>::do_is_digit(char_type ch) const
        {
            return std::isdigit(ch, to_locale(*this));
        }

        template <typename CharT>
        auto basic_custom_locale_ref<CharT>::do_decimal_point() const
            -> char_type
        {
            return static_cast<locale_data<CharT>*>(m_data)->decimal_point;
        }
        template <typename CharT>
        auto basic_custom_locale_ref<CharT>::do_thousands_separator() const
            -> char_type
        {
            return static_cast<locale_data<CharT>*>(m_data)
                ->thousands_separator;
        }
        template <typename CharT>
        auto basic_custom_locale_ref<CharT>::do_truename() const
            -> string_view_type
        {
            const auto& str =
                static_cast<locale_data<CharT>*>(m_data)->truename;
            return {str.data(), str.size()};
        }
        template <typename CharT>
        auto basic_custom_locale_ref<CharT>::do_falsename() const
            -> string_view_type
        {
            const auto& str =
                static_cast<locale_data<CharT>*>(m_data)->falsename;
            return {str.data(), str.size()};
        }

        static inline error convert_to_wide_impl(const std::locale&,
                                                 const char*,
                                                 const char*,
                                                 const char*&,
                                                 wchar_t*,
                                                 wchar_t*,
                                                 wchar_t*&)
        {
            SCN_EXPECT(false);
            SCN_UNREACHABLE;
        }
        static inline error convert_to_wide_impl(const std::locale&,
                                                 const wchar_t*,
                                                 const wchar_t*,
                                                 const wchar_t*&,
                                                 wchar_t*,
                                                 wchar_t*,
                                                 wchar_t*&)
        {
            SCN_EXPECT(false);
            SCN_UNREACHABLE;
        }

        template <typename CharT>
        error basic_custom_locale_ref<CharT>::convert_to_wide(
            const CharT* from_begin,
            const CharT* from_end,
            const CharT*& from_next,
            wchar_t* to_begin,
            wchar_t* to_end,
            wchar_t*& to_next) const
        {
            return convert_to_wide_impl(to_locale(*this), from_begin, from_end,
                                        from_next, to_begin, to_end, to_next);
        }

        static inline expected<wchar_t> convert_to_wide_impl(const std::locale&,
                                                             const char*,
                                                             const char*)
        {
            SCN_EXPECT(false);
            SCN_UNREACHABLE;
        }
        static inline expected<wchar_t> convert_to_wide_impl(const std::locale&,
                                                             const wchar_t*,
                                                             const wchar_t*)
        {
            SCN_EXPECT(false);
            SCN_UNREACHABLE;
        }

        template <typename CharT>
        expected<wchar_t> basic_custom_locale_ref<CharT>::convert_to_wide(
            const CharT* from_begin,
            const CharT* from_end) const
        {
            return convert_to_wide_impl(to_locale(*this), from_begin, from_end);
        }

        template <typename CharT>
        bool basic_custom_locale_ref<CharT>::do_is_space(
            span<const char_type> ch) const
        {
            const auto& locale = to_locale(*this);
            if (sizeof(CharT) == 1) {
                SCN_EXPECT(ch.size() >= 1);
                code_point cp{};
                auto it = parse_code_point(ch.begin(), ch.end(), cp);
                SCN_EXPECT(it);
                return is_space(cp);
            }
            SCN_EXPECT(ch.size() == 1);
            return std::isspace(ch[0], locale);
        }
        template <typename CharT>
        bool basic_custom_locale_ref<CharT>::do_is_digit(
            span<const char_type> ch) const
        {
            const auto& locale = to_locale(*this);
            if (sizeof(CharT) == 1) {
                SCN_EXPECT(ch.size() >= 1);
                code_point cp{};
                auto it = parse_code_point(ch.begin(), ch.end(), cp);
                SCN_EXPECT(it);
                return is_digit(cp);
            }
            SCN_EXPECT(ch.size() == 1);
            return std::isdigit(ch[0], locale);
        }

#define SCN_DEFINE_CUSTOM_LOCALE_CTYPE(f)                                 \
    template <typename CharT>                                             \
    bool basic_custom_locale_ref<CharT>::is_##f(char_type ch) const       \
    {                                                                     \
        return std::is##f(ch, to_locale(*this));                          \
    }                                                                     \
    template <typename CharT>                                             \
    bool basic_custom_locale_ref<CharT>::is_##f(code_point cp) const      \
    {                                                                     \
        return std::is##f(static_cast<wchar_t>(cp), to_locale(*this));    \
    }                                                                     \
    template <typename CharT>                                             \
    bool basic_custom_locale_ref<CharT>::is_##f(span<const char_type> ch) \
        const                                                             \
    {                                                                     \
        const auto& locale = to_locale(*this);                            \
        if (sizeof(CharT) == 1) {                                         \
            SCN_EXPECT(ch.size() >= 1);                                   \
            code_point cp{};                                              \
            auto it = parse_code_point(ch.begin(), ch.end(), cp);         \
            SCN_EXPECT(it);                                               \
            return is_##f(cp);                                            \
        }                                                                 \
        SCN_EXPECT(ch.size() == 1);                                       \
        return std::is##f(ch[0], locale);                                 \
    }
        SCN_DEFINE_CUSTOM_LOCALE_CTYPE(alnum)
        SCN_DEFINE_CUSTOM_LOCALE_CTYPE(alpha)
        SCN_DEFINE_CUSTOM_LOCALE_CTYPE(cntrl)
        SCN_DEFINE_CUSTOM_LOCALE_CTYPE(graph)
        SCN_DEFINE_CUSTOM_LOCALE_CTYPE(lower)
        SCN_DEFINE_CUSTOM_LOCALE_CTYPE(print)
        SCN_DEFINE_CUSTOM_LOCALE_CTYPE(punct)
        SCN_DEFINE_CUSTOM_LOCALE_CTYPE(upper)
        SCN_DEFINE_CUSTOM_LOCALE_CTYPE(xdigit)
#undef SCN_DEFINE_CUSTOM_LOCALE_CTYPE

        template <typename CharT>
        bool basic_custom_locale_ref<CharT>::is_space(code_point cp) const
        {
            return std::isspace(static_cast<wchar_t>(cp), to_locale(*this));
        }
        template <typename CharT>
        bool basic_custom_locale_ref<CharT>::is_digit(code_point cp) const
        {
            return std::isdigit(static_cast<wchar_t>(cp), to_locale(*this));
        }

        // For some reason, there's no isblank in libc++
        template <typename CharT>
        bool basic_custom_locale_ref<CharT>::is_blank(char_type ch) const
        {
            return std::use_facet<std::ctype<CharT>>(to_locale(*this))
                .is(std::ctype_base::blank, ch);
        }
        template <typename CharT>
        bool basic_custom_locale_ref<CharT>::is_blank(code_point ch) const
        {
            return std::use_facet<std::ctype<wchar_t>>(to_locale(*this))
                .is(std::ctype_base::blank, static_cast<wchar_t>(ch));
        }
        template <typename CharT>
        bool basic_custom_locale_ref<CharT>::is_blank(
            span<const char_type> ch) const
        {
            const auto& locale = to_locale(*this);
            if (sizeof(CharT) == 1) {
                SCN_EXPECT(ch.size() >= 1);
                code_point cp{};
                auto it = parse_code_point(ch.begin(), ch.end(), cp);
                SCN_EXPECT(it);
                return is_blank(cp);
            }
            SCN_EXPECT(ch.size() == 1);
            return std::use_facet<std::ctype<CharT>>(locale).is(
                std::ctype_base::blank, ch[0]);
        }

        template <typename T>
        auto read_num_check_range(T val) ->
            typename std::enable_if<std::is_integral<T>::value, error>::type
        {
            if (val == std::numeric_limits<T>::max()) {
                return error(error::value_out_of_range,
                             "Scanned number out of range: overflow");
            }
            if (val == std::numeric_limits<T>::min()) {
                return error(error::value_out_of_range,
                             "Scanned number out of range: underflow");
            }
            return error(error::invalid_scanned_value,
                         "Localized number read failed");
        }
        template <typename T>
        auto read_num_check_range(T val) ->
            typename std::enable_if<std::is_floating_point<T>::value,
                                    error>::type
        {
            SCN_GCC_COMPAT_PUSH
            SCN_GCC_COMPAT_IGNORE("-Wfloat-equal")
            if (val == std::numeric_limits<T>::max() ||
                val == -std::numeric_limits<T>::max()) {
                return error(error::value_out_of_range,
                             "Scanned number out of range: overflow");
            }
            if (val == zero_value<T>::value) {
                return error(error::value_out_of_range,
                             "Scanned number out of range: underflow");
            }
            SCN_GCC_COMPAT_POP
            return error(error::invalid_scanned_value,
                         "Localized number read failed");
        }

        template <typename T, typename CharT>
        error do_read_num_impl(T& val, std::basic_istringstream<CharT>& ss)
        {
            ss >> val;
            return {};
        }
        template <typename CharT>
        error do_read_num_impl(CharT& val, std::basic_istringstream<CharT>& ss)
        {
            long long tmp;
            if (!(ss >> tmp)) {
                return {};
            }
            if (tmp > std::numeric_limits<CharT>::max()) {
                return {error::value_out_of_range,
                        "Scanned number out of range: overflow"};
            }
            if (tmp < std::numeric_limits<CharT>::min()) {
                return {error::value_out_of_range,
                        "Scanned number out of range: underflow"};
            }
            val = static_cast<CharT>(tmp);
            return {};
        }
        template <typename CharT>
        error do_read_num_impl(signed char& val,
                               std::basic_istringstream<CharT>& ss)
        {
            int tmp;
            if (!(ss >> tmp)) {
                return {};
            }
            if (tmp > std::numeric_limits<signed char>::max()) {
                return {error::value_out_of_range,
                        "Scanned number out of range: overflow"};
            }
            if (tmp < std::numeric_limits<signed char>::min()) {
                return {error::value_out_of_range,
                        "Scanned number out of range: underflow"};
            }
            val = static_cast<signed char>(tmp);
            return {};
        }
        template <typename CharT>
        error do_read_num_impl(unsigned char& val,
                               std::basic_istringstream<CharT>& ss)
        {
            int tmp;
            if (!(ss >> tmp)) {
                return {};
            }
            if (tmp > std::numeric_limits<unsigned char>::max()) {
                return {error::value_out_of_range,
                        "Scanned number out of range: overflow"};
            }
            if (tmp < 0) {
                return {error::value_out_of_range,
                        "Scanned number out of range: underflow"};
            }
            val = static_cast<unsigned char>(tmp);
            return {};
        }

        template <typename T, typename CharT>
        expected<std::ptrdiff_t> do_read_num(
            T& val,
            const std::locale& loc,
            const std::basic_string<CharT>& buf,
            int base)
        {
#if SCN_HAS_EXCEPTIONS
            std::basic_istringstream<CharT> ss(buf);
            ss.imbue(loc);
            ss >> std::setbase(base);

            try {
                T tmp;
                auto e = do_read_num_impl(tmp, ss);
                if (ss.bad()) {
                    return error(error::unrecoverable_internal_error,
                                 "Localized stringstream is bad");
                }
                if (!e) {
                    return e;
                }
                if (ss.fail()) {
                    return read_num_check_range(tmp);
                }
                val = tmp;
            }
            catch (const std::ios_base::failure& f) {
                return error(error::invalid_scanned_value, f.what());
            }
            return ss.eof() ? static_cast<std::ptrdiff_t>(buf.size())
                            : static_cast<std::ptrdiff_t>(ss.tellg());
#else
            SCN_UNUSED(val);
            SCN_UNUSED(loc);
            SCN_UNUSED(buf);
            return error(error::exceptions_required,
                         "Localized number reading is only supported with "
                         "exceptions enabled");
#endif
        }

        template <>
        expected<std::ptrdiff_t> do_read_num(wchar_t&,
                                             const std::locale&,
                                             const std::string&,
                                             int)
        {
            SCN_EXPECT(false);
            SCN_UNREACHABLE;
        }
        template <>
        expected<std::ptrdiff_t> do_read_num(char&,
                                             const std::locale&,
                                             const std::wstring&,
                                             int)
        {
            SCN_EXPECT(false);
            SCN_UNREACHABLE;
        }

        template <typename CharT>
        template <typename T>
        expected<std::ptrdiff_t> basic_custom_locale_ref<CharT>::read_num(
            T& val,
            const string_type& buf,
            int b) const
        {
            return do_read_num<T, CharT>(val, to_locale(*this), buf, b);
        }

#if SCN_INCLUDE_SOURCE_DEFINITIONS

        SCN_CLANG_PUSH
        SCN_CLANG_IGNORE("-Wpadded")
        SCN_CLANG_IGNORE("-Wweak-template-vtables")
        template class basic_custom_locale_ref<char>;
        template class basic_custom_locale_ref<wchar_t>;
        SCN_CLANG_POP

        template expected<std::ptrdiff_t>
        basic_custom_locale_ref<char>::read_num<signed char>(signed char&,
                                                             const string_type&,
                                                             int) const;
        template expected<std::ptrdiff_t>
        basic_custom_locale_ref<char>::read_num<short>(short&,
                                                       const string_type&,
                                                       int) const;
        template expected<std::ptrdiff_t>
        basic_custom_locale_ref<char>::read_num<int>(int&,
                                                     const string_type&,
                                                     int) const;
        template expected<std::ptrdiff_t>
        basic_custom_locale_ref<char>::read_num<long>(long&,
                                                      const string_type&,
                                                      int) const;
        template expected<std::ptrdiff_t>
        basic_custom_locale_ref<char>::read_num<long long>(long long&,
                                                           const string_type&,
                                                           int) const;
        template expected<std::ptrdiff_t> basic_custom_locale_ref<
            char>::read_num<unsigned char>(unsigned char&,
                                           const string_type&,
                                           int) const;
        template expected<std::ptrdiff_t> basic_custom_locale_ref<
            char>::read_num<unsigned short>(unsigned short&,
                                            const string_type&,
                                            int) const;
        template expected<std::ptrdiff_t> basic_custom_locale_ref<
            char>::read_num<unsigned int>(unsigned int&,
                                          const string_type&,
                                          int) const;
        template expected<std::ptrdiff_t> basic_custom_locale_ref<
            char>::read_num<unsigned long>(unsigned long&,
                                           const string_type&,
                                           int) const;
        template expected<std::ptrdiff_t> basic_custom_locale_ref<
            char>::read_num<unsigned long long>(unsigned long long&,
                                                const string_type&,
                                                int) const;
        template expected<std::ptrdiff_t>
        basic_custom_locale_ref<char>::read_num<char>(char&,
                                                      const string_type&,
                                                      int) const;
        template expected<std::ptrdiff_t>
        basic_custom_locale_ref<char>::read_num<wchar_t>(wchar_t&,
                                                         const string_type&,
                                                         int) const;
        template expected<std::ptrdiff_t>
        basic_custom_locale_ref<char>::read_num<float>(float&,
                                                       const string_type&,
                                                       int) const;
        template expected<std::ptrdiff_t>
        basic_custom_locale_ref<char>::read_num<double>(double&,
                                                        const string_type&,
                                                        int) const;
        template expected<std::ptrdiff_t>
        basic_custom_locale_ref<char>::read_num<long double>(long double&,
                                                             const string_type&,
                                                             int) const;

        template expected<std::ptrdiff_t> basic_custom_locale_ref<
            wchar_t>::read_num<signed char>(signed char&,
                                            const string_type&,
                                            int) const;
        template expected<std::ptrdiff_t>
        basic_custom_locale_ref<wchar_t>::read_num<short>(short&,
                                                          const string_type&,
                                                          int) const;
        template expected<std::ptrdiff_t>
        basic_custom_locale_ref<wchar_t>::read_num<int>(int&,
                                                        const string_type&,
                                                        int) const;
        template expected<std::ptrdiff_t>
        basic_custom_locale_ref<wchar_t>::read_num<long>(long&,
                                                         const string_type&,
                                                         int) const;
        template expected<std::ptrdiff_t> basic_custom_locale_ref<
            wchar_t>::read_num<long long>(long long&,
                                          const string_type&,
                                          int) const;
        template expected<std::ptrdiff_t> basic_custom_locale_ref<
            wchar_t>::read_num<unsigned char>(unsigned char&,
                                              const string_type&,
                                              int) const;
        template expected<std::ptrdiff_t> basic_custom_locale_ref<
            wchar_t>::read_num<unsigned short>(unsigned short&,
                                               const string_type&,
                                               int) const;
        template expected<std::ptrdiff_t> basic_custom_locale_ref<
            wchar_t>::read_num<unsigned int>(unsigned int&,
                                             const string_type&,
                                             int) const;
        template expected<std::ptrdiff_t> basic_custom_locale_ref<
            wchar_t>::read_num<unsigned long>(unsigned long&,
                                              const string_type&,
                                              int) const;
        template expected<std::ptrdiff_t> basic_custom_locale_ref<
            wchar_t>::read_num<unsigned long long>(unsigned long long&,
                                                   const string_type&,
                                                   int) const;
        template expected<std::ptrdiff_t>
        basic_custom_locale_ref<wchar_t>::read_num<float>(float&,
                                                          const string_type&,
                                                          int) const;
        template expected<std::ptrdiff_t>
        basic_custom_locale_ref<wchar_t>::read_num<double>(double&,
                                                           const string_type&,
                                                           int) const;
        template expected<std::ptrdiff_t> basic_custom_locale_ref<
            wchar_t>::read_num<long double>(long double&,
                                            const string_type&,
                                            int) const;
        template expected<std::ptrdiff_t>
        basic_custom_locale_ref<wchar_t>::read_num<char>(char&,
                                                         const string_type&,
                                                         int) const;
        template expected<std::ptrdiff_t>
        basic_custom_locale_ref<wchar_t>::read_num<wchar_t>(wchar_t&,
                                                            const string_type&,
                                                            int) const;
#endif

    }  // namespace detail

    SCN_END_NAMESPACE
}  // namespace scn
