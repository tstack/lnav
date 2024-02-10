/**
 * Copyright (c) 2014, Timothy Stack
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
 * @file intern_string.cc
 */

#include <mutex>

#include "intern_string.hh"

#include <string.h>

#include "config.h"
#include "pcrepp/pcre2pp.hh"
#include "ww898/cp_utf8.hpp"
#include "xxHash/xxhash.h"

const static int TABLE_SIZE = 4095;

struct intern_string::intern_table {
    ~intern_table()
    {
        for (auto is : this->it_table) {
            auto curr = is;

            while (curr != nullptr) {
                auto next = curr->is_next;

                delete curr;
                curr = next;
            }
        }
    }

    intern_string* it_table[TABLE_SIZE];
};

intern_table_lifetime
intern_string::get_table_lifetime()
{
    static intern_table_lifetime retval = std::make_shared<intern_table>();

    return retval;
}

unsigned long
hash_str(const char* str, size_t len)
{
    return XXH3_64bits(str, len);
}

const intern_string*
intern_string::lookup(const char* str, ssize_t len) noexcept
{
    unsigned long h;
    intern_string* curr;

    if (len == -1) {
        len = strlen(str);
    }
    h = hash_str(str, len) % TABLE_SIZE;

    {
        static std::mutex table_mutex;

        std::lock_guard<std::mutex> lk(table_mutex);
        auto tab = get_table_lifetime();

        curr = tab->it_table[h];
        while (curr != nullptr) {
            if (static_cast<ssize_t>(curr->is_str.size()) == len
                && strncmp(curr->is_str.c_str(), str, len) == 0)
            {
                return curr;
            }
            curr = curr->is_next;
        }

        curr = new intern_string(str, len);
        curr->is_next = tab->it_table[h];
        tab->it_table[h] = curr;

        return curr;
    }
}

const intern_string*
intern_string::lookup(const string_fragment& sf) noexcept
{
    return lookup(sf.data(), sf.length());
}

const intern_string*
intern_string::lookup(const std::string& str) noexcept
{
    return lookup(str.c_str(), str.size());
}

bool
intern_string::startswith(const char* prefix) const
{
    const char* curr = this->is_str.data();

    while (*prefix != '\0' && *prefix == *curr) {
        prefix += 1;
        curr += 1;
    }

    return *prefix == '\0';
}

string_fragment
string_fragment::trim(const char* tokens) const
{
    string_fragment retval = *this;

    while (retval.sf_begin < retval.sf_end) {
        bool found = false;

        for (int lpc = 0; tokens[lpc] != '\0'; lpc++) {
            if (retval.sf_string[retval.sf_begin] == tokens[lpc]) {
                found = true;
                break;
            }
        }
        if (!found) {
            break;
        }

        retval.sf_begin += 1;
    }
    while (retval.sf_begin < retval.sf_end) {
        bool found = false;

        for (int lpc = 0; tokens[lpc] != '\0'; lpc++) {
            if (retval.sf_string[retval.sf_end - 1] == tokens[lpc]) {
                found = true;
                break;
            }
        }
        if (!found) {
            break;
        }

        retval.sf_end -= 1;
    }

    return retval;
}

string_fragment
string_fragment::trim() const
{
    return this->trim(" \t\r\n");
}

nonstd::optional<string_fragment>
string_fragment::consume_n(int amount) const
{
    if (amount > this->length()) {
        return nonstd::nullopt;
    }

    return string_fragment{
        this->sf_string,
        this->sf_begin + amount,
        this->sf_end,
    };
}

string_fragment::split_result
string_fragment::split_n(int amount) const
{
    if (amount > this->length()) {
        return nonstd::nullopt;
    }

    return std::make_pair(
        string_fragment{
            this->sf_string,
            this->sf_begin,
            this->sf_begin + amount,
        },
        string_fragment{
            this->sf_string,
            this->sf_begin + amount,
            this->sf_end,
        });
}

