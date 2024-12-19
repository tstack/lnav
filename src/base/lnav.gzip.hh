/**
 * Copyright (c) 2021, Timothy Stack
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
 * @file lnav.gzip.hh
 */

#ifndef lnav_gzip_hh
#define lnav_gzip_hh

#include <string>

#include <sys/time.h>

#include "auto_mem.hh"
#include "intern_string.hh"
#include "result.h"

namespace lnav {
namespace gzip {

struct header {
    timeval h_mtime{};
    auto_buffer h_extra{auto_buffer::alloc(0)};
    std::string h_name;
    std::string h_comment;

    bool empty() const
    {
        return this->h_mtime.tv_sec == 0 && this->h_extra.empty()
            && this->h_name.empty() && this->h_comment.empty();
    }
};

bool is_gzipped(const char* buffer, size_t len);

Result<auto_buffer, std::string> compress(const void* input, size_t len);

Result<auto_buffer, std::string> uncompress(const std::string& src,
                                            const void* buffer,
                                            size_t size);

Result<std::unique_ptr<string_fragment_producer>, std::string>
uncompress_stream(const std::string& src,
                  const unsigned char* buffer,
                  size_t size);

}  // namespace gzip
}  // namespace lnav

#endif
