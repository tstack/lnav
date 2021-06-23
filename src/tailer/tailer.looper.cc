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

#include <regex>

#include "base/humanize.network.hh"
#include "base/lnav_log.hh"
#include "base/paths.hh"
#include "tailer.looper.hh"
#include "tailer.looper.cfg.hh"
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

    log_info("stderr reader started...");
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
                    log_debug("%.*s", line_str.length(), line_str.data());
                });
            }
        }
    }
}

static void update_tailer_progress(const std::string& netloc, const std::string& msg)
{
    lnav_data.ld_active_files.fc_progress->writeAccess()->
        sp_tailers[netloc].tp_message = msg;
}

static void update_tailer_description(
    const std::string& netloc,
    const std::map<std::string, logfile_open_options>& desired_paths,
    const std::string& remote_uname)
{
    std::vector<std::string> paths;

    for (const auto& des_pair : desired_paths) {
        paths.emplace_back(fmt::format(
            "{}{}", netloc, des_pair.first));
    }
    isc::to<main_looper&, services::main_t>()
        .send([paths, remote_uname](auto& mlooper) {
            auto& fc = lnav_data.ld_active_files;

            for (const auto& path : paths) {
                auto iter = fc.fc_other_files.find(path);

                if (iter == fc.fc_other_files.end()) {
                    continue;
                }

                iter->second.ofd_description = remote_uname;
            }
        });

}

void tailer::looper::loop_body()
{
    auto now = std::chrono::steady_clock::now();
    std::vector<std::string> to_erase;

    for (auto& qpair : this->l_netlocs_to_paths) {
        auto& netloc = qpair.first;
        auto& rpq = qpair.second;

        if (now < rpq.rpq_next_attempt_time) {
            continue;
        }
        if (this->l_remotes.count(netloc) == 0) {
            auto create_res = host_tailer::for_host(netloc);

            if (create_res.isErr()) {
                report_error(netloc, create_res.unwrapErr());
                if (std::any_of(rpq.rpq_new_paths.begin(),
                                rpq.rpq_new_paths.end(),
                                [](const auto& pair) {
                                    return !pair.second.loo_tail;
                                })) {
                    rpq.send_synced_to_main(netloc);
                    to_erase.push_back(netloc);
                } else {
                    rpq.rpq_next_attempt_time = now + HOST_RETRY_DELAY;
                }
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
                      rpq.rpq_new_paths.begin()->first.c_str());
            this->l_remotes[netloc]->send(
                [paths = rpq.rpq_new_paths](auto &ht) {
                    for (const auto &pair : paths) {
                        log_debug("adding path to tailer -- %s",
                                  pair.first.c_str());
                        ht.open_remote_path(pair.first, pair.second);
                    }
                });

            rpq.rpq_existing_paths.insert(rpq.rpq_new_paths.begin(),
                                          rpq.rpq_new_paths.end());
            rpq.rpq_new_paths.clear();
        }
    }

    for (const auto& netloc : to_erase) {
        this->l_netlocs_to_paths.erase(netloc);
    }
}

void tailer::looper::add_remote(const network::path &path,
                                logfile_open_options options)
{
    auto netloc_str = fmt::format("{}", path.home());
    this->l_netlocs_to_paths[netloc_str].rpq_new_paths[path.p_path] =
        std::move(options);
}

void tailer::looper::load_preview(int64_t id, const network::path& path)
{
    auto netloc_str = fmt::format("{}", path.home());
    auto iter = this->l_remotes.find(netloc_str);

    if (iter == this->l_remotes.end()) {
        auto create_res = host_tailer::for_host(netloc_str);

        if (create_res.isErr()) {
            auto msg = create_res.unwrapErr();
            isc::to<main_looper&, services::main_t>()
                .send([id, msg](auto& mlooper) {
                    if (lnav_data.ld_preview_generation != id) {
                        return;
                    }
                    lnav_data.ld_preview_status_source.get_description()
                        .set_cylon(false)
                        .clear();
                    lnav_data.ld_preview_source.clear();
                    lnav_data.ld_bottom_source.grep_error(msg);
                });
            return;
        }

        auto ht = create_res.unwrap();
        this->l_remotes[netloc_str] = ht;
        this->s_children.add_child_service(ht);
    }

    this->l_remotes[netloc_str]->send([id, file_path = path.p_path](auto &ht) {
        ht.load_preview(id, file_path);
    });
}

