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
 * @file isc.cc
 */

#include <algorithm>

#include "isc.hh"

#include "config.h"

namespace isc {

void
service_base::start()
{
    log_debug("starting service thread for: %s", this->s_name.c_str());
    this->s_thread = std::thread(&service_base::run, this);
    this->s_started = true;
}

void*
service_base::run()
{
    log_info("BEGIN isc thread: %s", this->s_name.c_str());
    while (this->s_looping) {
        mstime_t current_time = getmstime();
        auto timeout = this->compute_timeout(current_time);

        try {
            this->s_port.process_for(timeout);
        } catch (const std::exception& e) {
            log_error("%s: message failed with -- %s",
                      this->s_name.c_str(),
                      e.what());
            this->s_looping = false;
            continue;
        } catch (...) {
            log_error("%s: message failed with non-standard exception",
                      this->s_name.c_str());
            this->s_looping = false;
            continue;
        }
        this->s_children.cleanup_children();

        try {
            this->loop_body();
        } catch (const std::exception& e) {
            log_error("%s: loop_body() failed with -- %s",
                      this->s_name.c_str(),
                      e.what());
            this->s_looping = false;
        } catch (...) {
            log_error("%s: loop_body() failed with non-standard exception",
                      this->s_name.c_str());
            this->s_looping = false;
        }
    }
    if (!this->s_children.empty()) {
        log_debug("stopping children of service: %s", this->s_name.c_str());
        this->s_children.stop_children();
    }
    this->stopped();
    log_info("END isc thread: %s", this->s_name.c_str());

    return nullptr;
}

void
service_base::stop()
{
    if (this->s_started) {
        log_debug("stopping service thread: %s", this->s_name.c_str());
        if (this->s_looping) {
            this->s_looping = false;
            this->s_port.send(empty_msg());
        }
        log_debug("waiting for service thread: %s", this->s_name.c_str());
        this->s_thread.join();
        log_debug("joined service thread: %s", this->s_name.c_str());
        this->s_started = false;
    }
}

supervisor::supervisor(service_list servs, service_base* parent)
    : s_service_list(std::move(servs)), s_parent(parent)
{
    for (auto& serv : this->s_service_list) {
        serv->start();
    }
}

supervisor::~supervisor()
{
    this->stop_children();
}

void
supervisor::stop_children()
{
    for (auto& serv : this->s_service_list) {
        serv->stop();
    }
    this->cleanup_children();
}

void
supervisor::cleanup_children()
{
    this->s_service_list.erase(
        std::remove_if(this->s_service_list.begin(),
                       this->s_service_list.end(),
                       [this](auto& child) {
                           if (child->is_looping()) {
                               return false;
                           }

                           child->stop();
                           if (this->s_parent != nullptr) {
                               this->s_parent->child_finished(child);
                           }
                           return true;
                       }),
        this->s_service_list.end());
}

void
supervisor::add_child_service(std::shared_ptr<service_base> new_service)
{
    this->s_service_list.emplace_back(new_service);
    new_service->start();
}

}  // namespace isc
