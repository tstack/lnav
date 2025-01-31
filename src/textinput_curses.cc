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
 */

#include <algorithm>

#include "textinput_curses.hh"

#include "config.h"
#include "readline_highlighters.hh"
#include "ww898/cp_utf8.hpp"

void
textinput_curses::set_content(const attr_line_t& al)
{
    auto al_copy = al;

    highlight_syntax(this->tc_text_format, al_copy);
    this->tc_lines = al_copy.split_lines();
    if (this->tc_lines.empty()) {
        this->tc_lines.emplace_back(attr_line_t());
    }
    this->tc_left = 0;
    this->tc_top = 0;
    this->tc_cursor_x = 0;
    this->tc_cursor_y = 0;
}

bool
textinput_curses::contains(int x, int y) const
{
    return this->vc_x <= x && x < this->vc_x + this->vc_width && this->vc_y <= y
        && y < this->vc_y + this->tc_height;
}

bool
textinput_curses::handle_mouse(mouse_event& me)
{
    log_debug("mouse here! %d %d %d", me.me_state, me.me_x, me.me_y);
    if (me.me_state == mouse_button_state_t::BUTTON_STATE_DRAGGED) {
        this->tc_cursor_x = this->tc_left + me.me_x;
        this->tc_cursor_y = this->tc_top + me.me_y;
        log_debug("new cursor %d %d", this->tc_cursor_x, this->tc_cursor_y);
        this->ensure_cursor_visible();
    }
    if (me.me_button == mouse_button_t::BUTTON_SCROLL_UP) {
        if (this->tc_cursor_y > 0) {
            this->tc_cursor_y -= 1;
            this->ensure_cursor_visible();
        }
    } else if (me.me_button == mouse_button_t::BUTTON_SCROLL_DOWN) {
        if (this->tc_cursor_y + 1 < this->tc_lines.size()) {
            this->tc_cursor_y += 1;
            this->ensure_cursor_visible();
        }
    }

    return true;
}

