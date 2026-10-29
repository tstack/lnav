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

namespace {

/** A 64-bit finalizer (splitmix64) to spread the bits of the input. */
uint64_t
mix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

}  // namespace

void
simple_bloom_set::insert(uint32_t val)
{
    const auto h = mix64(val);
    const auto h1 = static_cast<uint32_t>(h);
    // An odd step makes sure the probes are well distributed.
    const auto h2 = static_cast<uint32_t>(h >> 32) | 1U;

    for (size_t lpc = 0; lpc < HASH_COUNT; lpc++) {
        const auto bit = (h1 + lpc * h2) % BIT_COUNT;

        this->sbs_bits[bit / WORD_BITS] |= uint64_t{1} << (bit % WORD_BITS);
    }
}

bool
simple_bloom_set::contains(uint32_t val) const
{
    const auto h = mix64(val);
    const auto h1 = static_cast<uint32_t>(h);
    const auto h2 = static_cast<uint32_t>(h >> 32) | 1U;

    for (size_t lpc = 0; lpc < HASH_COUNT; lpc++) {
        const auto bit = (h1 + lpc * h2) % BIT_COUNT;

        if (!(this->sbs_bits[bit / WORD_BITS]
              & (uint64_t{1} << (bit % WORD_BITS))))
        {
            return false;
        }
    }

    return true;
}

void
simple_bloom_set::clear()
{
    this->sbs_bits.fill(0);
}
