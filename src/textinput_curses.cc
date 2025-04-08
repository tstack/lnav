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
#include "unictype.h"
#include "ww898/cp_utf8.hpp"

using namespace std::chrono_literals;
using namespace lnav::roles::literals;

const attr_line_t&
textinput_curses::get_help_text()
{
    static const auto retval
        = attr_line_t()
              .append("Prompt Help"_h1)
              .append("\n\n")
              .append("Editing"_h2)
              .append("\n ")
              .append("\u2022"_list_glyph)
              .append(" ")
              .append("ESC"_hotkey)
              .append("       - Cancel editing\n ")
              .append("\u2022"_list_glyph)
              .append(" ")
              .append("CTRL-X"_hotkey)
              .append("    - Save and exit the editor\n ")
              .append("\u2022"_list_glyph)
              .append(" ")
              .append("HOME"_hotkey)
              .append("      - Move to the beginning of the buffer\n ")
              .append("\u2022"_list_glyph)
              .append(" ")
              .append("END"_hotkey)
              .append("       - Move to the end of the buffer\n ")
              .append("\u2022"_list_glyph)
              .append(" ")
              .append("CTRL-A"_hotkey)
              .append("    - Move to the beginning of the line\n ")
              .append("\u2022"_list_glyph)
              .append(" ")
              .append("CTRL-E"_hotkey)
              .append("    - Move to the end of the line\n ")
              .append("\u2022"_list_glyph)
              .append(" ")
              .append("CTRL-N"_hotkey)
              .append(
                  "    - Move down one line.  If a popup is open, move the "
                  "selection down.\n ")
              .append("\u2022"_list_glyph)
              .append(" ")
              .append("CTRL-P"_hotkey)
              .append(
                  "    - Move up one line.  If a popup is open, move the "
                  "selection up.\n ")
              .append("\u2022"_list_glyph)
              .append(" ")
              .append("ALT  \u2190"_hotkey)
              .append("    - Move to the previous word\n ")
              .append("\u2022"_list_glyph)
              .append(" ")
              .append("ALT  \u2192"_hotkey)
              .append("    - Move to the end of the line\n ")
              .append("\u2022"_list_glyph)
              .append(" ")
              .append("CTRL-K"_hotkey)
              .append("    - Cut to the end of the line into the clipboard\n ")
              .append("\u2022"_list_glyph)
              .append(" ")
              .append("CTRL-U"_hotkey)
              .append(
                  "    - Cut from the beginning of the line to the cursor "
                  "into the clipboard\n ")
              .append("\u2022"_list_glyph)
              .append(" ")
              .append("CTRL-W"_hotkey)
              .append(
                  "    - Cut from the beginning of the previous word into "
                  "the clipboard\n ")
              .append("\u2022"_list_glyph)
              .append(" ")
              .append("Rt-click"_hotkey)
              .append("  - Copy selection to the system clipboard\n ")
              .append("\u2022"_list_glyph)
              .append(" ")
              .append("CTRL-Y"_hotkey)
              .append("    - Paste the clipboard content\n ")
              .append("\u2022"_list_glyph)
              .append(" ")
              .append("TAB/ENTER"_hotkey)
              .append(" - Accept a completion suggestion\n ")
              .append("\u2022"_list_glyph)
              .append(" ")
              .append("CTRL-_"_hotkey)
              .append("    - Undo a change\n ")
              .append("\u2022"_list_glyph)
              .append(" ")
              .append("CTRL-L"_hotkey)
              .append("    - Reformat the contents, if available\n ")
              .append("\u2022"_list_glyph)
              .append(" ")
              .append("CTRL-O"_hotkey)
              .append("    - Open the contents in an external editor\n")
              .append("\n")
              .append("History"_h2)
              .append("\n ")
              .append("\u2022"_list_glyph)
              .append(" ")
              .append("\u2191"_hotkey)
              .append("      - Select content from the history\n ")
              .append("\u2022"_list_glyph)
              .append(" ")
              .append("CTRL-R"_hotkey)
              .append(" - Search history using current contents\n")
              .append("\n")
              .append("Searching"_h2)
              .append("\n ")
              .append("\u2022"_list_glyph)
              .append(" ")
              .append("CTRL-S"_hotkey)
              .append(" - Switch to search mode\n ")
              .append("\u2022"_list_glyph)
              .append(" ")
              .append("CTRL-R"_hotkey)
              .append(" - Search backwards for the string\n");

    return retval;
}

const std::vector<attr_line_t>&
textinput_curses::unhandled_input()
{
    static const auto retval = std::vector{
        attr_line_t()
            .append(" Notice: "_status_subtitle)
            .append(" Unhandled key press.  Press F1 for help")
            .with_attr_for_all(VC_ROLE.value(role_t::VCR_ALERT_STATUS)),
    };

    return retval;
}

const std::vector<attr_line_t>&
textinput_curses::no_changes()
{
    static const auto retval = std::vector{
        attr_line_t()
            .append(" Notice: "_status_subtitle)
            .append(" No changes to undo")
            .with_attr_for_all(VC_ROLE.value(role_t::VCR_STATUS)),
    };

    return retval;
}

const std::vector<attr_line_t>&
textinput_curses::external_edit_failed()
{
    static const auto retval = std::vector{
        attr_line_t()
            .append(" Error: "_status_subtitle)
            .append(" Unable to write file for external edit")
            .with_attr_for_all(VC_ROLE.value(role_t::VCR_ALERT_STATUS)),
    };

    return retval;
}

class textinput_mouse_delegate : public text_delegate {
public:
    textinput_mouse_delegate(textinput_curses* input) : tmd_input(input) {}

    bool text_handle_mouse(textview_curses& tc,
                           const listview_curses::display_line_content_t& dlc,
                           mouse_event& me) override
    {
        if (me.me_button == mouse_button_t::BUTTON_LEFT
            && me.me_state == mouse_button_state_t::BUTTON_STATE_RELEASED
            && dlc.is<listview_curses::main_content>())
        {
            ncinput ch{};

            ch.id = NCKEY_TAB;
            ch.eff_text[0] = '\t';
            ch.eff_text[1] = '\0';
            return this->tmd_input->handle_key(ch);
        }

        return false;
    }

    textinput_curses* tmd_input;
};

textinput_curses::textinput_curses()
{
    this->vc_enabled = false;
    this->vc_children.emplace_back(&this->tc_popup);

    this->tc_popup.tc_cursor_role = role_t::VCR_CURSOR_LINE;
    this->tc_popup.tc_disabled_cursor_role = role_t::VCR_DISABLED_CURSOR_LINE;
    this->tc_popup.lv_border_left_role = role_t::VCR_POPUP_BORDER;
    this->tc_popup.set_visible(false);
    this->tc_popup.set_title("textinput popup");
    this->tc_popup.set_head_space(0_vl);
    this->tc_popup.set_selectable(true);
    this->tc_popup.set_show_scrollbar(true);
    this->tc_popup.set_default_role(role_t::VCR_POPUP);
    this->tc_popup.set_sub_source(&this->tc_popup_source);
    this->tc_popup.set_delegate(
        std::make_shared<textinput_mouse_delegate>(this));

    this->vc_children.emplace_back(&this->tc_help_view);
    this->tc_help_view.set_visible(false);
    this->tc_help_view.set_title("textinput help");
    this->tc_help_view.set_show_scrollbar(true);
    this->tc_help_view.set_default_role(role_t::VCR_STATUS);
    this->tc_help_view.set_sub_source(&this->tc_help_source);

    this->tc_on_help = [](textinput_curses& ti) {
        ti.tc_mode = mode_t::show_help;
        ti.set_needs_update();
    };
    this->tc_help_source.replace_with(get_help_text());

    this->set_content("");
}

