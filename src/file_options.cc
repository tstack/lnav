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

#include "file_options.hh"

#include <fnmatch.h>

#include "base/lnav_log.hh"
#include "yajlpp/yajlpp.hh"
#include "yajlpp/yajlpp_def.hh"

namespace lnav {

static const typed_json_path_container<file_options> options_handlers = {
    yajlpp::property_handler("default-zone")
        .with_synopsis("<zone>")
        .with_description("The default zone")
        .with_example("America/Los_Angeles"),
};

static const typed_json_path_container<file_options_collection>
    collection_handlers = {
        yajlpp::pattern_property_handler("(.*)")
            .with_description("Path pattern")
            .with_children(options_handlers),
};

bool
file_options::operator==(const lnav::file_options& rhs) const
{
    return this->fo_default_zone == rhs.fo_default_zone;
}

nonstd::optional<file_options>
file_options_collection::match(const std::string& path) const
{
    auto iter = this->foc_pattern_to_options.find(path);
    if (iter != this->foc_pattern_to_options.end()) {
        return iter->second;
    }

    for (const auto& pair : this->foc_pattern_to_options) {
        auto rc = fnmatch(pair.first.c_str(), path.c_str(), FNM_PATHNAME);

        if (rc == 0) {
            return pair.second;
        }
        if (rc != FNM_NOMATCH) {
            log_error("fnmatch('%s', '%s') failed -- %s",
                      pair.first.c_str(),
                      path.c_str(),
                      strerror(errno));
        }
    }

    return nonstd::nullopt;
}

nonstd::optional<file_options>
file_options_hier::match(const ghc::filesystem::path& path) const
{
    auto lookup_path = path.parent_path();

    while (true) {
        const auto iter = this->foh_path_to_collection.find(lookup_path);
        if (iter != this->foh_path_to_collection.end()) {
            return iter->second.match(path.string());
        }

        auto next_lookup_path = lookup_path.parent_path();
        if (lookup_path == next_lookup_path) {
            break;
        }
        lookup_path = next_lookup_path;
    }
    return nonstd::nullopt;
}

}  // namespace lnav
