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
        .with_description("The default zone for log messages if the timestamp "
                          "does not include a zone.")
        .with_example("America/Los_Angeles")
        .for_field(&file_options::fo_default_zone),
};

static const typed_json_path_container<file_options_collection>
    pattern_to_options_handlers = {
        yajlpp::pattern_property_handler("(?<path>[^/]+)")
            .with_description("Path or glob pattern")
            .with_children(options_handlers)
            .for_field(&file_options_collection::foc_pattern_to_options),
};

static const typed_json_path_container<file_options_collection>
    collection_handlers = {
        yajlpp::property_handler("paths")
            .with_description("Mapping of file paths or glob patterns to the "
                              "associated options")
            .with_children(pattern_to_options_handlers),
};

bool
file_options::operator==(const lnav::file_options& rhs) const
{
    return this->fo_default_zone.pp_value == rhs.fo_default_zone.pp_value;
}

json_string
file_options::to_json_string() const
{
    return options_handlers.to_json_string(*this);
}

Result<file_options_collection, std::vector<lnav::console::user_message>>
file_options_collection::from_json(intern_string_t src,
                                   const string_fragment& frag)
{
    return collection_handlers.parser_for(src).of(frag);
}

std::string
file_options_collection::to_json() const
{
    return collection_handlers.formatter_for(*this)
        .with_config(yajl_gen_beautify, true)
        .to_string();
}

std::optional<std::pair<std::string, file_options>>
file_options_collection::match(const std::string& path) const
{
    const auto iter = this->foc_pattern_to_options.find(path);
    if (iter != this->foc_pattern_to_options.end()) {
        log_trace("  file options exact match: %s", path.c_str());
        return *iter;
    }

    for (const auto& pair : this->foc_pattern_to_options) {
        log_trace("  file options pattern check: %s ~ %s",
                  path.c_str(),
                  pair.first.c_str());
        auto rc = fnmatch(pair.first.c_str(), path.c_str(), FNM_PATHNAME);

        if (rc == 0) {
            return pair;
        }
        if (rc != FNM_NOMATCH) {
            log_error("fnmatch('%s', '%s') failed -- %s",
                      pair.first.c_str(),
                      path.c_str(),
                      strerror(errno));
        }
    }

    for (const auto& pair : this->foc_pattern_to_options) {
        log_trace("  file options prefix check: %s ~ %s",
                  path.c_str(),
                  pair.first.c_str());
        if (startswith(path, pair.first)) {
            return pair;
        }
    }

    return std::nullopt;
}

std::optional<std::pair<std::string, file_options>>
file_options_hier::match(const std::filesystem::path& path) const
{
    static const auto ROOT_PATH = std::filesystem::path("/");

    auto lookup_path = path.parent_path();

    while (true) {
        const auto iter = this->foh_path_to_collection.find(lookup_path);
        if (iter != this->foh_path_to_collection.end()) {
            return iter->second.match(path.string());
        }

        auto next_lookup_path = lookup_path.parent_path();
        if (lookup_path == next_lookup_path) {
            if (lookup_path != ROOT_PATH) {
                // remote paths won't end with root, so try that
                next_lookup_path = ROOT_PATH;
            } else {
                break;
            }
        }
        lookup_path = next_lookup_path;
    }
    return std::nullopt;
}

}  // namespace lnav
