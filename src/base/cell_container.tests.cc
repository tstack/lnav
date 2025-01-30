/**
 * Copyright (c) 2025, Timothy Stack
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

#include "cell_container.hh"

#include "ArenaAlloc/arenaalloc.h"
#include "doctest/doctest.h"

TEST_CASE("cell_container-basic")
{
    {
        auto cont = lnav::cell_container();
        auto cell1 = cont.end_cursor();
        printf(" %p: %d\n", cell1.c_chunk, cell1.c_offset);
        cont.push_null_cell();

        CHECK(cell1.get_type() == lnav::cell_type::CT_NULL);

        auto cell2 = cell1.next();
        CHECK(!cell2.has_value());
    }

    {
        auto cont = lnav::cell_container();
        auto cell1 = cont.end_cursor();
        printf(" %p: %d\n", cell1.c_chunk, cell1.c_offset);
        cont.push_null_cell();
        cont.push_null_cell();

        CHECK(cell1.get_type() == lnav::cell_type::CT_NULL);

        auto cell2 = cell1.next();
        CHECK(cell2.has_value());
        CHECK(cell2->get_type() == lnav::cell_type::CT_NULL);

        auto cell3 = cell2->next();
        CHECK(!cell3.has_value());

        cont.reset();
        CHECK(cont.cc_first->cc_size == 0);
    }

    {
        auto str1 = string_fragment::from_const("test");
        auto long_str1 = std::string();
        long_str1.append(200, 'a');
        auto str2 = string_fragment::from_str(long_str1);
        auto str3 = string_fragment::from_const("bye");

        auto cont = lnav::cell_container();
        auto start_cursor = cont.end_cursor();
        cont.push_text_cell(str1);
        cont.push_text_cell(str2);
        cont.push_text_cell(str3);

        auto cell1 = start_cursor.sync().value();
        CHECK(cell1.get_type() == lnav::cell_type::CT_TEXT);
        auto t = cell1.get_text();
        printf("1 wow %.*s\n", t.length(), t.data());
        CHECK(t == str1);

        auto cell2 = cell1.next();
        CHECK(cell2.has_value());
        CHECK(cell2->get_type() == lnav::cell_type::CT_TEXT);
        auto t2 = cell2->get_text();
        printf("2 wow %.*s\n", t2.length(), t2.data());
        CHECK(cell2->get_text() == str2);

        auto cell3 = cell2->next();
        CHECK(cell3.has_value());
        CHECK(cell3->get_type() == lnav::cell_type::CT_TEXT);
        auto t3 = cell3->get_text();
        printf("3 wow %.*s\n", t3.length(), t3.data());
        CHECK(cell3->get_text() == str3);
    }

    {
        auto cont = lnav::cell_container();
        auto cell1 = cont.end_cursor();
        printf(" %p: %d\n", cell1.c_chunk, cell1.c_offset);
        cont.push_int_cell(123);

        CHECK(cell1.get_type() == lnav::cell_type::CT_INTEGER);
        CHECK(cell1.get_int() == 123);
        cont.push_int_cell(-123);
        auto cell2 = cell1.next();
        CHECK(cell2.has_value());
        CHECK(cell2->get_type() == lnav::cell_type::CT_INTEGER);
        CHECK(cell2->get_int() == -123);
    }

    {
        auto cont = lnav::cell_container();
        auto cell1 = cont.end_cursor();
        printf(" %p: %d\n", cell1.c_chunk, cell1.c_offset);
        cont.push_float_cell(123.456);

        CHECK(cell1.get_type() == lnav::cell_type::CT_FLOAT);
        CHECK(cell1.get_float() == 123.456);
        cont.push_float_cell(-123.456);
        auto cell2 = cell1.next();
        CHECK(cell2.has_value());
        CHECK(cell2->get_type() == lnav::cell_type::CT_FLOAT);
        CHECK(cell2->get_float() == -123.456);
    }

    {
        const size_t actual = 12 * 1024 * 1024 * 1024;
        auto gb = string_fragment::from_const("12GB");
        auto cont = lnav::cell_container();
        auto cell1 = cont.end_cursor();
        printf(" %p: %d\n", cell1.c_chunk, cell1.c_offset);
        cont.push_float_with_units_cell(actual, gb);

        CHECK(cell1.get_type() == lnav::cell_type::CT_FLOAT);
        CHECK(cell1.get_float() == actual);
        CHECK(cell1.get_float_as_text() == "12GB");

        ArenaAlloc::Alloc<char> arena;
        auto full = cell1.to_string_fragment(arena);
        CHECK(full == "12GB");
    }
    {
        auto short_str1 = std::string();
        short_str1.append(62, 'a');
        auto str1 = string_fragment::from_str(short_str1);

        auto cont = lnav::cell_container();
        auto start_cursor = cont.end_cursor();
        cont.push_text_cell(str1);

        auto cell1 = start_cursor.sync().value();
        CHECK(cell1.get_type() == lnav::cell_type::CT_TEXT);
        auto t = cell1.get_text();
        printf("1 wow %.*s\n", t.length(), t.data());
        CHECK(t == str1);

        const unsigned char tl = (62 << 2) | lnav::cell_type::CT_TEXT;
        CHECK(cont.cc_last->cc_data[0] == tl);
        CHECK(cont.cc_last->cc_data[1] == 'a');
    }
}
