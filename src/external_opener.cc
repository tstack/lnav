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

#include "external_opener.hh"

#include <fcntl.h>
#include <unistd.h>

#include "base/auto_pid.hh"
#include "base/fs_util.hh"
#include "base/injector.hh"
#include "external_opener.cfg.hh"
#include "fmt/format.h"

namespace lnav::external_opener {

static std::optional<impl>
get_impl()
{
    const auto& cfg = injector::get<const config&>();

    for (const auto& pair : cfg.c_impls) {
        const auto full_cmd = fmt::format(FMT_STRING("{} > /dev/null 2>&1"),
                                          pair.second.i_test_command);

        log_debug("testing opener impl %s using: %s",
                  pair.first.c_str(),
                  full_cmd.c_str());
        if (system(full_cmd.c_str()) == 0) {
            log_info("detected opener: %s", pair.first.c_str());
            return pair.second;
        }
    }

    return std::nullopt;
}

Result<void, std::string>
for_href(const std::string& href)
{
    static const auto IMPL = get_impl();

    if (!IMPL) {
        const static std::string MSG = "no external opener found";

        return Err(MSG);
    }

    auto child_pid_res = lnav::pid::from_fork();
    if (child_pid_res.isErr()) {
        return Err(child_pid_res.unwrapErr());
    }

    auto child_pid = child_pid_res.unwrap();
    if (child_pid.in_child()) {
        auto open_res
            = lnav::filesystem::open_file("/dev/null", O_RDONLY | O_CLOEXEC);
        open_res.then([](auto_fd&& fd) {
            fd.copy_to(STDIN_FILENO);
            fd.copy_to(STDOUT_FILENO);
        });

        execlp(IMPL->i_command.c_str(),
               IMPL->i_command.c_str(),
               href.c_str(),
               nullptr);
        _exit(EXIT_FAILURE);
    }

    return Ok();
}

}  // namespace lnav::external_opener
