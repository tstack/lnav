/**
 * Copyright (c) 2018, Timothy Stack
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

#ifndef log_actions_hh
#define log_actions_hh

#include <functional>
#include <utility>

#include "log_data_helper.hh"
#include "logfile_sub_source.hh"

class piper_proc;

class action_delegate : public text_delegate {
public:
    explicit action_delegate(
        logfile_sub_source& lss,
        std::function<void(pid_t)> child_cb,
        std::function<void(const std::string&, std::shared_ptr<piper_proc>)>
            piper_cb)
        : ad_log_helper(lss), ad_child_cb(std::move(child_cb)),
          ad_piper_cb(std::move(piper_cb))
    {
    }

    bool text_handle_mouse(textview_curses& tc, mouse_event& me) override;

private:
    std::string execute_action(const std::string& action_name);

    log_data_helper ad_log_helper;
    std::function<void(pid_t)> ad_child_cb;
    std::function<void(const std::string&, std::shared_ptr<piper_proc>)>
        ad_piper_cb;
    vis_line_t ad_press_line{-1};
    int ad_press_value{-1};
    size_t ad_line_index{0};
};

#endif
