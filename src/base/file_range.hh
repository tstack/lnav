/**
 * Copyright (c) 2019, Timothy Stack
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

#ifndef lnav_file_range_hh
#define lnav_file_range_hh

#include <sys/types.h>

#include "intern_string.hh"

using file_off_t = int64_t;
using file_size_t = uint64_t;
using file_ssize_t = int64_t;

class file_range {
public:
    struct metadata {
        bool m_valid_utf{true};
        bool m_has_ansi{false};

        metadata& operator|=(const metadata& meta)
        {
            if (!meta.m_valid_utf) {
                this->m_valid_utf = false;
            }
            if (meta.m_has_ansi) {
                this->m_has_ansi = true;
            }

            return *this;
        }
    };

    file_off_t fr_offset{0};
    file_ssize_t fr_size{0};
    metadata fr_metadata;

    void clear()
    {
        this->fr_offset = 0;
        this->fr_size = 0;
    }

    ssize_t next_offset() const { return this->fr_offset + this->fr_size; }

    bool empty() const { return this->fr_size == 0; }
};

struct source_location {
    source_location()
        : sl_source(intern_string::lookup("unknown")), sl_line_number(0)
    {
    }

    explicit source_location(intern_string_t source, int32_t line = 0)
        : sl_source(source), sl_line_number(line)
    {
    }

    bool operator==(const source_location& rhs) const
    {
        return this->sl_source == rhs.sl_source
            && this->sl_line_number == rhs.sl_line_number;
    }

    intern_string_t sl_source;
    int32_t sl_line_number;
};

#define INTERNAL_SRC_LOC \
    (+[]() { \
        static const intern_string_t PATH \
            = intern_string::lookup("__" __FILE__); \
        return source_location{PATH, __LINE__}; \
    })()

#endif
