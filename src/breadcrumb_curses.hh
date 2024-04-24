/**
 * Copyright (c) 2022, Timothy Stack
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

#ifndef lnav_breadcrumb_curses_hh
#define lnav_breadcrumb_curses_hh

#include <functional>
#include <utility>
#include <vector>

#include "plain_text_source.hh"
#include "textview_curses.hh"
#include "view_curses.hh"

class breadcrumb_curses : public view_curses {
public:
    using action = std::function<void(breadcrumb_curses&)>;

    breadcrumb_curses();

    void set_window(WINDOW* win)
    {
        this->bc_window = win;
        this->bc_match_view.set_window(win);
    }

    void set_line_source(std::function<std::vector<breadcrumb::crumb>()> ls)
    {
        this->bc_line_source = std::move(ls);
    }

    bool handle_mouse(mouse_event& me) override;

    void focus();
    void blur();

    bool handle_key(int ch);

    bool do_update() override;

    void reload_data();

    static void no_op_action(breadcrumb_curses&);

    action on_focus{no_op_action};
    action on_blur{no_op_action};

private:
    class search_overlay_source : public list_overlay_source {
    public:
        bool list_static_overlay(const listview_curses& lv,
                                 int y,
                                 int bottom,
                                 attr_line_t& value_out) override;

        breadcrumb_curses* sos_parent{nullptr};
    };

    enum class perform_behavior_t {
        always,
        if_different,
    };

    void perform_selection(perform_behavior_t behavior);

    WINDOW* bc_window{nullptr};
    std::function<std::vector<breadcrumb::crumb>()> bc_line_source;
    std::vector<breadcrumb::crumb> bc_focused_crumbs;
    nonstd::optional<size_t> bc_selected_crumb;
    nonstd::optional<size_t> bc_last_selected_crumb;
    std::vector<breadcrumb::possibility> bc_possible_values;
    std::vector<breadcrumb::possibility> bc_similar_values;
    std::string bc_current_search;

    plain_text_source bc_match_source;
    search_overlay_source bc_match_search_overlay;
    textview_curses bc_match_view;

    struct displayed_crumb {
        displayed_crumb(line_range range, size_t index)
            : dc_range(range), dc_index(index)
        {
        }

        line_range dc_range;
        size_t dc_index{0};
    };

    std::vector<displayed_crumb> bc_displayed_crumbs;
    bool bc_initial_mouse_event{true};
};

#endif
