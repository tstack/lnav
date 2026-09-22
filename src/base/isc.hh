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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file isc.hh
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

#include <unistd.h>

#include "guard_util.hh"
#include "injector.hh"
#include "safe/safe.h"
#include "time_util.hh"

#ifndef lnav_isc_hh
#    define lnav_isc_hh

namespace isc {

struct msg {
    std::function<void()> m_callback;
};

class msg_port {
public:
    msg_port() = default;

    void send(msg&& m) { this->mp_messages.emplace_back(std::move(m)); }

    std::deque<msg> mp_messages;
};

class service_base;
using service_list = std::vector<std::shared_ptr<service_base>>;

struct supervisor {
    explicit supervisor(service_list servs = {},
                        service_base* parent = nullptr);

    ~supervisor();

    bool empty() const { return this->s_service_list.empty(); }

    void add_child_service(std::shared_ptr<service_base> new_service);

    void stop_child(std::shared_ptr<service_base> child);

    void stop_children();

    void cleanup_children();

protected:
    service_list s_service_list;
    service_base* s_parent;
};

class service_base : public std::enable_shared_from_this<service_base> {
public:
    explicit service_base(std::string name, uint8_t workers = 1)
        : s_name(std::move(name)), s_workers(workers), s_children({}, this)
    {
    }

    virtual ~service_base() = default;

    bool is_looping() const { return this->s_looping; }

    friend supervisor;

    int s_wakeup_fd{-1};

private:
    void start();

    void stop();

protected:
    struct worker {
        std::thread w_thread;
        std::condition_variable w_cond;
        bool w_kicked{true};
    };

    virtual void* run(worker*);
    virtual void loop_body() {}
    virtual void child_finished(std::shared_ptr<service_base> child) {}
    virtual void stopped() {}
    virtual std::optional<std::chrono::milliseconds> compute_timeout(
        mstime_t current_time) const
    {
        using namespace std::literals::chrono_literals;

        return 1s;
    }

    template<class Rep, class Period>
    void worker_wait(
        worker* w,
        std::unique_lock<std::mutex>& guard,
        const std::optional<std::chrono::duration<Rep, Period>> rel_time)
    {
        if (this->s_looping && this->s_port.mp_messages.empty()) {
            if (w->w_kicked) {
                this->s_ready_workers.emplace_back(w);
                w->w_kicked = false;
            }

            if (rel_time) {
                w->w_cond.wait_for(guard, *rel_time);
            } else {
                w->w_cond.wait(guard);
            }
        }
    }

    void process_msg(msg& msg)
    {
        try {
            msg.m_callback();
        } catch (const std::exception& e) {
            log_error("%s: message failed with -- %s",
                      this->s_name.c_str(),
                      e.what());
            this->s_looping = false;
        } catch (...) {
            log_error("%s: message failed with non-standard exception",
                      this->s_name.c_str());
            this->s_looping = false;
        }
    }

    template<class Rep, class Period>
    void process_for(
        worker* w,
        const std::optional<std::chrono::duration<Rep, Period>> rel_time)
    {
        std::deque<msg> msgs;

        {
            std::unique_lock<std::mutex> guard(this->s_mutex);

            this->worker_wait(w, guard, rel_time);

            std::swap(this->s_port.mp_messages, msgs);
        }

        for (auto& msg : msgs) {
            this->process_msg(msg);
        }
    }

    const std::string s_name;
    bool s_started{false};
    std::mutex s_mutex;
    std::vector<worker> s_workers;
    std::vector<worker*> s_ready_workers;
    std::atomic<bool> s_looping{true};
    msg_port s_port;
    supervisor s_children;

public:
    template<class Rep, class Period>
    void process_for(const std::chrono::duration<Rep, Period> rel_time)
    {
        this->process_for(&this->s_workers.front(),
                          std::make_optional(rel_time));
    }
};

template<typename T, uint8_t WORKERS = 1>
class service : public service_base {
public:
    explicit service(const std::string& sub_name = "")
        : service_base(std::string(__PRETTY_FUNCTION__) + " " + sub_name,
                       WORKERS)
    {
    }

    template<typename F>
    void send(F msg)
    {
        std::lock_guard<std::mutex> lock(this->s_mutex);

        this->s_port.send({
            [lifetime = this->shared_from_this(), this, msg2 = std::move(msg)] {
                msg2(*(static_cast<T*>(this)));
            },
        });

        if (!this->s_ready_workers.empty()) {
            auto* ready = this->s_ready_workers.back();
            this->s_ready_workers.pop_back();
            ready->w_kicked = true;
            ready->w_cond.notify_one();
        }
    }

    template<typename F>
    void send_and_wait(F msg)
    {
        std::mutex reply_lock;
        std::condition_variable reply_cond;
        auto done = false;

        this->send([&done, &reply_lock, &reply_cond, msg = std::move(msg)](
                       auto& looper) {
            auto fi = lnav::finally([&] {
                std::unique_lock<std::mutex> lock(reply_lock);

                done = true;
                reply_cond.notify_one();
            });
            msg(looper);
        });

        if (this->s_wakeup_fd != -1) {
            char bit = 0;
            write(this->s_wakeup_fd, &bit, 1);
        }

        {
            std::unique_lock<std::mutex> reply_lock_guard(reply_lock);
            reply_cond.wait(reply_lock_guard, [&done] { return done; });
        }
    }
};

template<typename T, typename Service, typename... Annotations>
struct to {
    void send(std::function<void(T&)> cb)
    {
        auto& service = injector::get<T&, Service>();

        service.send(std::move(cb));
    }

    void send_and_wait(std::function<void(T)> cb)
    {
        auto& service = injector::get<T&, Service>();

        service.send_and_wait(std::move(cb));
    }
};

}  // namespace isc

#endif
