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
#include <vector>

#include "textview_curses.hh"

#include "base/ansi_scrubber.hh"
#include "base/humanize.time.hh"
#include "base/injector.hh"
#include "base/lnav_log.hh"
#include "base/time_util.hh"
#include "config.h"
#include "data_scanner.hh"
#include "fmt/format.h"
#include "lnav_config.hh"
#include "log_format_fwd.hh"
#include "logfile.hh"
#include "shlex.hh"
#include "view_curses.hh"

constexpr auto REVERSE_SEARCH_OFFSET = 2000_vl;

void
text_filter::revert_to_last(logfile_filter_state& lfs, size_t rollback_size)
{
    require(lfs.tfs_lines_for_message[this->lf_index] == 0);

    lfs.tfs_message_matched[this->lf_index]
        = lfs.tfs_last_message_matched[this->lf_index];
    lfs.tfs_lines_for_message[this->lf_index]
        = lfs.tfs_last_lines_for_message[this->lf_index];

    for (size_t lpc = 0; lpc < lfs.tfs_lines_for_message[this->lf_index]; lpc++)
    {
        if (lfs.tfs_message_matched[this->lf_index]) {
            lfs.tfs_filter_hits[this->lf_index] -= 1;
        }
        lfs.tfs_filter_count[this->lf_index] -= 1;
        size_t line_number = lfs.tfs_filter_count[this->lf_index];

        lfs.tfs_mask[line_number] &= ~(((uint32_t) 1) << this->lf_index);
    }
    if (lfs.tfs_lines_for_message[this->lf_index] > 0) {
        require(lfs.tfs_lines_for_message[this->lf_index] >= rollback_size);

        lfs.tfs_lines_for_message[this->lf_index] -= rollback_size;
    }
    if (lfs.tfs_lines_for_message[this->lf_index] == 0) {
        lfs.tfs_message_matched[this->lf_index] = false;
    }
}

bool
text_filter::add_line(logfile_filter_state& lfs,
                      logfile::const_iterator ll,
                      const shared_buffer_ref& line)
{
    if (ll->is_message()) {
        this->end_of_message(lfs);
    }
    auto retval = this->matches(line_source{*lfs.tfs_logfile, ll}, line);

    lfs.tfs_message_matched[this->lf_index]
        = lfs.tfs_message_matched[this->lf_index] || retval;
    lfs.tfs_lines_for_message[this->lf_index] += 1;

    return retval;
}

void
text_filter::end_of_message(logfile_filter_state& lfs)
{
    uint32_t mask = 0;

    mask = ((uint32_t) 1U << this->lf_index);

    for (size_t lpc = 0; lpc < lfs.tfs_lines_for_message[this->lf_index]; lpc++)
    {
        require(lfs.tfs_filter_count[this->lf_index]
                <= lfs.tfs_logfile->size());

        size_t line_number = lfs.tfs_filter_count[this->lf_index];

        if (lfs.tfs_message_matched[this->lf_index]) {
            lfs.tfs_mask[line_number] |= mask;
        } else {
            lfs.tfs_mask[line_number] &= ~mask;
        }
        lfs.tfs_filter_count[this->lf_index] += 1;
        if (lfs.tfs_message_matched[this->lf_index]) {
            lfs.tfs_filter_hits[this->lf_index] += 1;
        }
    }
    lfs.tfs_last_message_matched[this->lf_index]
        = lfs.tfs_message_matched[this->lf_index];
    lfs.tfs_last_lines_for_message[this->lf_index]
        = lfs.tfs_lines_for_message[this->lf_index];
    lfs.tfs_message_matched[this->lf_index] = false;
    lfs.tfs_lines_for_message[this->lf_index] = 0;
}

log_accel::direction_t
text_accel_source::get_line_accel_direction(vis_line_t vl)
{
    log_accel la;

    while (vl >= 0) {
        const auto* curr_line = this->text_accel_get_line(vl);

        if (!curr_line->is_message()) {
            --vl;
            continue;
        }

        if (!la.add_point(
                curr_line->get_time<std::chrono::milliseconds>().count()))
        {
            break;
        }

        --vl;
    }

    return la.get_direction();
}

std::string
text_accel_source::get_time_offset_for_line(textview_curses& tc, vis_line_t vl)
{
    auto ll = this->text_accel_get_line(vl);
    auto curr_tv = ll->get_timeval();
    struct timeval diff_tv;

    auto prev_umark = tc.get_bookmarks()[&textview_curses::BM_USER].prev(vl);
    auto next_umark = tc.get_bookmarks()[&textview_curses::BM_USER].next(vl);
    auto prev_emark
        = tc.get_bookmarks()[&textview_curses::BM_USER_EXPR].prev(vl);
    auto next_emark
        = tc.get_bookmarks()[&textview_curses::BM_USER_EXPR].next(vl);
    if (!prev_umark && !prev_emark && (next_umark || next_emark)) {
        auto next_line = this->text_accel_get_line(
            std::max(next_umark.value_or(0_vl), next_emark.value_or(0_vl)));

        diff_tv = curr_tv - next_line->get_timeval();
    } else {
        auto prev_row
            = std::max(prev_umark.value_or(0_vl), prev_emark.value_or(0_vl));
        auto* first_line = this->text_accel_get_line(prev_row);
        auto start_tv = first_line->get_timeval();
        diff_tv = curr_tv - start_tv;
    }

    return humanize::time::duration::from_tv(diff_tv).to_string();
}

const bookmark_type_t textview_curses::BM_ERRORS("error");
const bookmark_type_t textview_curses::BM_WARNINGS("warning");
const bookmark_type_t textview_curses::BM_USER("user");
const bookmark_type_t textview_curses::BM_USER_EXPR("user-expr");
const bookmark_type_t textview_curses::BM_SEARCH("search");
const bookmark_type_t textview_curses::BM_META("meta");
const bookmark_type_t textview_curses::BM_PARTITION("partition");

