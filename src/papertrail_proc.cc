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

#include "papertrail_proc.hh"
#include "yajl/api/yajl_parse.h"

const char *papertrail_proc::PT_SEARCH_URL =
        "https://papertrailapp.com/api/v1/events/search.json?min_id=%s&";

static int read_max_id(yajlpp_parse_context *ypc, const unsigned char *str, size_t len)
{
    papertrail_proc *ptp = (papertrail_proc *) ypc->ypc_userdata;

    ptp->ptp_last_max_id = std::string((const char *) str, len);

    return 1;
}

static int read_partial(yajlpp_parse_context *ypc, int val)
{
    papertrail_proc *ptp = (papertrail_proc *) ypc->ypc_userdata;

    if (val) {
        ptp->ptp_partial_read = true;
    }

    return 1;
}

static int read_limit(yajlpp_parse_context *ypc, int val)
{
    papertrail_proc *ptp = (papertrail_proc *) ypc->ypc_userdata;

    if (val) {
        ptp->ptp_partial_read = true;
    }

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

int papertrail_proc::json_map_start(void *ctx)
{
    yajlpp_parse_context *ypc = (yajlpp_parse_context *)ctx;
    papertrail_proc *ptp = (papertrail_proc *) ypc->ypc_userdata;

    if (ypc->ypc_path_index_stack.size() == 3) {
        yajl_gen_map_open(ptp->ptp_gen);
    }

    return 1;
}

int papertrail_proc::json_map_end(void *ctx)
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
        json_path_handler("^/(partial_results)", read_partial),
        json_path_handler("^/(reached_record_limit|reached_time_limit)", read_limit),
        json_path_handler("^/(min_id|min_time_at|max_time_at|"
                                  "reached_beginning|reached_end|tail|no_events)")
                .add_cb(ignore_bool)
                .add_cb(ignore_str),
        json_path_handler("^/events#/\\w+")
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

long papertrail_proc::complete(CURLcode result)
{
    curl_request::complete(result);

    yajl_reset(this->ptp_jhandle.in());

    if (result != CURLE_OK) {
        static const char *err_msg = "Unable to execute papertrail search -- ";

        write(this->ptp_fd, err_msg, strlen(err_msg));
        write(this->ptp_fd, this->cr_error_buffer, strlen(this->cr_error_buffer));
        return -1;
    }

    if (this->ptp_max_time) {
        return -1;
    }

    this->set_url();
    if (this->ptp_partial_read) {
        this->ptp_partial_read = false;
        return 1;
    }

    return 3000;
}

#endif
