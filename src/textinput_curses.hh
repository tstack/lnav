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
#include <string>
#include <vector>

#include "base/attr_line.hh"
#include "plain_text_source.hh"
#include "text_format.hh"
#include "textview_curses.hh"
#include "view_curses.hh"

class textinput_curses : public view_curses {
public:
    textinput_curses();

    void set_content(const attr_line_t& al);

    bool contains(int x, int y) const override;

    bool handle_mouse(mouse_event& me) override;

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

    ncplane* tc_window{nullptr};
    size_t tc_max_popup_height{5};
    int tc_left{0};
    size_t tc_top{0};
    int tc_height{0};
    int tc_cursor_x{0};
    int tc_cursor_y{0};
    text_format_t tc_text_format{text_format_t::TF_UNKNOWN};
    std::vector<attr_line_t> tc_lines;
    std::string tc_clipboard;
    textview_curses tc_popup;
    plain_text_source tc_popup_source;
    std::function<void(textinput_curses&)> tc_on_abort;
    std::function<void(textinput_curses&)> tc_on_change;
    std::function<void(textinput_curses&)> tc_on_completion;
    std::function<void(textinput_curses&)> tc_on_history;
    std::function<void(textinput_curses&)> tc_on_perform;
};

#endif
