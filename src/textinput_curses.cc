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

#include "base/attr_line.hh"
#include "base/auto_mem.hh"
#include "base/itertools.hh"
#include "base/keycodes.hh"
#include "base/string_attr_type.hh"
#include "config.h"
#include "data_parser.hh"
#include "data_scanner.hh"
#include "readline_highlighters.hh"
#include "sysclip.hh"
#include "ww898/cp_utf8.hpp"

textinput_curses::textinput_curses()
{
    this->vc_children.emplace_back(&this->tc_popup);
    this->tc_popup_source.set_reverse_selection(true);
    this->tc_popup.set_visible(false);
    this->tc_popup.set_title("textinput popup");
    this->tc_popup.set_selectable(true);
    this->tc_popup.set_show_scrollbar(true);
    this->tc_popup.set_default_role(role_t::VCR_POPUP);
    this->tc_popup.set_sub_source(&this->tc_popup_source);
}

void
textinput_curses::set_content(const attr_line_t& al)
{
    auto al_copy = al;

    highlight_syntax(this->tc_text_format, al_copy);
    this->tc_doc_meta = lnav::document::discover(al_copy)
                            .with_text_format(this->tc_text_format)
                            .save_words()
                            .perform();
    log_debug("doc indents:");
    for (auto ind : this->tc_doc_meta.m_indents) {
        log_debug("  indent=%d", ind);
    }
    lnav::document::hier_node::depth_first(
        this->tc_doc_meta.m_sections_root.get(),
        [this](lnav::document::hier_node* hn) {
            log_debug("hier_node: %p %s",
                      hn,
                      fmt::format(FMT_STRING("{}"),
                                  this->tc_doc_meta.path_for_range(
                                      hn->hn_start, hn->hn_start))
                          .c_str());
        });
    this->tc_lines = al_copy.split_lines();
    if (this->tc_lines.empty()) {
        this->tc_lines.emplace_back();
    } else {
        this->apply_highlights();
    }
    this->tc_left = 0;
    this->tc_top = 0;
    this->tc_cursor = {};
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
    auto inner_height = this->tc_lines.size();

    log_debug("mouse here! button=%d state=%d x=%d y=%d",
              me.me_button,
              me.me_state,
              me.me_x,
              me.me_y);

    if (me.me_button == mouse_button_t::BUTTON_SCROLL_UP) {
        auto dim = this->get_visible_dimensions();
        if (this->tc_top > 0) {
            this->tc_top -= 1;
            if (this->tc_top + dim.dr_height - 2 < this->tc_cursor.y) {
                this->move_cursor_by({direction_t::up, 1});
            } else {
                this->ensure_cursor_visible();
            }
        }
    } else if (me.me_button == mouse_button_t::BUTTON_SCROLL_DOWN) {
        auto dim = this->get_visible_dimensions();
        if (this->tc_top + dim.dr_height < inner_height) {
            this->tc_top += 1;
            if (this->tc_cursor.y <= this->tc_top) {
                this->tc_cursor.y = this->tc_top + 1;
            }
        }
        this->ensure_cursor_visible();
    } else if (me.me_button == mouse_button_t::BUTTON_LEFT) {
        this->tc_mode = mode_t::editing;
        auto inner_press_point = input_point{
            this->tc_left + me.me_press_x,
            (int) this->tc_top + me.me_press_y,
        };
        this->clamp_point(inner_press_point);
        auto inner_point = input_point{
            this->tc_left + me.me_x,
            (int) this->tc_top + me.me_y,
        };
        this->clamp_point(inner_point);
        auto sel_range
            = selected_range::from_mouse(inner_press_point, inner_point);

        this->tc_popup.set_visible(false);
        this->tc_complete_range = std::nullopt;
        this->tc_cursor = inner_point;
        log_debug("new cursor x=%d y=%d", this->tc_cursor.x, this->tc_cursor.y);
        if (me.me_state == mouse_button_state_t::BUTTON_STATE_DOUBLE_CLICK) {
            const auto& al = this->tc_lines[this->tc_cursor.y];
            auto sf = string_fragment::from_str(al.al_string);
            auto cursor_sf = sf.sub_cell_range(this->tc_left + me.me_x,
                                               this->tc_left + me.me_x);
            auto ds = data_scanner(sf);

            while (true) {
                auto tok_res = ds.tokenize2(this->tc_text_format);
                if (!tok_res.has_value()) {
                    break;
                }

                auto tok = tok_res.value();
                log_debug("tok %d", tok.tr_token);

                auto tok_sf = (tok.tr_token == data_token_t::DT_QUOTED_STRING
                               && (cursor_sf.sf_begin
                                       == tok.to_string_fragment().sf_begin
                                   || cursor_sf.sf_begin
                                       == tok.to_string_fragment().sf_end - 1))
                    ? tok.to_string_fragment()
                    : tok.inner_string_fragment();
                log_debug("tok %d:%d  curs %d:%d",
                          tok_sf.sf_begin,
                          tok_sf.sf_end,
                          cursor_sf.sf_begin,
                          cursor_sf.sf_end);
                if (tok_sf.contains(cursor_sf)
                    && tok.tr_token != data_token_t::DT_WHITE)
                {
                    log_debug("hit!");
                    auto group_tok
                        = ds.find_matching_bracket(this->tc_text_format, tok);
                    if (group_tok) {
                        tok_sf = group_tok.value().to_string_fragment();
                    }
                    auto tok_start = input_point{
                        (int) sf.byte_to_column_index(tok_sf.sf_begin)
                            - this->tc_left,
                        this->tc_cursor.y,
                    };
                    auto tok_end = input_point{
                        (int) sf.byte_to_column_index(tok_sf.sf_end)
                            - this->tc_left,
                        this->tc_cursor.y,
                    };

                    log_debug("st %d:%d", tok_start.x, tok_end.x);
                    this->tc_drag_selection = std::nullopt;
                    this->tc_selection
                        = selected_range::from_mouse(tok_start, tok_end);
                    this->set_needs_update();
                }
            }
        } else if (me.me_state == mouse_button_state_t::BUTTON_STATE_PRESSED) {
            this->tc_selection = std::nullopt;
            this->tc_drag_selection = sel_range;
        } else if (me.me_state == mouse_button_state_t::BUTTON_STATE_DRAGGED) {
            this->tc_drag_selection = sel_range;
        } else if (me.me_state == mouse_button_state_t::BUTTON_STATE_RELEASED) {
            this->tc_drag_selection = std::nullopt;
            if (inner_press_point == inner_point) {
                this->tc_selection = std::nullopt;
            } else {
                this->tc_selection = sel_range;
            }
        }
        this->ensure_cursor_visible();
    }

    return true;
}

