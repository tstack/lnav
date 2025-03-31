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

#include "breadcrumb_curses.hh"

#include "base/itertools.enumerate.hh"
#include "base/itertools.hh"
#include "base/keycodes.hh"
#include "itertools.similar.hh"

using namespace lnav::roles::literals;

void
breadcrumb_curses::no_op_action(breadcrumb_curses&)
{
}

breadcrumb_curses::breadcrumb_curses()
{
    this->bc_match_search_overlay.sos_parent = this;
    this->bc_match_source.set_reverse_selection(true);
    this->bc_match_view.set_title("breadcrumb popup");
    this->bc_match_view.set_selectable(true);
    this->bc_match_view.set_overlay_source(&this->bc_match_search_overlay);
    this->bc_match_view.set_sub_source(&this->bc_match_source);
    this->bc_match_view.set_height(0_vl);
    this->bc_match_view.set_show_scrollbar(true);
    this->bc_match_view.set_default_role(role_t::VCR_POPUP);
    this->bc_match_view.set_head_space(0_vl);
    this->add_child_view(&this->bc_match_view);
}

bool
breadcrumb_curses::do_update()
{
    if (!this->bc_line_source) {
        return false;
    }

    if (!this->vc_needs_update) {
        return view_curses::do_update();
    }

    size_t sel_crumb_offset = 0;
    auto width = ncplane_dim_x(this->bc_window);
    auto crumbs = this->bc_focused_crumbs.empty() ? this->bc_line_source()
                                                  : this->bc_focused_crumbs;
    if (this->bc_last_selected_crumb
        && this->bc_last_selected_crumb.value() >= crumbs.size())
    {
        this->bc_last_selected_crumb = crumbs.size() - 1;
    }
    this->bc_displayed_crumbs.clear();
    attr_line_t crumbs_line;
    for (const auto& [crumb_index, crumb] : lnav::itertools::enumerate(crumbs))
    {
        auto accum_width = crumbs_line.column_width();
        auto elem_width = crumb.c_display_value.column_width();
        auto is_selected = this->bc_selected_crumb
            && (crumb_index == this->bc_selected_crumb.value());

        if (is_selected && ((accum_width + elem_width) > width)) {
            crumbs_line.clear();
            crumbs_line.append("\u22ef\uff1a"_breadcrumb);
            accum_width = 2;
        }

        line_range crumb_range;
        crumb_range.lr_start = (int) crumbs_line.length();
        crumbs_line.append(crumb.c_display_value);
        crumb_range.lr_end = (int) crumbs_line.length();
        if (is_selected) {
            sel_crumb_offset = accum_width;
            crumbs_line.get_attrs().emplace_back(
                crumb_range, VC_STYLE.value(text_attrs::with_reverse()));
        }

        this->bc_displayed_crumbs.emplace_back(
            line_range{
                (int) accum_width,
                (int) (accum_width + elem_width),
                line_range::unit::codepoint,
            },
            crumb_index);
        crumbs_line.append(" \uff1a"_breadcrumb);
    }

    if (!this->vc_enabled) {
        for (auto& attr : crumbs_line.al_attrs) {
            if (attr.sa_type != &VC_ROLE) {
                continue;
            }

            auto role = attr.sa_value.get<role_t>();
            if (role == role_t::VCR_STATUS_TITLE) {
                attr.sa_value = role_t::VCR_STATUS_DISABLED_TITLE;
            }
        }
    }

    line_range lr{0, static_cast<int>(width)};
    auto default_role = this->vc_enabled ? role_t::VCR_STATUS
                                         : role_t::VCR_INACTIVE_STATUS;
    mvwattrline(this->bc_window, this->vc_y, 0, crumbs_line, lr, default_role);

    if (this->bc_selected_crumb) {
        this->bc_match_view.set_x(sel_crumb_offset);
    }
    view_curses::do_update();

    return true;
}

