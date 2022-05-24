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
 *
 * @file pcap_manager.cc
 */

#include <memory>
#include <thread>
#include <vector>

#include "pcap_manager.hh"

#include <unistd.h>

#include "base/fs_util.hh"
#include "config.h"
#include "line_buffer.hh"

namespace pcap_manager {

Result<convert_result, std::string>
convert(const std::string& filename)
{
    log_info("attempting to convert pcap file -- %s", filename.c_str());

    auto outfile = TRY(lnav::filesystem::open_temp_file(
        ghc::filesystem::temp_directory_path() / "lnav.pcap.XXXXXX"));
    ghc::filesystem::remove(outfile.first);
    auto err_pipe = TRY(auto_pipe::for_child_fd(STDERR_FILENO));
    auto child = TRY(lnav::pid::from_fork());

    err_pipe.after_fork(child.in());
    if (child.in_child()) {
        auto dev_null = open("/dev/null", O_RDONLY);

        dup2(dev_null, STDIN_FILENO);
        dup2(outfile.second.release(), STDOUT_FILENO);
        setenv("TZ", "UTC", 1);

        const char* args[] = {
            "tshark",
            "-T",
            "ek",
            "-P",
            "-V",
            "-t",
            "ad",
            "-r",
            filename.c_str(),
            nullptr,
        };

        execvp("tshark", (char**) args);
        if (errno == ENOENT) {
            fprintf(stderr,
                    "pcap support requires 'tshark' v3+ to be installed\n");
        } else {
            fprintf(
                stderr, "failed to execute 'tshark' -- %s\n", strerror(errno));
        }
        _exit(EXIT_FAILURE);
    }

    auto error_queue = std::make_shared<std::vector<std::string>>();
    std::thread err_reader([err = std::move(err_pipe.read_end()),
                            error_queue,
                            child_pid = child.in()]() mutable {
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
                    lb.read_range(li.li_file_range)
                        .then([error_queue, child_pid](auto sbr) {
                            auto line_str = string_fragment(
                                                sbr.get_data(), 0, sbr.length())
                                                .trim("\n");
                            if (error_queue->size() < 5) {
                                error_queue->emplace_back(line_str.to_string());
                            }

                            log_debug("pcap[%d]: %.*s",
                                      child_pid,
                                      line_str.length(),
                                      line_str.data());
                        });
                }
            }
        }
    });
    err_reader.detach();

    log_info("started tshark %d to process file", child.in());

    return Ok(convert_result{
        std::move(child),
        std::move(outfile.second),
        error_queue,
    });
}

}  // namespace pcap_manager
