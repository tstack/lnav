/**
 * Copyright (c) 2017, Timothy Stack
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
 */

#include "config.h"

#include <stdio.h>
#include <assert.h>
#include <view_curses.hh>
#include <attr_line.hh>

#include "lnav_config.hh"
#include "help_text_formatter.hh"

struct _lnav_config lnav_config;

lnav_config_listener *lnav_config_listener::LISTENER_LIST;

int main(int argc, char *argv[])
{
	int retval = EXIT_SUCCESS;

    static help_text ht = help_text(
        "regexp_replace",
        "Replace parts of a string that match a regular expression")
        .with_parameters(
            {
                {"str", "The string to perform replacements on"},
                {"re", "The regular expression to match"},
                {"repl", "The replacement string"},
            })
        .with_example(
            {
                ";SELECT regexp_replace('abbb bbbc', 'b+', '') AS res",
                "a c",
            });

    {
        setenv("TERM", "ansi", 1);
        screen_curses sc;
        view_colors::init();

        attr_line_t al;

        format_help_text_for_term(ht, 35, al);

        std::vector<attr_line_t> lines;

        al.split_lines(lines);

        line_range lr{0, 80};

        int y = 0;
        for (auto &line : lines) {
            view_curses::mvwattrline(sc.get_window(), y++, 0, line, lr);
        }

        getch();
    }

	return retval;
}
