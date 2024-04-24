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
 *
 * @file input_dispatcher.hh
 */

#ifndef INPUT_DISPATCHER_HH
#define INPUT_DISPATCHER_HH

#include <functional>

#include <sys/types.h>

#include "base/keycodes.hh"

class input_dispatcher {
public:
    void new_input(const struct timeval& current_time, int ch);

    void poll(const struct timeval& current_time);

    bool in_escape() const { return this->id_escape_index > 0; }

    enum class escape_match_t {
        NONE,
        PARTIAL,
        FULL,
    };

    std::function<escape_match_t(const char*)> id_escape_matcher;
    std::function<bool(int, const char*)> id_key_handler;
    std::function<void(const char*)> id_escape_handler;
    std::function<void()> id_mouse_handler;
    std::function<void(const char*)> id_unhandled_handler;

private:
    void reset_escape_buffer(int ch,
                             const struct timeval& current_time,
                             ssize_t expected_size = -1);
    void append_to_escape_buffer(int ch);

    char id_escape_buffer[32];
    ssize_t id_escape_index{0};
    ssize_t id_escape_expected_size{-1};
    struct timeval id_escape_start_time {
        0, 0
    };
};

#endif
