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
 *
 * @file pcrepp.cc
 */

#include "pcre2pp.hh"

#include "config.h"
#include "ww898/cp_utf8.hpp"

namespace lnav::pcre2pp {

std::string
match_data::to_string() const
{
    std::string retval;

    if (this->get_count() == 1) {
        auto cap = (*this)[0];
        retval.append(cap->data(), cap->length());
    } else {
        for (size_t lpc = 1; lpc < this->get_count(); lpc++) {
            auto cap = (*this)[lpc];

            if (!cap) {
                continue;
            }

            retval.append(cap->data(), cap->length());
        }
    }

    return retval;
}

matcher
capture_builder::into(lnav::pcre2pp::match_data& md) &&
{
    if (md.get_capacity() < this->mb_code.get_match_data_capacity()) {
        md = this->mb_code.create_match_data();
    }

    return matcher{
        this->mb_code,
        this->mb_input,
        md,
    };
}

match_data
code::create_match_data() const
{
    auto_mem<pcre2_match_data> md(pcre2_match_data_free);

    md = pcre2_match_data_create_from_pattern(this->p_code, nullptr);

    return match_data{std::move(md)};
}

Result<code, compile_error>
code::from(string_fragment sf, int options)
{
    compile_error ce;
    auto_mem<pcre2_code> co(pcre2_code_free);

    options |= PCRE2_UTF;
    co = pcre2_compile(
        sf.udata(), sf.length(), options, &ce.ce_code, &ce.ce_offset, nullptr);

    if (co == nullptr) {
        ce.ce_pattern = sf.to_string();
        return Err(ce);
    }

    auto jit_rc = pcre2_jit_compile(co, PCRE2_JIT_COMPLETE);
    if (jit_rc < 0) {
        // log_error("failed to JIT compile pattern: %d", jit_rc);
    }

    return Ok(code{std::move(co), sf.to_string()});
}

code::named_captures
code::get_named_captures() const
{
    named_captures retval;

    pcre2_pattern_info(
        this->p_code.in(), PCRE2_INFO_NAMECOUNT, &retval.nc_count);
    pcre2_pattern_info(
        this->p_code.in(), PCRE2_INFO_NAMEENTRYSIZE, &retval.nc_entry_size);
    pcre2_pattern_info(
        this->p_code.in(), PCRE2_INFO_NAMETABLE, &retval.nc_name_table);

    return retval;
}

size_t
code::match_partial(string_fragment in) const
{
    auto md = this->create_match_data();
    auto length = in.length();

    do {
        auto rc = pcre2_match(this->p_code.in(),
                              in.udata(),
                              length,
                              0,
                              PCRE2_PARTIAL_HARD,
                              md.md_data.in(),
                              nullptr);

        if (rc == PCRE2_ERROR_PARTIAL) {
            return md.md_ovector[1];
        }

        if (length > 0) {
            length -= 1;
        }
    } while (length > 0);

    return 0;
}

const char*
code::get_name_for_capture(size_t index) const
{
    for (const auto cap : this->get_named_captures()) {
        if (cap.get_index() == index) {
            return cap.get_name().data();
        }
    }

    return nullptr;
}

size_t
code::get_capture_count() const
{
    uint32_t retval;

    pcre2_pattern_info(this->p_code.in(), PCRE2_INFO_CAPTURECOUNT, &retval);

    return retval;
}

std::vector<string_fragment>
code::get_captures() const
{
    bool in_class = false, in_escape = false, in_literal = false;
    auto pat_frag = string_fragment::from_str(this->p_pattern);
    std::vector<string_fragment> cap_in_progress;
    std::vector<string_fragment> retval;

    for (int lpc = 0; this->p_pattern[lpc]; lpc++) {
        if (in_escape) {
            in_escape = false;
            if (this->p_pattern[lpc] == 'Q') {
                in_literal = true;
            }
        } else if (in_class) {
            if (this->p_pattern[lpc] == ']') {
                in_class = false;
            }
            if (this->p_pattern[lpc] == '\\') {
                in_escape = true;
            }
        } else if (in_literal) {
            if (this->p_pattern[lpc] == '\\' && this->p_pattern[lpc + 1] == 'E')
            {
                in_literal = false;
                lpc += 1;
            }
        } else {
            switch (this->p_pattern[lpc]) {
                case '\\':
                    in_escape = true;
                    break;
                case '[':
                    in_class = true;
                    break;
                case '(':
                    cap_in_progress.emplace_back(pat_frag.sub_range(lpc, lpc));
                    break;
                case ')': {
                    if (!cap_in_progress.empty()) {
                        static const auto DEFINE_SF
                            = string_fragment::from_const("(?(DEFINE)");

                        auto& cap = cap_in_progress.back();
                        char first = '\0', second = '\0', third = '\0';
                        bool is_cap = false;

                        cap.sf_end = lpc + 1;
                        if (cap.length() >= 2) {
                            first = this->p_pattern[cap.sf_begin + 1];
                        }
                        if (cap.length() >= 3) {
                            second = this->p_pattern[cap.sf_begin + 2];
                        }
                        if (cap.length() >= 4) {
                            third = this->p_pattern[cap.sf_begin + 3];
                        }
                        if (cap.sf_begin >= 2) {
                            auto poss_define = string_fragment::from_str_range(
                                this->p_pattern, cap.sf_begin - 2, cap.sf_end);
                            if (poss_define == DEFINE_SF) {
                                cap_in_progress.pop_back();
                                continue;
                            }
                        }
                        if (first == '?') {
                            if (second == '\'') {
                                is_cap = true;
                            }
                            if (second == '<'
                                && (isalpha(third) || third == '_'))
                            {
                                is_cap = true;
                            }
                            if (second == 'P' && third == '<') {
                                is_cap = true;
                            }
                        } else if (first != '*') {
                            is_cap = true;
                        }
                        if (is_cap) {
                            retval.emplace_back(cap);
                        }
                        cap_in_progress.pop_back();
                    }
                    break;
                }
            }
        }
    }

    assert((size_t) this->get_capture_count() == retval.size());

    return retval;
}

std::string
code::replace(string_fragment str, const char* repl) const
{
    std::string retval;
    std::string::size_type start = 0;
    string_fragment remaining = str;

    auto md = this->create_match_data();
    while (remaining.is_valid()) {
        auto find_res = this->capture_from(str)
                            .at(remaining)
                            .into(md)
                            .matches()
                            .ignore_error();
        if (!find_res) {
            break;
        }
        auto all = find_res->f_all;
        remaining = find_res->f_remaining;
        bool in_escape = false;

        retval.append(str.data(), start, (all.sf_begin - start));
        start = all.sf_end;
        for (int lpc = 0; repl[lpc]; lpc++) {
            auto ch = repl[lpc];

            if (in_escape) {
                if (isdigit(ch)) {
                    auto capture_index = size_t(ch - '0');

                    if (capture_index < md.get_count()) {
                        auto cap = md[capture_index];
                        if (cap) {
                            retval.append(cap->data(), cap->length());
                        }
                    } else if (capture_index > this->get_capture_count()) {
                        retval.push_back('\\');
                        retval.push_back(ch);
                    }
                } else {
                    if (ch != '\\') {
                        retval.push_back('\\');
                    }
                    retval.push_back(ch);
                }
                in_escape = false;
            } else {
                switch (ch) {
                    case '\\':
                        in_escape = true;
                        break;
                    default:
                        retval.push_back(ch);
                        break;
                }
            }
        }
    }
    if (remaining.is_valid()) {
        retval.append(remaining.data(), 0, remaining.length());
    }

    return retval;
}

int
code::name_index(const char* name) const
{
    return pcre2_substring_number_from_name(this->p_code.in(),
                                            (PCRE2_SPTR) name);
}

size_t
code::named_capture::get_index() const
{
    return (this->nc_entry[0] << 8) | (this->nc_entry[1] & 0xff);
}

string_fragment
code::named_capture::get_name() const
{
    return string_fragment::from_bytes(
        &this->nc_entry[2], strlen((const char*) &this->nc_entry[2]));
}

code::named_capture
code::named_captures::iterator::operator*() const
{
    return code::named_capture{this->i_entry};
}

code::named_captures::iterator&
code::named_captures::iterator::operator++()
{
    this->i_entry += this->i_entry_size;

    return *this;
}

bool
code::named_captures::iterator::operator==(const iterator& other) const
{
    return this->i_entry == other.i_entry
        && this->i_entry_size == other.i_entry_size;
}

bool
code::named_captures::iterator::operator!=(const iterator& other) const
{
    return this->i_entry != other.i_entry
        || this->i_entry_size != other.i_entry_size;
}

code::named_captures::iterator
code::named_captures::begin() const
{
    return iterator{this->nc_entry_size, this->nc_name_table};
}

code::named_captures::iterator
code::named_captures::end() const
{
    return iterator{
        this->nc_entry_size,
        this->nc_name_table + (this->nc_count * this->nc_entry_size),
    };
}

bool
matcher::found_p(uint32_t options)
{
    this->mb_input.i_offset = this->mb_input.i_next_offset;

    if (this->mb_input.i_offset == -1) {
        return false;
    }

    auto rc = pcre2_match(this->mb_code.p_code.in(),
                          this->mb_input.i_string.udata(),
                          this->mb_input.i_string.length(),
                          this->mb_input.i_offset,
                          options,
                          this->mb_match_data.md_data.in(),
                          nullptr);

    if (rc > 0) {
        this->mb_match_data.md_input = this->mb_input;
        this->mb_match_data.md_code = &this->mb_code;
        this->mb_match_data.md_capture_end = rc;
        if (this->mb_match_data[0]->empty()
            && this->mb_match_data[0]->sf_end >= this->mb_input.i_string.sf_end)
        {
            this->mb_input.i_next_offset = -1;
        } else if (this->mb_match_data[0]->empty()) {
            this->mb_input.i_next_offset
                = this->mb_match_data.md_ovector[1] + 1;
        } else {
            this->mb_input.i_next_offset = this->mb_match_data.md_ovector[1];
        }
        this->mb_match_data.md_input.i_next_offset
            = this->mb_input.i_next_offset;
        return true;
    }

    this->mb_match_data.md_input = this->mb_input;
    this->mb_match_data.md_ovector[0] = this->mb_input.i_offset;
    this->mb_match_data.md_ovector[1] = this->mb_input.i_offset;
    this->mb_match_data.md_capture_end = 1;
    if (rc == PCRE2_ERROR_NOMATCH) {
        return false;
    }

    return false;
}

matcher::matches_result
matcher::matches(uint32_t options)
{
    this->mb_input.i_offset = this->mb_input.i_next_offset;

    if (this->mb_input.i_offset == -1) {
        return not_found{};
    }

    auto rc = pcre2_match(this->mb_code.p_code.in(),
                          this->mb_input.i_string.udata(),
                          this->mb_input.i_string.length(),
                          this->mb_input.i_offset,
                          options,
                          this->mb_match_data.md_data.in(),
                          nullptr);

    if (rc > 0) {
        this->mb_match_data.md_input = this->mb_input;
        this->mb_match_data.md_code = &this->mb_code;
        this->mb_match_data.md_capture_end = rc;
        if (this->mb_match_data[0]->empty()
            && this->mb_match_data[0]->sf_end >= this->mb_input.i_string.sf_end)
        {
            this->mb_input.i_next_offset = -1;
        } else if (this->mb_match_data[0]->empty()) {
            this->mb_input.i_next_offset
                = this->mb_match_data.md_ovector[1] + 1;
        } else {
            this->mb_input.i_next_offset = this->mb_match_data.md_ovector[1];
        }
        this->mb_match_data.md_input.i_next_offset
            = this->mb_input.i_next_offset;
        return found{
            this->mb_match_data[0].value(),
            this->mb_match_data.remaining(),
        };
    }

    this->mb_match_data.md_input = this->mb_input;
    this->mb_match_data.md_ovector[0] = this->mb_input.i_offset;
    this->mb_match_data.md_ovector[1] = this->mb_input.i_offset;
    this->mb_match_data.md_capture_end = 1;
    if (rc == PCRE2_ERROR_NOMATCH) {
        return not_found{};
    }

    return error{&this->mb_code, rc};
}

void
matcher::matches_result::handle_error(matcher::error err)
{
    unsigned char buffer[1024];

    pcre2_get_error_message(err.e_error_code, buffer, sizeof(buffer));
    // log_error("pcre2_match failure: %s", buffer);
}

std::string
compile_error::get_message() const
{
    unsigned char buffer[1024];

    pcre2_get_error_message(this->ce_code, buffer, sizeof(buffer));

    return {(const char*) buffer};
}

std::string
matcher::error::get_message()
{
    unsigned char buffer[1024];

    pcre2_get_error_message(this->e_error_code, buffer, sizeof(buffer));

    return {(const char*) buffer};
}

}  // namespace lnav::pcre2pp