bool
textinput_curses::handle_key(const ncinput& ch)
{
    auto dim = this->get_visible_dimensions();
    auto inner_height = this->tc_lines.size();
    auto bottom = inner_height - 1;
    auto chid = ch.id;

    if (ncinput_alt_p(&ch)) {
        switch (chid) {
            case 'f':
            case 'F': {
                log_debug("next word");
                return true;
            }
        }
    }

    if (ncinput_ctrl_p(&ch)) {
        switch (ch.id) {
            case 'a':
            case 'A': {
                this->tc_cursor_x = 0;
                this->ensure_cursor_visible();
                return true;
            }
            case 'b':
            case 'B': {
                chid = NCKEY_LEFT;
                break;
            }
            case 'e':
            case 'E': {
                this->tc_cursor_x
                    = this->tc_lines[this->tc_cursor_y].column_width();
                this->ensure_cursor_visible();
                return true;
            }
            case 'f':
            case 'F': {
                chid = NCKEY_RIGHT;
                break;
            }
            case 'k':
            case 'K': {
                auto& al = this->tc_lines[this->tc_cursor_y];
                auto byte_index = al.column_to_byte_index(this->tc_cursor_x);
                this->tc_clipboard = al.subline(byte_index).al_string;
                al.erase(byte_index);
                this->update_lines();
                return true;
            }
            case 'u':
            case 'U': {
                auto& al = this->tc_lines[this->tc_cursor_y];
                auto byte_index = al.column_to_byte_index(this->tc_cursor_x);
                this->tc_clipboard = al.subline(0, byte_index).al_string;
                al.erase(0, byte_index);
                this->tc_cursor_x = 0;
                this->update_lines();
                return true;
            }
            case 'y':
            case 'Y': {
                if (!this->tc_clipboard.empty()) {
                    auto& al = this->tc_lines[this->tc_cursor_y];
                    al.insert(al.column_to_byte_index(this->tc_cursor_x),
                              this->tc_clipboard);
                    const auto clip_cols
                        = string_fragment::from_str(this->tc_clipboard)
                              .column_width();
                    this->tc_cursor_x += clip_cols;
                    this->update_lines();
                }
                return true;
            }
            default:
                return false;
        }
    }

    switch (chid) {
        case NCKEY_ENTER: {
            auto& curr_al = this->tc_lines[this->tc_cursor_y];
            auto byte_index = curr_al.column_to_byte_index(this->tc_cursor_x);
            auto remaining = curr_al.subline(byte_index);
            curr_al.erase(byte_index);
            this->tc_cursor_x = 0;
            this->tc_cursor_y += 1;
            this->tc_lines.insert(this->tc_lines.begin() + this->tc_cursor_y,
                                  remaining);
            this->update_lines();
            return true;
        }
        case NCKEY_HOME: {
            this->tc_cursor_x = 0;
            this->tc_cursor_y = 0;
            this->ensure_cursor_visible();
            return true;
        }
        case NCKEY_END: {
            this->tc_cursor_x = 0;
            this->tc_cursor_y = bottom;
            this->ensure_cursor_visible();
            return true;
        }
        case NCKEY_PGUP: {
            if (this->tc_cursor_y > 0) {
                if (this->tc_cursor_y < dim.dr_height) {
                    this->tc_cursor_y = 0;
                } else {
                    if (this->tc_top < dim.dr_height) {
                        this->tc_top = 0;
                    } else {
                        this->tc_top -= dim.dr_height;
                    }
                    this->tc_cursor_y -= dim.dr_height;
                }
                this->ensure_cursor_visible();
            }
            return true;
        }
        case NCKEY_PGDOWN: {
            if (this->tc_cursor_y < bottom) {
                if (this->tc_cursor_y + dim.dr_height < inner_height) {
                    this->tc_top += dim.dr_height;
                    this->tc_cursor_y += dim.dr_height;
                } else {
                    this->tc_cursor_y = bottom;
                }
                this->ensure_cursor_visible();
            }
            return true;
        }
        case NCKEY_BACKSPACE: {
            if (this->tc_cursor_x > 0) {
                auto& al = this->tc_lines[this->tc_cursor_y];
                auto start = al.column_to_byte_index(this->tc_cursor_x - 1);
                auto end = al.column_to_byte_index(this->tc_cursor_x);
                al.erase(start, end - start);
                this->tc_cursor_x -= 1;
                this->update_lines();
            } else if (this->tc_cursor_y > 0) {
                auto& prev_al = this->tc_lines[this->tc_cursor_y - 1];
                auto new_cursor_x = prev_al.column_width();
                prev_al.append(this->tc_lines[this->tc_cursor_y]);
                this->tc_lines.erase(this->tc_lines.begin()
                                     + this->tc_cursor_y);
                this->tc_cursor_x = new_cursor_x;
                this->tc_cursor_y -= 1;
                this->ensure_cursor_visible();
            } else {
                // at origin, nothing to do
            }
            return true;
        }
        case NCKEY_UP: {
            if (this->tc_cursor_y > 0) {
                this->tc_cursor_y -= 1;
                this->ensure_cursor_visible();
            }
            return true;
        }
        case NCKEY_DOWN: {
            if (this->tc_cursor_y + 1 < inner_height) {
                this->tc_cursor_y += 1;
                this->ensure_cursor_visible();
            }
            return true;
        }
        case NCKEY_LEFT: {
            if (this->tc_cursor_x > 0) {
                this->tc_cursor_x -= 1;
                this->ensure_cursor_visible();
            } else if (this->tc_cursor_y > 0) {
                this->tc_cursor_y -= 1;
                this->tc_cursor_x
                    = this->tc_lines[this->tc_cursor_y].column_width();
                this->ensure_cursor_visible();
            }
            return true;
        }
        case NCKEY_RIGHT: {
            if (this->tc_cursor_x
                < this->tc_lines[this->tc_cursor_y].column_width())
            {
                this->tc_cursor_x += 1;
                this->ensure_cursor_visible();
            } else if (this->tc_cursor_y < bottom) {
                this->tc_cursor_x = 0;
                this->tc_cursor_y += 1;
                this->ensure_cursor_visible();
            }
            return true;
        }
        default: {
            char utf8[32];
            size_t index = 0;
            for (const auto eff_ch : ch.eff_text) {
                if (eff_ch == 0) {
                    break;
                }
                ww898::utf::utf8::write(eff_ch,
                                        [&utf8, &index](const char bits) {
                                            utf8[index] = bits;
                                            index += 1;
                                        });
            }
            utf8[index] = 0;
            auto& al = this->tc_lines[this->tc_cursor_y];
            al.insert(al.column_to_byte_index(this->tc_cursor_x), utf8);
            this->tc_cursor_x += 1;
            this->update_lines();
            return true;
        }
    }

    return false;
}