textview_curses::textview_curses()
    : lnav_config_listener(__FILE__), tc_search_action(noop_func{})
{
    this->set_data_source(this);
}

textview_curses::~textview_curses()
{
    this->tc_search_action = noop_func{};
}

void
textview_curses::reload_config(error_reporter& reporter)
{
    const static auto DEFAULT_THEME_NAME = std::string("default");
    const auto& vc = view_colors::singleton();

    for (auto iter = this->tc_highlights.begin();
         iter != this->tc_highlights.end();)
    {
        if (iter->first.first != highlight_source_t::THEME) {
            ++iter;
            continue;
        }

        iter = this->tc_highlights.erase(iter);
    }

    for (const auto& theme_name : {DEFAULT_THEME_NAME, lnav_config.lc_ui_theme})
    {
        auto theme_iter = lnav_config.lc_ui_theme_defs.find(theme_name);

        if (theme_iter == lnav_config.lc_ui_theme_defs.end()) {
            continue;
        }

        auto vars = &theme_iter->second.lt_vars;
        for (const auto& hl_pair : theme_iter->second.lt_highlights) {
            if (hl_pair.second.hc_regex.pp_value == nullptr) {
                continue;
            }

            const auto& sc = hl_pair.second.hc_style;
            std::string fg_color, bg_color, errmsg;
            bool invalid = false;
            text_attrs attrs;

            auto fg1 = sc.sc_color;
            auto bg1 = sc.sc_background_color;
            shlex(fg1).eval(fg_color, scoped_resolver{vars});
            shlex(bg1).eval(bg_color, scoped_resolver{vars});

            attrs.ta_fg_color = vc.match_color(
                styling::color_unit::from_str(fg_color).unwrapOrElse(
                    [&](const auto& msg) {
                        reporter(&sc.sc_color,
                                 lnav::console::user_message::error(
                                     attr_line_t("invalid color -- ")
                                         .append_quoted(sc.sc_color))
                                     .with_reason(msg));
                        invalid = true;
                        return styling::color_unit::EMPTY;
                    }));
            attrs.ta_bg_color = vc.match_color(
                styling::color_unit::from_str(bg_color).unwrapOrElse(
                    [&](const auto& msg) {
                        reporter(&sc.sc_background_color,
                                 lnav::console::user_message::error(
                                     attr_line_t("invalid background color -- ")
                                         .append_quoted(sc.sc_background_color))
                                     .with_reason(msg));
                        invalid = true;
                        return styling::color_unit::EMPTY;
                    }));
            if (invalid) {
                continue;
            }

            if (sc.sc_bold) {
                attrs |= text_attrs::style::bold;
            }
            if (sc.sc_underline) {
                attrs |= text_attrs::style::underline;
            }
            this->tc_highlights[{highlight_source_t::THEME, hl_pair.first}]
                = highlighter(hl_pair.second.hc_regex.pp_value)
                      .with_attrs(attrs)
                      .with_nestable(false);
        }
    }

    if (this->tc_reload_config_delegate) {
        this->tc_reload_config_delegate(*this);
    }
}

void
textview_curses::invoke_scroll()
{
    this->tc_selected_text = std::nullopt;
    if (this->tc_sub_source != nullptr) {
        this->tc_sub_source->scroll_invoked(this);
    }

    listview_curses::invoke_scroll();
}

void
textview_curses::reload_data()
{
    this->tc_selected_text = std::nullopt;
    if (this->tc_sub_source != nullptr) {
        this->tc_sub_source->text_update_marks(this->tc_bookmarks);

        auto* ttt = dynamic_cast<text_time_translator*>(this->tc_sub_source);

        if (ttt != nullptr) {
            ttt->data_reloaded(this);
        }
        listview_curses::reload_data();
    }
}

void
textview_curses::grep_begin(grep_proc<vis_line_t>& gp,
                            vis_line_t start,
                            vis_line_t stop)
{
    require(this->tc_searching >= 0);

    this->tc_searching += 1;
    this->tc_search_action(this);

    if (start != -1_vl) {
        auto& search_bv = this->tc_bookmarks[&BM_SEARCH];
        auto pair = search_bv.equal_range(start, stop);

        if (pair.first != pair.second) {
            this->set_needs_update();
        }
        for (auto mark_iter = pair.first; mark_iter != pair.second; ++mark_iter)
        {
            if (this->tc_sub_source) {
                this->tc_sub_source->text_mark(&BM_SEARCH, *mark_iter, false);
            }
        }
        if (pair.first != pair.second) {
            auto to_del = std::vector<vis_line_t>{};
            for (auto file_iter = pair.first; file_iter != pair.second;
                 ++file_iter)
            {
                to_del.emplace_back(*file_iter);
            }

            for (auto cl : to_del) {
                search_bv.bv_tree.erase(cl);
            }
        }
    }

    listview_curses::reload_data();
}

void
textview_curses::grep_end_batch(grep_proc<vis_line_t>& gp)
{
    if (this->tc_follow_deadline.tv_sec
        && this->tc_follow_selection == this->get_selection())
    {
        timeval now;

        gettimeofday(&now, nullptr);
        if (this->tc_follow_deadline < now) {
        } else {
            if (this->tc_follow_func) {
                if (this->tc_follow_func()) {
                    this->tc_follow_deadline = {0, 0};
                }
            } else {
                this->tc_follow_deadline = {0, 0};
            }
        }
    }
    this->tc_search_action(this);
}

