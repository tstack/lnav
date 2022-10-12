/**
 * Copyright (c) 2013, Timothy Stack
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
 * @file test_ansi_scrubber.cc
 *
 * Test for the scrub_ansi_string function.
 *
 * TODO: Add a test for the ansi-colors.0.in file.  It has a matrix of all the
 * color/style combinations.
 */

#include <assert.h>

#include "base/ansi_scrubber.hh"
#include "config.h"
#include "view_curses.hh"

using namespace std;

int
main(int argc, char* argv[])
{
    {
        char input[] = "Hello, \x1b[33;mWorld\x1b[0;m!";

        auto new_len = erase_ansi_escapes(string_fragment::from_const(input));

        printf("result '%s'\n", input);

        assert(new_len == 13);
    }

    {
        std::string boldish
            = "\u2022\b\u2022\u2023\b\u2023 h\bhe\bel\blo\bo _\ba_\bb_\bc a\b_ "
              "b";
        auto boldish2 = boldish;
        string_attrs_t sa;

        sa.clear();
        scrub_ansi_string(boldish, &sa);
        printf("boldish %s\n", boldish.c_str());
        assert(boldish == "\u2022\u2023 helo abc a b");

        auto new_len = erase_ansi_escapes(boldish2);
        boldish2.resize(new_len);
        printf("boldish2 %s\n", boldish2.c_str());
        assert(boldish2 == "\u2022\u2023 helo abc a b");

        for (const auto& attr : sa) {
            printf("attr %d:%d %s\n",
                   attr.sa_range.lr_start,
                   attr.sa_range.lr_end,
                   attr.sa_type->sat_name);
            if (attr.sa_type == &SA_ORIGIN_OFFSET) {
                printf("  value: %d\n", attr.sa_value.get<int64_t>());
            }
        }
    }

    string_attrs_t sa;
    string str_cp;

    str_cp = "Hello, World!";
    scrub_ansi_string(str_cp, &sa);

    assert(str_cp == "Hello, World!");
    assert(sa.empty());

    str_cp = "Hello\x1b[44;m, \x1b[33;mWorld\x1b[0;m!";
    scrub_ansi_string(str_cp, &sa);
    assert(str_cp == "Hello, World!");
}
