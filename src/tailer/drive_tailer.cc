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

#include <unistd.h>
#include "config.h"

#include "ghc/filesystem.hpp"
#include "auto_pid.hh"
#include "auto_fd.hh"
#include "tailerpp.hh"

int main(int argc, char *const *argv)
{
    auto tmppath = ghc::filesystem::temp_directory_path() / "drive_tailer";

    ghc::filesystem::remove_all(tmppath);
    ghc::filesystem::create_directories(tmppath);

    auto_pipe in_pipe(STDIN_FILENO);
    auto_pipe out_pipe(STDOUT_FILENO);

    in_pipe.open();
    out_pipe.open();

    auto child = auto_pid(fork());

    if (child.failed()) {
        exit(EXIT_FAILURE);
    }

    in_pipe.after_fork(child.in());
    out_pipe.after_fork(child.in());

    if (child.in_child()) {
        auto this_exe = ghc::filesystem::path(argv[0]);
        auto exe_dir = this_exe.parent_path();
        auto tailer_exe = exe_dir / "tailer";

        execvp(tailer_exe.c_str(), argv);
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "info: child pid %d\n", child.in());

    auto &to_child = in_pipe.write_end();
    auto &from_child = out_pipe.read_end();

    for (int lpc = 1; lpc < argc; lpc++) {
        send_packet(to_child,
                    TPT_OPEN_PATH,
                    TPPT_STRING, argv[lpc],
                    TPPT_DONE);
    }

    bool done = false;
    while (!done) {
        auto read_res = tailer::read_packet(from_child);

        if (read_res.isErr()) {
            fprintf(stderr, "read error: %s\n", read_res.unwrapErr().c_str());
            exit(EXIT_FAILURE);
        }

        auto packet = read_res.unwrap();
        packet.match(
            [&](const tailer::packet_eof &te) {
                printf("all done!\n");
                done = true;
            },
            [&](const tailer::packet_error &te) {
                printf("Got an error: %s -- %s\n", te.te_path.c_str(),
                       te.te_msg.c_str());

                auto remote_path = ghc::filesystem::absolute(
                    ghc::filesystem::path(te.te_path)).relative_path();
                auto local_path = tmppath / remote_path;

                printf("removing %s\n", local_path.c_str());
                ghc::filesystem::remove_all(local_path);
            },
            [&](const tailer::packet_offer_block &tob) {
                printf("Got an offer: %s  %lld - %lld\n", tob.tob_path.c_str(),
                       tob.tob_offset, tob.tob_length);

                auto remote_path = ghc::filesystem::absolute(
                    ghc::filesystem::path(tob.tob_path)).relative_path();
                auto local_path = tmppath / remote_path;
                auto fd = auto_fd(open(local_path.c_str(), O_RDONLY));

                fprintf(stderr, "offer fd %d\n", fd.get());
                if (fd == -1) {
                    printf("sending need block\n");
                    send_packet(to_child.get(),
                                TPT_NEED_BLOCK,
                                TPPT_STRING, tob.tob_path.c_str(),
                                TPPT_DONE);
                    return;
                }

                auto_mem<char> buffer;

                buffer = (char *) malloc(tob.tob_length);
                auto bytes_read = pread(fd, buffer, tob.tob_length,
                                        tob.tob_offset);

                fprintf(stderr, "debug: bytes_read %ld\n", bytes_read);
                if (bytes_read == tob.tob_length) {
                    tailer::hash_frag thf;
                    calc_sha_256(thf.thf_hash, buffer, bytes_read);

                    if (thf == tob.tob_hash) {
                        send_packet(to_child.get(),
                                    TPT_ACK_BLOCK,
                                    TPPT_STRING, tob.tob_path.c_str(),
                                    TPPT_DONE);
                        return;
                    }
                } else if (bytes_read == -1) {
                    ghc::filesystem::remove(local_path);
                }
                send_packet(to_child.get(),
                            TPT_NEED_BLOCK,
                            TPPT_STRING, tob.tob_path.c_str(),
                            TPPT_DONE);
            },
            [&](const tailer::packet_tail_block &ttb) {
                //printf("got a tail: %s %ld\n", ttb.ttb_path.c_str(),
                //       ttb.ttb_bits.size());
                auto remote_path = ghc::filesystem::absolute(
                    ghc::filesystem::path(ttb.ttb_path)).relative_path();
                auto local_path = tmppath / remote_path;

                ghc::filesystem::create_directories(local_path.parent_path());
                auto fd = auto_fd(
                    open(local_path.c_str(), O_WRONLY | O_APPEND | O_CREAT,
                         0600));

                if (fd == -1) {
                    perror("open");
                } else {
                    write(fd, ttb.ttb_bits.data(), ttb.ttb_bits.size());
                }
            }
        );
    }

    child.wait_for_child();
    if (!child.was_normal_exit()) {
        fprintf(stderr, "error: child exited abnormally\n");
    }
}
