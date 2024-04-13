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
 * @file statusview_curses.hh
 */

#ifndef statusview_curses_hh
#define statusview_curses_hh

#include <string>
#include <vector>

#include "view_curses.hh"

/**
 * Container for individual status values.
 */
class status_field {
public:
    using action = std::function<void(status_field&)>;

    /**
     * @param width The maximum width of the field in characters.
     * @param role The color role for this field, defaults to VCR_STATUS.
     */
    status_field(int width = 1, role_t role = role_t::VCR_STATUS)
        : sf_width(width), sf_role(role)
    {
    }

    virtual ~status_field() = default;

    /** @param value The new value for this field. */
    void set_value(std::string value);

    /**
     * Set the new value for this field using a formatted string.
     *
     * @param fmt The format string.
     * @param ... Arguments for the format.
     */
    status_field& set_value(const char* fmt, ...)
    {
        char buffer[256];
        va_list args;

        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        this->set_value(std::string(buffer));
        va_end(args);

        return *this;
    }

    void set_stitch_value(role_t left, role_t right);

    void set_left_pad(size_t val) { this->sf_left_pad = val; }

    size_t get_left_pad() const { return this->sf_left_pad; }

    /** @return The string value for this field. */
    attr_line_t& get_value() { return this->sf_value; }

    void right_justify(bool yes) { this->sf_right_justify = yes; }
    bool is_right_justified() const { return this->sf_right_justify; }

    status_field& set_cylon(bool yes)
    {
        this->sf_cylon = yes;
        return *this;
    }

    bool is_cylon() const { return this->sf_cylon; }

    void do_cylon();

    /** @return True if this field's value is an empty string. */
    bool empty() const { return this->sf_value.get_string().empty(); }

    void clear() { this->sf_value.clear(); }

    /** @param role The color role for this field. */
    void set_role(role_t role) { this->sf_role = role; }
    /** @return The color role for this field. */
    role_t get_role() const { return this->sf_role; }

    /** @param width The maximum display width, in characters. */
    void set_width(ssize_t width) { this->sf_width = width; }
    /** @param width The maximum display width, in characters. */
    ssize_t get_width() const { return this->sf_width; }

    /** @param width The maximum display width, in characters. */
    void set_min_width(int width) { this->sf_min_width = width; }
    /** @param width The maximum display width, in characters. */
    size_t get_min_width() const { return this->sf_min_width; }

    void set_share(int share) { this->sf_share = share; }

    int get_share() const { return this->sf_share; }

    static void no_op_action(status_field&);

    action on_click{no_op_action};

protected:
    ssize_t sf_width; /*< The maximum display width, in chars. */
    ssize_t sf_min_width{0}; /*< The minimum display width, in chars. */
    bool sf_right_justify{false};
    bool sf_cylon{false};
    ssize_t sf_cylon_pos{0};
    attr_line_t sf_value; /*< The value to display for this field. */
    role_t sf_role; /*< The color role for this field. */
    int sf_share{0};
    size_t sf_left_pad{0};
};

/**
 * Data source for the fields to be displayed in a status view.
 */
class status_data_source {
public:
    virtual ~status_data_source() = default;

    /**
     * @return The number of status_fields in this source.
     */
    virtual size_t statusview_fields() = 0;

    /**
     * Callback used to get a particular field.
     *
     * @param field The index of the field to return.
     * @return A reference to the field at the given index.
     */
    virtual status_field& statusview_value_for_field(int field) = 0;
};

/**
 * A view that displays a collection of fields in a line on the display.
 */
class statusview_curses : public view_curses {
public:
    void set_data_source(status_data_source* src) { this->sc_source = src; }
    status_data_source* get_data_source() { return this->sc_source; }

    void set_window(WINDOW* win) { this->sc_window = win; }
    WINDOW* get_window() { return this->sc_window; }

    void set_enabled(bool value) { this->sc_enabled = value; }
    bool get_enabled() const { return this->sc_enabled; }

    void set_default_role(role_t role) { this->sc_default_role = role; }
    role_t get_default_role() const { return this->sc_default_role; }

    void window_change();

    bool do_update() override;

    bool handle_mouse(mouse_event& me) override;

private:
    status_data_source* sc_source{nullptr};
    WINDOW* sc_window{nullptr};
    bool sc_enabled{true};
    role_t sc_default_role{role_t::VCR_STATUS};

    struct displayed_field {
        displayed_field(line_range lr, size_t field_index)
            : df_range(lr), df_field_index(field_index)
        {
        }

        line_range df_range;
        size_t df_field_index;
    };

    std::vector<displayed_field> sc_displayed_fields;
};

#endif
