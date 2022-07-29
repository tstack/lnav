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
#define SCN_VSCAN_CPP
#endif

#include <scn/scan/vscan.h>

#include <scn/detail/context.h>
#include <scn/detail/parse_context.h>
#include <scn/detail/visitor.h>

namespace scn {
    SCN_BEGIN_NAMESPACE

#if SCN_INCLUDE_SOURCE_DEFINITIONS

#define SCN_VSCAN_DEFINE(Range, WrappedAlias, CharAlias)                  \
    vscan_result<detail::vscan_macro::WrappedAlias> vscan(                \
        detail::vscan_macro::WrappedAlias&& range,                        \
        basic_string_view<detail::vscan_macro::CharAlias> fmt,            \
        basic_args<detail::vscan_macro::CharAlias>&& args)                \
    {                                                                     \
        return detail::vscan_boilerplate(SCN_MOVE(range), fmt,            \
                                         SCN_MOVE(args));                 \
    }                                                                     \
                                                                          \
    vscan_result<detail::vscan_macro::WrappedAlias> vscan_default(        \
        detail::vscan_macro::WrappedAlias&& range, int n_args,            \
        basic_args<detail::vscan_macro::CharAlias>&& args)                \
    {                                                                     \
        return detail::vscan_boilerplate_default(SCN_MOVE(range), n_args, \
                                                 SCN_MOVE(args));         \
    }                                                                     \
                                                                          \
    vscan_result<detail::vscan_macro::WrappedAlias> vscan_localized(      \
        detail::vscan_macro::WrappedAlias&& range,                        \
        basic_locale_ref<detail::vscan_macro::CharAlias>&& loc,           \
        basic_string_view<detail::vscan_macro::CharAlias> fmt,            \
        basic_args<detail::vscan_macro::CharAlias>&& args)                \
    {                                                                     \
        return detail::vscan_boilerplate_localized(                       \
            SCN_MOVE(range), SCN_MOVE(loc), fmt, SCN_MOVE(args));         \
    }                                                                     \
                                                                          \
    error vscan_usertype(                                                 \
        basic_context<detail::vscan_macro::WrappedAlias>& ctx,            \
        basic_string_view<detail::vscan_macro::CharAlias> f,              \
        basic_args<detail::vscan_macro::CharAlias>&& args)                \
    {                                                                     \
        auto pctx = make_parse_context(f, ctx.locale());                  \
        return visit(ctx, pctx, SCN_MOVE(args));                          \
    }

    SCN_VSCAN_DEFINE(string_view, string_view_wrapped, string_view_char)
    SCN_VSCAN_DEFINE(wstring_view, wstring_view_wrapped, wstring_view_char)
    SCN_VSCAN_DEFINE(std::string, string_wrapped, string_char)
    SCN_VSCAN_DEFINE(std::wstring, wstring_wrapped, wstring_char)
    SCN_VSCAN_DEFINE(file&, file_ref_wrapped, file_ref_char)
    SCN_VSCAN_DEFINE(wfile&, wfile_ref_wrapped, wfile_ref_char)

#endif

    SCN_END_NAMESPACE
}  // namespace scn