std::vector<string_fragment>
string_fragment::split_lines() const
{
    std::vector<string_fragment> retval;
    int start = this->sf_begin;

    for (auto index = start; index < this->sf_end; index++) {
        if (this->sf_string[index] == '\n') {
            retval.emplace_back(this->sf_string, start, index + 1);
            start = index + 1;
        }
    }
    if (retval.empty() || start < this->sf_end) {
        retval.emplace_back(this->sf_string, start, this->sf_end);
    }

    return retval;
}

Result<ssize_t, const char*>
string_fragment::utf8_length() const
{
    ssize_t retval = 0;

    for (ssize_t byte_index = this->sf_begin; byte_index < this->sf_end;) {
        auto ch_size = TRY(ww898::utf::utf8::char_size([this, byte_index]() {
            return std::make_pair(this->sf_string[byte_index],
                                  this->sf_end - byte_index);
        }));
        byte_index += ch_size;
        retval += 1;
    }

    return Ok(retval);
}

string_fragment::case_style
string_fragment::detect_text_case_style() const
{
    static const auto LOWER_RE
        = lnav::pcre2pp::code::from_const(R"(^[^A-Z]+$)");
    static const auto UPPER_RE
        = lnav::pcre2pp::code::from_const(R"(^[^a-z]+$)");
    static const auto CAMEL_RE
        = lnav::pcre2pp::code::from_const(R"(^(?:[A-Z][a-z0-9]+)+$)");

    if (LOWER_RE.find_in(*this).ignore_error().has_value()) {
        return case_style::lower;
    }
    if (UPPER_RE.find_in(*this).ignore_error().has_value()) {
        return case_style::upper;
    }
    if (CAMEL_RE.find_in(*this).ignore_error().has_value()) {
        return case_style::camel;
    }

    return case_style::mixed;
}

std::string
string_fragment::to_string_with_case_style(case_style style) const
{
    std::string retval;

    switch (style) {
        case case_style::lower: {
            for (auto ch : *this) {
                retval.append(1, std::tolower(ch));
            }
            break;
        }
        case case_style::upper: {
            for (auto ch : *this) {
                retval.append(1, std::toupper(ch));
            }
            break;
        }
        case case_style::camel: {
            retval = this->to_string();
            if (!this->empty()) {
                retval[0] = toupper(retval[0]);
            }
            break;
        }
        case case_style::mixed: {
            return this->to_string();
        }
    }

    return retval;
}

std::string
string_fragment::to_unquoted_string() const
{
    auto sub_sf = *this;

    if (sub_sf.startswith("r") || sub_sf.startswith("u")) {
        sub_sf = sub_sf.consume_n(1).value();
    }
    if (sub_sf.length() >= 2
        && ((sub_sf.startswith("\"") && sub_sf.endswith("\""))
            || (sub_sf.startswith("'") && sub_sf.endswith("'"))))
    {
        std::string retval;

        sub_sf.sf_begin += 1;
        sub_sf.sf_end -= 1;
        retval.reserve(this->length());

        auto in_escape = false;
        for (auto ch : sub_sf) {
            if (in_escape) {
                switch (ch) {
                    case 'n':
                        retval.push_back('\n');
                        break;
                    case 't':
                        retval.push_back('\t');
                        break;
                    case 'r':
                        retval.push_back('\r');
                        break;
                    default:
                        retval.push_back(ch);
                        break;
                }
                in_escape = false;
            } else if (ch == '\\') {
                in_escape = true;
            } else {
                retval.push_back(ch);
            }
        }

        return retval;
    }

    return this->to_string();
}

uint32_t
string_fragment::front_codepoint() const
{
    size_t index = 0;
    try {
        return ww898::utf::utf8::read(
            [this, &index]() { return this->data()[index++]; });
    } catch (const std::runtime_error& e) {
        return this->data()[0];
    }
}

Result<ssize_t, const char*>
string_fragment::codepoint_to_byte_index(ssize_t cp_index) const
{
    ssize_t retval = 0;

    while (cp_index > 0) {
        if (retval >= this->length()) {
            return Err("index is beyond the end of the string");
        }
        auto ch_len = TRY(ww898::utf::utf8::char_size([this, retval]() {
            return std::make_pair(this->data()[retval],
                                  this->length() - retval - 1);
        }));

        retval += ch_len;
        cp_index -= 1;
    }

    return Ok(retval);
}
