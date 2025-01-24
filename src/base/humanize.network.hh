/**
 * Copyright (c) 2021, Timothy Stack
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
 */

#ifndef lnav_humanize_network_hh
#define lnav_humanize_network_hh

#include <string>

#include "fmt/format.h"
#include "intern_string.hh"
#include "network.tcp.hh"

namespace fmt {

template<>
struct formatter<network::locality> {
    constexpr auto parse(format_parse_context& ctx)
    {
        const auto it = ctx.begin();
        const auto end = ctx.end();

        // Check if reached the end of the range:
        if (it != end && *it != '}') {
            throw format_error("invalid format");
        }

        // Return an iterator past the end of the parsed range:
        return it;
    }

    template<typename FormatContext>
    auto format(const network::locality& l, FormatContext& ctx)
    {
        bool is_ipv6 = l.l_hostname.find(':') != std::string::npos;

        return format_to(ctx.out(),
                         "{}{}{}{}{}",
                         l.l_username.value_or(std::string()),
                         l.l_username ? "@" : "",
                         is_ipv6 ? "[" : "",
                         l.l_hostname,
                         is_ipv6 ? "]" : "");
    }
};

template<>
struct formatter<network::path> {
    constexpr auto parse(format_parse_context& ctx)
    {
        const auto it = ctx.begin();
        const auto end = ctx.end();

        // Check if reached the end of the range:
        if (it != end && *it != '}') {
            throw format_error("invalid format");
        }

        // Return an iterator past the end of the parsed range:
        return it;
    }

    template<typename FormatContext>
    auto format(const network::path& p, FormatContext& ctx)
    {
        return format_to(
            ctx.out(), "{}:{}", p.p_locality, p.p_path == "." ? "" : p.p_path);
    }
};

}  // namespace fmt

namespace humanize::network::path {

std::optional<::network::path> from_str(string_fragment sf);

}

#endif
