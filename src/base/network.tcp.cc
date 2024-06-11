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

#include "network.tcp.hh"

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "auto_mem.hh"
#include "config.h"
#include "fmt/format.h"

namespace network {
namespace tcp {

Result<auto_fd, std::string>
connect(const char* hostname, const char* servname)
{
    struct addrinfo hints;
    auto_mem<addrinfo> ai(freeaddrinfo);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    auto rc = getaddrinfo(hostname, servname, &hints, ai.out());

    if (rc != 0) {
        return Err(fmt::format(FMT_STRING("unable to resolve {}:{} -- {}"),
                               hostname,
                               servname,
                               gai_strerror(rc)));
    }

    auto retval = auto_fd(socket(ai->ai_family, ai->ai_socktype, 0));

    rc = ::connect(retval, ai->ai_addr, ai->ai_addrlen);
    if (rc != 0) {
        return Err(fmt::format(FMT_STRING("unable to connect to {}:{} -- {}"),
                               hostname,
                               servname,
                               strerror(errno)));
    }

    return Ok(std::move(retval));
}

}  // namespace tcp
}  // namespace network
