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
    CHECK(humanize::file_size(1000, humanize::alignment::columnar) == "1.0KB");
    CHECK(humanize::file_size(1500, humanize::alignment::columnar) == "1.5KB");
    CHECK(humanize::file_size(42100000000LL, humanize::alignment::columnar)
          == "42.1GB");
    CHECK(humanize::file_size(-1LL, humanize::alignment::columnar)
          == "Unknown");
    CHECK(humanize::file_size(std::numeric_limits<int64_t>::max(),
                              humanize::alignment::columnar)
          == "9.2EB");
}

TEST_CASE("humanize::try_from")
{
    {
        auto integer = string_fragment::from_const("123 ");
        auto try_res = humanize::try_from<double>(integer);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 123);
        CHECK(try_res->unit_suffix == "");
    }
    {
        auto real = string_fragment::from_const(" 123.456");
        auto try_res = humanize::try_from<double>(real);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 123.456);
        CHECK(try_res->unit_suffix == "");
    }
    {
        // SI prefix forms now use strict 1000-multipliers.
        auto file_size = string_fragment::from_const(" 123.4GB");
        auto try_res = humanize::try_from<double>(file_size);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 123.4 * 1000 * 1000 * 1000);
        CHECK(try_res->unit_suffix == "B");
    }
    {
        auto file_size = string_fragment::from_const(" 123.4 GB");
        auto try_res = humanize::try_from<double>(file_size);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 123.4 * 1000 * 1000 * 1000);
        CHECK(try_res->unit_suffix == "B");
    }
    {
        // IEC "bibyte" prefixes should parse to the same numeric value
        // as the informal K/M/G/T/P/E spellings already supported.
        auto file_size = string_fragment::from_const("4KiB");
        auto try_res = humanize::try_from<double>(file_size);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 4.0 * 1024);
        CHECK(try_res->unit_suffix == "B");
    }
    {
        auto file_size = string_fragment::from_const(" 2.5 MiB");
        auto try_res = humanize::try_from<double>(file_size);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 2.5 * 1024 * 1024);
        CHECK(try_res->unit_suffix == "B");
    }
    {
        auto file_size = string_fragment::from_const("1 GiB");
        auto try_res = humanize::try_from<double>(file_size);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 1.0 * 1024 * 1024 * 1024);
        CHECK(try_res->unit_suffix == "B");
    }
    {
        // "iB" alone (with no prefix letter) should not parse as a
        // size — the `i` marker is only meaningful after K/M/G/T/P/E.
        auto bad = string_fragment::from_const("5iB");
        auto try_res = humanize::try_from<double>(bad);

        CHECK(!try_res.has_value());
    }
    {
        auto secs = string_fragment::from_const("1.2s");
        auto try_res = humanize::try_from<double>(secs);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 1.2);
        CHECK(try_res->unit_suffix == "s");
    }
    {
        auto secs = string_fragment::from_const("1ms");
        auto try_res = humanize::try_from<double>(secs);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 0.001);
        CHECK(try_res->unit_suffix == "s");
    }
    {
        auto secs = string_fragment::from_const("1 ms");
        auto try_res = humanize::try_from<double>(secs);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 0.001);
        CHECK(try_res->unit_suffix == "s");
    }
    {
        auto secs = string_fragment::from_const("1.2ms");
        auto try_res = humanize::try_from<double>(secs);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 0.0012);
        CHECK(try_res->unit_suffix == "s");
    }
    {
        auto secs = string_fragment::from_const("1:25");
        auto try_res = humanize::try_from<double>(secs);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 60 + 25);
        CHECK(try_res->unit_suffix == "s");
    }
    {
        auto secs_sub = string_fragment::from_const("1:25.6");
        auto try_res = humanize::try_from<double>(secs_sub);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 60 + 25.6);
        CHECK(try_res->unit_suffix == "s");
    }
    {
        auto secs = string_fragment::from_const("1:30:25.33 ");
        auto try_res = humanize::try_from<double>(secs);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 3600 + 30 * 60 + 25.33);
        CHECK(try_res->unit_suffix == "s");
    }
    {
        // Percentages parse to the ratio (e.g. `42%` → 0.42).
        auto pct = string_fragment::from_const("42%");
        auto try_res = humanize::try_from<double>(pct);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 0.42);
        CHECK(try_res->unit_suffix == "%");
    }
    {
        auto pct = string_fragment::from_const("-3.14 %");
        auto try_res = humanize::try_from<double>(pct);

        CHECK(try_res.has_value());
        CHECK(try_res->value == -3.14 / 100.0);
        CHECK(try_res->unit_suffix == "%");
    }
    {
        auto pct = string_fragment::from_const("1.5%");
        auto try_res = humanize::try_from<double>(pct);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 0.015);
        CHECK(try_res->unit_suffix == "%");
    }
    {
        // Short SI-prefixed counts: bare `k`, `M`, `G`, `T` (no B).
        auto n = string_fragment::from_const("1.5k");
        auto try_res = humanize::try_from<double>(n);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 1500.0);
        CHECK(try_res->unit_suffix == "");
    }
    {
        auto n = string_fragment::from_const("2M");
        auto try_res = humanize::try_from<double>(n);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 2000000.0);
        CHECK(try_res->unit_suffix == "");
    }
    {
        auto n = string_fragment::from_const("3Gi");
        auto try_res = humanize::try_from<double>(n);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 3.0 * 1024 * 1024 * 1024);
        CHECK(try_res->unit_suffix == "");
    }
    {
        // Frequency.
        auto f = string_fragment::from_const("100Hz");
        auto try_res = humanize::try_from<double>(f);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 100.0);
        CHECK(try_res->unit_suffix == "Hz");
    }
    {
        auto f = string_fragment::from_const("2.5GHz");
        auto try_res = humanize::try_from<double>(f);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 2.5e9);
        CHECK(try_res->unit_suffix == "Hz");
    }
    {
        auto f = string_fragment::from_const("440 kHz");
        auto try_res = humanize::try_from<double>(f);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 440000.0);
        CHECK(try_res->unit_suffix == "Hz");
    }
    {
        // Throughput: named per-second rates; value carries SI prefix.
        auto r = string_fragment::from_const("500iops");
        auto try_res = humanize::try_from<double>(r);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 500.0);
        CHECK(try_res->unit_suffix == "iops");
    }
    {
        auto r = string_fragment::from_const("10Mreq/s");
        auto try_res = humanize::try_from<double>(r);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 10.0e6);
        CHECK(try_res->unit_suffix == "req/s");
    }
    {
        auto r = string_fragment::from_const("500qps");
        auto try_res = humanize::try_from<double>(r);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 500.0);
        CHECK(try_res->unit_suffix == "qps");
    }
    {
        auto r = string_fragment::from_const("120rps");
        auto try_res = humanize::try_from<double>(r);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 120.0);
        CHECK(try_res->unit_suffix == "rps");
    }
    {
        auto r = string_fragment::from_const("5pps");
        auto try_res = humanize::try_from<double>(r);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 5.0);
        CHECK(try_res->unit_suffix == "pps");
    }
    {
        // Generic per-second rate.
        auto r = string_fragment::from_const("42/s");
        auto try_res = humanize::try_from<double>(r);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 42.0);
        CHECK(try_res->unit_suffix == "/s");
    }
    {
        // Power / voltage / current.
        auto p = string_fragment::from_const("5kW");
        auto try_res = humanize::try_from<double>(p);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 5000.0);
        CHECK(try_res->unit_suffix == "W");
    }
    {
        auto p = string_fragment::from_const("3mW");
        auto try_res = humanize::try_from<double>(p);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 0.003);
        CHECK(try_res->unit_suffix == "W");
    }
    {
        auto p = string_fragment::from_const("3.3V");
        auto try_res = humanize::try_from<double>(p);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 3.3);
        CHECK(try_res->unit_suffix == "V");
    }
    {
        auto p = string_fragment::from_const("250 mA");
        auto try_res = humanize::try_from<double>(p);

        CHECK(try_res.has_value());
        CHECK(try_res->value == 0.25);
        CHECK(try_res->unit_suffix == "A");
    }
}

