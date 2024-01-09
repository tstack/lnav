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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file file_collection.hh
 */

#ifndef lnav_file_collection_hh
#define lnav_file_collection_hh

#include <forward_list>
#include <list>
#include <map>
#include <set>
#include <string>
#include <utility>

#include <sys/resource.h>

#include "archive_manager.hh"
#include "base/auto_pid.hh"
#include "base/future_util.hh"
#include "file_format.hh"
#include "logfile_fwd.hh"
#include "safe/safe.h"

struct tailer_progress {
    std::string tp_message;
};

struct scan_progress {
    std::list<archive_manager::extract_progress> sp_extractions;
    std::map<std::string, tailer_progress> sp_tailers;

    bool empty() const
    {
        return this->sp_extractions.empty() && this->sp_tailers.empty();
    }
};

using safe_scan_progress = safe::Safe<scan_progress>;

struct other_file_descriptor {
    file_format_t ofd_format;
    std::string ofd_description;

    other_file_descriptor(file_format_t format = file_format_t::UNKNOWN,
                          std::string description = "")
        : ofd_format(format), ofd_description(std::move(description))
    {
    }
};

struct file_error_info {
    const time_t fei_mtime;
    const std::string fei_description;
};

using safe_name_to_errors = safe::Safe<std::map<std::string, file_error_info>>;

struct file_collection;

enum class child_poll_result_t {
    ALIVE,
    FINISHED,
};

class child_poller {
public:
    explicit child_poller(
        nonstd::optional<std::string> filename,
        auto_pid<process_state::running> child,
        std::function<void(file_collection&,
                           auto_pid<process_state::finished>&)> finalizer)
        : cp_filename(filename), cp_child(std::move(child)),
          cp_finalizer(std::move(finalizer))
    {
        ensure(this->cp_finalizer);
    }

    child_poller(child_poller&& other) noexcept
        : cp_filename(other.cp_filename), cp_child(std::move(other.cp_child)),
          cp_finalizer(std::move(other.cp_finalizer))
    {
        ensure(this->cp_finalizer);
    }

    child_poller& operator=(child_poller&& other) noexcept
    {
        require(other.cp_finalizer);

        this->cp_filename = other.cp_filename;
        this->cp_child = std::move(other.cp_child);
        this->cp_finalizer = std::move(other.cp_finalizer);

        return *this;
    }

    ~child_poller() noexcept = default;

    child_poller(const child_poller&) = delete;

    child_poller& operator=(const child_poller&) = delete;

    const nonstd::optional<std::string>& get_filename() const
    {
        return this->cp_filename;
    }

    void send_sigint();

    child_poll_result_t poll(file_collection& fc);

private:
    nonstd::optional<std::string> cp_filename;
    nonstd::optional<auto_pid<process_state::running>> cp_child;
    std::function<void(file_collection&, auto_pid<process_state::finished>&)>
        cp_finalizer;
};

struct file_collection {
    bool fc_invalidate_merge{false};

    bool fc_recursive{false};
    bool fc_rotated{false};

    std::shared_ptr<safe_name_to_errors> fc_name_to_errors{
        std::make_shared<safe_name_to_errors>()};
    std::map<std::string, logfile_open_options> fc_file_names;
    std::vector<std::shared_ptr<logfile>> fc_files;
    int fc_files_generation{0};
    std::vector<std::pair<std::shared_ptr<logfile>, std::string>>
        fc_renamed_files;
    std::set<std::string> fc_closed_files;
    std::map<std::string, other_file_descriptor> fc_other_files;
    std::set<std::string> fc_synced_files;
    std::shared_ptr<safe_scan_progress> fc_progress{
        std::make_shared<safe_scan_progress>()};
    std::vector<struct stat> fc_new_stats;
    std::list<child_poller> fc_child_pollers;
    size_t fc_largest_path_length{0};

    struct limits_t {
        limits_t();

        rlim_t l_fds;
        rlim_t l_open_files;
    };

    static const limits_t& get_limits();

    file_collection() = default;
    file_collection(const file_collection&) = delete;
    file_collection& operator=(const file_collection&) = delete;
    file_collection(file_collection&&) = default;

    file_collection copy();

    bool empty() const
    {
        return this->fc_name_to_errors->readAccess()->empty()
            && this->fc_file_names.empty() && this->fc_files.empty()
            && this->fc_progress->readAccess()->empty()
            && this->fc_other_files.empty();
    }

    void clear()
    {
        this->fc_name_to_errors->writeAccess()->clear();
        this->fc_file_names.clear();
        this->fc_files.clear();
        this->fc_closed_files.clear();
        this->fc_other_files.clear();
        this->fc_new_stats.clear();
    }

    bool is_below_open_file_limit() const
    {
        return this->fc_files.size() < get_limits().l_open_files;
    }

    size_t other_file_format_count(file_format_t ff) const;

    file_collection rescan_files(bool required = false);

    void expand_filename(lnav::futures::future_queue<file_collection>& fq,
                         const std::string& path,
                         logfile_open_options& loo,
                         bool required);

    nonstd::optional<std::future<file_collection>> watch_logfile(
        const std::string& filename, logfile_open_options& loo, bool required);

    void merge(file_collection& other);

    void request_close(const std::shared_ptr<logfile>& lf);

    void close_files(const std::vector<std::shared_ptr<logfile>>& files);

    void regenerate_unique_file_names();

    size_t active_pipers() const;

    size_t finished_pipers();
};

#endif
