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

#include <iostream>

#include <zlib.h>

#include "base/lnav.gzip.hh"
#include "config.h"
#include "doctest/doctest.h"

TEST_CASE("lnav::gzip::uncompress")
{
    {
        auto u_res = lnav::gzip::uncompress("empty", nullptr, 0);

        CHECK(u_res.isErr());
        CHECK(u_res.unwrapErr()
              == "unable to uncompress: empty -- stream error");
    }

    {
        auto u_res = lnav::gzip::uncompress("garbage", "abc", 3);

        CHECK(u_res.isErr());
        CHECK(u_res.unwrapErr()
              == "unable to uncompress: garbage -- incorrect header check");
    }
}

TEST_CASE("lnav::gzip::roundtrip")
{
    const char msg[] = "Hello, World!";

    auto c_res = lnav::gzip::compress(msg, sizeof(msg));
    auto buf = c_res.unwrap();
    auto u_res = lnav::gzip::uncompress("test", buf.in(), buf.size());
    auto buf2 = u_res.unwrap();

    CHECK(std::string(msg) == std::string(buf2.in()));
}
