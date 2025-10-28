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

#ifndef lnav_exec_phase_hh
#define lnav_exec_phase_hh

#include <cstdint>
#include <string_view>

#include "fmt/core.h"

namespace lnav {

enum class phase_t : uint8_t {
    init,
    scan,
    build,
    commands,
    load_session,
    interactive,
    done,
};

struct exec_phase {
    [[nodiscard]] bool scanning() const
    {
        return this->ep_value == phase_t::scan;
    }

    [[nodiscard]] bool building_index() const
    {
        return this->ep_value == phase_t::build;
    }

    [[nodiscard]] bool running_commands() const
    {
        return this->ep_value == phase_t::commands;
    }

    [[nodiscard]] bool loading_session() const
    {
        return this->ep_value == phase_t::load_session;
    }

    [[nodiscard]] bool allow_user_input() const
    {
        return this->ep_value >= phase_t::build
            && this->ep_value <= phase_t::interactive;
    }

    [[nodiscard]] bool interactive() const
    {
        return this->ep_value == phase_t::interactive;
    }

    [[nodiscard]] bool scan_completed() const
    {
        return this->ep_value > phase_t::scan;
    }

    [[nodiscard]] bool build_completed() const
    {
        return this->ep_value > phase_t::build;
    }

    [[nodiscard]] bool spinning_up() const
    {
        return this->ep_value < phase_t::interactive;
    }

    void completed(phase_t current_phase);

    static std::string_view get_phase_name(phase_t phase);

    phase_t ep_value{phase_t::init};
};

}  // namespace lnav

#endif
