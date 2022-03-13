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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file intern_string.cc
 */

#include "config.h"

#include <string.h>

#include <mutex>

#include "intern_string.hh"

const static int TABLE_SIZE = 4095;

struct intern_string::intern_table {
    ~intern_table() {
        for (auto is : this->it_table) {
            auto curr = is;

            while (curr != nullptr) {
                auto next = curr->is_next;

                delete curr;
                curr = next;
            }
        }
    }

    intern_string *it_table[TABLE_SIZE];
};

intern_table_lifetime intern_string::get_table_lifetime()
{
    static intern_table_lifetime retval = std::make_shared<intern_table>();

    return retval;
}

unsigned long
hash_str(const char *str, size_t len)
{
    unsigned long retval = 5381;

    for (size_t lpc = 0; lpc < len; lpc++) {
        /* retval * 33 + c */
        retval = ((retval << 5) + retval) + (unsigned char)str[lpc];
    }

    return retval;
}

const intern_string *intern_string::lookup(const char *str, ssize_t len) noexcept
{
    unsigned long h;
    intern_string *curr;

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
            if (curr->is_str.size() == len && strncmp(curr->is_str.c_str(), str, len) == 0) {
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

const intern_string *intern_string::lookup(const string_fragment &sf) noexcept
{
    return lookup(sf.data(), sf.length());
}

const intern_string *intern_string::lookup(const std::string &str) noexcept
{
    return lookup(str.c_str(), str.size());
}

bool intern_string::startswith(const char *prefix) const
{
    const char *curr = this->is_str.data();

    while (*prefix != '\0' && *prefix == *curr) {
        prefix += 1;
        curr += 1;
    }

    return *prefix == '\0';
}

void string_fragment::trim(const char *tokens)
{
    while (this->sf_begin < this->sf_end) {
        bool found = false;

        for (int lpc = 0; tokens[lpc] != '\0'; lpc++) {
            if (this->sf_string[this->sf_begin] == tokens[lpc]) {
                found = true;
                break;
            }
        }
        if (!found) {
            break;
        }

        this->sf_begin += 1;
    }
    while (this->sf_begin < this->sf_end) {
        bool found = false;

        for (int lpc = 0; tokens[lpc] != '\0'; lpc++) {
            if (this->sf_string[this->sf_end - 1] == tokens[lpc]) {
                found = true;
                break;
            }
        }
        if (!found) {
            break;
        }

        this->sf_end -= 1;
    }
}

nonstd::optional<string_fragment> string_fragment::consume_n(int amount) const
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