void
textview_curses::grep_end(grep_proc<vis_line_t>& gp)
{
    this->tc_searching -= 1;
    this->grep_end_batch(gp);
    if (this->tc_searching == 0 && this->tc_search_start_time) {
        const auto now = std::chrono::steady_clock::now();
        this->tc_search_duration
            = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - this->tc_search_start_time.value());
        this->tc_search_start_time = std::nullopt;
        if (this->tc_state_event_handler) {
            this->tc_state_event_handler(*this);
        }
    }

    ensure(this->tc_searching >= 0);
}

void
textview_curses::grep_match(grep_proc<vis_line_t>& gp, vis_line_t line)
{
    this->tc_bookmarks[&BM_SEARCH].insert_once(vis_line_t(line));
    if (this->tc_sub_source != nullptr) {
        this->tc_sub_source->text_mark(&BM_SEARCH, line, true);
    }

    if (this->get_top() <= line && line <= this->get_bottom()) {
        listview_curses::reload_data();
    }
}

void
textview_curses::listview_value_for_rows(const listview_curses& lv,
                                         vis_line_t row,
                                         std::vector<attr_line_t>& rows_out)
{
    for (auto& al : rows_out) {
        this->textview_value_for_row(row, al);
        ++row;
    }
}

bool
textview_curses::handle_mouse(mouse_event& me)
{
    unsigned long width;
    vis_line_t height;

    if (!this->vc_visible || this->lv_height == 0) {
        return false;
    }

    if (!this->tc_selection_start && listview_curses::handle_mouse(me)) {
        return true;
    }

    auto mouse_line = (me.me_y < 0 || me.me_y >= this->lv_display_lines.size())
        ? empty_space{}
        : this->lv_display_lines[me.me_y];
    this->get_dimensions(height, width);

    if (!mouse_line.is<overlay_menu>()
        && (me.me_button != mouse_button_t::BUTTON_LEFT
            || me.me_state != mouse_button_state_t::BUTTON_STATE_RELEASED))
    {
        this->tc_selected_text = std::nullopt;
        this->set_needs_update();
    }

    std::optional<int> overlay_content_min_y;
    std::optional<int> overlay_content_max_y;
    if (this->tc_press_line.is<overlay_content>()) {
        auto main_line
            = this->tc_press_line.get<overlay_content>().oc_main_line;
        for (size_t lpc = 0; lpc < this->lv_display_lines.size(); lpc++) {
            if (overlay_content_min_y
                && !this->lv_display_lines[lpc].is<static_overlay_content>()
                && !this->lv_display_lines[lpc].is<overlay_content>())
            {
                overlay_content_max_y = lpc;
                break;
            }
            if (this->lv_display_lines[lpc].is<main_content>()) {
                auto& mc = this->lv_display_lines[lpc].get<main_content>();
                if (mc.mc_line == main_line) {
                    overlay_content_min_y = lpc;
                }
            }
        }
        if (overlay_content_min_y && !overlay_content_max_y) {
            overlay_content_max_y = this->lv_display_lines.size();
        }
    }

    auto* sub_delegate = dynamic_cast<text_delegate*>(this->tc_sub_source);

    switch (me.me_state) {
        case mouse_button_state_t::BUTTON_STATE_PRESSED: {
            this->tc_press_line = mouse_line;
            this->tc_press_left = this->lv_left + me.me_press_x;
            if (!this->lv_selectable) {
                this->set_selectable(true);
            }
            mouse_line.match(
                [this, &me, sub_delegate, &mouse_line](const main_content& mc) {
                    this->tc_text_selection_active = true;
                    this->tc_press_left = this->lv_left
                        + mc.mc_line_range.lr_start + me.me_press_x;
                    if (this->vc_enabled) {
                        if (this->tc_supports_marks
                            && me.me_button == mouse_button_t::BUTTON_LEFT
                            && (me.is_modifier_pressed(
                                    mouse_event::modifier_t::shift)
                                || me.is_modifier_pressed(
                                    mouse_event::modifier_t::ctrl)))
                        {
                            this->tc_selection_start = mc.mc_line;
                        }
                        this->set_selection_without_context(mc.mc_line);
                        this->tc_press_event = me;
                    }
                    if (this->tc_delegate != nullptr) {
                        this->tc_delegate->text_handle_mouse(
                            *this, mouse_line, me);
                    }
                    if (sub_delegate != nullptr) {
                        sub_delegate->text_handle_mouse(*this, mouse_line, me);
                    }
                },
                [](const overlay_menu& om) {},
                [](const static_overlay_content& soc) {},
                [this](const overlay_content& oc) {
                    this->set_overlay_selection(oc.oc_line);
                },
                [](const empty_space& es) {});
            break;
        }
        case mouse_button_state_t::BUTTON_STATE_DOUBLE_CLICK: {
            if (!this->lv_selectable) {
                this->set_selectable(true);
            }
            this->tc_text_selection_active = false;
            mouse_line.match(
                [this, &me, &mouse_line, sub_delegate](const main_content& mc) {
                    if (this->vc_enabled) {
                        if (this->tc_supports_marks
                            && me.me_button == mouse_button_t::BUTTON_LEFT)
                        {
                            attr_line_t al;

                            this->textview_value_for_row(mc.mc_line, al);
                            auto line_sf
                                = string_fragment::from_str(al.get_string());
                            auto cursor_sf = line_sf.sub_cell_range(
                                this->lv_left + mc.mc_line_range.lr_start
                                    + me.me_x,
                                this->lv_left + mc.mc_line_range.lr_start
                                    + me.me_x);
                            auto ds = data_scanner(line_sf);
                            auto tf = this->tc_sub_source->get_text_format();
                            while (true) {
                                auto tok_res = ds.tokenize2(tf);
                                if (!tok_res) {
                                    break;
                                }

                                auto tok = tok_res.value();
                                auto tok_sf
                                    = (tok.tr_token
                                           == data_token_t::DT_QUOTED_STRING
                                       && (cursor_sf.sf_begin
                                               == tok.to_string_fragment()
                                                      .sf_begin
                                           || cursor_sf.sf_begin
                                               == tok.to_string_fragment()
                                                       .sf_end
                                                   - 1))
                                    ? tok.to_string_fragment()
                                    : tok.inner_string_fragment();
                                if (tok_sf.contains(cursor_sf)
                                    && tok.tr_token != data_token_t::DT_WHITE)
                                {
                                    auto group_tok
                                        = ds.find_matching_bracket(tf, tok);
                                    if (group_tok) {
                                        tok_sf = group_tok.value()
                                                     .to_string_fragment();
                                    }
                                    this->tc_selected_text = selected_text_info{
                                        me.me_x,
                                        mc.mc_line,
                                        line_range{
                                            tok_sf.sf_begin,
                                            tok_sf.sf_end,
                                        },
                                        al.al_attrs,
                                        tok_sf.to_string(),
                                    };
                                    this->set_needs_update();
                                    break;
                                }
                            }
                        }
                        this->set_selection_without_context(mc.mc_line);
                    }
                    if (this->tc_delegate != nullptr) {
                        this->tc_delegate->text_handle_mouse(
                            *this, mouse_line, me);
                    }
                    if (sub_delegate != nullptr) {
                        sub_delegate->text_handle_mouse(*this, mouse_line, me);
                    }
                },
                [](const static_overlay_content& soc) {},
                [](const overlay_menu& om) {},
                [](const overlay_content& oc) {},
                [](const empty_space& es) {});
            break;
        }
        case mouse_button_state_t::BUTTON_STATE_DRAGGED: {
            this->tc_text_selection_active = true;
            if (!this->vc_enabled) {
            } else if (me.me_y == me.me_press_y) {
                if (mouse_line.is<main_content>()) {
                    auto& mc = mouse_line.get<main_content>();
                    attr_line_t al;
                    auto low_x
                        = std::min(this->tc_press_left,
                                   (int) this->lv_left
                                       + mc.mc_line_range.lr_start + me.me_x);
                    auto high_x
                        = std::max(this->tc_press_left,
                                   (int) this->lv_left
                                       + mc.mc_line_range.lr_start + me.me_x);

                    this->set_selection_without_context(mc.mc_line);
                    if (this->tc_supports_marks
                        && me.me_button == mouse_button_t::BUTTON_LEFT)
                    {
                        this->textview_value_for_row(mc.mc_line, al);
                        auto line_sf
                            = string_fragment::from_str(al.get_string());
                        auto cursor_sf = line_sf.sub_cell_range(low_x, high_x);
                        if (me.me_x <= 1) {
                            this->set_left(this->lv_left - 1);
                        } else if (me.me_x >= width - 1) {
                            this->set_left(this->lv_left + 1);
                        }
                        if (!cursor_sf.empty()) {
                            this->tc_selected_text = {
                                me.me_x,
                                mc.mc_line,
                                line_range{
                                    cursor_sf.sf_begin,
                                    cursor_sf.sf_end,
                                },
                                al.al_attrs,
                                cursor_sf.to_string(),
                            };
                        }
                    }
                }
            } else {
                if (this->tc_press_line.is<main_content>()) {
                    if (me.me_y < 0) {
                        this->shift_selection(shift_amount_t::up_line);
                    } else if (me.me_y >= height) {
                        this->shift_selection(shift_amount_t::down_line);
                    } else if (mouse_line.is<main_content>()) {
                        this->set_selection_without_context(
                            mouse_line.get<main_content>().mc_line);
                    }
                } else if (this->tc_press_line.is<overlay_content>()
                           && overlay_content_min_y && overlay_content_max_y)
                {
                    if (me.me_y < overlay_content_min_y.value()) {
                        this->set_overlay_selection(
                            this->get_overlay_selection().value_or(0_vl)
                            - 1_vl);
                    } else if (me.me_y >= overlay_content_max_y.value()) {
                        this->set_overlay_selection(
                            this->get_overlay_selection().value_or(0_vl)
                            + 1_vl);
                    } else if (mouse_line.is<overlay_content>()) {
                        this->set_overlay_selection(
                            mouse_line.get<overlay_content>().oc_line);
                    }
                }
            }
            break;
        }
        case mouse_button_state_t::BUTTON_STATE_RELEASED: {
            auto* ov = this->get_overlay_source();
            if (ov != nullptr && mouse_line.is<overlay_menu>()
                && this->tc_selected_text)
            {
                auto& om = mouse_line.get<overlay_menu>();
                auto& sti = this->tc_selected_text.value();

                for (const auto& mi : ov->los_menu_items) {
                    if (om.om_line == mi.mi_line
                        && me.is_click_in(mouse_button_t::BUTTON_LEFT,
                                          mi.mi_range))
                    {
                        mi.mi_action(sti.sti_value);
                        break;
                    }
                }
            }
            this->tc_text_selection_active = false;
            if (me.is_click_in(mouse_button_t::BUTTON_RIGHT, 0, INT_MAX)) {
                auto* lov = this->get_overlay_source();
                if (lov != nullptr) {
                    this->set_show_details_in_overlay(
                        !lov->get_show_details_in_overlay());
                }
            }
            if (this->vc_enabled) {
                if (this->tc_selection_start) {
                    this->toggle_user_mark(&BM_USER,
                                           this->tc_selection_start.value(),
                                           this->get_selection());
                    this->reload_data();
                }
                this->tc_selection_start = std::nullopt;
            }
            if (mouse_line.is<main_content>()) {
                const auto mc = mouse_line.get<main_content>();
                attr_line_t al;

                this->textview_value_for_row(mc.mc_line, al);
                auto line_sf = string_fragment::from_str(al.get_string());
                auto cursor_sf = line_sf.sub_cell_range(
                    this->lv_left + me.me_x, this->lv_left + me.me_x);
                auto link_iter = find_string_attr_containing(
                    al.get_attrs(), &VC_HYPERLINK, cursor_sf.sf_begin);
                if (link_iter != al.get_attrs().end()) {
                    auto href = link_iter->sa_value.get<std::string>();
                    auto* ta = dynamic_cast<text_anchors*>(this->tc_sub_source);

                    if (me.me_button == mouse_button_t::BUTTON_LEFT
                        && ta != nullptr && startswith(href, "#")
                        && !startswith(href, "#/frontmatter"))
                    {
                        auto row_opt = ta->row_for_anchor(href);

                        if (row_opt.has_value()) {
                            this->set_selection(row_opt.value());
                        }
                    } else {
                        this->tc_selected_text = selected_text_info{
                            me.me_x,
                            mc.mc_line,
                            link_iter->sa_range,
                            al.get_attrs(),
                            al.to_string_fragment(link_iter).to_string(),
                            href,
                        };
                    }
                }
                if (this->tc_on_click) {
                    this->tc_on_click(*this, al, cursor_sf.sf_begin);
                }
            }
            if (mouse_line.is<overlay_content>()) {
                const auto& oc = mouse_line.get<overlay_content>();
                std::vector<attr_line_t> ov_lines;

                this->lv_overlay_source->list_value_for_overlay(
                    *this, oc.oc_main_line, ov_lines);
                const auto& al = ov_lines[oc.oc_line];
                auto line_sf = string_fragment::from_str(al.get_string());
                auto cursor_sf = line_sf.sub_cell_range(
                    this->lv_left + me.me_x, this->lv_left + me.me_x);
                auto link_iter = find_string_attr_containing(
                    al.get_attrs(), &VC_HYPERLINK, cursor_sf.sf_begin);
                if (link_iter != al.get_attrs().end()) {
                    auto href = link_iter->sa_value.get<std::string>();
                    auto* ta = dynamic_cast<text_anchors*>(this->tc_sub_source);

                    if (me.me_button == mouse_button_t::BUTTON_LEFT
                        && ta != nullptr && startswith(href, "#")
                        && !startswith(href, "#/frontmatter"))
                    {
                        auto row_opt = ta->row_for_anchor(href);

                        if (row_opt.has_value()) {
                            this->tc_sub_source->get_location_history() |
                                [&oc](auto lh) {
                                    lh->loc_history_append(oc.oc_main_line);
                                };
                            this->set_selection(row_opt.value());
                        }
                    }
                }
                if (this->tc_on_click) {
                    this->tc_on_click(*this, al, cursor_sf.sf_begin);
                }
            }
            if (this->tc_delegate != nullptr) {
                this->tc_delegate->text_handle_mouse(*this, mouse_line, me);
            }
            if (sub_delegate != nullptr) {
                sub_delegate->text_handle_mouse(*this, mouse_line, me);
            }
            if (mouse_line.is<overlay_menu>()) {
                this->tc_selected_text = std::nullopt;
                this->set_needs_update();
            }
            break;
        }
    }

    return true;
}