void
breadcrumb_curses::reload_data()
{
    if (!this->bc_selected_crumb) {
        return;
    }

    auto& selected_crumb_ref
        = this->bc_focused_crumbs[this->bc_selected_crumb.value()];
    this->bc_possible_values = selected_crumb_ref.c_possibility_provider();

    std::optional<size_t> selected_value;
    this->bc_similar_values = this->bc_possible_values
        | lnav::itertools::similar_to(
                                  [](const auto& elem) { return elem.p_key; },
                                  this->bc_current_search,
                                  128)
        | lnav::itertools::sort_with(breadcrumb::possibility::sort_cmp);
    for (auto& al : this->bc_similar_values) {
        al.p_display_value.highlight_fuzzy_matches(this->bc_current_search);
    }
    if (selected_crumb_ref.c_key.is<std::string>()
        && selected_crumb_ref.c_expected_input
            != breadcrumb::crumb::expected_input_t::anything)
    {
        auto& selected_crumb_key = selected_crumb_ref.c_key.get<std::string>();
        auto found_poss_opt = this->bc_similar_values
            | lnav::itertools::find_if([&selected_crumb_key](const auto& elem) {
                                  return elem.p_key == selected_crumb_key;
                              });

        if (found_poss_opt) {
            selected_value = std::distance(this->bc_similar_values.begin(),
                                           found_poss_opt.value());
        } else {
            selected_value = 0;
        }
    }

    auto matches = attr_line_t()
                       .join(this->bc_similar_values
                                 | lnav::itertools::map(
                                     &breadcrumb::possibility::p_display_value),
                             "\n")
                       .move();
    this->bc_match_source.replace_with(matches);
    auto width = this->bc_possible_values
        | lnav::itertools::fold(
                     [](const auto& match, auto& accum) {
                         auto mlen = match.p_display_value.length();
                         if (mlen > accum) {
                             return mlen;
                         }
                         return accum;
                     },
                     selected_crumb_ref.c_display_value.length());

    if (static_cast<ssize_t>(selected_crumb_ref.c_search_placeholder.size())
        > width)
    {
        width = selected_crumb_ref.c_search_placeholder.size();
    }
    this->bc_match_view.set_height(vis_line_t(
        std::min(this->bc_match_source.get_lines().size() + 1, size_t{4})));
    this->bc_match_view.set_width(width + 3);
    this->bc_match_view.set_needs_update();
    this->bc_match_view.set_selection(
        vis_line_t(selected_value.value_or(-1_vl)));
    if (selected_value) {
        this->bc_match_view.set_top(vis_line_t(selected_value.value()));
    }
    this->bc_match_view.reload_data();
    this->set_needs_update();
}

void
breadcrumb_curses::focus()
{
    this->bc_match_view.set_y(this->vc_y + 1);
    this->bc_focused_crumbs = this->bc_line_source();
    if (this->bc_focused_crumbs.empty()) {
        return;
    }

    this->bc_current_search.clear();
    if (this->bc_last_selected_crumb
        && this->bc_last_selected_crumb.value()
            >= this->bc_focused_crumbs.size())
    {
        this->bc_last_selected_crumb = this->bc_focused_crumbs.size() - 1;
    }
    this->bc_selected_crumb = this->bc_last_selected_crumb.value_or(0);
    this->reload_data();
}

void
breadcrumb_curses::blur()
{
    this->bc_last_selected_crumb = this->bc_selected_crumb;
    this->bc_focused_crumbs.clear();
    this->bc_selected_crumb = std::nullopt;
    this->bc_current_search.clear();
    this->bc_match_view.set_height(0_vl);
    this->bc_match_view.set_selection(-1_vl);
    this->bc_match_source.clear();
    this->set_needs_update();
}