void
textinput_curses::ensure_cursor_visible()
{
    auto dim = this->get_visible_dimensions();

    if (this->tc_cursor_y < 0) {
        this->tc_cursor_y = 0;
    }
    if (this->tc_cursor_y >= this->tc_lines.size()) {
        this->tc_cursor_y = this->tc_lines.size() - 1;
    }
    if (this->tc_cursor_x < 0) {
        this->tc_cursor_x = 0;
    }
    if (this->tc_cursor_x >= this->tc_lines[this->tc_cursor_y].column_width()) {
        this->tc_cursor_x = this->tc_lines[this->tc_cursor_y].column_width();
    }

    if (this->tc_cursor_x < this->tc_left) {
        this->tc_left = this->tc_cursor_x;
    }
    if (this->tc_cursor_x > this->tc_left + dim.dr_width) {
        this->tc_left = this->tc_cursor_x - dim.dr_width;
    }
    if (this->tc_cursor_y < this->tc_top) {
        this->tc_top = this->tc_cursor_y;
    }
    if (this->tc_cursor_y >= this->tc_top + dim.dr_height) {
        this->tc_top = (this->tc_cursor_y - dim.dr_height) + 1;
    }
    if (this->tc_cursor_x
        >= this->tc_lines[this->tc_cursor_y].column_width() + 1)
    {
        this->tc_cursor_x = this->tc_lines[this->tc_cursor_y].column_width();
    }
    this->set_needs_update();
}

void
textinput_curses::update_lines()
{
    auto content = attr_line_t(this->get_content());

    highlight_syntax(this->tc_text_format, content);
    this->tc_lines = content.split_lines();
    this->ensure_cursor_visible();
}

textinput_curses::dimension_result
textinput_curses::get_visible_dimensions() const
{
    dimension_result retval;
    unsigned height = 0;
    unsigned width = 0;

    ncplane_dim_yx(this->tc_window, &height, &width);

    if (this->vc_y < height) {
        retval.dr_height
            = std::min((int) height - this->vc_y, this->vc_y + this->tc_height);
    }
    if (this->vc_x < width) {
        retval.dr_width = std::min(width - this->vc_x,
                                   this->vc_x + (unsigned) this->vc_width);
    }
    return retval;
}

std::string
textinput_curses::get_content() const
{
    std::string retval;

    for (const auto& al : this->tc_lines) {
        retval.append(al.al_string).append("\n");
    }
    return retval;
}

void
textinput_curses::focus()
{
    notcurses_cursor_enable(ncplane_notcurses(this->tc_window),
                            this->vc_y + this->tc_cursor_y - this->tc_top,
                            this->vc_x + this->tc_cursor_x - this->tc_left);
}

void
textinput_curses::blur()
{
    notcurses_cursor_disable(ncplane_notcurses(this->tc_window));
}

bool
textinput_curses::do_update()
{
    if (!this->vc_needs_update) {
        return false;
    }

    auto dim = this->get_visible_dimensions();
    auto y = this->vc_y;
    auto y_max = this->vc_y + dim.dr_height;
    for (auto curr_line = this->tc_top;
         curr_line < this->tc_lines.size() && y < y_max;
         curr_line++, y++)
    {
        ncplane_erase_region(this->tc_window, y, this->vc_x, 1, dim.dr_width);
        auto lr = line_range{this->tc_left, this->tc_left + dim.dr_width};
        auto al = this->tc_lines[curr_line];
        auto mvw_res = mvwattrline(this->tc_window, y, this->vc_x, al, lr);
    }
    for (; y < y_max; y++) {
        ncplane_erase_region(this->tc_window, y, this->vc_x, 1, dim.dr_width);
    }

    return true;
}
