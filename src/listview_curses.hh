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
 * @file listview_curses.hh
 */

#ifndef listview_curses_hh
#define listview_curses_hh

#include <list>
#include <string>
#include <utility>
#include <vector>

#include <sys/types.h>

#include "base/func_util.hh"
#include "view_curses.hh"
#include "vis_line.hh"

class listview_curses;

/**
 * Data source for lines to be displayed by the listview_curses object.
 */
class list_data_source {
public:
    virtual ~list_data_source() = default;

    /** @return The number of rows in the list. */
    virtual size_t listview_rows(const listview_curses& lv) = 0;

    virtual size_t listview_width(const listview_curses& lv) { return INT_MAX; }

    /**
     * Get the string value for a row in the list.
     *
     * @param row The row number.
     * @param value_out The destination for the string value.
     */
    virtual void listview_value_for_rows(const listview_curses& lv,
                                         vis_line_t start_row,
                                         std::vector<attr_line_t>& rows_out)
        = 0;

    virtual size_t listview_size_for_row(const listview_curses& lv,
                                         vis_line_t row)
        = 0;

    virtual std::string listview_source_name(const listview_curses& lv)
    {
        return "";
    }

    virtual bool listview_is_row_selectable(const listview_curses& lv,
                                            vis_line_t row)
    {
        return true;
    }

    virtual void listview_selection_changed(const listview_curses& lv) {}
};

class list_gutter_source {
public:
    virtual ~list_gutter_source() = default;

    virtual void listview_gutter_value_for_range(const listview_curses& lv,
                                                 int start,
                                                 int end,
                                                 chtype& ch_out,
                                                 role_t& role_out,
                                                 role_t& bar_role_out)
    {
        ch_out = ACS_VLINE;
    }
};

class list_overlay_source {
public:
    virtual ~list_overlay_source() = default;

    virtual bool list_value_for_overlay(const listview_curses& lv,
                                        int y,
                                        int bottom,
                                        vis_line_t line,
                                        attr_line_t& value_out)
        = 0;
};

class list_input_delegate {
public:
    virtual ~list_input_delegate() = default;

    virtual bool list_input_handle_key(listview_curses& lv, int ch) = 0;

    virtual void list_input_handle_scroll_out(listview_curses& lv) {}
};

/**
 * View that displays a list of lines that can optionally contain highlighting.
 */
