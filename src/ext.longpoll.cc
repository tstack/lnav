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

#include "base/injector.hh"
#include "base/itertools.enumerate.hh"
#include "base/progress.hh"
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

PollResult
longpoll(const PollInput& pi)
{
    auto pi_retval = PollInput{
        0,
    };
    auto timeout = 10000ms;

    {
        auto& bts = lnav::progress_tracker::get_tasks();

        for (const auto& bt : **bts.readAccess()) {
            auto tp = bt();
            if (tp.tp_status == lnav::progress_status_t::working) {
                timeout = 333ms;
                break;
            }
        }
    }

    {
        auto p = POLLERS.writeAccess<std::unique_lock>();
        auto views_are_same = pi.view_states.log == p->p_latest_state.vs_log
            && pi.view_states.text == p->p_latest_state.vs_text;
        auto tasks_are_same = true;

        {
            auto& bts = lnav::progress_tracker::get_tasks();
            auto* task_cont = *bts.readAccess();
            tasks_are_same = pi.task_states.size() == task_cont->size();
            if (tasks_are_same) {
                for (size_t lpc = 0; lpc < pi.task_states.size(); lpc++) {
                    if (lpc >= task_cont->size()) {
                        continue;
                    }

                    auto tp = (*std::next(task_cont->begin(), lpc))();
                    if (tp.tp_version != pi.task_states[lpc]) {
                        tasks_are_same = false;
                        break;
                    }
                }
            }
        }

        if (views_are_same && tasks_are_same) {
            p->p_pollers.emplace_front(pi);
            auto iter = p->p_pollers.begin();

            p->p_condvar.wait_for(p.lock, timeout);
            p->p_pollers.erase(iter);
        }
        pi_retval.view_states = ViewStates{
            ::rust::String::lossy(p->p_latest_state.vs_log),
            ::rust::String::lossy(p->p_latest_state.vs_text),
        };
    }

    ::rust::Vec<ExtProgress> bt_out;
    {
        auto& bts = lnav::progress_tracker::get_tasks();
        for (const auto& [index, bt] :
             lnav::itertools::enumerate(**bts.readAccess()))
        {
            auto tp = bt();
            pi_retval.task_states.emplace_back(tp.tp_version);
            if (tp.tp_version == 0) {
                continue;
            }
            if (tp.tp_status == lnav::progress_status_t::idle
                && index < pi.task_states.size()
                && pi.task_states[index] == tp.tp_version)
            {
                continue;
            }

            ::rust::Vec<ExtError> errors_out;
            for (const auto& msg : tp.tp_messages) {
                errors_out.emplace_back(ExtError{
                    msg.um_message.al_string,
                    msg.um_reason.al_string,
                    msg.um_help.al_string,
                });
            }

            auto ep = ExtProgress{
                tp.tp_id,
                tp.tp_status == lnav::progress_status_t::idle ? Status::idle
                                                              : Status::working,
                tp.tp_version,
                tp.tp_step,
                tp.tp_completed,
                tp.tp_total,
            };
            bt_out.emplace_back(ep);
        }
    }

    return PollResult{
        pi_retval,
        std::move(bt_out),
    };
}

void
notify_pollers()
{
#    ifdef HAVE_RUST_DEPS
    auto p = POLLERS.writeAccess<std::unique_lock>();
    p->p_condvar.notify_all();
#    endif
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
