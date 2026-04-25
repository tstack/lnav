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

#include <algorithm>
#include <array>
#include <cmath>

#include "humanize.hh"

#include <lnav_log.hh>

#include "config.h"
#include "fmt/format.h"
#include "humanize.time.hh"
#include "pcrepp/pcre2pp.hh"
#include "scn/scan.h"

namespace humanize {

template<>
std::optional<try_from_result<double>>
try_from(const string_fragment& sf)
{
    static constexpr auto EMPTY_SUFFIX = string_fragment::from_const("");
    static constexpr auto BYTES_SUFFIX = string_fragment::from_const("B");
    static constexpr auto SECONDS_SUFFIX = string_fragment::from_const("s");
    static constexpr auto PERCENT_SUFFIX = string_fragment::from_const("%");
    static constexpr auto HZ_SUFFIX = string_fragment::from_const("Hz");
    static constexpr auto WATT_SUFFIX = string_fragment::from_const("W");
    static constexpr auto VOLT_SUFFIX = string_fragment::from_const("V");
    static constexpr auto AMP_SUFFIX = string_fragment::from_const("A");
    static constexpr auto PER_S_SUFFIX = string_fragment::from_const("/s");

    enum capture_item {
        all,
        integer,
        real,
        file_size,
        secs,
        hms,
        ms,
        percent,
        frequency,
        electrical,
        throughput,
        per_second,
        short_count,
    };

    // PCRE2 extended mode (`(?x)`): whitespace and `#` comments in
    // the pattern are ignored, so the alternation below can be
    // broken out one unit family per line.
    static const auto code = lnav::pcre2pp::code::from_const(R"((?x)
        ^ \s* (?:
              # integer
              ( [\-\+]? \d+ )
              # real with optional exponent
            | ( [\-\+]? \d+ \. \d+ (?: [eE] [\-\+] \d+ )? )
              # file size: 4KiB, 2 MB, 100B, 500KBps
            | ( [\-\+]? \d+ (?:\.\d+)? \s* (?:[KMGTPE] i?)? [Bb] (?:ps)? )
              # seconds with SI prefix: 1.2s, 1ms, 5us, 10ns
            | ( [\-\+]? \d+ (?:\.\d+)? \s* [munpf]? ) s
              # duration h:m:s
            | ( \d{1,2} : \d{2} : \d{2} (?: \. \d{1,6} )? )
              # duration m:s
            | ( \d{1,2} : \d{2} (?: \. \d{1,6} )? )
              # percent: 42%, -3.14 %
            | ( [\-\+]? \d+ (?:\.\d+)? ) \s* %
              # frequency: 100Hz, 2.5GHz, 440 kHz
            | ( [\-\+]? \d+ (?:\.\d+)? \s* [kKMG]? Hz )
              # power/voltage/current: 5W, 5kW, 3mW, 3.3V, 250 mA
            | ( [\-\+]? \d+ (?:\.\d+)? \s* [kKmM]? [WVA] )
              # named throughput: 10req/s, 1000iops, 500qps, 5pps
            | ( [\-\+]? \d+ (?:\.\d+)? \s* (?:[KMGTPE] i?)?
                (?: req/s | ops/s | iops | qps | rps | pps ) )
              # generic per-second rate: 42/s, 1.5/s
            | ( [\-\+]? \d+ (?:\.\d+)? ) /s
              # short SI-prefixed count: 1.5k, 2M, 3Gi, 4T
            | ( [\-\+]? \d+ (?:\.\d+)? \s* [kKMGTPE] i? )
        ) \s* $
    )");
    thread_local auto md = lnav::pcre2pp::match_data::unitialized();

    if (!code.capture_from(sf).into(md).found_p()) {
        return std::nullopt;
    }

    if (md[integer]) {
        auto scan_res = scn::scan_value<int64_t>(md[integer]->to_string_view());

        if (!scan_res) {
            return std::nullopt;
        }
        return try_from_result<double>{(double) scan_res->value(),
                                       EMPTY_SUFFIX};
    }

    if (md[real]) {
        auto scan_res = scn::scan_value<double>(md[real]->to_string_view());

        if (!scan_res) {
            return std::nullopt;
        }
        return try_from_result<double>{scan_res->value(), EMPTY_SUFFIX};
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
            // IEC `KiB`/`MiB`/... use 1024-multipliers; the bare
            // `KB`/`MB`/... forms use the strict SI 1000-multipliers.
            const double mult = (start + 1 < unit_range.size()
                                 && unit_range[start + 1] == 'i')
                ? 1024.0
                : 1000.0;
            switch (unit_range[start]) {
                case 'E':
                    retval *= mult;
                case 'P':
                    retval *= mult;
                case 'T':
                    retval *= mult;
                case 'G':
                    retval *= mult;
                case 'M':
                    retval *= mult;
                case 'K':
                    retval *= mult;
                    break;
            }
        }
        return try_from_result<double>{retval, BYTES_SUFFIX};
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

        return try_from_result<double>{retval, SECONDS_SUFFIX};
    }

