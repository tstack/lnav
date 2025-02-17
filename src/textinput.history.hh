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

#ifndef textinput_history_hh
#define textinput_history_hh

#include <chrono>
#include <functional>
#include <optional>
#include <string>

#include "base/guard_util.hh"
#include "base/intern_string.hh"
#include "base/log_level_enum.hh"

namespace lnav::textinput {

struct history {
    static history for_context(string_fragment name);

    using timestamp_t = std::chrono::system_clock::time_point;

    struct entry {
        std::string e_session_id;
        timestamp_t e_start_time;
        std::optional<timestamp_t> e_end_time;
        std::string e_content;
        log_level_t e_status{log_level_t::LEVEL_INFO};
    };

    struct op_guard {
        string_fragment og_context;
        string_fragment og_content;
        timestamp_t og_start_time{std::chrono::system_clock::now()};
        log_level_t og_status{log_level_t::LEVEL_INFO};
        guard_helper og_guard_helper;

        op_guard() = default;
        op_guard(op_guard&&) = default;
        op_guard(const op_guard&) = delete;
        op_guard& operator=(const op_guard&) = delete;
        op_guard& operator=(op_guard&&) = default;

        ~op_guard();
    };

    void insert_plain_content(string_fragment content);

    op_guard start_operation(string_fragment content) const
    {
        return {
            this->h_context,
            content,
        };
    }

    using entry_handler_t = std::function<void(const entry&)>;
    void query_entries(string_fragment str, entry_handler_t handler);

    string_fragment h_context;
};

}  // namespace lnav::textinput

#endif
