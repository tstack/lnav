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
 * @file input_dispatcher.cc
 */

#include <array>

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "config.h"

#if defined HAVE_NCURSESW_CURSES_H
#    include <ncursesw/curses.h>
#elif defined HAVE_NCURSESW_H
#    include <ncursesw.h>
#elif defined HAVE_NCURSES_CURSES_H
#    include <ncurses/curses.h>
#elif defined HAVE_NCURSES_H
#    include <ncurses.h>
#elif defined HAVE_CURSES_H
#    include <curses.h>
#else
#    error "SysV or X/Open-compatible Curses header file required"
#endif

#include "base/lnav_log.hh"
#include "base/time_util.hh"
#include "input_dispatcher.hh"
#include "ww898/cp_utf8.hpp"

using namespace ww898;

template<typename A>
static void
to_key_seq(A& dst, const char* src)
{
    dst[0] = '\0';
    for (size_t lpc = 0; src[lpc]; lpc++) {
        snprintf(dst.data() + strlen(dst.data()),
                 dst.size() - strlen(dst.data()),
                 "x%02x",
                 src[lpc] & 0xff);
    }
}

void
input_dispatcher::new_input(const struct timeval& current_time, int ch)
{
    std::optional<bool> handled = std::nullopt;
    std::array<char, 32 * 3 + 1> keyseq{0};

    switch (ch) {
        case KEY_ESCAPE:
            this->reset_escape_buffer(ch, current_time);
            break;
        case KEY_MOUSE:
            this->id_mouse_handler();
            break;
        default:
            if (this->id_escape_index > 0) {
                this->append_to_escape_buffer(ch);

                if (strcmp("\x1b[", this->id_escape_buffer) == 0) {
                } else if (strcmp("\x1b[<", this->id_escape_buffer) == 0) {
                    this->id_mouse_handler();
                    this->id_escape_index = 0;
                } else if (this->id_escape_expected_size == -1
                           || this->id_escape_index
                               == this->id_escape_expected_size)
                {
                    to_key_seq(keyseq, this->id_escape_buffer);
                    switch (this->id_escape_matcher(keyseq.data())) {
                        case escape_match_t::NONE: {
                            for (int lpc = 0; this->id_escape_buffer[lpc];
                                 lpc++)
                            {
                                snprintf(keyseq.data(),
                                         keyseq.size(),
                                         "x%02x",
                                         this->id_escape_buffer[lpc] & 0xff);
                                handled = this->id_key_handler(
                                    this->id_escape_buffer[lpc], keyseq.data());
                            }
                            this->id_escape_index = 0;
                            break;
                        }
                        case escape_match_t::PARTIAL:
                            break;
                        case escape_match_t::FULL:
                            this->id_escape_handler(keyseq.data());
                            this->id_escape_index = 0;
                            break;
                    }
                }
                if (this->id_escape_expected_size != -1
                    && this->id_escape_index == this->id_escape_expected_size)
                {
                    this->id_escape_index = 0;
                }
            } else if (ch > 0xff) {
                if (KEY_F(0) <= ch && ch <= KEY_F(64)) {
                    snprintf(keyseq.data(), keyseq.size(), "f%d", ch - KEY_F0);
                } else {
                    snprintf(keyseq.data(), keyseq.size(), "n%04o", ch);
                }
                handled = this->id_key_handler(ch, keyseq.data());
            } else {
                auto seq_size = utf::utf8::char_size([ch]() {
                                    return std::make_pair(ch, 16);
                                }).unwrapOr(size_t{1});

                if (seq_size == 1) {
                    snprintf(keyseq.data(), keyseq.size(), "x%02x", ch & 0xff);
                    handled = this->id_key_handler(ch, keyseq.data());
                } else {
                    this->reset_escape_buffer(ch, current_time, seq_size);
                }
            }
            break;
    }

    if (handled && !handled.value()) {
        this->id_unhandled_handler(keyseq.data());
    }
}

void
input_dispatcher::poll(const struct timeval& current_time)
{
    if (this->id_escape_index == 1) {
        static const struct timeval escape_threshold = {0, 10000};
        struct timeval diff;

        timersub(&current_time, &this->id_escape_start_time, &diff);
        if (escape_threshold < diff) {
            static const char ESC_KEYSEQ[] = "\x1b";
            this->id_key_handler(KEY_ESCAPE, ESC_KEYSEQ);
            this->id_escape_index = 0;
        }
    }
}

void
input_dispatcher::reset_escape_buffer(int ch,
                                      const timeval& current_time,
                                      ssize_t expected_size)
{
    this->id_escape_index = 0;
    this->append_to_escape_buffer(ch);
    this->id_escape_expected_size = expected_size;
    this->id_escape_start_time = current_time;
}

void
input_dispatcher::append_to_escape_buffer(int ch)
{
    if (this->id_escape_index
        < static_cast<ssize_t>(sizeof(this->id_escape_buffer) - 1))
    {
        this->id_escape_buffer[this->id_escape_index++] = static_cast<char>(ch);
        this->id_escape_buffer[this->id_escape_index] = '\0';
    }
}
