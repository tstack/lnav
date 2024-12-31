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

#pragma once

#include <scn/scan.h>

#if !SCN_DISABLE_IOSTREAM

#include <ios>
#include <streambuf>

namespace scn {
SCN_BEGIN_NAMESPACE

namespace detail {
template <typename T, typename CharT, typename Enable = void>
struct is_streamable_impl : std::false_type {};

template <typename T, typename CharT>
struct is_streamable_impl<
    T,
    CharT,
    std::enable_if_t<sizeof(SCN_DECLVAL(std::basic_istream<CharT>&)
                            << std::declval<T>()) != 0>> : std::true_type {};

template <typename CharT>
struct dummy_context_for_is_streamamble {
    using char_type = CharT;
};

template <typename T, typename CharT>
struct is_streamable
    : std::conditional_t<
          std::is_convertible_v<
              std::add_lvalue_reference_t<
                  decltype(arg_mapper<dummy_context_for_is_streamamble<CharT>>::
                               map(SCN_DECLVAL(T&)))>,
              unscannable&>,
          is_streamable_impl<T, CharT>,
          std::false_type> {};

/**
 * Wraps `SourceRange`, and makes it a `std::basic_streambuf`.
 *
 * Used by `basic_istream_scanner`.
 */
template <typename SourceRange>
class basic_range_streambuf
    : public std::basic_streambuf<detail::char_t<SourceRange>> {
    using base = std::basic_streambuf<detail::char_t<SourceRange>>;

public:
    using range_type = SourceRange;
    using iterator = ranges::iterator_t<SourceRange>;
    using char_type = typename base::char_type;
    using traits_type = typename base::traits_type;
    using int_type = typename base::int_type;

    explicit basic_range_streambuf(range_type range)
        : m_range(range), m_begin(ranges::begin(m_range)), m_begin_prev(m_begin)
    {
    }

    iterator begin() const
        noexcept(std::is_nothrow_copy_constructible_v<iterator>)
    {
        return m_begin;
    }
    iterator begin_prev() const
        noexcept(std::is_nothrow_copy_constructible_v<iterator>)
    {
        return m_begin_prev;
    }
    int_type last_char() const noexcept
    {
        return m_ch;
    }

private:
    int_type underflow() override
    {
        // Already read
        if (!traits_type::eq_int_type(m_ch, traits_type::eof())) {
            return m_ch;
        }

        if (m_begin == ranges::end(m_range)) {
            return traits_type::eof();
        }
        m_begin_prev = m_begin;
        SCN_CLANG_PUSH_IGNORE_UNSAFE_BUFFER_USAGE
        m_ch = traits_type::to_int_type(*m_begin++);
        SCN_CLANG_POP_IGNORE_UNSAFE_BUFFER_USAGE
        return m_ch;
    }

    int_type uflow() override
    {
        auto ret = underflow();
        if (ret != traits_type::eof()) {
            m_ch = traits_type::eof();
        }
        return ret;
    }

    std::streamsize showmanyc() override
    {
        return traits_type::eq_int_type(m_ch, traits_type::eof()) ? 0 : 1;
    }

    int_type pbackfail(int_type c) override
    {
        SCN_EXPECT(traits_type::eq_int_type(c, traits_type::eof()));
        SCN_EXPECT(!m_has_put_back);
        m_has_put_back = true;

        m_begin = m_begin_prev;

        if (m_begin == ranges::end(m_range)) {
            return traits_type::eof();
        }
        return traits_type::to_int_type(0);
    }

    range_type m_range;
    iterator m_begin;
    iterator m_begin_prev;
    int_type m_ch{traits_type::eof()};
    bool m_has_put_back{false};
};

using range_streambuf = basic_range_streambuf<scan_context::range_type>;
using wrange_streambuf = basic_range_streambuf<wscan_context::range_type>;
}  // namespace detail

/**
 * Implements the `scn::scanner` interface, by reading the value with
 * `operator>>`.
 *
 * Example:
 *
 * \code{.cpp}
 * #include <scn/istream.h> // required for basic_istream_scanner
 *
 * struct mytype {
 *   friend std::istream& operator>>(std::istream&, const mytype&);
 * };
 *
 * // Use mytype::operator>> for scanning with scnlib
 * template <typename CharT>
 * struct scn::scanner<mytype> : scn::basic_istream_scanner<CharT> {};
 *
 * auto [result, myvalue] = scn::scan<mytype>(...);
 * \endcode
 */
template <typename CharT>
struct basic_istream_scanner {
    template <typename ParseContext>
    typename ParseContext::iterator parse(ParseContext& ctx)
    {
        return ctx.begin();
    }

    template <typename T, typename Context>
    scan_expected<typename Context::iterator> scan(T& val, Context& ctx) const
    {
        detail::basic_range_streambuf<typename Context::range_type> streambuf(
            ctx.range());
        using traits = typename decltype(streambuf)::traits_type;
        std::basic_istream<CharT> stream(std::addressof(streambuf));

        if (!(stream >> val)) {
            if (stream.eof()) {
                return detail::unexpected_scan_error(scan_error::end_of_input,
                                                     "EOF");
            }
            if (SCN_UNLIKELY(stream.bad())) {
                return detail::unexpected_scan_error(
                    scan_error::invalid_source_state,
                    "Bad std::istream after reading");
            }

            SCN_UNLIKELY_ATTR
            return detail::unexpected_scan_error(
                scan_error::invalid_scanned_value,
                "Failed to read with std::istream");
        }

        if (traits::eq_int_type(streambuf.last_char(), traits::eof())) {
            return streambuf.begin();
        }
        return streambuf.begin_prev();
    }
};

SCN_END_NAMESPACE
}  // namespace scn

#endif  // SCN_USE_IOSTREAMS