void
textinput_curses::content_to_lines(std::string content, int x)
{
    auto al = attr_line_t(content);

    if (!this->tc_prefix.empty()) {
        al.insert(0, this->tc_prefix);
        x += this->tc_prefix.length();
    }
    highlight_syntax(this->tc_text_format, al, x);
    if (!this->tc_prefix.empty()) {
        // XXX yuck
        al.erase(0, this->tc_prefix.al_string.size());
    }
    this->tc_doc_meta = lnav::document::discover(al)
                            .with_text_format(this->tc_text_format)
                            .save_words()
                            .perform();
    this->tc_lines = al.split_lines();
    if (endswith(al.al_string, "\n")) {
        this->tc_lines.emplace_back();
    }
    if (this->tc_lines.empty()) {
        this->tc_lines.emplace_back();
    } else {
        this->apply_highlights();
    }
}

void
textinput_curses::set_content(std::string content)
{
    this->content_to_lines(std::move(content), this->tc_prefix.length());

    this->tc_change_log.clear();
    this->tc_marks.clear();
    this->tc_notice = std::nullopt;
    this->tc_left = 0;
    this->tc_top = 0;
    this->tc_cursor = {};
    this->clamp_point(this->tc_cursor);
    this->set_needs_update();
}

void
textinput_curses::set_height(int height)
{
    if (this->tc_height == height) {
        return;
    }

    this->tc_height = height;
    if (this->tc_height == 1) {
        if (this->tc_cursor.y != 0) {
            this->move_cursor_to(this->tc_cursor.copy_with_y(0));
        }
    }
    this->set_needs_update();
}

std::optional<view_curses*>
textinput_curses::contains(int x, int y)
{
    if (!this->vc_visible) {
        return std::nullopt;
    }

    auto child = view_curses::contains(x, y);
    if (child) {
        return child;
    }

    if (this->vc_x <= x && x < this->vc_x + this->vc_width && this->vc_y <= y
        && y < this->vc_y + this->tc_height)
    {
        return this;
    }
    return std::nullopt;
}

bool
textinput_curses::handle_mouse(mouse_event& me)
{
    ssize_t inner_height = this->tc_lines.size();

    log_debug("mouse here! button=%d state=%d x=%d y=%d",
              me.me_button,
              me.me_state,
              me.me_x,
              me.me_y);
    this->tc_notice = std::nullopt;
    this->tc_last_tick_after_input = std::nullopt;
    if (this->tc_mode == mode_t::show_help) {
        return this->tc_help_view.handle_mouse(me);
    }
    if (me.me_button == mouse_button_t::BUTTON_SCROLL_UP) {
        auto dim = this->get_visible_dimensions();
        if (this->tc_top > 0) {
            this->tc_top -= 1;
            if (this->tc_top + dim.dr_height - 2 < this->tc_cursor.y) {
                this->move_cursor_by({direction_t::up, 1});
            } else {
                this->ensure_cursor_visible();
            }
            this->set_needs_update();
        }
    } else if (me.me_button == mouse_button_t::BUTTON_SCROLL_DOWN) {
        auto dim = this->get_visible_dimensions();
        if (this->tc_top + dim.dr_height < inner_height) {
            this->tc_top += 1;
            if (this->tc_cursor.y <= this->tc_top) {
                this->move_cursor_by({direction_t::down, 1});
            } else {
                this->ensure_cursor_visible();
            }
            this->set_needs_update();
        }
    } else if (me.me_button == mouse_button_t::BUTTON_RIGHT) {
        if (this->tc_selection) {
            std::string content;
            auto range = this->tc_selection;
            auto add_nl = false;
            for (auto y = range->sr_start.y;
                 y <= range->sr_end.y && y < this->tc_lines.size();
                 ++y)
            {
                if (add_nl) {
                    content.push_back('\n');
                }
                auto sel_range = range->range_for_line(y);
                if (!sel_range) {
                    continue;
                }

                const auto& al = this->tc_lines[y];
                auto byte_start = al.column_to_byte_index(sel_range->lr_start);
                auto byte_end = al.column_to_byte_index(sel_range->lr_end);
                auto al_sf = string_fragment::from_str_range(
                    al.al_string, byte_start, byte_end);
                content.append(al_sf.data(), al_sf.length());
                add_nl = true;
            }

            this->tc_clipboard.clear();
            this->tc_cut_location = this->tc_cursor;
            this->tc_clipboard.emplace_back(content);
            this->sync_to_sysclip();
        }
    } else if (me.me_button == mouse_button_t::BUTTON_LEFT) {
        this->tc_mode = mode_t::editing;
        auto adj_press_x = me.me_press_x;
        if (me.me_press_y == 0 && me.me_press_x > 0) {
            adj_press_x -= this->tc_prefix.column_width();
        }
        auto adj_x = me.me_x;
        if (me.me_y == 0 && me.me_x > 0) {
            adj_x -= this->tc_prefix.column_width();
        }
        auto inner_press_point = input_point{
            this->tc_left + adj_press_x,
            (int) this->tc_top + me.me_press_y,
        };
        this->clamp_point(inner_press_point);
        auto inner_point = input_point{
            this->tc_left + adj_x,
            (int) this->tc_top + me.me_y,
        };
        this->clamp_point(inner_point);

        this->tc_popup_type = popup_type_t::none;
        this->tc_popup.set_visible(false);
        this->tc_complete_range = std::nullopt;
        this->tc_cursor = inner_point;
        log_debug("new cursor x=%d y=%d", this->tc_cursor.x, this->tc_cursor.y);
        if (me.me_state == mouse_button_state_t::BUTTON_STATE_DOUBLE_CLICK) {
            const auto& al = this->tc_lines[this->tc_cursor.y];
            auto sf = string_fragment::from_str(al.al_string);
            auto cursor_sf = sf.sub_cell_range(this->tc_left + adj_x,
                                               this->tc_left + adj_x);
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
            this->tc_cursor_anchor = inner_press_point;
            this->tc_drag_selection = selected_range::from_mouse(
                this->tc_cursor_anchor, inner_point);
        } else if (me.me_state == mouse_button_state_t::BUTTON_STATE_DRAGGED) {
            this->tc_drag_selection = selected_range::from_mouse(
                this->tc_cursor_anchor, inner_point);
        } else if (me.me_state == mouse_button_state_t::BUTTON_STATE_RELEASED) {
            this->tc_drag_selection = std::nullopt;
            if (inner_press_point == inner_point) {
                this->tc_selection = std::nullopt;
            } else {
                this->tc_selection = selected_range::from_mouse(
                    this->tc_cursor_anchor, inner_point);
            }
        }
        this->ensure_cursor_visible();
    }

    return true;
}

bool
textinput_curses::handle_help_key(const ncinput& ch)
{
    switch (ch.id) {
        case ' ':
        case 'b':
        case 'j':
        case 'k':
        case 'g':
        case 'G':
        case NCKEY_HOME:
        case NCKEY_END:
        case NCKEY_UP:
        case NCKEY_DOWN:
        case NCKEY_PGUP:
        case NCKEY_PGDOWN: {
            log_debug("passing key press to help view");
            return this->tc_help_view.handle_key(ch);
        }
        default: {
            log_debug("switching back to editing from help");
            this->tc_mode = mode_t::editing;
            this->tc_help_view.set_visible(false);
            if (this->tc_on_change) {
                this->tc_on_change(*this);
            }
            this->set_needs_update();
            return true;
        }
    }
}

