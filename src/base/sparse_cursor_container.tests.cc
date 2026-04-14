/**
 * Copyright (c) 2026, Timothy Stack
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

#include "sparse_cursor_container.hh"

#include "doctest/doctest.h"

TEST_CASE("sparse_cursor_container-basic")
{
    constexpr size_t NCOLS = 3;
    constexpr size_t NROWS = 500;

    lnav::cell_container cells;
    lnav::sparse_cursor_container<16> idx;
    idx.set_columns(NCOLS);

    for (size_t r = 0; r < NROWS; ++r) {
        idx.push_row(cells.end_cursor());
        cells.push_int_cell(static_cast<int64_t>(r * 100 + 0));
        cells.push_int_cell(static_cast<int64_t>(r * 100 + 1));
        cells.push_int_cell(static_cast<int64_t>(r * 100 + 2));
    }

    CHECK(idx.size() == NROWS);
    // One block cursor per 16 rows, rounded up.
    CHECK(idx.block_count() == (NROWS + 15) / 16);

    // Spot-check every 37th row to cover aligned/unaligned positions.
    for (size_t r = 0; r < NROWS; r += 37) {
        auto c = idx.at(r);
        CHECK(c.get_type() == lnav::cell_type::CT_INTEGER);
        CHECK(c.get_int() == static_cast<int64_t>(r * 100 + 0));

        auto c1 = c.next();
        REQUIRE(c1.has_value());
        CHECK(c1->get_int() == static_cast<int64_t>(r * 100 + 1));

        auto c2 = c1->next();
        REQUIRE(c2.has_value());
        CHECK(c2->get_int() == static_cast<int64_t>(r * 100 + 2));
    }

    // First and last.
    CHECK(idx.front().get_int() == 0);
    CHECK(idx.back().get_int()
          == static_cast<int64_t>((NROWS - 1) * 100 + 0));
}

TEST_CASE("sparse_cursor_container-empty")
{
    lnav::sparse_cursor_container<> idx;
    CHECK(idx.empty());
    CHECK(idx.size() == 0);
    CHECK(idx.block_count() == 0);
}

TEST_CASE("sparse_cursor_container-single-row")
{
    lnav::cell_container cells;
    lnav::sparse_cursor_container<16> idx;
    idx.set_columns(2);

    idx.push_row(cells.end_cursor());
    cells.push_text_cell(string_fragment::from_const("hello"));
    cells.push_int_cell(42);

    CHECK(idx.size() == 1);
    CHECK(idx.block_count() == 1);

    auto c = idx.at(0);
    CHECK(c.get_type() == lnav::cell_type::CT_TEXT);
    CHECK(c.get_text() == "hello");
}

TEST_CASE("sparse_cursor_container-spans-chunk-boundaries")
{
    // A `cell_container` chunk is ~32 KiB; each int cell is 9 bytes,
    // so two cells per row is 18 bytes/row.  Pushing 10000 rows forces
    // several new-chunk allocations, which is the case where a block-
    // start cursor captured by `end_cursor()` ends up pinned to a
    // chunk whose capacity was then exhausted — the cursor's
    // `c_offset` equals the old chunk's `cc_size`, and a subsequent
    // `next()` call reads past valid data unless `at()` syncs first.
    constexpr size_t NCOLS = 2;
    constexpr size_t NROWS = 10000;

    lnav::cell_container cells;
    lnav::sparse_cursor_container<64> idx;
    idx.set_columns(NCOLS);

    for (size_t r = 0; r < NROWS; ++r) {
        idx.push_row(cells.end_cursor());
        cells.push_int_cell(static_cast<int64_t>(r));
        cells.push_int_cell(static_cast<int64_t>(r + 1000000));
    }

    CHECK(idx.size() == NROWS);
    // Every row's first and second cell should read back its seeded
    // value, no matter where in the chunk chain it sits.
    for (size_t r = 0; r < NROWS; ++r) {
        auto c0 = idx.at(r);
        CHECK_MESSAGE(c0.get_type() == lnav::cell_type::CT_INTEGER,
                      "row ", r);
        CHECK_MESSAGE(c0.get_int() == static_cast<int64_t>(r),
                      "row ", r);

        auto c1 = c0.next();
        REQUIRE_MESSAGE(c1.has_value(), "row ", r);
        CHECK_MESSAGE(c1->get_int() == static_cast<int64_t>(r + 1000000),
                      "row ", r);
    }
}

TEST_CASE("sparse_cursor_container-clear")
{
    lnav::cell_container cells;
    lnav::sparse_cursor_container<16> idx;
    idx.set_columns(1);

    for (int i = 0; i < 50; ++i) {
        idx.push_row(cells.end_cursor());
        cells.push_int_cell(i);
    }
    CHECK(idx.size() == 50);

    idx.clear();
    cells.reset();

    CHECK(idx.empty());
    CHECK(idx.block_count() == 0);

    // Still usable after clear.
    idx.push_row(cells.end_cursor());
    cells.push_int_cell(999);
    CHECK(idx.size() == 1);
    CHECK(idx.at(0).get_int() == 999);
}
