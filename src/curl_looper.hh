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
 * @file curl_looper.hh
 */

#ifndef curl_looper_hh
#define curl_looper_hh

#include <atomic>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/isc.hh"
#include "base/lnav.console.hh"
#include "base/result.h"
#include "config.h"

#if !defined(HAVE_LIBCURL)

typedef int CURLcode;

class curl_request {
public:
    curl_request(const std::string& name){};
};

class curl_looper : public isc::service<curl_looper> {
public:
    void start(){};
    void stop(){};
    void add_request(std::shared_ptr<curl_request> cr){};
    void close_request(const std::string& name){};
    void process_all(){};
};

#else
#    include <condition_variable>
#    include <mutex>
#    include <thread>

#    include <curl/curl.h>

#    include "base/auto_mem.hh"
#    include "base/lnav_log.hh"
#    include "base/time_util.hh"

class curl_request {
public:
    explicit curl_request(std::string name);

    curl_request(const curl_request&) = delete;
    curl_request(curl_request&&) = delete;
    void operator=(curl_request&&) = delete;

    virtual ~curl_request() = default;

    const std::string& get_name() const { return this->cr_name; }

    virtual void close() { this->cr_open = false; }

    bool is_open() const { return this->cr_open; }

    CURL* get_handle() const { return this->cr_handle; }

    operator CURL*() const { return this->cr_handle; }

    int get_completions() const { return this->cr_completions; }

    virtual long complete(CURLcode result);

    Result<std::string, CURLcode> perform() const;

    long get_response_code() const
    {
        long retval;

        curl_easy_getinfo(this->get_handle(), CURLINFO_RESPONSE_CODE, &retval);
        return retval;
    }

protected:
    static int debug_cb(
        CURL* handle, curl_infotype type, char* data, size_t size, void* userp);

    static size_t string_cb(void* data, size_t size, size_t nmemb, void* userp);

    const std::string cr_name;
    bool cr_open{true};
    auto_mem<CURL> cr_handle;
    char cr_error_buffer[CURL_ERROR_SIZE];
    int cr_completions{0};
};

class curl_looper : public isc::service<curl_looper> {
public:
    curl_looper();

    void process_all();

    void add_request(const std::shared_ptr<curl_request>& cr)
    {
        require(cr != nullptr);

        this->cl_all_requests.emplace_back(cr);
        this->cl_new_requests.emplace_back(cr);
    }

    void close_request(const std::string& name)
    {
        this->cl_close_requests.emplace_back(name);
    }

protected:
    void loop_body() override;

private:
    void perform_io();
    void check_for_new_requests();
    void check_for_finished_requests();
    void requeue_requests(mstime_t up_to_time);
    std::chrono::milliseconds compute_timeout(
        mstime_t current_time) const override;

    auto_mem<CURLM> cl_curl_multi;
    std::vector<std::shared_ptr<curl_request> > cl_all_requests;
    std::vector<std::shared_ptr<curl_request> > cl_new_requests;
    std::vector<std::string> cl_close_requests;
    std::map<CURL*, std::shared_ptr<curl_request> > cl_handle_to_request;
    std::vector<std::pair<mstime_t, std::shared_ptr<curl_request> > >
        cl_poll_queue;
};
#endif

#endif
