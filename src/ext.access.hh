/**
 * Copyright (c) 2026, Timothy Stack
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

#ifndef lnav_ext_access_hh
#define lnav_ext_access_hh

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "base/intern_string.hh"
#include "yajlpp/yajlpp.hh"

namespace lnav::ext {

/** The contents of a discovery file. */
struct instance_record {
    int64_t ir_pid{0};
    std::string ir_version;
    std::string ir_name;
    std::string ir_cwd;
    std::vector<std::string> ir_paths;
    std::string ir_url;
    int64_t ir_port{0};
    std::string ir_api_key;
    int64_t ir_started{0};

    static const string_fragment SCHEMA_ID;
    static const typed_json_path_container<instance_record> handlers;
};

/**
 * The details of this instance's external-access server that are written
 * to the discovery file so that clients can find and authenticate to it.
 */
struct instance_info {
    uint16_t ii_port{0};
    std::string ii_url;
    /** The raw API key, before Base64 encoding. */
    std::string ii_api_key;
    std::string ii_name;
};

/**
 * Remember the paths given on the command-line so the discovery file can
 * show users which lnav is which.  Local paths should already be absolute.
 */
void set_command_line_paths(std::vector<std::string> paths);

/** The directory that holds a discovery file for each running instance. */
std::filesystem::path instance_dir();

/** A random API key for when the user did not provide one. */
std::string generate_api_key();

/**
 * Record this instance as running so clients can discover it and remove
 * the files left behind by instances that have exited.
 */
void register_instance(instance_info ii);

/** Rewrite the discovery file, for example, after the cwd changed. */
void refresh_instance();

/** Remove this instance's discovery file. */
void unregister_instance();

std::optional<instance_info> current_instance();

/** The discovery files of the other instances that are still running. */
std::vector<instance_record> running_instances();

}  // namespace lnav::ext

#endif
