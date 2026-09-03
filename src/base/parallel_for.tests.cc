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
#include <numeric>
#include <thread>
#include <vector>

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
