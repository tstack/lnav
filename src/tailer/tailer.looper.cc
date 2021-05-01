/**
 * Copyright (c) 2021, Timothy Stack
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
 */

#include "config.h"

#include "base/lnav_log.hh"
#include "tailer.looper.hh"
#include "tailer.h"
#include "tailerpp.hh"
#include "lnav.hh"
#include "service_tags.hh"
#include "line_buffer.hh"
#include "tailerbin.h"

using namespace std::chrono_literals;

static const auto HOST_RETRY_DELAY = 1min;

static void read_err_pipe(const std::string &netloc, auto_fd &err,
                          std::vector<std::string> &eq)
{
    line_buffer lb;
    file_range pipe_range;
    bool done = false;

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
                lb.read_range(li.li_file_range).then([netloc, &eq](auto sbr) {
                    auto line_str = string_fragment(sbr.get_data(),
                                                    0,
                                                    sbr.length());
                    line_str.trim("\n");
                    if (eq.size() < 10) {
                        eq.template emplace_back(line_str.to_string());
                    }
                    log_debug("tailer(%s): %.*s",
                              netloc.c_str(),
                              line_str.length(),
                              line_str.data());
                });
            }
        }
    }
}

void tailer::looper::loop_body()
{
    auto now = std::chrono::steady_clock::now();

    for (auto& qpair : this->l_netlocs_to_paths) {
        auto& netloc = qpair.first;
        auto& rpq = qpair.second;

        if (now < rpq.rpq_next_attempt_time) {
            continue;
        }
        if (this->l_remotes.count(netloc) == 0) {
            auto create_res = host_tailer::for_host(netloc);

            if (create_res.isErr()) {
                this->report_error(netloc, create_res.unwrapErr());
                rpq.rpq_next_attempt_time = now + HOST_RETRY_DELAY;
                continue;
            }

            auto ht = create_res.unwrap();
            this->l_remotes[netloc] = ht;
            this->s_children.add_child_service(ht);

            rpq.rpq_new_paths.insert(rpq.rpq_existing_paths.begin(),
                                     rpq.rpq_existing_paths.end());
            rpq.rpq_existing_paths.clear();
        }

        if (!rpq.rpq_new_paths.empty()) {
            log_debug("%s: new paths to monitor -- %s",
                      netloc.c_str(),
                      rpq.rpq_new_paths.begin()->c_str());
            this->l_remotes[netloc]->send(
                [paths = rpq.rpq_new_paths](auto &ht) {
                    for (const auto &path : paths) {
                        log_debug("  adding path to tailer -- %s",
                                  path.c_str());
                        ht.open_remote_path(path);
                    }
                });

            rpq.rpq_existing_paths.insert(rpq.rpq_new_paths.begin(),
                                          rpq.rpq_new_paths.end());
            rpq.rpq_new_paths.clear();
        }
    }
}

void tailer::looper::add_remote(std::string netloc, std::string path)
{
    this->l_netlocs_to_paths[netloc].rpq_new_paths.insert(path);
}

Result<std::shared_ptr<tailer::looper::host_tailer>, std::string>
tailer::looper::host_tailer::for_host(const std::string& netloc)
{
    log_debug("tailer(%s): transferring tailer to remote", netloc.c_str());
    {
        auto in_pipe = TRY(auto_pipe::for_child_fd(STDIN_FILENO));
        auto out_pipe = TRY(auto_pipe::for_child_fd(STDOUT_FILENO));
        auto err_pipe = TRY(auto_pipe::for_child_fd(STDERR_FILENO));
        auto child = TRY(lnav::pid::from_fork());

        in_pipe.after_fork(child.in());
        out_pipe.after_fork(child.in());
        err_pipe.after_fork(child.in());

        if (child.in_child()) {
            execlp("ssh", "ssh",
                   "-oStrictHostKeyChecking=no",
                   "-oBatchMode=yes",
                   netloc.c_str(),
                   "cat > tailer.bin && chmod ugo+rx ./tailer.bin",
                   nullptr);
            exit(EXIT_FAILURE);
        }

        std::vector<std::string> error_queue;
        log_debug("starting err reader");
        std::thread err_reader([netloc, err = std::move(err_pipe.read_end()), &error_queue]() mutable {
            read_err_pipe(netloc, err, error_queue);
        });

        log_debug("writing to child");
        auto sf = tailer_bin[0].to_string_fragment();
        ssize_t total_bytes = 0;

        while (total_bytes < sf.length()) {
            auto rc = write(in_pipe.write_end(), sf.data(), sf.length() - total_bytes);

            if (rc < 0) {
                break;
            }
            total_bytes += rc;
        }

        in_pipe.write_end().reset();

        while (true) {
            char buffer[1024];

            auto rc = read(out_pipe.read_end(), buffer, sizeof(buffer));
            if (rc < 0) {
                break;
            }
            if (rc == 0) {
                break;
            }
            log_debug("tailer(%s): transfer output -- %.*s",
                      netloc.c_str(), rc, buffer);
        }

        auto finished_child = std::move(child).wait_for_child();

        err_reader.join();
        if (!finished_child.was_normal_exit() ||
            finished_child.exit_status() != EXIT_SUCCESS) {
            auto error_msg = error_queue.empty() ? "unknown" : error_queue.back();
            return Err(fmt::format("failed to ssh to host: {}", error_msg));
        }
    }

    auto in_pipe = TRY(auto_pipe::for_child_fd(STDIN_FILENO));
    auto out_pipe = TRY(auto_pipe::for_child_fd(STDOUT_FILENO));
    auto err_pipe = TRY(auto_pipe::for_child_fd(STDERR_FILENO));
    auto child = TRY(lnav::pid::from_fork());

    in_pipe.after_fork(child.in());
    out_pipe.after_fork(child.in());
    err_pipe.after_fork(child.in());

    if (child.in_child()) {
        execlp("ssh", "ssh",
               // "-q",
               "-oStrictHostKeyChecking=no",
               "-oBatchMode=yes",
               netloc.c_str(),
               "./tailer.bin",
               nullptr);
        exit(EXIT_FAILURE);
    }

    return Ok(std::make_shared<host_tailer>(
        netloc,
        std::move(child),
        std::move(in_pipe.write_end()),
        std::move(out_pipe.read_end()),
        std::move(err_pipe.read_end())
    ));
}

