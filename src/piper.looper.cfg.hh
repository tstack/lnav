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

#ifndef piper_looper_cfg_hh
#define piper_looper_cfg_hh

#include <map>
#include <string>

#include <stdint.h>

#include "pcrepp/pcre2pp.hh"
#include "yajlpp/yajlpp_def.hh"

namespace lnav {
namespace piper {

struct demux_def {
    bool dd_enabled{true};
    bool dd_valid{false};
    factory_container<pcre2pp::code> dd_control_pattern;
    factory_container<pcre2pp::code> dd_pattern;
    int dd_timestamp_capture_index{-1};
    int dd_muxid_capture_index{-1};
    int dd_body_capture_index{-1};
    std::map<std::string, int> dd_meta_capture_indexes;
};

struct config {
    uint64_t c_max_size{10ULL * 1024ULL * 1024ULL};
    uint32_t c_rotations{4};
    std::chrono::seconds c_ttl{std::chrono::hours(48)};

    std::map<std::string, demux_def> c_demux_definitions;
};

}  // namespace piper
}  // namespace lnav

#endif