void
textview_curses::apply_highlights(attr_line_t& al,
                                  const line_range& body,
                                  const line_range& orig_line)
{
    intern_string_t format_name;

    auto format_attr_opt = get_string_attr(al.al_attrs, SA_FORMAT);
    if (format_attr_opt.has_value()) {
        format_name = format_attr_opt.value().get();
    }

    auto source_format = this->tc_sub_source->get_text_format();
    if (source_format == text_format_t::TF_BINARY) {
        return;
    }
    for (const auto& tc_highlight : this->tc_highlights) {
        bool internal_hl
            = tc_highlight.first.first == highlight_source_t::INTERNAL
            || tc_highlight.first.first == highlight_source_t::THEME;

        if (!tc_highlight.second.applies_to_format(source_format)) {
            continue;
        }

        if (!tc_highlight.second.h_format_name.empty()
            && tc_highlight.second.h_format_name != format_name)
        {
            continue;
        }

        if (this->tc_disabled_highlights.count(tc_highlight.first.first)) {
            continue;
        }

        // Internal highlights should only apply to the log message body so
        // that we don't start highlighting other fields.  User-provided
        // highlights should apply only to the line itself and not any of
        // the surrounding decorations that are added (for example, the file
        // lines that are inserted at the beginning of the log view).
        int start_pos = internal_hl ? body.lr_start : orig_line.lr_start;
        tc_highlight.second.annotate(al, start_pos);
    }
}

