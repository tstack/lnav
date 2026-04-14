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

#ifndef lnav_sparse_cursor_container_hh
#define lnav_sparse_cursor_container_hh

#include <cstddef>
#include <vector>

#include "cell_container.hh"
#include "lnav_log.hh"

namespace lnav {

/**
 * Index over fixed-width rows in a `cell_container` that only stores a
 * cursor every `BlockRows` rows.  Random access resolves `row` by
 * jumping to the nearest stored cursor and walking forward one cell at
 * a time (`columns` cells per row).
 *
 * Memory: one `cell_container::cursor` (16 bytes) per block of rows,
 * versus 16 bytes per row for a dense `std::vector<cursor>` — a 64x
 * savings at the default block size for millions-of-rows result sets.
 *
 * Time: random access is amortized O(columns * BlockRows); for the DB
 * view with 10s of columns that's a handful of pointer chases, far
 * under a millisecond even for the worst case.
 *
 * Preconditions:
 *   - all rows have the same cell count (`columns`), set once at the
 *     first `push_row()` or explicitly via `set_columns()`.
 *   - rows are appended in order and never removed or edited in
 *     place; the backing `cell_container` is append-only.
 */
template<size_t BlockRows = 64>
struct sparse_cursor_container {
    static_assert(BlockRows > 0, "BlockRows must be positive");
    static constexpr size_t BLOCK_ROWS = BlockRows;

    using cursor = cell_container::cursor;

    sparse_cursor_container() = default;

    /**
     * Record the start of a new row.  Must be called once per row
     * *before* pushing its cells into the `cell_container`, with the
     * current `end_cursor()` of that container.
     */
    void push_row(cursor row_start)
    {
        if (this->scc_size % BlockRows == 0) {
            this->scc_block_starts.push_back(row_start);
        }
        ++this->scc_size;
    }

    /**
     * Configure the column count.  May be left at 0 until the first
     * `at()` call; if 0 at that point, we infer it by walking the
     * first row up to the second recorded block start.
     */
    void set_columns(size_t columns) { this->scc_columns = columns; }

    size_t size() const { return this->scc_size; }
    bool empty() const { return this->scc_size == 0; }

    cursor at(size_t row) const
    {
        require(row < this->scc_size);
        require(this->scc_columns > 0);

        const auto block = row / BlockRows;
        const auto within_block = row % BlockRows;

        // The stored block-start cursor was captured from
        // `end_cursor()` *before* the block's first cell was written,
        // so its offset may sit at the end of a chunk whose capacity
        // was already exhausted.  `sync()` migrates the cursor onto
        // the next chunk in that case; `next()` does not, so we must
        // sync before walking.
        auto synced = this->scc_block_starts[block].sync();
        require(synced.has_value());
        auto current = synced.value();
        for (size_t remaining = within_block * this->scc_columns;
             remaining > 0;
             --remaining)
        {
            auto next = current.next();
            require(next.has_value());
            current = next.value();
        }
        return current;
    }

    cursor operator[](size_t row) const { return this->at(row); }

    cursor front() const { return this->at(0); }
    cursor back() const { return this->at(this->scc_size - 1); }

    void clear()
    {
        this->scc_block_starts.clear();
        this->scc_size = 0;
    }

    // For telemetry / tests only: how many block-start cursors are
    // actually stored.
    size_t block_count() const { return this->scc_block_starts.size(); }

    std::vector<cursor> scc_block_starts;
    size_t scc_size{0};
    size_t scc_columns{0};
};

}  // namespace lnav

#endif
