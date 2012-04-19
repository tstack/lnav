/**
 * @file vt52_curses.hh
 */

#ifndef __vt52_curses_hh
#define __vt52_curses_hh

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
class vt52_curses
    : public view_curses {
public:
    vt52_curses();
    virtual ~vt52_curses();

    /** @param win The curses window this view is attached to. */
    void set_window(WINDOW *win) { this->vc_window = win; };

    /** @return The curses window this view is attached to. */
    WINDOW *get_window() { return this->vc_window; };

    /**
     * Set the Y position of this view on the display.  A value greater than
     * zero is considered to be an absolute size.  A value less than zero makes
     * the position relative to the bottom of the enclosing window.
     *
     * @param y The Y position of the cursor on the curses display.
     */
    void set_y(int y) { this->vc_y = y; };

    /** @return The abs/rel Y position of the cursor on the curses display. */
    int get_y() { return this->vc_y; };

    /** @param x The X position of the cursor on the curses display. */
    void set_x(int x) { this->vc_x = x; };

    /** @return The X position of the cursor on the curses display. */
    int get_x() { return this->vc_x; };

    /**
     * @return The height of this view, which consists of a single line for
     * input, plus any past lines of output, which will appear ABOVE the Y
     * position for this view.
     * @todo Kinda hardwired to the way readline works.
     */
    int get_height() { return this->vc_past_lines.size() + 1; };

    void set_max_height(int mh) { this->vc_max_height = mh; };
    int get_max_height() { return this->vc_max_height; };

    /**
     * Map an ncurses input keycode to a vt52 sequence.
     *
     * @param ch The input character.
     * @param len_out The length of the returned sequence.
     * @return The vt52 sequence to send to the child.
     */
    const char *map_input(int ch, int &len_out);

    /**
     * Map VT52 output to ncurses calls.
     *
     * @param output VT52 encoded output from the child process.
     * @param len The length of the output array.
     */
    void map_output(const char *output, int len);

    /**
     * Paints any past lines and moves the cursor to the current X position.
     */
    void do_update(void);

    const static char ESCAPE    = 27;   /*< VT52 Escape key value. */
    const static char BACKSPACE = 8;    /*< VT52 Backspace key value. */
    const static char BELL      = 7;    /*< VT52 Bell value. */
    const static char STX       = 2;    /*< VT52 Start-of-text value. */

protected:

    /** @return The absolute Y position of this view. */
    int get_actual_y()
    {
	unsigned long width, height;
	int           retval;

	getmaxyx(this->vc_window, height, width);
	if (this->vc_y < 0) {
	    retval = height + this->vc_y;
	}
	else {
	    retval = this->vc_y;
	}

	return retval;
    };

    WINDOW *vc_window;        /*< The window that contains this view. */
    int    vc_x;              /*< The X position of the cursor. */
    int    vc_y;              /*< The Y position of the cursor. */
    int    vc_max_height;
    char   vc_escape[16];     /*< Storage for escape sequences. */
    int    vc_escape_len;     /*< The number of chars in vc_escape. */
    char   vc_map_buffer;     /*<
			       * Buffer returned by map_input for trivial
			       * translations (one-to-one).
			       */

    /** Vector of past lines of output from the child. */
    std::list<std::string> vc_past_lines;
};

#endif