bool
textinput_curses::handle_search_key(const ncinput& ch)
{
    if (ncinput_ctrl_p(&ch)) {
        switch (ch.id) {
            case 's':
            case 'S': {
                this->tc_search_start_point = this->tc_cursor;
                this->move_cursor_to_next_search_hit();
                return true;
            }
            case 'r':
            case 'R': {
                this->tc_search_start_point = this->tc_cursor;
                this->move_cursor_to_prev_search_hit();
                return true;
            }
        }
        return false;
    }

    switch (ch.id) {
        case NCKEY_ESC:
            this->tc_mode = mode_t::editing;
            this->set_needs_update();
            return true;
        case NCKEY_BACKSPACE: {
            if (!this->tc_search.empty()) {
                this->tc_search.pop_back();
                auto compile_res = lnav::pcre2pp::code::from(
                    lnav::pcre2pp::quote(this->tc_search), PCRE2_CASELESS);
                this->tc_search_code = compile_res.unwrap().to_shared();
                this->move_cursor_to_next_search_hit();
            }
            return true;
        }
        case NCKEY_ENTER: {
            this->tc_search_start_point = this->tc_cursor;
            this->move_cursor_to_next_search_hit();
            break;
        }
        case NCKEY_LEFT:
        case NCKEY_RIGHT:
        case NCKEY_UP:
        case NCKEY_DOWN: {
            this->tc_mode = mode_t::editing;
            this->handle_key(ch);
            break;
        }
        default: {
            char utf8[32];
            size_t index = 0;
            for (const auto eff_ch : ch.eff_text) {
                log_debug(" eff %x", eff_ch);
                if (eff_ch == 0) {
                    break;
                }
                ww898::utf::utf8::write(eff_ch,
                                        [&utf8, &index](const char bits) {
                                            utf8[index] = bits;
                                            index += 1;
                                        });
            }
            if (index > 0) {
                utf8[index] = 0;

                this->tc_search.append(utf8);
                if (!this->tc_search.empty()) {
                    auto compile_res = lnav::pcre2pp::code::from(
                        lnav::pcre2pp::quote(this->tc_search), PCRE2_CASELESS);
                    this->tc_search_code = compile_res.unwrap().to_shared();
                    this->move_cursor_to_next_search_hit();
                }
            }
            return true;
        }
    }

    return false;
}

void
textinput_curses::move_cursor_to_next_search_hit()
{
    auto x = this->tc_search_start_point.x;
    if (this->tc_search_found && !this->tc_search_found.value()) {
        this->tc_search_start_point.y = 0;
    }
    this->tc_search_found = false;
    for (auto y = this->tc_search_start_point.y; y < this->tc_lines.size(); y++)
    {
        thread_local auto md = lnav::pcre2pp::match_data::unitialized();

        const auto& al = this->tc_lines[y];
        auto byte_x = al.column_to_byte_index(x);
        auto after_x_sf = al.to_string_fragment().substr(byte_x);
        auto find_res = this->tc_search_code->capture_from(after_x_sf)
                            .into(md)
                            .matches()
                            .ignore_error();
        if (find_res) {
            this->tc_cursor.x
                = al.byte_to_column_index(find_res.value().f_all.sf_end);
            this->tc_cursor.y = y;
            log_debug(
                "search found %d:%d", this->tc_cursor.x, this->tc_cursor.y);
            this->tc_search_found = true;
            this->ensure_cursor_visible();
            break;
        }
        x = 0;
    }
    this->set_needs_update();
}

