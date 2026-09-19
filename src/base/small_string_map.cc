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

#include <cstring>

#include "small_string_map.hh"

#include "lnav_log.hh"

namespace lnav {

namespace {

/** Load four bytes with the first byte in the low bits. */
uint64_t
load_le32(const unsigned char* src)
{
    uint32_t retval;
    memcpy(&retval, src, sizeof(retval));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    retval = __builtin_bswap32(retval);
#endif
    return retval;
}

/**
 * Pack a key of 1 to MAX_KEY_SIZE bytes into a uint64_t with byte N of the
 * key in bits 8N to 8N+7 and zeros above the last byte.  The loads overlap
 * as needed, so no bytes past the end of the key are read.
 */
uint64_t
pack_key(const string_fragment& key)
{
    const auto* src = key.udata();
    const auto len = static_cast<size_t>(key.length());

    if (len >= 4) {
        // The overlapping bytes are the same in both loads, so OR-ing them
        // together is harmless.
        return load_le32(src) | (load_le32(&src[len - 4]) << ((len - 4) * 8));
    }

    const auto mid = len / 2;
    return uint64_t{src[0]} | (uint64_t{src[mid]} << (mid * 8))
        | (uint64_t{src[len - 1]} << ((len - 1) * 8));
}

/** Return a mask with bit N set when slot N holds the given key. */
uint8_t
match_mask(const uint64_t (&keys)[small_string_map::MAP_SIZE], uint64_t key)
{
    uint8_t retval = 0;

    for (int lpc = 0; lpc < small_string_map::MAP_SIZE; lpc++) {
        retval |= static_cast<uint8_t>(keys[lpc] == key) << lpc;
    }
    return retval;
}

}  // namespace

std::optional<uint32_t>
small_string_map::lookup(const string_fragment& in)
{
    if (in.empty() || in.length() > MAX_KEY_SIZE) {
        return std::nullopt;
    }

    const auto mask = match_mask(this->ssm_keys, pack_key(in)) & this->ssm_used;
    if (mask == 0) {
        return std::nullopt;
    }

    const auto index = __builtin_ctz(mask);
    this->ssm_start_index = index;
    this->ssm_age |= 1U << index;
    return this->ssm_values[index];
}

void
small_string_map::insert(const string_fragment& key, uint32_t value)
{
    if (key.empty() || key.length() > MAX_KEY_SIZE) {
        return;
    }

    // Fill an empty slot if there is one.  Otherwise, replace the highest
    // slot that has not been used recently, then start aging over again.
    const unsigned unused = static_cast<uint8_t>(~this->ssm_used);
    const unsigned unaged = static_cast<uint8_t>(~this->ssm_age);
    int key_index;
    if (unused != 0) {
        key_index = __builtin_ctz(unused);
    } else if (unaged != 0) {
        key_index = 31 - __builtin_clz(unaged);
    } else {
        key_index = (this->ssm_start_index + 1) % MAP_SIZE;
    }

    this->ssm_age = (1U << this->ssm_start_index) | (1U << key_index);
    this->ssm_used |= 1U << key_index;
    this->ssm_keys[key_index] = pack_key(key);
    this->ssm_values[key_index] = value;
    this->ssm_start_index = key_index;
}

}  // namespace lnav
