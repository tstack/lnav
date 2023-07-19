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

#include "piper.file.hh"

#include <arpa/inet.h>
#include <unistd.h>

#include "base/lnav_log.hh"
#include "base/paths.hh"

namespace lnav {
namespace piper {

const char HEADER_MAGIC[4] = {'L', 0, 'N', 1};

const ghc::filesystem::path&
storage_path()
{
    static auto INSTANCE = lnav::paths::workdir() / "piper";

    return INSTANCE;
}

nonstd::optional<auto_buffer>
read_header(int fd, const char* first8)
{
    if (memcmp(first8, HEADER_MAGIC, sizeof(HEADER_MAGIC)) != 0) {
        log_trace("first 4 bytes are not a piper header: %02x%02x%02x%02x",
                  first8[0],
                  first8[1],
                  first8[2],
                  first8[3]);
        return nonstd::nullopt;
    }

    uint32_t meta_size = ntohl(*((uint32_t*) &first8[4]));

    auto meta_buf = auto_buffer::alloc(meta_size);
    if (meta_buf.in() == nullptr) {
        log_error("failed to alloc %d bytes for header", meta_size);
        return nonstd::nullopt;
    }
    auto meta_prc = pread(fd, meta_buf.in(), meta_size, 8);
    if (meta_prc != meta_size) {
        log_error("failed to read piper header: %s", strerror(errno));
        return nonstd::nullopt;
    }
    meta_buf.resize(meta_size);

    return meta_buf;
}

}  // namespace piper
}  // namespace lnav