void
textview_curses::textview_value_for_row(vis_line_t row, attr_line_t& value_out)
{
    auto& sa = value_out.get_attrs();
    auto& str = value_out.get_string();

    this->tc_sub_source->text_value_for_line(*this, row, str);
    this->tc_sub_source->text_attrs_for_line(*this, row, sa);

    for (const auto& attr : sa) {
        require_ge(attr.sa_range.lr_start, 0);
    }

    line_range body, orig_line;

    body = find_string_attr_range(sa, &SA_BODY);
    if (!body.is_valid()) {
        body.lr_start = 0;
        body.lr_end = str.size();
    }

    orig_line = find_string_attr_range(sa, &SA_ORIGINAL_LINE);
    if (!orig_line.is_valid()) {
        orig_line.lr_start = 0;
        orig_line.lr_end = str.size();
    }

    if (this->is_selectable() && this->tc_cursor_role
        && this->tc_disabled_cursor_role)
    {
        vis_line_t sel_start, sel_end;

        sel_start = sel_end = this->get_selection();
        if (this->tc_selection_start) {
            if (this->tc_selection_start.value() < sel_end) {
                sel_start = this->tc_selection_start.value();
            } else {
                sel_end = this->tc_selection_start.value();
            }
        }

        if (sel_start <= row && row <= sel_end) {
            auto role = (this->get_overlay_selection() || !this->vc_enabled)
                ? this->tc_disabled_cursor_role.value()
                : this->tc_cursor_role.value();

            sa.emplace_back(line_range{0, -1}, VC_ROLE.value(role));
        }
    }

    if (!body.empty() || !orig_line.empty()) {
        this->apply_highlights(value_out, body, orig_line);
    }

    if (this->tc_hide_fields) {
        value_out.apply_hide();
    }

    const auto& user_marks = this->tc_bookmarks[&BM_USER];
    const auto& user_expr_marks = this->tc_bookmarks[&BM_USER_EXPR];
    if (user_marks.bv_tree.exists(row) || user_expr_marks.bv_tree.exists(row)) {
        sa.emplace_back(line_range{orig_line.lr_start, -1},
                        VC_STYLE.value(text_attrs::with_reverse()));
    }

    if (this->tc_selected_text) {
        const auto& sti = this->tc_selected_text.value();
        if (sti.sti_line == row) {
            sa.emplace_back(sti.sti_range,
                            VC_ROLE.value(role_t::VCR_SELECTED_TEXT));
        }
    }
}