    if (md[hms]) {
        auto scan_res = scn::scan<int, int, double>(md[hms]->to_string_view(),
                                                    "{}:{}:{}");
        auto [hours, mins, secs] = scan_res->values();

        return try_from_result<double>{hours * 3600.0 + mins * 60.0 + secs,
                                       SECONDS_SUFFIX};
    }

    if (md[ms]) {
        auto scan_res
            = scn::scan<int, double>(md[ms]->to_string_view(), "{}:{}");
        auto [mins, secs] = scan_res->values();

        return try_from_result<double>{mins * 60.0 + secs, SECONDS_SUFFIX};
    }

    if (md[percent]) {
        // Return the ratio (e.g. `42%` → 0.42), matching how
        // humanize handles the other unit families.
        auto scan_res = scn::scan_value<double>(md[percent]->to_string_view());
        if (!scan_res) {
            return std::nullopt;
        }
        return try_from_result<double>{scan_res->value() / 100.0,
                                       PERCENT_SUFFIX};
    }

    // Apply a single-letter SI prefix multiplier to `retval` based on
    // the first non-space char in `unit`.  Uppercase `K`/`M`/.../`E`
    // mean kilo..exa (×1000ⁿ).  Lowercase `m` means milli (÷1000).
    auto apply_si_prefix = [](double& retval, const auto& unit) {
        size_t start = 0;
        while (start < unit.size() && isspace(unit[start])) {
            start += 1;
        }
        if (start >= unit.size()) {
            return;
        }
        switch (unit[start]) {
            case 'E':
                retval *= 1000.0;
            case 'P':
                retval *= 1000.0;
            case 'T':
                retval *= 1000.0;
            case 'G':
                retval *= 1000.0;
            case 'M':
                retval *= 1000.0;
            case 'K':
            case 'k':
                retval *= 1000.0;
                break;
            case 'm':
                retval /= 1000.0;
                break;
        }
    };

    if (md[frequency]) {
        auto scan_res
            = scn::scan_value<double>(md[frequency]->to_string_view());
        if (!scan_res) {
            return std::nullopt;
        }
        auto retval = scan_res->value();
        apply_si_prefix(retval, scan_res->range());
        return try_from_result<double>{retval, HZ_SUFFIX};
    }

    if (md[electrical]) {
        auto scan_res
            = scn::scan_value<double>(md[electrical]->to_string_view());
        if (!scan_res) {
            return std::nullopt;
        }
        auto retval = scan_res->value();
        apply_si_prefix(retval, scan_res->range());
        // Final character of the matched range picks W/V/A.
        const auto range = scan_res->range();
        auto suffix = WATT_SUFFIX;
        if (!range.empty()) {
            const auto ch = range[range.size() - 1];
            if (ch == 'V') {
                suffix = VOLT_SUFFIX;
            } else if (ch == 'A') {
                suffix = AMP_SUFFIX;
            }
        }
        return try_from_result<double>{retval, suffix};
    }

    if (md[throughput]) {
        const auto matched = md[throughput].value();
        auto scan_res = scn::scan_value<double>(matched.to_string_view());
        if (!scan_res) {
            return std::nullopt;
        }
        auto retval = scan_res->value();
        apply_si_prefix(retval, scan_res->range());
        // The matched fragment is already a view into the source text;
        // walk past the digits, any whitespace, and one SI-prefix
        // letter to land on the unit (iops, qps, req/s, …) and return
        // that tail as a sub-fragment.
        const int num_len
            = matched.length() - static_cast<int>(scan_res->range().size());
        int pos = num_len;
        while (pos < matched.length() && isspace(matched[pos])) {
            pos++;
        }
        if (pos < matched.length()) {
            switch (matched[pos]) {
                case 'K':
                case 'M':
                case 'G':
                case 'T':
                case 'P':
                case 'E':
                    pos++;
                    if (pos < matched.length() && matched[pos] == 'i') {
                        pos++;
                    }
                    break;
            }
        }
        return try_from_result<double>{
            retval, matched.sub_range(pos, matched.length())};
    }

    if (md[per_second]) {
        auto scan_res
            = scn::scan_value<double>(md[per_second]->to_string_view());
        if (!scan_res) {
            return std::nullopt;
        }
        return try_from_result<double>{scan_res->value(), PER_S_SUFFIX};
    }

    if (md[short_count]) {
        auto scan_res
            = scn::scan_value<double>(md[short_count]->to_string_view());
        if (!scan_res) {
            return std::nullopt;
        }
        auto retval = scan_res->value();
        const auto range = scan_res->range();
        // IEC `Ki`/`Mi`/... use 1024-multipliers; bare `K`/`M`/...
        // use SI 1000-multipliers.
        size_t start = 0;
        while (start < range.size() && isspace(range[start])) {
            start += 1;
        }
        const double mult
            = (start + 1 < range.size() && range[start + 1] == 'i') ? 1024.0
                                                                    : 1000.0;
        if (start < range.size()) {
            switch (range[start]) {
                case 'E':
                    retval *= mult;
                case 'P':
                    retval *= mult;
                case 'T':
                    retval *= mult;
                case 'G':
                    retval *= mult;
                case 'M':
                    retval *= mult;
                case 'K':
                case 'k':
                    retval *= mult;
                    break;
            }
        }
        return try_from_result<double>{retval, EMPTY_SUFFIX};
    }

