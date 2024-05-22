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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <thread>

#include <unistd.h>

#include "base/auto_fd.hh"
#include "base/auto_pid.hh"
#include "config.h"
#include <filesystem>
#include "line_buffer.hh"
#include "tailerpp.hh"

static void
read_err_pipe(auto_fd& err, std::string& eq)
{
    while (true) {
        char buffer[1024];
        auto rc = read(err.get(), buffer, sizeof(buffer));

        if (rc <= 0) {
            break;
        }

        eq.append(buffer, rc);
    }
}

int
main(int argc, char* const* argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <cmd> <path>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    auto in_pipe_res = auto_pipe::for_child_fd(STDIN_FILENO);
    if (in_pipe_res.isErr()) {
        fprintf(stderr,
                "cannot open stdin pipe for child: %s\n",
                in_pipe_res.unwrapErr().c_str());
        exit(EXIT_FAILURE);
    }

    auto out_pipe_res = auto_pipe::for_child_fd(STDOUT_FILENO);
    if (out_pipe_res.isErr()) {
        fprintf(stderr,
                "cannot open stdout pipe for child: %s\n",
                out_pipe_res.unwrapErr().c_str());
        exit(EXIT_FAILURE);
    }

    auto err_pipe_res = auto_pipe::for_child_fd(STDERR_FILENO);
    if (err_pipe_res.isErr()) {
        fprintf(stderr,
                "cannot open stderr pipe for child: %s\n",
                err_pipe_res.unwrapErr().c_str());
        exit(EXIT_FAILURE);
    }

    auto fork_res = lnav::pid::from_fork();
    if (fork_res.isErr()) {
        fprintf(
            stderr, "cannot start tailer: %s\n", fork_res.unwrapErr().c_str());
        exit(EXIT_FAILURE);
    }

    auto in_pipe = in_pipe_res.unwrap();
    auto out_pipe = out_pipe_res.unwrap();
    auto err_pipe = err_pipe_res.unwrap();
    auto child = fork_res.unwrap();

    in_pipe.after_fork(child.in());
    out_pipe.after_fork(child.in());
    err_pipe.after_fork(child.in());

    if (child.in_child()) {
        auto this_exe = std::filesystem::path(argv[0]);
        auto exe_dir = this_exe.parent_path();
        auto tailer_exe = exe_dir / "tailer";

        execlp(tailer_exe.c_str(), tailer_exe.c_str(), "-k", nullptr);
        exit(EXIT_FAILURE);
    }

    std::string error_queue;
    std::thread err_reader(
        [err = std::move(err_pipe.read_end()), &error_queue]() mutable {
            read_err_pipe(err, error_queue);
        });

    auto& to_child = in_pipe.write_end();
    auto& from_child = out_pipe.read_end();
    auto cmd = std::string(argv[1]);

    if (cmd == "open") {
        send_packet(
            to_child.get(), TPT_OPEN_PATH, TPPT_STRING, argv[2], TPPT_DONE);
    } else if (cmd == "preview") {
        send_packet(to_child.get(),
                    TPT_LOAD_PREVIEW,
                    TPPT_STRING,
                    argv[2],
                    TPPT_INT64,
                    int64_t{1234},
                    TPPT_DONE);
    } else if (cmd == "possible") {
        send_packet(
            to_child.get(), TPT_COMPLETE_PATH, TPPT_STRING, argv[2], TPPT_DONE);
    } else {
        fprintf(stderr, "error: unknown command -- %s\n", cmd.c_str());
        exit(EXIT_FAILURE);
    }

    close(to_child.get());

    bool done = false;
    while (!done) {
        auto read_res = tailer::read_packet(from_child);

        if (read_res.isErr()) {
            fprintf(stderr, "read error: %s\n", read_res.unwrapErr().c_str());
            exit(EXIT_FAILURE);
        }

        auto packet = read_res.unwrap();
        packet.match(
            [&](const tailer::packet_eof& te) {
                printf("all done!\n");
                done = true;
            },
            [&](const tailer::packet_announce& pa) {},
            [&](const tailer::packet_log& te) {
                printf("log: %s\n", te.pl_msg.c_str());
            },
            [&](const tailer::packet_error& pe) {
                printf("Got an error: %s -- %s\n",
                       pe.pe_path.c_str(),
                       pe.pe_msg.c_str());

                auto remote_path = std::filesystem::absolute(
                                       std::filesystem::path(pe.pe_path))
                                       .relative_path();

                printf("removing %s\n", remote_path.c_str());
            },
            [&](const tailer::packet_offer_block& pob) {
                printf("Got an offer: %s  %lld - %lld\n",
                       pob.pob_path.c_str(),
                       pob.pob_offset,
                       pob.pob_length);

                auto remote_path = std::filesystem::absolute(
                                       std::filesystem::path(pob.pob_path))
                                       .relative_path();
#if 0
                auto local_path = tmppath / remote_path;
                auto fd = auto_fd(open(local_path.c_str(), O_RDONLY));

                if (fd == -1) {
                    printf("sending need block\n");
                    send_packet(to_child.get(),
                                TPT_NEED_BLOCK,
                                TPPT_STRING, pob.pob_path.c_str(),
                                TPPT_DONE);
                    return;
                }

                struct stat st;

                if (fstat(fd, &st) == -1 || !S_ISREG(st.st_mode)) {
                    std::filesystem::remove_all(local_path);
                    send_packet(to_child.get(),
                                TPT_NEED_BLOCK,
                                TPPT_STRING, pob.pob_path.c_str(),
                                TPPT_DONE);
                    return;
                }
                auto_mem<char> buffer;

                buffer = (char *) malloc(pob.pob_length);
                auto bytes_read = pread(fd, buffer, pob.pob_length,
                                        pob.pob_offset);

                // fprintf(stderr, "debug: bytes_read %ld\n", bytes_read);
                if (bytes_read == pob.pob_length) {
                    tailer::hash_frag thf;
                    calc_sha_256(thf.thf_hash, buffer, bytes_read);

                    if (thf == pob.pob_hash) {
                        send_packet(to_child.get(),
                                    TPT_ACK_BLOCK,
                                    TPPT_STRING, pob.pob_path.c_str(),
                                    TPPT_DONE);
                        return;
                    }
                } else if (bytes_read == -1) {
                    std::filesystem::remove_all(local_path);
                }
                send_packet(to_child.get(),
                            TPT_NEED_BLOCK,
                            TPPT_STRING, pob.pob_path.c_str(),
                            TPPT_DONE);
#endif
            },
            [&](const tailer::packet_tail_block& ptb) {
#if 0
                //printf("got a tail: %s %lld %ld\n", ptb.ptb_path.c_str(),
                //       ptb.ptb_offset, ptb.ptb_bits.size());
                auto remote_path = std::filesystem::absolute(
                    std::filesystem::path(ptb.ptb_path)).relative_path();
                auto local_path = tmppath / remote_path;

                std::filesystem::create_directories(local_path.parent_path());
                auto fd = auto_fd(
                    open(local_path.c_str(), O_WRONLY | O_APPEND | O_CREAT,
                         0600));

                if (fd == -1) {
                    perror("open");
                } else {
                    ftruncate(fd, ptb.ptb_offset);
                    pwrite(fd, ptb.ptb_bits.data(), ptb.ptb_bits.size(), ptb.ptb_offset);
                }
#endif
            },
            [&](const tailer::packet_synced& ps) {

            },
            [&](const tailer::packet_link& pl) {
                printf("link value: %s -> %s\n",
                       pl.pl_path.c_str(),
                       pl.pl_link_value.c_str());
            },
            [&](const tailer::packet_preview_error& ppe) {
                fprintf(stderr,
                        "preview error: %s -- %s\n",
                        ppe.ppe_path.c_str(),
                        ppe.ppe_msg.c_str());
            },
            [&](const tailer::packet_preview_data& ppd) {
                printf("preview of file: %s\n%.*s\n",
                       ppd.ppd_path.c_str(),
                       (int) ppd.ppd_bits.size(),
                       ppd.ppd_bits.data());
            },
            [&](const tailer::packet_possible_path& ppp) {
                printf("possible path: %s\n", ppp.ppp_path.c_str());
            });
    }

    auto finished_child = std::move(child).wait_for_child();
    if (!finished_child.was_normal_exit()) {
        fprintf(stderr, "error: child exited abnormally\n");
    }

    err_reader.join();

    printf("tailer stderr:\n%s", error_queue.c_str());
    fprintf(stderr, "tailer stderr:\n%s", error_queue.c_str());
}
