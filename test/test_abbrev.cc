/**
 * Copyright (c) 2016, Timothy Stack
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

#include <assert.h>

#include "base/string_util.hh"
#include "config.h"

static struct test_data {
    const char* str{nullptr};
    const char* abbrev_str{nullptr};
    size_t max_len{0};
} TEST_DATA[] = {
    {"abc", "abc", 5},
    {"com.example.foo.bar", "c.e.f.bar", 5},
    {"com.example.foo.bar", "c.e.foo.bar", 15},
    {"no dots in here", "no dots in here", 5},
};

int
main(int argc, char* argv[])
{
    for (const auto& td : TEST_DATA) {
        char buffer[1024];

        strcpy(buffer, td.str);
        size_t actual = abbreviate_str(buffer, strlen(td.str), td.max_len);
        buffer[actual] = '\0';

        printf("orig: %s\n", td.str);
        printf(" act: %s\n", buffer);
        assert(strcmp(buffer, td.abbrev_str) == 0);
    }
}
