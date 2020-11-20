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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file file_format.hh
 */

#include "config.h"

#include "base/intern_string.hh"
#include "auto_fd.hh"
#include "lnav_util.hh"
#include "file_format.hh"
#include "archive_manager.hh"

file_format_t detect_file_format(const ghc::filesystem::path &filename)
{
    if (archive_manager::is_archive(filename)) {
        return file_format_t::FF_ARCHIVE;
    }

    file_format_t retval = file_format_t::FF_UNKNOWN;
    auto_fd       fd;

    if ((fd = openp(filename, O_RDONLY)) != -1) {
        char buffer[32];
        ssize_t rc;

        if ((rc = read(fd, buffer, sizeof(buffer))) > 0) {
            static auto SQLITE3_HEADER = "SQLite format 3";
            auto header_frag = string_fragment(buffer, 0, rc);

            if (header_frag.startswith(SQLITE3_HEADER)) {
                retval = file_format_t::FF_SQLITE_DB;
            }
        }
    }

    return retval;
}
