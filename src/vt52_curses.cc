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
 * @file vt52_curses.cc
 */

#include <map>

#include "vt52_curses.hh"

#include <string.h>

#include "base/lnav_log.hh"
#include "config.h"

#if defined HAVE_NCURSESW_CURSES_H
#    include <ncursesw/curses.h>
#    include <ncursesw/term.h>
#elif defined HAVE_NCURSESW_H
#    include <ncursesw.h>
#    include <term.h>
#elif defined HAVE_NCURSES_CURSES_H
#    include <ncurses/curses.h>
#    include <ncurses/term.h>
#elif defined HAVE_NCURSES_H
#    include <ncurses.h>
#    include <term.h>
#elif defined HAVE_CURSES_H
#    include <curses.h>
#    include <term.h>
#else
#    error "SysV or X/Open-compatible Curses header file required"
#endif

/**
 * Singleton used to hold the mapping of ncurses keycodes to VT52 escape
 * sequences.
 */
class vt52_escape_map {
public:
    /** @return The singleton. */
    static vt52_escape_map& singleton()
    {
        static vt52_escape_map s_vem;

        return s_vem;
    };

    /**
     * @param ch The ncurses keycode.
     * @return The null terminated VT52 escape sequence.
     */
    const char* operator[](int ch) const
    {
        auto iter = this->vem_map.find(ch);
        const char* retval = nullptr;

        if (iter == this->vem_map.end()) {
            if (ch > KEY_MAX) {
                auto name = keyname(ch);

                if (name != nullptr) {
                    auto seq = tigetstr(name);

                    if (seq != nullptr) {
                        this->vem_map[ch] = seq;
                        retval = seq;
                    }
                }
            }
        } else {
            retval = iter->second;
        }

        return retval;
    };

    const char* operator[](const char* seq) const
    {
        std::map<std::string, const char*>::const_iterator iter;
        const char* retval = nullptr;

        require(seq != nullptr);

        if ((iter = this->vem_input_map.find(seq)) != this->vem_input_map.end())
        {
            retval = iter->second;
        }

        return retval;
    };

private:
    /** Construct the map with a few escape sequences. */
    vt52_escape_map()
    {
        static char area_buffer[1024];
        char* area = area_buffer;

        if (tgetent(nullptr, "vt52") == ERR) {
            perror("tgetent");
        }
        this->vem_map[KEY_UP] = tgetstr((char*) "ku", &area);
        this->vem_map[KEY_DOWN] = tgetstr((char*) "kd", &area);
        this->vem_map[KEY_RIGHT] = tgetstr((char*) "kr", &area);
        this->vem_map[KEY_LEFT] = tgetstr((char*) "kl", &area);
        this->vem_map[KEY_HOME] = tgetstr((char*) "kh", &area);
        if (this->vem_map[KEY_HOME] == nullptr) {
            this->vem_map[KEY_HOME] = "\x01";
        }
        this->vem_map[KEY_BACKSPACE] = "\010";
        this->vem_map[KEY_DC] = "\x04";

        this->vem_map[KEY_BEG] = "\x01";
        this->vem_map[KEY_END] = "\x05";

        this->vem_map[KEY_SLEFT] = tgetstr((char*) "#4", &area);
        if (this->vem_map[KEY_SLEFT] == nullptr) {
            this->vem_map[KEY_SLEFT] = "\033b";
        }
        this->vem_map[KEY_SRIGHT] = tgetstr((char*) "%i", &area);
        if (this->vem_map[KEY_SRIGHT] == nullptr) {
            this->vem_map[KEY_SRIGHT] = "\033f";
        }

        this->vem_map[KEY_BTAB] = "\033[Z";

        this->vem_input_map[tgetstr((char*) "ce", &area)] = "ce";
        this->vem_input_map[tgetstr((char*) "kl", &area)] = "kl";
        this->vem_input_map[tgetstr((char*) "kr", &area)] = "kr";
        // bracketed paste mode
        this->vem_input_map["\x1b[?2004h"] = "BE";
        this->vem_input_map["\x1b[?2004l"] = "BD";
        tgetent(nullptr, getenv("TERM"));
    };

    /** Map of ncurses keycodes to VT52 escape sequences. */
    mutable std::map<int, const char*> vem_map;
    std::map<std::string, const char*> vem_input_map;
};

