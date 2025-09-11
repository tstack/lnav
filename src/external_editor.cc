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

#include <filesystem>
#include <optional>
#include <thread>
#include <vector>

#include "external_editor.hh"

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "base/auto_fd.hh"
#include "base/auto_pid.hh"
#include "base/fs_util.hh"
#include "base/injector.hh"
#include "base/lnav_log.hh"
#include "base/result.h"
#include "base/time_util.hh"
#include "external_editor.cfg.hh"
#include "fmt/format.h"
#include "shlex.hh"

namespace lnav::external_editor {

static time64_t
get_config_dir_mtime(const std::filesystem::path& path,
                     const std::filesystem::path& config_dir)
{
    if (config_dir.empty()) {
        return 0;
    }

    auto parent = path.parent_path();
    std::error_code ec;

    while (!parent.empty()) {
        auto config_path = parent / config_dir;
        auto mtime = std::filesystem::last_write_time(config_path, ec);
        if (!ec) {
            auto retval = mtime.time_since_epoch().count();

            log_debug("  found editor config dir: %s (%lld)",
                      config_path.c_str(),
                      retval);
            return retval;
        }
        auto new_parent = parent.parent_path();
        if (new_parent == parent) {
            return 0;
        }
        parent = new_parent;
    }
    return 0;
}

static std::optional<impl>
get_impl(const std::filesystem::path& path)
{
    const auto& cfg = injector::get<const config&>();
    std::vector<std::tuple<time64_t, bool, impl>> candidates;

    log_debug("editor impl count: %d", cfg.c_impls.size());
    for (const auto& [name, impl] : cfg.c_impls) {
        const auto full_cmd = fmt::format(FMT_STRING("{} > /dev/null 2>&1"),
                                          impl.i_test_command);

        log_debug(" testing editor impl %s using: %s",
                  name.c_str(),
                  full_cmd.c_str());
        if (system(full_cmd.c_str()) == 0) {
            log_info("  detected editor: %s", name.c_str());
            auto prefers = impl.i_prefers.pp_value
                ? impl.i_prefers.pp_value->find_in(path.string())
                      .ignore_error()
                      .has_value()
                : false;
            candidates.emplace_back(
                get_config_dir_mtime(path, impl.i_config_dir), prefers, impl);
        }
    }

    std::stable_sort(candidates.begin(),
                     candidates.end(),
                     [](const auto& lhs, const auto& rhs) {
                         const auto& [lmtime, lprefers, limpl] = lhs;
                         const auto& [rmtime, rprefers, rimpl] = rhs;

                         return lmtime > rmtime ||
                             (lmtime == rmtime && lprefers);
                     });

    if (candidates.empty()) {
        return std::nullopt;
    }

    return std::get<2>(candidates.front());
}

Result<void, std::string>
open(std::filesystem::path p, uint32_t line, uint32_t col)
{
    const auto impl = get_impl(p);

    if (!impl) {
        const static std::string MSG = "no external editor found";

        return Err(MSG);
    }

    log_info("external editor command: %s", impl->i_command.c_str());

    auto err_pipe = TRY(auto_pipe::for_child_fd(STDERR_FILENO));
    auto child_pid_res = lnav::pid::from_fork();
    if (child_pid_res.isErr()) {
        return Err(child_pid_res.unwrapErr());
    }

    auto child_pid = child_pid_res.unwrap();
    err_pipe.after_fork(child_pid.in());
    if (child_pid.in_child()) {
        auto open_res
            = lnav::filesystem::open_file("/dev/null", O_RDONLY | O_CLOEXEC);
        open_res.then([](auto_fd&& fd) {
            fd.copy_to(STDIN_FILENO);
            fd.copy_to(STDOUT_FILENO);
        });
        setenv("FILE_PATH", p.c_str(), 1);
        auto line_str = fmt::to_string(line);
        setenv("LINE", line_str.c_str(), 1);
        auto col_str = fmt::to_string(col);
        setenv("COL", col_str.c_str(), 1);

        execlp("sh", "sh", "-c", impl->i_command.c_str(), nullptr);
        _exit(EXIT_FAILURE);
    }
    log_debug("started external editor, pid: %d", child_pid.in());

    std::string error_queue;
    std::thread err_reader(
        [err = std::move(err_pipe.read_end()), &error_queue]() mutable {
            while (true) {
                char buffer[1024];
                auto rc = read(err.get(), buffer, sizeof(buffer));
                if (rc <= 0) {
                    break;
                }

                error_queue.append(buffer, rc);
            }

            log_debug("external editor stderr closed");
        });

    auto finished_child = std::move(child_pid).wait_for_child();
    err_reader.join();
    if (!finished_child.was_normal_exit()) {
        return Err(fmt::format(FMT_STRING("editor failed with signal {}"),
                               finished_child.term_signal()));
    }
    auto exit_status = finished_child.exit_status();
    if (exit_status != 0) {
        return Err(fmt::format(FMT_STRING("editor failed with status {} -- {}"),
                               exit_status,
                               error_queue));
    }

    return Ok();
}

}  // namespace lnav::external_editor