void
textview_curses::execute_search(const std::string& regex_orig)
{
    std::string regex = regex_orig;

    if ((this->tc_search_child == nullptr)
        || (regex != this->tc_current_search))
    {
        std::shared_ptr<lnav::pcre2pp::code> code;
        this->match_reset();

        this->tc_search_child.reset();
        this->tc_source_search_child.reset();

        log_debug("start search for: '%s'", regex.c_str());

        if (regex.empty()) {
        } else {
            auto compile_res = lnav::pcre2pp::code::from(regex, PCRE2_CASELESS);

            if (compile_res.isErr()) {
                auto ce = compile_res.unwrapErr();
                regex = lnav::pcre2pp::quote(regex);

                log_info("invalid search regex (%s), using quoted: %s",
                         ce.get_message().c_str(),
                         regex.c_str());

                auto compile_quote_res
                    = lnav::pcre2pp::code::from(regex, PCRE2_CASELESS);
                if (compile_quote_res.isErr()) {
                    log_error("Unable to compile quoted regex: %s",
                              regex.c_str());
                } else {
                    code = compile_quote_res.unwrap().to_shared();
                }
            } else {
                code = compile_res.unwrap().to_shared();
            }
        }

        if (code != nullptr) {
            highlighter hl(code);

            hl.with_role(role_t::VCR_SEARCH);

            auto& hm = this->get_highlights();
            hm[{highlight_source_t::PREVIEW, "search"}] = hl;

            auto gp = injector::get<std::shared_ptr<grep_proc<vis_line_t>>>(
                code, *this);

            gp->set_sink(this);
            auto top = this->get_top();
            if (top < REVERSE_SEARCH_OFFSET) {
                top = 0_vl;
            } else {
                top -= REVERSE_SEARCH_OFFSET;
            }
            gp->queue_request(top);
            if (top > 0) {
                gp->queue_request(0_vl, top);
            }
            this->tc_search_start_time = std::chrono::steady_clock::now();
            this->tc_search_duration = std::nullopt;
            gp->start();

            this->tc_search_child = std::make_shared<grep_highlighter>(
                gp, highlight_source_t::PREVIEW, "search", hm);

            if (this->tc_sub_source != nullptr) {
                this->tc_sub_source->get_grepper() | [this, code](auto pair) {
                    auto sgp
                        = injector::get<std::shared_ptr<grep_proc<vis_line_t>>>(
                            code, *pair.first);

                    sgp->set_sink(pair.second);
                    sgp->queue_request(0_vl);
                    sgp->start();

                    this->tc_source_search_child = sgp;
                };
            }
        }
    }

    this->tc_current_search = regex;
    if (this->tc_state_event_handler) {
        this->tc_state_event_handler(*this);
    }
}

std::optional<std::pair<int, int>>
textview_curses::horiz_shift(vis_line_t start, vis_line_t end, int off_start)
{
    auto hl_iter
        = this->tc_highlights.find({highlight_source_t::PREVIEW, "search"});
    if (hl_iter == this->tc_highlights.end()
        || hl_iter->second.h_regex == nullptr)
    {
        return std::nullopt;
    }
    int prev_hit = -1, next_hit = INT_MAX;

    for (; start < end; ++start) {
        std::vector<attr_line_t> rows(1);
        this->listview_value_for_rows(*this, start, rows);

        const auto& str = rows[0].get_string();
        hl_iter->second.h_regex->capture_from(str).for_each(
            [&](lnav::pcre2pp::match_data& md) {
                auto cap = md[0].value();
                if (cap.sf_begin < off_start) {
                    prev_hit = std::max(prev_hit, cap.sf_begin);
                } else if (cap.sf_begin > off_start) {
                    next_hit = std::min(next_hit, cap.sf_begin);
                }
            });
    }

    if (prev_hit == -1 && next_hit == INT_MAX) {
        return std::nullopt;
    }
    return std::make_pair(prev_hit, next_hit);
}

void
textview_curses::set_user_mark(const bookmark_type_t* bm,
                               vis_line_t vl,
                               bool marked)
{
    auto& bv = this->tc_bookmarks[bm];

    if (marked) {
        bv.insert_once(vl);
    } else {
        bv.bv_tree.erase(vl);
    }
    if (this->tc_sub_source) {
        this->tc_sub_source->text_mark(bm, vl, marked);
    }

    if (marked) {
        this->search_range(vl, vl + 1_vl);
        this->search_new_data();
    }
    this->set_needs_update();
}

