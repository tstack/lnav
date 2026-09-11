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
#include <set>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "base/intern_string.hh"
#include "base/lnav.console.hh"
#include "base/lnav.resolver.hh"
#include "yajlpp/yajlpp.hh"

class log_vtab_manager;

std::vector<intern_string_t> load_format_file(
    const std::filesystem::path& filename,
    std::vector<lnav::console::user_message>& errors);

/**
 * Parse the format definitions in a file and run the semantic checks that
 * build() performs.  The formats are parsed into an empty registry that is
 * thrown away afterwards, so the result does not depend on, and cannot
 * disturb, the formats that have already been loaded.
 *
 * @return the names of the formats defined in the file
 */
std::vector<intern_string_t> validate_format_file(
    const std::filesystem::path& filename,
    std::vector<lnav::console::user_message>& errors);

void load_formats(const std::vector<std::filesystem::path>& extra_paths,
                  std::vector<lnav::console::user_message>& errors);

void load_format_vtabs(log_vtab_manager* vtab_manager,
                       std::vector<lnav::console::user_message>& errors);

/**
 * Execute the SQL scripts that have been installed.
 *
 * @param skip_paths scripts to leave alone.  Installing a script means
 *   running the copy that is being installed, so the copy that is already
 *   in place has to be passed over or the two collide.
 */
void load_format_extra(sqlite3* db,
                       const std::map<std::string, scoped_value_t>& global_vars,
                       const std::vector<std::filesystem::path>& extra_paths,
                       const std::set<std::filesystem::path>& skip_paths,
                       std::vector<lnav::console::user_message>& errors);

extern const json_path_container format_handlers;
extern const json_path_container root_format_handler;

#endif