void
textinput_curses::move_cursor_to_prev_search_hit()
{
    auto max_x = std::make_optional(this->tc_search_start_point.x);
    if (this->tc_search_found && !this->tc_search_found.value()) {
        this->tc_search_start_point.y = this->tc_lines.size() - 1;
    }
    this->tc_search_found = false;
    for (auto y = this->tc_search_start_point.y; y >= 0; y--) {
        thread_local auto md = lnav::pcre2pp::match_data::unitialized();

        const auto& al = this->tc_lines[y];
        auto before_x_sf = al.to_string_fragment();
        if (max_x) {
            before_x_sf = before_x_sf.sub_cell_range(0, max_x.value());
        }
        auto find_res = this->tc_search_code->capture_from(before_x_sf)
                            .into(md)
                            .matches()
                            .ignore_error();
        if (find_res) {
            auto new_input_point = input_point{
                (int) al.byte_to_column_index(find_res.value().f_all.sf_end),
                y,
            };
            if (new_input_point != this->tc_cursor) {
                this->tc_cursor = new_input_point;
                this->tc_search_found = true;
                this->ensure_cursor_visible();
                break;
            }
        }
        max_x = std::nullopt;
    }
    this->set_needs_update();
}

bool
textinput_curses::handle_key(const ncinput& ch)
{
    if (this->tc_mode == mode_t::searching) {
        return this->handle_search_key(ch);
    }

    auto dim = this->get_visible_dimensions();
    auto inner_height = this->tc_lines.size();
    auto bottom = inner_height - 1;
    auto chid = ch.id;

    if (ch.id == NCKEY_PASTE) {
        static const auto lf_re = lnav::pcre2pp::code::from_const("\r\n?");
        auto paste_sf = string_fragment::from_c_str(ch.paste_content);
        if (!this->tc_selection) {
            this->tc_selection = selected_range::from_point(this->tc_cursor);
        }
        auto text = lf_re.replace(paste_sf, "\n");
        this->replace_selection(text);
        return true;
    }

    if (ncinput_alt_p(&ch)) {
        log_debug("alt pressed");
        switch (chid) {
            case 'f':
            case 'F': {
                log_debug("next word");
                return true;
            }
            case NCKEY_LEFT: {
                auto& al = this->tc_lines[this->tc_cursor.y];
                auto next_col_opt = string_fragment::from_str(al.al_string)
                                        .prev_word(this->tc_cursor.x);

                this->move_cursor_to(
                    this->tc_cursor.copy_with_x(next_col_opt.value_or(0)));
                return true;
            }
            case NCKEY_RIGHT: {
                auto& al = this->tc_lines[this->tc_cursor.y];
                auto next_col_opt = string_fragment::from_str(al.al_string)
                                        .next_word(this->tc_cursor.x);
                this->move_cursor_to(
                    this->tc_cursor.copy_with_x(next_col_opt.value_or(
                        this->tc_lines[this->tc_cursor.y].column_width())));
                return true;
            }
        }
    }

    if (ncinput_ctrl_p(&ch)) {
        switch (ch.id) {
            case 'a':
            case 'A': {
                this->move_cursor_to(this->tc_cursor.copy_with_x(0));
                return true;
            }
            case 'b':
            case 'B': {
                chid = NCKEY_LEFT;
                break;
            }
            case 'e':
            case 'E': {
                this->move_cursor_to(this->tc_cursor.copy_with_x(
                    this->tc_lines[this->tc_cursor.y].column_width()));
                return true;
            }
            case 'f':
            case 'F': {
                chid = NCKEY_RIGHT;
                break;
            }
            case 'k':
            case 'K': {
                if (this->tc_selection) {
                    auto range = this->tc_selection;
                    auto new_clip = std::string();
                    for (auto curr_line = range->sr_start.y;
                         curr_line <= range->sr_end.y;
                         ++curr_line)
                    {
                        auto sel_range = range->range_for_line(curr_line);
                        if (!sel_range) {
                            continue;
                        }

                        auto& al = this->tc_lines[curr_line];
                        auto start_byte
                            = al.column_to_byte_index(sel_range->lr_start);
                        auto end_byte
                            = al.column_to_byte_index(sel_range->lr_end);
                        auto sub
                            = al.subline(start_byte, end_byte - start_byte);
                        if (curr_line > range->sr_start.y) {
                            new_clip.push_back('\n');
                        }
                        new_clip.append(sub.al_string);
                    }
                    this->tc_clipboard.clear();
                    this->tc_clipboard.emplace_back(new_clip);
                    this->replace_selection(string_fragment{});
                } else {
                    if (this->tc_cursor != this->tc_cut_location) {
                        this->tc_clipboard.clear();
                    }
                    this->tc_cut_location = this->tc_cursor;
                    auto& al = this->tc_lines[this->tc_cursor.y];
                    auto byte_index
                        = al.column_to_byte_index(this->tc_cursor.x);
                    this->tc_clipboard.emplace_back(
                        al.subline(byte_index).al_string);
                    al.erase(byte_index);
                    if (this->tc_clipboard.back().empty()
                        && this->tc_cursor.y + 1 < this->tc_lines.size())
                    {
                        this->tc_clipboard.back().push_back('\n');
                        al.append(this->tc_lines[this->tc_cursor.y + 1]);
                        this->tc_lines.erase(this->tc_lines.begin()
                                             + this->tc_cursor.y + 1);
                    }
                }
                {
                    auto clip_open_res
                        = sysclip::open(sysclip::type_t::GENERAL);

                    if (clip_open_res.isOk()) {
                        auto clip_file = clip_open_res.unwrap();
                        fprintf(clip_file.in(),
                                "%s",
                                this->tc_clipboard.back().c_str());
                    } else {
                        auto err_msg = clip_open_res.unwrapErr();
                        log_error("unable to open clipboard: %s",
                                  err_msg.c_str());
                    }
                }
                log_debug("clip contents") for (const auto& line :
                                                this->tc_clipboard)
                {
                    log_debug("line '%s'", line.c_str());
                }
                this->tc_drag_selection = std::nullopt;
                this->update_lines();
                return true;
            }
            case 's':
            case 'S': {
                this->tc_mode = mode_t::searching;
                this->tc_search_start_point = this->tc_cursor;
                this->tc_search_found = std::nullopt;
                this->set_needs_update();
                return true;
            }
            case 'u':
            case 'U': {
                auto& al = this->tc_lines[this->tc_cursor.y];
                auto byte_index = al.column_to_byte_index(this->tc_cursor.x);
                this->tc_clipboard.emplace_back(
                    al.subline(0, byte_index).al_string);
                al.erase(0, byte_index);
                this->tc_cursor.x = 0;
                this->tc_selection = std::nullopt;
                this->tc_drag_selection = std::nullopt;
                this->update_lines();
                return true;
            }
            case 'y':
            case 'Y': {
                auto open_clip_res = sysclip::open(sysclip::type_t::GENERAL,
                                                   sysclip::op_t::READ);
                if (open_clip_res.isOk()) {
                    auto clip_file = open_clip_res.unwrap();
                    auto buf = auto_buffer::alloc(1024);

                    while (true) {
                        if (buf.available() == 0) {
                            buf.expand_by(1024);
                        }

                        auto rc = fread(buf.in(),
                                        sizeof(char),
                                        buf.available(),
                                        clip_file.in());
                        log_debug("fread returned: %d", rc);
                        if (rc == 0) {
                            break;
                        }
                        buf.resize_by(rc);
                    }

                    auto from_sysclip = buf.to_string();
                    if (this->tc_clipboard.empty()
                        || from_sysclip != this->tc_clipboard.back())
                    {
                        this->tc_clipboard.emplace_back(from_sysclip);
                    }
                }

                for (const auto& clipping : this->tc_clipboard) {
                    auto& al = this->tc_lines[this->tc_cursor.y];
                    log_debug("before insert: '%s'", al.al_string.c_str());
                    al.insert(al.column_to_byte_index(this->tc_cursor.x),
                              clipping);
                    log_debug("after insert: '%s'", al.al_string.c_str());
                    const auto clip_sf = string_fragment::from_str(clipping);
                    const auto clip_cols
                        = clip_sf
                              .find_left_boundary(clip_sf.length(),
                                                  string_fragment::tag1{'\n'})
                              .column_width();
                    auto line_count = clip_sf.count('\n');
                    if (line_count > 0) {
                        this->tc_cursor.x = 0;
                    } else {
                        this->tc_cursor.x += clip_cols;
                    }
                    this->tc_cursor.y += line_count;
                    this->tc_selection = std::nullopt;
                    this->tc_drag_selection = std::nullopt;
                    this->update_lines();
                }
                return true;
            }
            default:
                return false;
        }
    }

    log_debug("chid %x", chid);
    switch (chid) {
        case NCKEY_ESC:
        case KEY_CTRL(']'): {
            if (this->tc_popup.is_visible()) {
                this->tc_popup.set_visible(false);
                this->tc_complete_range = std::nullopt;
                this->set_needs_update();
            } else if (this->tc_on_abort) {
                this->tc_on_abort(*this);
            }

            this->tc_selection = std::nullopt;
            this->tc_drag_selection = std::nullopt;
            return true;
        }
        case NCKEY_ENTER: {
            if (this->tc_popup.is_visible()) {
                if (this->tc_on_completion) {
                    this->tc_on_completion(*this);
                }
                this->tc_popup.set_visible(false);
                this->tc_complete_range = std::nullopt;
                this->set_needs_update();
            } else {
                if (!this->tc_selection) {
                    this->tc_selection
                        = selected_range::from_point(this->tc_cursor);
                }
                auto indent = std::string("\n");
                auto& al = this->tc_lines[this->tc_cursor.y];
                auto indent_and_body = al.to_string_fragment().split_when(
                    [](auto ch) { return !isspace(ch); });
                indent.append(indent_and_body.first.data(),
                              indent_and_body.first.length());
                this->replace_selection(indent);
            }
            return true;
        }
        case NCKEY_TAB: {
            if (this->tc_popup.is_visible()) {
                if (this->tc_on_completion) {
                    this->tc_on_completion(*this);
                }
                this->tc_popup.set_visible(false);
                this->tc_complete_range = std::nullopt;
                this->set_needs_update();
            } else if (!this->tc_selection) {
                auto indent_amount = 4;
                auto line_sf
                    = this->tc_lines[this->tc_cursor.y].to_string_fragment();
                const auto [before, after]
                    = line_sf.split_when([](auto ch) { return !isspace(ch); });
                auto indent_iter
                    = std::lower_bound(this->tc_doc_meta.m_indents.begin(),
                                       this->tc_doc_meta.m_indents.end(),
                                       before.length());
                if (indent_iter != this->tc_doc_meta.m_indents.end()) {
                    if (ncinput_shift_p(&ch)) {
                        if (indent_iter == this->tc_doc_meta.m_indents.begin())
                        {
                            indent_amount = 0;
                        } else {
                            indent_amount = *std::prev(indent_iter);
                        }
                    } else if (before.empty()) {
                        indent_amount = *indent_iter;
                    } else {
                        auto next_indent_iter = std::next(indent_iter);
                        if (next_indent_iter
                            == this->tc_doc_meta.m_indents.end())
                        {
                            indent_amount += *indent_iter;
                        } else {
                            indent_amount = *next_indent_iter;
                        }
                    }
                }
                this->tc_selection = selected_range::from_key(
                    this->tc_cursor.copy_with_x(0),
                    this->tc_cursor.copy_with_x(before.length()));
                auto indent = std::string(indent_amount, ' ');
                auto old_cursor = this->tc_cursor;
                this->replace_selection(indent);
                this->tc_cursor.x
                    = indent.length() - before.length() + old_cursor.x;
            }
            break;
        }
        case NCKEY_HOME: {
            this->move_cursor_to({0, 0});
            return true;
        }
        case NCKEY_END: {
            this->move_cursor_to({0, (int) bottom});
            return true;
        }
        case NCKEY_PGUP: {
            if (this->tc_cursor.y > 0) {
                this->move_cursor_by({direction_t::up, (size_t) dim.dr_height});
            }
            return true;
        }
        case NCKEY_PGDOWN: {
            if (this->tc_cursor.y < bottom) {
                this->move_cursor_by(
                    {direction_t::down, (size_t) dim.dr_height});
            }
            return true;
        }
        case NCKEY_DEL: {
            this->tc_selection = selected_range::from_key(
                this->tc_cursor,
                this->tc_cursor + movement{direction_t::right, 1});
            this->replace_selection(string_fragment{});
            break;
        }
        case NCKEY_BACKSPACE: {
            if (!this->tc_selection) {
                auto line_sf
                    = this->tc_lines[this->tc_cursor.y].to_string_fragment();
                const auto [before, after]
                    = line_sf
                          .split_n(
                              line_sf.column_to_byte_index(this->tc_cursor.x))
                          .value();

                log_debug("before: '%.*s'", before.length(), before.data());
                log_debug("after: '%.*s'", after.length(), after.data());
                if (this->tc_cursor.x > 0 && before.trim().empty()) {
                    log_debug("x %d", this->tc_cursor.x);
                    auto indent_iter
                        = std::lower_bound(this->tc_doc_meta.m_indents.begin(),
                                           this->tc_doc_meta.m_indents.end(),
                                           this->tc_cursor.x);
                    if (indent_iter != this->tc_doc_meta.m_indents.end()) {
                        log_debug("eh? %d", *indent_iter);
                        if (indent_iter == this->tc_doc_meta.m_indents.begin())
                        {
                            this->tc_selection = selected_range::from_key(
                                this->tc_cursor.copy_with_x(0),
                                this->tc_cursor);
                        } else {
                            auto prev_indent_iter = std::prev(indent_iter);
                            this->tc_selection = selected_range::from_key(
                                this->tc_cursor.copy_with_x(*prev_indent_iter),
                                this->tc_cursor);
                        }
                    }
                }
                if (!this->tc_selection) {
                    this->tc_selection
                        = selected_range::from_point_and_movement(
                            this->tc_cursor, movement{direction_t::left, 1});
                }
            }
            this->replace_selection(string_fragment{});
            return true;
        }
        case NCKEY_UP: {
            if (this->tc_popup.is_visible()) {
                this->tc_popup.handle_key(ch);
            } else {
                if (ncinput_shift_p(&ch)) {
                    log_debug("up shift");
                    if (!this->tc_selection) {
                        this->tc_cursor_anchor = this->tc_cursor;
                    }
                }
                if (this->tc_cursor.y > 0) {
                    this->move_cursor_by({direction_t::up, 1});
                } else {
                    this->move_cursor_to({0, 0});
                }
                if (ncinput_shift_p(&ch)) {
                    this->tc_selection = selected_range::from_key(
                        this->tc_cursor_anchor, this->tc_cursor);
                }
            }
            return true;
        }
        case NCKEY_DOWN: {
            if (this->tc_popup.is_visible()) {
                this->tc_popup.handle_key(ch);
            } else {
                if (ncinput_shift_p(&ch)) {
                    if (!this->tc_selection) {
                        this->tc_cursor_anchor = this->tc_cursor;
                    }
                }
                if (this->tc_cursor.y + 1 < inner_height) {
                    this->move_cursor_by({direction_t::down, 1});
                } else {
                    this->move_cursor_to({
                        (int) this->tc_lines[this->tc_cursor.y].column_width(),
                        (int) this->tc_lines.size() - 1,
                    });
                }
                if (ncinput_shift_p(&ch)) {
                    this->tc_selection = selected_range::from_key(
                        this->tc_cursor_anchor, this->tc_cursor);
                }
            }
            return true;
        }
        case NCKEY_LEFT: {
            if (ncinput_shift_p(&ch)) {
                if (!this->tc_selection) {
                    this->tc_cursor_anchor = this->tc_cursor;
                }
            }
            this->move_cursor_by({direction_t::left, 1});
            if (ncinput_shift_p(&ch)) {
                this->tc_selection = selected_range::from_key(
                    this->tc_cursor_anchor, this->tc_cursor);
            }
            return true;
        }
        case NCKEY_RIGHT: {
            if (ncinput_shift_p(&ch)) {
                if (!this->tc_selection) {
                    this->tc_cursor_anchor = this->tc_cursor;
                }
            }
            this->move_cursor_by({direction_t::right, 1});
            if (ncinput_shift_p(&ch)) {
                this->tc_selection = selected_range::from_key(
                    this->tc_cursor_anchor, this->tc_cursor);
            }
            return true;
        }
        default: {
            char utf8[32];
            size_t index = 0;
            for (const auto eff_ch : ch.eff_text) {
                log_debug(" eff %x", eff_ch);
                if (eff_ch == 0) {
                    break;
                }
                ww898::utf::utf8::write(eff_ch,
                                        [&utf8, &index](const char bits) {
                                            utf8[index] = bits;
                                            index += 1;
                                        });
            }
            if (index > 0) {
                utf8[index] = 0;

                if (!this->tc_selection) {
                    this->tc_selection
                        = selected_range::from_point(this->tc_cursor);
                }
                this->replace_selection(string_fragment::from_c_str(utf8));
            }
            return true;
        }
    }

    return false;
}

