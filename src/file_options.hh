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

#ifndef lnav_file_options_hh
#define lnav_file_options_hh

#include <map>

#include "base/lnav.console.hh"
#include "base/result.h"
#include "date/tz.h"
#include "ghc/filesystem.hpp"
#include "mapbox/variant.hpp"
#include "safe/safe.h"
#include "yajlpp/yajlpp.hh"

namespace lnav {

struct file_options {
    positioned_property<const date::time_zone*> fo_default_zone{nullptr};

    bool empty() const { return this->fo_default_zone.pp_value == nullptr; }

    json_string to_json_string() const;

    bool operator==(const file_options& rhs) const;
};

struct file_options_collection {
    static Result<file_options_collection,
                  std::vector<lnav::console::user_message>>
    from_json(intern_string_t src, const string_fragment& frag);

    std::map<std::string, file_options> foc_pattern_to_options;

    std::optional<std::pair<std::string, file_options>> match(
        const std::string& path) const;

    std::string to_json() const;
};

struct file_options_hier {
    std::map<ghc::filesystem::path, file_options_collection>
        foh_path_to_collection;
    size_t foh_generation{0};

    std::optional<std::pair<std::string, file_options>> match(
        const ghc::filesystem::path& path) const;
};

using safe_file_options_hier = safe::Safe<file_options_hier>;

}  // namespace lnav

#endif
