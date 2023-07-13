/**
 * Copyright (c) 2023, Timothy Stack
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

// XXX This code is unused right now, but it's nice and I don't want to delete
// it just yet.

#ifndef lnav_url_handler_hh
#define lnav_url_handler_hh

#include <map>
#include <string>

#include "base/auto_fd.hh"
#include "base/auto_pid.hh"
#include "base/isc.hh"
#include "base/lnav.console.hh"
#include "base/result.h"
#include "line_buffer.hh"
#include "mapbox/variant.hpp"

namespace lnav {
namespace url_handler {

class looper : public isc::service<looper> {
public:
    Result<void, lnav::console::user_message> open(std::string url);

    void close(std::string url);

private:
    class handler_looper : public isc::service<handler_looper> {
    public:
        handler_looper(std::string url,
                       auto_pid<process_state::running> pid,
                       auto_fd infd)
            : isc::service<handler_looper>(url), hl_state(std::move(pid))
        {
            this->hl_line_buffer.set_fd(infd);
        }

    protected:
        void loop_body() override;

        std::chrono::milliseconds compute_timeout(
            mstime_t current_time) const override
        {
            return std::chrono::milliseconds{0};
        }

    public:
        struct handler_completed {};

        using state_v = mapbox::util::variant<auto_pid<process_state::running>,
                                              handler_completed>;

        file_range hl_last_range;
        line_buffer hl_line_buffer;
        state_v hl_state;
    };

    struct child {};

    std::map<std::string, std::shared_ptr<handler_looper>> l_children;
};

}  // namespace url_handler
}  // namespace lnav

#endif
