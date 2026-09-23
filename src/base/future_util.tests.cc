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
 */

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

#include "future_util.hh"

#include "doctest/doctest.h"

using lnav::futures::future_queue;

TEST_CASE("future_queue-submit")
{
    std::vector<int> results;
    std::mutex threads_mutex;
    std::set<std::thread::id> threads;
    std::atomic<int> running{0};
    std::atomic<int> max_running{0};

    {
        future_queue<int> fq(
            [&results](std::future<int>& fut) {
                results.push_back(fut.get());
                return lnav::progress_result_t::ok;
            },
            3);

        for (int lpc = 0; lpc < 50; lpc++) {
            // A move-only capture, like the file-open closures carry.
            auto value = std::make_unique<int>(lpc);
            fq.push_back(fq.submit([&, value = std::move(value)]() {
                const auto now = running.fetch_add(1) + 1;
                auto prev = max_running.load();
                while (now > prev && !max_running.compare_exchange_weak(prev, now))
                {
                }
                {
                    std::lock_guard<std::mutex> lk(threads_mutex);
                    threads.insert(std::this_thread::get_id());
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                running.fetch_sub(1);
                return *value;
            }));
        }
    }

    // Results come back in submission order, from no more threads than the
    // queue allows to be in flight.
    REQUIRE(results.size() == 50);
    for (int lpc = 0; lpc < 50; lpc++) {
        CHECK(results[lpc] == lpc);
    }
    CHECK(threads.size() <= 3);
    CHECK(max_running.load() <= 3);
    CHECK(threads.count(std::this_thread::get_id()) == 0);
}

TEST_CASE("future_queue-submit-exception")
{
    auto caught = false;

    {
        future_queue<int> fq([&caught](std::future<int>& fut) {
            try {
                fut.get();
            } catch (const std::runtime_error&) {
                caught = true;
            }
            return lnav::progress_result_t::ok;
        });

        fq.push_back(
            fq.submit([]() -> int { throw std::runtime_error("boom"); }));
    }

    CHECK(caught);
}

TEST_CASE("future_queue-no-submit")
{
    // A queue that only ever sees ready futures starts no threads, and
    // destroying it must not wait on any.
    std::vector<int> results;
    {
        future_queue<int> fq([&results](std::future<int>& fut) {
            results.push_back(fut.get());
            return lnav::progress_result_t::ok;
        });

        fq.push_back(lnav::futures::make_ready_future(1));
    }
    CHECK(results == std::vector<int>{1});
}

TEST_CASE("future_queue-processor-throws")
{
    // A processor that throws must not leave its future in the queue to be
    // processed a second time, and the destructor still has to wait for the
    // rest.
    std::vector<int> results;
    auto thrown = false;

    {
        future_queue<int> fq(
            [&results](std::future<int>& fut) {
                auto v = fut.get();
                if (v == 1) {
                    throw std::runtime_error("processor failed");
                }
                results.push_back(v);
                return lnav::progress_result_t::ok;
            },
            1);

        fq.push_back(fq.submit([]() { return 0; }));
        fq.push_back(fq.submit([]() { return 1; }));
        try {
            fq.push_back(fq.submit([]() { return 2; }));
        } catch (const std::runtime_error&) {
            thrown = true;
        }
        fq.push_back(fq.submit([]() { return 3; }));
    }

    CHECK(thrown);
    CHECK(results == std::vector<int>{0, 2, 3});
}

TEST_CASE("future_queue-zero-size")
{
    std::vector<int> results;
    {
        future_queue<int> fq(
            [&results](std::future<int>& fut) {
                results.push_back(fut.get());
                return lnav::progress_result_t::ok;
            },
            0);

        fq.push_back(fq.submit([]() { return 1; }));
        fq.push_back(fq.submit([]() { return 2; }));
    }
    CHECK(results == std::vector<int>{1, 2});
}