    return std::nullopt;
}

std::string
file_size(file_ssize_t value, alignment align)
{
    static const double LN1000 = std::log(1000.0);
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
        = floor(std::min(log(value) / LN1000, (double) (UNITS.size() - 1)));
    const auto divisor = pow(1000, exp);

    if (align == alignment::none && divisor <= 1) {
        return fmt::format(FMT_STRING("{}B"), value);
    }
    return fmt::format(FMT_STRING("{:.1f}{}B"),
                       divisor == 0 ? value : value / divisor,
                       UNITS[exp]);
}

namespace {

// Pick an SI prefix letter based on the absolute magnitude of
// `value` relative to 1.  Covers the `E..p` range that
// `humanize::try_from` recognizes (peta down to pico).
struct scale_result {
    double value;
    const char* prefix;
};

// Indexed by exponent + 4; exponents run from -4 (pico) to +6 (exa).
static constexpr std::array<const char*, 11> SI_PREFIXES = {
    "p",
    "n",
    "u",
    "m",
    "",
    "k",
    "M",
    "G",
    "T",
    "P",
    "E",
};

static const double LN1000 = std::log(1000.0);

// `min_exp` clamps the lower bound of the exponent so up-only
// scaling (counts, rates, frequencies — any non-physical unit where
// sub-unit prefixes like `m` would look odd) leaves values < 1
// unchanged.  Bidirectional scaling passes -4 to emit down to `p`
// (pico).
scale_result
si_scale(double value, int min_exp)
{
    const auto abs_value = std::abs(value);
    if (abs_value == 0.0) {
        return {0.0, ""};
    }
    const auto exp = std::clamp(
        (int) std::floor(std::log(abs_value) / LN1000), min_exp, 6);
    return {value / std::pow(1000.0, exp), SI_PREFIXES[exp + 4]};
}

}  // namespace

std::string
format(double value, string_fragment suffix, alignment align)
{
    if (suffix.empty()) {
        return fmt::format(FMT_STRING("{:.6g}"), value);
    }
    // File size — delegate to the existing int64-based formatter.
    if (suffix.iequal("B"_frag)) {
        return file_size(static_cast<file_ssize_t>(value), align);
    }
    // Seconds — sub-second values render with SI prefixes (ps/ns/us/ms)
    // since humanize::time::duration underflows below 1us; values ≥ 1s
    // delegate to time::duration for the familiar 1h22m33s breakdown.
    if (suffix.iequal("s"_frag)) {
        if (value == 0.0) {
            return "0s";
        }
        if (std::abs(value) < 1.0) {
            const auto sr = si_scale(value, -4);
            return fmt::format(FMT_STRING("{:.3g}{}s"), sr.value, sr.prefix);
        }
        timeval tv;
        const auto secs = std::trunc(value);
        tv.tv_sec = static_cast<time_t>(secs);
        tv.tv_usec = static_cast<suseconds_t>((value - secs) * 1e6);
        return humanize::time::duration::from_tv(tv).to_string();
    }
    // Any other suffix gets up-only SI scaling so large counts /
    // rates / frequencies render compactly (e.g. `1.2kHz`,
    // `15kqueries`) while values below the base unit pass through
    // unchanged.
    const auto sr = si_scale(value, 0);
    return fmt::format(
        FMT_STRING("{:.3g}{}{}"), sr.value, sr.prefix, suffix.to_string());
}

const std::string&
sparkline(double value,
          std::optional<double> bound_a_opt,
          std::optional<double> bound_b_opt)
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

    // The two bounds can be given in either order; whichever is
    // smaller becomes the floor and the other becomes the ceiling.
    const auto bound_a = bound_a_opt.value_or(100.0);
    const auto bound_b = bound_b_opt.value_or(0.0);
    const auto lower = std::min(bound_a, bound_b);
    const auto upper = std::max(bound_a, bound_b);

    // Absence marker: only meaningful when the scale starts at zero;
    // zoomed-in ranges (lower != 0) treat at-or-below-min as a 1-cell
    // bar so "present at min" is distinguishable from a gap.
    if (lower == 0.0 && value <= 0.0) {
        return ZERO;
    }
    if (value <= lower) {
        return BARS[0];
    }
    if (value >= upper) {
        return BARS[(size_t) BARS_COUNT - 1];
    }

    const size_t index
        = ceil(((value - lower) / (upper - lower)) * BARS_COUNT) - 1;

    return BARS[index];
}

}  // namespace humanize
