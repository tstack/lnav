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
 *
 * @file parallel_for.cc
 */

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "parallel_for.hh"

#include "config.h"
#include "lnav_log.hh"

namespace lnav {

size_t
default_worker_count(size_t count)
{
    static constexpr size_t MAX_WORKERS = 8;

    auto hw = static_cast<size_t>(std::thread::hardware_concurrency());
    if (hw == 0) {
        // The standard allows 0 for "cannot tell".
        hw = 4;
    }

    return std::min({hw, count, MAX_WORKERS});
}

void
parallel_for_each(size_t count,
                  size_t width,
                  const std::function<void(size_t)>& body,
                  const std::function<void()>& tick,
                  std::chrono::milliseconds tick_interval)
{
    if (count == 0) {
        return;
    }

    // Only worth skipping the threads when nobody is waiting on them.  A
    // caller that passes a tick wants to keep working while the body runs,
    // which it cannot do if the body is running on its own thread.
    if (!tick && (width <= 1 || count == 1)) {
        for (size_t index = 0; index < count; index++) {
            body(index);
        }
        return;
    }

    // At least one: count is non-zero here, and the loop below waits on
    // `finished` reaching it, which no amount of ticking can do when the
    // width left no worker to run the body.
    width = std::clamp(width, size_t{1}, count);

    std::atomic<size_t> cursor{0};
    std::atomic<size_t> finished{0};
    std::mutex done_mutex;
    std::condition_variable done_cond;

    auto worker = [&]() {
        for (;;) {
            const auto index = cursor.fetch_add(1, std::memory_order_relaxed);
            if (index >= count) {
                break;
            }

            try {
                body(index);
            } catch (const std::exception& e) {
                log_error(
                    "parallel_for_each: item %zu threw -- %s", index, e.what());
            } catch (...) {
                log_error("parallel_for_each: item %zu threw", index);
            }

            // Release so the caller, which acquires below, sees everything
            // this worker wrote.
            if (finished.fetch_add(1, std::memory_order_release) + 1 == count) {
                // Taken so the notify cannot land between the caller's
                // predicate check and its wait.
                std::lock_guard<std::mutex> lk(done_mutex);
                done_cond.notify_all();
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(width);
    for (size_t lpc = 0; lpc < width; lpc++) {
        workers.emplace_back(worker);
    }

    {
        std::unique_lock<std::mutex> lk(done_mutex);

        while (finished.load(std::memory_order_acquire) < count) {
            if (!tick) {
                done_cond.wait(lk);
                continue;
            }

            done_cond.wait_for(lk, tick_interval);
            if (finished.load(std::memory_order_acquire) >= count) {
                // Woken by the last worker rather than by the timeout.  There
                // is no progress left to report, and ticking here would leave
                // the UI showing a pass that is already over.
                break;
            }
            // Dropped so a worker finishing mid-tick is not held up by it.
            lk.unlock();
            try {
                tick();
            } catch (const std::exception& e) {
                log_error("parallel_for_each: tick threw -- %s", e.what());
            } catch (...) {
                log_error("parallel_for_each: tick threw");
            }
            lk.lock();
        }
    }

    for (auto& th : workers) {
        th.join();
    }
}

}  // namespace lnav
