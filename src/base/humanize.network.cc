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

#include "humanize.network.hh"

#include "config.h"
#include "pcrepp/pcrepp.hh"

namespace humanize {
namespace network {
namespace path {

nonstd::optional<::network::path>
from_str(const char* str)
{
    static const pcrepp REMOTE_PATTERN(
        "(?:(?<username>[\\w\\._\\-]+)@)?"
        "(?:\\[(?<ipv6>[^\\]]+)\\]|(?<hostname>[^\\[/:]+)):"
        "(?<path>.*)");

    pcre_context_static<30> pc;
    pcre_input pi(str);

    if (!REMOTE_PATTERN.match(pc, pi)) {
        return nonstd::nullopt;
    }

    const auto username = pi.get_substr_opt(pc["username"]);
    const auto ipv6 = pi.get_substr_opt(pc["ipv6"]);
    const auto hostname = pi.get_substr_opt(pc["hostname"]);
    const auto locality_hostname = ipv6 ? ipv6.value() : hostname.value();
    auto path = pi.get_substr(pc["path"]);

    if (path.empty()) {
        path = ".";
    }
    return ::network::path{
        {username, locality_hostname, nonstd::nullopt},
        path,
    };
}

}  // namespace path
}  // namespace network
}  // namespace humanize
