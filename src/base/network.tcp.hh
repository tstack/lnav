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

#ifndef lnav_network_tcp_hh
#define lnav_network_tcp_hh

#include <string>

#include "auto_fd.hh"
#include "result.h"

namespace network {

struct locality {
    locality(std::optional<std::string> username,
             std::string hostname,
             std::optional<std::string> service)
        : l_username(std::move(username)), l_hostname(std::move(hostname)),
          l_service(std::move(service))
    {
    }

    std::optional<std::string> l_username;
    std::string l_hostname;
    std::optional<std::string> l_service;
};

struct path {
    locality p_locality;
    std::string p_path;

    path(locality loc, std::string path)
        : p_locality(std::move(loc)), p_path(std::move(path))
    {
    }

    path home() const
    {
        return {
            this->p_locality,
            ".",
        };
    }
};

namespace tcp {

Result<auto_fd, std::string> connect(const char* hostname,
                                     const char* servname);

}
}  // namespace network

#endif
