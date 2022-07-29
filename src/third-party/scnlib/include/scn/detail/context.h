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

#ifndef SCN_DETAIL_CONTEXT_H
#define SCN_DETAIL_CONTEXT_H

#include "args.h"

namespace scn {
    SCN_BEGIN_NAMESPACE

    template <typename WrappedRange>
    class basic_context {
    public:
        using range_type = WrappedRange;
        using iterator = typename range_type::iterator;
        using sentinel = typename range_type::sentinel;
        using char_type = typename range_type::char_type;
        using locale_type = basic_locale_ref<char_type>;

        basic_context(range_type&& r) : m_range(SCN_MOVE(r)) {}
        basic_context(range_type&& r, locale_type&& loc)
            : m_range(SCN_MOVE(r)), m_locale(SCN_MOVE(loc))
        {
        }

        SCN_NODISCARD iterator& begin()
        {
            return m_range.begin();
        }
        const sentinel& end() const
        {
            return m_range.end();
        }

        range_type& range() & noexcept
        {
            return m_range;
        }
        const range_type& range() const& noexcept
        {
            return m_range;
        }
        range_type range() && noexcept
        {
            return m_range;
        }

        locale_type& locale() noexcept
        {
            return m_locale;
        }
        const locale_type& locale() const noexcept
        {
            return m_locale;
        }

    private:
        range_type m_range;
        locale_type m_locale{};
    };

    template <typename WrappedRange,
              typename CharT = typename WrappedRange::char_type>
    basic_context<WrappedRange> make_context(WrappedRange r)
    {
        return {SCN_MOVE(r)};
    }
    template <typename WrappedRange, typename LocaleRef>
    basic_context<WrappedRange> make_context(WrappedRange r, LocaleRef&& loc)
    {
        return {SCN_MOVE(r), SCN_FWD(loc)};
    }

    template <typename CharT>
    auto get_arg(const basic_args<CharT>& args, std::ptrdiff_t id)
        -> expected<basic_arg<CharT>>
    {
        auto a = args.get(id);
        if (!a) {
            return error(error::invalid_format_string,
                         "Argument id out of range");
        }
        return a;
    }
    template <typename CharT, typename ParseCtx>
    auto get_arg(const basic_args<CharT>& args,
                 ParseCtx& pctx,
                 std::ptrdiff_t id) -> expected<basic_arg<CharT>>
    {
        return pctx.check_arg_id(id) ? get_arg(args, id)
                                     : error(error::invalid_format_string,
                                             "Argument id out of range");
    }
    template <typename CharT, typename ParseCtx>
    auto get_arg(const basic_args<CharT>&, ParseCtx&, basic_string_view<CharT>)
        -> expected<basic_arg<CharT>>
    {
        return error(error::invalid_format_string, "Argument id out of range");
    }

    template <typename CharT, typename ParseCtx>
    auto next_arg(const basic_args<CharT>& args, ParseCtx& pctx)
        -> expected<basic_arg<CharT>>
    {
        return get_arg(args, pctx.next_arg_id());
    }

    SCN_END_NAMESPACE
}  // namespace scn

#endif  // SCN_DETAIL_CONTEXT_H
