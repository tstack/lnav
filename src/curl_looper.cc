/**
 * Copyright (c) 2015, Timothy Stack
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
 * @file curl_looper.cc
 */

#include <algorithm>

#include "config.h"

#if defined(HAVE_LIBCURL)
#    include <curl/multi.h>

#    include "curl_looper.hh"

using namespace std::chrono_literals;

struct curl_request_eq {
    explicit curl_request_eq(const std::string& name) : cre_name(name){};

    bool operator()(const std::shared_ptr<curl_request>& cr) const
    {
        return this->cre_name == cr->get_name();
    };

    bool operator()(
        const std::pair<mstime_t, std::shared_ptr<curl_request>>& pair) const
    {
        return this->cre_name == pair.second->get_name();
    };

    const std::string& cre_name;
};

int
curl_request::debug_cb(
    CURL* handle, curl_infotype type, char* data, size_t size, void* userp)
{
    curl_request* cr = (curl_request*) userp;
    bool write_to_log;

    switch (type) {
        case CURLINFO_TEXT:
            write_to_log = true;
            break;
        case CURLINFO_HEADER_IN:
        case CURLINFO_HEADER_OUT:
            if (lnav_log_level == lnav_log_level_t::TRACE) {
                write_to_log = true;
            } else {
                write_to_log = false;
            }
            break;
        default:
            write_to_log = false;
            break;
    }

    if (write_to_log) {
        while (size > 0 && isspace(data[size - 1])) {
            size -= 1;
        }
        log_debug("%s:%.*s", cr->get_name().c_str(), size, data);
    }

    return 0;
}

void
curl_looper::loop_body()
{
    mstime_t current_time = getmstime();

    this->perform_io();

    this->check_for_finished_requests();

    this->check_for_new_requests();

    this->requeue_requests(current_time + 5);
}

void
curl_looper::perform_io()
{
    if (this->cl_handle_to_request.empty()) {
        return;
    }

    mstime_t current_time = getmstime();
    auto timeout = this->compute_timeout(current_time);
    int running_handles;

    curl_multi_wait(this->cl_curl_multi, nullptr, 0, timeout.count(), nullptr);
    curl_multi_perform(this->cl_curl_multi, &running_handles);
}

void
curl_looper::requeue_requests(mstime_t up_to_time)
{
    while (!this->cl_poll_queue.empty()
           && this->cl_poll_queue.front().first <= up_to_time)
    {
        auto cr = this->cl_poll_queue.front().second;

        log_debug("%s:polling request is ready again -- %p",
                  cr->get_name().c_str(),
                  cr.get());
        this->cl_handle_to_request[cr->get_handle()] = cr;
        curl_multi_add_handle(this->cl_curl_multi, cr->get_handle());
        this->cl_poll_queue.erase(this->cl_poll_queue.begin());
    }
}

void
curl_looper::check_for_new_requests()
{
    while (!this->cl_new_requests.empty()) {
        auto cr = this->cl_new_requests.back();

        log_info("%s:new curl request %p", cr->get_name().c_str(), cr.get());
        this->cl_handle_to_request[cr->get_handle()] = cr;
        curl_multi_add_handle(this->cl_curl_multi, cr->get_handle());
        this->cl_new_requests.pop_back();
    }
    while (!this->cl_close_requests.empty()) {
        const std::string& name = this->cl_close_requests.back();
        auto all_iter = find_if(this->cl_all_requests.begin(),
                                this->cl_all_requests.end(),
                                curl_request_eq(name));

        log_info("attempting to close request -- %s", name.c_str());
        if (all_iter != this->cl_all_requests.end()) {
            auto cr = *all_iter;

            log_info(
                "%s:closing request -- %p", cr->get_name().c_str(), cr.get());
            (*all_iter)->close();
            auto act_iter = this->cl_handle_to_request.find(cr->get_handle());
            if (act_iter != this->cl_handle_to_request.end()) {
                curl_multi_remove_handle(this->cl_curl_multi, cr->get_handle());
                this->cl_handle_to_request.erase(act_iter);
            }
            auto poll_iter = find_if(this->cl_poll_queue.begin(),
                                     this->cl_poll_queue.end(),
                                     curl_request_eq(name));
            if (poll_iter != this->cl_poll_queue.end()) {
                this->cl_poll_queue.erase(poll_iter);
            }
            this->cl_all_requests.erase(all_iter);
        } else {
            log_error("Unable to find request with the name -- %s",
                      name.c_str());
        }

        this->cl_close_requests.pop_back();
    }
}

void
curl_looper::check_for_finished_requests()
{
    CURLMsg* msg;
    int msgs_left;

    while ((msg = curl_multi_info_read(this->cl_curl_multi, &msgs_left))
           != nullptr) {
        if (msg->msg != CURLMSG_DONE) {
            continue;
        }

        CURL* easy = msg->easy_handle;
        auto iter = this->cl_handle_to_request.find(easy);

        curl_multi_remove_handle(this->cl_curl_multi, easy);
        if (iter != this->cl_handle_to_request.end()) {
            auto cr = iter->second;
            long delay_ms;

            this->cl_handle_to_request.erase(iter);
            delay_ms = cr->complete(msg->data.result);
            if (delay_ms < 0) {
                log_info("%s:curl_request %p finished, deleting...",
                         cr->get_name().c_str(),
                         cr.get());
                auto all_iter = find(this->cl_all_requests.begin(),
                                     this->cl_all_requests.end(),
                                     cr);
                if (all_iter != this->cl_all_requests.end()) {
                    this->cl_all_requests.erase(all_iter);
                }
            } else {
                log_debug("%s:curl_request %p is polling, requeueing in %d",
                          cr->get_name().c_str(),
                          cr.get(),
                          delay_ms);
                this->cl_poll_queue.emplace_back(getmstime() + delay_ms, cr);
                sort(this->cl_poll_queue.begin(), this->cl_poll_queue.end());
            }
        }
    }
}

std::chrono::milliseconds
curl_looper::compute_timeout(mstime_t current_time) const
{
    std::chrono::milliseconds retval = 1s;

    if (!this->cl_handle_to_request.empty()) {
        retval = 1ms;
    } else if (!this->cl_poll_queue.empty()) {
        retval
            = std::max(1ms,
                       std::chrono::milliseconds(
                           this->cl_poll_queue.front().first - current_time));
    }

    ensure(retval.count() > 0);

    return retval;
}

#endif
