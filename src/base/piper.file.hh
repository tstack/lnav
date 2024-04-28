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

#ifndef lnav_piper_file_hh
#define lnav_piper_file_hh

#include <map>
#include <optional>
#include <string>

#include <sys/time.h>

#include "auto_mem.hh"
#include "base/intern_string.hh"
#include "ghc/filesystem.hpp"
#include "time_util.hh"

namespace lnav {
namespace piper {

struct header {
    timeval h_ctime{};
    std::string h_name;
    std::string h_cwd;
    std::map<std::string, std::string> h_env;
    std::string h_timezone;
    std::map<std::string, std::string> h_demux_meta;

    bool operator<(const header& rhs) const
    {
        if (this->h_ctime < rhs.h_ctime) {
            return true;
        }

        if (this->h_ctime == rhs.h_ctime) {
            return this->h_name < rhs.h_name;
        }

        return false;
    }
};

const ghc::filesystem::path& storage_path();

constexpr size_t HEADER_SIZE = 8;
extern const char HEADER_MAGIC[4];

std::optional<auto_buffer> read_header(int fd, const char* first8);

std::optional<std::string> multiplex_id_for_line(string_fragment line);

}  // namespace piper
}  // namespace lnav

#endif
