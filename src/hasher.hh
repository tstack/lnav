/**
 * Copyright (c) 2023, Timothy Stack
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

#ifndef lnav_hasher_hh
#define lnav_hasher_hh

#include <string>

#include <stdint.h>

#include "base/auto_mem.hh"
#include "base/intern_string.hh"
#include "byte_array.hh"
#include "spookyhash/SpookyV2.h"

class hasher {
public:
    using array_t = byte_array<2, uint64_t>;
    static constexpr size_t STRING_SIZE = array_t::STRING_SIZE;

    hasher() { this->h_context.Init(0, 0); }

    hasher& update(const std::string& str)
    {
        this->h_context.Update(str.data(), str.length());

        return *this;
    }

    hasher& update(const string_fragment& str)
    {
        this->h_context.Update(str.data(), str.length());

        return *this;
    }

    hasher& update(const char* bits, size_t len)
    {
        this->h_context.Update(bits, len);

        return *this;
    }

    hasher& update(int64_t value)
    {
        value = SPOOKYHASH_LITTLE_ENDIAN_64(value);
        this->h_context.Update(&value, sizeof(value));

        return *this;
    }

    array_t to_array() const
    {
        uint64_t h1;
        uint64_t h2;
        array_t retval;

        this->h_context.Final(&h1, &h2);
        *retval.out(0) = SPOOKYHASH_LITTLE_ENDIAN_64(h1);
        *retval.out(1) = SPOOKYHASH_LITTLE_ENDIAN_64(h2);
        return retval;
    }

    void to_string(auto_buffer& buf) const
    {
        auto bits = this->to_array();

        bits.to_string(std::back_inserter(buf));
    }

    void to_string(char buf[STRING_SIZE])
    {
        static const char HEX_DIGITS[] = {
            '0', '1', '2', '3', '4', '5', '6', '7',
            '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
        };

        auto bits = this->to_array();

        buf[0] = HEX_DIGITS[bits.ba_data[0] >> 4U];
        buf[1] = HEX_DIGITS[bits.ba_data[0] & 0x0fU];
        buf[2] = HEX_DIGITS[bits.ba_data[1] >> 4U];
        buf[3] = HEX_DIGITS[bits.ba_data[1] & 0x0fU];
        buf[4] = HEX_DIGITS[bits.ba_data[2] >> 4U];
        buf[5] = HEX_DIGITS[bits.ba_data[2] & 0x0fU];
        buf[6] = HEX_DIGITS[bits.ba_data[3] >> 4U];
        buf[7] = HEX_DIGITS[bits.ba_data[3] & 0x0fU];
        buf[8] = HEX_DIGITS[bits.ba_data[4] >> 4U];
        buf[9] = HEX_DIGITS[bits.ba_data[4] & 0x0fU];
        buf[10] = HEX_DIGITS[bits.ba_data[5] >> 4U];
        buf[11] = HEX_DIGITS[bits.ba_data[5] & 0x0fU];
        buf[12] = HEX_DIGITS[bits.ba_data[6] >> 4U];
        buf[13] = HEX_DIGITS[bits.ba_data[6] & 0x0fU];
        buf[14] = HEX_DIGITS[bits.ba_data[7] >> 4U];
        buf[15] = HEX_DIGITS[bits.ba_data[7] & 0x0fU];
        buf[16] = HEX_DIGITS[bits.ba_data[8] >> 4U];
        buf[17] = HEX_DIGITS[bits.ba_data[8] & 0x0fU];
        buf[18] = HEX_DIGITS[bits.ba_data[9] >> 4U];
        buf[19] = HEX_DIGITS[bits.ba_data[9] & 0x0fU];
        buf[20] = HEX_DIGITS[bits.ba_data[10] >> 4U];
        buf[21] = HEX_DIGITS[bits.ba_data[10] & 0x0fU];
        buf[22] = HEX_DIGITS[bits.ba_data[11] >> 4U];
        buf[23] = HEX_DIGITS[bits.ba_data[11] & 0x0fU];
        buf[24] = HEX_DIGITS[bits.ba_data[12] >> 4U];
        buf[25] = HEX_DIGITS[bits.ba_data[12] & 0x0fU];
        buf[26] = HEX_DIGITS[bits.ba_data[13] >> 4U];
        buf[27] = HEX_DIGITS[bits.ba_data[13] & 0x0fU];
        buf[28] = HEX_DIGITS[bits.ba_data[14] >> 4U];
        buf[29] = HEX_DIGITS[bits.ba_data[14] & 0x0fU];
        buf[30] = HEX_DIGITS[bits.ba_data[15] >> 4U];
        buf[31] = HEX_DIGITS[bits.ba_data[15] & 0x0fU];
        buf[32] = '\0';
    }

    std::string to_string() const
    {
        auto bits = this->to_array();
        return bits.to_string();
    }

    std::string to_uuid_string() const
    {
        auto bits = this->to_array();
        return bits.to_uuid_string();
    }

private:
    SpookyHash h_context;
};

#endif
