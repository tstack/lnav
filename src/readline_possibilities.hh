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

#ifndef LNAV_READLINE_POSSIBILITIES_H
#define LNAV_READLINE_POSSIBILITIES_H

#include <string>

#include "readline_curses.hh"
#include "textview_curses.hh"
#include "view_helpers.hh"

enum class text_quoting {
    none,
    sql,
    prql,
    regex,
};

void add_view_text_possibilities(readline_curses* rlc,
                                 int context,
                                 const std::string& type,
                                 textview_curses* tc,
                                 text_quoting tq);

template<typename T,
         typename... Args,
         std::enable_if_t<std::is_enum<T>::value, bool> = true>
void
add_view_text_possibilities(readline_curses* rlc, T context, Args... args)
{
    add_view_text_possibilities(
        rlc, lnav::enums::to_underlying(context), args...);
}

void add_filter_expr_possibilities(readline_curses* rlc,
                                   int context,
                                   const std::string& type);

template<typename T,
         typename... Args,
         std::enable_if_t<std::is_enum<T>::value, bool> = true>
void
add_filter_expr_possibilities(readline_curses* rlc, T context, Args... args)
{
    add_filter_expr_possibilities(
        rlc, lnav::enums::to_underlying(context), args...);
}

void add_env_possibilities(ln_mode_t context);
void add_filter_possibilities(textview_curses* tc);
void add_mark_possibilities();
void add_config_possibilities();
void add_tag_possibilities();
void add_file_possibilities();
void add_recent_netlocs_possibilities();
void add_tz_possibilities(ln_mode_t context);
void add_sqlite_possibilities();

extern struct sqlite_metadata_callbacks lnav_sql_meta_callbacks;

#endif  // LNAV_READLINE_POSSIBILITIES_H
