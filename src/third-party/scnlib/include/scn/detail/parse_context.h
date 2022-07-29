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

#ifndef SCN_DETAIL_PARSE_CONTEXT_H
#define SCN_DETAIL_PARSE_CONTEXT_H

#include "../util/expected.h"
#include "locale.h"

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace detail {
        class parse_context_base {
        public:
            SCN_CONSTEXPR14 std::ptrdiff_t next_arg_id()
            {
                return m_next_arg_id >= 0 ? m_next_arg_id++ : 0;
            }
            SCN_CONSTEXPR14 bool check_arg_id(std::ptrdiff_t)
            {
                if (m_next_arg_id > 0) {
                    return false;
                }
                m_next_arg_id = -1;
                return true;
            }

        protected:
            parse_context_base() = default;

            std::ptrdiff_t m_next_arg_id{0};
        };
    }  // namespace detail

    SCN_CLANG_PUSH
    SCN_CLANG_IGNORE("-Wpadded")

    template <typename CharT>
    class basic_parse_context : public detail::parse_context_base {
    public:
        using char_type = CharT;
        using locale_type = basic_locale_ref<CharT>;
        using string_view_type = basic_string_view<char_type>;
        using iterator = typename string_view_type::iterator;

        constexpr basic_parse_context(basic_string_view<char_type> f,
                                      locale_type& loc)
            : m_str(f), m_locale(loc)
        {
        }

        /**
         * Returns `true`, if `next_char()` is a whitespace character according
         * to the static locale. This means, that `skip_range_whitespace()`
         * should be called on the source range.
         */
        bool should_skip_ws()
        {
            bool skip = false;
            while (*this && m_locale.get_static().is_space(next_char())) {
                skip = true;
                advance_char();
            }
            return skip;
        }
        /**
         * Returns `true`, if a character equal to `next_char()` should be read
         * from the source range.
         *
         * If `*this` currently points to an escaped
         * brace character `"{{"` or `"}}"`, skips the first brace, so that
         * after this function is called, `next_char()` returns the character
         * that should be read.
         */
        bool should_read_literal()
        {
            const auto brace = detail::ascii_widen<char_type>('{');
            if (next_char() != brace) {
                if (next_char() == detail::ascii_widen<char_type>('}')) {
                    advance_char();
                }
                return true;
            }
            if (SCN_UNLIKELY(m_str.size() > 1 &&
                             *(m_str.begin() + 1) == brace)) {
                advance_char();
                return true;
            }
            return false;
        }
        /**
         * Returns `true` if `ch` is equal to `next_char()`
         */
        SCN_NODISCARD constexpr bool check_literal(char_type ch) const
        {
            return ch == next_char();
        }
        /**
         * Returns `true` if the code units contained in `ch` are equal to the
         * code units starting from `pctx.begin()`. If `chars_left() <
         * ch.size()`, returns `false`.
         */
        SCN_NODISCARD SCN_CONSTEXPR14 bool check_literal(
            span<const char_type> ch) const
        {
            if (chars_left() < ch.size()) {
                return false;
            }
            for (size_t i = 0; i < ch.size(); ++i) {
                if (ch[i] != m_str[i]) {
                    return false;
                }
            }
            return true;
        }
        /**
         * Returns `true` if `cp` is equal to the value returned by `next_cp()`.
         * If `next_cp()` errored, returns that error
         * (`error::invalid_encoding`).
         */
        SCN_NODISCARD SCN_CONSTEXPR14 expected<bool> check_literal_cp(
            code_point cp) const
        {
            auto next = next_cp();
            if (!next) {
                return next.error();
            }
            return cp == next.value();
        }

        /**
         * Returns `true` if there are characters left in `*this`.
         */
        constexpr bool good() const
        {
            return !m_str.empty();
        }
        constexpr explicit operator bool() const
        {
            return good();
        }

        /**
         * Returns the next character (= code unit) in `*this`.
         * `good()` must be `true`.
         */
        constexpr char_type next_char() const
        {
            return m_str.front();
        }
        /**
         * Returns the next code point in `*this`.
         * If the code point is encoded incorrectly, returns
         * `error::invalid_encoding`.
         */
        SCN_NODISCARD SCN_CONSTEXPR14 expected<code_point> next_cp() const
        {
            code_point cp{};
            auto it = parse_code_point(m_str.begin(), m_str.end(), cp);
            if (!it) {
                return it.error();
            }
            return {cp};
        }

        /**
         * Returns the number of chars (= code units) left in `*this`.
         */
        constexpr std::size_t chars_left() const noexcept
        {
            return m_str.size();
        }
        /**
         * Returns the number of code points left in `*this`. If `*this`
         * contains invalid encoding, returns `error::invalid_encoding`.
         */
        SCN_NODISCARD SCN_CONSTEXPR14 expected<std::size_t> cp_left()
            const noexcept
        {
            auto d = code_point_distance(m_str.begin(), m_str.end());
            if (!d) {
                return d.error();
            }
            return {static_cast<std::size_t>(d.value())};
        }

        /**
         * Advances `*this` by `n` characters (= code units). `*this` must have
         * at least `n` characters left.
         */
        SCN_CONSTEXPR14 void advance_char(std::ptrdiff_t n = 1) noexcept
        {
            SCN_EXPECT(chars_left() >= static_cast<std::size_t>(n));
            m_str.remove_prefix(static_cast<std::size_t>(n));
        }
        /**
         * Advances `*this` by a single code point. If the code point is encoded
         * incorrectly, returns `error::invalid_encoding`.
         */
        SCN_NODISCARD SCN_CONSTEXPR14 error advance_cp() noexcept
        {
            code_point cp{};
            auto it = parse_code_point(m_str.begin(), m_str.end(), cp);
            if (!it) {
                return it.error();
            }
            m_str.remove_prefix(
                static_cast<size_t>(it.value() - m_str.begin()));
            return {};
        }

        /**
         * Returns `true`, if `*this` has over `n` characters (= code units)
         * left, so that `peek_char()` with the same `n` parameter can be
         * called.
         */
        constexpr bool can_peek_char(std::size_t n = 1) const noexcept
        {
            return chars_left() > n;
        }

        /**
         * Returns the character (= code unit) `n` characters past the current
         * character, so that `peek_char(0)` is equivalent to `next_char()`.
         * `n <= chars_left()` must be `true`.
         */
        SCN_CONSTEXPR14 char_type peek_char(std::size_t n = 1) const noexcept
        {
            SCN_EXPECT(n <= chars_left());
            return m_str[n];
        }
        /**
         * Returns the code point past the current code point (`next_cp()`).
         *
         * If there is no code point to peek (the current code point is the last
         * one in `*this`), returns `error::end_of_range`.
         * If `*this` contains invalid encoding, returns
         * `error::invalid_encoding`.
         */
        SCN_NODISCARD SCN_CONSTEXPR14 expected<code_point> peek_cp()
            const noexcept
        {
            if (m_str.size() < 2) {
                return error{error::end_of_range,
                             "End of format string, cannot peek"};
            }

            code_point cp{};
            auto it = parse_code_point(m_str.begin(), m_str.end(), cp);
            if (!it) {
                return it.error();
            }
            if (it.value() == m_str.end()) {
                return error{error::end_of_range,
                             "End of format string, cannot peek"};
            }

            it = parse_code_point(it.value(), m_str.end(), cp);
            if (!it) {
                return it.error();
            }
            return {cp};
        }

        SCN_CONSTEXPR14 iterator begin() const noexcept
        {
            return m_str.begin();
        }
        SCN_CONSTEXPR14 iterator end() const noexcept
        {
            return m_str.end();
        }

        /**
         * Returns `true`, if `next_char() == '{'`
         */
        SCN_CONSTEXPR14 bool check_arg_begin() const
        {
            SCN_EXPECT(good());
            return next_char() == detail::ascii_widen<char_type>('{');
        }
        /**
         * Returns `true`, if `next_char() == '}'`
         */
        SCN_CONSTEXPR14 bool check_arg_end() const
        {
            SCN_EXPECT(good());
            return next_char() == detail::ascii_widen<char_type>('}');
        }

        using parse_context_base::check_arg_id;
        SCN_CONSTEXPR14 void check_arg_id(basic_string_view<CharT>) {}

        SCN_CONSTEXPR14 void arg_begin() const noexcept {}
        SCN_CONSTEXPR14 void arg_end() const noexcept {}

        SCN_CONSTEXPR14 void arg_handled() const noexcept {}

        const locale_type& locale() const
        {
            return m_locale;
        }

        /**
         * Parse `*this` using `s`
         */
        template <typename Scanner>
        error parse(Scanner& s)
        {
            return s.parse(*this);
        }

        bool has_arg_id()
        {
            SCN_EXPECT(good());
            if (m_str.size() == 1) {
                return true;
            }
            if (m_str[1] == detail::ascii_widen<char_type>('}')) {
                advance_char();
                return false;
            }
            if (m_str[1] == detail::ascii_widen<char_type>(':')) {
                advance_char(2);
                return false;
            }
            return true;
        }
        expected<string_view_type> parse_arg_id()
        {
            SCN_EXPECT(good());
            advance_char();
            if (SCN_UNLIKELY(!good())) {
                return error(error::invalid_format_string,
                             "Unexpected end of format argument");
            }
            auto it = m_str.begin();
            for (std::ptrdiff_t i = 0; good(); ++i, (void)advance_char()) {
                if (check_arg_end()) {
                    return string_view_type{
                        it,
                        static_cast<typename string_view_type::size_type>(i)};
                }
                if (next_char() == detail::ascii_widen<char_type>(':')) {
                    advance_char();
                    return string_view_type{
                        it,
                        static_cast<typename string_view_type::size_type>(i)};
                }
            }
            return error(error::invalid_format_string,
                         "Unexpected end of format argument");
        }

    private:
        string_view_type m_str;
        locale_type& m_locale;
    };

    template <typename CharT>
    class basic_empty_parse_context : public detail::parse_context_base {
    public:
        using char_type = CharT;
        using locale_type = basic_locale_ref<char_type>;
        using string_view_type = basic_string_view<char_type>;

        constexpr basic_empty_parse_context(int args,
                                            locale_type& loc,
                                            bool localized = false)
            : m_locale(loc), m_args_left(args), m_localized(localized)
        {
        }

        SCN_CONSTEXPR14 bool should_skip_ws()
        {
            if (m_should_skip_ws) {
                m_should_skip_ws = false;
                return true;
            }
            return false;
        }
        constexpr bool should_read_literal() const
        {
            return false;
        }
        constexpr bool check_literal(char_type) const
        {
            return false;
        }
        constexpr bool check_literal(span<const char_type>) const
        {
            return false;
        }
        constexpr bool check_literal_cp(code_point) const
        {
            return false;
        }

        constexpr bool good() const
        {
            return m_args_left > 0;
        }
        constexpr explicit operator bool() const
        {
            return good();
        }

        SCN_CONSTEXPR14 void advance_char(std::ptrdiff_t = 1) const noexcept {}
        SCN_CONSTEXPR14 error advance_cp() const noexcept
        {
            return {};
        }

        char_type next_char() const
        {
            SCN_EXPECT(false);
            SCN_UNREACHABLE;
        }
        expected<std::pair<code_point, std::ptrdiff_t>> next_cp() const
        {
            SCN_EXPECT(false);
            SCN_UNREACHABLE;
        }

        std::size_t chars_left() const noexcept
        {
            SCN_EXPECT(false);
            SCN_UNREACHABLE;
        }
        std::size_t cp_left() const noexcept
        {
            SCN_EXPECT(false);
            SCN_UNREACHABLE;
        }

        constexpr bool can_peek_char() const noexcept
        {
            return false;
        }
        constexpr bool can_peek_cp() const noexcept
        {
            return false;
        }

        char_type peek_char(std::ptrdiff_t = 1) const noexcept
        {
            SCN_EXPECT(false);
            SCN_UNREACHABLE;
        }
        expected<code_point> peek_cp() const noexcept
        {
            SCN_EXPECT(false);
            SCN_UNREACHABLE;
        }

        constexpr bool check_arg_begin() const
        {
            return true;
        }
        constexpr bool check_arg_end() const
        {
            return true;
        }

        using parse_context_base::check_arg_id;
        SCN_CONSTEXPR14 void check_arg_id(basic_string_view<CharT>) {}

        SCN_CONSTEXPR14 void arg_begin() const noexcept {}
        SCN_CONSTEXPR14 void arg_end() const noexcept {}

        SCN_CONSTEXPR14 void arg_handled()
        {
            m_should_skip_ws = true;
            --m_args_left;
        }

        const locale_type& locale() const
        {
            return m_locale;
        }

        template <typename Scanner>
        SCN_CONSTEXPR14 error parse(Scanner& s) const
        {
            if (m_localized) {
                s.make_localized();
            }
            return {};
        }

        constexpr bool has_arg_id() const
        {
            return false;
        }
        SCN_CONSTEXPR14 expected<string_view_type> parse_arg_id() const
        {
            SCN_EXPECT(good());
            return string_view_type{};
        }

        void reset_args_left(int n)
        {
            m_args_left = n;
            parse_context_base::m_next_arg_id = 0;
            m_should_skip_ws = false;
        }

    private:
        locale_type& m_locale;
        int m_args_left;
        bool m_localized;
        bool m_should_skip_ws{false};
    };

    namespace detail {
        template <typename CharT>
        basic_parse_context<CharT> make_parse_context_impl(
            basic_string_view<CharT> f,
            basic_locale_ref<CharT>& loc,
            bool)
        {
            return {f, loc};
        }
        template <typename CharT>
        basic_empty_parse_context<CharT> make_parse_context_impl(
            int i,
            basic_locale_ref<CharT>& loc,
            bool localized)
        {
            return {i, loc, localized};
        }

        template <typename CharT>
        struct parse_context_template_for_format<basic_string_view<CharT>> {
            template <typename T>
            using type = basic_parse_context<T>;
        };
        template <>
        struct parse_context_template_for_format<int> {
            template <typename CharT>
            using type = basic_empty_parse_context<CharT>;
        };

        template <typename F, typename CharT>
        auto make_parse_context(F f,
                                basic_locale_ref<CharT>& locale,
                                bool localized)
            -> decltype(make_parse_context_impl(f, locale, localized))
        {
            return make_parse_context_impl(f, locale, localized);
        }
    }  // namespace detail

    template <typename F, typename CharT>
    auto make_parse_context(F f, basic_locale_ref<CharT>& locale)
        -> decltype(detail::make_parse_context_impl(f, locale, false))
    {
        return detail::make_parse_context_impl(f, locale, false);
    }

    SCN_CLANG_POP  // -Wpadded

        SCN_END_NAMESPACE
}  // namespace scn

#endif  // SCN_DETAIL_PARSE_CONTEXT_H
