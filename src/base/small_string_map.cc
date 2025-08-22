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

#include "small_string_map.hh"

namespace lnav {

std::optional<uint32_t>
small_string_map::lookup(const string_fragment& in) const
{
    if (in.length() > MAX_KEY_SIZE) {
        return std::nullopt;
    }

    alignas(8) char in_key[MAX_KEY_SIZE]{};
    memcpy(in_key, in.data(), in.length());

    for (int lpc = 0; lpc < MAP_SIZE; ++lpc) {
        auto match = true;
#if 0
        for (int index = 0; index < MAX_KEY_SIZE; ++index) {
            if (this->ssm_keys[lpc * MAX_KEY_SIZE + index] != in_key[index]) {
                match = false;
            }
        }
#else
        if (memcmp(&this->ssm_keys[lpc * MAX_KEY_SIZE], in_key, MAX_KEY_SIZE)
            != 0)
        {
            match = false;
        }
#endif
        if (match) {
            return this->ssm_values[lpc];
        }
    }

    return std::nullopt;
}

void
small_string_map::insert(const string_fragment& key, uint32_t value)
{
    if (key.length() >= MAX_KEY_SIZE) {
        return;
    }

    auto key_index = this->ssm_used_keys < MAP_SIZE ? this->ssm_used_keys++
                                                    : key.hash() % MAP_SIZE;

    memset(&this->ssm_keys[key_index * MAX_KEY_SIZE],
           0,
           MAX_KEY_SIZE * sizeof(char));
    memcpy(&this->ssm_keys[key_index * MAX_KEY_SIZE], key.data(), key.length());
    this->ssm_values[key_index] = value;
}

}  // namespace lnav
