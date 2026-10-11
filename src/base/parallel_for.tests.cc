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
 * @file parallel_for.tests.cc
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <numeric>
#include <ostream>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include "parallel_for.hh"

#include "doctest/doctest.h"

TEST_CASE("parallel_for_each: every item runs exactly once")
{
    constexpr size_t COUNT = 500;
    std::vector<std::atomic<int>> hits(COUNT);
    for (auto& h : hits) {
        h.store(0);
    }

    lnav::parallel_for_each(
        COUNT, 4, [&hits](size_t index) { hits[index].fetch_add(1); });

    for (size_t lpc = 0; lpc < COUNT; lpc++) {
        CHECK(1 == hits[lpc].load());
    }
}

TEST_CASE("parallel_for_each: results do not race")
{
    constexpr size_t COUNT = 1000;
    std::vector<size_t> out(COUNT, 0);

    // Each item writes only its own slot, which is the contract callers are
    // expected to honor.
    lnav::parallel_for_each(
        COUNT, 8, [&out](size_t index) { out[index] = index * 2; });

    for (size_t lpc = 0; lpc < COUNT; lpc++) {
        CHECK(lpc * 2 == out[lpc]);
    }
}

TEST_CASE("parallel_for_each: zero and one width run inline")
{
    for (auto width : {(size_t) 0, (size_t) 1}) {
        std::vector<size_t> order;
        auto caller = std::this_thread::get_id();
        auto same_thread = true;

        lnav::parallel_for_each(5, width, [&](size_t index) {
            if (std::this_thread::get_id() != caller) {
                same_thread = false;
            }
            order.push_back(index);
        });

        CHECK(same_thread);
        CHECK(std::vector<size_t>{0, 1, 2, 3, 4} == order);
    }
}

TEST_CASE("parallel_for_each: a tick means a worker, even for one item")
{
    auto caller = std::this_thread::get_id();
    std::thread::id body_thread;
    std::atomic<int> ticks{0};

    lnav::parallel_for_each(
        1,
        1,
        [&body_thread](size_t) {
            body_thread = std::this_thread::get_id();
            std::this_thread::sleep_for(std::chrono::milliseconds{60});
        },
        [&ticks]() { ticks.fetch_add(1); },
        std::chrono::milliseconds{5});

    // The caller has to be free to tick, so the body cannot be on its thread.
    CHECK(body_thread != caller);
    CHECK(ticks.load() > 0);
}

TEST_CASE("parallel_for_each: an empty range does nothing")
{
    auto ran = false;

    lnav::parallel_for_each(0, 4, [&ran](size_t) { ran = true; });

    CHECK_FALSE(ran);
}

TEST_CASE("parallel_for_each: the caller ticks while work is outstanding")
{
    std::atomic<int> ticks{0};
    std::atomic<int> done{0};

    lnav::parallel_for_each(
        4,
        4,
        [&done](size_t) {
            std::this_thread::sleep_for(std::chrono::milliseconds{60});
            done.fetch_add(1);
        },
        [&ticks]() { ticks.fetch_add(1); },
        std::chrono::milliseconds{5});

    CHECK(4 == done.load());
    // 60ms of work against a 5ms tick, so this is not a tight bound.
    CHECK(ticks.load() > 0);
}

TEST_CASE("parallel_for_each: a throwing item does not strand the rest")
{
    constexpr size_t COUNT = 40;
    std::atomic<int> ran{0};

    lnav::parallel_for_each(COUNT, 4, [&ran](size_t index) {
        if (index % 7 == 0) {
            throw std::runtime_error("nope");
        }
        ran.fetch_add(1);
    });

    // 0, 7, 14, 21, 28, 35 threw; the other 34 still ran, and the call
    // returned rather than waiting forever on the ones that did not finish.
    CHECK(34 == ran.load());
}

TEST_CASE("default_worker_count is bounded by the item count")
{
    CHECK(1 == lnav::default_worker_count(1));
    CHECK(2 == lnav::default_worker_count(2));
    CHECK(lnav::default_worker_count(1000) <= 8);
    CHECK(lnav::default_worker_count(1000) >= 1);
}

TEST_CASE("parallel_for_each: a tick means a worker, even at zero width")
{
    // The inline path is only for callers with nothing else to do, so a
    // width of 0 has to become a worker here rather than no worker at all --
    // the wait below would otherwise tick forever against a body that never
    // runs.  A regression shows up as this case hanging, not as a failed
    // assertion.
    auto caller = std::this_thread::get_id();
    std::thread::id body_thread;
    std::atomic<int> ticks{0};

    lnav::parallel_for_each(
        3,
        0,
        [&body_thread](size_t) {
            body_thread = std::this_thread::get_id();
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
        },
        [&ticks]() { ticks.fetch_add(1); },
        std::chrono::milliseconds{5});

    CHECK(body_thread != caller);
    CHECK(ticks.load() > 0);
}

