/**
 * Copyright (c) 2013, Timothy Stack
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
 *
 * @file nextwork-extension-functions.cc
 */

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>

#include "base/auto_mem.hh"
#include "config.h"
#include "sqlite-extension-func.hh"
#include "sqlite3.h"
#include "vtab_module.hh"

static std::string
sql_gethostbyname(const char* name_in)
{
    char buffer[INET6_ADDRSTRLEN];
    auto_mem<struct addrinfo> ai(freeaddrinfo);
    void* addr_ptr = nullptr;
    struct addrinfo hints;
    int rc;

    memset(&hints, 0, sizeof(hints));
    for (auto family : {AF_INET, AF_INET6}) {
        hints.ai_family = family;
        while ((rc = getaddrinfo(name_in, nullptr, &hints, ai.out()))
               == EAI_AGAIN)
        {
            sqlite3_sleep(10);
        }
        if (rc != 0) {
            return name_in;
        }

        switch (ai.in()->ai_family) {
            case AF_INET:
                addr_ptr = &((struct sockaddr_in*) ai.in()->ai_addr)->sin_addr;
                break;

            case AF_INET6:
                addr_ptr
                    = &((struct sockaddr_in6*) ai.in()->ai_addr)->sin6_addr;
                break;

            default:
                return name_in;
        }

        inet_ntop(ai.in()->ai_family, addr_ptr, buffer, sizeof(buffer));
        break;
    }

    return buffer;
}

static std::string
sql_gethostbyaddr(const char* addr_str)
{
    union {
        struct sockaddr_in sin;
        struct sockaddr_in6 sin6;
    } sa;
    char buffer[NI_MAXHOST];
    int family, socklen;
    char* addr_raw;
    int rc;

    memset(&sa, 0, sizeof(sa));
    if (strchr(addr_str, ':')) {
        family = AF_INET6;
        socklen = sizeof(struct sockaddr_in6);
        sa.sin6.sin6_family = family;
        addr_raw = (char*) &sa.sin6.sin6_addr;
    } else {
        family = AF_INET;
        socklen = sizeof(struct sockaddr_in);
        sa.sin.sin_family = family;
        addr_raw = (char*) &sa.sin.sin_addr;
    }

    if (inet_pton(family, addr_str, addr_raw) != 1) {
        return addr_str;
    }

    while ((rc = getnameinfo((struct sockaddr*) &sa,
                             socklen,
                             buffer,
                             sizeof(buffer),
                             NULL,
                             0,
                             0))
           == EAI_AGAIN)
    {
        sqlite3_sleep(10);
    }

    if (rc != 0) {
        return addr_str;
    }

    return buffer;
}

int
network_extension_functions(struct FuncDef** basic_funcs,
                            struct FuncDefAgg** agg_funcs)
{
    static struct FuncDef network_funcs[] = {
        sqlite_func_adapter<decltype(&sql_gethostbyname), sql_gethostbyname>::
            builder(
                help_text("gethostbyname",
                          "Get the IP address for the given hostname")
                    .sql_function()
                    .with_prql_path({"net", "gethostbyname"})
                    .with_parameter({"hostname", "The DNS hostname to lookup."})
                    .with_tags({"net"})
                    .with_example({
                        "To get the IP address for 'localhost'",
                        "SELECT gethostbyname('localhost')",
                    })),

        sqlite_func_adapter<decltype(&sql_gethostbyaddr), sql_gethostbyaddr>::
            builder(
                help_text("gethostbyaddr",
                          "Get the hostname for the given IP address")
                    .sql_function()
                    .with_prql_path({"net", "gethostbyaddr"})
                    .with_parameter({"hostname", "The IP address to lookup."})
                    .with_tags({"net"})
                    .with_example({
                        "To get the hostname for the IP '127.0.0.1'",
                        "SELECT gethostbyaddr('127.0.0.1')",
                    })),

        {nullptr},
    };

    *basic_funcs = network_funcs;

    return SQLITE_OK;
}
