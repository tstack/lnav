/**
 * Copyright (c) 2025, Timothy Stack
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
 */

#ifndef textinput_curses_hh
#define textinput_curses_hh

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "base/attr_line.hh"
#include "base/line_range.hh"
#include "pcrepp/pcre2pp.hh"
#include "document.sections.hh"
#include "plain_text_source.hh"
#include "text_format.hh"
#include "textview_curses.hh"
#include "view_curses.hh"

/**
 * A multi-line text input box that supports the following UX:
 *
 * - Pressing up:
 *   * on the home line moves the cursor to the beginning of the line;
 *   * on a line in the middle moves to the previous line and moves to the
 *     end of the line, if the previous line is shorter;
 *   * scrolls the view so that one line above the cursor is visible.
 * - Pressing down:
 *   * on the bottom line moves the cursor to the end of the line;
 *   * scrolls the view so that the cursor is visible;
 *   * does not move the cursor past the last line in the buffer.
 * - Typing a character:
 *   * inserts it at the cursor position
 *   * scrolls the view to the right
 * - Pressing backspace deletes the previous character.
 *   * At the beginning of a line, the current line is appended to the
 *     previous.
 *   * At the beginning of the buffer, nothing happens.
 * - Pressing CTRL-K:
 *   * without an active selection, copies the text from the cursor to
 *     the end of the line into the clipboard and then deletes it;
 *     - If the cursor is at the end of the line, the line-feed is deleted
 *       and the following line is appended to the current line.
 *   * with a selection, copies the selected text into the clipboard
 *     and then deletes it.
 */
class textinput_curses : public view_curses {
public:
    enum class direction_t {
        left,
        right,
        up,
        down,
    };

    struct movement {
        const direction_t hm_dir;
        const size_t hm_amount;

        movement(direction_t hm_dir, size_t amount)
            : hm_dir(hm_dir), hm_amount(amount)
        {
        }
    };

    struct input_point {
        int x{0};
        int y{0};

        input_point copy_with_x(int x) { return {x, this->y}; }

        input_point copy_with_y(int y) { return {this->x, y}; }

        bool operator<(const input_point& rhs) const
        {
            return this->y < rhs.y || (this->y == rhs.y && this->x < rhs.x);
        }

        bool operator==(const input_point& rhs) const
        {
            return this->x == rhs.x && this->y == rhs.y;
        }
        
        bool operator!=(const input_point& rhs) const
        {
            return this->x != rhs.x || this->y != rhs.y;
        }

        input_point operator+(const movement& rhs) const
        {
            auto retval = *this;
            switch (rhs.hm_dir) {
                case direction_t::left:
                    retval.x -= rhs.hm_amount;
                    break;
                case direction_t::right:
                    retval.x += rhs.hm_amount;
                    break;
                case direction_t::up:
                    retval.y -= rhs.hm_amount;
                    break;
                case direction_t::down:
                    retval.y += rhs.hm_amount;
                    break;
            }

            return retval;
        }

        input_point& operator+=(const movement& rhs)
        {
            switch (rhs.hm_dir) {
                case direction_t::left:
                    this->x -= rhs.hm_amount;
                    break;
                case direction_t::right:
                    this->x += rhs.hm_amount;
                    break;
                case direction_t::up:
                    this->y -= rhs.hm_amount;
                    break;
                case direction_t::down:
                    this->y += rhs.hm_amount;
                    break;
            }

            return *this;
        }
    };

    struct selected_range {
        input_point sr_start;
        input_point sr_end;

        static selected_range from_mouse(input_point start, input_point end)
        {
            return {start, end, bounds_t::inclusive};
        }

        static selected_range from_key(input_point start, input_point end)
        {
            return {start, end, bounds_t::exclusive};
        }

        static selected_range from_point(input_point ip)
        {
            return selected_range{ip};
        }

        static selected_range from_point_and_movement(input_point ip,
                                                      movement m)
        {
            return {ip + m, ip + m, bounds_t::inclusive};
        }

        bool empty() const { return this->sr_start == this->sr_end; }

        bool contains_line(int y) const
        {
            return this->sr_start.y <= y && y <= this->sr_end.y;
        }

        bool contains(const input_point& ip) const
        {
            return this->sr_start.y <= ip.y && ip.y <= this->sr_end.y
                && this->sr_start.x <= ip.x && ip.x <= this->sr_end.x;
        }

        std::optional<line_range> range_for_line(int y) const
        {
            if (!this->contains_line(y)) {
                return std::nullopt;
            }

            line_range retval;

            if (y > this->sr_start.y) {
                retval.lr_start = 0;
            } else {
                retval.lr_start = this->sr_start.x;
            }
            if (y < this->sr_end.y) {
                retval.lr_end = -1;
            } else {
                retval.lr_end = this->sr_end.x;
            }

            return retval;
        }

