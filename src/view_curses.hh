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
 * @file view_curses.hh
 */

#ifndef __view_curses_hh
#define __view_curses_hh

#include "config.h"

#include <zlib.h>
#include <assert.h>
#include <stdint.h>
#include <limits.h>
#include <sys/time.h>

#if defined HAVE_NCURSESW_CURSES_H
#  include <ncursesw/curses.h>
#elif defined HAVE_NCURSESW_H
#  include <ncursesw.h>
#elif defined HAVE_NCURSES_CURSES_H
#  include <ncurses/curses.h>
#elif defined HAVE_NCURSES_H
#  include <ncurses.h>
#elif defined HAVE_CURSES_H
#  include <curses.h>
#else
#  error "SysV or X/Open-compatible Curses header file required"
#endif

#include <map>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>

#define KEY_CTRL_G    7
#define KEY_CTRL_R    18
#define KEY_CTRL_W    23
#define KEY_CTRL_RBRACKET 0x1d

class view_curses;

/**
 * An RAII class that initializes and deinitializes curses.
 */
class screen_curses {
public:
    screen_curses()
        : sc_main_window(initscr()) {};
    virtual ~screen_curses()
    {
        endwin();
    };

    WINDOW *get_window() { return this->sc_main_window; };

private:
    WINDOW *sc_main_window;
};

class alerter {

public:
    static alerter &singleton();

    void chime(void) {
        if (this->a_do_flash)
            ::flash();
        this->a_do_flash = false;
    };

    void new_input(int ch) {
        if (this->a_last_input != ch) {
            this->a_do_flash = true;
        }
        this->a_last_input = ch;
    };

private:
    alerter() : a_do_flash(true), a_last_input(-1) { };

    bool a_do_flash;
    int a_last_input;
};

/**
 * Encapsulates a range in a string.
 */
struct line_range {
    int lr_start;
    int lr_end;

    line_range(int start = -1, int end = -1) : lr_start(start), lr_end(end) { };

    int length() const
    {
        return this->lr_end == -1 ? INT_MAX : this->lr_end - this->lr_start;
    };

    bool contains(int pos) const {
        return this->lr_start <= pos && pos < this->lr_end;
    };

    bool operator<(const struct line_range &rhs) const
    {
        if (this->lr_start < rhs.lr_start) { return true; }
        else if (this->lr_start > rhs.lr_start) { return false; }

        if (this->lr_end == rhs.lr_end) { return false; }

        if (this->lr_end < rhs.lr_end) { return true; }
        return false;
    };
};

/**
 * Container for attribute values for a substring.
 */
typedef union {
    void *sa_ptr;
    int   sa_int;
} string_attr_t;

/**
 * Construct a string_attr_t the a void pointer value.
 *
 * @param name The name of the attribute.
 * @param val The value to store in the attribute.
 */
inline std::pair<std::string, string_attr_t>
make_string_attr(const std::string &name, void *val)
{
    string_attr_t sa;

    sa.sa_ptr = val;

    return std::make_pair(name, sa);
}

/**
 * Construct a string_attr_t the an integer value.
 *
 * @param name The name of the attribute.
 * @param val The value to store in the attribute.
 */
inline std::pair<std::string, string_attr_t>
make_string_attr(const std::string &name, int val)
{
    string_attr_t sa;

    sa.sa_int = val;

    return std::make_pair(name, sa);
}

/** A map of symbolic names to attribute values. */
typedef std::multimap<std::string, string_attr_t> attrs_map_t;
/** A map of line ranges to attributes for that range. */
typedef std::map<struct line_range, attrs_map_t>  string_attrs_t;

inline struct line_range
find_string_attr_range(const string_attrs_t &sa, const std::string &name)
{
    struct line_range retval;

    for (string_attrs_t::const_iterator iter = sa.begin();
         iter != sa.end();
         ++iter) {
        if (iter->second.find(name) != iter->second.end()) {
            retval = iter->first;
            break;
        }
    }

    return retval;
}

/**
 * A line that has attributes.
 */
class attr_line_t {
public:
    attr_line_t() { };
    attr_line_t(const std::string &str) : al_string(str) { };

    /** @return The string itself. */
    std::string &get_string() { return this->al_string; };