const char*
vt52_curses::map_input(int ch, int& len_out)
{
    const char *esc, *retval;

    /* Check for an escape sequence, otherwise just return the char. */
    if ((esc = vt52_escape_map::singleton()[ch]) != nullptr) {
        retval = esc;
        len_out = strlen(retval);
    } else {
        switch (ch) {
            case 0x7f:
                ch = BACKSPACE;
                break;
        }
        this->vc_map_buffer = (char) ch;
        retval = &this->vc_map_buffer; /* XXX probably shouldn't do this. */
        len_out = 1;
    }

    ensure(retval != nullptr);
    ensure(len_out > 0);

    return retval;
}

void
vt52_curses::map_output(const char* output, int len)
{
    int lpc;

    require(this->vc_window != nullptr);

    for (lpc = 0; lpc < len; lpc++) {
        if (this->vc_escape_len > 0) {
            const char* cap;

            this->vc_escape[this->vc_escape_len] = output[lpc];
            this->vc_escape_len += 1;
            this->vc_escape[this->vc_escape_len] = '\0';

            if (this->vc_expected_escape_len != -1) {
                if (this->vc_escape_len == this->vc_expected_escape_len) {
                    auto& line_string = this->vc_line.get_string();
                    auto x_byte_index = utf8_char_to_byte_index(
                        line_string, this->vc_cursor_x);

                    for (int esc_index = 0; esc_index < this->vc_escape_len;
                         esc_index++)
                    {
                        if (x_byte_index < this->vc_line.length()) {
                            line_string[x_byte_index]
                                = this->vc_escape[esc_index];
                        } else {
                            this->vc_line.append(1, this->vc_escape[esc_index]);
                        }
                        x_byte_index += 1;
                    }
                    this->vc_cursor_x += 1;
                    this->vc_escape_len = 0;
                }
            } else if ((cap = vt52_escape_map::singleton()[this->vc_escape])
                       != nullptr)
            {
                this->vc_escape_len = 0;
                if (strcmp(cap, "ce") == 0) {
                    this->vc_line.erase_utf8_chars(this->vc_cursor_x);
                } else if (strcmp(cap, "kl") == 0) {
                    this->vc_cursor_x -= 1;
                } else if (strcmp(cap, "kr") == 0) {
                    this->vc_cursor_x += 1;
                } else if (strcmp(cap, "BE") == 0 || strcmp(cap, "BD") == 0) {
                    // TODO pass bracketed paste mode through
                } else {
                    ensure(0);
                }
            }
        } else {
            auto next_ch = output[lpc];
            auto seq_size = ww898::utf::utf8::char_size([next_ch]() {
                                return std::make_pair(next_ch, 16);
                            }).unwrapOr(size_t{1});

            if (seq_size > 1) {
                this->vc_escape[0] = next_ch;
                this->vc_escape_len = 1;
                this->vc_expected_escape_len = seq_size;
                continue;
            }

            switch (next_ch) {
                case STX:
                    this->vc_cursor_x = 0;
                    this->vc_line.clear();
                    break;

                case BELL:
                    flash();
                    break;

                case BACKSPACE:
                    this->vc_cursor_x -= 1;
                    break;

                case ESCAPE:
                    this->vc_escape[0] = ESCAPE;
                    this->vc_escape_len = 1;
                    this->vc_expected_escape_len = -1;
                    break;

                case '\n':
                    this->vc_cursor_x = 0;
                    this->vc_line.clear();
                    break;

                case '\r':
                    this->vc_cursor_x = 0;
                    break;

                default: {
                    auto& line_string = this->vc_line.get_string();
                    auto x_byte_index = utf8_char_to_byte_index(
                        line_string, this->vc_cursor_x);

                    if (x_byte_index < this->vc_line.length()) {
                        line_string[x_byte_index] = next_ch;
                    } else {
                        this->vc_line.append(1, next_ch);
                    }
                    this->vc_cursor_x += 1;
                    break;
                }
            }
        }
    }
}

bool
vt52_curses::do_update()
{
    auto actual_width = this->get_actual_width();
    view_curses::mvwattrline(this->vc_window,
                             this->get_actual_y(),
                             this->vc_x,
                             this->vc_line,
                             line_range{0, (int) actual_width});
    wmove(
        this->vc_window, this->get_actual_y(), this->vc_x + this->vc_cursor_x);
    return true;
}
