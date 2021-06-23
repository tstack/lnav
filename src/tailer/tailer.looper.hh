/**
 * Copyright (c) 2021, Timothy Stack
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
 */

#ifndef lnav_tailer_looper_hh
#define lnav_tailer_looper_hh

#include <set>
#include <logfile_fwd.hh>

#include "base/isc.hh"
#include "base/auto_pid.hh"
#include "base/network.tcp.hh"
#include "auto_fd.hh"
#include "ghc/filesystem.hpp"
#include "mapbox/variant.hpp"

namespace tailer {

class looper : public isc::service<looper> {
public:
    void add_remote(const network::path &path,
                    logfile_open_options options);

    void load_preview(int64_t id, const network::path& path);

    void complete_path(const network::path& path);

    bool empty() const {
        return this->l_netlocs_to_paths.empty();
    }

    std::set<std::string> active_netlocs() const {
        std::set<std::string> retval;

        for (const auto& pair : this->l_remotes) {
            retval.insert(pair.first);
        }
        return retval;
    }

protected:
    void loop_body() override;

    void child_finished(std::shared_ptr<service_base> child) override;

private:

    class host_tailer : public isc::service<host_tailer> {
    public:
        static Result<std::shared_ptr<host_tailer>, std::string> for_host(
            const std::string& netloc);

        host_tailer(const std::string& netloc,
                    auto_pid<process_state::RUNNING> child,
                    auto_fd to_child, auto_fd from_child,
                    auto_fd err_from_child);

        void open_remote_path(const std::string& path, logfile_open_options loo);

        void load_preview(int64_t id, const std::string& path);

        void complete_path(const std::string& path);

        bool is_synced() const {
            return this->ht_state.is<synced>();
        }

    protected:
        void *run() override;

        void loop_body() override;

        void stopped() override;

        std::chrono::milliseconds
        compute_timeout(mstime_t current_time) const override;

    private:
        static ghc::filesystem::path tmp_path();

        std::string get_display_path(const std::string& remote_path) const;

        struct connected {
            auto_pid<process_state::RUNNING> ht_child;
            auto_fd ht_to_child;
            auto_fd ht_from_child;
            std::map<std::string, logfile_open_options> c_desired_paths;
            std::map<std::string, logfile_open_options> c_child_paths;

            auto_pid<process_state::FINISHED> close() &&;
        };

        struct disconnected {};
        struct synced {};

        using state_v = mapbox::util::variant<connected, disconnected, synced>;

        const std::string ht_netloc;
        std::string ht_uname;
        const ghc::filesystem::path ht_local_path;
        std::set<ghc::filesystem::path> ht_active_files;
        std::vector<std::string> ht_error_queue;
        std::thread ht_error_reader;
        state_v ht_state{disconnected()};
    };

    static void report_error(std::string path, std::string msg);

    using attempt_time_point = std::chrono::time_point<std::chrono::steady_clock>;

    struct remote_path_queue {
        attempt_time_point rpq_next_attempt_time{std::chrono::steady_clock::now()};
        std::map<std::string, logfile_open_options> rpq_new_paths;
        std::map<std::string, logfile_open_options> rpq_existing_paths;

        void send_synced_to_main(const std::string& netloc);
    };

    std::map<std::string, remote_path_queue> l_netlocs_to_paths;
    std::map<std::string, std::shared_ptr<host_tailer>> l_remotes;
};

}

#endif