void
textinput_curses::ensure_cursor_visible()
{
    auto dim = this->get_visible_dimensions();

    this->clamp_point(this->tc_cursor);
    if (this->tc_cursor.y < 0) {
        this->tc_cursor.y = 0;
    }
    if (this->tc_cursor.y >= this->tc_lines.size()) {
        this->tc_cursor.y = this->tc_lines.size() - 1;
    }
    if (this->tc_cursor.x < 0) {
        this->tc_cursor.x = 0;
    }
    if (this->tc_cursor.x >= this->tc_lines[this->tc_cursor.y].column_width()) {
        this->tc_cursor.x = this->tc_lines[this->tc_cursor.y].column_width();
    }

    if (this->tc_cursor.x <= this->tc_left) {
        this->tc_left = this->tc_cursor.x;
        if (this->tc_left > 0) {
            this->tc_left -= 1;
        }
    }
    if (this->tc_cursor.x >= this->tc_left + (dim.dr_width - 2)) {
        this->tc_left = (this->tc_cursor.x - dim.dr_width) + 2;
    }
    if (this->tc_top >= this->tc_cursor.y) {
        this->tc_top = this->tc_cursor.y;
        if (this->tc_top > 0) {
            this->tc_top -= 1;
        }
    }
    if (this->tc_cursor.y + 1 >= this->tc_top + dim.dr_height) {
        this->tc_top = (this->tc_cursor.y + 1 - dim.dr_height) + 1;
    }
    if (this->tc_top + dim.dr_height > this->tc_lines.size()) {
        if (this->tc_lines.size() > dim.dr_height) {
            this->tc_top = this->tc_lines.size() - dim.dr_height + 1;
        } else {
            this->tc_top = 0;
        }
    }
    if (this->tc_popup.is_visible() && this->tc_complete_range
        && !this->tc_complete_range->contains(this->tc_cursor))
    {
        this->tc_popup.set_visible(false);
        this->tc_complete_range = std::nullopt;
    }

    this->set_needs_update();
}

