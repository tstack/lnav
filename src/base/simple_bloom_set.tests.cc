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

#include "simple_bloom_set.hh"

#include "doctest/doctest.h"

TEST_CASE("simple_bloom_set empty")
{
    simple_bloom_set sbs;

    CHECK_FALSE(sbs.contains(0));
    CHECK_FALSE(sbs.contains(123));
    CHECK_FALSE(sbs.contains(UINT32_MAX));
}

TEST_CASE("simple_bloom_set no false negatives")
{
    simple_bloom_set sbs;

    for (uint32_t lpc = 0; lpc < 100; lpc++) {
        sbs.insert(lpc * 7919 + 13);
    }
    for (uint32_t lpc = 0; lpc < 100; lpc++) {
        CHECK(sbs.contains(lpc * 7919 + 13));
    }
}

TEST_CASE("simple_bloom_set false positive rate")
{
    simple_bloom_set sbs;

    for (uint32_t lpc = 0; lpc < 100; lpc++) {
        sbs.insert(lpc * 7919 + 13);
    }

    size_t false_positives = 0;
    const size_t trials = 10000;
    for (uint32_t lpc = 0; lpc < trials; lpc++) {
        // 7919 > 100 so these never collide with inserted values
        if (sbs.contains(0x80000000U + lpc)) {
            false_positives += 1;
        }
    }
    CHECK(false_positives < trials / 20);
}

TEST_CASE("simple_bloom_set clear")
{
    simple_bloom_set sbs;

    sbs.insert(42);
    CHECK(sbs.contains(42));
    sbs.clear();
    CHECK_FALSE(sbs.contains(42));
}