class listview_curses
    : public view_curses
    , private log_state_dumper {
public:
    using action = std::function<void(listview_curses*)>;

    listview_curses();

    listview_curses(const listview_curses&) = delete;
    listview_curses(listview_curses&) = delete;

    void set_title(const std::string& title) { this->lv_title = title; }

    const std::string& get_title() const { return this->lv_title; }

    /** @param src The data source delegate. */
    void set_data_source(list_data_source* src)
    {
        this->lv_source = src;
        this->invoke_scroll();
        this->reload_data();
    }

    /** @return The data source delegate. */
    list_data_source* get_data_source() const { return this->lv_source; }

    void set_gutter_source(list_gutter_source* src)
    {
        this->lv_gutter_source = src;
    }

    /** @param src The data source delegate. */
    listview_curses& set_overlay_source(list_overlay_source* src)
    {
        this->lv_overlay_source = src;
        this->reload_data();

        return *this;
    }

    /** @return The overlay source delegate. */
    list_overlay_source* get_overlay_source()
    {
        return this->lv_overlay_source;
    }

    listview_curses& add_input_delegate(list_input_delegate& lid)
    {
        this->lv_input_delegates.push_back(&lid);

        return *this;
    }

    /**
     * @param va The action to invoke when the view is scrolled.
     * @todo Allow multiple observers.
     */
    void set_scroll_action(action va) { this->lv_scroll = std::move(va); }

    void set_show_scrollbar(bool ss) { this->lv_show_scrollbar = ss; }

    bool get_show_scrollbar() const { return this->lv_show_scrollbar; }

    void set_show_bottom_border(bool val)
    {
        if (this->lv_show_bottom_border != val) {
            this->lv_show_bottom_border = val;
            this->set_needs_update();
        }
    }
    bool get_show_bottom_border() const { return this->lv_show_bottom_border; }

    void set_selectable(bool sel)
    {
        this->lv_selectable = sel;
        this->vc_needs_update = true;
    }

    bool is_selectable() const { return this->lv_selectable; }

    void set_selection(vis_line_t sel);

    void shift_selection(int offset);

    vis_line_t get_selection() const
    {
        if (this->lv_selectable) {
            return this->lv_selection;
        }
        return this->lv_top;
    }

    listview_curses& set_word_wrap(bool ww)
    {
        bool scroll_down = this->lv_top >= this->get_top_for_last_row();

        this->lv_word_wrap = ww;
        if (ww && scroll_down && this->lv_top < this->get_top_for_last_row()) {
            this->lv_top = this->get_top_for_last_row();
        }
        if (ww) {
            this->lv_left = 0;
        }
        this->set_needs_update();

        return *this;
    }

    bool get_word_wrap() const { return this->lv_word_wrap; }

    enum row_direction_t {
        RD_UP = -1,
        RD_DOWN = 1,
    };

    vis_line_t rows_available(vis_line_t line, row_direction_t dir) const;

    template<typename F>
    auto map_top_row(F func) ->
        typename std::result_of<F(const attr_line_t&)>::type
    {
        if (this->lv_top >= this->get_inner_height()) {
            return nonstd::nullopt;
        }

        std::vector<attr_line_t> top_line{1};

        this->lv_source->listview_value_for_rows(*this, this->lv_top, top_line);
        return func(top_line[0]);
    }

    /** @param win The curses window this view is attached to. */
    void set_window(WINDOW* win) { this->lv_window = win; }

    /** @return The curses window this view is attached to. */
    WINDOW* get_window() const { return this->lv_window; }

    void set_y(unsigned int y)
    {
        if (y != this->lv_y) {
            this->lv_y = y;
            this->set_needs_update();
        }
    }

    unsigned int get_y() const { return this->lv_y; }

    void set_x(unsigned int x)
    {
        if (x != this->lv_x) {
            this->lv_x = x;
            this->set_needs_update();
        }
    }

    unsigned int get_x() const { return this->lv_x; }

    /**
     * Set the line number to be displayed at the top of the view.  If the
     * value is invalid, flash() will be called.  If the value is valid, the
     * new value will be set and the scroll action called.
     *
     * @param top The new value for top.
     * @param suppress_flash Don't call flash() if the top is out-of-bounds.
     */
    void set_top(vis_line_t top, bool suppress_flash = false);

    /** @return The line number that is displayed at the top. */
    vis_line_t get_top() const { return this->lv_top; }

    nonstd::optional<vis_line_t> get_top_opt() const
    {
        if (this->get_inner_height() == 0_vl) {
            return nonstd::nullopt;
        }

        return this->lv_top;
    }

    /** @return The line number that is displayed at the bottom. */
    vis_line_t get_bottom() const;

    vis_line_t get_top_for_last_row()
    {
        auto retval = 0_vl;

        if (this->get_inner_height() > 0) {
            vis_line_t last_line(this->get_inner_height() - 1);

            retval = last_line
                - vis_line_t(this->rows_available(last_line, RD_UP) - 1);
            if ((retval + this->lv_tail_space) < this->get_inner_height()) {
                retval += this->lv_tail_space;
            }
        }

        return retval;
    }

    /** @return True if the given line is visible. */
    bool is_line_visible(vis_line_t line) const
    {
        return this->get_top() <= line && line <= this->get_bottom();
    }

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
            if (suppress_flash == false) {
                alerter::singleton().chime(
                    "the top of the view has been reached");
            }
        } else {
            this->set_top(std::max(0_vl, this->lv_top + offset),
                          suppress_flash);
        }

        return this->lv_top;
    }

    /**
     * Set the column number to be displayed at the left of the view.  If the
     * value is invalid, flash() will be called.  If the value is valid, the
     * new value will be set and the scroll action called.
     *
     * @param left The new value for left.
     */
    void set_left(unsigned int left)
    {
        if (this->lv_left == left) {
            return;
        }

        if (left > this->lv_left) {
            unsigned long width;
            vis_line_t height;

            this->get_dimensions(height, width);
            if ((this->get_inner_width() - this->lv_left) <= width) {
                alerter::singleton().chime(
                    "the maximum width of the view has been reached");
                return;
            }
        }

        this->lv_left = left;
        this->invoke_scroll();
        this->set_needs_update();
    }

    /** @return The column number that is displayed at the left. */
    unsigned int get_left() const { return this->lv_left; }

    /**
     * Shift the value of left by the given value.
     *
     * @param offset The amount to change top by.
     * @return The final value of top.
     */
    unsigned int shift_left(int offset)
    {
        if (this->lv_word_wrap) {
            alerter::singleton().chime(
                "cannot scroll horizontally when word wrap is enabled");
        } else if (offset < 0 && this->lv_left < (unsigned int) -offset) {
            this->set_left(0);
        } else {
            this->set_left(this->lv_left + offset);
        }

        return this->lv_left;
    }

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
            this->lv_height = height;
            this->set_needs_update();
        }
    }

    /** @return The absolute or relative height of the window. */
    vis_line_t get_height() const { return this->lv_height; }

    /** @return The number of rows of data in this view's source data. */
    vis_line_t get_inner_height() const
    {
        return vis_line_t(this->lv_source == nullptr
                              ? 0
                              : this->lv_source->listview_rows(*this));
    }

    size_t get_inner_width() const
    {
        return this->lv_source == nullptr
            ? 0
            : this->lv_source->listview_width(*this);
    }

    void set_overlay_needs_update() { this->lv_overlay_needs_update = true; }

    /**
     * Get the actual dimensions of the view.
     *
     * @param height_out The actual height of the view in lines.
     * @param width_out The actual width of the view in columns.
     */
    void get_dimensions(vis_line_t& height_out, unsigned long& width_out) const
    {
        unsigned long height;

        if (this->lv_window == nullptr) {
            height_out = std::max(this->lv_height, 1_vl);
            if (this->lv_source) {
                width_out = this->lv_source->listview_width(*this);
            } else {
                width_out = 80;
            }
        } else {
            getmaxyx(this->lv_window, height, width_out);
            if (this->lv_height < 0) {
                height_out = vis_line_t(height) + this->lv_height
                    - vis_line_t(this->lv_y);
                if (height_out < 0_vl) {
                    height_out = 0_vl;
                }
            } else {
                height_out = this->lv_height;
            }
        }
        if (this->lv_x < width_out) {
            width_out -= this->lv_x;
        } else {
            width_out = 0;
        }
    }

    std::pair<vis_line_t, unsigned long> get_dimensions() const
    {
        unsigned long width;
        vis_line_t height;

        this->get_dimensions(height, width);
        return std::make_pair(height, width);
    }

    /** This method should be called when the data source has changed. */
    virtual void reload_data();

    /**
     * @param ch The input to be handled.
     * @return True if the key was eaten by this view.
     */
    bool handle_key(int ch);

    /**
     * Query the data source and draw the visible lines on the display.
     */
    void do_update();

    bool handle_mouse(mouse_event& me);

    listview_curses& set_tail_space(vis_line_t space)
    {
        this->lv_tail_space = space;

        return *this;
    }

    void log_state()
    {
        log_debug("listview_curses=%p", this);
        log_debug("  lv_title=%s", this->lv_title.c_str());
        log_debug("  lv_y=%u", this->lv_y);
        log_debug("  lv_top=%d", (int) this->lv_top);
    }

    virtual void invoke_scroll() { this->lv_scroll(this); }

