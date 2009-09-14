/**
 * @file statusview_curses.hh
 */

#ifndef __statusview_curses_hh
#define __statusview_curses_hh

#include <curses.h>

#include <string>
#include <vector>

#include "view_curses.hh"

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
	  sf_right_justify(false),
	  sf_cylon(false),
	  sf_cylon_pos(0),
	  sf_role(role) { };

    virtual ~status_field() { };

    /** @param value The new value for this field. */
    void set_value(std::string value) {
	if (value.size() > this->get_width()) {
	    if (value.size() <= 11) {
		value.resize(11);
	    }
	    else {
		static const std::string MIDSUB = " .. ";

		size_t half_width = this->get_width() / 2 - MIDSUB.size() / 2;
		std::string abbrev;
		
		abbrev.append(value, 0, half_width);
		abbrev.append(MIDSUB);
		abbrev.append(value,
			      value.size() - half_width,
			      std::string::npos);
		value = abbrev;
	    }
	}
	
	this->sf_value = value;

	string_attrs_t &sa = this->sf_value.get_attrs();
	sa.clear();
	
	if (this->sf_cylon) {
	    struct line_range lr = { this->sf_cylon_pos,
				     this->sf_width };
	    
	    sa[lr].insert(make_string_attr("style", COLOR_PAIR(view_colors::VC_WHITE_ON_GREEN) | A_BOLD));

	    this->sf_cylon_pos += 1;
	    if (this->sf_cylon_pos > this->sf_width)
		this->sf_cylon_pos = 0;
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

    /** @return The string value for this field. */
    attr_line_t &get_value() { return this->sf_value; };

    void right_justify(bool yes) { this->sf_right_justify = yes; };
    bool is_right_justified(void) { return this->sf_right_justify; };

    void set_cylon(bool yes) { this->sf_cylon = yes; };
    bool is_cylon(void) { return this->sf_cylon; };

    /** @return True if this field's value is an empty string. */
    bool empty() { return this->sf_value.get_string().empty(); };

    void clear() { this->sf_value.clear(); };

    /** @param role The color role for this field. */
    void set_role(view_colors::role_t role) { this->sf_role = role; };
    /** @return The color role for this field. */
    view_colors::role_t get_role() { return this->sf_role; };

    /** @param width The maximum display width, in characters. */
    void set_width(int width) { this->sf_width = width; };
    /** @param width The maximum display width, in characters. */
    int get_width() { return this->sf_width; };

protected:
    int                 sf_width; /*< The maximum display width, in chars. */
    bool                sf_right_justify;
    bool sf_cylon;
    int sf_cylon_pos;
    attr_line_t         sf_value; /*< The value to display for this field. */
    view_colors::role_t sf_role;  /*< The color role for this field. */
};

class telltale_field : public status_field {

public:
    
};

class status_data_source {
public:
    virtual ~status_data_source() { };

    virtual size_t statusview_fields(void) = 0;
    virtual status_field &statusview_value_for_field(int field) = 0;
};

class statusview_curses
    : public view_curses {
public:
    statusview_curses()
	: sc_window(NULL),
	  sc_top(0) { };
    virtual ~statusview_curses() { };

    void set_data_source(status_data_source *src) { this->sc_source = src; };
    status_data_source *get_data_source() { return this->sc_source; };

    void set_top(int top) { this->sc_top = top; };
    int get_top() { return this->sc_top; };

    void set_window(WINDOW *win) { this->sc_window = win; };
    WINDOW *get_window() { return this->sc_window; };

    void do_update(void);

private:
    status_data_source *sc_source;
    WINDOW             *sc_window;
    int sc_top;
};

#endif