bool
breadcrumb_curses::handle_key(const ncinput& ch)
{
    bool retval = false;
    auto mapped_id = ch.id;

    if (mapped_id == NCKEY_TAB && ncinput_shift_p(&ch)) {
        mapped_id = NCKEY_LEFT;
    } else if (ncinput_ctrl_p(&ch)) {
        switch (mapped_id) {
            case 'a':
            case 'A':
                mapped_id = KEY_CTRL('a');
                break;
                break;
            case 'e':
            case 'E':
                mapped_id = KEY_CTRL('e');
                break;
        }
    }
    switch (mapped_id) {
        case KEY_CTRL('a'):
            if (this->bc_selected_crumb) {
                this->bc_selected_crumb = 0;
                this->bc_current_search.clear();
                this->reload_data();
            }
            retval = true;
            break;
        case KEY_CTRL('e'):
            if (this->bc_selected_crumb) {
                this->bc_selected_crumb = this->bc_focused_crumbs.size() - 1;
                this->bc_current_search.clear();
                this->reload_data();
            }
            retval = true;
            break;
        case NCKEY_LEFT:
            if (this->bc_selected_crumb) {
                if (this->bc_selected_crumb.value() > 0) {
                    this->bc_selected_crumb
                        = this->bc_selected_crumb.value() - 1;
                } else {
                    this->bc_selected_crumb
                        = this->bc_focused_crumbs.size() - 1;
                }
                this->bc_current_search.clear();
                this->reload_data();
            }
            retval = true;
            break;
        case NCKEY_TAB:
        case NCKEY_RIGHT:
            if (this->bc_selected_crumb) {
                this->perform_selection(perform_behavior_t::if_different);
                this->blur();
                this->focus();
                this->reload_data();
                if (this->bc_selected_crumb.value()
                    < this->bc_focused_crumbs.size() - 1)
                {
                    this->bc_selected_crumb
                        = this->bc_selected_crumb.value() + 1;
                    retval = true;
                }
                this->bc_current_search.clear();
                this->reload_data();
            } else {
                retval = true;
            }
            break;
        case NCKEY_HOME:
            this->bc_match_view.set_selection(0_vl);
            retval = true;
            break;
        case NCKEY_END:
            this->bc_match_view.set_selection(
                this->bc_match_view.get_inner_height() - 1_vl);
            retval = true;
            break;
        case NCKEY_PGDOWN:
            this->bc_match_view.shift_selection(
                listview_curses::shift_amount_t::down_page);
            retval = true;
            break;
        case NCKEY_PGUP:
            this->bc_match_view.shift_selection(
                listview_curses::shift_amount_t::up_page);
            retval = true;
            break;
        case NCKEY_UP:
            this->bc_match_view.shift_selection(
                listview_curses::shift_amount_t::up_line);
            retval = true;
            break;
        case NCKEY_DOWN:
            this->bc_match_view.shift_selection(
                listview_curses::shift_amount_t::down_line);
            retval = true;
            break;
        case KEY_DELETE:
        case NCKEY_BACKSPACE:
            if (!this->bc_current_search.empty()) {
                this->bc_current_search.pop_back();
                this->reload_data();
            }
            retval = true;
            break;
        case NCKEY_ENTER:
        case '\r':
            this->perform_selection(perform_behavior_t::always);
            break;
        case KEY_ESCAPE:
            break;
        default:
            if (ch.id < 0x7f && isprint(ch.id)) {
                this->bc_current_search.push_back(ch.id);
                this->reload_data();
                retval = true;
            }
            break;
    }

    if (!retval) {
        this->blur();
    }
    this->set_needs_update();
    return retval;
}

void
breadcrumb_curses::perform_selection(perform_behavior_t behavior)
{
    if (!this->bc_selected_crumb) {
        return;
    }

    auto& selected_crumb_ref
        = this->bc_focused_crumbs[this->bc_selected_crumb.value()];
    auto match_sel = this->bc_match_view.get_selection();
    if (match_sel >= 0
        && match_sel < vis_line_t(this->bc_similar_values.size()))
    {
        const auto& new_value = this->bc_similar_values[match_sel].p_key;

        switch (behavior) {
            case perform_behavior_t::if_different:
                if (breadcrumb::crumb::key_t{new_value}
                    == selected_crumb_ref.c_key)
                {
                    return;
                }
                break;
            case perform_behavior_t::always:
                break;
        }
        if (this->bc_perform_handler) {
            this->bc_perform_handler(selected_crumb_ref.c_performer, new_value);
        }
    } else if (!this->bc_current_search.empty()) {
        switch (selected_crumb_ref.c_expected_input) {
            case breadcrumb::crumb::expected_input_t::exact:
                break;
            case breadcrumb::crumb::expected_input_t::index:
            case breadcrumb::crumb::expected_input_t::index_or_exact: {
                size_t index;

                if (sscanf(this->bc_current_search.c_str(), "%zu", &index) == 1)
                {
                    selected_crumb_ref.c_performer(index);
                }
                break;
            }
            case breadcrumb::crumb::expected_input_t::anything:
                if (this->bc_perform_handler) {
                    this->bc_perform_handler(selected_crumb_ref.c_performer,
                                             this->bc_current_search);
                }
                break;
        }
    }
}

