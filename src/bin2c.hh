/**
 * Copyright (c) 2020, Timothy Stack
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
 *
 * @file bin2c.hh
 */

#ifndef lnav_bin2c_hh
#define lnav_bin2c_hh

#include <memory>

#include <sys/types.h>

#include "base/intern_string.hh"

struct bin_src_file {
    constexpr bin_src_file(const char* name,
                           const unsigned char* data,
                           size_t compressed_size,
                           size_t size);

    std::unique_ptr<string_fragment_producer> to_string_fragment_producer()
        const;

    const char* get_name() const { return this->bsf_name; }

private:
    const char* bsf_name;
    const unsigned char* bsf_compressed_data;
    size_t bsf_compressed_size;
};

constexpr bin_src_file::bin_src_file(const char* name,
                                     const unsigned char* data,
                                     size_t compressed_size,
                                     size_t size)
    : bsf_name(name), bsf_compressed_data(data),
      bsf_compressed_size(compressed_size)
{
}

#endif
