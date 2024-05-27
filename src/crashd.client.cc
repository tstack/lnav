/**
 * Copyright (c) 2024, Timothy Stack
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

#include "crashd.client.hh"

#include "base/fs_util.hh"
#include "curl_looper.hh"
#include "hasher.hh"

namespace lnav::crashd::client {

static constexpr auto USER_AGENT = "lnav/" PACKAGE_VERSION;
static constexpr auto SECRET = "2F40374C-25CE-4472-883F-CBBA4660A586";

static int
progress_tramp(
    void* clientp, double dltotal, double dlnow, double ultotal, double ulnow)
{
    progress_callback& cb = *static_cast<progress_callback*>(clientp);

    switch (cb(dltotal, dlnow, ultotal, ulnow)) {
        case progress_result_t::ok:
            return 0;
        default:
            return 1;
    }
}

Result<void, lnav::console::user_message>
upload(const std::filesystem::path& log_path, progress_callback cb)
{
    const auto read_res = lnav::filesystem::read_file(log_path);
    if (read_res.isErr()) {
        return Err(lnav::console::user_message::error(
                       attr_line_t("unable to read crash log: ")
                           .append(lnav::roles::file(log_path)))
                       .with_reason(read_res.unwrapErr()));
    }

    const auto log_content = read_res.unwrap();
    std::string hash_str;
    auto nonce = 0;

    {
        while (true) {
            auto ha = hasher();

            ha.update(fmt::to_string(nonce));
            ha.update(log_content);

            hash_str = ha.to_string();
            if (startswith(hash_str, "0000")) {
                break;
            }
            nonce += 1;
        }
    }

    curl_request cr("https://crash.lnav.org/crash");

    curl_easy_setopt(cr, CURLOPT_VERBOSE, 0);
    curl_easy_setopt(cr, CURLOPT_URL, cr.get_name().c_str());
    curl_easy_setopt(cr, CURLOPT_POST, 1);
    curl_easy_setopt(cr, CURLOPT_POSTFIELDS, log_content.c_str());
    curl_easy_setopt(cr, CURLOPT_POSTFIELDSIZE, log_content.size());
    curl_easy_setopt(cr, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(cr, CURLOPT_XFERINFODATA, &cb);
    curl_easy_setopt(cr, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(cr, CURLOPT_XFERINFOFUNCTION, progress_tramp);

    const auto secret_header
        = fmt::format(FMT_STRING("lnav-secret: {}"), SECRET);
    const auto nonce_header
        = fmt::format(FMT_STRING("X-lnav-nonce: {}"), nonce);
    const auto hash_header
        = fmt::format(FMT_STRING("X-lnav-hash: {}"), hash_str);

    auto_mem<curl_slist> list(curl_slist_free_all);

    list = curl_slist_append(list, "Content-Type: text/plain");
    list = curl_slist_append(list, secret_header.c_str());
    list = curl_slist_append(list, nonce_header.c_str());
    list = curl_slist_append(list, hash_header.c_str());

    curl_easy_setopt(cr, CURLOPT_HTTPHEADER, list.in());

    const auto perform_res = cr.perform();
    if (perform_res.isErr()) {
        return Err(
            lnav::console::user_message::error("unable to upload crash log")
                .with_reason(curl_easy_strerror(perform_res.unwrapErr())));
    }

    const auto response = perform_res.unwrap();
    if (cr.get_response_code() != 200) {
        return Err(lnav::console::user_message::error(
                       attr_line_t("server rejected crash log: ")
                           .append(lnav::roles::file(log_path)))
                       .with_reason(response));
    }
    log_info("crashd response: %s", response.c_str());

    return Ok();
}

}  // namespace lnav::crashd::client
