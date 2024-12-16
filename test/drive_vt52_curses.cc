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
 */

#include <assert.h>
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "base/injector.bind.hh"
#include "base/lnav_log.hh"
#include "config.h"
#include "view_curses.hh"
#include "vt52_curses.hh"
#include "xterm_mouse.hh"

#undef set_window

static auto bound_xterm_mouse = injector::bind<xterm_mouse>::to_singleton();

int
main(int argc, char* argv[])
{
    int lpc, c, fd, retval = EXIT_SUCCESS;
    vt52_curses vt;

    setenv("LANG", "en_US.UTF-8", 1);
    setlocale(LC_ALL, "");
    fd = open("/tmp/lnav.err", O_WRONLY | O_CREAT | O_APPEND, 0666);
    dup2(fd, STDERR_FILENO);
    close(fd);
    fprintf(stderr, "startup\n");
    lnav_log_file = stderr;

    while ((c = getopt(argc, argv, "y:")) != -1) {
        switch (c) {
            case 'y':
                vt.set_y(atoi(optarg));
                break;
        }
    }

    for (lpc = 0; lpc < 1000; lpc++) {
        ncinput nci;

        nci.id = random() & 0xff;
        nci.utf8[0] = nci.id;
        nci.utf8[1] = '\0';
        assert(!vt.map_input(nci).empty());
    }

    {
        static const char* CANNED_INPUT[] = {
            "Gru\xC3\x9F",
            "\r",
            "\x1bK",
            "de",
            "\n",
            "1",
            "2",
            "3",
            "\n",
            "abc",
            "\x02",
            "\a",
            "ab\bcdef",
        };

        auto nco = notcurses_options{};
        nco.flags |= NCOPTION_SUPPRESS_BANNERS;
        auto sc = screen_curses::create(nco).unwrap();
        vt.set_window(sc.get_std_plane());
        vt.set_width(10);

        ncinput nci;

        for (const auto* canned : CANNED_INPUT) {
            vt.map_output(canned, strlen(canned));
            vt.do_update();
            notcurses_render(sc.get_notcurses());
            view_curses::awaiting_user_input();
            notcurses_get_blocking(sc.get_notcurses(), &nci);
        }

        view_curses::awaiting_user_input();
        notcurses_get_blocking(sc.get_notcurses(), &nci);
    }

    return retval;
}
