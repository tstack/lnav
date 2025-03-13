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

#ifndef piper_looper_hh
#define piper_looper_hh

#include <filesystem>
#include <future>
#include <memory>
#include <string>

#include "base/auto_fd.hh"
#include "base/lnav.console.hh"
#include "base/piper.file.hh"
#include "base/result.h"
#include "safe/safe.h"

namespace lnav::piper {

enum class state {
    running,
    finished,
};

struct demux_info {
    std::string di_name;
    std::vector<lnav::console::user_message> di_details;
};

using safe_demux_info = safe::Safe<demux_info>;

struct options {
    bool o_demux{false};
    bool o_follow{true};

    options& with_demux(bool v)
    {
        this->o_demux = v;
        return *this;
    }

    options& with_follow(bool v)
    {
        this->o_follow = v;
        return *this;
    }
};

class looper {
public:
    looper(std::string name,
           auto_fd stdout_fd,
           auto_fd stderr_fd,
           options opts);

    ~looper();

    std::string get_name() const { return this->l_name; }

    std::filesystem::path get_out_dir() const { return this->l_out_dir; }

    std::filesystem::path get_out_pattern() const
    {
        return this->l_out_dir / "out.*";
    }

    demux_info get_demux_info() const
    {
        return *this->l_demux_info.readAccess();
    }

    std::string get_url() const
    {
        return fmt::format(FMT_STRING("piper://{}"),
                           this->l_out_dir.filename().string());
    }

    int get_loop_count() const { return this->l_loop_count.load(); }

    bool is_finished() const
    {
        return this->l_future.wait_for(std::chrono::seconds(0))
            == std::future_status::ready;
    }

    size_t consume_finished()
    {
        if (!this->is_finished()) {
            return 0;
        }

        if (this->l_finished.fetch_or(1) == 0) {
            return 1;
        }
        return 0;
    }

private:
    void loop();

    static auto_pipe& get_wakeup_pipe();

    std::atomic<bool> l_looping{true};
    const std::string l_name;
    const std::string l_cwd;
    const std::map<std::string, std::string> l_env;
    std::filesystem::path l_out_dir;
    auto_fd l_stdout;
    auto_fd l_stderr;
    options l_options;
    std::future<void> l_future;
    std::atomic<int> l_finished{0};
    std::atomic<int> l_loop_count{0};
    safe_demux_info l_demux_info;
};

template<state LooperState>
class handle {
public:
    explicit handle(std::shared_ptr<looper> looper)
        : h_looper(std::move(looper))
    {
    }

    std::string get_name() const { return this->h_looper->get_name(); }

    std::filesystem::path get_out_dir() const
    {
        return this->h_looper->get_out_dir();
    }

    std::filesystem::path get_out_pattern() const
    {
        return this->h_looper->get_out_pattern();
    }

    std::string get_demux_id() const
    {
        return this->h_looper->get_demux_info().di_name;
    }

    std::vector<lnav::console::user_message> get_demux_details() const
    {
        return this->h_looper->get_demux_info().di_details;
    }

    std::string get_url() const { return this->h_looper->get_url(); }

    int get_loop_count() const { return this->h_looper->get_loop_count(); }

    bool is_finished() const { return this->h_looper->is_finished(); }

    size_t consume_finished() { return this->h_looper->consume_finished(); }

    bool operator==(const handle& other) const
    {
        return this->h_looper.get() == other.h_looper.get();
    }

private:
    std::shared_ptr<looper> h_looper;
};

using running_handle = handle<state::running>;

Result<handle<state::running>, std::string> create_looper(std::string name,
                                                          auto_fd stdout_fd,
                                                          auto_fd stderr_fd,
                                                          options opts = {});

void cleanup();

}  // namespace lnav::piper

#endif
