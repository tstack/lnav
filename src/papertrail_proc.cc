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
 * @file papertrail_proc.cc
 */

#include "config.h"

#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#endif

#include "papertrail_proc.hh"
#include "yajl/api/yajl_parse.h"

static const int POLL_DELAY = 2;
static const char *PT_SEARCH_URL = "https://papertrailapp.com/api/v1/events/search.json?min_id=%s&q=%s";

static int read_max_id(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    papertrail_proc *ptp = (papertrail_proc *) ypc->ypc_userdata;

    ptp->ptp_last_max_id = std::string((const char *) str, len);

    return 1;
}

static int ignore_bool(yajlpp_parse_context *ypc, int val)
{
    return 1;
}

static int ignore_str(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    return 1;
}

static int read_event_int(yajlpp_parse_context *ypc, long long val)
{
    papertrail_proc *ptp = (papertrail_proc *) ypc->ypc_userdata;

    yajl_gen_string(ptp->ptp_gen, ypc->get_path_fragment(2));
    yajl_gen_integer(ptp->ptp_gen, val);

    return 1;
}

static int read_event_field(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    papertrail_proc *ptp = (papertrail_proc *) ypc->ypc_userdata;

    yajl_gen_string(ptp->ptp_gen, ypc->get_path_fragment(2));
    yajl_gen_string(ptp->ptp_gen, str, len);

    return 1;
}

static int json_map_start(void *ctx)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;
    papertrail_proc *ptp = (papertrail_proc *) ypc->ypc_userdata;

    if (ypc->ypc_path_index_stack.size() == 3) {
        yajl_gen_map_open(ptp->ptp_gen);
    }

    return 1;
}

static int json_map_end(void *ctx)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;
    papertrail_proc *ptp = (papertrail_proc *) ypc->ypc_userdata;

    if (ypc->ypc_path_index_stack.size() == 2) {
        yajl_gen_map_close(ptp->ptp_gen);
        yajl_gen_reset(ptp->ptp_gen, "\n");
    }

    return 1;
}

struct json_path_handler papertrail_proc::FORMAT_HANDLERS[] = {
        json_path_handler("^/max_id", read_max_id),
        json_path_handler("/(min_id|min_time_at|reached_beginning|reached_record_limit|tail|partial_results)")
                .add_cb(ignore_bool)
                .add_cb(ignore_str),
        json_path_handler("/events#/\\w+")
                .add_cb(read_event_field)
                .add_cb(read_event_int),

        json_path_handler()
};

size_t papertrail_proc::write_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
    yajl_handle handle = (yajl_handle) userp;
    size_t realsize = size * nmemb;

    if (yajl_parse(handle, (const unsigned char *)contents, realsize) != yajl_status_ok) {
        return -1;
    }

    return realsize;
}

void papertrail_proc::yajl_writer(void *context, const char *str, size_t len)
{
    papertrail_proc *ptp = (papertrail_proc *) context;

    write(ptp->ptp_fd, str, len);
}

bool papertrail_proc::start(void)
{
#ifndef HAVE_LIBCURL
    return false;
#else
    this->ptp_api_key = getenv("PAPERTRAIL_API_TOKEN");

    if (this->ptp_api_key == NULL) {
        this->ptp_error = "papertrail search requested, but PAPERTRAIL_API_TOKEN is not set";
        return false;
    }

    char piper_tmpname[PATH_MAX];
    const char *tmpdir;

    if ((tmpdir = getenv("TMPDIR")) == NULL) {
        tmpdir = _PATH_VARTMP;
    }
    snprintf(piper_tmpname, sizeof(piper_tmpname),
             "%s/lnav.papertrail.XXXXXX",
             tmpdir);
    if ((this->ptp_fd = mkstemp(piper_tmpname)) == -1) {
        this->ptp_error = "Unable to make temporary file for papertrail";
        return false;
    }

    unlink(piper_tmpname);

    fcntl(this->ptp_fd.get(), F_SETFD, FD_CLOEXEC);

    if ((this->ptp_child = fork()) < 0) {
        this->ptp_error = "Unable to fork papertrail child";
        return false;
    }

    signal(SIGTERM, SIG_DFL);
    signal(SIGINT, SIG_DFL);

    if (this->ptp_child != 0) {
        return true;
    }

    try {
        this->child_body();
    } catch (...) {
        fprintf(stderr, "papertrail child failed");
    }

    _exit(0);
#endif
};

void papertrail_proc::child_body()
{
#ifdef HAVE_LIBCURL
    int nullfd;

    nullfd = open("/dev/null", O_RDWR);
    dup2(nullfd, STDIN_FILENO);
    dup2(nullfd, STDOUT_FILENO);

    auto_mem<CURL> handle(curl_easy_cleanup);
    yajlpp_parse_context ypc("papertrailapp.com", FORMAT_HANDLERS);
    ypc.ypc_alt_callbacks.yajl_start_map = json_map_start;
    ypc.ypc_alt_callbacks.yajl_end_map = json_map_end;
    yajl_handle jhandle = yajl_alloc(&ypc.ypc_callbacks, NULL, &ypc);
    auto_mem<yajl_gen_t> gen(yajl_gen_free);
    this->ptp_gen = gen = yajl_gen_alloc(NULL);

    ypc.ypc_userdata = this;
    yajl_gen_config(gen, yajl_gen_print_callback, yajl_writer, this);

    const char *quoted_search = curl_easy_escape(
            handle, this->ptp_search.c_str(), this->ptp_search.size());

    bool looping = true;

    while (looping) {
        auto_mem<char> url;
        handle = curl_easy_init();

        asprintf(url.out(), PT_SEARCH_URL, this->ptp_last_max_id.c_str(),
                 quoted_search);
        if (!url.in()) {
            break;
        }
        curl_easy_setopt(handle, CURLOPT_URL, url.in());
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, jhandle);

        auto_mem<struct curl_slist> chunk(curl_slist_free_all);
        auto_mem<char> token_header;

        asprintf(token_header.out(), "X-Papertrail-Token: %s", this->ptp_api_key);
        if (!token_header.in()) {
            break;
        }
        chunk = curl_slist_append(chunk, token_header.in());

        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, chunk.in());

        curl_easy_perform(handle);

        yajl_reset(jhandle);

        sleep(POLL_DELAY);
    }
#endif
}
