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
 * @file vt52_curses.hh
 */

#ifndef vt52_curses_hh
#define vt52_curses_hh

#include <list>
#include <string>

#include "view_curses.hh"

/**
 * VT52 emulator for curses, useful for mediating between curses and readline,
 * which don't play well together.  It is expected that a subclass of this
 * class will fork off a child process that sends and receives VT52 keycodes(?)
 * which is translated by this class into curses calls.
 *
 * VT52 seems to be the simplest terminal to emulate since we do not need to
 * maintain the state of the screen, beyond past lines.  For example, when
 * inserting a character, VT52 moves the cursor to the insertion point, clears
 * the rest of the line and then rewrites the rest of the line with the new
 * character.  This is in contrast to VT100 which moves the cursor to the
 * insertion point and then sends a code to insert the character and relying
 * on the terminal to shift the rest of the line to the right a character.
 */
class vt52_curses : public view_curses {
public:
    /** @param win The curses window this view is attached to. */
    void set_window(ncplane* win) { this->vc_window = win; }

    /** @return The curses window this view is attached to. */
    ncplane* get_window() { return this->vc_window; }

    /** @param x The X position of the cursor on the curses display. */
    void set_cursor_x(int x) { this->vc_cursor_x = x; }

    /** @return The X position of the cursor on the curses display. */
    int get_cursor_x() const { return this->vc_cursor_x; }

    /**
     * @return The height of this view, which consists of a single line for
     * input, plus any past lines of output, which will appear ABOVE the Y
     * position for this view.
     * @todo Kinda hardwired to the way readline works.
     */
    int get_height() { return 1; }

    void set_max_height(int mh) { this->vc_max_height = mh; }
    int get_max_height() const { return this->vc_max_height; }

    /**
     * Map an ncurses input keycode to a vt52 sequence.
     *
     * @param ch The input character.
     * @param len_out The length of the returned sequence.
     * @return The vt52 sequence to send to the child.
     */
    string_fragment map_input(const ncinput& ch);

    /**
     * Map VT52 output to ncurses calls.
     *
     * @param output VT52 encoded output from the child process.
     * @param len The length of the output array.
     */
    void map_output(const char* output, int len);

    /**
     * Paints any past lines and moves the cursor to the current X position.
     */
    bool do_update() override;

    const static char ESCAPE = 27; /*< VT52 Escape key value. */
    const static char BACKSPACE = 8; /*< VT52 Backspace key value. */
    const static char BELL = 7; /*< VT52 Bell value. */
    const static char STX = 2; /*< VT52 Start-of-text value. */

protected:
    /** @return The absolute Y position of this view. */
    int get_actual_y()
    {
        unsigned int width, height;
        int retval;

        ncplane_dim_yx(this->vc_window, &height, &width);
        if (this->vc_y < 0) {
            retval = height + this->vc_y;
        } else {
            retval = this->vc_y;
        }

        return retval;
    }

    int get_actual_width()
    {
        auto retval = ncplane_dim_x(this->vc_window);

        if (this->vc_width < 0) {
            retval -= this->vc_x;
            retval += this->vc_width;
        } else if (this->vc_width > 0) {
            retval = this->vc_width;
        } else {
            retval = retval - this->vc_x;
        }
        return retval;
    }

    ncplane* vc_window{nullptr}; /*< The window that contains this view. */
    int vc_cursor_x{0}; /*< The X position of the cursor. */
    int vc_max_height{0};
    char vc_escape[16]; /*< Storage for escape sequences. */
    int vc_escape_len{0}; /*< The number of chars in vc_escape. */
    int vc_expected_escape_len{-1};
    char vc_map_buffer{0}; /*<
                            * Buffer returned by map_input for trivial
                            * translations (one-to-one).
                            */
    attr_line_t vc_line;
};

#endif
