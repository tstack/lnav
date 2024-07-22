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

#include <cmath>
#include <vector>

#include "humanize.hh"

#include "config.h"
#include "fmt/format.h"

namespace humanize {

std::string
file_size(file_ssize_t value, alignment align)
{
    static const double LN1024 = log(1024.0);
    static const std::vector<const char*> UNITS = {
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

    auto exp
        = floor(std::min(log(value) / LN1024, (double) (UNITS.size() - 1)));
    auto divisor = pow(1024, exp);

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
    static const double BARS_COUNT = std::distance(begin(BARS), end(BARS));

    if (value <= 0.0) {
        return ZERO;
    }

    auto upper = upper_opt.value_or(100.0);

    if (value >= upper) {
        return BARS[(size_t) BARS_COUNT - 1];
    }

    size_t index = ceil((value / upper) * BARS_COUNT) - 1;

    return BARS[index];
}

}  // namespace humanize
