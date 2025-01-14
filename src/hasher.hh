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
        auto bits = this->to_array();

        fmt::format_to(
            buf,
            FMT_STRING("{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}"
                       "{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}"),
            bits.ba_data[0],
            bits.ba_data[1],
            bits.ba_data[2],
            bits.ba_data[3],
            bits.ba_data[4],
            bits.ba_data[5],
            bits.ba_data[6],
            bits.ba_data[7],
            bits.ba_data[8],
            bits.ba_data[9],
            bits.ba_data[10],
            bits.ba_data[11],
            bits.ba_data[12],
            bits.ba_data[13],
            bits.ba_data[14],
            bits.ba_data[15]);
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
