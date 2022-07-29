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

#ifndef SCN_DETAIL_VISITOR_H
#define SCN_DETAIL_VISITOR_H

#include "../reader/reader.h"

namespace scn {
    SCN_BEGIN_NAMESPACE

    template <typename Context, typename ParseCtx>
    class basic_visitor {
    public:
        using context_type = Context;
        using char_type = typename Context::char_type;
        using arg_type = basic_arg<char_type>;

        basic_visitor(Context& ctx, ParseCtx& pctx)
            : m_ctx(std::addressof(ctx)), m_pctx(std::addressof(pctx))
        {
        }

        template <typename T>
        auto operator()(T&& val) -> error
        {
            return visit(SCN_FWD(val), detail::priority_tag<1>{});
        }

    private:
        auto visit(code_point& val, detail::priority_tag<1>) -> error
        {
            return detail::visitor_boilerplate<detail::code_point_scanner>(
                val, *m_ctx, *m_pctx);
        }
        auto visit(span<char_type>& val, detail::priority_tag<1>) -> error
        {
            return detail::visitor_boilerplate<detail::span_scanner>(
                val, *m_ctx, *m_pctx);
        }
        auto visit(bool& val, detail::priority_tag<1>) -> error
        {
            return detail::visitor_boilerplate<detail::bool_scanner>(
                val, *m_ctx, *m_pctx);
        }

#define SCN_VISIT_INT(T)                                                \
    error visit(T& val, detail::priority_tag<0>)                        \
    {                                                                   \
        return detail::visitor_boilerplate<detail::integer_scanner<T>>( \
            val, *m_ctx, *m_pctx);                                      \
    }
        SCN_VISIT_INT(signed char)
        SCN_VISIT_INT(short)
        SCN_VISIT_INT(int)
        SCN_VISIT_INT(long)
        SCN_VISIT_INT(long long)
        SCN_VISIT_INT(unsigned char)
        SCN_VISIT_INT(unsigned short)
        SCN_VISIT_INT(unsigned int)
        SCN_VISIT_INT(unsigned long)
        SCN_VISIT_INT(unsigned long long)
        SCN_VISIT_INT(char_type)
#undef SCN_VISIT_INT

#define SCN_VISIT_FLOAT(T)                                            \
    error visit(T& val, detail::priority_tag<1>)                      \
    {                                                                 \
        return detail::visitor_boilerplate<detail::float_scanner<T>>( \
            val, *m_ctx, *m_pctx);                                    \
    }
        SCN_VISIT_FLOAT(float)
        SCN_VISIT_FLOAT(double)
        SCN_VISIT_FLOAT(long double)
#undef SCN_VISIT_FLOAT

        auto visit(std::basic_string<char_type>& val, detail::priority_tag<1>)
            -> error
        {
            return detail::visitor_boilerplate<detail::string_scanner>(
                val, *m_ctx, *m_pctx);
        }
        auto visit(basic_string_view<char_type>& val, detail::priority_tag<1>)
            -> error
        {
            return detail::visitor_boilerplate<detail::string_view_scanner>(
                val, *m_ctx, *m_pctx);
        }
        auto visit(typename arg_type::handle val, detail::priority_tag<1>)
            -> error
        {
            return val.scan(*m_ctx, *m_pctx);
        }
        [[noreturn]] auto visit(detail::monostate, detail::priority_tag<0>)
            -> error
        {
            SCN_UNREACHABLE;
        }

        Context* m_ctx;
        ParseCtx* m_pctx;
    };