    private:
        explicit selected_range(input_point ip) : sr_start{ip}, sr_end{ip} {}

        enum class bounds_t {
            inclusive,
            exclusive,
        };

        selected_range(input_point start, input_point end, bounds_t bounds)
            : sr_start(start < end ? start : end),
              sr_end(start < end
                         ? end
                         : (bounds == bounds_t::inclusive
                                ? (start + movement{direction_t::right, 1})
                                : start))
        {
        }
    };

    textinput_curses();

    void set_content(const attr_line_t& al);

    bool contains(int x, int y) const override;

    bool handle_mouse(mouse_event& me) override;

    bool handle_search_key(const ncinput& ch);

    bool handle_key(const ncinput& ch);

    void update_lines();

    void ensure_cursor_visible();

    void focus();

    void blur();

    std::string get_content() const;

    struct dimension_result {
        int dr_height{0};
        int dr_width{0};
        unsigned dr_full_height{0};
        unsigned dr_full_width{0};
    };

    dimension_result get_visible_dimensions() const;

    bool do_update() override;

    void open_popup_for_completion(size_t left,
                                   std::vector<attr_line_t> possibilities);

    void open_popup_for_history(std::vector<attr_line_t> possibilities);

    void apply_highlights();

    void replace_selection(string_fragment sf);

    void move_cursor_by(movement move)
    {
        auto cursor_y_offset = this->tc_cursor.y - this->tc_top;
        this->tc_cursor += move;
        if (move.hm_amount > 1
            && (move.hm_dir == direction_t::up
                || move.hm_dir == direction_t::down))
        {
            this->tc_top = this->tc_cursor.y - cursor_y_offset;
        }
        if (this->tc_cursor.x < 0) {
            if (this->tc_cursor.y > 0) {
                this->tc_cursor.y -= 1;
                this->tc_cursor.x
                    = this->tc_lines[this->tc_cursor.y].column_width();
            } else {
                this->tc_cursor.x = 0;
            }
        }
        if (move.hm_dir == direction_t::right
            && this->tc_cursor.x
                > this->tc_lines[this->tc_cursor.y].column_width())
        {
            if (this->tc_cursor.y + 1 < this->tc_lines.size()) {
                this->tc_cursor.x = 0;
                this->tc_cursor.y += 1;
            }
        }
        this->tc_drag_selection = std::nullopt;
        this->tc_selection = std::nullopt;
        this->ensure_cursor_visible();
    }

    void move_cursor_to(input_point ip)
    {
        this->tc_cursor = ip;
        this->tc_drag_selection = std::nullopt;
        this->tc_selection = std::nullopt;
        this->ensure_cursor_visible();
    }

    void clamp_point(input_point& ip) const
    {
        if (ip.y < 0) {
            ip.y = 0;
        }
        if (ip.y >= this->tc_lines.size()) {
            ip.y = this->tc_lines.size() - 1;
        }
        if (ip.x < 0) {
            ip.x = 0;
        }
        if (ip.x >= this->tc_lines[ip.y].column_width()) {
            ip.x = this->tc_lines[ip.y].column_width();
        }
    }

    void move_cursor_to_next_search_hit();

    void move_cursor_to_prev_search_hit();

    enum class mode_t {
        editing,
        searching,
    };

    ncplane* tc_window{nullptr};
    size_t tc_max_popup_height{5};
    int tc_left{0};
    size_t tc_top{0};
    int tc_height{0};
    input_point tc_cursor;
    mode_t tc_mode{mode_t::editing};
    std::string tc_search;
    std::shared_ptr<lnav::pcre2pp::code> tc_search_code;
    std::optional<bool> tc_search_found;
    input_point tc_search_start_point;
    text_format_t tc_text_format{text_format_t::TF_UNKNOWN};
    std::vector<attr_line_t> tc_lines;
    lnav::document::metadata tc_doc_meta;
    highlight_map_t tc_highlights;
    input_point tc_cursor_anchor;
    std::optional<selected_range> tc_drag_selection;
    std::optional<selected_range> tc_selection;
    input_point tc_cut_location;
    std::vector<std::string> tc_clipboard;
    std::optional<selected_range> tc_complete_range;
    textview_curses tc_popup;
    plain_text_source tc_popup_source;
    std::function<void(textinput_curses&)> tc_on_abort;
    std::function<void(textinput_curses&)> tc_on_change;
    std::function<void(textinput_curses&)> tc_on_completion;
    std::function<void(textinput_curses&)> tc_on_history;
    std::function<void(textinput_curses&)> tc_on_perform;
};

#endif
