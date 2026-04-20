/**
 * Copyright (c) 2026, Timothy Stack
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

#include "separated_string.hh"

#include <string.h>

// True when `ch` is a valid continuation character for a numeric
// unit suffix — letters and `%`.  Used by the cell classifier to
// recognize shapes like `20.0KB`, `12ms`, and `42%`.
static bool
is_suffix_char(char ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '%';
}

std::string
separated_string::unescape_quoted(string_fragment sf)
{
    std::string retval;
    retval.reserve(sf.length());
    const auto* data = sf.data();
    const auto len = static_cast<size_t>(sf.length());
    for (size_t idx = 0; idx < len; /* advanced below */) {
        if (idx + 1 < len && data[idx] == '"' && data[idx + 1] == '"') {
            retval.push_back('"');
            idx += 2;
        } else {
            retval.push_back(data[idx]);
            idx += 1;
        }
    }
    return retval;
}

void
separated_string::iterator::update()
{
    const auto& ss = this->i_parent;
    const char* const data_end = ss.ss_str + ss.ss_len;
    const char sep_ch = ss.ss_separator;

    // If the previous call flagged a ghost trailing cell, this call is
    // the one that materializes it.  Flip in_ghost so the iterator
    // won't compare equal to end() until a subsequent ++.
    if (this->i_in_ghost) {
        this->i_in_ghost = false;
        this->i_value_start = this->i_pos;
        this->i_value_end = this->i_pos;
        this->i_next_pos = this->i_pos;
        return;
    }
    if (this->i_pending_ghost) {
        this->i_pending_ghost = false;
        this->i_in_ghost = true;
        this->i_value_start = this->i_pos;
        this->i_value_end = this->i_pos;
        this->i_next_pos = this->i_pos;
        this->i_kind = cell_kind::empty;
        return;
    }

    enum state_t : uint8_t {
        LEAD_WS,
        SIGN,
        DIGITS,
        TRAIL_WS,
        SUFFIX,  // entered after DIGITS/TRAIL_WS sees alpha/% — e.g.
                 // the `KB` in `20.0KB`, or `ms` in `12ms`.
        OTHER
    };
    auto state = LEAD_WS;
    bool saw_digit = false;
    bool saw_dot = false;
    bool saw_exp = false;
    bool saw_suffix = false;
    bool in_quotes = false;
    bool saw_quote = false;
    const char* quote_start = nullptr;
    const char* quote_end = nullptr;

    const char* p = this->i_pos;
    while (p < data_end) {
        if (!in_quotes && *p == sep_ch) {
            break;
        }
        const char c = *p;

        if (c == '"' && !saw_quote && state == LEAD_WS) {
            in_quotes = true;
            saw_quote = true;
            quote_start = p + 1;
            p += 1;
            continue;
        }
        if (c == '"' && in_quotes) {
            // CSV escape: `""` inside a quoted cell is a
            // literal double-quote, not a close-quote.
            if (p + 1 < data_end && p[1] == '"') {
                state = OTHER;  // embedded quote → non-numeric
                p += 2;
                continue;
            }
            in_quotes = false;
            quote_end = p;
            // Only trailing whitespace is allowed after the
            // closing quote; anything else demotes to OTHER.
            if (state != OTHER) {
                state = TRAIL_WS;
            }
            p += 1;
            continue;
        }

        // Once we've committed to `cell_kind::other`, the classifier
        // no longer cares about any character until we hit the next
        // boundary (separator, or close-quote if we're in a quoted
        // region).  memchr jumps straight there.
        if (state == OTHER) {
            const void* boundary = memchr(
                p, in_quotes ? '"' : sep_ch, static_cast<size_t>(data_end - p));
            p = (boundary != nullptr) ? static_cast<const char*>(boundary)
                                      : data_end;
            continue;
        }

        if (c == ' ' || c == '\t') {
            if (state == DIGITS || state == SIGN || state == SUFFIX) {
                state = TRAIL_WS;
            }
        } else if (state == SUFFIX) {
            if (is_suffix_char(c)) {
                // stay in SUFFIX
            } else {
                // Digit inside a unit (e.g. `0x1F` after the `x`
                // flipped us to SUFFIX) or any other character ends
                // the number-with-suffix shape.
                state = OTHER;
            }
        } else if (state == TRAIL_WS) {
            if (is_suffix_char(c) && saw_digit && !saw_suffix) {
                // Space-separated unit, e.g. `3.2 dB`.
                state = SUFFIX;
                saw_suffix = true;
            } else {
                // Either no digits yet, or we already consumed a
                // suffix and hit more non-ws content (e.g.
                // `12 KB extra`) — not a clean number_with_suffix.
                state = OTHER;
            }
        } else if (c >= '0' && c <= '9') {
            saw_digit = true;
            state = DIGITS;
        } else if ((c == '+' || c == '-') && state == LEAD_WS) {
            state = SIGN;
        } else if (c == '.' && !saw_dot && state != TRAIL_WS) {
            saw_dot = true;
            state = DIGITS;
        } else if ((c == 'e' || c == 'E') && saw_digit && !saw_exp
                   && state == DIGITS)
        {
            // Exponent takes priority over a suffix starting with e/E.
            saw_exp = true;
            saw_dot = true;  // exponent implies floating
            if (p + 1 < data_end && (p[1] == '+' || p[1] == '-')) {
                p += 1;  // consume the exponent sign
            }
        } else if (is_suffix_char(c) && state == DIGITS && saw_digit) {
            // Numeric prefix followed by a unit with no space, e.g.
            // `20.0KB` or `12ms`.
            state = SUFFIX;
            saw_suffix = true;
        } else {
            state = OTHER;
        }
        p += 1;
    }

    // When the separator we just consumed lives flush against the
    // end of input, convention says one more empty cell should be
    // emitted.  Defer it to the next update() call via
    // i_pending_ghost so the user still sees the current cell first.
    if (p < data_end && p + 1 == data_end) {
        this->i_pending_ghost = true;
    }
    this->i_next_pos = (p < data_end) ? p + 1 : data_end;

    if (saw_quote) {
        // Use the span between the quotes.  An unterminated
        // quote takes everything up to the current position.
        this->i_value_start = quote_start;
        this->i_value_end = (quote_end != nullptr) ? quote_end : p;
    } else {
        const char* vs = this->i_pos;
        const char* ve = p;
        while (vs < ve && (*vs == ' ' || *vs == '\t')) {
            vs += 1;
        }
        while (ve > vs && (ve[-1] == ' ' || ve[-1] == '\t')) {
            ve -= 1;
        }
        this->i_value_start = vs;
        this->i_value_end = ve;
    }

    if (state == OTHER) {
        this->i_kind = cell_kind::other;
    } else if (saw_suffix) {
        // Reached via either ending in SUFFIX or in TRAIL_WS after a
        // SUFFIX run.  Either way the cell is `<number><unit>`.
        this->i_kind = cell_kind::number_with_suffix;
    } else if (!saw_digit) {
        this->i_kind = cell_kind::empty;
    } else if (saw_dot) {
        this->i_kind = cell_kind::floating;
    } else {
        this->i_kind = cell_kind::integer;
    }
}
