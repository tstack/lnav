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

#include "config.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "column_namer.hh"
#include "doctest/doctest.h"

TEST_CASE("column_namer::default")
{
    column_namer cn{column_namer::language::SQL};

    auto def_name0 = cn.add_column(string_fragment{});
    CHECK(def_name0 == "col_0");
    auto def_name1 = cn.add_column(string_fragment{});
    CHECK(def_name1 == "col_1");
}

TEST_CASE("column_namer::no-collision")
{
    column_namer cn{column_namer::language::SQL};

    auto name0 = cn.add_column(string_fragment{"abc"});
    CHECK(name0 == "abc");
    auto name1 = cn.add_column(string_fragment{"def"});
    CHECK(name1 == "def");
}

TEST_CASE("column_namer::collisions")
{
    column_namer cn{column_namer::language::SQL};

    auto name0 = cn.add_column(string_fragment{"abc"});
    CHECK(name0 == "abc");
    auto name1 = cn.add_column(string_fragment{"abc"});
    CHECK(name1 == "abc_0");
    auto name2 = cn.add_column(string_fragment{"abc"});
    CHECK(name2 == "abc_1");
}
