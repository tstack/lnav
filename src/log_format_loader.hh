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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file log_format_loader.hh
 */

#ifndef __log_format_loader_hh
#define __log_format_loader_hh

#include <sqlite3.h>

#include <vector>
#include <string>

class log_vtab_manager;

std::vector<intern_string_t> load_format_file(
        const std::string &filename, std::vector<std::string> &errors);

void load_formats(const std::vector<std::string> &extra_paths,
                  std::vector<std::string> &errors);

void load_format_vtabs(log_vtab_manager *vtab_manager,
                       std::vector<std::string> &errors);

void load_format_extra(sqlite3 *db,
                       const std::vector<std::string> &extra_paths,
                       std::vector<std::string> &errors);

struct script_metadata {
    std::string sm_path;
    std::string sm_name;
    std::string sm_synopsis;
    std::string sm_description;
};

void extract_metadata_from_file(struct script_metadata &meta_inout);

void find_format_scripts(const std::vector<std::string> &extra_paths,
                         std::map<std::string, std::vector<script_metadata> > &scripts);

extern struct json_path_handler format_handlers[];

#endif
