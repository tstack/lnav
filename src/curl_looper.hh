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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file curl_looper.hh
 */

#ifndef curl_looper_hh
#define curl_looper_hh

#include <map>
#include <string>
#include <vector>

#ifndef HAVE_LIBCURL

typedef int CURLcode;

class curl_request {
public:
    curl_request(const std::string &name) {
    };
};

class curl_looper {
public:
    void start() { };
    void stop() { };
    void add_request(curl_request *cr) { };
    void close_request(const std::string &name) { };
    void process_all() { };
};

#else
#include <curl/curl.h>

#include "auto_mem.hh"
#include "lnav_log.hh"
#include "lnav_util.hh"
#include "pthreadpp.hh"

class curl_request {
public:
    curl_request(const std::string &name)
            : cr_name(name),
              cr_open(true),
              cr_handle(curl_easy_cleanup),
              cr_completions(0) {
        this->cr_handle.reset(curl_easy_init());
        curl_easy_setopt(this->cr_handle, CURLOPT_NOSIGNAL, 1);
        curl_easy_setopt(this->cr_handle, CURLOPT_ERRORBUFFER, this->cr_error_buffer);
        curl_easy_setopt(this->cr_handle, CURLOPT_DEBUGFUNCTION, debug_cb);
        curl_easy_setopt(this->cr_handle, CURLOPT_DEBUGDATA, this);
        curl_easy_setopt(this->cr_handle, CURLOPT_VERBOSE, 1);
        if (getenv("SSH_AUTH_SOCK") != NULL) {
            curl_easy_setopt(this->cr_handle, CURLOPT_SSH_AUTH_TYPES,
                             CURLSSH_AUTH_AGENT|CURLSSH_AUTH_PASSWORD);
        }
    };

    virtual ~curl_request() {

    };

    const std::string &get_name() const {
        return this->cr_name;
    };

    virtual void close() {
        this->cr_open = false;
    };

    bool is_open() {
        return this->cr_open;
    };

    CURL *get_handle() const {
        return this->cr_handle;
    };

    int get_completions() const {
        return this->cr_completions;
    };

    virtual long complete(CURLcode result) {
        double total_time = 0, download_size = 0, download_speed = 0;

        this->cr_completions += 1;
        curl_easy_getinfo(this->cr_handle, CURLINFO_TOTAL_TIME, &total_time);
        log_debug("%s: total_time=%f", this->cr_name.c_str(), total_time);
        curl_easy_getinfo(this->cr_handle, CURLINFO_SIZE_DOWNLOAD, &download_size);
        log_debug("%s: download_size=%f", this->cr_name.c_str(), download_size);
        curl_easy_getinfo(this->cr_handle, CURLINFO_SPEED_DOWNLOAD, &download_speed);
        log_debug("%s: download_speed=%f", this->cr_name.c_str(), download_speed);

        return -1;
    };

protected:

    static int debug_cb(CURL *handle,
                        curl_infotype type,
                        char *data,
                        size_t size,
                        void *userp);

    const std::string cr_name;
    bool cr_open;
    auto_mem<CURL> cr_handle;
    char cr_error_buffer[CURL_ERROR_SIZE];
    int cr_completions;
};

class curl_looper {
public:
    curl_looper()
            : cl_started(false),
              cl_looping(true),
              cl_curl_multi(curl_multi_cleanup) {
        this->cl_curl_multi.reset(curl_multi_init());
        pthread_mutex_init(&this->cl_mutex, NULL);
        pthread_cond_init(&this->cl_cond, NULL);
    };

    ~curl_looper() {
        this->stop();
        pthread_cond_destroy(&this->cl_cond);
        pthread_mutex_destroy(&this->cl_mutex);
    }

    void start() {
        if (pthread_create(&this->cl_thread, NULL, trampoline, this) == 0) {
            this->cl_started = true;
        }
    };

    void stop() {
        if (this->cl_started) {
            void *result;

            this->cl_looping = false;
            {
                mutex_guard mg(this->cl_mutex);

                pthread_cond_broadcast(&this->cl_cond);
            }
            log_debug("waiting for curl_looper thread");
            pthread_join(this->cl_thread, &result);
            log_debug("curl_looper thread joined");
            this->cl_started = false;
        }
    };

    void process_all() {
        this->check_for_new_requests();

        this->requeue_requests(LONG_MAX);

        while (!this->cl_handle_to_request.empty()) {
            this->perform_io();

            this->check_for_finished_requests();
        }
    };

    void add_request(curl_request *cr) {
        mutex_guard mg(this->cl_mutex);

        require(cr != NULL);

        this->cl_all_requests.push_back(cr);
        this->cl_new_requests.push_back(cr);
        pthread_cond_broadcast(&this->cl_cond);
    };

    void close_request(const std::string &name) {
        mutex_guard mg(this->cl_mutex);

        this->cl_close_requests.push_back(name);
        pthread_cond_broadcast(&this->cl_cond);
    };

private:

    void *run();
    void loop_body();
    void perform_io();
    void check_for_new_requests();
    void check_for_finished_requests();
    void requeue_requests(mstime_t up_to_time);

    int compute_timeout(mstime_t current_time) const {
        int retval = 1000;

        if (!this->cl_poll_queue.empty()) {
            retval = std::max(
                    (mstime_t) 1,
                    this->cl_poll_queue.front().first - current_time);
        }

        ensure(retval > 0);

        return retval;
    };

    static void *trampoline(void *arg);

    bool cl_started;
    pthread_t cl_thread;
    volatile bool cl_looping;
    auto_mem<CURLM> cl_curl_multi;
    pthread_mutex_t cl_mutex;
    pthread_cond_t cl_cond;
    std::vector<curl_request *> cl_all_requests;
    std::vector<curl_request *> cl_new_requests;
    std::vector<std::string> cl_close_requests;
    std::map<CURL *, curl_request *> cl_handle_to_request;
    std::vector<std::pair<mstime_t, curl_request *> > cl_poll_queue;

};
#endif

#endif
