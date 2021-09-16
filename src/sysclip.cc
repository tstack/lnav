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
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
* ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* @file sysclip.cc
*/

#include "config.h"

#include <stdio.h>

#include "base/injector.hh"
#include "base/lnav_log.hh"
#include "fmt/format.h"
#include "sysclip.hh"
#include "sysclip.cfg.hh"

namespace sysclip {

static nonstd::optional<clipboard> get_commands()
{
    auto& cfg = injector::get<const config&>();

    for (const auto& pair : cfg.c_clipboard_impls) {
        const auto full_cmd = fmt::format("{} > /dev/null 2>&1",
                                          pair.second.c_test_command);

        log_debug("testing clipboard impl %s using: %s",
                  pair.first.c_str(), full_cmd.c_str());
        if (system(full_cmd.c_str()) == 0) {
            log_info("detected clipboard: %s", pair.first.c_str());
            return pair.second;
        }
    }

    return nonstd::nullopt;
}

/* XXX For one, this code is kinda crappy.  For two, we should probably link
 * directly with X so we don't need to have xclip installed and it'll work if
 * we're ssh'd into a box.
 */
FILE *open(type_t type, op_t op)
{
    const char *mode = op == op_t::WRITE ? "w" : "r";
    static const auto clip_opt = sysclip::get_commands();

    if (!clip_opt) {
        log_error("unable to detect clipboard implementation");
        return nullptr;
    }

    auto cmd = clip_opt.value().select(type).select(op);

    if (cmd.empty()) {
        log_error("clipboard does not support type/op");
        return nullptr;
    }

    switch (op) {
        case op_t::WRITE:
            cmd = fmt::format("{} > /dev/null 2>&1", cmd);
            break;
        case op_t::READ:
            cmd = fmt::format("{} < /dev/null 2>/dev/null", cmd);
            break;
    }

    return popen(cmd.c_str(), mode);
}

}
