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
 * @file papertrail_proc.hh
 */

#ifndef LNAV_PAPERTRAIL_PROC_HH
#define LNAV_PAPERTRAIL_PROC_HH

#ifdef HAVE_LIBCURL

#include <fcntl.h>
#include <paths.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#include <memory>
#include <string>

#include "auto_fd.hh"
#include "auto_mem.hh"
#include "yajlpp.hh"
#include "curl_looper.hh"
#include "line_buffer.hh"

class papertrail_proc : public curl_request {

public:
    papertrail_proc(const std::string &search,
                    time_t min_time,
                    time_t max_time)
            : curl_request("papertrailapp.com"),
              ptp_jcontext(this->cr_name, FORMAT_HANDLERS),
              ptp_jhandle(yajl_free),
              ptp_gen(yajl_gen_free),
              ptp_search(search),
              ptp_quoted_search(curl_free),
              ptp_header_list(curl_slist_free_all),
              ptp_partial_read(false),
              ptp_min_time(min_time),
              ptp_max_time(max_time) {
        char piper_tmpname[PATH_MAX];
        const char *tmpdir;

        if ((tmpdir = getenv("TMPDIR")) == NULL) {
            tmpdir = _PATH_VARTMP;
        }
        snprintf(piper_tmpname, sizeof(piper_tmpname),
                 "%s/lnav.pt.XXXXXX",
                 tmpdir);
        if ((this->ptp_fd = mkstemp(piper_tmpname)) == -1) {
            return;
        }

        unlink(piper_tmpname);

        this->ptp_jcontext.ypc_alt_callbacks.yajl_start_map = json_map_start;
        this->ptp_jcontext.ypc_alt_callbacks.yajl_end_map = json_map_end;
        this->ptp_jcontext.ypc_userdata = this;
        this->ptp_jhandle = yajl_alloc(&this->ptp_jcontext.ypc_callbacks, NULL, &this->ptp_jcontext);

        this->ptp_gen = yajl_gen_alloc(NULL);
        yajl_gen_config(this->ptp_gen, yajl_gen_print_callback, yajl_writer, this);

        curl_easy_setopt(this->cr_handle, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(this->cr_handle, CURLOPT_WRITEDATA, this->ptp_jhandle.in());
        curl_easy_setopt(this->cr_handle, CURLOPT_FAILONERROR, 1L);

        this->ptp_api_key = getenv("PAPERTRAIL_API_TOKEN");

        if (this->ptp_api_key == NULL) {
            this->ptp_error = "papertrail search requested, but PAPERTRAIL_API_TOKEN is not set";
        }

        this->ptp_quoted_search = curl_easy_escape(this->cr_handle,
                                                   this->ptp_search.c_str(),
                                                   this->ptp_search.size());

        asprintf(this->ptp_token_header.out(),
                 "X-Papertrail-Token: %s",
                 this->ptp_api_key);
        this->ptp_header_list = curl_slist_append(this->ptp_header_list,
                this->ptp_token_header.in());

        curl_easy_setopt(this->cr_handle, CURLOPT_HTTPHEADER, this->ptp_header_list.in());

        this->set_url();
    };

    ~papertrail_proc() {
    }

    auto_fd copy_fd() const {
        return this->ptp_fd;
    };

    long complete(CURLcode result);

    void set_url() {
        char base_url[1024];

        snprintf(base_url, sizeof(base_url),
                 PT_SEARCH_URL,
                 this->ptp_last_max_id.c_str());
        if (this->ptp_min_time) {
            size_t base_len = strlen(base_url);
            snprintf(&base_url[base_len], sizeof(base_url) - base_len,
                     "min_time=%ld&",
                     this->ptp_min_time);
        }
        if (this->ptp_max_time) {
            size_t base_len = strlen(base_url);
            snprintf(&base_url[base_len], sizeof(base_url) - base_len,
                     "max_time=%ld&",
                     this->ptp_max_time);
        }
        asprintf(this->ptp_url.out(),
                 "%sq=%s",
                 base_url,
                 this->ptp_quoted_search.in());
        curl_easy_setopt(this->cr_handle, CURLOPT_URL, this->ptp_url.in());
    };

    static size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp);

    static int json_map_start(void *ctx);
    static int json_map_end(void *ctx);

    static void yajl_writer(void *context, const char *str, size_t len);
    static struct json_path_handler FORMAT_HANDLERS[];
    static const char *PT_SEARCH_URL;

    yajlpp_parse_context ptp_jcontext;
    auto_mem<yajl_handle_t> ptp_jhandle;
    auto_mem<yajl_gen_t> ptp_gen;
    const char *ptp_api_key;
    const std::string ptp_search;
    auto_mem<const char> ptp_quoted_search;
    auto_mem<char> ptp_url;
    auto_mem<char> ptp_token_header;
    auto_mem<struct curl_slist> ptp_header_list;
    auto_fd ptp_fd;
    std::string ptp_last_max_id;
    bool ptp_partial_read;
    std::string ptp_error;
    time_t ptp_min_time;
    time_t ptp_max_time;
};

#endif

#endif //LNAV_PAPERTRAIL_PROC_HH
