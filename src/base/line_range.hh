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

#ifndef lnav_base_line_range_hh
#define lnav_base_line_range_hh

#include <string>

/**
 * Encapsulates a range in a string.
 */
struct line_range {
    enum class unit {
        bytes,
        codepoint,
    };

    static constexpr line_range empty_at(int start)
    {
        return line_range{start, start};
    }

    int lr_start;
    int lr_end;
    unit lr_unit;

    explicit constexpr line_range(int start = -1,
                                  int end = -1,
                                  unit u = unit::bytes)
        : lr_start(start), lr_end(end), lr_unit(u)
    {
    }

    bool is_valid() const { return this->lr_start != -1; }

    int length() const
    {
        return this->lr_end == -1 ? INT_MAX : this->lr_end - this->lr_start;
    }

    bool empty() const { return this->length() == 0; }

    void clear()
    {
        this->lr_start = -1;
        this->lr_end = -1;
    }

    int end_for_string(const std::string& str) const
    {
        return this->lr_end == -1 ? str.length() : this->lr_end;
    }

    bool contains(int pos) const
    {
        return this->lr_start <= pos
            && (this->lr_end == -1 || pos < this->lr_end);
    }

    bool contains(const line_range& other) const
    {
        return this->contains(other.lr_start)
            && (this->lr_end == -1 || other.lr_end <= this->lr_end);
    }

    bool intersects(const line_range& other) const
    {
        if (this->contains(other.lr_start)) {
            return true;
        }
        if (other.lr_end > 0 && this->contains(other.lr_end - 1)) {
            return true;
        }
        if (other.contains(this->lr_start)) {
            return true;
        }

        return false;
    }

    line_range intersection(const line_range& other) const;

    line_range& shift(int32_t start, int32_t amount);

    line_range& shift_range(const line_range& cover, int32_t amount);

    void ltrim(const char* str)
    {
        while (this->lr_start < this->lr_end && isspace(str[this->lr_start])) {
            this->lr_start += 1;
        }
    }

    bool operator<(const line_range& rhs) const
    {
        if (this->lr_start < rhs.lr_start) {
            return true;
        }
        if (this->lr_start > rhs.lr_start) {
            return false;
        }

        // this->lr_start == rhs.lr_start
        if (this->lr_end == rhs.lr_end) {
            return false;
        }

        if (this->empty()) {
            return true;
        }

        if (rhs.empty()) {
            return false;
        }

        // When the start is the same, the longer range has a lower priority
        // than the shorter range.
        if (rhs.lr_end == -1) {
            return false;
        }

        if ((this->lr_end == -1) || (this->lr_end > rhs.lr_end)) {
            return true;
        }
        return false;
    }

    bool operator==(const line_range& rhs) const
    {
        return (this->lr_start == rhs.lr_start && this->lr_end == rhs.lr_end);
    }

    const char* substr(const std::string& str) const
    {
        if (this->lr_start == -1) {
            return str.c_str();
        }
        return &(str.c_str()[this->lr_start]);
    }

    size_t sublen(const std::string& str) const
    {
        if (this->lr_start == -1) {
            return str.length();
        }
        if (this->lr_end == -1) {
            return str.length() - this->lr_start;
        }
        return this->length();
    }
};

#endif
