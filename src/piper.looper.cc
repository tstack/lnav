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

#include <chrono>
#include <thread>

#include "piper.looper.hh"

#include <poll.h>

#include "base/fs_util.hh"
#include "base/injector.hh"
#include "base/paths.hh"
#include "base/time_util.hh"
#include "config.h"
#include "line_buffer.hh"
#include "lnav_util.hh"
#include "piper.looper.cfg.hh"

using namespace std::chrono_literals;

static ssize_t
write_timestamp(int fd, log_level_t level, off_t woff)
{
    char time_str[64];
    struct timeval tv;

    gettimeofday(&tv, nullptr);
    fmt::format_to_n(time_str,
                     sizeof(time_str),
                     FMT_STRING("{}.{}:{};"),
                     tv.tv_sec,
                     tv.tv_usec,
                     level_names[level][0]);

    return pwrite(fd, time_str, strlen(time_str), woff);
}

namespace lnav {
namespace piper {

looper::looper(std::string name, auto_fd stdout_fd, auto_fd stderr_fd)
    : l_name(std::move(name)), l_stdout(std::move(stdout_fd)),
      l_stderr(std::move(stderr_fd))
{
    size_t count = 0;
    do {
        this->l_out_dir
            = lnav::paths::workdir()
            / fmt::format(
                  FMT_STRING("piper-{}-{}"),
                  hasher().update(getmstime()).update(l_name).to_string(),
                  count);
        count += 1;
    } while (ghc::filesystem::exists(this->l_out_dir));
    ghc::filesystem::create_directories(this->l_out_dir);
    this->l_future = std::async(std::launch::async, [this]() { this->loop(); });
}

looper::~looper()
{
    log_info("piper destructed, shutting down: %s", this->l_name.c_str());
    this->l_looping = false;
    this->l_future.wait();
}

enum class read_mode_t {
    binary,
    line,
};

void
looper::loop()
{
    const auto& cfg = injector::get<const config&>();
    struct pollfd pfd[2];
    struct {
        line_buffer lb;
        file_range last_range;
        pollfd* pfd{nullptr};
        log_level_t cf_level{LEVEL_INFO};
        read_mode_t cf_read_mode{read_mode_t::line};

        void reset_pfd()
        {
            this->pfd->fd = this->lb.get_fd();
            this->pfd->events = POLLIN;
            this->pfd->revents = 0;
        }
    } captured_fds[2];
    off_t woff = 0, last_woff = 0;
    auto_fd outfd;
    size_t rotate_count = 0;

    log_info("starting loop to capture: %s (%d %d)",
             this->l_name.c_str(),
             this->l_stdout.get(),
             this->l_stderr.get());
    captured_fds[0].lb.set_fd(this->l_stdout);
    if (this->l_stderr.has_value()) {
        captured_fds[1].lb.set_fd(this->l_stderr);
    }
    captured_fds[1].cf_level = LEVEL_ERROR;
    do {
        static const auto TIMEOUT
            = std::chrono::duration_cast<std::chrono::milliseconds>(1s).count();

        size_t used_pfds = 0;
        for (auto& cap : captured_fds) {
            if (cap.lb.get_fd() != -1 && cap.lb.is_pipe()
                && !cap.lb.is_pipe_closed())
            {
                cap.pfd = &pfd[used_pfds];
                used_pfds += 1;
                cap.reset_pfd();
            } else {
                cap.pfd = nullptr;
            }
        }

        if (used_pfds == 0) {
            log_info("inputs consumed, breaking loop: %s",
                     this->l_name.c_str());
            this->l_looping = false;
            break;
        }

        auto poll_rc = poll(pfd, used_pfds, TIMEOUT);
        if (poll_rc == 0) {
            // update the timestamp to keep the file alive from any
            // cleanup processes
            if (outfd.has_value()) {
                log_perror(futimes(outfd.get(), nullptr));
            }
            continue;
        }
        for (auto& cap : captured_fds) {
            while (this->l_looping) {
                if (cap.pfd == nullptr
                    || !(cap.pfd->revents & (POLLIN | POLLHUP)))
                {
                    break;
                }

                if (cap.cf_read_mode == read_mode_t::binary) {
                    char buffer[8192];
                    auto read_rc
                        = read(cap.lb.get_fd(), buffer, sizeof(buffer));

                    if (read_rc < 0) {
                        if (errno == EAGAIN) {
                            break;
                        }
                        log_error("failed to read next chunk: %s -- %s",
                                  this->l_name.c_str(),
                                  strerror(errno));
                        this->l_looping = false;
                    } else if (read_rc == 0) {
                        this->l_looping = false;
                    } else {
                        auto rc = write(outfd.get(), buffer, read_rc);
                        if (rc != read_rc) {
                            log_error(
                                "failed to write to capture file: %s -- %s",
                                this->l_name.c_str(),
                                strerror(errno));
                        }
                    }
                    continue;
                }

                auto load_result = cap.lb.load_next_line(cap.last_range);

                if (load_result.isErr()) {
                    log_error("failed to load next line: %s -- %s",
                              this->l_name.c_str(),
                              load_result.unwrapErr().c_str());
                    this->l_looping = false;
                    break;
                }

                auto li = load_result.unwrap();

                if (cap.last_range.fr_offset == 0 && !cap.lb.is_header_utf8()) {
                    log_info("switching capture to binary mode: %s",
                             this->l_name.c_str());
                    cap.cf_read_mode = read_mode_t::binary;

                    auto out_path = this->l_out_dir / "out.0";
                    log_info("creating binary capture file: %s -- %s",
                             this->l_name.c_str(),
                             out_path.c_str());
                    auto create_res = lnav::filesystem::create_file(
                        out_path, O_WRONLY | O_CLOEXEC | O_TRUNC, 0600);
                    if (create_res.isErr()) {
                        log_error("unable to open capture file: %s -- %s",
                                  this->l_name.c_str(),
                                  create_res.unwrapErr().c_str());
                        break;
                    }

                    outfd = create_res.unwrap();
                    auto header_avail = cap.lb.get_available();
                    auto read_res = cap.lb.read_range(header_avail);
                    if (read_res.isOk()) {
                        auto sbr = read_res.unwrap();
                        write(outfd.get(), sbr.get_data(), sbr.length());
                    } else {
                        log_error("failed to get header data: %s -- %s",
                                  this->l_name.c_str(),
                                  read_res.unwrapErr().c_str());
                    }
                    continue;
                }

                if (li.li_partial && !cap.lb.is_pipe_closed()) {
                    break;
                }

                if (li.li_file_range.empty()) {
                    break;
                }

                auto read_result = cap.lb.read_range(li.li_file_range);

                if (read_result.isErr()) {
                    log_error("failed to read next line: %s -- %s",
                              this->l_name.c_str(),
                              read_result.unwrapErr().c_str());
                    this->l_looping = false;
                    break;
                }

                auto sbr = read_result.unwrap();

                if (woff > last_woff && woff >= cfg.c_max_size) {
                    log_info(
                        "capture file has reached max size, rotating: %s -- "
                        "%lld",
                        this->l_name.c_str(),
                        woff);
                    outfd.reset();
                }

                if (!outfd.has_value()) {
                    auto out_path = this->l_out_dir
                        / fmt::format(FMT_STRING("out.{}"),
                                      rotate_count % cfg.c_rotations);
                    log_info("creating capturing file: %s -- %s",
                             this->l_name.c_str(),
                             out_path.c_str());
                    auto create_res = lnav::filesystem::create_file(
                        out_path, O_WRONLY | O_CLOEXEC | O_TRUNC, 0600);
                    if (create_res.isErr()) {
                        log_error("unable to open capture file: %s -- %s",
                                  this->l_name.c_str(),
                                  create_res.unwrapErr().c_str());
                        break;
                    }

                    outfd = create_res.unwrap();
                    rotate_count += 1;

                    static const char lnav_header[]
                        = {'L', 0, 'N', 1, 0, 0, 0, 0};
                    auto prc
                        = write(outfd.get(), lnav_header, sizeof(lnav_header));
                    woff = prc;
                }

                ssize_t wrc;

                last_woff = woff;
                wrc = write_timestamp(outfd.get(), cap.cf_level, woff);
                if (wrc == -1) {
                    log_error("unable to write timestamp: %s -- %s",
                              this->l_name.c_str(),
                              strerror(errno));
                    this->l_looping = false;
                    break;
                }
                woff += wrc;

                /* Need to do pwrite here since the fd is used by the main
                 * lnav process as well.
                 */
                wrc = pwrite(outfd.get(), sbr.get_data(), sbr.length(), woff);
                if (wrc == -1) {
                    log_error("unable to write captured data: %s -- %s",
                              this->l_name.c_str(),
                              strerror(errno));
                    this->l_looping = false;
                    break;
                }
                woff += wrc;

                cap.last_range = li.li_file_range;
                if (li.li_partial && sbr.get_data()[sbr.length() - 1] != '\n'
                    && (cap.last_range.next_offset() != cap.lb.get_file_size()))
                {
                    woff = last_woff;
                }
            }
        }
    } while (this->l_looping);

    log_info("exiting loop to capture: %s", this->l_name.c_str());
}

Result<handle<state::running>, std::string>
create_looper(std::string name, auto_fd stdout_fd, auto_fd stderr_fd)
{
    return Ok(handle<state::running>(std::make_shared<looper>(
        name, std::move(stdout_fd), std::move(stderr_fd))));
}

}  // namespace piper
}  // namespace lnav
