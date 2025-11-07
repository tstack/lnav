/**
 * Copyright (c) 2025, Timothy Stack
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

#include <string_view>

#include "lnav.exec-phase.hh"

#include "base/lnav_log.hh"

using namespace std::string_view_literals;

namespace lnav {

void
exec_phase::completed(phase_t current_phase)
{
    const auto name = get_phase_name(current_phase);
    log_debug("phase completed: %.*s", (int) name.size(), name.data());

    require(this->ep_value == current_phase);

    switch (this->ep_value) {
        case phase_t::init:
            this->ep_value = phase_t::scan;
            break;
        case phase_t::scan:
            this->ep_value = phase_t::build;
            break;
        case phase_t::build:
            this->ep_value = phase_t::commands;
            break;
        case phase_t::commands:
            this->ep_value = phase_t::load_session;
            break;
        case phase_t::load_session:
            this->ep_value = phase_t::interactive;
            break;
        case phase_t::interactive:
            this->ep_value = phase_t::done;
            break;
        case phase_t::done:
            ensure(0);
            break;
    }
}

std::string_view
exec_phase::get_phase_name(phase_t phase)
{
    switch (phase) {
        case phase_t::init:
            return "init"sv;
        case phase_t::scan:
            return "scan"sv;
        case phase_t::build:
            return "build"sv;
        case phase_t::commands:
            return "commands"sv;
        case phase_t::load_session:
            return "load_session"sv;
        case phase_t::interactive:
            return "interactive"sv;
        case phase_t::done:
            return "done"sv;
    }

    ensure(0);
}

}  // namespace lnav