void tailer::looper::complete_path(const network::path& path)
{
    auto netloc_str = fmt::format("{}", path.home());
    auto iter = this->l_remotes.find(netloc_str);

    if (iter == this->l_remotes.end()) {
        auto create_res = host_tailer::for_host(netloc_str);

        if (create_res.isErr()) {
            return;
        }

        auto ht = create_res.unwrap();
        this->l_remotes[netloc_str] = ht;
        this->s_children.add_child_service(ht);
    }

    this->l_remotes[netloc_str]->send([file_path = path.p_path](auto &ht) {
        ht.complete_path(file_path);
    });
}

static std::vector<std::string>
create_ssh_args_from_config(const std::string& dest)
{
    auto& cfg = injector::get<const tailer::config&>();
    std::vector<std::string> retval;

    retval.emplace_back(cfg.c_ssh_cmd);
    if (!cfg.c_ssh_flags.empty()) {
        if (startswith(cfg.c_ssh_flags, "-")) {
            retval.emplace_back(cfg.c_ssh_flags);
        } else {
            retval.emplace_back(fmt::format("-{}", cfg.c_ssh_flags));
        }
    }
    for (const auto& pair : cfg.c_ssh_options) {
        if (pair.second.empty()) {
            continue;
        }
        retval.emplace_back(fmt::format("-{}", pair.first));
        retval.emplace_back(pair.second);
    }
    for (const auto& pair : cfg.c_ssh_config) {
        if (pair.second.empty()) {
            continue;
        }
        retval.emplace_back(fmt::format(
            "-o{}={}", pair.first, pair.second));
    }
    retval.emplace_back(dest);

    return retval;
}