tailer::looper::host_tailer::host_tailer(const std::string &netloc,
                                         auto_pid<process_state::RUNNING> child,
                                         auto_fd to_child,
                                         auto_fd from_child,
                                         auto_fd err_from_child)
    : isc::service<host_tailer>(netloc),
      ht_netloc(netloc),
      ht_local_path(ghc::filesystem::temp_directory_path() /
                    fmt::format("lnav-{}-remotes", getuid()) /
                    netloc),
      ht_error_reader([
          netloc, err = std::move(err_from_child), &eq = this->ht_error_queue]() mutable {
          read_err_pipe(netloc, err, eq);
      }),
      ht_state(connected{
          std::move(child),
          std::move(to_child),
          std::move(from_child),
      })
{
}

void tailer::looper::host_tailer::open_remote_path(const std::string& path)
{
    this->ht_state.match(
        [&](connected& conn) {
            conn.c_desired_paths.insert(path);
            send_packet(conn.ht_to_child.get(),
                        TPT_OPEN_PATH,
                        TPPT_STRING, path.c_str(),
                        TPPT_DONE);
        },
        [&](const disconnected& d) {
            log_warning("tailer(%s): disconnected from host, cannot tail: %s",
                        this->ht_netloc.c_str(),
                        path.c_str());
        }
    );
}

