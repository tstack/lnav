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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file isc.hh
 */

#include <atomic>
#include <deque>
#include <chrono>
#include <mutex>
#include <thread>
#include <condition_variable>

#include "injector.hh"
#include "time_util.hh"
#include "safe/safe.h"

#ifndef lnav_isc_hh
#define lnav_isc_hh

namespace isc {

struct msg {
    std::function<void()> m_callback;
};

inline msg empty_msg() {
    return { []() { } };
}

class msg_port {
public:
    msg_port() = default;

    void send(msg&& m) {
        safe::WriteAccess<safe_message_list, std::unique_lock> writable_msgs(this->mp_messages);

        writable_msgs->emplace_back(m);
        this->sp_cond.notify_all();
    }

    template<class Rep, class Period>
    void process_for(const std::chrono::duration<Rep, Period>& rel_time) {
        std::deque<msg> tmp_msgs;

        {
            safe::WriteAccess<safe_message_list, std::unique_lock> writable_msgs(
                this->mp_messages);

            if (writable_msgs->empty()) {
                this->sp_cond.template wait_for(writable_msgs.lock, rel_time);
            }

            tmp_msgs.swap(*writable_msgs);
        }
        while (!tmp_msgs.empty()) {
            auto &m = tmp_msgs.front();

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

class service {
public:
    virtual ~service() = default;

    void start() {
        log_debug("starting service thread");
        this->s_thread = std::thread(&service::run, this);
        this->s_started = true;
    };

    void stop() {
        if (this->s_started) {
            this->s_looping = false;
            this->s_port.send(empty_msg());
            log_debug("waiting for service thread");
            this->s_thread.join();
            log_debug("service thread joined");
            this->s_started = false;
        }
    };

    msg_port& get_port() { return this->s_port; };

protected:
    void *run();
    virtual void loop_body() {};
    virtual std::chrono::milliseconds compute_timeout(mstime_t current_time) const {
        using namespace std::literals::chrono_literals;

        return 1s;
    };

    bool s_started{false};
    std::thread s_thread;
    std::atomic<bool> s_looping{true};
    msg_port s_port;
};

using service_list = std::vector<std::shared_ptr<service>>;

struct service_guard {
    service_guard(service_list servs)
        : sg_service_list(servs) {
        for (auto& serv : servs) {
            serv->start();
        }
    };

    ~service_guard() {
        for (auto& serv : this->sg_service_list) {
            serv->stop();
        }
    }
private:
    service_list sg_service_list;
};

template<typename T, typename Service, typename...Annotations>
struct to {
    void send(std::function<void(T)> cb) {
        auto& service = injector::get<isc::service&, Service>();

        service.get_port().send({
            [cb]() {
                cb(injector::get<T, Service, Annotations...>());
            }
        });
    }

    template<class Rep, class Period>
    void send_and_wait(std::function<void(T)> cb,
                       const std::chrono::duration<Rep, Period>& rel_time) {
        auto& service = injector::get<isc::service&, Service>();
        msg_port reply_port;

        service.get_port().send({
            [cb, &reply_port]() {
                cb(injector::get<T, Service, Annotations...>());
                reply_port.send(empty_msg());
            }
        });
        reply_port.template process_for(rel_time);
    }

    void send_and_wait(std::function<void(T)> cb) {
        using namespace std::literals::chrono_literals;

        this->send_and_wait(cb, 48h);
    }
};

}

#endif
