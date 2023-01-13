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

#include "base/itertools.hh"
#include "itertools.similar.hh"
#include "log_format_fwd.hh"
#include "logfile.hh"

using namespace lnav::roles::literals;

breadcrumb_curses::breadcrumb_curses()
{
    this->bc_match_search_overlay.sos_parent = this;
    this->bc_match_source.set_reverse_selection(true);
    this->bc_match_view.set_selectable(true);
    this->bc_match_view.set_overlay_source(&this->bc_match_search_overlay);
    this->bc_match_view.set_sub_source(&this->bc_match_source);
    this->bc_match_view.set_height(0_vl);
    this->bc_match_view.set_show_scrollbar(true);
    this->bc_match_view.set_default_role(role_t::VCR_POPUP);
    this->add_child_view(&this->bc_match_view);
}

void
breadcrumb_curses::do_update()
{
    if (!this->bc_line_source) {
        return;
    }

    size_t crumb_index = 0;
    size_t sel_crumb_offset = 0;
    auto width = static_cast<size_t>(getmaxx(this->bc_window));
    auto crumbs = this->bc_focused_crumbs.empty() ? this->bc_line_source()
                                                  : this->bc_focused_crumbs;
    if (this->bc_last_selected_crumb
        && this->bc_last_selected_crumb.value() >= crumbs.size())
    {
        this->bc_last_selected_crumb = crumbs.size() - 1;
    }
    attr_line_t crumbs_line;
    for (const auto& crumb : crumbs) {
        auto accum_width
            = utf8_string_length(crumbs_line.get_string()).template unwrap();
        auto elem_width = utf8_string_length(crumb.c_display_value.get_string())
                              .template unwrap();
        auto is_selected = this->bc_selected_crumb
            && (crumb_index == this->bc_selected_crumb.value());

        if (is_selected && ((accum_width + elem_width) > width)) {
            crumbs_line.clear();
            crumbs_line.append("\u22ef\u276d"_breadcrumb);
            accum_width = 2;
        }

        crumbs_line.append(crumb.c_display_value);
        if (is_selected) {
            sel_crumb_offset = accum_width;
            crumbs_line.get_attrs().emplace_back(
                line_range{
                    (int) (crumbs_line.length()
                           - crumb.c_display_value.length()),
                    (int) crumbs_line.length(),
                },
                VC_STYLE.template value(text_attrs{A_REVERSE}));
        }
        crumb_index += 1;
        crumbs_line.append("\u276d"_breadcrumb);
    }

    line_range lr{0, static_cast<int>(width)};
    view_curses::mvwattrline(
        this->bc_window, this->bc_y, 0, crumbs_line, lr, role_t::VCR_STATUS);

    if (this->bc_selected_crumb) {
        this->bc_match_view.set_x(sel_crumb_offset);
    }
    view_curses::do_update();
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

    nonstd::optional<size_t> selected_value;
    this->bc_similar_values = this->bc_possible_values
        | lnav::itertools::similar_to(
                                  [](const auto& elem) { return elem.p_key; },
                                  this->bc_current_search,
                                  128)
        | lnav::itertools::sort_with([](const auto& lhs, const auto& rhs) {
                                  return strnatcasecmp(lhs.p_key.size(),
                                                       lhs.p_key.data(),
                                                       rhs.p_key.size(),
                                                       rhs.p_key.data())
                                      < 0;
                              });
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

    auto matches = attr_line_t().join(
        this->bc_similar_values
            | lnav::itertools::map(&breadcrumb::possibility::p_display_value),
        "\n");
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
    this->bc_match_view.reload_data();
    this->set_needs_update();
}

void
breadcrumb_curses::focus()
{
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
    this->bc_selected_crumb = nonstd::nullopt;
    this->bc_current_search.clear();
    this->bc_match_view.set_height(0_vl);
    this->bc_match_view.set_selection(-1_vl);
    this->bc_match_source.clear();
    this->set_needs_update();
}

bool
breadcrumb_curses::handle_key(int ch)
{
    bool retval = false;

    switch (ch) {
        case KEY_CTRL_A:
            if (this->bc_selected_crumb) {
                this->bc_selected_crumb = 0;
                this->bc_current_search.clear();
                this->reload_data();
            }
            retval = true;
            break;
        case KEY_CTRL_E:
            if (this->bc_selected_crumb) {
                this->bc_selected_crumb = this->bc_focused_crumbs.size() - 1;
                this->bc_current_search.clear();
                this->reload_data();
            }
            retval = true;
            break;
        case KEY_BTAB:
        case KEY_LEFT:
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
        case '\t':
        case KEY_RIGHT:
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
        case KEY_HOME:
            this->bc_match_view.set_selection(0_vl);
            retval = true;
            break;
        case KEY_END:
            this->bc_match_view.set_selection(
                this->bc_match_view.get_inner_height() - 1_vl);
            retval = true;
            break;
        case KEY_NPAGE:
            this->bc_match_view.shift_selection(3);
            retval = true;
            break;
        case KEY_PPAGE:
            this->bc_match_view.shift_selection(-3);
            retval = true;
            break;
        case KEY_UP:
            this->bc_match_view.shift_selection(-1);
            retval = true;
            break;
        case KEY_DOWN:
            this->bc_match_view.shift_selection(1);
            retval = true;
            break;
        case 0x7f:
        case KEY_BACKSPACE:
            if (!this->bc_current_search.empty()) {
                this->bc_current_search.pop_back();
                this->reload_data();
            }
            retval = true;
            break;
        case KEY_ENTER:
        case '\r':
            this->perform_selection(perform_behavior_t::always);
            break;
        default:
            if (isprint(ch)) {
                this->bc_current_search.push_back(ch);
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
breadcrumb_curses::perform_selection(
    breadcrumb_curses::perform_behavior_t behavior)
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
        selected_crumb_ref.c_performer(new_value);
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
                selected_crumb_ref.c_performer(this->bc_current_search);
                break;
        }
    }
}

bool
breadcrumb_curses::search_overlay_source::list_value_for_overlay(
    const listview_curses& lv,
    int y,
    int bottom,
    vis_line_t line,
    attr_line_t& value_out)
{
    if (y == 0) {
        auto* parent = this->sos_parent;
        auto sel_opt = parent->bc_focused_crumbs
            | lnav::itertools::nth(parent->bc_selected_crumb);
        auto exp_input = sel_opt
            | lnav::itertools::map(&breadcrumb::crumb::c_expected_input)
            | lnav::itertools::unwrap_or(
                             breadcrumb::crumb::expected_input_t::exact);

        value_out.with_attr_for_all(VC_STYLE.value(text_attrs{A_UNDERLINE}));

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
                            >= (sel_opt
                                | lnav::itertools::map([](const auto& cr) {
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
                                          return cr->c_possible_range.value_or(
                                              0);
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
    }

    return false;
}
