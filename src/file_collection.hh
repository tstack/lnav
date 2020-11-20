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
 * @file file_collection.hh
 */

#ifndef lnav_file_collection_hh
#define lnav_file_collection_hh

#include <map>
#include <set>
#include <list>
#include <string>

#include "safe/safe.h"

#include "base/future_util.hh"
#include "logfile_fwd.hh"
#include "archive_manager.hh"
#include "file_format.hh"

struct scan_progress {
    std::list<archive_manager::extract_progress> sp_extractions;
};

using safe_scan_progress = safe::Safe<scan_progress>;

struct file_collection {
    bool fc_recursive{false};
    bool fc_rotated{false};

    std::map<std::string, std::string> fc_name_to_errors;
    std::map<std::string, logfile_open_options> fc_file_names;
    std::vector<std::shared_ptr<logfile>> fc_files;
    int fc_files_generation{0};
    std::vector<std::pair<std::shared_ptr<logfile>, std::string>>
        fc_renamed_files;
    std::set<std::string> fc_closed_files;
    std::map<std::string, file_format_t> fc_other_files;
    std::shared_ptr<safe_scan_progress> fc_progress;
    size_t fc_largest_path_length{0};

    file_collection()
        : fc_progress(std::make_shared<safe::Safe<scan_progress>>())
    {}

    void clear()
    {
        this->fc_name_to_errors.clear();
        this->fc_file_names.clear();
        this->fc_files.clear();
        this->fc_closed_files.clear();
        this->fc_other_files.clear();
    }

    file_collection rescan_files(bool required = false);

    void
    expand_filename(future_queue<file_collection> &fq, const std::string &path,
                    logfile_open_options &loo, bool required);

    std::future<file_collection>
    watch_logfile(const std::string &filename, logfile_open_options &loo,
                  bool required);

    void merge(const file_collection &other);

    void close_file(const std::shared_ptr<logfile> &lf);

    void regenerate_unique_file_names();
};


#endif
