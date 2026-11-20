/**
 * Copyright (c) 2020, Timothy Stack
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

#ifndef lnav_humanize_hh
#define lnav_humanize_hh

#include <string>
#include <optional>

#include <sys/types.h>

#include "file_range.hh"

namespace humanize {

// `try_from` reports both the parsed numeric value (normalized to the
// base unit — e.g. "1.5KB" → 1500, "1ms" → 0.001, and "42%" → 42, in
// percentage points) and the canonical
// unit suffix it was recognized as ("B" for bytes, "s" for seconds,
// "Hz" for frequency, etc.).  Callers pair the two directly with
// humanize::format(value, suffix).  For plain numbers without a unit,
// the suffix is an empty fragment.  The suffix fragments point at
// static storage, so they outlive any caller-held copy.
template<typename R>
struct try_from_result {
    R value;
    string_fragment unit_suffix;
};

template<typename R>
std::optional<try_from_result<R>> try_from(const string_fragment& v);

enum class alignment {
    none,
    columnar,
};

/**
 * Format the given size as a human-friendly string.
 *
 * @param value The value to format.
 * @return The formatted string.
 */
std::string file_size(file_ssize_t value, alignment align);

// Render `value` as a human-friendly string using `suffix` as a hint
// for which unit family the value belongs to.  `suffix` mirrors the
// tokens `try_from<double>` recognizes — "B" (bytes), "s" (seconds),
// "Hz" (frequency), "W"/"V"/"A" (electrical), "%" (percent),
// "iops"/"req/s"/"qps"/"pps"/"rps" (throughput), "/s" (per-second),
// plus the bare SI count suffix for short_count.  A "%" value is in
// percentage points and is shown without SI prefixes: a whole number as
// is, otherwise with one decimal, or two significant digits below 1
// (e.g. "38.7%", "0.03%").  With `alignment::columnar`, the number,
// after any SI scaling, is a whole number without decimals, anything
// else with two, or two significant digits when it would otherwise show
// as zero (e.g. "12.50%", "3%", "1.20kHz", "999 Hz"); seconds and bytes
// keep their own forms.  With no suffix, a
// whole number is shown in full and anything else with 6 significant
// digits.  Unrecognized suffixes degrade gracefully to
// `{value}{suffix}` with no scaling so callers can call this
// unconditionally.
std::string format(double value,
                   string_fragment suffix,
                   alignment align = alignment::none);

// Map a numeric value onto one of nine bar glyphs ("", ▁..█).
//
// The two bound arguments can be passed in either order — the smaller
// is used as the floor, the larger as the ceiling.  Either or both may
// be omitted; `upper` defaults to 100, `lower` defaults to 0.  Callers
// zooming into a clustered range (e.g. memory samples between 2048 and
// 2065) pass both bounds so the variation fills the full glyph height.
//
// Rendering rules:
//   * `value == 0` with a floor of 0 renders as blank — distinguishes
//     "absent" from "observed at minimum" when the scale starts at zero.
//   * `value <= floor` with a non-zero floor renders as ▁ so the
//     at-minimum moment is visible rather than collapsing to blank.
//   * `value >= ceiling` renders as █.
//   * Otherwise the normalized `(value - floor) / (ceiling - floor)`
//     picks one of ▁..▇ via ceiling.
const std::string& sparkline(double value,
                             std::optional<double> bound_a,
                             std::optional<double> bound_b = std::nullopt);

}  // namespace humanize

#endif
