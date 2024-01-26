/**
 * Copyright (c) 2022, Timothy Stack
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

#include "config.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "text_anonymizer.hh"

TEST_CASE("ipv4")
{
    lnav::text_anonymizer ta;

    CHECK(ta.next(string_fragment::from_const("127.0.1.1 says hi"))
          == "10.0.0.1 says hi");
    CHECK(ta.next(string_fragment::from_const("127.0.1.1 says hi"))
          == "10.0.0.1 says hi");
    CHECK(ta.next(string_fragment::from_const("u'127.0.1.1' says hi"))
          == "u'10.0.0.1' says hi");
}

TEST_CASE("ipv6")
{
    lnav::text_anonymizer ta;

    CHECK(ta.next(
              string_fragment::from_const("fe80::1887:2f2d:bc2e:8e41 says hi"))
          == "2001:db8::1 says hi");
}

TEST_CASE("url")
{
    lnav::text_anonymizer ta;

    CHECK(ta.next(string_fragment::from_const("retrieving https://bob:abc@example.com/fooooooo22/192.168.1.33/barrrrr44?abcdef=foobar&ghijkl=123456&bazzer&ip=192.168.1.2#heading-2")) ==
          "aback https://meerkat:67c93775f715ab8ab01178caf86713c6@achondroplasia.example.com/abaft22/10.0.0.1/abashed44?aberrant=abhorrent&abiding=123456&abject&ip=10.0.0.2#heading-2");
}

TEST_CASE("email")
{
    lnav::text_anonymizer ta;

    CHECK(ta.next(string_fragment::from_const("hello support@lnav.org"))
          == "aback meerkat@achondroplasia.example.com");
}

TEST_CASE("symbol")
{
    lnav::text_anonymizer ta;

    CHECK(ta.next(string_fragment::from_const(
              "state is Constants.DOWNLOAD_STARTED"))
          == "aback is Abandoned.ABASHED_ABERRANT");
}

TEST_CASE("date")
{
    lnav::text_anonymizer ta;

    CHECK(ta.next(string_fragment::from_const("2022-06-02T12:26:22.072Z"))
          == "2022-06-02T12:26:22.072Z");
}

TEST_CASE("uuid")
{
    lnav::text_anonymizer ta;

    CHECK(ta.next(string_fragment::from_const(
              "52556d7e-c34d-d7f9-73b6-f52ad939952e"))
          == "bc8b6954-c2a4-e7f3-0e18-2fa4035db1c9");
}

TEST_CASE("MAC-address")
{
    lnav::text_anonymizer ta;

    CHECK(ta.next(string_fragment::from_const("ether f2:09:1a:a2:e3:e2"))
          == "aback 00:00:5e:00:53:00");
}

TEST_CASE("hex-dump")
{
    lnav::text_anonymizer ta;

    CHECK(ta.next(string_fragment::from_const("key f2:09:1a:a2"))
          == "key 68:48:d3:93");
}

TEST_CASE("cc")
{
    lnav::text_anonymizer ta;

    CHECK(ta.next(string_fragment::from_const("cc 6011 1111 1111 1117"))
          == "cc 1a49 c794 31d9 3eb2");
    CHECK(ta.next(string_fragment::from_const("cc 6011111111111117"))
          == "cc 1a49c79431d93eb2");
}

TEST_CASE("xml")
{
    lnav::text_anonymizer ta;

    CHECK(ta.next(string_fragment::from_const("<o:gupdate xmlns:o=\"http://www.google.com/update2/request\" protocol=\"2.0\" version=\"KeystoneDaemon-1.2.0.7709\" ismachine=\"1\" requestid=\"{0DFDBCD1-5E29-4DFC-BD99-31A2397198FE}\">")) ==
          "<o:gupdate  xmlns:o=\"http://achondroplasia.example.com/aback2/abandoned\" protocol=\"2.0\" version=\"KeystoneDaemon-1.2.0.7709\" ismachine=\"1\" requestid=\"{1ca0a968-cbe9-e75b-d00b-4859609878ea}\">");
}
