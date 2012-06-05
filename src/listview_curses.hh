/**
 * @file listview_curses.hh
 */

#ifndef __listview_curses_hh
#define __listview_curses_hh

#include <sys/types.h>

#include <string>
#include <algorithm>

#include "strong_int.hh"
#include "view_curses.hh"

/** Strongly-typed integer for visible lines. */
STRONG_INT_TYPE(int, vis_line);

class listview_curses;

/**
 * Data source for lines to be displayed by the listview_curses object.
 */
class list_data_source {
public:
    virtual ~list_data_source() { };

    /** @return The number of rows in the list. */
    virtual size_t listview_rows(const listview_curses &lv) = 0;

    /**
     * Get the string value for a row in the list.
     *
     * @param row The row number.
     * @param value_out The destination for the string value.
     */
    virtual void listview_value_for_row(const listview_curses &lv,
					vis_line_t row,
					attr_line_t &value_out) = 0;
};

/**
 * View that displays a list of lines that can optionally contain highlighting.
 */
class listview_curses
    : public view_curses {
public:
    typedef view_action<listview_curses> action;

    /** Construct an empty list view. */
    listview_curses();

    virtual ~listview_curses();

    /** @param src The data source delegate. */
    void set_data_source(list_data_source *src)
    {
	this->lv_source = src;
	this->reload_data();
    };

    /** @return The data source delegate. */
    list_data_source *get_data_source() { return this->lv_source; };

    /**
     * @param va The action to invoke when the view is scrolled.
     * @todo Allow multiple observers.
     */
    void set_scroll_action(action va) { this->lv_scroll = va; };

    template<class _Receiver>
    void set_scroll_action(action::mem_functor_t < _Receiver > *mf)
    {
	this->lv_scroll = action(mf);
    };

    void set_show_scrollbar(bool ss) { this->lv_show_scrollbar = ss; };
    bool get_show_scrollbar() { return this->lv_show_scrollbar; };

    /** @param win The curses window this view is attached to. */
    void set_window(WINDOW *win) { this->lv_window = win; };

    /** @return The curses window this view is attached to. */
    WINDOW *get_window() { return this->lv_window; };

    void set_y(unsigned int y)
    {
	if (y != this->lv_y) {
	    this->lv_y            = y;
	    this->lv_needs_update = true;
	}
    };
    unsigned int get_y() { return this->lv_y; };

    /**
     * Set the line number to be displayed at the top of the view.  If the
     * value is invalid, flash() will be called.  If the value is valid, the
     * new value will be set and the scroll action called.
     *
     * @param top The new value for top.
     * @param suppress_flash Don't call flash() if the top is out-of-bounds.
     */
    void set_top(vis_line_t top, bool suppress_flash = false)
    {
	if (top < 0 || (top > 0 && top >= this->get_inner_height())) {
	    if (!suppress_flash)
		flash();
	}
	else if (this->lv_top != top) {
	    this->lv_top = top;
	    this->lv_scroll.invoke(this);
	    this->lv_needs_update = true;
	}
    };

    /** @return The line number that is displayed at the top. */
    vis_line_t get_top() { return this->lv_top; };

    /** @return The line number that is displayed at the bottom. */
    vis_line_t get_bottom()
    {
	vis_line_t    retval, height;
	unsigned long width;

	this->get_dimensions(height, width);
	retval = std::min(this->lv_top + height - vis_line_t(1),
			  vis_line_t(this->get_inner_height() - 1));

	return retval;
    };

    /**
     * Shift the value of top by the given value.
     *
     * @param offset The amount to change top by.
     * @param suppress_flash Don't call flash() if the offset is out-of-bounds.
     * @return The final value of top.
     */
    vis_line_t shift_top(vis_line_t offset, bool suppress_flash = false)
    {
	if (offset < 0 && this->lv_top == 0) {
	    if (suppress_flash == false)
		flash();
	}
	else {
	    this->set_top(std::max(vis_line_t(0), this->lv_top + offset), suppress_flash);
	}

	return this->lv_top;
    };


    /**
     * Set the column number to be displayed at the left of the view.  If the
     * value is invalid, flash() will be called.  If the value is valid, the
     * new value will be set and the scroll action called.
     *
     * @param left The new value for left.
     */
    void set_left(unsigned int left)
    {
	if (this->lv_left != left) {
	    this->lv_left = left;
	    this->lv_scroll.invoke(this);
	    this->lv_needs_update = true;
	}
    };

    /** @return The column number that is displayed at the left. */
    unsigned int get_left() { return this->lv_left; };

    /**
     * Shift the value of left by the given value.
     *
     * @param offset The amount to change top by.
     * @return The final value of top.
     */
    unsigned int shift_left(int offset)
    {
    	if (offset < 0 && this->lv_left < (unsigned int)-offset) {
    		this->set_left(0);
    	}
    	else {
	    	this->set_left(this->lv_left + offset);
	}

	return this->lv_left;
    };

    /**
     * Set the height of the view.  A value greater than one is considered to
     * be an absolute size.  A value less than or equal to zero makes the
     * height relative to the size of the enclosing window.
     *
     * @height The new height.
     */
    void set_height(vis_line_t height)
    {
	if (this->lv_height != height) {
	    this->lv_height       = height;
	    this->lv_needs_update = true;
	}
    };

    /** @return The absolute or relative height of the window. */
    vis_line_t get_height() { return this->lv_height; };

    /** @return The number of rows of data in this view's source data. */
    vis_line_t get_inner_height() const
    {
	return vis_line_t(this->lv_source == NULL ? 0 :
		          this->lv_source->listview_rows(*this));
    };

    void set_needs_update() { this->lv_needs_update = true; };

    /**
     * Get the actual dimensions of the view.
     *
     * @param height_out The actual height of the view in lines.
     * @param width_out The actual width of the view in columns.
     */
    void get_dimensions(vis_line_t &height_out, unsigned long &width_out)
    {
	unsigned long height;

	getmaxyx(this->lv_window, height, width_out);
	if (this->lv_height < 1) {
	    height_out = vis_line_t(height) +
			 this->lv_height -
			 vis_line_t(this->lv_y);
	}
	else {
	    height_out = this->lv_height;
	}
    };

    /** This method should be called when the data source has changed. */
    void reload_data(void);

    /**
     * @param ch The input to be handled.
     * @return True if the key was eaten by this view.
     */
    bool handle_key(int ch);

    /**
     * Query the data source and draw the visible lines on the display.
     */
    void do_update(void);

protected:
    list_data_source *lv_source;  /*< The data source delegate. */
    action           lv_scroll;   /*< The scroll action. */
    WINDOW           *lv_window;  /*< The window that contains this view. */
    unsigned int        lv_y;	  /*< The y offset of this view. */
    vis_line_t lv_top;            /*< The line at the top of the view. */
    unsigned int        lv_left;  /*< The column at the left of the view. */
    vis_line_t lv_height;         /*< The abs/rel height of the view. */
    bool       lv_needs_update;	  /*< Flag to indicate if a display update
    				   *  is needed.
    				   */
    bool       lv_show_scrollbar; /*< Draw the scrollbar in the view. */
};

#endif
