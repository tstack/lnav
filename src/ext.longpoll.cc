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

#include <condition_variable>
#include <list>

#include "ext.longpoll.hh"

#include "config.h"
#include "lnav_rs_ext.cxx.hh"
#include "safe/safe.h"

using namespace std::chrono_literals;

#ifdef HAVE_RUST_DEPS
namespace lnav_rs_ext {

struct pollers {
    std::list<PollInput> p_pollers;
    lnav::ext::view_states p_latest_state;
    std::condition_variable p_condvar;
};

using safe_pollers_t = safe::Safe<pollers>;

safe_pollers_t POLLERS;

PollInput
longpoll(const PollInput& pi)
{
    auto p = POLLERS.writeAccess<std::unique_lock>();

    if (pi.view_states.log == p->p_latest_state.vs_log
        && pi.view_states.text == p->p_latest_state.vs_text)
    {
        p->p_pollers.emplace_front(pi);
        auto iter = p->p_pollers.begin();

        p->p_condvar.wait_for(p.lock, 10s);
        p->p_pollers.erase(iter);
    }

    return PollInput{
        0,
        ViewStates{
            ::rust::String::lossy(p->p_latest_state.vs_log),
            ::rust::String::lossy(p->p_latest_state.vs_text),
        },
    };
}

}  // namespace lnav_rs_ext
#endif

namespace lnav::ext {

void
notify_pollers(const view_states& vs)
{
#ifdef HAVE_RUST_DEPS
    auto p = lnav_rs_ext::POLLERS.writeAccess<std::unique_lock>();

    for (const auto& poller : p->p_pollers) {
        if (poller.view_states.log != vs.vs_log
            || poller.view_states.text != vs.vs_text)
        {
            p->p_condvar.notify_all();
            break;
        }
    }
    p->p_latest_state = vs;
#endif
}

}  // namespace lnav::ext
