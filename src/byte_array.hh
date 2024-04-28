/**
 * Copyright (c) 2007-2013, Timothy Stack
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
#ifndef byte_array_hh
#define byte_array_hh

#include <optional>
#include <ostream>
#include <string>

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "base/lnav_log.hh"
#include "fmt/format.h"

template<size_t COUNT, typename T = unsigned char>
struct byte_array {
    static constexpr size_t BYTE_COUNT = COUNT * sizeof(T);
    static constexpr size_t STRING_SIZE = BYTE_COUNT * 2 + 1;

    byte_array() = default;

    static byte_array from(std::initializer_list<T> bytes)
    {
        byte_array retval;
        size_t index = 0;

        for (const auto by : bytes) {
            retval.ba_data[index++] = by;
        }
        return retval;
    }

    byte_array(const byte_array& other)
    {
        memcpy(this->ba_data, other.ba_data, BYTE_COUNT);
    }

    bool operator<(const byte_array& other) const
    {
        return memcmp(this->ba_data, other.ba_data, BYTE_COUNT) < 0;
    }

    bool operator!=(const byte_array& other) const
    {
        return memcmp(this->ba_data, other.ba_data, BYTE_COUNT) != 0;
    }

    bool operator==(const byte_array& other) const
    {
        return memcmp(this->ba_data, other.ba_data, BYTE_COUNT) == 0;
    }

    void clear() { memset(this->ba_data, 0, BYTE_COUNT); }

    template<typename OutputIt>
    void to_string(OutputIt out,
                   std::optional<char> separator = std::nullopt) const
    {
        for (size_t lpc = 0; lpc < BYTE_COUNT; lpc++) {
            if (lpc > 0 && separator) {
                *out = separator.value();
            }
            fmt::format_to(out, FMT_STRING("{:02x}"), this->ba_data[lpc]);
        }
    }

    std::string to_uuid_string() const
    {
        return fmt::format(
            FMT_STRING("{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-"
                       "{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}"),
            this->ba_data[0 % BYTE_COUNT],
            this->ba_data[1 % BYTE_COUNT],
            this->ba_data[2 % BYTE_COUNT],
            this->ba_data[3 % BYTE_COUNT],
            this->ba_data[4 % BYTE_COUNT],
            this->ba_data[5 % BYTE_COUNT],
            this->ba_data[6 % BYTE_COUNT],
            this->ba_data[7 % BYTE_COUNT],
            this->ba_data[8 % BYTE_COUNT],
            this->ba_data[9 % BYTE_COUNT],
            this->ba_data[10 % BYTE_COUNT],
            this->ba_data[11 % BYTE_COUNT],
            this->ba_data[12 % BYTE_COUNT],
            this->ba_data[13 % BYTE_COUNT],
            this->ba_data[14 % BYTE_COUNT],
            this->ba_data[15 % BYTE_COUNT]);
    }

    std::string to_string(std::optional<char> separator = std::nullopt) const
    {
        std::string retval;

        retval.reserve(STRING_SIZE);
        this->to_string(std::back_inserter(retval), separator);
        return retval;
    }

    const unsigned char* in() const { return this->ba_data; }

    T* out(int offset = 0)
    {
        T* ptr = (T*) this->ba_data;

        return &ptr[offset];
    }

    unsigned char ba_data[BYTE_COUNT]{};
};

template<size_t COUNT, typename T = unsigned char>
std::ostream&
operator<<(std::ostream& os, const byte_array<COUNT, T>& ba)
{
    os << ba.to_string();
    return os;
}

#endif
