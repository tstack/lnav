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

#include "base/humanize.hh"

#include "config.h"
#include "doctest/doctest.h"

TEST_CASE("humanize::file_size")
{
    CHECK(humanize::file_size(0, humanize::alignment::columnar) == "0.0 B");
    CHECK(humanize::file_size(1, humanize::alignment::columnar) == "1.0 B");
    CHECK(humanize::file_size(1024, humanize::alignment::columnar) == "1.0KB");
    CHECK(humanize::file_size(1500, humanize::alignment::columnar) == "1.5KB");
    CHECK(humanize::file_size(55LL * 784LL * 1024LL * 1024LL,
                              humanize::alignment::columnar)
          == "42.1GB");
    CHECK(humanize::file_size(-1LL, humanize::alignment::columnar)
          == "Unknown");
    CHECK(humanize::file_size(std::numeric_limits<int64_t>::max(),
                              humanize::alignment::columnar)
          == "8.0EB");
}

TEST_CASE("humanize::try_from")
{
    {
        auto integer = string_fragment::from_const("123 ");
        auto try_res = humanize::try_from<double>(integer);

        CHECK(try_res.has_value());
        CHECK(try_res.value() == 123);
    }
    {
        auto real = string_fragment::from_const(" 123.456");
        auto try_res = humanize::try_from<double>(real);

        CHECK(try_res.has_value());
        CHECK(try_res.value() == 123.456);
    }
    {
        auto file_size = string_fragment::from_const(" 123.4GB");
        auto try_res = humanize::try_from<double>(file_size);

        CHECK(try_res.has_value());
        CHECK(try_res.value() == 123.4 * 1024 * 1024 * 1024);
    }
    {
        auto file_size = string_fragment::from_const(" 123.4 GB");
        auto try_res = humanize::try_from<double>(file_size);

        CHECK(try_res.has_value());
        CHECK(try_res.value() == 123.4 * 1024 * 1024 * 1024);
    }
    {
        auto secs = string_fragment::from_const("1.2s");
        auto try_res = humanize::try_from<double>(secs);

        CHECK(try_res.has_value());
        CHECK(try_res.value() == 1.2);
    }
    {
        auto secs = string_fragment::from_const("1ms");
        auto try_res = humanize::try_from<double>(secs);

        CHECK(try_res.has_value());
        CHECK(try_res.value() == 0.001);
    }
    {
        auto secs = string_fragment::from_const("1 ms");
        auto try_res = humanize::try_from<double>(secs);

        CHECK(try_res.has_value());
        CHECK(try_res.value() == 0.001);
    }
    {
        auto secs = string_fragment::from_const("1.2ms");
        auto try_res = humanize::try_from<double>(secs);

        CHECK(try_res.has_value());
        CHECK(try_res.value() == 0.0012);
    }
    {
        auto secs = string_fragment::from_const("1:25");
        auto try_res = humanize::try_from<double>(secs);

        CHECK(try_res.has_value());
        CHECK(try_res.value() == 60 + 25);
    }
    {
        auto secs_sub = string_fragment::from_const("1:25.6");
        auto try_res = humanize::try_from<double>(secs_sub);

        CHECK(try_res.has_value());
        CHECK(try_res.value() == 60 + 25.6);
    }
    {
        auto secs = string_fragment::from_const("1:30:25.33 ");
        auto try_res = humanize::try_from<double>(secs);

        CHECK(try_res.has_value());
        CHECK(try_res.value() == 3600 + 30 * 60 + 25.33);
    }
}