bool
textinput_curses::handle_search_key(const ncinput& ch)
{
    if (ncinput_ctrl_p(&ch)) {
        switch (ch.id) {
            case 'a':
            case 'A':
            case 'e':
            case 'E': {
                this->tc_mode = mode_t::editing;
                return this->handle_key(ch);
            }
            case 's':
            case 'S': {
                if (!this->tc_search.empty()) {
                    this->tc_search_start_point = this->tc_cursor;
                    this->move_cursor_to_next_search_hit();
                }
                return true;
            }
            case 'r':
            case 'R': {
                if (!this->tc_search.empty()) {
                    this->tc_search_start_point = this->tc_cursor;
                    this->move_cursor_to_prev_search_hit();
                }
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
                if (this->tc_search_found.has_value()) {
                    this->tc_search.pop_back();
                    auto compile_res = lnav::pcre2pp::code::from(
                        lnav::pcre2pp::quote(this->tc_search), PCRE2_CASELESS);
                    this->tc_search_code = compile_res.unwrap().to_shared();
                } else {
                    this->tc_search.clear();
                    this->tc_search_code.reset();
                }
                this->move_cursor_to_next_search_hit();
            }
            return true;
        }
        case NCKEY_ENTER: {
            this->tc_search_start_point = this->tc_cursor;
            this->move_cursor_to_next_search_hit();
            return true;
        }
        case NCKEY_LEFT:
        case NCKEY_RIGHT:
        case NCKEY_UP:
        case NCKEY_DOWN: {
            this->tc_mode = mode_t::editing;
            this->handle_key(ch);
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
            if (index > 0) {
                utf8[index] = 0;

                if (!this->tc_search_found.has_value()) {
                    this->tc_search.clear();
                }
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
    if (this->tc_search_code == nullptr) {
        return;
    }

    auto x = this->tc_search_start_point.x;
    if (this->tc_search_found && !this->tc_search_found.value()) {
        this->tc_search_start_point.y = 0;
    }
    this->tc_search_found = false;
    for (auto y = this->tc_search_start_point.y;
         y < (ssize_t) this->tc_lines.size();
         y++)
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

void
textinput_curses::command_indent(indent_mode_t mode)
{
    log_debug("indenting line: %d", this->tc_cursor.y);

    if (this->tc_cursor.y == 0 && !this->tc_prefix.empty()) {
        return;
    }

    int indent_amount;
    switch (mode) {
        case indent_mode_t::left:
        case indent_mode_t::clear_left:
            indent_amount = 0;
            break;
        case indent_mode_t::right:
            indent_amount = 4;
            break;
    }
    auto& al = this->tc_lines[this->tc_cursor.y];
    auto line_sf = al.to_string_fragment();
    const auto [before, after]
        = line_sf.split_when([](auto ch) { return !isspace(ch); });
    auto indent_iter = std::lower_bound(this->tc_doc_meta.m_indents.begin(),
                                        this->tc_doc_meta.m_indents.end(),
                                        before.length());
    if (indent_iter != this->tc_doc_meta.m_indents.end()) {
        if (mode == indent_mode_t::left || mode == indent_mode_t::clear_left) {
            if (indent_iter == this->tc_doc_meta.m_indents.begin()) {
                indent_amount = 0;
            } else {
                indent_amount = *std::prev(indent_iter);
            }
        } else if (before.empty()) {
            indent_amount = *indent_iter;
        } else {
            auto next_indent_iter = std::next(indent_iter);
            if (next_indent_iter == this->tc_doc_meta.m_indents.end()) {
                indent_amount += *indent_iter;
            } else {
                indent_amount = *next_indent_iter;
            }
        }
    }
    auto sel_len = (before.empty() && mode == indent_mode_t::clear_left)
        ? line_sf.column_width()
        : before.length();
    this->tc_selection = selected_range::from_key(
        this->tc_cursor.copy_with_x(0), this->tc_cursor.copy_with_x(sel_len));
    auto indent = std::string(indent_amount, ' ');
    auto old_cursor = this->tc_cursor;
    this->replace_selection(indent);
    this->tc_cursor.x = indent.length() - sel_len + old_cursor.x;
}

void
textinput_curses::command_down(const ncinput& ch)
{
    if (this->tc_popup.is_visible()) {
        this->tc_popup.handle_key(ch);
        if (this->tc_on_popup_change) {
            this->tc_in_popup_change = true;
            this->tc_on_popup_change(*this);
            this->tc_in_popup_change = false;
        }
    } else {
        ssize_t inner_height = this->tc_lines.size();
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
}

void
textinput_curses::command_up(const ncinput& ch)
{
    if (this->tc_popup.is_visible()) {
        this->tc_popup.handle_key(ch);
        if (this->tc_on_popup_change) {
            this->tc_in_popup_change = true;
            this->tc_on_popup_change(*this);
            this->tc_in_popup_change = false;
        }
    } else if (this->tc_height == 1) {
        if (this->tc_on_history_list) {
            this->tc_on_history_list(*this);
        }
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
}

bool
textinput_curses::handle_key(const ncinput& ch)
{
    static const auto PREFIX_RE = lnav::pcre2pp::code::from_const(
        R"(^\s*((?:-|\*|1\.|>)(?:\s+\[( |x|X)\])?\s*))");
    static const auto PREFIX_OR_WS_RE = lnav::pcre2pp::code::from_const(
        R"(^\s*(>\s*|(?:-|\*|1\.)?(?:\s+\[( |x|X)\])?\s+))");
    thread_local auto md = lnav::pcre2pp::match_data::unitialized();

    if (this->tc_notice) {
        this->tc_notice = std::nullopt;
        switch (ch.id) {
            case NCKEY_F01:
            case NCKEY_UP:
            case NCKEY_DOWN:
            case NCKEY_LEFT:
            case NCKEY_RIGHT:
                break;
            default:
                return true;
        }
    }
    this->tc_last_tick_after_input = std::nullopt;
    switch (this->tc_mode) {
        case mode_t::searching:
            return this->handle_search_key(ch);
        case mode_t::show_help:
            return this->handle_help_key(ch);
        case mode_t::editing:
            break;
    }

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
        log_debug("applying bracketed paste of size %zu", text.length());
        this->replace_selection(text);
        return true;
    }

    if (ncinput_alt_p(&ch)) {
        switch (chid) {
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
        if (ncinput_shift_p(&ch) && chid == '-') {
            chid = '_';  // XXX
        }
        switch (chid) {
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
                    log_debug("cutting selection [%d:%d) - [%d:%d)",
                              range->sr_start.x,
                              range->sr_start.y,
                              range->sr_end.x,
                              range->sr_end.y);
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
                    log_debug("cutting from %d to end of line %d",
                              this->tc_cursor.x,
                              this->tc_cursor.y);
                    if (this->tc_cursor != this->tc_cut_location) {
                        log_debug("  cursor moved, clearing clipboard");
                        this->tc_clipboard.clear();
                    }
                    auto& al = this->tc_lines[this->tc_cursor.y];
                    auto byte_index
                        = al.column_to_byte_index(this->tc_cursor.x);
                    this->tc_clipboard.emplace_back(
                        al.subline(byte_index).al_string);
                    this->tc_selection = selected_range::from_key(
                        this->tc_cursor,
                        this->tc_cursor.copy_with_x(al.column_width()));
                    if (this->tc_selection->empty()
                        && this->tc_cursor.y + 1
                            < (ssize_t) this->tc_lines.size())
                    {
                        this->tc_clipboard.back().push_back('\n');
                        this->tc_selection = selected_range::from_key(
                            this->tc_cursor,
                            input_point{0, this->tc_cursor.y + 1});
                    }
                    this->replace_selection(string_fragment{});
                    this->tc_cut_location = this->tc_cursor;
                }
                this->sync_to_sysclip();
                this->tc_drag_selection = std::nullopt;
                this->update_lines();
                return true;
            }
            case 'l':
            case 'L': {
                log_debug("reformat content");
                if (this->tc_on_reformat) {
                    this->tc_on_reformat(*this);
                }
                return true;
            }
            case 'n':
            case 'N': {
                chid = NCKEY_DOWN;
                break;
            }
            case 'o':
            case 'O': {
                log_debug("opening in external editor");
                if (this->tc_on_external_open) {
                    this->tc_on_external_open(*this);
                }
                return true;
            }
            case 'p':
            case 'P': {
                chid = NCKEY_UP;
                break;
            }
            case 'r':
            case 'R': {
                if (this->tc_on_history_search) {
                    this->tc_on_history_search(*this);
                }
                return true;
            }
            case 's':
            case 'S': {
                if (this->tc_height > 1) {
                    log_debug("switching to search mode from edit");
                    this->tc_mode = mode_t::searching;
                    this->tc_search_start_point = this->tc_cursor;
                    this->tc_search_found = std::nullopt;
                    this->set_needs_update();
                }
                return true;
            }
            case 'u':
            case 'U': {
                log_debug("cutting to beginning of line");
                auto& al = this->tc_lines[this->tc_cursor.y];
                auto byte_index = al.column_to_byte_index(this->tc_cursor.x);
                if (this->tc_cursor != this->tc_cut_location) {
                    log_debug("  cursor moved, clearing clipboard");
                    this->tc_clipboard.clear();
                }
                this->tc_clipboard.emplace_back(
                    al.subline(0, byte_index).al_string);
                this->sync_to_sysclip();
                this->tc_selection = selected_range::from_key(
                    this->tc_cursor.copy_with_x(0), this->tc_cursor);
                this->replace_selection(string_fragment{});
                this->tc_cut_location = this->tc_cursor;
                this->tc_selection = std::nullopt;
                this->tc_drag_selection = std::nullopt;
                this->update_lines();
                return true;
            }
            case 'w':
            case 'W': {
                log_debug("cutting to beginning of previous word");
                auto al_sf
                    = this->tc_lines[this->tc_cursor.y].to_string_fragment();
                auto prev_word_start_opt = al_sf.prev_word(this->tc_cursor.x);
                if (!prev_word_start_opt && this->tc_cursor.x > 0) {
                    prev_word_start_opt = 0;
                }
                if (prev_word_start_opt) {
                    if (this->tc_cut_location != this->tc_cursor) {
                        log_debug(
                            "  cursor moved since last cut, clearing "
                            "clipboard");
                        this->tc_clipboard.clear();
                    }
                    auto prev_word = al_sf.sub_cell_range(
                        prev_word_start_opt.value(), this->tc_cursor.x);
                    this->tc_clipboard.emplace_front(prev_word.to_string());
                    this->sync_to_sysclip();
                    this->tc_selection = selected_range::from_key(
                        this->tc_cursor.copy_with_x(
                            prev_word_start_opt.value()),
                        this->tc_cursor);
                    this->replace_selection(string_fragment{});
                    this->tc_cut_location = this->tc_cursor;
                }
                return true;
            }
            case 'x':
            case 'X': {
                log_debug("performing action");
                this->blur();
                if (this->tc_on_perform) {
                    this->tc_on_perform(*this);
                }
                return true;
            }
            case 'y':
            case 'Y': {
                log_debug("pasting clipboard contents");
                for (const auto& clipping : this->tc_clipboard) {
                    auto& al = this->tc_lines[this->tc_cursor.y];
                    al.insert(al.column_to_byte_index(this->tc_cursor.x),
                              clipping);
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
            case ']': {
                if (this->tc_popup.is_visible()) {
                    this->tc_popup_type = popup_type_t::none;
                    this->tc_popup.set_visible(false);
                    this->tc_complete_range = std::nullopt;
                    this->set_needs_update();
                } else {
                    this->abort();
                }

                this->tc_selection = std::nullopt;
                this->tc_drag_selection = std::nullopt;
                return true;
            }
            case '_': {
                if (this->tc_change_log.empty()) {
                    this->tc_notice = no_changes();
                    this->set_needs_update();
                } else {
                    log_debug("undo!");
                    const auto& ce = this->tc_change_log.back();
                    auto content_sf = string_fragment::from_str(ce.ce_content);
                    this->tc_selection = ce.ce_range;
                    log_debug(" range [%d:%d) - [%d:%d) - %s",
                              this->tc_selection->sr_start.x,
                              this->tc_selection->sr_start.y,
                              this->tc_selection->sr_end.x,
                              this->tc_selection->sr_end.y,
                              ce.ce_content.c_str());
                    this->replace_selection_no_change(content_sf);
                    this->tc_change_log.pop_back();
                }
                return true;
            }
            default: {
                this->tc_notice = unhandled_input();
                this->set_needs_update();
                return false;
            }
        }
    }

    switch (chid) {
        case NCKEY_ESC:
        case KEY_CTRL(']'): {
            if (this->tc_popup.is_visible()) {
                if (this->tc_on_popup_cancel) {
                    this->tc_on_popup_cancel(*this);
                }
                this->tc_popup_type = popup_type_t::none;
                this->tc_popup.set_visible(false);
                this->tc_complete_range = std::nullopt;
                this->set_needs_update();
            } else {
                this->abort();
            }

            this->tc_selection = std::nullopt;
            this->tc_drag_selection = std::nullopt;
            return true;
        }
        case NCKEY_ENTER: {
            if (this->tc_popup.is_visible()) {
                this->tc_popup.set_visible(false);
                if (this->tc_on_completion) {
                    this->tc_on_completion(*this);
                }
                this->tc_popup_type = popup_type_t::none;
                this->set_needs_update();
            } else if (this->tc_height == 1) {
                this->blur();
                if (this->tc_on_perform) {
                    this->tc_on_perform(*this);
                }
            } else {
                const auto& al = this->tc_lines[this->tc_cursor.y];
                auto al_sf = al.to_string_fragment();
                auto prefix_sf = al_sf.rtrim(" ");
                auto indent = std::string("\n");
                if (!this->tc_selection) {
                    log_debug("checking for prefix");
                    auto match_opt = PREFIX_OR_WS_RE.capture_from(al_sf)
                                         .into(md)
                                         .matches()
                                         .ignore_error();
                    if (match_opt) {
                        log_debug("has prefix");
                        this->tc_selection = selected_range::from_key(
                            this->tc_cursor.copy_with_x(
                                prefix_sf.column_width()),
                            this->tc_cursor.copy_with_x(al_sf.column_width()));
                        auto is_comment
                            = al.al_attrs
                            | lnav::itertools::find_if(
                                  [](const string_attr& sa) {
                                      return (sa.sa_type == &VC_ROLE)
                                          && sa.sa_value.get<role_t>()
                                          == role_t::VCR_COMMENT;
                                  });
                        if (!is_comment && !al.empty()
                            && !md[1]->startswith(">")
                            && match_opt->f_all.length() == al.length())
                        {
                            log_debug("clear left");
                            this->command_indent(indent_mode_t::clear_left);
                        } else if (this->is_cursor_at_end_of_line()) {
                            indent.append(match_opt->f_all.data(),
                                          match_opt->f_all.length());
                            if (md[2] && md[2]->front() != ' ') {
                                indent[1 + md[2]->sf_begin] = ' ';
                            }
                        } else {
                            indent.append(match_opt->f_all.length(), ' ');
                            this->tc_selection
                                = selected_range::from_point(this->tc_cursor);
                        }
                    } else {
                        this->tc_selection
                            = selected_range::from_point(this->tc_cursor);
                        log_debug("no prefix, replace point: [%d:%d]",
                                  this->tc_selection->sr_start.x,
                                  this->tc_selection->sr_start.y);
                    }
                }
                this->replace_selection(indent);
            }
            // TODO implement "double enter" to call tc_on_perform
            return true;
        }
        case NCKEY_TAB: {
            if (this->tc_popup.is_visible()) {
                log_debug("performing completion");
                this->tc_popup_type = popup_type_t::none;
                this->tc_popup.set_visible(false);
                if (this->tc_on_completion) {
                    this->tc_on_completion(*this);
                }
                this->set_needs_update();
            } else if (!this->tc_suggestion.empty()
                       && this->is_cursor_at_end_of_line())
            {
                log_debug("inserting suggestion");
                this->tc_selection = selected_range::from_key(this->tc_cursor,
                                                              this->tc_cursor);
                this->replace_selection(this->tc_suggestion);
            } else if (this->tc_height == 1) {
                log_debug("requesting completion at %d", this->tc_cursor.x);
                if (this->tc_on_completion_request) {
                    this->tc_on_completion_request(*this);
                }
            } else if (!this->tc_selection) {
                if (!ncinput_shift_p(&ch)
                    && (this->tc_cursor.x > 0
                        && this->tc_lines[this->tc_cursor.y].al_string.back()
                            != ' '))
                {
                    log_debug("requesting completion at %d", this->tc_cursor.x);
                    if (this->tc_on_completion_request) {
                        this->tc_on_completion_request(*this);
                    }
                    if (!this->tc_popup.is_visible()) {
                        this->command_indent(indent_mode_t::right);
                    }
                    return true;
                }

                this->command_indent(ncinput_shift_p(&ch)
                                         ? indent_mode_t::left
                                         : indent_mode_t::right);
            }
            return true;
        }
        case NCKEY_HOME: {
            this->move_cursor_to(input_point::home());
            return true;
        }
        case NCKEY_END: {
            this->move_cursor_to(input_point::end());
            return true;
        }
        case NCKEY_PGUP: {
            if (this->tc_cursor.y > 0) {
                this->move_cursor_by({direction_t::up, (size_t) dim.dr_height});
            }
            return true;
        }
        case NCKEY_PGDOWN: {
            if (this->tc_cursor.y < (ssize_t) bottom) {
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
            if (this->tc_lines.size() == 1 && this->tc_lines.front().empty()) {
                this->abort();
            } else if (!this->tc_selection) {
                const auto& al = this->tc_lines[this->tc_cursor.y];
                auto line_sf = al.to_string_fragment();
                const auto [before, after]
                    = line_sf
                          .split_n(
                              line_sf.column_to_byte_index(this->tc_cursor.x))
                          .value();
                auto match_opt = PREFIX_RE.capture_from(before)
                                     .into(md)
                                     .matches()
                                     .ignore_error();

                if (match_opt && !match_opt->f_all.empty()
                    && match_opt->f_all.sf_end == this->tc_cursor.x)
                {
                    auto is_comment = al.al_attrs
                        | lnav::itertools::find_if([](const string_attr& sa) {
                                          return (sa.sa_type == &VC_ROLE)
                                              && sa.sa_value.get<role_t>()
                                              == role_t::VCR_COMMENT;
                                      });
                    if (!is_comment && md[1]) {
                        this->tc_selection = selected_range::from_key(
                            this->tc_cursor.copy_with_x(md[1]->sf_begin),
                            this->tc_cursor);
                        auto indent = std::string(
                            md[1]->startswith(">") ? 0 : md[1]->length(), ' ');

                        this->replace_selection(indent);
                        return true;
                    }
                } else {
                    auto indent_iter
                        = std::lower_bound(this->tc_doc_meta.m_indents.begin(),
                                           this->tc_doc_meta.m_indents.end(),
                                           this->tc_cursor.x);
                    if (indent_iter != this->tc_doc_meta.m_indents.end()) {
                        if (indent_iter != this->tc_doc_meta.m_indents.begin())
                        {
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
            this->command_up(ch);
            return true;
        }
        case NCKEY_DOWN: {
            this->command_down(ch);
            return true;
        }
        case NCKEY_LEFT: {
            if (ncinput_shift_p(&ch)) {
                if (!this->tc_selection) {
                    this->tc_cursor_anchor = this->tc_cursor;
                }
                this->move_cursor_by({direction_t::left, 1});
                this->tc_selection = selected_range::from_key(
                    this->tc_cursor_anchor, this->tc_cursor);
            } else if (this->tc_selection) {
                this->tc_cursor = this->tc_selection->sr_start;
                this->tc_selection = std::nullopt;
                this->set_needs_update();
            } else {
                this->move_cursor_by({direction_t::left, 1});
            }
            return true;
        }
        case NCKEY_RIGHT: {
            if (ncinput_shift_p(&ch)) {
                if (!this->tc_selection) {
                    this->tc_cursor_anchor = this->tc_cursor;
                }
                this->move_cursor_by({direction_t::right, 1});
                this->tc_selection = selected_range::from_key(
                    this->tc_cursor_anchor, this->tc_cursor);
            } else if (this->tc_selection) {
                this->tc_cursor = this->tc_selection->sr_end;
                this->tc_selection = std::nullopt;
                this->set_needs_update();
            } else {
                this->move_cursor_by({direction_t::right, 1});
            }
            return true;
        }
        case NCKEY_F01: {
            if (this->tc_on_help) {
                this->tc_on_help(*this);
            }
            return true;
        }
        case ' ': {
            if (!this->tc_selection) {
                const auto& al = this->tc_lines[this->tc_cursor.y];
                const auto sf = al.to_string_fragment();
                if (PREFIX_RE.capture_from(sf).into(md).found_p() && md[2]
                    && this->tc_cursor.x == md[2]->sf_begin)
                {
                    this->tc_selection = selected_range::from_key(
                        this->tc_cursor,
                        this->tc_cursor.copy_with_x(this->tc_cursor.x + 1));

                    auto repl = (md[2]->front() == ' ') ? "X"_frag : " "_frag;
                    this->replace_selection(repl);
                    return true;
                }

                this->tc_selection
                    = selected_range::from_point(this->tc_cursor);
            }
            this->replace_selection(" "_frag);
            return true;
        }
        default: {
            if (NCKEY_F00 <= ch.id && ch.id <= NCKEY_F60) {
                this->tc_notice = unhandled_input();
                this->set_needs_update();
            } else {
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
                } else {
                    this->tc_notice = unhandled_input();
                    this->set_needs_update();
                }
            }
            return true;
        }
    }

    return false;
}

void
textinput_curses::ensure_cursor_visible()
{
    if (!this->vc_enabled) {
        return;
    }

    auto dim = this->get_visible_dimensions();
    auto orig_top = this->tc_top;
    auto orig_left = this->tc_left;
    auto orig_cursor = this->tc_cursor;
    auto orig_max_cursor_x = this->tc_max_cursor_x;

    this->clamp_point(this->tc_cursor);
    if (this->tc_cursor.y < 0) {
        this->tc_cursor.y = 0;
    }
    if (this->tc_cursor.y >= (ssize_t) this->tc_lines.size()) {
        this->tc_cursor.y = this->tc_lines.size() - 1;
    }
    if (this->tc_cursor.x < 0) {
        this->tc_cursor.x = 0;
    }
    if (this->tc_cursor.x
        >= (ssize_t) this->tc_lines[this->tc_cursor.y].column_width())
    {
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
    if (this->tc_top < 0) {
        this->tc_top = 0;
    }
    if (this->tc_top >= this->tc_cursor.y) {
        this->tc_top = this->tc_cursor.y;
        if (this->tc_top > 0) {
            this->tc_top -= 1;
        }
    }
    if (this->tc_height > 1
        && this->tc_cursor.y + 1 >= this->tc_top + dim.dr_height)
    {
        this->tc_top = (this->tc_cursor.y + 1 - dim.dr_height) + 1;
    }
    if (this->tc_top + dim.dr_height > (ssize_t) this->tc_lines.size()) {
        if ((ssize_t) this->tc_lines.size() > dim.dr_height) {
            this->tc_top = this->tc_lines.size() - dim.dr_height + 1;
        } else {
            this->tc_top = 0;
        }
    }
    if (!this->tc_in_popup_change && this->tc_popup.is_visible()
        && this->tc_complete_range
        && !this->tc_complete_range->contains(this->tc_cursor))
    {
        this->tc_popup.set_visible(false);
        this->tc_complete_range = std::nullopt;
    }

    if (this->tc_cursor.x
        == (ssize_t) this->tc_lines[this->tc_cursor.y].column_width())
    {
        if (this->tc_cursor.x >= this->tc_max_cursor_x) {
            this->tc_max_cursor_x = this->tc_cursor.x;
        }
    } else {
        this->tc_max_cursor_x = this->tc_cursor.x;
    }

    if (orig_top != this->tc_top || orig_left != this->tc_left
        || orig_cursor != this->tc_cursor
        || orig_max_cursor_x != this->tc_max_cursor_x)
    {
        this->set_needs_update();
    }
}

void
textinput_curses::apply_highlights()
{
    if (this->tc_text_format == text_format_t::TF_LNAV_SCRIPT) {
        return;
    }

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

std::string
textinput_curses::replace_selection_no_change(string_fragment sf)
{
    if (!this->tc_selection) {
        return "";
    }

    std::optional<int> del_max;
    auto full_first_line = false;
    std::string retval;

    auto range = std::exchange(this->tc_selection, std::nullopt).value();
    this->tc_cursor.y = range.sr_start.y;
    for (auto curr_line = range.sr_start.y;
         curr_line <= range.sr_end.y && curr_line < this->tc_lines.size();
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
                retval.push_back('\n');
                del_max = curr_line;
                full_first_line = true;
            }
        } else if (sel_range->lr_start
                       == (ssize_t) this->tc_lines[curr_line].column_width()
                   && sel_range->lr_end != -1
                   && sel_range->lr_start < sel_range->lr_end)
        {
            // Del deleting line feed
            if (curr_line + 1 < (ssize_t) this->tc_lines.size()) {
                this->tc_lines[curr_line].append(this->tc_lines[curr_line + 1]);
                retval.push_back('\n');
                del_max = curr_line + 1;
            }
        } else if (sel_range->lr_start == 0 && sel_range->lr_end == -1) {
            log_debug("delete full line");
            retval.append(this->tc_lines[curr_line].al_string);
            retval.push_back('\n');
            del_max = curr_line;
            if (curr_line == range.sr_start.y) {
                log_debug("full first");
                full_first_line = true;
            }
        } else {
            log_debug("partial line change");
            auto& al = this->tc_lines[curr_line];
            auto start = al.column_to_byte_index(sel_range->lr_start);
            auto end = sel_range->lr_end == -1
                ? al.al_string.length()
                : al.column_to_byte_index(sel_range->lr_end);

            retval.append(al.al_string.substr(start, end - start));
            if (sel_range->lr_end == -1) {
                retval.push_back('\n');
            }
            al.erase(start, end - start);
            if (full_first_line || curr_line == range.sr_start.y) {
                al.insert(start, sf.to_string());
                this->tc_cursor.x = sel_range->lr_start;
            }
            if (!full_first_line && sel_range->lr_start == 0
                && range.sr_start.y < curr_line && curr_line == range.sr_end.y)
            {
                del_max = curr_line;
                this->tc_lines[range.sr_start.y].append(al);
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
    if (retval == sf) {
        if (!sf.empty()) {
            this->content_to_lines(this->get_content(),
                                   this->get_cursor_offset());
        }
    } else {
        this->update_lines();
    }

    ensure(!this->tc_lines.empty());

    return retval;
}

void
textinput_curses::replace_selection(string_fragment sf)
{
    static constexpr uint32_t mask
        = UC_CATEGORY_MASK_L | UC_CATEGORY_MASK_N | UC_CATEGORY_MASK_Pc;

    if (!this->tc_selection) {
        return;
    }
    auto range = this->tc_selection.value();
    auto change_pos = this->tc_change_log.size();
    auto old_text = this->replace_selection_no_change(sf);
    if (old_text == sf) {
        log_trace("no-op replacement");
    } else {
        auto is_wordbreak = !sf.empty()
            && !uc_is_general_category_withtable(sf.front_codepoint(), mask);
        log_debug("repl sel [%d:%d) - cursor [%d:%d)",
                  range.sr_start.x,
                  range.sr_start.y,
                  this->tc_cursor.x,
                  this->tc_cursor.y);
        if (this->tc_change_log.empty()
            || this->tc_change_log.back().ce_range.sr_end != range.sr_start
            || is_wordbreak)
        {
            auto redo_range = selected_range::from_key(
                range.sr_start.x < 0 ? this->tc_cursor : range.sr_start,
                this->tc_cursor);
            log_debug("  redo range [%d:%d] - [%d:%d]",
                      redo_range.sr_start.x,
                      redo_range.sr_start.y,
                      redo_range.sr_end.x,
                      redo_range.sr_end.y);
            if (change_pos < this->tc_change_log.size()) {
                // XXX an on_change handler can run and do its own replacement
                // before we get a change to add or entry
                log_debug("inserting change log at %d", change_pos);
                this->tc_change_log.insert(
                    std::next(this->tc_change_log.begin(), change_pos),
                    change_entry{redo_range, old_text});
            } else {
                this->tc_change_log.emplace_back(redo_range, old_text);
            }
        } else {
            auto& last_range = this->tc_change_log.back().ce_range;
            last_range.sr_end = this->tc_cursor;
            log_debug("extending undo range [%d:%d] - [%d:%d]",
                      last_range.sr_start.x,
                      last_range.sr_start.y,
                      last_range.sr_end.x,
                      last_range.sr_end.y);
        }
    }
}

void
textinput_curses::move_cursor_by(movement move)
{
    auto cursor_y_offset = this->tc_cursor.y - this->tc_top;
    this->tc_cursor += move;
    if (move.hm_dir == direction_t::up || move.hm_dir == direction_t::down) {
        if (move.hm_amount > 1) {
            this->tc_top = this->tc_cursor.y - cursor_y_offset;
        }
        this->tc_cursor.x = this->tc_max_cursor_x;
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
            > (ssize_t) this->tc_lines[this->tc_cursor.y].column_width())
    {
        if (this->tc_cursor.y + 1 < (ssize_t) this->tc_lines.size()) {
            this->tc_cursor.x = 0;
            this->tc_cursor.y += 1;
            this->tc_max_cursor_x = 0;
        }
    }
    this->clamp_point(this->tc_cursor);
    this->tc_drag_selection = std::nullopt;
    this->tc_selection = std::nullopt;
    this->ensure_cursor_visible();
}

void
textinput_curses::move_cursor_to(input_point ip)
{
    this->tc_cursor = ip;
    this->tc_drag_selection = std::nullopt;
    this->tc_selection = std::nullopt;
    this->ensure_cursor_visible();
}

void
textinput_curses::update_lines()
{
    const auto x = this->get_cursor_offset();
    this->content_to_lines(this->get_content(), x);
    this->set_needs_update();
    this->ensure_cursor_visible();

    this->tc_marks.clear();
    if (this->tc_in_popup_change) {
        log_trace("in popup change, skipping");
    } else {
        this->tc_popup.set_visible(false);
        this->tc_complete_range = std::nullopt;
        if (this->tc_on_change) {
            this->tc_on_change(*this);
        }
        if (!this->tc_popup.is_visible()) {
            this->tc_popup_type = popup_type_t::none;
        }
    }

    ensure(!this->tc_lines.empty());
}

textinput_curses::dimension_result
textinput_curses::get_visible_dimensions() const
{
    dimension_result retval;

    ncplane_dim_yx(
        this->tc_window, &retval.dr_full_height, &retval.dr_full_width);

    if (this->vc_y < (ssize_t) retval.dr_full_height) {
        retval.dr_height = std::min((int) retval.dr_full_height - this->vc_y,
                                    this->tc_height);
    }
    if (this->vc_x < (ssize_t) retval.dr_full_width) {
        retval.dr_width = std::min((long) retval.dr_full_width - this->vc_x,
                                   this->vc_width);
    }
    return retval;
}

std::string
textinput_curses::get_content(bool trim) const
{
    auto need_lf = false;
    std::string retval;

    for (const auto& al : this->tc_lines) {
        const auto& line = al.al_string;
        auto line_sf = string_fragment::from_str(line);
        if (trim) {
            line_sf = line_sf.rtrim(" ");
        }
        if (need_lf) {
            retval.push_back('\n');
        }
        retval.append(line_sf.data(), line_sf.length());
        need_lf = true;
    }
    return retval;
}

void
textinput_curses::focus()
{
    if (!this->vc_enabled) {
        this->vc_enabled = true;
        if (this->tc_on_focus) {
            this->tc_on_focus(*this);
        }
        this->set_needs_update();
    }

    if (this->tc_mode == mode_t::show_help
        || (this->tc_height && this->tc_notice)
        || (this->tc_selection
            && this->tc_selection->contains_exclusive(this->tc_cursor)))
    {
        notcurses_cursor_disable(ncplane_notcurses(this->tc_window));
        return;
    }
    auto term_x = this->vc_x + this->tc_cursor.x - this->tc_left;
    if (this->tc_cursor.y == 0) {
        term_x += this->tc_prefix.column_width();
    }
    notcurses_cursor_enable(ncplane_notcurses(this->tc_window),
                            this->vc_y + this->tc_cursor.y - this->tc_top,
                            term_x);
}

void
textinput_curses::blur()
{
    this->tc_popup_type = popup_type_t::none;
    this->tc_popup.set_visible(false);
    this->vc_enabled = false;
    if (this->tc_on_blur) {
        this->tc_on_blur(*this);
    }

    notcurses_cursor_disable(ncplane_notcurses(this->tc_window));
    this->set_needs_update();
}

void
textinput_curses::abort()
{
    this->blur();
    this->tc_selection = std::nullopt;
    this->tc_drag_selection = std::nullopt;
    if (this->tc_on_abort) {
        this->tc_on_abort(*this);
    }
}

void
textinput_curses::sync_to_sysclip() const
{
    auto clip_open_res = sysclip::open(sysclip::type_t::GENERAL);

    if (clip_open_res.isOk()) {
        auto clip_file = clip_open_res.unwrap();
        fmt::print(clip_file.in(),
                   FMT_STRING("{}"),
                   fmt::join(this->tc_clipboard, ""));
    } else {
        auto err_msg = clip_open_res.unwrapErr();
        log_error("unable to open clipboard: %s", err_msg.c_str());
    }
}

bool
textinput_curses::do_update()
{
    static auto& vc = view_colors::singleton();
    auto retval = false;

    if (!this->is_visible()) {
        return retval;
    }

    auto popup_height = this->tc_popup.get_height();
    auto rel_y = (this->tc_popup_type == popup_type_t::history
                      ? 0
                      : this->tc_cursor.y - this->tc_top)
        - popup_height;
    if (this->vc_y + rel_y < 0) {
        rel_y = this->tc_cursor.y - this->tc_top + popup_height + 1;
    }
    this->tc_popup.set_y(this->vc_y + rel_y);

    if (!this->vc_needs_update) {
        return view_curses::do_update();
    }

    auto dim = this->get_visible_dimensions();
    if (!this->vc_enabled) {
        ncplane_erase_region(
            this->tc_window, this->vc_y, this->vc_x, 1, dim.dr_width);
        auto lr = line_range{this->tc_left, this->tc_left + dim.dr_width};
        mvwattrline(this->tc_window,
                    this->vc_y,
                    this->vc_x,
                    this->tc_inactive_value,
                    lr);

        if (!this->tc_alt_value.empty()
            && (ssize_t) (this->tc_inactive_value.column_width() + 3
                          + this->tc_alt_value.column_width())
                < dim.dr_width)
        {
            auto alt_x = dim.dr_width - this->tc_alt_value.column_width();
            auto lr = line_range{0, (int) this->tc_alt_value.column_width()};
            mvwattrline(
                this->tc_window, this->vc_y, alt_x, this->tc_alt_value, lr);
        }

        this->vc_needs_update = false;
        return true;
    }

    if (this->tc_mode == mode_t::show_help) {
        this->tc_help_view.set_window(this->tc_window);
        this->tc_help_view.set_x(this->vc_x);
        this->tc_help_view.set_y(this->vc_y);
        this->tc_help_view.set_width(this->vc_width);
        this->tc_help_view.set_height(vis_line_t(this->tc_height));
        this->tc_help_view.set_visible(true);
        return view_curses::do_update();
    }

    retval = true;
    ssize_t row_count = this->tc_lines.size();
    auto y = this->vc_y;
    auto y_max = this->vc_y + dim.dr_height;
    if (row_count == 1 && this->tc_lines[0].empty()
        && !this->tc_suggestion.empty())
    {
        ncplane_erase_region(this->tc_window, y, this->vc_x, 1, dim.dr_width);
        auto al = attr_line_t(this->tc_suggestion)
                      .with_attr_for_all(VC_ROLE.value(role_t::VCR_SUGGESTION));
        al.insert(0, this->tc_prefix);
        auto lr = line_range{this->tc_left, this->tc_left + dim.dr_width};
        mvwattrline(this->tc_window, y, this->vc_x, al, lr);
        row_count -= 1;
        y += 1;
    }
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
        if (!this->tc_suggestion.empty() && !this->tc_popup.is_visible()
            && curr_line == this->tc_cursor.y
            && this->tc_cursor.x == (ssize_t) al.column_width())
        {
            al.append(this->tc_suggestion,
                      VC_ROLE.value(role_t::VCR_SUGGESTION));
        }
        if (curr_line == 0) {
            al.insert(0, this->tc_prefix);
        }
        mvwattrline(this->tc_window, y, this->vc_x, al, lr);
    }
    for (; y < y_max; y++) {
        static constexpr auto EMPTY_LR = line_range::empty_at(0);

        auto al = attr_line_t();
        ncplane_erase_region(this->tc_window, y, this->vc_x, 1, dim.dr_width);
        mvwattrline(
            this->tc_window, y, this->vc_x, al, EMPTY_LR, role_t::VCR_ALT_ROW);
    }
    if (this->tc_notice) {
        auto notice_lines = this->tc_notice.value();
        auto avail_height = std::min(dim.dr_height, (int) notice_lines.size());
        auto notice_y = this->vc_y + dim.dr_height - avail_height;

        for (auto& al : notice_lines) {
            auto lr = line_range{0, dim.dr_width};
            mvwattrline(this->tc_window, notice_y++, this->vc_x, al, lr);
            if (notice_y >= y_max) {
                break;
            }
        }
    } else if (this->tc_mode == mode_t::searching) {
        auto search_prompt = attr_line_t(" ");
        if (this->tc_search.empty() || this->tc_search_found.has_value()) {
            search_prompt.append(this->tc_search)
                .append(" ", VC_ROLE.value(role_t::VCR_CURSOR_LINE));
        } else {
            search_prompt.append(this->tc_search,
                                 VC_ROLE.value(role_t::VCR_SEARCH));
        }
        if (this->tc_search_found && this->tc_search_found.value()) {
            search_prompt.with_attr_for_all(VC_ROLE.value(role_t::VCR_STATUS));
        }
        search_prompt.insert(0, " Search: "_status_subtitle);
        if (this->tc_search_found && !this->tc_search_found.value()) {
            search_prompt.with_attr_for_all(
                VC_ROLE.value(role_t::VCR_ALERT_STATUS));
        }
        auto lr = line_range{0, dim.dr_width};
        mvwattrline(this->tc_window,
                    this->vc_y + dim.dr_height - 1,
                    this->vc_x,
                    search_prompt,
                    lr);
    } else if (this->tc_height > 1) {
        auto mark_iter = this->tc_marks.find(this->tc_cursor);

        if (mark_iter != this->tc_marks.end()) {
            auto mark_lines = mark_iter->second.to_attr_line().split_lines();
            auto avail_height
                = std::min(dim.dr_height, (int) mark_lines.size());
            auto notice_y = this->vc_y + dim.dr_height - avail_height;
            for (auto& al : mark_lines) {
                al.with_attr_for_all(VC_ROLE.value(role_t::VCR_STATUS));
                auto lr = line_range{0, dim.dr_width};
                mvwattrline(this->tc_window, notice_y++, this->vc_x, al, lr);
                if (notice_y >= y_max) {
                    break;
                }
            }
        }
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
    line_range crange, std::vector<attr_line_t> possibilities)
{
    if (possibilities.empty()) {
        this->tc_popup_type = popup_type_t::none;
        return;
    }

    this->tc_popup_type = popup_type_t::completion;
    auto dim = this->get_visible_dimensions();
    auto max_width = possibilities
        | lnav::itertools::map(&attr_line_t::column_width)
        | lnav::itertools::max();

    auto full_width = std::min((int) max_width.value_or(1) + 3, dim.dr_width);
    auto new_sel = 0_vl;
    auto popup_height = vis_line_t(
        std::min(this->tc_max_popup_height, possibilities.size() + 1));
    ssize_t rel_x = crange.lr_start;
    if (this->tc_cursor.y == 0) {
        rel_x += this->tc_prefix.column_width();
    }
    if (rel_x + full_width > dim.dr_width) {
        rel_x = dim.dr_width - full_width;
    }
    if (this->vc_x + rel_x > 0) {
        rel_x -= 1;  // XXX for border
    }
    auto rel_y = this->tc_cursor.y - this->tc_top - popup_height;
    if (this->vc_y + rel_y < 0) {
        rel_y = this->tc_cursor.y - this->tc_top + 1;
    } else {
        std::reverse(possibilities.begin(), possibilities.end());
        new_sel = vis_line_t(possibilities.size() - 1);
    }

    this->tc_complete_range
        = selected_range::from_key(this->tc_cursor.copy_with_x(crange.lr_start),
                                   this->tc_cursor.copy_with_x(crange.lr_end));
    this->tc_popup_source.replace_with(possibilities);
    this->tc_popup.set_window(this->tc_window);
    this->tc_popup.set_x(this->vc_x + rel_x);
    this->tc_popup.set_y(this->vc_y + rel_y);
    this->tc_popup.set_width(full_width);
    this->tc_popup.set_height(popup_height);
    this->tc_popup.set_visible(true);
    this->tc_popup.set_top(0_vl);
    this->tc_popup.set_selection(new_sel);
    this->set_needs_update();
}

void
textinput_curses::open_popup_for_history(std::vector<attr_line_t> possibilities)
{
    if (possibilities.empty()) {
        this->tc_popup_type = popup_type_t::none;
        return;
    }

    this->tc_popup_type = popup_type_t::history;
    auto new_sel = 0_vl;
    auto popup_height = vis_line_t(
        std::min(this->tc_max_popup_height, possibilities.size() + 1));
    auto rel_y = this->tc_cursor.y - this->tc_top - popup_height;
    if (this->vc_y + rel_y < 0) {
        rel_y = this->tc_cursor.y - this->tc_top - popup_height;
    } else {
        std::reverse(possibilities.begin(), possibilities.end());
        new_sel = vis_line_t(possibilities.size() - 1);
    }

    this->tc_complete_range = selected_range::from_key(
        input_point::home(),
        input_point{
            (int) this->tc_lines.back().column_width(),
            (int) this->tc_lines.size() - 1,
        });
    this->tc_popup_source.replace_with(possibilities);
    this->tc_popup.set_window(this->tc_window);
    this->tc_popup.set_title("History");
    this->tc_popup.set_x(this->vc_x);
    this->tc_popup.set_y(this->vc_y + rel_y);
    this->tc_popup.set_width(this->vc_width);
    this->tc_popup.set_height(popup_height);
    this->tc_popup.set_top(0_vl);
    this->tc_popup.set_selection(new_sel);
    this->tc_popup.set_visible(true);
    if (this->tc_on_popup_change) {
        this->tc_in_popup_change = true;
        this->tc_on_popup_change(*this);
        this->tc_in_popup_change = false;
    }
    this->set_needs_update();
}

void
textinput_curses::tick(ui_clock::time_point now)
{
    if (this->tc_last_tick_after_input) {
        auto diff = now - this->tc_last_tick_after_input.value();

        if (diff >= 750ms && !this->tc_timeout_fired) {
            if (this->tc_on_timeout) {
                this->tc_on_timeout(*this);
            }
            this->tc_timeout_fired = true;
        }
    } else {
        this->tc_last_tick_after_input = now;
        this->tc_timeout_fired = false;
    }
}

int
textinput_curses::get_cursor_offset() const
{
    if (this->tc_cursor.y < 0
        || this->tc_cursor.y >= (ssize_t) this->tc_lines.size())
    {
        // XXX can happen during update_lines() with history/pasted insert
        return 0;
    }

    int retval = 0;
    for (auto row = 0; row < this->tc_cursor.y; row++) {
        retval += this->tc_lines[row].al_string.size() + 1;
    }
    retval += this->tc_cursor.x;

    return retval;
}

textinput_curses::input_point
textinput_curses::get_point_for_offset(int offset) const
{
    auto retval = input_point::home();
    auto row = size_t{0};
    for (; row < this->tc_lines.size() && offset > 0; row++) {
        if (offset < (ssize_t) this->tc_lines[row].al_string.size() + 1) {
            break;
        }
        offset -= this->tc_lines[row].al_string.size() + 1;
        retval.y += 1;
    }
    if (row < this->tc_lines.size()) {
        retval.x = this->tc_lines[row].byte_to_column_index(offset);
    }

    return retval;
}

void
textinput_curses::add_mark(input_point pos,
                           const lnav::console::user_message& msg)
{
    if (pos.y < 0 || pos.y >= (ssize_t) this->tc_lines.size()) {
        log_error("invalid mark position: %d:%d", pos.x, pos.y);
        return;
    }

    if (this->tc_marks.count(pos) > 0) {
        return;
    }

    auto& line = this->tc_lines[pos.y];
    auto byte_x = (int) line.column_to_byte_index(pos.x);
    auto lr = line_range{byte_x, byte_x + 1};
    line.al_attrs.emplace_back(lr, VC_ROLE.value(role_t::VCR_ERROR));
    line.al_attrs.emplace_back(lr, VC_STYLE.value(text_attrs::with_reverse()));

    this->tc_marks.emplace(pos, msg);
}