Result<std::shared_ptr<tailer::looper::host_tailer>, std::string>
tailer::looper::host_tailer::for_host(const std::string& netloc)
{
    log_debug("tailer(%s): transferring tailer to remote", netloc.c_str());

    update_tailer_progress(netloc, "Transferring tailer...");

    auto& cfg = injector::get<const tailer::config&>();
    auto tailer_bin_name = fmt::format("tailer.bin.{}", getpid());

    auto rp = humanize::network::path::from_str(netloc).value();
    auto ssh_dest = rp.p_locality.l_hostname;
    if (rp.p_locality.l_username.has_value()) {
        ssh_dest = fmt::format("{}@{}",
                               rp.p_locality.l_username.value(),
                               rp.p_locality.l_hostname);
    }

    {
        auto in_pipe = TRY(auto_pipe::for_child_fd(STDIN_FILENO));
        auto out_pipe = TRY(auto_pipe::for_child_fd(STDOUT_FILENO));
        auto err_pipe = TRY(auto_pipe::for_child_fd(STDERR_FILENO));
        auto child = TRY(lnav::pid::from_fork());

        in_pipe.after_fork(child.in());
        out_pipe.after_fork(child.in());
        err_pipe.after_fork(child.in());

        if (child.in_child()) {
            auto arg_strs = create_ssh_args_from_config(ssh_dest);
            std::vector<char *> args;

            arg_strs.emplace_back(fmt::format(
                cfg.c_transfer_cmd, tailer_bin_name));

            fmt::print(stderr, "tailer({}): executing -- {}\n",
                       netloc,
                       fmt::join(arg_strs, " "));
            for (const auto& arg : arg_strs) {
                args.push_back((char *) arg.data());
            }
            args.push_back(nullptr);

            execvp(cfg.c_ssh_cmd.c_str(), args.data());
            exit(EXIT_FAILURE);
        }

        std::vector<std::string> error_queue;
        log_debug("starting err reader");
        std::thread err_reader([netloc, err = std::move(err_pipe.read_end()), &error_queue]() mutable {
            log_set_thread_prefix(fmt::format("tailer({})", netloc));
            read_err_pipe(netloc, err, error_queue);
        });

        log_debug("writing to child");
        auto sf = tailer_bin[0].to_string_fragment();
        ssize_t total_bytes = 0;

        while (total_bytes < sf.length()) {
            log_debug("attempting to write %d", sf.length() - total_bytes);
            auto rc = write(in_pipe.write_end(), sf.data(), sf.length() - total_bytes);

            log_debug("wrote %d", rc);
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

    update_tailer_progress(netloc, "Starting tailer...");

    auto in_pipe = TRY(auto_pipe::for_child_fd(STDIN_FILENO));
    auto out_pipe = TRY(auto_pipe::for_child_fd(STDOUT_FILENO));
    auto err_pipe = TRY(auto_pipe::for_child_fd(STDERR_FILENO));
    auto child = TRY(lnav::pid::from_fork());

    in_pipe.after_fork(child.in());
    out_pipe.after_fork(child.in());
    err_pipe.after_fork(child.in());

    if (child.in_child()) {
        auto arg_strs = create_ssh_args_from_config(ssh_dest);
        std::vector<char *> args;

        arg_strs.emplace_back(fmt::format(cfg.c_start_cmd, tailer_bin_name));

        fmt::print(stderr, "tailer({}): executing -- {}\n",
                   netloc,
                   fmt::join(arg_strs, " "));
        for (const auto& arg : arg_strs) {
            args.push_back((char *) arg.data());
        }
        args.push_back(nullptr);

        execvp(cfg.c_ssh_cmd.c_str(), args.data());
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

ghc::filesystem::path tailer::looper::host_tailer::tmp_path()
{
    auto local_path = lnav::paths::workdir() / "remotes";

    ghc::filesystem::create_directories(local_path);
    auto_mem<char> resolved_path;

    resolved_path = realpath(local_path.c_str(), nullptr);
    if (resolved_path.in() == nullptr) {
        return local_path;
    }

    return resolved_path.in();
}

static
std::string scrub_netloc(const std::string& netloc)
{
    const static std::regex TO_SCRUB(R"([^\w\.\@])");

    return std::regex_replace(netloc, TO_SCRUB, "_");
}

tailer::looper::host_tailer::host_tailer(const std::string &netloc,
                                         auto_pid<process_state::RUNNING> child,
                                         auto_fd to_child,
                                         auto_fd from_child,
                                         auto_fd err_from_child)
    : isc::service<host_tailer>(netloc),
      ht_netloc(netloc),
      ht_local_path(tmp_path() / scrub_netloc(netloc)),
      ht_error_reader([
          netloc, err = std::move(err_from_child), &eq = this->ht_error_queue]() mutable {
          read_err_pipe(netloc, err, eq);
      }),
      ht_state(connected{
          std::move(child),
          std::move(to_child),
          std::move(from_child),
          {}
      })
{
}

void tailer::looper::host_tailer::open_remote_path(const std::string& path,
                                                   logfile_open_options loo)
{
    this->ht_state.match(
        [&](connected& conn) {
            conn.c_desired_paths[path] = std::move(loo);
            send_packet(conn.ht_to_child.get(),
                        TPT_OPEN_PATH,
                        TPPT_STRING, path.c_str(),
                        TPPT_DONE);
        },
        [&](const disconnected& d) {
            log_warning("disconnected from host, cannot tail: %s",
                        path.c_str());
        },
        [&](const synced& s) {
            log_warning("synced with host, not tailing: %s", path.c_str());
        }
    );
}

void tailer::looper::host_tailer::load_preview(int64_t id, const std::string &path)
{
    this->ht_state.match(
        [&](connected& conn) {
            send_packet(conn.ht_to_child.get(),
                        TPT_LOAD_PREVIEW,
                        TPPT_STRING, path.c_str(),
                        TPPT_INT64, id,
                        TPPT_DONE);
        },
        [&](const disconnected& d) {
            log_warning("disconnected from host, cannot preview: %s",
                        path.c_str());

            auto msg = fmt::format("error: disconnected from {}", this->ht_netloc);
            isc::to<main_looper&, services::main_t>()
                .send([=](auto& mlooper) {
                    if (lnav_data.ld_preview_generation != id) {
                        return;
                    }
                    lnav_data.ld_preview_status_source.get_description()
                        .set_cylon(false)
                        .set_value(msg);
                });
        },
        [&](const synced& s) {
            require(false);
        }
    );
}

void tailer::looper::host_tailer::complete_path(const std::string &path)
{
    this->ht_state.match(
        [&](connected& conn) {
            send_packet(conn.ht_to_child.get(),
                        TPT_COMPLETE_PATH,
                        TPPT_STRING, path.c_str(),
                        TPPT_DONE);
        },
        [&](const disconnected& d) {
            log_warning("disconnected from host, cannot preview: %s",
                        path.c_str());
        },
        [&](const synced& s) {
            require(false);
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
                log_debug("all done!");

                auto finished_child = std::move(conn).close();
                if (finished_child.exit_status() != 0 &&
                    !this->ht_error_queue.empty()) {
                    report_error(this->ht_netloc,
                                 this->ht_error_queue.back());
                }

                return state_v{disconnected()};
            },
            [&](const tailer::packet_announce &pa) {
                update_tailer_description(
                    this->ht_netloc, conn.c_desired_paths, pa.pa_uname);
                this->ht_uname = pa.pa_uname;
                return std::move(this->ht_state);
            },
            [&](const tailer::packet_log &pl) {
                log_debug("%s\n", pl.pl_msg.c_str());
                return std::move(this->ht_state);
            },
            [&](const tailer::packet_error &pe) {
                log_debug("Got an error: %s -- %s", pe.pe_path.c_str(),
                       pe.pe_msg.c_str());

                lnav_data.ld_active_files.fc_progress->writeAccess()->
                    sp_tailers.erase(this->ht_netloc);

                auto desired_iter = conn.c_desired_paths.find(pe.pe_path);
                if (desired_iter != conn.c_desired_paths.end()) {
                    report_error(this->get_display_path(pe.pe_path), pe.pe_msg);
                    if (!desired_iter->second.loo_tail) {
                        conn.c_desired_paths.erase(desired_iter);
                    }
                }
                else {
                    auto child_iter = conn.c_child_paths.find(pe.pe_path);

                    if (child_iter != conn.c_child_paths.end() &&
                        !child_iter->second.loo_tail) {
                        conn.c_child_paths.erase(child_iter);
                    }
                }

                auto remote_path = ghc::filesystem::absolute(
                    ghc::filesystem::path(pe.pe_path)).relative_path();
                auto local_path = this->ht_local_path / remote_path;

                log_debug("removing %s", local_path.c_str());
                this->ht_active_files.erase(local_path);
                ghc::filesystem::remove_all(local_path);

                if (conn.c_desired_paths.empty() &&
                    conn.c_child_paths.empty()) {
                    log_info("tailer(%s): all desired paths synced",
                             this->ht_netloc.c_str());
                    return state_v{synced{}};
                }

                return std::move(this->ht_state);
            },
            [&](const tailer::packet_offer_block &pob) {
                log_debug("Got an offer: %s  %lld - %lld", pob.pob_path.c_str(),
                       pob.pob_offset, pob.pob_length);

                logfile_open_options loo;
                if (pob.pob_path == pob.pob_root_path) {
                    auto root_iter = conn.c_desired_paths.find(pob.pob_path);

                    if (root_iter == conn.c_desired_paths.end()) {
                        log_warning("ignoring unknown root: %s",
                                    pob.pob_root_path.c_str());
                        return std::move(this->ht_state);
                    }

                    loo = std::move(root_iter->second);
                } else {
                    auto child_iter = conn.c_child_paths.find(pob.pob_path);
                    if (child_iter == conn.c_child_paths.end()) {
                        auto root_iter = conn.c_desired_paths.find(pob.pob_root_path);

                        if (root_iter == conn.c_desired_paths.end()) {
                            log_warning("ignoring child of unknown root: %s",
                                        pob.pob_root_path.c_str());
                            return std::move(this->ht_state);
                        }

                        conn.c_child_paths[pob.pob_path] = std::move(root_iter->second);
                        child_iter = conn.c_child_paths.find(pob.pob_path);
                    }

                    loo = std::move(child_iter->second);
                }

                update_tailer_description(
                    this->ht_netloc, conn.c_desired_paths, this->ht_uname);

                auto remote_path = ghc::filesystem::absolute(
                    ghc::filesystem::path(pob.pob_path)).relative_path();
                auto local_path = this->ht_local_path / remote_path;
                auto fd = auto_fd(::open(local_path.c_str(), O_RDONLY));

                if (this->ht_active_files.count(local_path) == 0) {
                    this->ht_active_files.insert(local_path);

                    auto custom_name = this->get_display_path(pob.pob_path);
                    isc::to<main_looper &, services::main_t>()
                        .send([local_path, custom_name, loo, netloc = this->ht_netloc](auto &mlooper) {
                            auto &active_fc = lnav_data.ld_active_files;
                            auto lpath_str = local_path.string();

                            {
                                safe::WriteAccess<safe_scan_progress> sp(
                                    *active_fc.fc_progress);

                                sp->sp_tailers.erase(netloc);
                            }
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
                                .with_source(logfile_name_source::REMOTE)
                                .with_tail(loo.loo_tail)
                                .with_non_utf_visibility(false)
                                .with_visible_size_limit(256 * 1024);
                            update_active_files(fc);
                        });
                }

                if (fd == -1) {
                    log_debug("file not found, sending need block");
                    send_packet(conn.ht_to_child.get(),
                                TPT_NEED_BLOCK,
                                TPPT_STRING, pob.pob_path.c_str(),
                                TPPT_DONE);
                    return std::move(this->ht_state);
                }

                struct stat st;

                if (fstat(fd, &st) == -1 || !S_ISREG(st.st_mode)) {
                    log_debug("path changed, sending need block");
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
                        log_debug("local file block is same, sending ack");
                        send_packet(conn.ht_to_child.get(),
                                    TPT_ACK_BLOCK,
                                    TPPT_STRING, pob.pob_path.c_str(),
                                    TPPT_DONE);
                        return std::move(this->ht_state);
                    }
                    log_debug("local file is different, sending need block");
                } else if (bytes_read == -1) {
                    log_debug("unable to read file, sending need block -- %s",
                              strerror(errno));
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

                log_debug("writing tail to: %lld/%ld %s",
                          ptb.ptb_offset,
                          ptb.ptb_bits.size(),
                          local_path.c_str());
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
                    auto mtime = ghc::filesystem::file_time_type{
                        std::chrono::seconds{ptb.ptb_mtime}};
                    // XXX This isn't atomic with the write...
                    ghc::filesystem::last_write_time(local_path, mtime);
                }
                return std::move(this->ht_state);
            },
            [&](const tailer::packet_synced &ps) {
                if (ps.ps_root_path == ps.ps_path) {
                    auto iter = conn.c_desired_paths.find(ps.ps_path);

                    if (iter != conn.c_desired_paths.end()) {
                        if (!iter->second.loo_tail) {
                            log_info("synced desired path: %s",
                                     iter->first.c_str());
                            conn.c_desired_paths.erase(iter);
                        }

                    }
                } else {
                    auto iter = conn.c_child_paths.find(ps.ps_path);

                    if (iter != conn.c_child_paths.end()) {
                        if (!iter->second.loo_tail) {
                            log_info("synced child path: %s",
                                     iter->first.c_str());
                            conn.c_child_paths.erase(iter);
                        }
                    }
                }

                if (conn.c_desired_paths.empty() &&
                    conn.c_child_paths.empty()) {
                    log_info("tailer(%s): all desired paths synced",
                             this->ht_netloc.c_str());
                    return state_v{synced{}};
                }

                return std::move(this->ht_state);
            },
            [&](const tailer::packet_link &pl) {
                auto remote_path = ghc::filesystem::absolute(
                    ghc::filesystem::path(pl.pl_path)).relative_path();
                auto local_path = this->ht_local_path / remote_path;
                auto remote_link_path = ghc::filesystem::path(pl.pl_link_value);
                std::string link_path;

                if (remote_path.is_absolute()) {
                    auto local_link_path =
                        this->ht_local_path / remote_link_path.relative_path();

                    link_path = local_link_path.string();
                } else {
                    link_path = remote_link_path.string();
                }

                log_debug("symlinking %s -> %s",
                          local_path.c_str(),
                          link_path.c_str());
                ghc::filesystem::create_directories(local_path.parent_path());
                ghc::filesystem::remove_all(local_path);
                if (symlink(link_path.c_str(), local_path.c_str()) < 0) {
                    log_error("symlink failed: %s", strerror(errno));
                }

                return std::move(this->ht_state);
            },
            [&](const tailer::packet_preview_error &ppe) {
                isc::to<main_looper&, services::main_t>()
                    .send([ppe](auto& mlooper) {
                        if (lnav_data.ld_preview_generation != ppe.ppe_id) {
                            log_debug("preview ID mismatch: %lld != %lld",
                                      lnav_data.ld_preview_generation,
                                      ppe.ppe_id);
                            return;
                        }
                        lnav_data.ld_preview_status_source.get_description()
                            .set_cylon(false)
                            .clear();
                        lnav_data.ld_preview_source.clear();
                        lnav_data.ld_bottom_source.grep_error(ppe.ppe_msg);
                    });

                return std::move(this->ht_state);
            },
            [&](const tailer::packet_preview_data &ppd) {
                isc::to<main_looper&, services::main_t>()
                    .send([netloc = this->ht_netloc, ppd](auto& mlooper) {
                        if (lnav_data.ld_preview_generation != ppd.ppd_id) {
                            log_debug("preview ID mismatch: %lld != %lld",
                                      lnav_data.ld_preview_generation,
                                      ppd.ppd_id);
                            return;
                        }
                        std::string str(ppd.ppd_bits.begin(),
                                        ppd.ppd_bits.end());
                        lnav_data.ld_preview_status_source.get_description()
                            .set_cylon(false)
                            .set_value("For file: %s:%s",
                                       netloc.c_str(),
                                       ppd.ppd_path.c_str());
                        lnav_data.ld_preview_source
                            .replace_with(str)
                            .set_text_format(detect_text_format(str));
                    });
                return std::move(this->ht_state);
            },
            [&](const tailer::packet_possible_path &ppp) {
                log_debug("possible path: %s", ppp.ppp_path.c_str());
                auto full_path = fmt::format("{}{}",
                                             this->ht_netloc,
                                             ppp.ppp_path);

                isc::to<main_looper&, services::main_t>()
                    .send([full_path](auto& mlooper) {
                        lnav_data.ld_rl_view->add_possibility(
                            LNM_COMMAND, "remote-path", full_path);
                    });
                return std::move(this->ht_state);
            }
        );

        if (!this->ht_state.is<connected>()) {
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
    if (this->ht_state.is<connected>()) {
        this->ht_state = disconnected();
    }
    if (this->ht_error_reader.joinable()) {
        this->ht_error_reader.join();
    }
}

std::string tailer::looper::host_tailer::get_display_path(const std::string& remote_path) const
{
    return fmt::format("{}{}", this->ht_netloc, remote_path);
}

void *tailer::looper::host_tailer::run()
{
    log_set_thread_prefix(fmt::format("tailer({})", this->ht_netloc));

    return service_base::run();
}

auto_pid<process_state::FINISHED>
tailer::looper::host_tailer::connected::close() &&
{
    this->ht_to_child.reset();
    this->ht_from_child.reset();

    return std::move(this->ht_child).wait_for_child();
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

        if (child_tailer->is_synced()) {
            log_info("synced with netloc '%s', removing", iter->first.c_str());
            auto netloc_iter = this->l_netlocs_to_paths.find(iter->first);

            if (netloc_iter != this->l_netlocs_to_paths.end()) {
                netloc_iter->second.send_synced_to_main(netloc_iter->first);
                this->l_netlocs_to_paths.erase(netloc_iter);
            }
        }
        lnav_data.ld_active_files.fc_progress->writeAccess()->
            sp_tailers.erase(iter->first);
        this->l_remotes.erase(iter);
        return;
    }
}

void
tailer::looper::remote_path_queue::send_synced_to_main(const std::string& netloc)
{
    std::set<std::string> synced_files;

    for (const auto& pair : this->rpq_new_paths) {
        if (!pair.second.loo_tail) {
            synced_files.emplace(fmt::format("{}{}", netloc, pair.first));
        }
    }
    for (const auto& pair : this->rpq_existing_paths) {
        if (!pair.second.loo_tail) {
            synced_files.emplace(fmt::format("{}{}", netloc, pair.first));
        }
    }

    isc::to<main_looper&, services::main_t>()
        .send([file_set = std::move(synced_files)](auto& mlooper) {
            file_collection fc;

            fc.fc_synced_files = file_set;
            update_active_files(fc);
        });
}

void tailer::looper::report_error(std::string path, std::string msg)
{
    isc::to<main_looper&, services::main_t>()
        .send([=](auto& mlooper) {
            file_collection fc;

            fc.fc_name_to_errors[path] = msg;
            update_active_files(fc);
            lnav_data.ld_active_files.fc_progress->writeAccess()->
                sp_tailers.erase(path);
        });
}
