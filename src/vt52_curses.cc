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

#include <term.h>

#undef lines
#undef set_window

#include <map>

#include <string.h>

#include "base/lnav_log.hh"
#include "config.h"
#include "vt52_curses.hh"
#include "ww898/cp_utf8.hpp"

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
    std::optional<string_fragment> operator[](int ch) const
    {
        const auto iter = this->vem_map.find(ch);

        if (iter == this->vem_map.end()) {
            return std::nullopt;
        }

        return iter->second;
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

        if (tgetent(nullptr, "vt52") <= 0) {
            perror("tgetent");
        }
        this->vem_map[NCKEY_UP]
            = string_fragment::from_c_str(tgetstr((char*) "ku", &area));
        this->vem_map[NCKEY_DOWN]
            = string_fragment::from_c_str(tgetstr((char*) "kd", &area));
        this->vem_map[NCKEY_RIGHT]
            = string_fragment::from_c_str(tgetstr((char*) "kr", &area));
        this->vem_map[NCKEY_LEFT]
            = string_fragment::from_c_str(tgetstr((char*) "kl", &area));
        this->vem_map[NCKEY_HOME]
            = string_fragment::from_c_str(tgetstr((char*) "kh", &area));
        if (this->vem_map[NCKEY_HOME].empty()) {
            this->vem_map[NCKEY_HOME] = string_fragment::from_const("\x01");
        }
        this->vem_map[NCKEY_BACKSPACE] = string_fragment::from_const("\010");
        this->vem_map[NCKEY_HOME] = string_fragment::from_const("\x01");
        this->vem_map[NCKEY_END] = string_fragment::from_const("\x05");
        this->vem_map[NCKEY_ENTER] = string_fragment::from_const("\r");

        this->vem_input_map[tgetstr((char*) "ce", &area)] = "ce";
        this->vem_input_map[tgetstr((char*) "kl", &area)] = "kl";
        this->vem_input_map[tgetstr((char*) "kr", &area)] = "kr";
        // bracketed paste mode
        this->vem_input_map["\x1b[?2004h"] = "BE";
        this->vem_input_map["\x1b[?2004l"] = "BD";
        tgetent(nullptr, getenv("TERM"));
    };

    /** Map of ncurses keycodes to VT52 escape sequences. */
    mutable std::map<int, string_fragment> vem_map;
    std::map<std::string, const char*> vem_input_map;
};

string_fragment
vt52_curses::map_input(const ncinput& ch)
{
    /* Check for an escape sequence, otherwise just return the char. */
    if (ch.modifiers == 0) {
        const auto esc = vt52_escape_map::singleton()[ch.id];
        if (esc) {
            return esc.value();
        }
    }
    if (ch.id == 0x7f) {
        this->vc_map_buffer[0] = static_cast<char>(ch.id);
        return string_fragment::from_bytes(this->vc_map_buffer, 1);
    }

    if ((ncinput_shift_p(&ch) || ncinput_ctrl_p(&ch) || ncinput_alt_p(&ch)
         || ncinput_meta_p(&ch))
        && ch.id == NCKEY_LEFT)
    {
        return string_fragment::from_const("\033b");
    }

    if ((ncinput_shift_p(&ch) || ncinput_ctrl_p(&ch) || ncinput_alt_p(&ch)
         || ncinput_meta_p(&ch))
        && ch.id == NCKEY_RIGHT)
    {
        return string_fragment::from_const("\033f");
    }

    if (ncinput_shift_p(&ch) && ch.id == NCKEY_TAB) {
        return string_fragment::from_const("\033[Z");
    }

    size_t index = 0;
    for (const auto eff_ch : ch.eff_text) {
        ww898::utf::utf8::write(eff_ch, [this, &index](const char bits) {
            this->vc_map_buffer[index] = bits;
            index += 1;
        });
    }

    return string_fragment::from_bytes(this->vc_map_buffer, index);
}

void
vt52_curses::map_output(const char* output, int len)
{
    require(this->vc_window != nullptr);

    for (int lpc = 0; lpc < len; lpc++) {
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
                    write(STDIN_FILENO, &next_ch, 1);
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
    mvwattrline(this->vc_window,
                this->get_actual_y(),
                this->vc_x,
                this->vc_line,
                line_range{0, (int) actual_width});
    return true;
}
