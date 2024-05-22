/**
 * Copyright (c) 2023, Timothy Stack
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

#include "url_handler.hh"

#include <curl/curl.h>

#include "base/fs_util.hh"
#include "base/injector.hh"
#include "base/paths.hh"
#include "lnav.hh"
#include "service_tags.hh"
#include "url_handler.cfg.hh"

namespace lnav {
namespace url_handler {

void
looper::handler_looper::loop_body()
{
    pollfd pfd[1];

    pfd[0].events = POLLIN;
    pfd[0].fd = this->hl_line_buffer.get_fd();
    pfd[0].revents = 0;

    log_debug("doing url handler poll");
    auto prc = poll(pfd, 1, 100);
    log_debug("poll rc %d", prc);
    if (prc > 0) {
        auto load_res
            = this->hl_line_buffer.load_next_line(this->hl_last_range);

        if (load_res.isErr()) {
            log_error("failed to load next line: %s",
                      load_res.unwrapErr().c_str());
            this->s_looping = false;
        } else {
            auto li = load_res.unwrap();

            log_debug("li %d  %d:%d",
                      li.li_partial,
                      li.li_file_range.fr_offset,
                      li.li_file_range.fr_size);
            if (!li.li_partial && !li.li_file_range.empty()) {
                auto read_res
                    = this->hl_line_buffer.read_range(li.li_file_range);

                if (read_res.isErr()) {
                    log_error("cannot read line: %s",
                              read_res.unwrapErr().c_str());
                } else {
                    auto cmd = trim(to_string(read_res.unwrap()));
                    log_debug("url handler command: %s", cmd.c_str());

                    isc::to<main_looper&, services::main_t>().send(
                        [cmd](auto& mlooper) {
                            auto exec_res
                                = execute_any(lnav_data.ld_exec_context, cmd);
                            if (exec_res.isErr()) {
                                auto um = exec_res.unwrapErr();
                                log_error(
                                    "%s",
                                    um.to_attr_line().get_string().c_str());
                            }
                        });
                }
                this->hl_last_range = li.li_file_range;
            }
        }

        if (this->hl_line_buffer.is_pipe_closed()) {
            log_info("URL handler finished");
            this->s_looping = false;
        }
    }
}

Result<void, lnav::console::user_message>
looper::open(std::string url)
{
    const auto& cfg = injector::get<const config&>();

    log_info("open request for URL: %s", url.c_str());

    auto* cu = curl_url();
    auto set_rc = curl_url_set(
        cu, CURLUPART_URL, url.c_str(), CURLU_NON_SUPPORT_SCHEME);
    if (set_rc != CURLUE_OK) {
        return Err(
            lnav::console::user_message::error(
                attr_line_t("invalid URL: ").append(lnav::roles::file(url)))
                .with_reason(curl_url_strerror(set_rc)));
    }

    char* scheme_part;
    auto get_rc = curl_url_get(cu, CURLUPART_SCHEME, &scheme_part, 0);
    if (get_rc != CURLUE_OK) {
        return Err(lnav::console::user_message::error(
                       attr_line_t("cannot get scheme from URL: ")
                           .append(lnav::roles::file(url)))
                       .with_reason(curl_url_strerror(get_rc)));
    }

    auto proto_iter = cfg.c_schemes.find(scheme_part);
    if (proto_iter == cfg.c_schemes.end()) {
        return Err(lnav::console::user_message::error(
                       attr_line_t("no defined handler for URL scheme: ")
                           .append(lnav::roles::file(scheme_part)))
                       .with_reason(curl_url_strerror(set_rc)));
    }

    log_info("found URL handler: %s",
             proto_iter->second.p_handler.pp_value.c_str());
    auto err_pipe_res = auto_pipe::for_child_fd(STDERR_FILENO);
    if (err_pipe_res.isErr()) {
        return Err(
            lnav::console::user_message::error(
                attr_line_t("cannot open URL: ").append(lnav::roles::file(url)))
                .with_reason(err_pipe_res.unwrapErr()));
    }
    auto err_pipe = err_pipe_res.unwrap();
    auto out_pipe_res = auto_pipe::for_child_fd(STDOUT_FILENO);
    if (out_pipe_res.isErr()) {
        return Err(
            lnav::console::user_message::error(
                attr_line_t("cannot open URL: ").append(lnav::roles::file(url)))
                .with_reason(out_pipe_res.unwrapErr()));
    }
    auto out_pipe = out_pipe_res.unwrap();
    auto child_pid_res = lnav::pid::from_fork();
    if (child_pid_res.isErr()) {
        return Err(
            lnav::console::user_message::error(
                attr_line_t("cannot open URL: ").append(lnav::roles::file(url)))
                .with_reason(child_pid_res.unwrapErr()));
    }

    auto child_pid = child_pid_res.unwrap();

    out_pipe.after_fork(child_pid.in());
    err_pipe.after_fork(child_pid.in());

    auto name = proto_iter->second.p_handler.pp_value;
    if (child_pid.in_child()) {
        auto dev_null = ::open("/dev/null", O_RDONLY | O_CLOEXEC);

        dup2(dev_null, STDIN_FILENO);

        char* host_part = nullptr;
        curl_url_get(cu, CURLUPART_HOST, &host_part, 0);
        std::string host_part_str;
        if (host_part != nullptr) {
            host_part_str = host_part;
        }

        auto source_path = ghc::filesystem::path{
            proto_iter->second.p_handler.pp_location.sl_source.get()};
        auto new_path = lnav::filesystem::build_path({
            source_path.parent_path(),
            lnav::paths::dotlnav() / "formats/default",
        });
        setenv("PATH", new_path.c_str(), 1);
        setenv("URL_HOSTNAME", host_part_str.c_str(), 1);

        const char* args[] = {
            name.c_str(),
            nullptr,
        };

        execvp(name.c_str(), (char**) args);
        _exit(EXIT_FAILURE);
    }

    auto error_queue = std::make_shared<std::vector<std::string>>();
    std::thread err_reader([err = std::move(err_pipe.read_end()),
                            name,
                            error_queue,
                            child_pid = child_pid.in()]() mutable {
        line_buffer lb;
        file_range pipe_range;
        bool done = false;

        log_debug("error reader");
        lb.set_fd(err);
        while (!done) {
            auto load_res = lb.load_next_line(pipe_range);

            if (load_res.isErr()) {
                done = true;
            } else {
                auto li = load_res.unwrap();

                pipe_range = li.li_file_range;
                if (li.li_file_range.empty()) {
                    done = true;
                } else {
                    lb.read_range(li.li_file_range)
                        .then([name, error_queue, child_pid](auto sbr) {
                            auto line_str = string_fragment(
                                                sbr.get_data(), 0, sbr.length())
                                                .trim("\n");
                            if (error_queue->size() < 5) {
                                error_queue->emplace_back(line_str.to_string());
                            }

                            log_debug("%s[%d]: %.*s",
                                      name.c_str(),
                                      child_pid,
                                      line_str.length(),
                                      line_str.data());
                        });
                }
            }
        }
    });
    err_reader.detach();

    auto child = std::make_shared<handler_looper>(
        url, std::move(child_pid), std::move(out_pipe.read_end()));
    this->s_children.add_child_service(child);
    this->l_children[url] = child;

    return Ok();
}

void
looper::close(std::string url)
{
}

}  // namespace url_handler
}  // namespace lnav
