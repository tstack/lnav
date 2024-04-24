/**
 * Copyright (c) 2015, Timothy Stack
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

#ifndef LNAV_READLINE_CALLBACKS_HH
#define LNAV_READLINE_CALLBACKS_HH

#include "readline_curses.hh"

void rl_set_help();
void rl_change(readline_curses* rc);
void rl_search(readline_curses* rc);
void lnav_rl_abort(readline_curses* rc);
void rl_callback(readline_curses* rc);
void rl_alt_callback(readline_curses* rc);
void rl_display_matches(readline_curses* rc);
void rl_display_next(readline_curses* rc);
void rl_completion_request(readline_curses* rc);
void rl_focus(readline_curses* rc);
void rl_blur(readline_curses* rc);

readline_context::split_result_t prql_splitter(readline_context& rc,
                                               const std::string& cmdline);

extern const char* RE_HELP;
extern const char* RE_EXAMPLE;
extern const char* SQL_HELP;
extern const char* SQL_EXAMPLE;

#endif  // LNAV_READLINE_CALLBACKS_HH
