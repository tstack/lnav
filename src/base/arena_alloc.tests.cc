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

#include <cstring>

#include "ArenaAlloc/arenaalloc.h"
#include "doctest/doctest.h"

TEST_CASE("arena-rollback-same-block")
{
    ArenaAlloc::Alloc<char> arena;

    auto* kept = arena.allocate(16);
    strcpy(kept, "keep me");
    const auto bytes_before = arena.getNumBytesAllocated();

    const auto cp = arena.getCheckpoint();
    auto* dropped = arena.allocate(32);
    CHECK(arena.getNumBytesAllocated() == bytes_before + 32);

    arena.rollback(cp);
    CHECK(arena.getNumBytesAllocated() == bytes_before);
    CHECK(strcmp(kept, "keep me") == 0);

    // the space is handed out again
    CHECK(arena.allocate(32) == dropped);
}

TEST_CASE("arena-rollback-across-blocks")
{
    // the smallest block size the arena allows
    ArenaAlloc::Alloc<char> arena(256);

    auto* kept = arena.allocate(16);
    strcpy(kept, "keep me");
    const auto bytes_before = arena.getNumBytesAllocated();

    const auto cp = arena.getCheckpoint();
    auto* next_in_block = arena.allocate(8);
    arena.rollback(cp);

    // too big for the current block, so new blocks are added
    auto* big = arena.allocate(4096);
    memset(big, 'x', 4096);
    arena.allocate(4096);
    CHECK(arena.getNumBytesAllocated() == bytes_before + 8192);

    arena.rollback(cp);
    CHECK(arena.getNumBytesAllocated() == bytes_before);
    CHECK(strcmp(kept, "keep me") == 0);

    // allocation resumes in the checkpointed block
    CHECK(arena.allocate(8) == next_in_block);

    // and the arena can still grow afterward
    auto* after = arena.allocate(4096);
    REQUIRE(after != nullptr);
    memset(after, 'y', 4096);
    CHECK(strcmp(kept, "keep me") == 0);
}

TEST_CASE("arena-rollback-repeated")
{
    ArenaAlloc::Alloc<char> arena(256);

    arena.allocate(16);
    const auto bytes_before = arena.getNumBytesAllocated();
    for (int lpc = 0; lpc < 1000; lpc++) {
        const auto cp = arena.getCheckpoint();
        arena.allocate(lpc % 2 == 0 ? 64 : 1024);
        arena.rollback(cp);
    }
    CHECK(arena.getNumBytesAllocated() == bytes_before);
}
