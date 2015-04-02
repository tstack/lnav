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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file vt52_curses.cc
 */

#include "config.h"

#include <unistd.h>
#include <string.h>

#include <map>

#include "lnav_log.hh"
#include "vt52_curses.hh"

#if defined HAVE_NCURSESW_CURSES_H
#  include <ncursesw/curses.h>
#  include <ncursesw/term.h>
#elif defined HAVE_NCURSESW_H
#  include <ncursesw.h>
#  include <term.h>
#elif defined HAVE_NCURSES_CURSES_H
#  include <ncurses/curses.h>
#  include <ncurses/term.h>
#elif defined HAVE_NCURSES_H
#  include <ncurses.h>
#  include <term.h>
#elif defined HAVE_CURSES_H
#  include <curses.h>
#  include <term.h>
#else
#  error "SysV or X/Open-compatible Curses header file required"
#endif

using namespace std;

/**
 * Singleton used to hold the mapping of ncurses keycodes to VT52 escape
 * sequences.
 */
class vt52_escape_map {
public:

    /** @return The singleton. */
    static vt52_escape_map &singleton()
    {
        static vt52_escape_map s_vem;

        return s_vem;
    };

    /**
     * @param ch The ncurses keycode.
     * @return The null terminated VT52 escape sequence.
     */
    const char *operator[](int ch) const
    {
        map<int, const char *>::const_iterator iter;
        const char *retval = NULL;

        if ((iter = this->vem_map.find(ch)) != this->vem_map.end()) {
            retval = iter->second;
        }

        return retval;
    };

    const char *operator[](const char *seq) const
    {
        map<string, const char *>::const_iterator iter;
        const char *retval = NULL;

        require(seq != NULL);

        if ((iter = this->vem_input_map.find(seq)) !=
            this->vem_input_map.end()) {
            retval = iter->second;
        }

        return retval;
    };

private:

    /** Construct the map with a few escape sequences. */
    vt52_escape_map()
    {
        static char area_buffer[1024];
        char *      area = area_buffer;

        if (tgetent(NULL, "vt52") == ERR) {
            perror("tgetent");
        }
        this->vem_map[KEY_UP]        = tgetstr((char *)"ku", &area);
        this->vem_map[KEY_DOWN]      = tgetstr((char *)"kd", &area);
        this->vem_map[KEY_RIGHT]     = tgetstr((char *)"kr", &area);
        this->vem_map[KEY_LEFT]      = tgetstr((char *)"kl", &area);
        this->vem_map[KEY_HOME]      = tgetstr((char *)"kh", &area);
        this->vem_map[KEY_BACKSPACE] = "\010";
        this->vem_map[KEY_DC]        = "\x4";

        this->vem_map[KEY_BEG] = "\x1";
        this->vem_map[KEY_END] = "\x5";

        this->vem_map[KEY_SLEFT] = tgetstr((char *)"#4", &area);
        if (this->vem_map[KEY_SLEFT] == NULL) {
            this->vem_map[KEY_SLEFT] = "\033b";
        }
        this->vem_map[KEY_SRIGHT] = tgetstr((char *)"%i", &area);
        if (this->vem_map[KEY_SRIGHT] == NULL) {
            this->vem_map[KEY_SRIGHT] = "\033f";
        }

        this->vem_input_map[tgetstr((char *)"ce", &area)] = "ce";
        this->vem_input_map[tgetstr((char *)"kl", &area)] = "kl";
        this->vem_input_map[tgetstr((char *)"kr", &area)] = "kr";
        tgetent(NULL, getenv("TERM"));
    };

    /** Map of ncurses keycodes to VT52 escape sequences. */
    map<int, const char *>    vem_map;
    map<string, const char *> vem_input_map;
};

vt52_curses::vt52_curses()
    : vc_window(NULL),
      vc_x(0),
      vc_y(0),
      vc_max_height(0),
      vc_escape_len(0),
      vc_map_buffer(0)
{ }

vt52_curses::~vt52_curses()
{ }

const char *vt52_curses::map_input(int ch, int &len_out)
{
    const char *esc, *retval;

    /* Check for an escape sequence, otherwise just return the char. */
    if ((esc = vt52_escape_map::singleton()[ch]) != NULL) {
        retval  = esc;
        len_out = strlen(retval);
    }
    else {
        this->vc_map_buffer = (char)ch;
        retval  = &this->vc_map_buffer; /* XXX probably shouldn't do this. */
        len_out = 1;
    }

    ensure(retval != NULL);
    ensure(len_out > 0);

    return retval;
}

void vt52_curses::map_output(const char *output, int len)
{
    int y, lpc;

    require(this->vc_window != NULL);

    y = this->get_actual_y();
    wmove(this->vc_window, y, this->vc_x);
    for (lpc = 0; lpc < len; lpc++) {
        if (this->vc_escape_len > 0) {
            const char *cap;

            this->vc_escape[this->vc_escape_len] = output[lpc];
            this->vc_escape_len += 1;
            this->vc_escape[this->vc_escape_len] = '\0';

            if ((cap = vt52_escape_map::singleton()[this->vc_escape]) !=
                NULL) {
                if (strcmp(cap, "ce") == 0) {
                    wclrtoeol(this->vc_window);
                    this->vc_escape_len = 0;
                }
                else if (strcmp(cap, "kl") == 0) {
                    this->vc_x -= 1;
                    wmove(this->vc_window, y, this->vc_x);
                    this->vc_escape_len = 0;
                }
                else if (strcmp(cap, "kr") == 0) {
                    this->vc_x += 1;
                    wmove(this->vc_window, y, this->vc_x);
                    this->vc_escape_len = 0;
                }
                else {
                    ensure(0);
                }
            }
        }
        else {
            switch (output[lpc]) {
            case STX:
                this->vc_past_lines.clear();
                this->vc_x = 0;
                wmove(this->vc_window, y, this->vc_x);
                wclrtoeol(this->vc_window);
                break;

            case BELL:
                flash();
                break;

            case BACKSPACE:
                this->vc_x -= 1;
                wmove(this->vc_window, y, this->vc_x);
                break;

            case ESCAPE:
                this->vc_escape[0]  = ESCAPE;
                this->vc_escape_len = 1;
                break;

            case '\n':
            {
                unsigned long width, height;
                char *        buffer;

                getmaxyx(this->vc_window, height, width);
                (void)height; //suppress unused warnings.

                buffer     = (char *)alloca(width);
                this->vc_x = 0;
                wmove(this->vc_window, y, this->vc_x);

                /*
                 * XXX Not sure why we need to subtract one from the width
                 * here, but otherwise it seems to spill over and screw up
                 * the next line when we're writing it back out in
                 * do_update().
                 */
                winnstr(this->vc_window, buffer, width - 1);
                this->vc_past_lines.push_back(buffer);
                wclrtoeol(this->vc_window);
            }
            break;

            case '\r':
                this->vc_x = 0;
                wmove(this->vc_window, y, this->vc_x);
                break;

            default:
                mvwaddch(this->vc_window, y, this->vc_x, output[lpc]);
                this->vc_x += 1;
                break;
            }
        }
    }
}

void vt52_curses::do_update(void)
{
    list<string>::iterator iter;
    int y;

    y = this->get_actual_y() - (int)this->vc_past_lines.size();
    for (iter = this->vc_past_lines.begin();
         iter != this->vc_past_lines.end();
         ++iter, y++) {
        if (y >= 0) {
            mvwprintw(this->vc_window, y, 0, "%s", iter->c_str());
            wclrtoeol(this->vc_window);
        }
    }
    wmove(this->vc_window, y, this->vc_x);
}
