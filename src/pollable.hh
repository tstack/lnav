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

#ifndef lnav_pollable_hh
#define lnav_pollable_hh

#include <memory>
#include <vector>

#include <poll.h>

#include "base/bus.hh"

class pollable_supervisor;

class pollable {
public:
    enum class category {
        background,
        interactive,
    };

    pollable(std::shared_ptr<pollable_supervisor> supervisor, category cat);

    pollable(const pollable&) = delete;

    virtual ~pollable();

    category get_category() const { return this->p_category; }

    virtual void update_poll_set(std::vector<pollfd>& pollfds) = 0;

    virtual void check_poll_set(const std::vector<pollfd>& pollfds) = 0;

private:
    std::shared_ptr<pollable_supervisor> p_supervisor;
    const category p_category;
};

class pollable_supervisor : public bus<pollable> {
public:
    struct update_result {
        size_t ur_background{0};
        size_t ur_interactive{0};
    };

    update_result update_poll_set(std::vector<pollfd>& pollfds);

    void check_poll_set(const std::vector<pollfd>& pollfds);

    size_t count(pollable::category cat);
};

short pollfd_revents(const std::vector<pollfd>& pollfds, int fd);

bool pollfd_ready(const std::vector<pollfd>& pollfds,
                  int fd,
                  short events = POLLIN | POLLHUP);

#endif
