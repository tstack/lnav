/**
 * Copyright (c) 2025, Timothy Stack
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

#ifndef lnav_progress_hh
#define lnav_progress_hh

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "distributed_slice.hh"
#include "lnav.console.hh"
#include "safe/safe.h"

namespace lnav {

enum class progress_result_t : uint8_t {
    ok,
    interrupt,
};

enum class progress_status_t : uint8_t {
    idle,
    working,
};

struct task_progress {
    std::string tp_id;
    progress_status_t tp_status{progress_status_t::idle};
    size_t tp_version{0};
    std::string tp_step;
    size_t tp_completed{0};
    size_t tp_total{0};
    std::vector<console::user_message> tp_messages;
};

using progress_reporter_t = task_progress (*)();

struct progress_tracker {
    using task_container = dist_slice_container<progress_reporter_t>;

    using safe_task_container = safe::Safe<task_container*>;

    static progress_tracker& instance();

    static safe_task_container& get_tasks();

    void wait_for_completion();

    void notify_completion();

    void abort();

private:
    safe_task_container pt_tasks;
    std::mutex pt_mutex;
    std::condition_variable pt_cv;
    uint64_t pt_version{0};
    bool pt_abort{false};

    progress_tracker();
};

}  // namespace lnav

#endif
