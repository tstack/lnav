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

#include "injector.hh"
#include "safe/safe.h"
#include "time_util.hh"

#ifndef lnav_isc_hh
#    define lnav_isc_hh

namespace isc {

struct msg {
    std::function<void()> m_callback;
};

inline msg
empty_msg()
{
    return {[]() {}};
}

class msg_port {
public:
    msg_port() = default;

    void send(msg&& m)
    {
        safe::WriteAccess<safe_message_list, std::unique_lock> writable_msgs(
            this->mp_messages);

        writable_msgs->emplace_back(m);
        this->sp_cond.notify_all();
    }

    template<class Rep, class Period>
    void process_for(const std::chrono::duration<Rep, Period>& rel_time)
    {
        std::deque<msg> tmp_msgs;

        {
            safe::WriteAccess<safe_message_list, std::unique_lock>
                writable_msgs(this->mp_messages);

            if (writable_msgs->empty() && rel_time.count() > 0) {
                this->sp_cond.template wait_for(writable_msgs.lock, rel_time);
            }

            tmp_msgs.swap(*writable_msgs);
        }
        while (!tmp_msgs.empty()) {
            auto& m = tmp_msgs.front();

            m.m_callback();
            tmp_msgs.pop_front();
        }
    }

private:
    using message_list = std::deque<msg>;
    using safe_message_list = safe::Safe<message_list>;

    std::condition_variable sp_cond;
    safe_message_list mp_messages;
};

class service_base;
using service_list = std::vector<std::shared_ptr<service_base>>;

struct supervisor {
    explicit supervisor(service_list servs = {},
                        service_base* parent = nullptr);

    ~supervisor();

    bool empty() const { return this->s_service_list.empty(); }

    void add_child_service(std::shared_ptr<service_base> new_service);

    void stop_children();

    void cleanup_children();

protected:
    service_list s_service_list;
    service_base* s_parent;
};

class service_base : public std::enable_shared_from_this<service_base> {
public:
    explicit service_base(std::string name)
        : s_name(std::move(name)), s_children({}, this)
    {
    }

    virtual ~service_base() = default;

    bool is_looping() const { return this->s_looping; }

    msg_port& get_port() { return this->s_port; }

    friend supervisor;

private:
    void start();

    void stop();

protected:
    virtual void* run();
    virtual void loop_body() {}
    virtual void child_finished(std::shared_ptr<service_base> child) {}
    virtual void stopped() {}
    virtual std::chrono::milliseconds compute_timeout(
        mstime_t current_time) const
    {
        using namespace std::literals::chrono_literals;

        return 1s;
    }

    const std::string s_name;
    bool s_started{false};
    std::thread s_thread;
    std::atomic<bool> s_looping{true};
    msg_port s_port;
    supervisor s_children;
};

template<typename T>
class service : public service_base {
public:
    explicit service(const std::string& sub_name = "")
        : service_base(std::string(__PRETTY_FUNCTION__) + " " + sub_name)
    {
    }

    template<typename F>
    void send(F msg)
    {
        this->s_port.send({
            [lifetime = this->shared_from_this(),
             this,
             msg2 = std::move(msg)]() { msg2(*(static_cast<T*>(this))); },
        });
    }

    template<typename F, class Rep, class Period>
    void send_and_wait(F msg,
                       const std::chrono::duration<Rep, Period>& rel_time)
    {
        msg_port reply_port;

        this->s_port.send({
            [lifetime = this->shared_from_this(),
             this,
             &reply_port,
             msg2 = std::move(msg)]() {
                msg2(*(static_cast<T*>(this)));
                reply_port.send(empty_msg());
            },
        });
        reply_port.process_for(rel_time);
    }
};

template<typename T, typename Service, typename... Annotations>
struct to {
    void send(std::function<void(T&)> cb)
    {
        auto& service = injector::get<T&, Service>();

        service.send(std::move(cb));
    }

    template<class Rep, class Period>
    void send_and_wait(std::function<void(T)> cb,
                       const std::chrono::duration<Rep, Period>& rel_time)
    {
        auto& service = injector::get<T&, Service>();

        service.send_and_wait(std::move(cb), rel_time);
    }

    void send_and_wait(std::function<void(T)> cb)
    {
        using namespace std::literals::chrono_literals;

        this->send_and_wait(std::move(cb), 48h);
    }
};

}  // namespace isc

#endif