void
textview_curses::toggle_user_mark(const bookmark_type_t* bm,
                                  vis_line_t start_line,
                                  vis_line_t end_line)
{
    if (end_line == -1) {
        end_line = start_line;
    }
    if (start_line > end_line) {
        std::swap(start_line, end_line);
    }

    if (start_line >= this->get_inner_height()) {
        return;
    }
    if (end_line >= this->get_inner_height()) {
        end_line = vis_line_t(this->get_inner_height() - 1);
    }
    for (auto curr_line = start_line; curr_line <= end_line; ++curr_line) {
        auto& bv = this->tc_bookmarks[bm];
        auto [insert_iter, added] = bv.insert_once(curr_line);
        if (this->tc_sub_source) {
            this->tc_sub_source->text_mark(bm, curr_line, added);
        }
    }
    this->search_range(start_line, end_line + 1_vl);
    this->search_new_data();
}

void
textview_curses::redo_search()
{
    if (this->tc_search_child) {
        auto* gp = this->tc_search_child->get_grep_proc();

        gp->invalidate();
        this->match_reset();
        gp->queue_request(0_vl).start();

        if (this->tc_source_search_child) {
            this->tc_source_search_child->invalidate()
                .queue_request(0_vl)
                .start();
        }
    }
}

bool
textview_curses::listview_is_row_selectable(const listview_curses& lv,
                                            vis_line_t row)
{
    if (this->tc_sub_source != nullptr) {
        return this->tc_sub_source->text_is_row_selectable(*this, row);
    }

    return true;
}

void
textview_curses::listview_selection_changed(const listview_curses& lv)
{
    if (this->tc_sub_source != nullptr) {
        this->tc_sub_source->text_selection_changed(*this);
    }
}

textview_curses&
textview_curses::set_sub_source(text_sub_source* src)
{
    if (this->tc_sub_source != src) {
        this->tc_bookmarks.clear();
        this->tc_sub_source = src;
        if (src) {
            src->register_view(this);
        }
        this->reload_data();
    }
    return *this;
}

std::optional<line_info>
textview_curses::grep_value_for_line(vis_line_t line, std::string& value_out)
{
    // log_debug("grep line %d", line);
    if (this->tc_sub_source
        && line < (int) this->tc_sub_source->text_line_count())
    {
        auto retval = this->tc_sub_source->text_value_for_line(
            *this, line, value_out, text_sub_source::RF_RAW);
        if (retval.li_utf8_scan_result.is_valid()
            && retval.li_utf8_scan_result.usr_has_ansi)
        {
            // log_debug("has ansi %d",
            // retval.li_utf8_scan_result.usr_has_ansi);
            auto new_size = erase_ansi_escapes(value_out);
            value_out.resize(new_size);
        }
        // log_debug("  line off %lld", retval.li_file_range.fr_offset);
        return retval;
    }

    return std::nullopt;
}

void
text_sub_source::scroll_invoked(textview_curses* tc)
{
    auto* ttt = dynamic_cast<text_time_translator*>(this);

    if (ttt != nullptr) {
        ttt->ttt_scroll_invoked(tc);
    }
}

void
text_time_translator::ttt_scroll_invoked(textview_curses* tc)
{
    if (tc->get_inner_height() > 0 && tc->get_selection() >= 0_vl) {
        this->time_for_row(tc->get_selection()) |
            [this](auto new_top_ri) { this->ttt_top_row_info = new_top_ri; };
    }
}

void
text_time_translator::data_reloaded(textview_curses* tc)
{
    if (tc->get_inner_height() == 0) {
        this->ttt_top_row_info = std::nullopt;
        return;
    }
    if (this->ttt_top_row_info) {
        this->row_for(this->ttt_top_row_info.value()) |
            [tc](auto new_top) { tc->set_selection(new_top); };
    }
}

template class bookmark_vector<vis_line_t>;

bool
empty_filter::matches(std::optional<line_source> ls,
                      const shared_buffer_ref& line)
{
    return false;
}

std::string
empty_filter::to_command() const
{
    return "";
}

std::optional<size_t>
filter_stack::next_index()
{
    bool used[32];

    memset(used, 0, sizeof(used));
    for (auto& iter : *this) {
        if (iter->lf_deleted) {
            continue;
        }

        size_t index = iter->get_index();

        require(used[index] == false);

        used[index] = true;
    }
    for (size_t lpc = this->fs_reserved;
         lpc < logfile_filter_state::MAX_FILTERS;
         lpc++)
    {
        if (!used[lpc]) {
            return lpc;
        }
    }
    return std::nullopt;
}

std::shared_ptr<text_filter>
filter_stack::get_filter(const std::string& id)
{
    auto iter = this->fs_filters.begin();
    std::shared_ptr<text_filter> retval;

    for (; iter != this->fs_filters.end() && (*iter)->get_id() != id; iter++) {
    }
    if (iter != this->fs_filters.end()) {
        retval = *iter;
    }

    return retval;
}

bool
filter_stack::delete_filter(const std::string& id)
{
    auto iter = this->fs_filters.begin();

    for (; iter != this->fs_filters.end() && (*iter)->get_id() != id; iter++) {
    }
    if (iter != this->fs_filters.end()) {
        this->fs_filters.erase(iter);
        return true;
    }

    return false;
}

void
filter_stack::get_mask(uint32_t& filter_mask)
{
    filter_mask = 0;
    for (auto& iter : *this) {
        std::shared_ptr<text_filter> tf = iter;

        if (tf->lf_deleted) {
            continue;
        }
        if (tf->is_enabled()) {
            uint32_t bit = (1UL << tf->get_index());

            switch (tf->get_type()) {
                case text_filter::EXCLUDE:
                case text_filter::INCLUDE:
                    filter_mask |= bit;
                    break;
                default:
                    ensure(0);
                    break;
            }
        }
    }
}

