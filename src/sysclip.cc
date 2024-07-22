/**
 * Copyright (c) 2014, Timothy Stack
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
 * @file sysclip.cc
 */

#include "sysclip.hh"

#include <stdio.h>
#include <unistd.h>

#include "base/injector.hh"
#include "base/lnav_log.hh"
#include "config.h"
#include "fmt/format.h"
#include "libbase64.h"
#include "sysclip.cfg.hh"

#define ANSI_OSC "\x1b]"

namespace sysclip {

static std::optional<clipboard>
get_commands()
{
    const auto& cfg = injector::get<const config&>();

    for (const auto& pair : cfg.c_clipboard_impls) {
        const auto full_cmd = fmt::format(FMT_STRING("{} > /dev/null 2>&1"),
                                          pair.second.c_test_command);

        log_debug("testing clipboard impl %s using: %s",
                  pair.first.c_str(),
                  full_cmd.c_str());
        if (system(full_cmd.c_str()) == 0) {
            log_info("detected clipboard: %s", pair.first.c_str());
            return pair.second;
        }
    }

    return std::nullopt;
}

static int
osc52_close(FILE* file)
{
    static const char ANSI_OSC_COPY_TO_CLIP[] = ANSI_OSC "52;c;";

    log_debug("writing %d bytes of clipboard data using OSC 52", ftell(file));
    write(STDOUT_FILENO, ANSI_OSC_COPY_TO_CLIP, strlen(ANSI_OSC_COPY_TO_CLIP));

    base64_state b64state{};
    base64_stream_encode_init(&b64state, 0);

    fseek(file, 0, SEEK_SET);

    auto done = false;
    while (!done) {
        char in_buffer[1024];
        char out_buffer[2048];
        size_t outlen = 0;

        auto rc = fread(in_buffer, 1, sizeof(in_buffer), file);
        if (rc <= 0) {
            base64_stream_encode_final(&b64state, out_buffer, &outlen);
            write(STDOUT_FILENO, out_buffer, outlen);
            break;
        }

        base64_stream_encode(&b64state, in_buffer, rc, out_buffer, &outlen);
        write(STDOUT_FILENO, out_buffer, outlen);
    }

    write(STDOUT_FILENO, "\a", 1);

    fclose(file);

    return 0;
}

/* XXX For one, this code is kinda crappy.  For two, we should probably link
 * directly with X so we don't need to have xclip installed and it'll work if
 * we're ssh'd into a box.
 */
Result<auto_mem<FILE>, std::string>
open(type_t type, op_t op)
{
    const char* mode = op == op_t::WRITE ? "w" : "r";
    static const auto clip_opt = sysclip::get_commands();

    std::string cmd;

    if (clip_opt) {
        cmd = clip_opt.value().select(type).select(op);
        if (cmd.empty()) {
            log_info("configured clipboard does not support type/op");
        }
    } else {
        log_info("unable to detect clipboard");
    }

    if (cmd.empty()) {
        log_info("  ... falling back to OSC 52");
        auto_mem<FILE> retval(osc52_close);

        retval = tmpfile();
        if (retval.in() == nullptr) {
            return Err(
                fmt::format(FMT_STRING("unable to open temporary file: {}"),
                            strerror(errno)));
        }

        return Ok(std::move(retval));
    }

    switch (op) {
        case op_t::WRITE:
            cmd = fmt::format(FMT_STRING("{} > /dev/null 2>&1"), cmd);
            break;
        case op_t::READ:
            cmd = fmt::format(FMT_STRING("{} < /dev/null 2>/dev/null"), cmd);
            break;
    }

    auto_mem<FILE> retval(pclose);

    log_debug("trying detected clipboard command: %s", cmd.c_str());
    retval = popen(cmd.c_str(), mode);
    if (retval.in() == nullptr) {
        return Err(fmt::format(FMT_STRING("failed to open clipboard: {} -- {}"),
                               cmd,
                               strerror(errno)));
    }

    return Ok(std::move(retval));
}

}  // namespace sysclip
