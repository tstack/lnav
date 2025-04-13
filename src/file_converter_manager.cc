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

#include <memory>
#include <thread>
#include <vector>

#include "file_converter_manager.hh"

#include <unistd.h>

#include "base/fs_util.hh"
#include "base/injector.hh"
#include "base/paths.hh"
#include "config.h"
#include "line_buffer.hh"
#include "piper.looper.cfg.hh"

namespace file_converter_manager {

static const std::filesystem::path&
cache_dir()
{
    static auto INSTANCE = lnav::paths::workdir() / "conversion";

    return INSTANCE;
}

Result<convert_result, std::string>
convert(const external_file_format& eff, const std::string& filename)
{
    log_info("attempting to convert file -- %s", filename.c_str());

    std::filesystem::create_directories(cache_dir());
    auto outfile = TRY(lnav::filesystem::open_temp_file(
        cache_dir()
        / fmt::format(FMT_STRING("{}.XXXXXX"), eff.eff_format_name)));
    auto err_pipe = TRY(auto_pipe::for_child_fd(STDERR_FILENO));
    auto child = TRY(lnav::pid::from_fork());

    err_pipe.after_fork(child.in());
    if (child.in_child()) {
        auto dev_null = open("/dev/null", O_RDONLY | O_CLOEXEC);

        dup2(dev_null, STDIN_FILENO);
        dup2(outfile.second.get(), STDOUT_FILENO);
        outfile.second.reset();

        auto new_path = lnav::filesystem::build_path({
            eff.eff_source_path.parent_path(),
            lnav::paths::dotlnav() / "formats/default",
        });
        setenv("PATH", new_path.c_str(), 1);
        auto format_str = eff.eff_format_name;

        const char* args[] = {
            eff.eff_converter.c_str(),
            format_str.c_str(),
            filename.c_str(),
            nullptr,
        };

        execvp(eff.eff_converter.c_str(), (char**) args);
        if (errno == ENOENT) {
            fprintf(stderr,
                    "cannot find converter: %s\n",
                    eff.eff_converter.c_str());
        } else {
            fprintf(stderr,
                    "failed to execute converter: %s -- %s\n",
                    eff.eff_converter.c_str(),
                    strerror(errno));
        }
        _exit(EXIT_FAILURE);
    }

    auto error_queue = std::make_shared<std::vector<std::string>>();
    std::thread err_reader([err = std::move(err_pipe.read_end()),
                            converter = eff.eff_converter,
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
                        .then([converter, error_queue, child_pid](auto sbr) {
                            auto line_str = string_fragment(
                                                sbr.get_data(), 0, sbr.length())
                                                .trim("\n");
                            if (error_queue->size() < 5) {
                                error_queue->emplace_back(line_str.to_string());
                            }

                            log_debug("%s[%d]: %.*s",
                                      converter.c_str(),
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
        outfile.first,
        error_queue,
    });
}

void
cleanup()
{
    (void) std::async(std::launch::async, []() {
        const auto& cfg = injector::get<const lnav::piper::config&>();
        auto now = std::filesystem::file_time_type::clock::now();
        auto cache_path = cache_dir();
        std::vector<std::filesystem::path> to_remove;

        for (const auto& entry :
             std::filesystem::directory_iterator(cache_path))
        {
            auto mtime = std::filesystem::last_write_time(entry.path());
            auto exp_time = mtime + cfg.c_ttl;
            if (now < exp_time) {
                continue;
            }

            to_remove.emplace_back(entry);
        }

        for (auto& entry : to_remove) {
            log_debug("removing conversion: %s", entry.c_str());
            std::filesystem::remove_all(entry);
        }
    });
}

}  // namespace file_converter_manager
