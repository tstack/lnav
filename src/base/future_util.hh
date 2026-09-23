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
 */

#ifndef lnav_future_util_hh
#define lnav_future_util_hh

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

#include "progress.hh"

namespace lnav::futures {

/**
 * Create a future that is ready to immediately return a result.
 *
 * @tparam T The result type of the future.
 * @param t The value the future should return.
 * @return The new future.
 */
template<class T>
std::future<std::decay_t<T>>
make_ready_future(T&& t)
{
    std::promise<std::decay_t<T>> pr;
    auto r = pr.get_future();
    pr.set_value(std::forward<T>(t));
    return r;
}

/**
 * A queue used to limit the number of futures that are running concurrently.
 * Work handed to submit() runs on at most max_queue_size threads that belong
 * to the queue and are joined when it is destroyed, rather than on a new
 * thread per item.  push_back() and pop_to() must only be called from the
 * thread that owns the queue.
 *
 * @tparam T The result of the futures.
 */
template<typename T>
class future_queue {
public:
    /**
     * @param processor The function to execute with the result of a future.
     * @param max_queue_size The maximum number of futures that can be in
     * flight, a value of zero is treated as one.
     */
    explicit future_queue(
        std::function<lnav::progress_result_t(std::future<T>&)> processor,
        size_t max_queue_size = 8)
        : fq_processor(std::move(processor)),
          fq_max_queue_size(std::max<size_t>(1, max_queue_size))
    {
    }

    future_queue(const future_queue&) = delete;
    future_queue& operator=(const future_queue&) = delete;

    ~future_queue()
    {
        // Every future has to be waited on before the workers are joined, so
        // an exception from the processor cannot be allowed to stop the drain.
        while (!this->fq_deque.empty()) {
            try {
                this->pop_to();
            } catch (...) {
            }
        }

        {
            std::lock_guard<std::mutex> lk(this->fq_task_mutex);
            this->fq_stopping = true;
        }
        this->fq_task_cond.notify_all();
        for (auto& th : this->fq_workers) {
            th.join();
        }
    }

    /**
     * Run a function on one of the queue's threads.  The caller is expected
     * to push_back() the returned future, which is what bounds how much work
     * is outstanding.
     *
     * @param func The function to run, it may be move-only.
     * @return The future for the function's result.
     */
    template<typename F>
    std::future<T> submit(F&& func)
    {
        std::packaged_task<T()> task(std::forward<F>(func));
        auto retval = task.get_future();

        {
            std::unique_lock<std::mutex> lk(this->fq_task_mutex);
            this->fq_tasks.emplace_back(std::move(task));
            if (this->fq_workers.size() < this->fq_max_queue_size
                && this->fq_tasks.size() > this->fq_idle_workers)
            {
                try {
                    this->fq_workers.emplace_back(&future_queue::run_tasks,
                                                  this);
                } catch (const std::system_error&) {
                    // Without any worker to pick up the task, run it here
                    // so the returned future still gets a result.
                    if (this->fq_workers.empty()) {
                        auto inline_task = std::move(this->fq_tasks.back());
                        this->fq_tasks.pop_back();
                        lk.unlock();
                        inline_task();
                        return retval;
                    }
                }
            }
        }
        this->fq_task_cond.notify_one();

        return retval;
    }

    /**
     * Add a future to the queue.  If the size of the queue is greater than the
     * max_queue_size, this call will block waiting for the first queued
     * future to return a result.
     *
     * @param f The future to add to the queue.
     */
    lnav::progress_result_t push_back(std::future<T>&& f)
    {
        this->fq_deque.emplace_back(std::move(f));
        return this->pop_to(this->fq_max_queue_size);
    }

    /**
     * Removes the next future from the queue, waits for the result, and then
     * repeats until the queue reaches the given size.
     *
     * @param size The new desired size of the queue.
     */
    lnav::progress_result_t pop_to(size_t size = 0)
    {
        lnav::progress_result_t retval = lnav::progress_result_t::ok;

        while (this->fq_deque.size() > size) {
            auto fut = std::move(this->fq_deque.front());

            this->fq_deque.pop_front();
            if (this->fq_processor(fut) == lnav::progress_result_t::interrupt) {
                retval = lnav::progress_result_t::interrupt;
            }
        }
        return retval;
    }

private:
    void run_tasks()
    {
        std::unique_lock<std::mutex> lk(this->fq_task_mutex);

        for (;;) {
            this->fq_idle_workers += 1;
            this->fq_task_cond.wait(lk, [this]() {
                return this->fq_stopping || !this->fq_tasks.empty();
            });
            this->fq_idle_workers -= 1;
            if (this->fq_tasks.empty()) {
                return;
            }

            auto task = std::move(this->fq_tasks.front());
            this->fq_tasks.pop_front();
            lk.unlock();
            task();
            lk.lock();
        }
    }

    std::function<lnav::progress_result_t(std::future<T>&)> fq_processor;
    std::deque<std::future<T>> fq_deque;
    const size_t fq_max_queue_size;

    std::mutex fq_task_mutex;
    std::condition_variable fq_task_cond;
    std::deque<std::packaged_task<T()>> fq_tasks;
    std::vector<std::thread> fq_workers;
    size_t fq_idle_workers{0};
    bool fq_stopping{false};
};

}  // namespace lnav::futures

#endif
