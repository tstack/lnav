/**
 * Copyright (c) 2007-2012, Timothy Stack
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
 * @file lnav_commands.hh
 */

#ifndef lnav_commands_hh
#define lnav_commands_hh

#include <optional>
#include <string>

#include "readline_context.hh"

/**
 * Initialize the given map with the builtin lnav commands.
 */
void init_lnav_commands(readline_context::command_map_t& cmd_map);

void init_lnav_bookmark_commands(readline_context::command_map_t& cmd_map);

void init_lnav_display_commands(readline_context::command_map_t& cmd_map);

void init_lnav_io_commands(readline_context::command_map_t& cmd_map);

void init_lnav_filtering_commands(readline_context::command_map_t& cmd_map);

std::string remaining_args(const std::string& cmdline,
                           const std::vector<std::string>& args,
                           size_t index = 1);

string_fragment remaining_args_frag(const std::string& cmdline,
                                    const std::vector<std::string>& args,
                                    size_t index = 1);

std::optional<std::string> find_arg(std::vector<std::string>& args,
                                    const std::string& flag);

bookmark_vector<vis_line_t> combined_user_marks(vis_bookmarks& vb);

#endif
