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
 */

#ifndef url_loader_hh
#define url_loader_hh

#include "config.h"

#ifdef HAVE_LIBCURL
#    include <curl/curl.h>
#    include <paths.h>

#    include "base/fs_util.hh"
#    include "base/paths.hh"
#    include "curl_looper.hh"

class url_loader : public curl_request {
public:
    url_loader(const std::string& url) : curl_request(url)
    {
        std::error_code errc;
        std::filesystem::create_directories(lnav::paths::workdir(), errc);
        auto tmp_res = lnav::filesystem::open_temp_file(lnav::paths::workdir()
                                                        / "url.XXXXXX");
        if (tmp_res.isErr()) {
            return;
        }

        auto tmp_pair = tmp_res.unwrap();
        this->ul_path = tmp_pair.first;
        this->ul_fd = std::move(tmp_pair.second);

        curl_easy_setopt(this->cr_handle, CURLOPT_URL, this->cr_name.c_str());
        curl_easy_setopt(this->cr_handle, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(this->cr_handle, CURLOPT_WRITEDATA, this);
        curl_easy_setopt(this->cr_handle, CURLOPT_FILETIME, 1);
        curl_easy_setopt(this->cr_handle, CURLOPT_BUFFERSIZE, 128L * 1024L);
    }

    std::filesystem::path get_path() const { return this->ul_path; }

    long complete(CURLcode result)
    {
        curl_request::complete(result);

        switch (result) {
            case CURLE_OK:
                break;
            case CURLE_BAD_DOWNLOAD_RESUME:
                break;
            default:
                log_error("%s:curl failure -- %ld %s",
                          this->cr_name.c_str(),
                          result,
                          curl_easy_strerror(result));
                log_perror(write(this->ul_fd,
                                 this->cr_error_buffer,
                                 strlen(this->cr_error_buffer)));
                return -1;
        }

        long file_time;
        CURLcode rc;

        rc = curl_easy_getinfo(this->cr_handle, CURLINFO_FILETIME, &file_time);
        if (rc == CURLE_OK) {
            time_t current_time;

            time(&current_time);
            if (file_time == -1
                || (current_time - file_time) < FOLLOW_IF_MODIFIED_SINCE)
            {
                char range[64];
                struct stat st;
                off_t start;

                fstat(this->ul_fd, &st);
                if (st.st_size > 0) {
                    start = st.st_size - 1;
                    this->ul_resume_offset = 1;
                } else {
                    start = 0;
                    this->ul_resume_offset = 0;
                }
                snprintf(range, sizeof(range), "%ld-", (long) start);
                curl_easy_setopt(this->cr_handle, CURLOPT_RANGE, range);
                return 2000;
            } else {
                log_debug("URL was not recently modified, not tailing: %s",
                          this->cr_name.c_str());
            }
        } else {
            log_error("Could not get file time for URL: %s -- %s",
                      this->cr_name.c_str(),
                      curl_easy_strerror(rc));
        }

        return -1;
    }

private:
    static const long FOLLOW_IF_MODIFIED_SINCE = 60 * 60;

    static ssize_t write_cb(void* contents,
                            size_t size,
                            size_t nmemb,
                            void* userp)
    {
        url_loader* ul = (url_loader*) userp;
        char* c_contents = (char*) contents;
        ssize_t retval;

        c_contents += ul->ul_resume_offset;
        retval = write(
            ul->ul_fd, c_contents, (size * nmemb) - ul->ul_resume_offset);
        retval += ul->ul_resume_offset;
        ul->ul_resume_offset = 0;
        return retval;
    }

    std::filesystem::path ul_path;
    auto_fd ul_fd;
    off_t ul_resume_offset{0};
};
#endif

#endif
