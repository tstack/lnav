/**
 * Copyright (c) 2013, Timothy Stack
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
 * @file log_format_loader.hh
 */

#ifndef log_format_loader_hh
#define log_format_loader_hh

#include <filesystem>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "base/intern_string.hh"
#include "base/lnav.console.hh"
#include "base/lnav.resolver.hh"
#include "text_format.hh"
#include "yajlpp/yajlpp_def.hh"

class log_vtab_manager;

std::vector<intern_string_t> load_format_file(
    const std::filesystem::path& filename,
    std::vector<lnav::console::user_message>& errors);

void load_formats(const std::vector<std::filesystem::path>& extra_paths,
                  std::vector<lnav::console::user_message>& errors);

void load_format_vtabs(log_vtab_manager* vtab_manager,
                       std::vector<lnav::console::user_message>& errors);

void load_format_extra(sqlite3* db,
                       const std::map<std::string, scoped_value_t>& global_vars,
                       const std::vector<std::filesystem::path>& extra_paths,
                       std::vector<lnav::console::user_message>& errors);

struct script_metadata {
    std::filesystem::path sm_path;
    std::string sm_name;
    std::string sm_synopsis;
    std::string sm_description;
    text_format_t sm_output_format{text_format_t::TF_UNKNOWN};
};

void extract_metadata_from_file(script_metadata& meta_inout);

struct available_scripts {
    std::map<std::string, std::vector<script_metadata>> as_scripts;
};

void find_format_scripts(const std::vector<std::filesystem::path>& extra_paths,
                         available_scripts& scripts);

extern const json_path_container format_handlers;
extern const json_path_container root_format_handler;

#endif