void
textinput_curses::apply_highlights()
{
    for (auto& line : this->tc_lines) {
        for (const auto& hl_pair : this->tc_highlights) {
            const auto& hl = hl_pair.second;

            if (!hl.applies_to_format(this->tc_text_format)) {
                continue;
            }
            hl.annotate(line, 0);
        }
    }
}

void
textinput_curses::replace_selection(string_fragment sf)
{
    if (!this->tc_selection) {
        return;
    }

    std::optional<int> del_max;
    auto full_first_line = false;

    auto range = std::exchange(this->tc_selection, std::nullopt).value();
    this->tc_cursor.y = range.sr_start.y;
    for (auto curr_line = range.sr_start.y; curr_line <= range.sr_end.y;
         ++curr_line)
    {
        auto sel_range = range.range_for_line(curr_line);

        if (!sel_range) {
            continue;
        }

        log_debug("sel_range y=%d [%d:%d)",
                  curr_line,
                  sel_range->lr_start,
                  sel_range->lr_end);
        if (sel_range->lr_start < 0) {
            if (curr_line > 0) {
                log_debug("append %d to %d", curr_line, curr_line - 1);
                this->tc_cursor.x
                    = this->tc_lines[curr_line - 1].column_width();
                this->tc_cursor.y = curr_line - 1;
                this->tc_lines[curr_line - 1].append(this->tc_lines[curr_line]);
                del_max = curr_line;
                full_first_line = true;
            }
        } else if (sel_range->lr_start
                       == this->tc_lines[curr_line].column_width()
                   && sel_range->lr_end != -1
                   && sel_range->lr_start < sel_range->lr_end)
        {
            this->tc_lines[curr_line].append(this->tc_lines[curr_line + 1]);
            del_max = curr_line + 1;
        } else if (sel_range->lr_start == 0 && sel_range->lr_end == -1) {
            log_debug("wtf");
            del_max = curr_line;
            if (curr_line == range.sr_start.y) {
                log_debug("full first");
                full_first_line = true;
            }
        } else {
            auto& al = this->tc_lines[curr_line];

            auto start = al.column_to_byte_index(sel_range->lr_start);
            auto end = sel_range->lr_end == -1
                ? al.al_string.length()
                : al.column_to_byte_index(sel_range->lr_end);
            this->tc_lines[curr_line].erase(start, end - start);
            if (curr_line == range.sr_start.y) {
                this->tc_lines[curr_line].insert(start, sf.to_string());
                this->tc_cursor.x = sel_range->lr_start;
            } else if (sel_range->lr_start > 0 && curr_line == range.sr_end.y) {
                del_max = curr_line;
                this->tc_lines[curr_line - 1].append(this->tc_lines[curr_line]);
            }
        }
    }

    if (del_max) {
        log_debug("deleting lines [%d+%d:%d)",
                  range.sr_start.y,
                  (full_first_line ? 0 : 1),
                  del_max.value() + 1);
        this->tc_lines.erase(this->tc_lines.begin() + range.sr_start.y
                                 + (full_first_line ? 0 : 1),
                             this->tc_lines.begin() + del_max.value() + 1);
    }

    const auto repl_last_line
        = sf.find_left_boundary(sf.length(), string_fragment::tag1{'\n'});
    log_debug(
        "last line '%.*s'", repl_last_line.length(), repl_last_line.data());
    const auto repl_cols
        = sf.find_left_boundary(sf.length(), string_fragment::tag1{'\n'})
              .column_width();
    const auto repl_lines = sf.count('\n');
    log_debug("repl_cols => %d", repl_cols);
    if (repl_lines > 0) {
        this->tc_cursor.x = repl_cols;
    } else {
        this->tc_cursor.x += repl_cols;
    }
    this->tc_cursor.y += repl_lines;

    this->tc_drag_selection = std::nullopt;
    this->update_lines();
}