TEST_CASE("humanize::format empty suffix")
{
    // No suffix → raw number, 6 sig figs.
    CHECK(humanize::format(0.0, string_fragment::from_const("")) == "0");
    CHECK(humanize::format(123.0, string_fragment::from_const("")) == "123");
    CHECK(humanize::format(1.5e6, string_fragment::from_const("")) == "1.5e+06");
}

TEST_CASE("humanize::format bytes")
{
    // `B` suffix delegates to `humanize::file_size`.
    CHECK(humanize::format(0.0, string_fragment::from_const("B")) == "0B");
    CHECK(humanize::format(500.0, string_fragment::from_const("B")) == "500B");
    CHECK(humanize::format(1500.0, string_fragment::from_const("B"))
          == "1.5KB");
    CHECK(humanize::format(1258291.0, string_fragment::from_const("B"))
          == "1.3MB");
    // Case-insensitive match.
    CHECK(humanize::format(2048.0, string_fragment::from_const("b"))
          == "2.0KB");
}

TEST_CASE("humanize::format seconds")
{
    // Sub-second → SI prefix scaling.
    CHECK(humanize::format(0.0, string_fragment::from_const("s")) == "0s");
    CHECK(humanize::format(0.0005, string_fragment::from_const("s"))
          == "500us");
    CHECK(humanize::format(0.012, string_fragment::from_const("s"))
          == "12ms");
    CHECK(humanize::format(2.5e-9, string_fragment::from_const("s"))
          == "2.5ns");
    // ≥ 1s → humanize::time::duration breakdown.
    CHECK(humanize::format(1.5, string_fragment::from_const("s")) == "1s500");
    CHECK(humanize::format(90.0, string_fragment::from_const("s"))
          == "1m30s");
    CHECK(humanize::format(3660.0, string_fragment::from_const("s"))
          == "1h01m00s");
}

