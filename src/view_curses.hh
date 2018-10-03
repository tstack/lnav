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
#include <stdint.h>
#include <limits.h>
#include <signal.h>
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

#include "lnav_log.hh"
#include "attr_line.hh"

#define KEY_CTRL_G    7
#define KEY_CTRL_L    12
#define KEY_CTRL_P    16
#define KEY_CTRL_R    18
#define KEY_CTRL_W    23
#define KEY_CTRL_RBRACKET 0x1d

class view_curses;

/**
 * An RAII class that initializes and deinitializes curses.
 */
class screen_curses : public log_crash_recoverer {
public:
    void log_crash_recover() override {
        endwin();
    };

    screen_curses()
        : sc_main_window(initscr()) {
    };

    virtual ~screen_curses()
    {
        endwin();
    };

    WINDOW *get_window() { return this->sc_main_window; };

private:
    WINDOW *sc_main_window;
};

class ui_periodic_timer {
public:
    static const struct itimerval INTERVAL;

    static ui_periodic_timer &singleton();

    bool time_to_update(sig_atomic_t &counter) const {
        if (this->upt_counter != counter) {
            counter = this->upt_counter;
            return true;
        }
        return false;
    };

    void start_fade(sig_atomic_t &counter, size_t decay) {
        counter = this->upt_counter + decay;
    };

    int fade_diff(sig_atomic_t &counter) {
        if (this->upt_counter >= counter) {
            return 0;
        }
        return counter - this->upt_counter;
    };

private:
    ui_periodic_timer();

    static void sigalrm(int sig);

    volatile sig_atomic_t upt_counter;
};

class alerter {

public:
    static alerter &singleton();

    void enabled(bool enable) { this->a_enabled = enable; };

    void chime(void) {
        if (!this->a_enabled) {
            return;
        }

        if (this->a_do_flash) {
            ::flash();
        }
        this->a_do_flash = false;
    };

    void new_input(int ch) {
        if (this->a_last_input != ch) {
            this->a_do_flash = true;
        }
        this->a_last_input = ch;
    };

private:
    alerter() : a_enabled(true), a_do_flash(true), a_last_input(-1) { };

    bool a_enabled;
    bool a_do_flash;
    int a_last_input;
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
        virtual ~broadcaster() = default;

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
    view_action(void(*invoker)(void *, _Sender *) = nullptr)
        : va_functor(nullptr),
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

struct rgb_color {
    static bool from_str(const string_fragment &color,
                         rgb_color &rgb_out,
                         std::string &errmsg);

    explicit rgb_color(short r = -1, short g = -1, short b = -1)
        : rc_r(r), rc_g(g), rc_b(b) {
    }

    bool empty() const {
        return this->rc_r == -1 && this->rc_g == -1 && this->rc_b == -1;
    }

    short rc_r;
    short rc_g;
    short rc_b;
};

struct lab_color {
    lab_color() : lc_l(0), lc_a(0), lc_b(0) {
    };

    lab_color(const rgb_color &rgb);

    double deltaE(const lab_color &other) const;

    lab_color& operator=(const lab_color &other) {
        this->lc_l = other.lc_l;
        this->lc_a = other.lc_a;
        this->lc_b = other.lc_b;

        return *this;
    };

    double lc_l;
    double lc_a;
    double lc_b;
};

/**
 * Singleton used to manage the colorspace.
 */
class view_colors {
public:
    static const unsigned long BASIC_COLOR_COUNT = 8;
    static const unsigned long HI_COLOR_COUNT = 6 * 3 * 3;

    static attr_t BASIC_HL_PAIRS[BASIC_COLOR_COUNT];

    /** Roles that can be mapped to curses attributes using attrs_for_role() */
    typedef enum {
        VCR_NONE = -1,

        VCR_TEXT,               /*< Raw text. */
        VCR_SEARCH,             /*< A search hit. */
        VCR_OK,
        VCR_ERROR,              /*< An error message. */
        VCR_WARNING,            /*< A warning message. */
        VCR_ALT_ROW,            /*< Highlight for alternating rows in a list */
        VCR_HIDDEN,
        VCR_ADJUSTED_TIME,
        VCR_SKEWED_TIME,
        VCR_OFFSET_TIME,
        VCR_STATUS,             /*< Normal status line text. */
        VCR_WARN_STATUS,
        VCR_ALERT_STATUS,       /*< Alert status line text. */
        VCR_ACTIVE_STATUS,      /*< */
        VCR_ACTIVE_STATUS2,     /*< */
        VCR_BOLD_STATUS,
        VCR_VIEW_STATUS,

        VCR_KEYWORD,
        VCR_STRING,
        VCR_COMMENT,
        VCR_VARIABLE,
        VCR_SYMBOL,
        VCR_RE_SPECIAL,
        VCR_RE_REPEAT,
        VCR_FILE,

        VCR_DIFF_DELETE,        /*< Deleted line in a diff. */
        VCR_DIFF_ADD,           /*< Added line in a diff. */
        VCR_DIFF_SECTION,       /*< Section marker in a diff. */

        VCR_LOW_THRESHOLD,
        VCR_MED_THRESHOLD,
        VCR_HIGH_THRESHOLD,

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

    void init_roles(int color_pair_base);

    /**
     * @param role The role to retrieve character attributes for.
     * @return The attributes to use for the given role.
     */
    attr_t attrs_for_role(role_t role) const
    {
        require(role >= 0);
        require(role < VCR__MAX);

        return this->vc_role_colors[role];
    };

    attr_t reverse_attrs_for_role(role_t role) const
    {
        require(role >= 0);
        require(role < VCR__MAX);

        return this->vc_role_reverse_colors[role];
    };

    attr_t attrs_for_ident(const char *str, size_t len) const {
        unsigned long index = crc32(1, (const Bytef*)str, len);
        attr_t retval;

        if (COLORS >= 256) {
            unsigned long offset = index % HI_COLOR_COUNT;
            retval = COLOR_PAIR(VC_ANSI_END + offset);
        }
        else {
            retval = BASIC_HL_PAIRS[index % BASIC_COLOR_COUNT];
        }

        return retval;
    };

    attr_t attrs_for_ident(const std::string &str) const {
        return this->attrs_for_ident(str.c_str(), str.length());
    };

    int ensure_color_pair(const rgb_color &fg, const rgb_color &bg);

    static inline int ansi_color_pair_index(int fg, int bg)
    {
        return VC_ANSI_START + ((fg * 8) + bg);
    };

    static inline attr_t ansi_color_pair(int fg, int bg)
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

    static bool initialized;

    /** Map of role IDs to attribute values. */
    attr_t vc_role_colors[VCR__MAX];
    /** Map of role IDs to reverse-video attribute values. */
    attr_t vc_role_reverse_colors[VCR__MAX];
    int vc_color_pair_end;

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
                int y = -1)
            : me_button(button),
              me_state(state),
              me_x(x),
              me_y(y) {
        memset(&this->me_time, 0, sizeof(this->me_time));
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

    static string_attr_type VC_STYLE;
    static string_attr_type VC_GRAPHIC;

    static void mvwattrline(WINDOW *window,
                            int y,
                            int x,
                            attr_line_t &al,
                            const struct line_range &lr,
                            view_colors::role_t base_role =
                                view_colors::VCR_TEXT);
};
#endif
