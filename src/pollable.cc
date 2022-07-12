/**
 * Copyright (c) 2022, Timothy Stack
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

#include <algorithm>

#include "pollable.hh"

#include "base/itertools.hh"
#include "base/lnav_log.hh"

pollable::pollable(std::shared_ptr<pollable_supervisor> supervisor,
                   category cat)
    : p_supervisor(supervisor), p_category(cat)
{
    log_debug("pollable attach %p to %p", this, this->p_supervisor.get());
    this->p_supervisor->attach(this);
}

pollable::~pollable()
{
    log_debug("pollable detach %p from %p", this, this->p_supervisor.get());
    this->p_supervisor->detach(this);
}

pollable_supervisor::update_result
pollable_supervisor::update_poll_set(std::vector<struct pollfd>& pollfds)
{
    update_result retval;
    size_t old_size = pollfds.size();

    for (auto& pol : this->b_components) {
        pol->update_poll_set(pollfds);
        switch (pol->get_category()) {
            case pollable::category::background:
                retval.ur_background += pollfds.size() - old_size;
                break;
            case pollable::category::interactive:
                retval.ur_interactive += pollfds.size() - old_size;
                break;
        }
        old_size = pollfds.size();
    }

    return retval;
}

void
pollable_supervisor::check_poll_set(const std::vector<struct pollfd>& pollfds)
{
    std::vector<pollable*> visited;
    auto found_new = false;

    // TODO move this loop into the superclass
    do {
        found_new = false;
        for (auto* pol : this->b_components) {
            if (std::find(visited.begin(), visited.end(), pol) == visited.end())
            {
                visited.emplace_back(pol);
                pol->check_poll_set(pollfds);
                found_new = true;
                break;
            }
        }
    } while (found_new);
}

size_t
pollable_supervisor::count(pollable::category cat)
{
    size_t retval = 0;

    for (const auto* pol : this->b_components) {
        if (pol->get_category() == cat) {
            retval += 1;
        }
    }

    return retval;
}

short
pollfd_revents(const std::vector<struct pollfd>& pollfds, int fd)
{
    return pollfds | lnav::itertools::find_if([fd](const auto& entry) {
               return entry.fd == fd;
           })
        | lnav::itertools::deref() | lnav::itertools::map(&pollfd::revents)
        | lnav::itertools::unwrap_or((short) 0);
}

bool
pollfd_ready(const std::vector<struct pollfd>& pollfds, int fd, short events)
{
    return std::any_of(
        pollfds.begin(), pollfds.end(), [fd, events](const auto& entry) {
            return entry.fd == fd && entry.revents & events;
        });
}