protected:
    void delegate_scroll_out()
    {
        for (auto& lv_input_delegate : this->lv_input_delegates) {
            lv_input_delegate->list_input_handle_scroll_out(*this);
        }
    }

    enum class lv_mode_t {
        NONE,
        DOWN,
        UP,
        DRAG
    };

    static list_gutter_source DEFAULT_GUTTER_SOURCE;

    std::string lv_title;
    list_data_source* lv_source{nullptr}; /*< The data source delegate. */
    std::list<list_input_delegate*> lv_input_delegates;
    list_overlay_source* lv_overlay_source{nullptr};
    action lv_scroll; /*< The scroll action. */
    WINDOW* lv_window{nullptr}; /*< The window that contains this view. */
    unsigned int lv_x{0};
    unsigned int lv_y{0}; /*< The y offset of this view. */
    vis_line_t lv_top{0}; /*< The line at the top of the view. */
    unsigned int lv_left{0}; /*< The column at the left of the view. */
    vis_line_t lv_height{0}; /*< The abs/rel height of the view. */
    int lv_history_position{0};
    bool lv_overlay_needs_update{true};
    bool lv_show_scrollbar{true}; /*< Draw the scrollbar in the view. */
    bool lv_show_bottom_border{false};
    list_gutter_source* lv_gutter_source{&DEFAULT_GUTTER_SOURCE};
    bool lv_word_wrap{false};
    bool lv_selectable{false};
    vis_line_t lv_selection{0};

    struct timeval lv_mouse_time {
        0, 0
    };
    int lv_scroll_accel{1};
    int lv_scroll_velo{0};
    int lv_mouse_y{-1};
    lv_mode_t lv_mouse_mode{lv_mode_t::NONE};
    vis_line_t lv_tail_space{1};
};

#endif
