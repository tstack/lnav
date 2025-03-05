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
 * @file logfile_sub_source.hh
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sqlite3.h>
#include <string.h>
#include <sys/socket.h>

#include "base/humanize.hh"
#include "base/intern_string.hh"
#include "base/strnatcmp.h"
#include "config.h"
#include "log_level.hh"

constexpr int MAX_ADDR_LEN = 128;

static int
try_inet_pton(int p_len, const char* p, char* n)
{
    static int ADDR_FAMILIES[] = {AF_INET, AF_INET6};

    char buf[MAX_ADDR_LEN + 1];
    int retval = AF_MAX;

    strncpy(buf, p, p_len);
    buf[p_len] = '\0';
    for (int family : ADDR_FAMILIES) {
        if (inet_pton(family, buf, n) == 1) {
            retval = family;
            break;
        }
    }

    return retval;
}

static int
convert_v6_to_v4(int family, char* n)
{
    auto* ia = (struct in6_addr*) n;

    if (family == AF_INET6
        && (IN6_IS_ADDR_V4COMPAT(ia) || IN6_IS_ADDR_V4MAPPED(ia)))
    {
        family = AF_INET;
        memmove(n, n + 12, sizeof(struct in_addr));
    }

    return family;
}

static int
ipaddress(void* ptr, int a_len, const void* a_in, int b_len, const void* b_in)
{
    char a_addr[sizeof(struct in6_addr)], b_addr[sizeof(struct in6_addr)];
    const char *a_str = (const char*) a_in, *b_str = (const char*) b_in;
    int a_family, b_family, retval;

    if ((a_len > MAX_ADDR_LEN) || (b_len > MAX_ADDR_LEN)) {
        return strnatcasecmp(a_len, a_str, b_len, b_str);
    }

    int v4res = 0;
    if (ipv4cmp(a_len, a_str, b_len, b_str, &v4res)) {
        return v4res;
    }

    a_family = try_inet_pton(a_len, a_str, a_addr);
    b_family = try_inet_pton(b_len, b_str, b_addr);

    if (a_family == AF_MAX && b_family == AF_MAX) {
        return strnatcasecmp(a_len, a_str, b_len, b_str);
    } else if (a_family == AF_MAX && b_family != AF_MAX) {
        retval = -1;
    } else if (a_family != AF_MAX && b_family == AF_MAX) {
        retval = 1;
    } else {
        a_family = convert_v6_to_v4(a_family, a_addr);
        b_family = convert_v6_to_v4(b_family, b_addr);
        if (a_family == b_family) {
            retval = memcmp(a_addr,
                            b_addr,
                            a_family == AF_INET ? sizeof(struct in_addr)
                                                : sizeof(struct in6_addr));
        } else if (a_family == AF_INET) {
            retval = -1;
        } else {
            retval = 1;
        }
    }

    return retval;
}

static int
sql_strnatcmp(
    void* ptr, int a_len, const void* a_in, int b_len, const void* b_in)
{
    return strnatcmp(a_len, (char*) a_in, b_len, (char*) b_in);
}

static int
sql_strnatcasecmp(
    void* ptr, int a_len, const void* a_in, int b_len, const void* b_in)
{
    return strnatcasecmp(a_len, (char*) a_in, b_len, (char*) b_in);
}

static int
sql_loglevelcmp(
    void* ptr, int a_len, const void* a_in, int b_len, const void* b_in)
{
    return levelcmp((const char*) a_in, a_len, (const char*) b_in, b_len);
}

static int
sql_measure_with_units(
    void* ptr, int a_len, const void* a_in, int b_len, const void* b_in)
{
    auto a_sf = string_fragment::from_bytes((const char*) a_in, a_len);
    auto b_sf = string_fragment::from_bytes((const char*) b_in, b_len);

    auto a_opt = humanize::try_from<double>(a_sf);
    auto b_opt = humanize::try_from<double>(b_sf);

    if (a_opt && b_opt) {
        auto a_val = a_opt.value();
        auto b_val = b_opt.value();

        if (a_val < b_val) {
            return -1;
        }
        if (a_val == b_val) {
            return 0;
        }
        return 1;
    }

    return sql_strnatcasecmp(nullptr, a_len, a_in, b_len, b_in);
}

int
register_collation_functions(sqlite3* db)
{
    sqlite3_create_collation(db, "ipaddress", SQLITE_UTF8, nullptr, ipaddress);
    sqlite3_create_collation(
        db, "naturalcase", SQLITE_UTF8, nullptr, sql_strnatcmp);
    sqlite3_create_collation(
        db, "naturalnocase", SQLITE_UTF8, nullptr, sql_strnatcasecmp);
    sqlite3_create_collation(
        db, "loglevel", SQLITE_UTF8, nullptr, sql_loglevelcmp);
    sqlite3_create_collation(
        db, "measure_with_units", SQLITE_UTF8, nullptr, sql_measure_with_units);

    return 0;
}