void tailer::looper::host_tailer::loop_body()
{
    if (!this->ht_state.is<connected>()) {
        return;
    }

    auto& conn = this->ht_state.get<connected>();

    pollfd pfds[1];

    pfds[0].fd = conn.ht_from_child.get();
    pfds[0].events = POLLIN;
    pfds[0].revents = 0;

    auto ready_count = poll(pfds, 1, 100);
    if (ready_count > 0) {
        auto read_res = tailer::read_packet(conn.ht_from_child);

        if (read_res.isErr()) {
            log_error("read error: %s", read_res.unwrapErr().c_str());
            exit(EXIT_FAILURE);
        }

        auto packet = read_res.unwrap();
        this->ht_state = packet.match(
            [&](const tailer::packet_eof &te) {
                log_debug("tailer(%s): all done!", this->ht_netloc.c_str());

                auto finished_child = std::move(conn).close();
                if (finished_child.exit_status() != 0 &&
                    !this->ht_error_queue.empty()) {
                    report_error(this->ht_netloc,
                                 this->ht_error_queue.back());
                }

                return state_v{disconnected()};
            },
            [&](const tailer::packet_log &pl) {
                log_debug("tailer(%s): %s\n",
                          this->ht_netloc.c_str(), pl.pl_msg.c_str());
                return std::move(this->ht_state);
            },
            [&](const tailer::packet_error &pe) {
                log_debug("Got an error: %s -- %s", pe.pe_path.c_str(),
                       pe.pe_msg.c_str());

                if (conn.c_desired_paths.count(pe.pe_path)) {
                    report_error(this->get_display_path(pe.pe_path), pe.pe_msg);
                }

                auto remote_path = ghc::filesystem::absolute(
                    ghc::filesystem::path(pe.pe_path)).relative_path();
                auto local_path = this->ht_local_path / remote_path;

                log_debug("removing %s", local_path.c_str());
                this->ht_active_files.erase(local_path);
                ghc::filesystem::remove_all(local_path);
                return std::move(this->ht_state);
            },
            [&](const tailer::packet_offer_block &pob) {
                log_debug("Got an offer: %s  %lld - %lld", pob.pob_path.c_str(),
                       pob.pob_offset, pob.pob_length);

                auto remote_path = ghc::filesystem::absolute(
                    ghc::filesystem::path(pob.pob_path)).relative_path();
                auto local_path = this->ht_local_path / remote_path;
                auto fd = auto_fd(::open(local_path.c_str(), O_RDONLY));

                if (this->ht_active_files.count(local_path) == 0) {
                    this->ht_active_files.insert(local_path);

                    auto custom_name = this->get_display_path(pob.pob_path);
                    isc::to<main_looper &, services::main_t>()
                        .send([local_path, custom_name](auto &mlooper) {
                            auto &active_fc = lnav_data.ld_active_files;
                            auto lpath_str = local_path.string();

                            if (active_fc.fc_file_names.count(lpath_str) > 0) {
                                log_debug("already in fc_file_names");
                                return;
                            }
                            if (active_fc.fc_closed_files.count(custom_name) > 0) {
                                log_debug("in closed");
                                return;
                            }

                            file_collection fc;

                            fc.fc_file_names[lpath_str]
                                .with_filename(custom_name)
                                .with_source(logfile_name_source::REMOTE);
                            update_active_files(fc);
                        });
                }

                if (fd == -1) {
                    log_debug("sending need block");
                    send_packet(conn.ht_to_child.get(),
                                TPT_NEED_BLOCK,
                                TPPT_STRING, pob.pob_path.c_str(),
                                TPPT_DONE);
                    return std::move(this->ht_state);
                }

                struct stat st;

                if (fstat(fd, &st) == -1 || !S_ISREG(st.st_mode)) {
                    ghc::filesystem::remove_all(local_path);
                    send_packet(conn.ht_to_child.get(),
                                TPT_NEED_BLOCK,
                                TPPT_STRING, pob.pob_path.c_str(),
                                TPPT_DONE);
                    return std::move(this->ht_state);
                }
                auto_mem<char> buffer;

                buffer = (char *) malloc(pob.pob_length);
                auto bytes_read = pread(fd, buffer, pob.pob_length,
                                        pob.pob_offset);

                if (bytes_read == pob.pob_length) {
                    tailer::hash_frag thf;
                    calc_sha_256(thf.thf_hash, buffer, bytes_read);

                    if (thf == pob.pob_hash) {
                        send_packet(conn.ht_to_child.get(),
                                    TPT_ACK_BLOCK,
                                    TPPT_STRING, pob.pob_path.c_str(),
                                    TPPT_DONE);
                        return std::move(this->ht_state);
                    }
                } else if (bytes_read == -1) {
                    ghc::filesystem::remove_all(local_path);
                }
                send_packet(conn.ht_to_child.get(),
                            TPT_NEED_BLOCK,
                            TPPT_STRING, pob.pob_path.c_str(),
                            TPPT_DONE);
                return std::move(this->ht_state);
            },
            [&](const tailer::packet_tail_block &ptb) {
                auto remote_path = ghc::filesystem::absolute(
                    ghc::filesystem::path(ptb.ptb_path)).relative_path();
                auto local_path = this->ht_local_path / remote_path;

                log_debug("writing tail to: %s", local_path.c_str());
                ghc::filesystem::create_directories(local_path.parent_path());
                auto fd = auto_fd(
                    ::open(local_path.c_str(), O_WRONLY | O_APPEND | O_CREAT,
                           0600));

                if (fd == -1) {
                    log_error("open: %s", strerror(errno));
                } else {
                    ftruncate(fd, ptb.ptb_offset);
                    pwrite(fd,
                           ptb.ptb_bits.data(), ptb.ptb_bits.size(),
                           ptb.ptb_offset);
                }
                return std::move(this->ht_state);
            }
        );

        if (this->ht_state.is<disconnected>()) {
            this->s_looping = false;
        }
    }
}

std::chrono::milliseconds
tailer::looper::host_tailer::compute_timeout(mstime_t current_time) const
{
    return 0s;
}

void tailer::looper::host_tailer::stopped()
{
    if (!this->ht_state.is<disconnected>()) {
        this->ht_state = disconnected();
    }
    if (this->ht_error_reader.joinable()) {
        this->ht_error_reader.join();
    }
}

std::string tailer::looper::host_tailer::get_display_path(const std::string& remote_path) const
{
    return fmt::format("{}:{}", this->ht_netloc, remote_path);
}

void
tailer::looper::child_finished(std::shared_ptr<service_base> child)
{
    auto child_tailer = std::static_pointer_cast<host_tailer>(child);

    for (auto iter = this->l_remotes.begin();
         iter != this->l_remotes.end();
         ++iter) {
        if (iter->second != child_tailer) {
            continue;
        }

        this->l_remotes.erase(iter);
        return;
    }
}

void tailer::looper::report_error(std::string path, std::string msg)
{
    isc::to<main_looper&, services::main_t>()
        .send([=](auto& mlooper) {
            file_collection fc;

            fc.fc_name_to_errors[path] = msg;
            update_active_files(fc);
        });
}
