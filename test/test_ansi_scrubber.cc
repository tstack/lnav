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
#include "base/attr_line.builder.hh"
#include "config.h"
#include "view_curses.hh"

using namespace std;

int
main(int argc, char* argv[])
{
    printf("BEGIN test\n");

    {
        std::string zero_width = "\x16 1 \x16 2 \x16";
        string_attrs_t sa;

        scrub_ansi_string(zero_width, &sa);
        printf("zero width: '%s'\n",
               fmt::format(FMT_STRING("{:?}"), zero_width).c_str());
        assert(zero_width == " 1  2 ");
        for (const auto& attr : sa) {
            printf("attr %d:%d %s\n",
                   attr.sa_range.lr_start,
                   attr.sa_range.lr_end,
                   attr.sa_type->sat_name);
            if (attr.sa_type == &VC_HYPERLINK) {
                printf("  value: %s\n",
                       attr.sa_value.get<std::string>().c_str());
            }
            if (attr.sa_type == &SA_ORIGIN_OFFSET) {
                printf("  value: %lld\n", attr.sa_value.get<int64_t>());
            }
        }
    }

    {
        std::string bad_bold = "That is not\b\b\ball\n";
        string_attrs_t sa;

        scrub_ansi_string(bad_bold, &sa);
        printf("bad bold1: '%s'\n",
               fmt::format(FMT_STRING("{:?}"), bad_bold).c_str());
        assert(bad_bold == "That is not\b\b\ball\n");
    }
    {
        std::string bad_bold = "test r\bra\bc not\b\b\ball \x16";
        string_attrs_t sa;

        scrub_ansi_string(bad_bold, &sa);
        printf("bad bold2: '%s'\n",
               fmt::format(FMT_STRING("{:?}"), bad_bold).c_str());
        assert(bad_bold == "test ra\bc not\b\b\ball ");
    }

    {
        char input[] = "Hello, \x1b[33;mWorld\x1b[0;m!";

        auto new_len = erase_ansi_escapes(string_fragment::from_const(input));

        printf("result '%s'\n", input);

        assert(new_len == 13);
    }

    {
        std::string bad_bold = "^_\x8b\b ";
        string_attrs_t sa;

        scrub_ansi_string(bad_bold, &sa);
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
    {
        string_attrs_t sa;
        string str_cp;

        str_cp = "Hello, World!";
        scrub_ansi_string(str_cp, &sa);

        assert(str_cp == "Hello, World!");
        assert(sa.empty());

        str_cp = "Hello\x1b[44;m, \x1b[33;mWorld\x1b[0;m!";
        scrub_ansi_string(str_cp, &sa);
        assert(str_cp == "Hello, World!");
        printf("%s\n", str_cp.c_str());
        for (const auto& attr : sa) {
            printf("  attr %d:%d %s %s\n",
                   attr.sa_range.lr_start,
                   attr.sa_range.lr_end,
                   attr.sa_type->sat_name,
                   string_fragment::from_str_range(
                       str_cp, attr.sa_range.lr_start, attr.sa_range.lr_end)
                       .to_string()
                       .c_str());
        }
    }

    {
        // "•]8;;http://example.com•\This_is_a_link•]8;;•\_"
        auto hlink = std::string(
            "\033]8;;http://example.com\033\\This is a "
            "link\033]8;;\033\\\n");

        auto al = attr_line_t();
        attr_line_builder alb(al);

        alb.append_as_hexdump(hlink);
        printf("%s\n", al.get_string().c_str());

        string_attrs_t sa;
        scrub_ansi_string(hlink, &sa);

        printf("hlink %d %d %s", hlink.size(), sa.size(), hlink.c_str());
        assert(sa.size() == 3);
        for (const auto& attr : sa) {
            printf("attr %d:%d %s\n",
                   attr.sa_range.lr_start,
                   attr.sa_range.lr_end,
                   attr.sa_type->sat_name);
            if (attr.sa_type == &VC_HYPERLINK) {
                printf("  value: %s\n",
                       attr.sa_value.get<std::string>().c_str());
            }
            if (attr.sa_type == &SA_ORIGIN_OFFSET) {
                printf("  value: %lld\n", attr.sa_value.get<int64_t>());
            }
        }
    }
}
