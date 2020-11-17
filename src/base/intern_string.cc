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

#include "intern_string.hh"

const static int TABLE_SIZE = 4095;
static intern_string *TABLE[TABLE_SIZE];

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

    curr = TABLE[h];
    while (curr != nullptr) {
        if (curr->is_len == len && strncmp(curr->is_str, str, len) == 0) {
            return curr;
        }
        curr = curr->is_next;
    }

    char *strcp = new char[len + 1];
    memcpy(strcp, str, len);
    strcp[len] = '\0';
    curr = new intern_string(strcp, len);
    curr->is_next = TABLE[h];
    TABLE[h] = curr;

    return curr;
}

const intern_string *intern_string::lookup(const string_fragment &sf) noexcept
{
    return lookup(sf.data(), sf.length());
}

const intern_string *intern_string::lookup(const std::string &str) noexcept
{
    return lookup(str.c_str(), str.size());
}