void
textinput_curses::update_lines()
{
    auto content = attr_line_t(this->get_content());

    highlight_syntax(this->tc_text_format, content);
    this->tc_doc_meta = lnav::document::discover(content)
                            .with_text_format(this->tc_text_format)
                            .save_words()
                            .perform();
    log_debug("doc indents:");
    for (auto ind : this->tc_doc_meta.m_indents) {
        log_debug("  indent=%d", ind);
    }
    this->tc_lines = content.split_lines();
    this->apply_highlights();
    this->ensure_cursor_visible();

    this->tc_popup.set_visible(false);
    this->tc_complete_range = std::nullopt;
    if (this->tc_on_change) {
        this->tc_on_change(*this);
    }
}

textinput_curses::dimension_result
textinput_curses::get_visible_dimensions() const
{
    dimension_result retval;

    ncplane_dim_yx(
        this->tc_window, &retval.dr_full_height, &retval.dr_full_width);

    if (this->vc_y < retval.dr_full_height) {
        retval.dr_height = std::min((int) retval.dr_full_height - this->vc_y,
                                    this->vc_y + this->tc_height);
    }
    if (this->vc_x < retval.dr_full_width) {
        retval.dr_width = std::min(retval.dr_full_width - this->vc_x,
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
                            this->vc_y + this->tc_cursor.y - this->tc_top,
                            this->vc_x + this->tc_cursor.x - this->tc_left);
}

void
textinput_curses::blur()
{
    notcurses_cursor_disable(ncplane_notcurses(this->tc_window));
}

bool
textinput_curses::do_update()
{
    static auto& vc = view_colors::singleton();
    auto retval = false;

    if (!this->vc_needs_update) {
        log_debug("skip update");
        return view_curses::do_update();
    }

    log_debug("render input");
    retval = true;
    auto dim = this->get_visible_dimensions();
    auto row_count = this->tc_lines.size();
    auto y = this->vc_y;
    auto y_max = this->vc_y + dim.dr_height;
    for (auto curr_line = this->tc_top; curr_line < row_count && y < y_max;
         curr_line++, y++)
    {
        ncplane_erase_region(this->tc_window, y, this->vc_x, 1, dim.dr_width);
        auto lr = line_range{this->tc_left, this->tc_left + dim.dr_width};
        auto al = this->tc_lines[curr_line];
        if (this->tc_drag_selection) {
            auto sel_lr = this->tc_drag_selection->range_for_line(curr_line);
            if (sel_lr) {
                al.al_attrs.emplace_back(
                    sel_lr.value(), VC_ROLE.value(role_t::VCR_SELECTED_TEXT));
            }
        } else if (this->tc_selection) {
            auto sel_lr = this->tc_selection->range_for_line(curr_line);
            if (sel_lr) {
                al.al_attrs.emplace_back(
                    sel_lr.value(), VC_STYLE.value(text_attrs::with_reverse()));
            } else {
                log_error("  no range");
            }
        }
        if (this->tc_mode == mode_t::searching
            && this->tc_search_found.value_or(false))
        {
            this->tc_search_code->capture_from(al.al_string)
                .for_each([&al](lnav::pcre2pp::match_data& md) {
                    al.al_attrs.emplace_back(
                        line_range{
                            md[0]->sf_begin,
                            md[0]->sf_end,
                        },
                        VC_ROLE.value(role_t::VCR_SEARCH));
                });
        }
        auto mvw_res = mvwattrline(this->tc_window, y, this->vc_x, al, lr);
    }
    for (; y < y_max; y++) {
        ncplane_erase_region(this->tc_window, y, this->vc_x, 1, dim.dr_width);
    }
    if (this->tc_mode == mode_t::searching) {
        auto search_prompt = attr_line_t("Search: ")
                                 .append(this->tc_search)
                                 .with_attr_for_all(VC_ROLE.value(
                                     (this->tc_search.empty()
                                      || this->tc_search_found.value_or(false))
                                         ? role_t::VCR_OK
                                         : role_t::VCR_ERROR));
        auto lr = line_range{0, dim.dr_width};
        mvwattrline(this->tc_window,
                    this->vc_y + dim.dr_height - 1,
                    this->vc_x,
                    search_prompt,
                    lr);
    }

    if (this->tc_height > 1) {
        double progress = 1.0;
        double coverage = 1.0;

        if (row_count > 0) {
            progress = (double) this->tc_top / (double) row_count;
            coverage = (double) dim.dr_height / (double) row_count;
        }

        auto scroll_top = (int) (progress * (double) dim.dr_height);
        auto scroll_bottom = scroll_top
            + std::min(dim.dr_height,
                       (int) (coverage * (double) dim.dr_height));

        for (auto y = this->vc_y; y < y_max; y++) {
            auto role = this->vc_default_role;
            auto bar_role = role_t::VCR_SCROLLBAR;
            auto ch = NCACS_VLINE;
            if (y >= this->vc_y + scroll_top && y <= this->vc_y + scroll_bottom)
            {
                role = bar_role;
            }
            auto attrs = vc.attrs_for_role(role);
            ncplane_putstr_yx(
                this->tc_window, y, this->vc_x + dim.dr_width - 1, ch);
            ncplane_set_cell_yx(this->tc_window,
                                y,
                                this->vc_x + dim.dr_width - 1,
                                attrs.ta_attrs | NCSTYLE_ALTCHARSET,
                                view_colors::to_channels(attrs));
        }
    }

    return view_curses::do_update() || retval;
}

void
textinput_curses::open_popup_for_completion(
    size_t left, std::vector<attr_line_t> possibilities)
{
    if (possibilities.empty()) {
        return;
    }

    auto dim = this->get_visible_dimensions();
    auto max_width = possibilities | lnav::itertools::map([](const auto& elem) {
                         return elem.column_width();
                     })
        | lnav::itertools::max();

    auto full_width = std::min((int) max_width.value_or(1) + 2, dim.dr_width);
    auto popup_height
        = vis_line_t(std::min(this->tc_max_popup_height, possibilities.size()));
    auto rel_x = left;
    if (rel_x + full_width > dim.dr_width) {
        rel_x = dim.dr_width - full_width;
    }
    auto rel_y = this->tc_cursor.y - this->tc_top + 1;
    if (this->vc_y + rel_y + popup_height > dim.dr_full_height) {
        rel_y = this->tc_cursor.y - this->tc_top - popup_height;
    }

    this->tc_complete_range = selected_range::from_key(
        this->tc_cursor.copy_with_x(left), this->tc_cursor);
    this->tc_popup_source.replace_with(possibilities);
    this->tc_popup.set_window(this->tc_window);
    this->tc_popup.set_x(this->vc_x + rel_x);
    this->tc_popup.set_y(this->vc_y + rel_y);
    this->tc_popup.set_width(full_width);
    this->tc_popup.set_height(popup_height);
    this->tc_popup.set_visible(true);
    this->tc_popup.set_selection(0_vl);
    this->set_needs_update();
}

void
textinput_curses::open_popup_for_history(std::vector<attr_line_t> possibilities)
{
    if (possibilities.empty()) {
        return;
    }
    this->tc_popup_source.replace_with(possibilities);
    this->tc_popup.set_window(this->tc_window);
    this->tc_popup.set_x(this->vc_x);
    this->tc_popup.set_y(this->vc_y + 1);
    this->tc_popup.set_width(this->vc_width);
    this->tc_popup.set_height(
        vis_line_t(std::min(this->tc_max_popup_height, possibilities.size())));
    this->tc_popup.set_visible(true);
    this->tc_popup.set_selection(0_vl);
    this->set_needs_update();
}
