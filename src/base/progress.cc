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

#include "progress.hh"

namespace lnav {

progress_tracker&
progress_tracker::instance()
{
    static auto pt = progress_tracker();

    return pt;
}

progress_tracker::safe_task_container&
progress_tracker::get_tasks()
{
    return instance().pt_tasks;
}

void
progress_tracker::wait_for_completion()
{
    std::unique_lock<std::mutex> lock(this->pt_mutex);

    auto active_tasks = false;
    {
        for (const auto& task : **this->pt_tasks.readAccess()) {
            auto tp = task();
            if (tp.tp_status == progress_status_t::working) {
                active_tasks = true;
                break;
            }
        }
    }
    if (!active_tasks) {
        return;
    }

    auto init_version = this->pt_version;
    this->pt_cv.wait(lock, [&] {
        return this->pt_abort || init_version != this->pt_version;
    });
}

void
progress_tracker::notify_completion()
{
    std::lock_guard<std::mutex> lock(this->pt_mutex);

    this->pt_version += 1;
    this->pt_cv.notify_all();
}

void
progress_tracker::abort()
{
    std::lock_guard lg{this->pt_mutex};

    this->pt_abort = true;
    this->pt_cv.notify_all();
}

progress_tracker::progress_tracker()
{
    static auto inner = DIST_SLICE_CONTAINER(progress_reporter_t, prog_reps);

    *this->pt_tasks.writeAccess() = &inner;
}

}  // namespace lnav
