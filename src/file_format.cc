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
 * @file file_format.hh
 */

#include <unordered_map>

#include "file_format.hh"

#include "archive_manager.hh"
#include "base/auto_fd.hh"
#include "base/fs_util.hh"
#include "base/intern_string.hh"
#include "base/lnav_log.hh"
#include "config.h"

static bool
is_pcap_header(uint8_t* buffer)
{
    size_t offset = 0;
    if (buffer[0] == 0x0a && buffer[1] == 0x0d && buffer[2] == 0x0d
        && buffer[3] == 0x0a)
    {
        offset += sizeof(uint32_t) * 2;
        if (buffer[offset + 0] == 0x1a && buffer[offset + 1] == 0x2b
            && buffer[offset + 2] == 0x3c && buffer[offset + 3] == 0x4d)
        {
            return true;
        }

        if (buffer[offset + 0] == 0x4d && buffer[offset + 1] == 0x3c
            && buffer[offset + 2] == 0x2b && buffer[offset + 3] == 0x1a)
        {
            return true;
        }
        return false;
    }

    if (buffer[0] == 0xa1 && buffer[1] == 0xb2 && buffer[2] == 0xc3
        && buffer[3] == 0xd4)
    {
        return true;
    }

    if (buffer[0] == 0xd4 && buffer[1] == 0xc3 && buffer[2] == 0xb2
        && buffer[3] == 0xa1)
    {
        return true;
    }

    if (buffer[0] == 0xa1 && buffer[1] == 0xb2 && buffer[2] == 0x3c
        && buffer[3] == 0x4d)
    {
        return true;
    }

    if (buffer[0] == 0x4d && buffer[1] == 0x3c && buffer[2] == 0xb2
        && buffer[3] == 0xa1)
    {
        return true;
    }

    return false;
}

file_format_t
detect_file_format(const ghc::filesystem::path& filename)
{
    if (archive_manager::is_archive(filename)) {
        return file_format_t::ARCHIVE;
    }

    file_format_t retval = file_format_t::UNKNOWN;
    auto_fd fd;

    if ((fd = lnav::filesystem::openp(filename, O_RDONLY)) != -1) {
        uint8_t buffer[32];
        ssize_t rc;

        if ((rc = read(fd, buffer, sizeof(buffer))) > 0) {
            static auto SQLITE3_HEADER = "SQLite format 3";
            auto header_frag = string_fragment(buffer, 0, rc);

            if (header_frag.startswith(SQLITE3_HEADER)) {
                retval = file_format_t::SQLITE_DB;
            } else if (rc > 24 && is_pcap_header(buffer)) {
                retval = file_format_t::PCAP;
            }
        }
    }

    return retval;
}
