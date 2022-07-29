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

#ifndef SCN_SCAN_VSCAN_H
#define SCN_SCAN_VSCAN_H

#include "../detail/context.h"
#include "../detail/file.h"
#include "../detail/parse_context.h"
#include "../detail/visitor.h"
#include "common.h"

namespace scn {
    SCN_BEGIN_NAMESPACE

    // Avoid documentation issues: without this, Doxygen will think
    // SCN_BEGIN_NAMESPACE is a part of the vscan declaration
    namespace dummy {
    }

    /**
     * Type returned by `vscan` and others
     */
    template <typename WrappedRange>
    struct vscan_result {
        error err;
        WrappedRange range;
    };

    namespace detail {
        template <typename WrappedRange,
                  typename CharT = typename WrappedRange::char_type>
        vscan_result<WrappedRange> vscan_boilerplate(
            WrappedRange&& r,
            basic_string_view<CharT> fmt,
            basic_args<CharT> args)
        {
            auto ctx = make_context(SCN_MOVE(r));
            auto pctx = make_parse_context(fmt, ctx.locale());
            auto err = visit(ctx, pctx, SCN_MOVE(args));
            return {err, SCN_MOVE(ctx.range())};
        }

        template <typename WrappedRange,
                  typename CharT = typename WrappedRange::char_type>
        vscan_result<WrappedRange> vscan_boilerplate_default(
            WrappedRange&& r,
            int n_args,
            basic_args<CharT> args)
        {
            auto ctx = make_context(SCN_MOVE(r));
            auto pctx = make_parse_context(n_args, ctx.locale());
            auto err = visit(ctx, pctx, SCN_MOVE(args));
            return {err, SCN_MOVE(ctx.range())};
        }

        template <typename WrappedRange,
                  typename Format,
                  typename CharT = typename WrappedRange::char_type>
        vscan_result<WrappedRange> vscan_boilerplate_localized(
            WrappedRange&& r,
            basic_locale_ref<CharT>&& loc,
            const Format& fmt,
            basic_args<CharT> args)
        {
            auto ctx = make_context(SCN_MOVE(r), SCN_MOVE(loc));
            auto pctx = make_parse_context(fmt, ctx.locale());
            auto err = visit(ctx, pctx, SCN_MOVE(args));
            return {err, SCN_MOVE(ctx.range())};
        }
    }  // namespace detail

    /**
     * In the spirit of {fmt}/`std::format` and `vformat`, `vscan` behaves
     * similarly to \ref scan, except instead of taking a variadic argument
     * pack, it takes an object of type `basic_args`, which type-erases the
     * arguments to scan. This, in effect, will decrease generated code size and
     * compile times dramatically.
     *
     * \param range Source range that has been wrapped with `detail::wrap`, and
     * passed in as an rvalue.
     * \param fmt Format string to use
     * \param args Type-erased values to read
     */
    template <typename WrappedRange,
              typename CharT = typename WrappedRange::char_type>
    vscan_result<WrappedRange> vscan(WrappedRange range,
                                     basic_string_view<CharT> fmt,
                                     basic_args<CharT>&& args)
    {
        return detail::vscan_boilerplate(SCN_MOVE(range), fmt, SCN_MOVE(args));
    }

    /**
     * To be used with `scan_default`
     *
     * \param range Source range that has been wrapped with `detail::wrap`, and
     * passed in as an rvalue.
     * \param n_args Number of arguments in args
     * \param args Type-erased values to read
     *
     * \see vscan
     */
    template <typename WrappedRange,
              typename CharT = typename WrappedRange::char_type>
    vscan_result<WrappedRange> vscan_default(WrappedRange range,
                                             int n_args,
                                             basic_args<CharT>&& args)
    {
        return detail::vscan_boilerplate_default(SCN_MOVE(range), n_args,
                                                 SCN_MOVE(args));
    }

    /**
     * To be used with `scan_localized`
     *
     * \param loc Locale to use
     * \param range Source range that has been wrapped with `detail::wrap`, and
     * passed in as an rvalue.
     * \param fmt Format string to use
     * \param args Type-erased values to read
     *
     * \see vscan
     */
    template <typename WrappedRange,
              typename CharT = typename WrappedRange::char_type>
    vscan_result<WrappedRange> vscan_localized(WrappedRange range,
                                               basic_locale_ref<CharT>&& loc,
                                               basic_string_view<CharT> fmt,
                                               basic_args<CharT>&& args)
    {
        return detail::vscan_boilerplate_localized(
            SCN_MOVE(range), SCN_MOVE(loc), fmt, SCN_MOVE(args));
    }

    /**
     * \see scan_usertype
     * \see vscan
     */
    template <typename WrappedRange,
              typename CharT = typename WrappedRange::char_type>
    error vscan_usertype(basic_context<WrappedRange>& ctx,
                         basic_string_view<CharT> f,
                         basic_args<CharT>&& args)
    {
        auto pctx = make_parse_context(f, ctx.locale());
        return visit(ctx, pctx, SCN_MOVE(args));
    }

#if !defined(SCN_HEADER_ONLY) || !SCN_HEADER_ONLY

#define SCN_VSCAN_DECLARE(Range, WrappedAlias, CharAlias)                   \
    namespace detail {                                                      \
        namespace vscan_macro {                                             \
            using WrappedAlias = range_wrapper_for_t<Range>;                \
            using CharAlias = typename WrappedAlias::char_type;             \
        }                                                                   \
    }                                                                       \
    vscan_result<detail::vscan_macro::WrappedAlias> vscan(                  \
        detail::vscan_macro::WrappedAlias&&,                                \
        basic_string_view<detail::vscan_macro::CharAlias>,                  \
        basic_args<detail::vscan_macro::CharAlias>&&);                      \
                                                                            \
    vscan_result<detail::vscan_macro::WrappedAlias> vscan_default(          \
        detail::vscan_macro::WrappedAlias&&, int,                           \
        basic_args<detail::vscan_macro::CharAlias>&&);                      \
                                                                            \
    vscan_result<detail::vscan_macro::WrappedAlias> vscan_localized(        \
        detail::vscan_macro::WrappedAlias&&,                                \
        basic_locale_ref<detail::vscan_macro::CharAlias>&&,                 \
        basic_string_view<detail::vscan_macro::CharAlias>,                  \
        basic_args<detail::vscan_macro::CharAlias>&&);                      \
                                                                            \
    error vscan_usertype(basic_context<detail::vscan_macro::WrappedAlias>&, \
                         basic_string_view<detail::vscan_macro::CharAlias>, \
                         basic_args<detail::vscan_macro::CharAlias>&&)

    SCN_VSCAN_DECLARE(string_view, string_view_wrapped, string_view_char);
    SCN_VSCAN_DECLARE(wstring_view, wstring_view_wrapped, wstring_view_char);
    SCN_VSCAN_DECLARE(std::string, string_wrapped, string_char);
    SCN_VSCAN_DECLARE(std::wstring, wstring_wrapped, wstring_char);
    SCN_VSCAN_DECLARE(file&, file_ref_wrapped, file_ref_char);
    SCN_VSCAN_DECLARE(wfile&, wfile_ref_wrapped, wfile_ref_char);

#endif  // !SCN_HEADER_ONLY

    SCN_END_NAMESPACE
}  // namespace scn

#if defined(SCN_HEADER_ONLY) && SCN_HEADER_ONLY && !defined(SCN_VSCAN_CPP)
#include "vscan.cpp"
#endif

#endif  // SCN_SCAN_VSCAN_H