bool
breadcrumb_curses::search_overlay_source::list_static_overlay(
    const listview_curses& lv, int y, int bottom, attr_line_t& value_out)
{
    if (y != 0) {
        return false;
    }
    auto* parent = this->sos_parent;
    auto sel_opt = parent->bc_focused_crumbs
        | lnav::itertools::nth(parent->bc_selected_crumb);
    auto exp_input = sel_opt
        | lnav::itertools::map(&breadcrumb::crumb::c_expected_input)
        | lnav::itertools::unwrap_or(
                         breadcrumb::crumb::expected_input_t::exact);

    value_out.with_attr_for_all(VC_STYLE.value(text_attrs::with_underline()));

    if (!parent->bc_current_search.empty()) {
        value_out = parent->bc_current_search;

        role_t combobox_role = role_t::VCR_STATUS;
        switch (exp_input) {
            case breadcrumb::crumb::expected_input_t::exact:
                if (parent->bc_similar_values.empty()) {
                    combobox_role = role_t::VCR_ALERT_STATUS;
                }
                break;
            case breadcrumb::crumb::expected_input_t::index: {
                size_t index;

                if (sscanf(parent->bc_current_search.c_str(), "%zu", &index)
                        != 1
                    || index < 0
                    || (index
                        >= (sel_opt | lnav::itertools::map([](const auto& cr) {
                                return cr->c_possible_range.value_or(0);
                            })
                            | lnav::itertools::unwrap_or(size_t{0}))))
                {
                    combobox_role = role_t::VCR_ALERT_STATUS;
                }
                break;
            }
            case breadcrumb::crumb::expected_input_t::index_or_exact: {
                size_t index;

                if (sscanf(parent->bc_current_search.c_str(), "%zu", &index)
                    == 1)
                {
                    if (index < 0
                        || (index
                            >= (sel_opt
                                | lnav::itertools::map([](const auto& cr) {
                                      return cr->c_possible_range.value_or(0);
                                  })
                                | lnav::itertools::unwrap_or(size_t{0}))))
                    {
                        combobox_role = role_t::VCR_ALERT_STATUS;
                    }
                } else if (parent->bc_similar_values.empty()) {
                    combobox_role = role_t::VCR_ALERT_STATUS;
                }
                break;
            }
            case breadcrumb::crumb::expected_input_t::anything:
                break;
        }
        value_out.with_attr_for_all(VC_ROLE.value(combobox_role));
        return true;
    }
    if (parent->bc_selected_crumb) {
        auto& selected_crumb_ref
            = parent->bc_focused_crumbs[parent->bc_selected_crumb.value()];

        if (!selected_crumb_ref.c_search_placeholder.empty()) {
            value_out = selected_crumb_ref.c_search_placeholder;
            value_out.with_attr_for_all(
                VC_ROLE.value(role_t::VCR_INACTIVE_STATUS));
            return true;
        }
    }

    return false;
}

bool
breadcrumb_curses::handle_mouse(mouse_event& me)
{
    if (me.me_state == mouse_button_state_t::BUTTON_STATE_PRESSED
        && this->bc_focused_crumbs.empty())
    {
        this->focus();
        this->on_focus(*this);
        this->do_update();
        this->bc_initial_mouse_event = true;
    }

    auto find_res = this->bc_displayed_crumbs
        | lnav::itertools::find_if([&me](const auto& elem) {
                        return me.me_button == mouse_button_t::BUTTON_LEFT
                            && elem.dc_range.contains(me.me_x);
                    });

    if (!this->bc_focused_crumbs.empty()) {
        if (me.me_y > 0 || !find_res
            || find_res.value()->dc_index == this->bc_selected_crumb)
        {
            if (view_curses::handle_mouse(me)) {
                if (me.me_y > 0
                    && (me.me_state
                            == mouse_button_state_t::BUTTON_STATE_DOUBLE_CLICK
                        || me.me_state
                            == mouse_button_state_t::BUTTON_STATE_RELEASED))
                {
                    this->perform_selection(perform_behavior_t::if_different);
                    this->blur();
                    this->reload_data();
                    this->on_blur(*this);
                }
                return true;
            }
        }
        if (!this->bc_initial_mouse_event
            && me.me_state == mouse_button_state_t::BUTTON_STATE_RELEASED
            && me.me_y == 0 && find_res
            && find_res.value()->dc_index == this->bc_selected_crumb.value())
        {
            this->blur();
            this->reload_data();
            this->on_blur(*this);
            return true;
        }
    }

    if (me.me_state == mouse_button_state_t::BUTTON_STATE_RELEASED) {
        this->bc_initial_mouse_event = false;
    }

    if (me.me_y != 0) {
        return true;
    }

    if (find_res) {
        auto crumb_index = find_res.value()->dc_index;

        if (this->bc_selected_crumb) {
            this->blur();
            this->focus();
            this->reload_data();
            this->bc_selected_crumb = crumb_index;
            this->bc_current_search.clear();
            this->reload_data();
        }
    }

    return true;
}