    template <typename Context, typename ParseCtx>
    error visit(Context& ctx,
                ParseCtx& pctx,
                basic_args<typename Context::char_type> args)
    {
        using char_type = typename Context::char_type;
        using arg_type = basic_arg<char_type>;
        auto arg = arg_type{};

        while (pctx) {
            if (pctx.should_skip_ws()) {
                // Skip whitespace from format string and from stream
                // EOF is not an error
                auto ret = skip_range_whitespace(ctx, false);
                if (SCN_UNLIKELY(!ret)) {
                    if (ret == error::end_of_range) {
                        break;
                    }
                    SCN_CLANG_PUSH_IGNORE_UNDEFINED_TEMPLATE
                    auto rb = ctx.range().reset_to_rollback_point();
                    if (!rb) {
                        return rb;
                    }
                    return ret;
                }
                // Don't advance pctx, should_skip_ws() does it for us
                continue;
            }

            // Non-brace character, or
            // Brace followed by another brace, meaning a literal '{'
            if (pctx.should_read_literal()) {
                if (SCN_UNLIKELY(!pctx)) {
                    return {error::invalid_format_string,
                            "Unexpected end of format string"};
                }
                // Check for any non-specifier {foo} characters
                alignas(typename Context::char_type) unsigned char buf[4] = {0};
                auto ret = read_code_point(ctx.range(), make_span(buf, 4));
                SCN_CLANG_POP_IGNORE_UNDEFINED_TEMPLATE
                if (!ret || !pctx.check_literal(ret.value().chars)) {
                    auto rb = ctx.range().reset_to_rollback_point();
                    if (!rb) {
                        // Failed rollback
                        return rb;
                    }
                    if (!ret) {
                        // Failed read
                        return ret.error();
                    }

                    // Mismatching characters in scan string and stream
                    return {error::invalid_scanned_value,
                            "Expected character from format string not "
                            "found in the stream"};
                }
                // Bump pctx to next char
                if (!pctx.advance_cp()) {
                    pctx.advance_char();
                }
            }
            else {
                // Scan argument
                auto arg_wrapped = [&]() -> expected<arg_type> {
                    if (!pctx.has_arg_id()) {
                        return next_arg(args, pctx);
                    }
                    auto id_wrapped = pctx.parse_arg_id();
                    if (!id_wrapped) {
                        return id_wrapped.error();
                    }
                    auto id = id_wrapped.value();
                    SCN_ENSURE(!id.empty());
                    if (ctx.locale().get_static().is_digit(id.front())) {
                        auto s =
                            detail::simple_integer_scanner<std::ptrdiff_t>{};
                        std::ptrdiff_t i{0};
                        auto span = make_span(id.data(), id.size());
                        SCN_CLANG_PUSH_IGNORE_UNDEFINED_TEMPLATE
                        auto ret = s.scan(span, i, 10);
                        SCN_CLANG_POP_IGNORE_UNDEFINED_TEMPLATE
                        if (!ret || ret.value() != span.end()) {
                            return error(error::invalid_format_string,
                                         "Failed to parse argument id from "
                                         "format string");
                        }
                        return get_arg(args, pctx, i);
                    }
                    return get_arg(args, pctx, id);
                }();
                if (!arg_wrapped) {
                    return arg_wrapped.error();
                }
                arg = arg_wrapped.value();
                SCN_ENSURE(arg);
                if (!pctx) {
                    return {error::invalid_format_string,
                            "Unexpected end of format argument"};
                }
                auto ret = visit_arg<char_type>(
                    basic_visitor<Context, ParseCtx>(ctx, pctx), arg);
                if (!ret) {
                    auto rb = ctx.range().reset_to_rollback_point();
                    if (!rb) {
                        return rb;
                    }
                    return ret;
                }
                // Handle next arg and bump pctx
                pctx.arg_handled();
                if (pctx) {
                    auto e = pctx.advance_cp();
                    if (!e) {
                        return e;
                    }
                }
            }
        }
        if (pctx) {
            // Format string not exhausted
            return {error::invalid_format_string,
                    "Format string not exhausted"};
        }
        ctx.range().set_rollback_point();
        return {};
    }

    SCN_END_NAMESPACE
}  // namespace scn

#endif  // SCN_DETAIL_VISITOR_H
