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
 * @file statusview_curses.hh
 */

#ifndef __statusview_curses_hh
#define __statusview_curses_hh

#include <string>
#include <vector>

#include "view_curses.hh"
#include "ansi_scrubber.hh"

/**
 * Container for individual status values.
 */
class status_field {
public:

    /**
     * @param width The maximum width of the field in characters.
     * @param role The color role for this field, defaults to VCR_STATUS.
     */
    status_field(int width = 1,
                 view_colors::role_t role = view_colors::VCR_STATUS)
        : sf_width(width),
          sf_min_width(0),
          sf_right_justify(false),
          sf_cylon(false),
          sf_cylon_pos(0),
          sf_role(role),
          sf_share(0) { };

    virtual ~status_field() { };

    /** @param value The new value for this field. */
    void set_value(std::string value)
    {
        string_attrs_t &sa = this->sf_value.get_attrs();

        sa.clear();

        scrub_ansi_string(value, sa);

        if (value.size() > this->get_width()) {
            if (value.size() <= 11) {
                value.resize(11);
            }
            else {
                static const std::string MIDSUB = " .. ";

                size_t half_width = this->get_width() / 2 -
                                    MIDSUB.size() / 2;
                std::string abbrev;

                abbrev.append(value, 0, half_width);
                abbrev.append(MIDSUB);
                abbrev.append(value,
                              value.size() - half_width,
                              std::string::npos);
                value = abbrev;
            }
        }

        if (this->sf_right_justify) {
            int padding = this->sf_width - value.size();

            if (padding > 2) {
                value.insert(0, padding, ' ');
            }
        }

        this->sf_value.with_string(value);

        if (this->sf_cylon) {
            struct line_range lr(this->sf_cylon_pos, this->sf_width);

            sa.push_back(string_attr(lr, &view_curses::VC_STYLE,
                view_colors::ansi_color_pair(COLOR_WHITE, COLOR_GREEN) | A_BOLD));

            this->sf_cylon_pos += 1;
            if (this->sf_cylon_pos > this->sf_width) {
                this->sf_cylon_pos = 0;
            }
        }
    };

    /**
     * Set the new value for this field using a formatted string.
     *
     * @param fmt The format string.
     * @param ... Arguments for the format.
     */
    void set_value(const char *fmt, ...)
    {
        char    buffer[128];
        va_list args;

        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        this->set_value(std::string(buffer));
        va_end(args);
    };

    void set_stitch_value(int color_pair)
    {
        string_attrs_t &  sa = this->sf_value.get_attrs();
        struct line_range lr(0, 1);

        this->sf_value.get_string() = "::";
        sa.push_back(string_attr(lr, &view_curses::VC_STYLE,
            A_REVERSE | COLOR_PAIR(color_pair)));
        lr.lr_start = 1;
        lr.lr_end   = 2;
        sa.push_back(string_attr(lr, &view_curses::VC_STYLE, COLOR_PAIR(color_pair)));
    };

    /** @return The string value for this field. */
    attr_line_t &get_value() { return this->sf_value; };

    void right_justify(bool yes) { this->sf_right_justify = yes; };
    bool is_right_justified(void) const { return this->sf_right_justify; };

    void set_cylon(bool yes) { this->sf_cylon = yes; };
    bool is_cylon(void) const { return this->sf_cylon; };

    /** @return True if this field's value is an empty string. */
    bool empty() { return this->sf_value.get_string().empty(); };

    void clear() { this->sf_value.clear(); };

    /** @param role The color role for this field. */
    void set_role(view_colors::role_t role) { this->sf_role = role; };
    /** @return The color role for this field. */
    view_colors::role_t get_role() const { return this->sf_role; };

    /** @param width The maximum display width, in characters. */
    void set_width(int width) { this->sf_width = width; };
    /** @param width The maximum display width, in characters. */
    size_t get_width() const { return this->sf_width; };

    /** @param width The maximum display width, in characters. */
    void set_min_width(int width) { this->sf_min_width = width; };
    /** @param width The maximum display width, in characters. */
    size_t get_min_width() const { return this->sf_min_width; };

    void set_share(int share) { this->sf_share = share; };
    int get_share() const { return this->sf_share; };

protected:
    size_t              sf_width;     /*< The maximum display width, in chars. */
    size_t              sf_min_width; /*< The minimum display width, in chars. */
    bool                sf_right_justify;
    bool                sf_cylon;
    size_t              sf_cylon_pos;
    attr_line_t         sf_value; /*< The value to display for this field. */
    view_colors::role_t sf_role;  /*< The color role for this field. */
    int sf_share;
};

/**
 * Data source for the fields to be displayed in a status view.
 */
class status_data_source {
public:
    virtual ~status_data_source() { };

    /**
     * @return The number of status_fields in this source.
     */
    virtual size_t statusview_fields(void) = 0;

    /**
     * Callback used to get a particular field.
     *
     * @param field The index of the field to return.
     * @return A reference to the field at the given index.
     */
    virtual status_field &statusview_value_for_field(int field) = 0;
};

/**
 * A view that displays a collection of fields in a line on the display.
 */
class statusview_curses
    : public view_curses {
public:
    statusview_curses()
        : sc_source(NULL),
          sc_window(NULL),
          sc_top(0) { };
    virtual ~statusview_curses() { };

    void set_data_source(status_data_source *src) { this->sc_source = src; };
    status_data_source *get_data_source() { return this->sc_source; };

    void set_top(int top) { this->sc_top = top; };
    int get_top() const { return this->sc_top; };

    void set_window(WINDOW *win) { this->sc_window = win; };
    WINDOW *get_window() { return this->sc_window; };

    void window_change(void)
    {
        int           field_count = this->sc_source->statusview_fields();
        int           remaining, total_shares = 0;
        unsigned long width, height;

        getmaxyx(this->sc_window, height, width);
        // Silence the compiler. Remove this if height is used at a later stage.
        (void)height;
        remaining = width - 4;
        for (int field = 0; field < field_count; field++) {
            status_field &sf = this->sc_source->statusview_value_for_field(
                field);

            remaining -=
                sf.get_share() ? sf.get_min_width() : sf.get_width();
            remaining    -= 1;
            total_shares += sf.get_share();
        }

        if (remaining < 2) {
            remaining = 0;
        }

        for (int field = 0; field < field_count; field++) {
            status_field &sf = this->sc_source->statusview_value_for_field(
                field);

            if (sf.get_share()) {
                int actual_width;

                actual_width  = sf.get_min_width();
                actual_width += remaining / (sf.get_share() / total_shares);

                sf.set_width(actual_width);
            }
        }
    };

    void do_update(void);

private:
    status_data_source *sc_source;
    WINDOW *            sc_window;
    int sc_top;
};
#endif