TEST_CASE("humanize::format arbitrary suffix — up-only SI scaling")
{
    // Sub-base values pass through (no `m`/`u`/`n` prefix) so count-
    // like suffixes don't acquire confusing sub-unit prefixes.
    CHECK(humanize::format(500.0, string_fragment::from_const("queries"))
          == "500queries");
    CHECK(humanize::format(3.14, string_fragment::from_const("foo"))
          == "3.14foo");
    CHECK(humanize::format(0.5, string_fragment::from_const("connections"))
          == "0.5connections");
    // Large values pick an SI prefix.
    CHECK(humanize::format(1200.0, string_fragment::from_const("Hz"))
          == "1.2kHz");
    CHECK(humanize::format(15000.0, string_fragment::from_const("queries"))
          == "15kqueries");
    CHECK(humanize::format(2.4e9, string_fragment::from_const("Hz"))
          == "2.4GHz");
}

TEST_CASE("humanize::format / try_from roundtrip")
{
    {
        auto wtf = "008ms"_frag;
        auto try_res = humanize::try_from<double>(wtf);
        CHECK(try_res.has_value());
        CHECK(try_res->value == 0.008);
        CHECK(try_res->unit_suffix == "s");
    }

    {
        auto wtf = "21us"_frag;
        auto try_res = humanize::try_from<double>(wtf);
        CHECK(try_res.has_value());
        CHECK(try_res->value < 0.000022);
        CHECK(try_res->value > 0.000020);
        CHECK(try_res->unit_suffix == "s");
    }

    // Bytes roundtrip only when the value lands on a clean SI-1000
    // boundary that survives the formatter's 1-decimal rounding.
    auto roundtrip_bytes = [](double v) {
        return humanize::try_from<double>(string_fragment::from_str(
            humanize::format(v, string_fragment::from_const("B"))));
    };
    CHECK(roundtrip_bytes(500.0)->value == 500.0);
    CHECK(roundtrip_bytes(1500.0)->value == 1500.0);
    CHECK(roundtrip_bytes(2000000.0)->value == 2000000.0);

    // Sub-second time values survive through the SI-prefix formatter
    // (ms/us/ns).  Values ≥ 1s go through humanize::time::duration,
    // which produces a breakdown `try_from` doesn't parse — no
    // roundtrip guarantee there.
    auto roundtrip_secs = [](double v) {
        return humanize::try_from<double>(string_fragment::from_str(
            humanize::format(v, string_fragment::from_const("s"))));
    };
    CHECK(roundtrip_secs(0.005)->value == 0.005);
    CHECK(roundtrip_secs(0.5)->value == 0.5);
    CHECK(roundtrip_secs(0.00025)->value == 0.00025);
    CHECK(roundtrip_secs(2.5e-9)->value == 2.5e-9);
}