void
filter_stack::get_enabled_mask(uint32_t& filter_in_mask,
                               uint32_t& filter_out_mask)
{
    filter_in_mask = filter_out_mask = 0;
    for (auto& iter : *this) {
        std::shared_ptr<text_filter> tf = iter;

        if (tf->lf_deleted) {
            continue;
        }
        if (tf->is_enabled()) {
            uint32_t bit = (1UL << tf->get_index());

            switch (tf->get_type()) {
                case text_filter::EXCLUDE:
                    filter_out_mask |= bit;
                    break;
                case text_filter::INCLUDE:
                    filter_in_mask |= bit;
                    break;
                default:
                    ensure(0);
                    break;
            }
        }
    }
}

void
filter_stack::add_filter(const std::shared_ptr<text_filter>& filter)
{
    this->fs_filters.push_back(filter);
}

void
vis_location_history::loc_history_append(vis_line_t top)
{
    auto iter = this->vlh_history.begin();
    iter += this->vlh_history.size() - this->lh_history_position;
    this->vlh_history.erase_from(iter);
    this->lh_history_position = 0;
    this->vlh_history.push_back(top);
}

std::optional<vis_line_t>
vis_location_history::loc_history_back(vis_line_t current_top)
{
    if (this->lh_history_position == 0) {
        vis_line_t history_top = this->current_position();
        if (history_top != current_top) {
            return history_top;
        }
    }

    if (this->lh_history_position + 1 >= this->vlh_history.size()) {
        return std::nullopt;
    }

    this->lh_history_position += 1;

    return this->current_position();
}

std::optional<vis_line_t>
vis_location_history::loc_history_forward(vis_line_t current_top)
{
    if (this->lh_history_position == 0) {
        return std::nullopt;
    }

    this->lh_history_position -= 1;

    return this->current_position();
}

void
text_sub_source::toggle_apply_filters()
{
    this->tss_apply_filters = !this->tss_apply_filters;
    this->text_filters_changed();
}

void
text_sub_source::text_crumbs_for_line(int line,
                                      std::vector<breadcrumb::crumb>& crumbs)
{
}

logfile_filter_state::logfile_filter_state(std::shared_ptr<logfile> lf)
    : tfs_logfile(std::move(lf))
{
    memset(this->tfs_filter_count, 0, sizeof(this->tfs_filter_count));
    memset(this->tfs_filter_hits, 0, sizeof(this->tfs_filter_hits));
    memset(this->tfs_message_matched, 0, sizeof(this->tfs_message_matched));
    memset(this->tfs_lines_for_message, 0, sizeof(this->tfs_lines_for_message));
    memset(this->tfs_last_message_matched,
           0,
           sizeof(this->tfs_last_message_matched));
    memset(this->tfs_last_lines_for_message,
           0,
           sizeof(this->tfs_last_lines_for_message));
    this->tfs_mask.reserve(64 * 1024);
}

void
logfile_filter_state::clear()
{
    this->tfs_logfile = nullptr;
    memset(this->tfs_filter_count, 0, sizeof(this->tfs_filter_count));
    memset(this->tfs_filter_hits, 0, sizeof(this->tfs_filter_hits));
    memset(this->tfs_message_matched, 0, sizeof(this->tfs_message_matched));
    memset(this->tfs_lines_for_message, 0, sizeof(this->tfs_lines_for_message));
    memset(this->tfs_last_message_matched,
           0,
           sizeof(this->tfs_last_message_matched));
    memset(this->tfs_last_lines_for_message,
           0,
           sizeof(this->tfs_last_lines_for_message));
    this->tfs_mask.clear();
    this->tfs_index.clear();
}

void
logfile_filter_state::clear_filter_state(size_t index)
{
    this->tfs_filter_count[index] = 0;
    this->tfs_filter_hits[index] = 0;
    this->tfs_message_matched[index] = false;
    this->tfs_lines_for_message[index] = 0;
    this->tfs_last_message_matched[index] = false;
    this->tfs_last_lines_for_message[index] = 0;
}

void
logfile_filter_state::clear_deleted_filter_state(uint32_t used_mask)
{
    for (int lpc = 0; lpc < MAX_FILTERS; lpc++) {
        if (!(used_mask & (1L << lpc))) {
            this->clear_filter_state(lpc);
        }
    }
    for (size_t lpc = 0; lpc < this->tfs_mask.size(); lpc++) {
        this->tfs_mask[lpc] &= used_mask;
    }
}

void
logfile_filter_state::resize(size_t newsize)
{
    size_t old_mask_size = this->tfs_mask.size();

    this->tfs_mask.resize(newsize);
    if (newsize > old_mask_size) {
        memset(&this->tfs_mask[old_mask_size],
               0,
               sizeof(uint32_t) * (newsize - old_mask_size));
    }
}

void
logfile_filter_state::reserve(size_t expected)
{
    this->tfs_mask.reserve(expected);
}

std::optional<size_t>
logfile_filter_state::content_line_to_vis_line(uint32_t line)
{
    if (this->tfs_index.empty()) {
        return std::nullopt;
    }

    auto iter = std::lower_bound(
        this->tfs_index.begin(), this->tfs_index.end(), line);

    if (iter == this->tfs_index.end() || *iter != line) {
        return std::nullopt;
    }

    return std::make_optional(std::distance(this->tfs_index.begin(), iter));
}

std::string
text_anchors::to_anchor_string(const std::string& raw)
{
    static const auto ANCHOR_RE = lnav::pcre2pp::code::from_const(R"([^\w]+)");

    return fmt::format(FMT_STRING("#{}"), ANCHOR_RE.replace(tolower(raw), "-"));
}