    /** @return The attributes for the string. */
    string_attrs_t &get_attrs() { return this->al_attrs; };

    size_t length() const { return this->al_string.length(); };

    void operator=(const std::string &rhs) { this->al_string = rhs; };

    /** Clear the string and the attributes for the string. */
    void clear()
    {
        this->al_string.clear();
        this->al_attrs.clear();
    };

private:
    std::string    al_string;
    string_attrs_t al_attrs;
};

/**
 * Class that encapsulates a method to execute and the object on which to
 * execute it.
 *
 * @param _Sender The type of object that will be triggering an action.
 */
template<class _Sender>
class view_action {
public:

    /**
     *
     * @param _Receiver The type of object that will be triggered by an action.
     */
    template<class _Receiver>
    class mem_functor_t {
public:
        mem_functor_t(_Receiver &receiver,
                      void(_Receiver::*selector)(_Sender *))
            : mf_receiver(receiver),
              mf_selector(selector) { };

        void operator()(_Sender *sender) const
        {
            (this->mf_receiver.*mf_selector)(sender);
        };

        static void invoke(mem_functor_t *self, _Sender *sender)
        {
            (*self)(sender);
        };

private:
        _Receiver & mf_receiver;
        void        (_Receiver::*mf_selector)(_Sender *);
    };

    class broadcaster
        : public std::vector<view_action> {
public:

        broadcaster()
            : b_functor(*this, &broadcaster::invoke) { };
        virtual ~broadcaster() { };

        void invoke(_Sender *sender)
        {
            typename std::vector<view_action>::iterator iter;

            for (iter = this->begin(); iter != this->end(); ++iter) {
                (*iter).invoke(sender);
            }
        };

        mem_functor_t<broadcaster> *get_functor()
        {
            return &this->b_functor;
        };

private:
        mem_functor_t<broadcaster> b_functor;
    };

    /**
     * @param receiver The object to pass as the first argument to the selector
     * function.
     * @param selector The function to execute.  The function should take two
     * parameters, the first being the value of the receiver pointer and the
     * second being the sender pointer as passed to invoke().
     */
    view_action(void(*invoker)(void *, _Sender *) = NULL)
        : va_functor(NULL),
          va_invoker(invoker) { };

    template<class _Receiver>
    view_action(mem_functor_t<_Receiver> *mf)
        : va_functor(mf),
          va_invoker((void(*) (void *, _Sender *))
                     mem_functor_t<_Receiver>::invoke) { };

    /**
     * Performs a shallow copy of another view_action.
     *
     * @param va The view_action to copy the receiver and selector pointers
     * from.
     */
    view_action(const view_action &va)
        : va_functor(va.va_functor),
          va_invoker(va.va_invoker) { };

    ~view_action() { };

    /**
     * @param rhs The view_action to shallow copy.
     * @return *this
     */
    view_action &operator=(const view_action &rhs)
    {
        this->va_functor = rhs.va_functor;
        this->va_invoker = rhs.va_invoker;

        return *this;
    };

    /**
     * Invoke the action by calling the selector function, if one is set.
     *
     * @param sender Pointer to the object that called this method.
     */
    void invoke(_Sender *sender)
    {
        if (this->va_invoker != NULL) {
            this->va_invoker(this->va_functor, sender);
        }
    };

private:

    /** The object to pass as the first argument to the selector function.*/
    void *va_functor;
    /** The function to call when this action is invoke()'d. */
    void (*va_invoker)(void *functor, _Sender *sender);
};

/**
 * Singleton used to manage the colorspace.
 */
class view_colors {
public:
    /** The number of colors used for highlighting. */
    static const int HL_BASIC_COLOR_COUNT = 4;
    static const int HL_COLOR_COUNT = 2 * HL_BASIC_COLOR_COUNT + 9 * 6;

