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
 */

#include <iostream>

#include "attr_line.hh"

#include "config.h"
#include "doctest/doctest.h"

using namespace lnav::roles::literals;

TEST_CASE("attr_line_t::basic-wrapping")
{
    text_wrap_settings tws = {3, 21};
    attr_line_t to_be_wrapped{"This line, right here, needs to be wrapped."};
    attr_line_t dst;

    to_be_wrapped.al_attrs.emplace_back(
        line_range{0, (int) to_be_wrapped.al_string.length()},
        VC_ROLE.value(role_t::VCR_ERROR));
    dst.append(to_be_wrapped, &tws);

    CHECK(dst.get_string() ==
          "This line, right\n"
          "   here, needs to be\n"
          "   wrapped.");

    for (const auto& attr : dst.al_attrs) {
        printf("attr %d:%d %s\n",
               attr.sa_range.lr_start,
               attr.sa_range.lr_end,
               attr.sa_type->sat_name);
    }
}

TEST_CASE("attr_line_t::unicode-wrap")
{
    text_wrap_settings tws = {3, 21};
    attr_line_t prefix;

    prefix.append(" ")
        .append("\u2022"_list_glyph)
        .append(" ")
        .with_attr_for_all(SA_PREFORMATTED.value());

    attr_line_t body;
    body.append("This is a long line that needs to be wrapped and indented");

    attr_line_t li;

    li.append(prefix)
        .append(body, &tws)
        .with_attr_for_all(SA_PREFORMATTED.value());

    attr_line_t dst;

    dst.append(li);

    CHECK(dst.get_string()
          == " \u2022 This is a long\n"
          "   line that needs to\n"
          "   be wrapped and\n"
          "   indented");
}
