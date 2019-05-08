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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file input_dispatcher.cc
 */

#include "config.h"

#include <string.h>
#include <sys/time.h>

#if defined HAVE_NCURSESW_CURSES_H
#  include <ncursesw/curses.h>
#elif defined HAVE_NCURSESW_H
#  include <ncursesw.h>
#elif defined HAVE_NCURSES_CURSES_H
#  include <ncurses/curses.h>
#elif defined HAVE_NCURSES_H
#  include <ncurses.h>
#elif defined HAVE_CURSES_H
#  include <curses.h>
#else
#  error "SysV or X/Open-compatible Curses header file required"
#endif

#include "base/lnav_log.hh"
#include "input_dispatcher.hh"

void input_dispatcher::new_input(const struct timeval &current_time, int ch)
{
    switch (ch) {
        case KEY_ESCAPE:
            this->id_escape_index = 0;
            this->append_to_escape_buffer(ch);
            this->id_escape_start_time = current_time;
            break;
        case KEY_MOUSE:
            this->id_mouse_handler();
            break;
        default:
            if (this->id_escape_index > 0) {
                this->append_to_escape_buffer(ch);

                if (strcmp("\x1b[", this->id_escape_buffer) == 0) {
                    this->id_mouse_handler();
                    this->id_escape_index = 0;
                } else {
                    switch (this->id_escape_matcher(this->id_escape_buffer)) {
                        case escape_match_t::NONE:
                            for (int lpc = 0; this->id_escape_buffer[lpc]; lpc++) {
                                this->id_key_handler(this->id_escape_buffer[lpc]);
                            }
                            this->id_escape_index = 0;
                            break;
                        case escape_match_t::PARTIAL:
                            break;
                        case escape_match_t::FULL:
                            this->id_escape_handler(this->id_escape_buffer);
                            this->id_escape_index = 0;
                            break;
                    }
                }
            } else {
                this->id_key_handler(ch);
            }
            break;
    }
}

void input_dispatcher::poll(const struct timeval &current_time)
{
    if (this->id_escape_index == 1) {
        struct timeval diff;

        gettimeofday((struct timeval *) &current_time, nullptr);

        timersub(&current_time, &this->id_escape_start_time, &diff);
        if (diff.tv_sec > 0 || diff.tv_usec > (10000)) {
            this->id_key_handler(KEY_CTRL_RBRACKET);
            this->id_escape_index = 0;
        }
    }
}
