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

#include <pthread.h>

#include "config.h"
#include "lnav_log.hh"

namespace lnav {

namespace {

constexpr const char* WORKER_THREAD_NAME = "lnav-worker";

/** Names the calling thread, so profiles can group the workers. */
void
name_this_thread()
{
#if defined(__APPLE__)
    pthread_setname_np(WORKER_THREAD_NAME);
#elif defined(__linux__)
    pthread_setname_np(pthread_self(), WORKER_THREAD_NAME);
#endif
}

/** Runs body(index), logging instead of propagating an exception. */
void
run_item(const std::function<void(size_t)>& body, size_t index)
{
    try {
        body(index);
    } catch (const std::exception& e) {
        log_error("parallel_for_each: item %zu threw -- %s", index, e.what());
    } catch (...) {
        log_error("parallel_for_each: item %zu threw", index);
    }
}

/** Calls tick(), logging instead of propagating an exception. */
void
run_tick(const std::function<void()>& tick)
{
    try {
        tick();
    } catch (const std::exception& e) {
        log_error("parallel_for_each: tick threw -- %s", e.what());
    } catch (...) {
        log_error("parallel_for_each: tick threw");
    }
}

/**
 * The fan-out with threads of its own, which exist only for the duration of
 * the call.  Used when the shared pool is busy: a nested call from a body or a
 * tick, or a call from a second thread.
 */
void
run_with_fresh_threads(size_t count,
                       size_t width,
                       const std::function<void(size_t)>& body,
                       const std::function<void()>& tick,
                       std::chrono::milliseconds tick_interval)
{
    std::atomic<size_t> cursor{0};
    std::atomic<size_t> finished{0};
    std::mutex done_mutex;
    std::condition_variable done_cond;

    auto worker = [&]() {
        name_this_thread();
        for (;;) {
            const auto index = cursor.fetch_add(1, std::memory_order_relaxed);
            if (index >= count) {
                break;
            }

            run_item(body, index);

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
            run_tick(tick);
            lk.lock();
        }
    }

    for (auto& th : workers) {
        th.join();
    }
}

/** How long a pool worker waits for work before it exits, in ms. */
std::atomic<std::chrono::milliseconds::rep> IDLE_TIMEOUT_MS{10'000};

/** One call's items, handed to the pool's workers. */
struct batch {
    size_t b_count{0};
    size_t b_width{0};
    const std::function<void(size_t)>* b_body{nullptr};
    std::atomic<size_t> b_cursor{0};
    /** Workers that joined, and those that have left; under wp_mutex. */
    size_t b_claimed{0};
    size_t b_exited{0};

    /**
     * Every item has been handed out and every worker that took one is back,
     * so they have all finished and none will touch this batch again.
     */
    bool done() const
    {
        return this->b_cursor.load(std::memory_order_relaxed) >= this->b_count
            && this->b_exited == this->b_claimed;
    }
};

/**
 * Workers that outlive a call, blocked on wp_work_cond between calls, until
 * they've waited IDLE_TIMEOUT_MS with nothing to do.  Never destroyed: a
 * worker may still be leaving, and at exit the rest are still waiting.
 */
struct worker_pool {
    /** Held by the caller whose batch the pool is running. */
    std::mutex wp_call_mutex;

    std::mutex wp_mutex;
    std::condition_variable wp_work_cond;
    std::condition_variable wp_done_cond;
    /** The running batch, or nullptr between calls; under wp_mutex. */
    batch* wp_batch{nullptr};
    /** Bumped for each batch, so a worker joins it at most once. */
    uint64_t wp_generation{0};

    /**
     * The workers that haven't exited; under wp_mutex.  They're detached, since
     * one that leaves can't join itself.
     */
    size_t wp_live{0};

    void worker_loop(uint64_t seen)
    {
        name_this_thread();
        std::unique_lock<std::mutex> lk(this->wp_mutex);
        for (;;) {
            const auto idle = std::chrono::milliseconds{
                IDLE_TIMEOUT_MS.load(std::memory_order_relaxed)};
            if (!this->wp_work_cond.wait_for(
                    lk, idle, [&] { return this->wp_generation != seen; }))
            {
                // Nothing to do for a while, which is usual once lnav has
                // finished indexing.  The next call starts a new worker.  The
                // caller grows the pool and publishes a batch under this same
                // lock, so it never counts on a worker that has left.
                this->wp_live -= 1;
                return;
            }
            seen = this->wp_generation;

            auto* b = this->wp_batch;
            if (b == nullptr || b->b_claimed >= b->b_width) {
                // Over or full: this one isn't needed.
                continue;
            }
            b->b_claimed += 1;
            lk.unlock();

            for (;;) {
                const auto index
                    = b->b_cursor.fetch_add(1, std::memory_order_relaxed);
                if (index >= b->b_count) {
                    break;
                }
                run_item(*b->b_body, index);
            }

            // Leaving under the lock is what lets the caller, which checks
            // done() under it, see everything this worker wrote.
            lk.lock();
            b->b_exited += 1;
            if (b->done()) {
                this->wp_done_cond.notify_all();
            }
        }
    }

    void run(size_t count,
             size_t width,
             const std::function<void(size_t)>& body,
             const std::function<void()>& tick,
             std::chrono::milliseconds tick_interval)
    {
        batch b;
        b.b_count = count;
        b.b_width = width;
        b.b_body = &body;

        std::unique_lock<std::mutex> lk(this->wp_mutex);
        while (this->wp_live < width) {
            // Started with the current generation, so it waits for the batch
            // published below instead of joining a finished one.
            std::thread(&worker_pool::worker_loop, this, this->wp_generation)
                .detach();
            this->wp_live += 1;
        }
        this->wp_batch = &b;
        this->wp_generation += 1;
        this->wp_work_cond.notify_all();

        while (!b.done()) {
            if (!tick) {
                this->wp_done_cond.wait(lk);
                continue;
            }

            this->wp_done_cond.wait_for(lk, tick_interval);
            if (b.done()) {
                // As in run_with_fresh_threads(): no tick for a pass that's
                // already over.
                break;
            }
            // Dropped so a worker finishing mid-tick is not held up by it.
            lk.unlock();
            run_tick(tick);
            lk.lock();
        }

        // Cleared in the same critical section as the last done() check, so a
        // worker that wakes late finds nothing to join and never touches the
        // batch, which goes away when this returns.
        this->wp_batch = nullptr;
    }
};

std::atomic<worker_pool*> POOL{nullptr};

/** Whether this thread is running a pool batch: it holds wp_call_mutex. */
thread_local bool IN_POOL_CALL = false;

void
forget_pool_in_child()
{
    // The parent's workers don't exist in the child, and the pool's mutexes
    // are in whatever state the fork caught them, so leave it behind.  A
    // child that fans out gets a pool of its own.
    POOL.store(nullptr, std::memory_order_relaxed);
}

worker_pool*
get_pool()
{
    auto* retval = POOL.load(std::memory_order_acquire);
    if (retval != nullptr) {
        return retval;
    }

    // Registered once per process; a forked child inherits it.
    static const auto REGISTERED = []() {
        pthread_atfork(nullptr, nullptr, forget_pool_in_child);
        return true;
    }();
    (void) REGISTERED;

    auto* fresh = new worker_pool();
    if (POOL.compare_exchange_strong(retval, fresh, std::memory_order_acq_rel))
    {
        return fresh;
    }
    // Another thread got there first; this one has no threads yet.
    delete fresh;
    return retval;
}

}  // namespace

void
set_parallel_worker_idle_timeout(std::chrono::milliseconds timeout)
{
    IDLE_TIMEOUT_MS.store(timeout.count(), std::memory_order_relaxed);
}

size_t
parallel_worker_count()
{
    auto* pool = POOL.load(std::memory_order_acquire);
    if (pool == nullptr) {
        return 0;
    }

    std::lock_guard<std::mutex> lg(pool->wp_mutex);
    return pool->wp_live;
}

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

    // At least one: count is non-zero here, and the wait for the items to
    // finish can't end when the width left no worker to run the body.
    width = std::clamp(width, size_t{1}, count);

    // A tick that fans out runs on this thread, which already holds the call
    // mutex, and trying to lock a mutex this thread owns is undefined.
    if (!IN_POOL_CALL) {
        auto* pool = get_pool();
        std::unique_lock<std::mutex> call(pool->wp_call_mutex,
                                          std::try_to_lock);
        if (call.owns_lock()) {
            IN_POOL_CALL = true;
            try {
                pool->run(count, width, body, tick, tick_interval);
            } catch (...) {
                // Starting a worker can throw; nothing was handed out then.
                IN_POOL_CALL = false;
                throw;
            }
            IN_POOL_CALL = false;
            return;
        }
    }

    run_with_fresh_threads(count, width, body, tick, tick_interval);
}

}  // namespace lnav
