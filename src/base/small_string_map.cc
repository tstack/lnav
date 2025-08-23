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

#include "lnav_log.hh"

namespace lnav {

std::optional<uint32_t>
small_string_map::lookup(const string_fragment& in)
{
    if (in.length() > MAX_KEY_SIZE) {
        return std::nullopt;
    }

    auto index = this->ssm_start_index;
    for (int lpc = 0; lpc < MAP_SIZE; ++lpc) {
        if (memcmp(
                &this->ssm_keys[index * MAX_KEY_SIZE], in.data(), in.length())
                == 0
            && (in.length() == MAX_KEY_SIZE
                || this->ssm_keys[index * MAX_KEY_SIZE + in.length()] == '\0'))
        {
            this->ssm_start_index = index;
            this->ssm_age[index] = true;
            return this->ssm_values[index];
        }
        index = (index + 1) % MAP_SIZE;
    }
    return std::nullopt;
}

void
small_string_map::insert(const string_fragment& key, uint32_t value)
{
    if (key.empty() || key.length() > MAX_KEY_SIZE) {
        return;
    }

    auto key_index = (this->ssm_start_index + 1) % MAP_SIZE;
    for (auto lpc = 0; lpc < MAP_SIZE; lpc++) {
        if (!this->ssm_age[lpc]) {
            key_index = lpc;
        } else {
            this->ssm_age[lpc] = false;
        }
    }
    this->ssm_age[this->ssm_start_index] = true;
    this->ssm_age[key_index] = true;

    memset(&this->ssm_keys[key_index * MAX_KEY_SIZE],
           0,
           MAX_KEY_SIZE * sizeof(char));
    memcpy(&this->ssm_keys[key_index * MAX_KEY_SIZE], key.data(), key.length());
    this->ssm_values[key_index] = value;
    this->ssm_start_index = key_index;
}

}  // namespace lnav
