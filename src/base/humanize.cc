/**
 * Copyright (c) 2019, Timothy Stack
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

#include <array>
#include <cmath>

#include "humanize.hh"

#include <lnav_log.hh>

#include "config.h"
#include "fmt/format.h"
#include "pcrepp/pcre2pp.hh"
#include "scn/scan.h"

namespace humanize {

template<>
std::optional<double>
try_from(const string_fragment& sf)
{
    enum capture_item {
        all,
        integer,
        real,
        file_size,
        secs,
        hms,
        ms,
    };

    static const auto code = lnav::pcre2pp::code::from_const(
        R"(^\s*(?:([\-\+]?\d+)|([\-\+]?\d+\.\d+(?:[eE][\-\+]\d+)?)|([\-\+]?\d+(?:\.\d+)?\s*[KMGTPE]?[Bb](?:ps)?)|([\-\+]?\d+(?:\.\d+)?\s*[munpf]?)s|(\d{1,2}:\d{2}:\d{2}(?:\.\d{1,6})?)|(\d{1,2}:\d{2}(?:\.\d{1,6})?))\s*$)");
    thread_local auto md = lnav::pcre2pp::match_data::unitialized();

    if (!code.capture_from(sf).into(md).found_p()) {
        return std::nullopt;
    }

    if (md[integer]) {
        auto scan_res = scn::scan_value<int64_t>(md[integer]->to_string_view());

        return scan_res->value();
    }

    if (md[real]) {
        auto scan_res = scn::scan_value<double>(md[real]->to_string_view());

        return scan_res->value();
    }

    if (md[file_size]) {
        auto scan_res
            = scn::scan_value<double>(md[file_size]->to_string_view());
        const auto unit_range = scan_res->range();
        auto retval = scan_res->value();

        if (unit_range.size() >= 2) {
            size_t start = 0;
            while (isspace(unit_range[start])) {
                start += 1;
            }
            switch (unit_range[start]) {
                case 'E':
                    retval *= 1024.0;
                case 'P':
                    retval *= 1024.0;
                case 'T':
                    retval *= 1024.0;
                case 'G':
                    retval *= 1024.0;
                case 'M':
                    retval *= 1024.0;
                case 'K':
                    retval *= 1024.0;
                    break;
            }
        }
        return retval;
    }

    if (md[secs]) {
        auto scan_res = scn::scan_value<double>(md[secs]->to_string_view());
        const auto unit_range = scan_res->range();
        auto retval = scan_res->value();

        if (!unit_range.empty()) {
            size_t start = 0;
            while (isspace(unit_range[start])) {
                start += 1;
            }
            switch (unit_range[start]) {
                case 'f':
                    retval /= 1000.0;
                case 'p':
                    retval /= 1000.0;
                case 'n':
                    retval /= 1000.0;
                case 'u':
                    retval /= 1000.0;
                case 'm':
                    retval /= 1000.0;
                    break;
            }
        }

        return retval;
    }

    if (md[hms]) {
        auto scan_res = scn::scan<int, int, double>(md[hms]->to_string_view(),
                                                    "{}:{}:{}");
        auto [hours, mins, secs] = scan_res->values();

        return hours * 3600.0 + mins * 60.0 + secs;
    }

    if (md[ms]) {
        auto scan_res
            = scn::scan<int, double>(md[ms]->to_string_view(), "{}:{}");
        auto [mins, secs] = scan_res->values();

        return mins * 60.0 + secs;
    }

    return std::nullopt;
}

std::string
file_size(file_ssize_t value, alignment align)
{
    static const double LN1024 = std::log(1024.0);
    static constexpr std::array<const char*, 7> UNITS = {
        " ",
        "K",
        "M",
        "G",
        "T",
        "P",
        "E",
    };

    if (value < 0) {
        return "Unknown";
    }

    if (value == 0) {
        switch (align) {
            case alignment::none:
                return "0B";
            case alignment::columnar:
                return "0.0 B";
        }
    }

    const auto exp
        = floor(std::min(log(value) / LN1024, (double) (UNITS.size() - 1)));
    const auto divisor = pow(1024, exp);

    if (align == alignment::none && divisor <= 1) {
        return fmt::format(FMT_STRING("{}B"), value, UNITS[exp]);
    }
    return fmt::format(FMT_STRING("{:.1f}{}B"),
                       divisor == 0 ? value : value / divisor,
                       UNITS[exp]);
}

const std::string&
sparkline(double value, std::optional<double> upper_opt)
{
    static const std::string ZERO = " ";
    static const std::string BARS[] = {
        "\u2581",
        "\u2582",
        "\u2583",
        "\u2584",
        "\u2585",
        "\u2586",
        "\u2587",
        "\u2588",
    };
    static constexpr double BARS_COUNT = std::distance(begin(BARS), end(BARS));

    if (value <= 0.0) {
        return ZERO;
    }

    const auto upper = upper_opt.value_or(100.0);
    if (value >= upper) {
        return BARS[(size_t) BARS_COUNT - 1];
    }

    const size_t index = ceil((value / upper) * BARS_COUNT) - 1;

    return BARS[index];
}

}  // namespace humanize
