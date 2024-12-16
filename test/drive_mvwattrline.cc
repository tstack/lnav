/**
 * Copyright (c) 2014, Timothy Stack
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

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "base/injector.bind.hh"
#include "config.h"
#include "view_curses.hh"
#include "xterm_mouse.hh"

static auto bound_xterm_mouse = injector::bind<xterm_mouse>::to_singleton();

int
main(int argc, char* argv[])
{
    int c, retval = EXIT_SUCCESS;
    bool wait_for_input = false;

    while ((c = getopt(argc, argv, "w")) != -1) {
        switch (c) {
            case 'w':
                wait_for_input = true;
                break;
        }
    }

    setenv("LANG", "en_US.UTF-8", 1);
    setlocale(LC_ALL, "");

    {
        notcurses_options nco;
        memset(&nco, 0, sizeof(nco));
        nco.flags |= NCOPTION_SUPPRESS_BANNERS | NCOPTION_NO_WINCH_SIGHANDLER;
        auto sc = screen_curses::create(nco).unwrap();
        struct line_range lr(0, 40);
        attr_line_t al;
        int y = 0;
        auto win = sc.get_std_plane();

        view_colors::singleton().init(sc.get_notcurses());

        al.with_string("Plain text");
        view_curses::mvwattrline(win, y++, 0, al, lr);

        al.clear()
            .with_string("\tLeading tab")
            .with_attr(string_attr(line_range(0, 1),
                                   VC_STYLE.value(text_attrs::with_reverse())));
        view_curses::mvwattrline(win, y++, 0, al, lr);

        al.clear()
            .with_string("Tab\twith text")
            .with_attr(string_attr(line_range(1, 4),
                                   VC_STYLE.value(text_attrs::with_reverse())));
        view_curses::mvwattrline(win, y++, 0, al, lr);

        al.clear()
            .with_string("Tab\twith text #2")
            .with_attr(string_attr(line_range(3, 4),
                                   VC_STYLE.value(text_attrs::with_reverse())));
        view_curses::mvwattrline(win, y++, 0, al, lr);

        al.clear()
            .with_string("Two\ttabs\twith text")
            .with_attr(string_attr(line_range(4, 6),
                                   VC_STYLE.value(text_attrs::with_reverse())))
            .with_attr(string_attr(line_range(9, 13),
                                   VC_STYLE.value(text_attrs::with_reverse())));
        view_curses::mvwattrline(win, y++, 0, al, lr);

        auto mixed_style = text_attrs{};
        mixed_style.ta_fg_color = palette_color{COLOR_RED};
        mixed_style.ta_bg_color = palette_color{COLOR_BLACK};
        al.clear()
            .with_string("Text with mixed attributes.")
            .with_attr(string_attr(
                line_range(5, 9),
                VC_STYLE.value(mixed_style)))
            .with_attr(string_attr(line_range(7, 12),
                                   VC_STYLE.value(text_attrs::with_reverse())));
        view_curses::mvwattrline(win, y++, 0, al, lr);

        const char* text = u8"Text with unicode ▶ characters";
        int offset = strstr(text, "char") - text;
        al.clear().with_string(text).with_attr(
            string_attr(line_range(offset, offset + 4),
                        VC_STYLE.value(text_attrs::with_reverse())));
        view_curses::mvwattrline(win, y++, 0, al, lr);

        notcurses_render(sc.get_notcurses());

        if (wait_for_input) {
            ncinput nci;
            notcurses_get_blocking(sc.get_notcurses(), &nci);
        }
    }

    return retval;
}