    /** Roles that can be mapped to curses attributes using attrs_for_role() */
    typedef enum {
        VCR_NONE = -1,

        VCR_TEXT,               /*< Raw text. */
        VCR_SEARCH,             /*< A search hit. */
        VCR_OK,
        VCR_ERROR,              /*< An error message. */
        VCR_WARNING,            /*< A warning message. */
        VCR_ALT_ROW,            /*< Highlight for alternating rows in a list */
        VCR_ADJUSTED_TIME,
        VCR_STATUS,             /*< Normal status line text. */
        VCR_WARN_STATUS,
        VCR_ALERT_STATUS,       /*< Alert status line text. */
        VCR_ACTIVE_STATUS,      /*< */
        VCR_ACTIVE_STATUS2,     /*< */
        VCR_BOLD_STATUS,

        VCR_KEYWORD,
        VCR_STRING,
        VCR_COMMENT,
        VCR_VARIABLE,

        VCR_DIFF_DELETE,        /*< Deleted line in a diff. */
        VCR_DIFF_ADD,           /*< Added line in a diff. */
        VCR_DIFF_SECTION,       /*< Section marker in a diff. */

        VCR_HIGHLIGHT_START,
        VCR_HIGHLIGHT_END = VCR_HIGHLIGHT_START + HL_COLOR_COUNT,

        VCR__MAX
    } role_t;

    /** @return A reference to the singleton. */
    static view_colors &singleton();

    /**
     * Performs curses-specific initialization.  The other methods can be
     * called before this method, but the returned attributes cannot be used
     * with curses code until this method is called.
     */
    static void init(void);

    void init_roles(void);

    /**
     * @param role The role to retrieve character attributes for.
     * @return The attributes to use for the given role.
     */
    int attrs_for_role(role_t role) const
    {
        assert(role >= 0);
        assert(role < VCR__MAX);

        return this->vc_role_colors[role];
    };

    int reverse_attrs_for_role(role_t role) const
    {
        assert(role >= 0);
        assert(role < VCR__MAX);

        return this->vc_role_reverse_colors[role];
    };

    /**
     * @return The next set of attributes to use for highlighting text.  This
     * method will iterate through eight-or-so attributes combinations so there
     * is some variety in how text is highlighted.
     */
    role_t next_highlight();

    role_t next_plain_highlight();

    int attrs_for_ident(const char *str, size_t len) const {
        int index = crc32(1, (const Bytef*)str, len);

        return this->vc_role_colors[VCR_HIGHLIGHT_START + (abs(index) % HL_COLOR_COUNT)];
    };

    static inline int ansi_color_pair_index(int fg, int bg)
    {
        return VC_ANSI_START + ((fg * 8) + bg);
    };

    static inline int ansi_color_pair(int fg, int bg)
    {
        return COLOR_PAIR(ansi_color_pair_index(fg, bg));
    };

    enum {
        VC_ANSI_START = 0,
        VC_ANSI_END = VC_ANSI_START + (8 * 8),
    };

private:

    /** Private constructor that initializes the member fields. */
    view_colors();

    /** Map of role IDs to attribute values. */
    int vc_role_colors[VCR__MAX];
    /** Map of role IDs to reverse-video attribute values. */
    int vc_role_reverse_colors[VCR__MAX];
    /** The index of the next highlight color to use. */
    int vc_next_highlight;
    int vc_next_plain_highlight;
};

enum mouse_button_t {
    BUTTON_LEFT,
    BUTTON_MIDDLE,
    BUTTON_RIGHT,

    BUTTON_SCROLL_UP,
    BUTTON_SCROLL_DOWN,
};

enum mouse_button_state_t {
    BUTTON_STATE_PRESSED,
    BUTTON_STATE_DRAGGED,
    BUTTON_STATE_RELEASED,
};

struct mouse_event {
    mouse_event(mouse_button_t button = BUTTON_LEFT,
                mouse_button_state_t state = BUTTON_STATE_PRESSED,
                int x = -1,
                int y = -1) : me_button(button), me_state(state), me_x(x), me_y(y) {
    };

    mouse_button_t me_button;
    mouse_button_state_t me_state;
    struct timeval me_time;
    int me_x;
    int me_y;
};

/**
 * Interface for "view" classes that will update a curses(3) display.
 */
class view_curses {
public:
    virtual ~view_curses() { };

    /**
     * Update the curses display.
     */
    virtual void do_update(void) = 0;

    virtual bool handle_mouse(mouse_event &me) { return false; };

    static void mvwattrline(WINDOW *window,
                            int y,
                            int x,
                            attr_line_t &al,
                            struct line_range &lr,
                            view_colors::role_t base_role =
                                view_colors::VCR_TEXT);
};
#endif
