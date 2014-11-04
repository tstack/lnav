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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file nextwork-extension-functions.cc
 */

#include "config.h"

#include <stdio.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "sqlite3.h"

#include "sqlite-extension-func.h"

static void sql_gethostbyname(sqlite3_context *context,
                              int argc, sqlite3_value **argv)
{
    char             buffer[INET6_ADDRSTRLEN];
    const char *     name_in;
    struct addrinfo *ai;
    void *           addr_ptr = NULL;
    int rc;

    assert(argc >= 1 && argc <= 2);

    if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
        sqlite3_result_null(context);
        return;
    }

    name_in = (const char *)sqlite3_value_text(argv[0]);
    while ((rc = getaddrinfo(name_in, NULL, NULL, &ai)) == EAI_AGAIN) {
        sqlite3_sleep(10);
    }
    if (rc != 0) {
        sqlite3_result_text(context, name_in, -1, SQLITE_TRANSIENT);
        return;
    }

    switch (ai->ai_family) {
    case AF_INET:
        addr_ptr = &((struct sockaddr_in *)ai->ai_addr)->sin_addr;
        break;

    case AF_INET6:
        addr_ptr = &((struct sockaddr_in6 *)ai->ai_addr)->sin6_addr;
        break;

        default:
            sqlite3_result_error(context, "unknown address family", -1);
            return;
    }

    inet_ntop(ai->ai_family, addr_ptr, buffer, sizeof(buffer));

    sqlite3_result_text(context, buffer, -1, SQLITE_TRANSIENT);

    freeaddrinfo(ai);
}

static void sql_gethostbyaddr(sqlite3_context *context,
                              int argc, sqlite3_value **argv)
{
    union {
        struct sockaddr_in  sin;
        struct sockaddr_in6 sin6;
    }           sa;
    const char *addr_str;
    char        buffer[NI_MAXHOST];
    int         family, socklen;
    char *      addr_raw;
    int         rc;

    assert(argc == 1);

    if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
        sqlite3_result_null(context);
        return;
    }

    addr_str = (const char *)sqlite3_value_text(argv[0]);

    memset(&sa, 0, sizeof(sa));
    if (strchr(addr_str, ':')) {
        family              = AF_INET6;
        socklen             = sizeof(struct sockaddr_in6);
        sa.sin6.sin6_family = family;
        addr_raw            = (char *)&sa.sin6.sin6_addr;
    }
    else {
        family            = AF_INET;
        socklen           = sizeof(struct sockaddr_in);
        sa.sin.sin_family = family;
        addr_raw          = (char *)&sa.sin.sin_addr;
    }

    if (inet_pton(family, addr_str, addr_raw) != 1) {
        sqlite3_result_text(context, addr_str, -1, SQLITE_TRANSIENT);
        return;
    }

    while ((rc = getnameinfo((struct sockaddr *)&sa, socklen,
                             buffer, sizeof(buffer), NULL, 0,
                             0)) == EAI_AGAIN) {
        sqlite3_sleep(10);
    }

    if (rc != 0) {
        sqlite3_result_text(context, addr_str, -1, SQLITE_TRANSIENT);
        return;
    }

    sqlite3_result_text(context, buffer, -1, SQLITE_TRANSIENT);
}

int network_extension_functions(const struct FuncDef **basic_funcs,
                                const struct FuncDefAgg **agg_funcs)
{
    static const struct FuncDef network_funcs[] = {
        { "gethostbyname", 1, 0, SQLITE_UTF8, 0, sql_gethostbyname },
        { "gethostbyaddr", 1, 0, SQLITE_UTF8, 0, sql_gethostbyaddr },

        { NULL }
    };

    *basic_funcs = network_funcs;

    return SQLITE_OK;
}
