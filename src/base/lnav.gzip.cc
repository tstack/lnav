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
 * @file lnav.gzip.cc
 */

#include "lnav.gzip.hh"

#include <zlib.h>

#include "config.h"
#include "fmt/format.h"

namespace lnav {
namespace gzip {

bool
is_gzipped(const char* buffer, size_t len)
{
    return len > 2 && buffer[0] == '\037' && buffer[1] == '\213';
}

Result<auto_buffer, std::string>
compress(const void* input, size_t len)
{
    auto retval = auto_buffer::alloc(len + 4096);

    z_stream zs = {};
    zs.avail_in = (uInt) len;
    zs.next_in = (Bytef*) input;
    zs.avail_out = (uInt) retval.capacity();
    zs.next_out = (Bytef*) retval.in();

    auto rc = deflateInit2(
        &zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY);
    if (rc != Z_OK) {
        return Err(fmt::format(
            FMT_STRING("unable to initialize compressor -- {}"), zError(rc)));
    }
    rc = deflate(&zs, Z_FINISH);
    if (rc != Z_STREAM_END) {
        return Err(fmt::format(FMT_STRING("unable to compress data -- {}"),
                               zError(rc)));
    }
    rc = deflateEnd(&zs);
    if (rc != Z_OK) {
        return Err(fmt::format(
            FMT_STRING("unable to finalize compression -- {}"), zError(rc)));
    }
    return Ok(std::move(retval.resize(zs.total_out)));
}

Result<auto_buffer, std::string>
uncompress(const std::string& src, const void* buffer, size_t size)
{
    auto uncomp = auto_buffer::alloc(size * 2);
    z_stream strm = {};
    int err;

    strm.next_in = (Bytef*) buffer;
    strm.avail_in = size;

    if ((err = inflateInit2(&strm, (16 + MAX_WBITS))) != Z_OK) {
        return Err(fmt::format(FMT_STRING("invalid gzip data: {} -- {}"),
                               src,
                               strm.msg ? strm.msg : zError(err)));
    }

    bool done = false;

    while (!done) {
        if (strm.total_out >= uncomp.size()) {
            uncomp.expand_by(size / 2);
        }

        strm.next_out = (Bytef*) (uncomp.in() + strm.total_out);
        strm.avail_out = uncomp.capacity() - strm.total_out;

        // Inflate another chunk.
        err = inflate(&strm, Z_SYNC_FLUSH);
        if (err == Z_STREAM_END) {
            done = true;
        } else if (err != Z_OK) {
            inflateEnd(&strm);
            return Err(fmt::format(FMT_STRING("unable to uncompress: {} -- {}"),
                                   src,
                                   strm.msg ? strm.msg : zError(err)));
        }
    }

    if (inflateEnd(&strm) != Z_OK) {
        return Err(fmt::format(FMT_STRING("unable to uncompress: {} -- {}"),
                               src,
                               strm.msg ? strm.msg : zError(err)));
    }

    return Ok(std::move(uncomp.resize(strm.total_out)));
}

}  // namespace gzip
}  // namespace lnav