TEST_CASE("parallel_for_each: workers are reused from call to call")
{
    auto caller = std::this_thread::get_id();
    std::mutex ids_mutex;
    std::set<std::thread::id> ids;

    for (int call = 0; call < 50; call++) {
        lnav::parallel_for_each(16, 4, [&](size_t) {
            std::lock_guard<std::mutex> lg(ids_mutex);
            ids.insert(std::this_thread::get_id());
        });
    }

    CHECK(ids.count(caller) == 0);
    // Other cases may have grown the pool, but never past the widest call (8),
    // and starting threads per call would give up to 200 here.
    CHECK(ids.size() <= 8);
}

TEST_CASE("parallel_for_each: a call from a body completes")
{
    std::atomic<int> inner{0};

    lnav::parallel_for_each(4, 2, [&inner](size_t) {
        lnav::parallel_for_each(
            4, 2, [&inner](size_t) { inner.fetch_add(1); });
    });

    CHECK(16 == inner.load());
}

TEST_CASE("parallel_for_each: a call from a tick completes")
{
    std::atomic<int> outer{0};
    std::atomic<int> inner{0};

    lnav::parallel_for_each(
        2,
        2,
        [&outer](size_t) {
            std::this_thread::sleep_for(std::chrono::milliseconds{30});
            outer.fetch_add(1);
        },
        [&inner]() {
            lnav::parallel_for_each(
                3, 2, [&inner](size_t) { inner.fetch_add(1); });
        },
        std::chrono::milliseconds{5});

    CHECK(2 == outer.load());
    CHECK(inner.load() > 0);
    CHECK(inner.load() % 3 == 0);
}

TEST_CASE("parallel_for_each: two threads can call it at once")
{
    constexpr size_t COUNT = 100;
    std::vector<std::atomic<int>> hits_a(COUNT);
    std::vector<std::atomic<int>> hits_b(COUNT);
    for (size_t lpc = 0; lpc < COUNT; lpc++) {
        hits_a[lpc].store(0);
        hits_b[lpc].store(0);
    }

    auto fan_out = [](std::vector<std::atomic<int>>& hits) {
        for (int round = 0; round < 20; round++) {
            lnav::parallel_for_each(
                hits.size(), 4, [&hits](size_t index) { hits[index].fetch_add(1); });
        }
    };
    std::thread a(fan_out, std::ref(hits_a));
    std::thread b(fan_out, std::ref(hits_b));
    a.join();
    b.join();

    for (size_t lpc = 0; lpc < COUNT; lpc++) {
        CHECK(20 == hits_a[lpc].load());
        CHECK(20 == hits_b[lpc].load());
    }
}

TEST_CASE("parallel_for_each: idle workers exit, and the next call starts new ones")
{
    // Put back the default even if a check below fails, so later cases don't
    // run with workers that leave after 50ms.
    struct restore_timeout {
        ~restore_timeout()
        {
            lnav::set_parallel_worker_idle_timeout(std::chrono::seconds{10});
        }
    } restore;
    lnav::set_parallel_worker_idle_timeout(std::chrono::milliseconds{50});

    auto wait_for_no_workers = []() {
        // A worker already waiting under the 10s default leaves on that
        // timeout, not this one, so allow for it.
        auto deadline
            = std::chrono::steady_clock::now() + std::chrono::seconds{15};
        while (lnav::parallel_worker_count() > 0
               && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
        return lnav::parallel_worker_count();
    };

    // Counted from inside the bodies, once all four are running: a worker
    // can't leave while it's running a body, but after the call returns they
    // may all have timed out before the count is read.
    auto fan_out = []() {
        std::atomic<size_t> arrived{0};
        std::atomic<size_t> fewest_live{SIZE_MAX};
        lnav::parallel_for_each(4, 4, [&](size_t) {
            arrived.fetch_add(1);
            while (arrived.load() < 4) {
                std::this_thread::yield();
            }
            auto live = lnav::parallel_worker_count();
            auto prev = fewest_live.load();
            while (live < prev
                   && !fewest_live.compare_exchange_weak(prev, live))
            {
            }
        });
        return fewest_live.load();
    };

    CHECK(fan_out() >= 4);
    CHECK(0 == wait_for_no_workers());

    CHECK(fan_out() >= 4);
    CHECK(0 == wait_for_no_workers());
}

#if defined(__has_feature)
#    if __has_feature(thread_sanitizer)
#        define PARALLEL_FOR_TSAN 1
#    endif
#endif
#if defined(__SANITIZE_THREAD__)
#    define PARALLEL_FOR_TSAN 1
#endif

// ThreadSanitizer doesn't let the child of a multithreaded process start
// threads (die_after_fork).
#ifndef PARALLEL_FOR_TSAN
TEST_CASE("parallel_for_each: a forked child can fan out")
{
    // Start the pool in the parent first; its workers don't exist in the
    // child, so the child has to get a pool of its own.
    lnav::parallel_for_each(4, 4, [](size_t) {});

    auto pid = fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        std::atomic<int> ran{0};
        lnav::parallel_for_each(8, 4, [&ran](size_t) { ran.fetch_add(1); });
        _exit(ran.load() == 8 ? 0 : 1);
    }

    int status = 0;
    REQUIRE(waitpid(pid, &status, 0) == pid);
    CHECK(WIFEXITED(status));
    CHECK(0 == WEXITSTATUS(status));
}
#endif
