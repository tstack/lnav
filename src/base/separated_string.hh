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

#ifndef lnav_separated_string_hh
#define lnav_separated_string_hh

#include <cstddef>

#include "intern_string.hh"

// A single-byte-delimited string tokenizer with CSV awareness:
// iterates cells while honoring surrounding double-quotes (including
// `""`-escaped literals) and classifies each cell's shape (empty /
// integer / floating / other) in a single pass.  operator*() returns
// the unquoted, trimmed value region so callers can feed it to
// scn::scan_value or humanize::try_from without extra cleanup.
struct separated_string {
    // Classification of a single cell's contents.  Lets callers
    // fast-path integer/float parses and fall back to humanize::
    // `try_from()` only when the cell has non-numeric characters.
    enum class cell_kind {
        empty,  // zero bytes, or whitespace only
        integer,  // optional sign + digits (with optional surrounding ws)
        floating,  // digits with a single '.' and/or exponent
        number_with_suffix,  // numeric prefix followed by a unit (e.g.
                             // `20.0KB`, `12ms`, `42%`) — callers can
                             // send these straight to humanize::try_from
                             // without a lexical probe.
        other,  // anything else — text, identifiers, JSON blobs, etc.
    };

    const char* ss_str;
    size_t ss_len;
    char ss_separator{','};

    separated_string(const char* str, size_t len) : ss_str(str), ss_len(len) {}

    explicit separated_string(const string_fragment& sf)
        : ss_str(sf.data()), ss_len(sf.length())
    {
    }

    separated_string& with_separator(char sep)
    {
        this->ss_separator = sep;
        return *this;
    }

    // Collapse CSV's `""`-escaped double-quote back into a single
    // `"`.  `operator*()` deliberately preserves the `""` pairs
    // verbatim (they may be meaningful to downstream parsers that
    // expect raw input); callers that want the canonical cell text
    // — column headers, for instance — should run the output
    // through this.
    static std::string unescape_quoted(string_fragment sf);

    struct iterator {
        const separated_string& i_parent;
        const char* i_pos;
        const char* i_next_pos;
        // Value region — unquoted + leading/trailing-whitespace-trimmed.
        // Set by update() so `operator*()` and `kind()` both see the
        // same range without extra work by callers.
        const char* i_value_start;
        const char* i_value_end;
        size_t i_index;
        cell_kind i_kind{cell_kind::empty};
        // Set by update() when it consumed a separator whose tail sits
        // at the end of input, e.g. the final `,` in `a,b,`.  The next
        // call will materialize the conventional empty trailing cell.
        bool i_pending_ghost{false};
        // True while the iterator is pointing AT the trailing empty
        // cell.  Distinguishes "at data_end because there's a ghost to
        // show" from "at data_end, natural end" so operator==(end())
        // doesn't cut iteration short.
        bool i_in_ghost{false};

        iterator(const separated_string& ss, const char* pos)
            : i_parent(ss), i_pos(pos), i_next_pos(pos), i_value_start(pos),
              i_value_end(pos), i_index(0)
        {
            this->update();
        }

        // Find the next separator, unquote surrounding CSV double-
        // quotes, and classify the cell — all in one pass.  A separator
        // that falls inside a quoted region does not split the field,
        // and `""` inside a quoted cell is treated as a literal `"`.
        // Also drives the two-step transition that emits an empty
        // trailing cell after a trailing separator (matches Python
        // csv / awk behavior).
        void update();

        iterator& operator++()
        {
            this->i_pos = this->i_next_pos;
            this->update();
            this->i_index += 1;

            return *this;
        }

        string_fragment operator*() const
        {
            const auto& ss = this->i_parent;
            return string_fragment::from_byte_range(
                ss.ss_str,
                this->i_value_start - ss.ss_str,
                this->i_value_end - ss.ss_str);
        }

        cell_kind kind() const { return this->i_kind; }

        bool operator==(const iterator& other) const
        {
            return (&this->i_parent == &other.i_parent)
                && (this->i_pos == other.i_pos)
                && (this->i_pending_ghost == other.i_pending_ghost)
                && (this->i_in_ghost == other.i_in_ghost);
        }

        bool operator!=(const iterator& other) const
        {
            return !(*this == other);
        }

        size_t index() const { return this->i_index; }
    };

    iterator begin() const { return {*this, this->ss_str}; }

    iterator end() const { return {*this, this->ss_str + this->ss_len}; }
};

#endif
