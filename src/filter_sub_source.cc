/**
 * Copyright (c) 2018, Timothy Stack
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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include "lnav.hh"

#include "filter_sub_source.hh"
#include "readline_possibilities.hh"
#include "readline_highlighters.hh"

using namespace std;

filter_sub_source::filter_sub_source()
    : fss_change_wire(*this, &filter_sub_source::rl_change),
      fss_perform_wire(*this, &filter_sub_source::rl_perform),
      fss_abort_wire(*this, &filter_sub_source::rl_abort),
      fss_display_match_wire(*this, &filter_sub_source::rl_display_matches),
      fss_display_next_wire(*this, &filter_sub_source::rl_display_next)
{
    this->filter_context.set_highlighter(readline_regex_highlighter)
        .set_append_character(0);
    this->fss_editor.add_context(LNM_FILTER, this->filter_context);
    this->fss_editor.set_change_action(&this->fss_change_wire);
    this->fss_editor.set_perform_action(&this->fss_perform_wire);
    this->fss_editor.set_abort_action(&this->fss_abort_wire);
    this->fss_editor.set_display_match_action(&this->fss_display_match_wire);
    this->fss_editor.set_display_next_action(&this->fss_display_next_wire);
    this->fss_match_view.set_sub_source(&this->fss_match_source);
    this->fss_match_view.set_height(0_vl);
    this->fss_match_view.set_show_scrollbar(true);
    this->fss_match_view.set_default_role(view_colors::VCR_POPUP);
}

bool filter_sub_source::list_input_handle_key(listview_curses &lv, int ch)
{
    if (this->fss_editing) {
        switch (ch) {
            case KEY_ESCAPE:
            case KEY_CTRL_RBRACKET:
                this->fss_editor.abort();
                return true;
            default:
                this->fss_editor.handle_key(ch);
                return true;
        }
    }

    switch (ch) {
        case '\t':
        case KEY_BTAB:
        case 'q':
            lnav_data.ld_mode = LNM_PAGING;
            lnav_data.ld_filter_view.reload_data();
            return true;
        case ' ': {
            textview_curses *top_view = *lnav_data.ld_view_stack.top();
            text_sub_source *tss = top_view->get_sub_source();
            filter_stack &fs = tss->get_filters();

            if (fs.empty()) {
                return true;
            }

            shared_ptr<text_filter> tf = *(fs.begin() + lv.get_selection());

            fs.set_filter_enabled(tf, !tf->is_enabled());
            tss->text_filters_changed();
            lv.reload_data();
            return true;
        }
        case 'D': {
            textview_curses *top_view = *lnav_data.ld_view_stack.top();
            text_sub_source *tss = top_view->get_sub_source();
            filter_stack &fs = tss->get_filters();

            if (fs.empty()) {
                return true;
            }

            shared_ptr<text_filter> tf = *(fs.begin() + lv.get_selection());

            fs.delete_filter(tf->get_id());
            tss->text_filters_changed();
            lv.reload_data();
            return true;
        }
        case 'i': {
            textview_curses *top_view = *lnav_data.ld_view_stack.top();
            text_sub_source *tss = top_view->get_sub_source();
            filter_stack &fs = tss->get_filters();

            if (fs.full()) {
                return true;
            }

            auto ef = make_shared<empty_filter>(text_filter::type_t::INCLUDE, fs.next_index());
            fs.add_filter(ef);
            lv.set_selection(vis_line_t(fs.size() - 1));
            lv.reload_data();

            this->fss_editing = true;

            add_view_text_possibilities(&this->fss_editor, LNM_FILTER, "*", top_view);
            lnav_data.ld_filter_status_source.tss_prompt.set_value(
                "Enter a regular expression to match lines to filter in:");
            this->fss_editor.set_window(lv.get_window());
            this->fss_editor.set_visible(true);
            this->fss_editor.set_y(lv.get_y() + (int) (lv.get_selection() - lv.get_top()));
            this->fss_editor.set_left(8);
            this->fss_editor.set_width(this->tss_view->get_width() - 8 - 1);
            this->fss_editor.focus(LNM_FILTER, "", "");
            this->fss_filter_state = true;
            ef->disable();
            tss->text_filters_changed();
            return true;
        }
        case 'o': {
            textview_curses *top_view = *lnav_data.ld_view_stack.top();
            text_sub_source *tss = top_view->get_sub_source();
            filter_stack &fs = tss->get_filters();

            if (fs.full()) {
                return true;
            }

            auto ef = make_shared<empty_filter>(text_filter::type_t::EXCLUDE, fs.next_index());
            fs.add_filter(ef);
            lv.set_selection(vis_line_t(fs.size() - 1));
            lv.reload_data();

            this->fss_editing = true;

            add_view_text_possibilities(&this->fss_editor, LNM_FILTER, "*", top_view);
            lnav_data.ld_filter_status_source.tss_prompt.set_value(
                "Enter a regular expression to match lines to filter out:");
            this->fss_editor.set_window(lv.get_window());
            this->fss_editor.set_visible(true);
            this->fss_editor.set_y(lv.get_y() + (int) (lv.get_selection() - lv.get_top()));
            this->fss_editor.set_left(8);
            this->fss_editor.set_width(this->tss_view->get_width() - 8 - 1);
            this->fss_editor.focus(LNM_FILTER, "", "");
            this->fss_filter_state = true;
            ef->disable();
            tss->text_filters_changed();
            return true;
        }
        case '\r':
        case KEY_ENTER: {
            textview_curses *top_view = *lnav_data.ld_view_stack.top();
            text_sub_source *tss = top_view->get_sub_source();
            filter_stack &fs = tss->get_filters();

            if (fs.empty()) {
                return true;
            }

            shared_ptr<text_filter> tf = *(fs.begin() + lv.get_selection());

            this->fss_editing = true;

            add_view_text_possibilities(&this->fss_editor, LNM_FILTER, "*", top_view);
            this->fss_editor.set_window(lv.get_window());
            this->fss_editor.set_visible(true);
            this->fss_editor.set_y(lv.get_y() + (int) (lv.get_selection() - lv.get_top()));
            this->fss_editor.set_left(8);
            this->fss_editor.set_width(this->tss_view->get_width() - 8 - 1);
            this->fss_editor.focus(LNM_FILTER, "", tf->get_id().c_str());
            this->fss_filter_state = tf->is_enabled();
            tf->disable();
            tss->text_filters_changed();
            return true;
        }
        default:
            log_debug("unhandled %x", ch);
            break;
    }

    return false;
}

size_t filter_sub_source::text_line_count()
{
    return (lnav_data.ld_view_stack.top() | [] (auto tc) {
        text_sub_source *tss = tc->get_sub_source();
        filter_stack &fs = tss->get_filters();

        return nonstd::make_optional(fs.size());
    }).value_or(0);
}

size_t filter_sub_source::text_line_width(textview_curses &curses)
{
    textview_curses *top_view = *lnav_data.ld_view_stack.top();
    text_sub_source *tss = top_view->get_sub_source();
    filter_stack &fs = tss->get_filters();
    size_t retval = 0;

    for (auto &filter : fs) {
        retval = std::max(filter->get_id().size() + 8, retval);
    }

    return retval;
}

void filter_sub_source::text_value_for_line(textview_curses &tc, int line,
                                            std::string &value_out,
                                            text_sub_source::line_flags_t flags)
{
    textview_curses *top_view = *lnav_data.ld_view_stack.top();
    text_sub_source *tss = top_view->get_sub_source();
    filter_stack &fs = tss->get_filters();
    shared_ptr<text_filter> tf = *(fs.begin() + line);

    value_out = "    ";
    switch (tf->get_type()) {
        case text_filter::INCLUDE:
            value_out.append(" IN ");
            break;
        case text_filter::EXCLUDE:
            value_out.append("OUT ");
            break;
        default:
            ensure(0);
            break;
    }

    value_out.append(tf->get_id());
}

void filter_sub_source::text_attrs_for_line(textview_curses &tc, int line,
                                            string_attrs_t &value_out)
{
    textview_curses *top_view = *lnav_data.ld_view_stack.top();
    text_sub_source *tss = top_view->get_sub_source();
    filter_stack &fs = tss->get_filters();
    shared_ptr<text_filter> tf = *(fs.begin() + line);
    bool selected = lnav_data.ld_mode == LNM_FILTER && line == tc.get_selection();
    int bg = selected ? COLOR_WHITE : COLOR_BLACK;

    if (selected) {
        value_out.emplace_back(line_range{0, 1}, &view_curses::VC_GRAPHIC, ACS_RARROW);
    }

    chtype enabled = tf->is_enabled() ? ACS_DIAMOND : ' ';

    line_range lr{2, 3};
    value_out.emplace_back(lr, &view_curses::VC_GRAPHIC, enabled);
    if (tf->is_enabled()) {
        value_out.emplace_back(lr, &view_curses::VC_FOREGROUND, COLOR_GREEN);
    }

    int fg = tf->get_type() == text_filter::INCLUDE ? COLOR_GREEN : COLOR_RED;
    value_out.emplace_back(line_range{4, 7}, &view_curses::VC_FOREGROUND, fg);
    value_out.emplace_back(line_range{4, 7}, &view_curses::VC_STYLE, A_BOLD);
    fg = selected ? COLOR_BLACK : COLOR_WHITE;
    value_out.emplace_back(line_range{0, -1}, &view_curses::VC_FOREGROUND, fg);
    value_out.emplace_back(line_range{0, -1}, &view_curses::VC_BACKGROUND, bg);
}

size_t filter_sub_source::text_size_for_line(textview_curses &tc, int line,
                                             text_sub_source::line_flags_t raw)
{
    textview_curses *top_view = *lnav_data.ld_view_stack.top();
    text_sub_source *tss = top_view->get_sub_source();
    filter_stack &fs = tss->get_filters();
    shared_ptr<text_filter> tf = *(fs.begin() + line);

    return 8 + tf->get_id().size();
}

void filter_sub_source::rl_change(readline_curses *rc)
{
    textview_curses *top_view = *lnav_data.ld_view_stack.top();
    text_sub_source *tss = top_view->get_sub_source();
    filter_stack &fs = tss->get_filters();
    if (fs.empty()) {
        return;
    }

    auto iter = fs.begin() + this->tss_view->get_selection();
    shared_ptr<text_filter> tf = *iter;
    string new_value = rc->get_line_buffer();
    auto_mem<pcre> code;
    const char *errptr;
    int eoff;

    if ((code = pcre_compile(new_value.c_str(),
                             PCRE_CASELESS,
                             &errptr,
                             &eoff,
                             nullptr)) == nullptr) {
        lnav_data.ld_filter_status_source.tss_error
                 .set_value("error: %s", errptr);
    } else {
        textview_curses::highlight_map_t &hm = top_view->get_highlights();
        highlighter hl(code.release());
        int color;

        if (tf->get_type() == text_filter::EXCLUDE) {
            color = COLOR_RED;
        } else {
            color = COLOR_GREEN;
        }
        hl.with_attrs(
            view_colors::ansi_color_pair(COLOR_BLACK, color) | A_BLINK);

        hm["$preview"] = hl;
        top_view->reload_data();
        lnav_data.ld_filter_status_source.tss_error.clear();
    }
}

void filter_sub_source::rl_perform(readline_curses *rc)
{
    textview_curses *top_view = *lnav_data.ld_view_stack.top();
    text_sub_source *tss = top_view->get_sub_source();
    filter_stack &fs = tss->get_filters();
    auto iter = fs.begin() + this->tss_view->get_selection();
    shared_ptr<text_filter> tf = *iter;
    string new_value = rc->get_value();
    auto_mem<pcre> code;
    const char *errptr;
    int eoff;

    if (new_value.empty()) {
        this->rl_abort(rc);
    } else if ((code = pcre_compile(new_value.c_str(),
                                    PCRE_CASELESS,
                                    &errptr,
                                    &eoff,
                                    nullptr)) == nullptr) {
        this->rl_abort(rc);
    } else {
        tf->lf_deleted = true;
        tss->text_filters_changed();

        auto pf = make_shared<pcre_filter>(tf->get_type(),
                                           new_value, tf->get_index(), code.release());

        *iter = pf;
        tss->text_filters_changed();
    }

    lnav_data.ld_filter_status_source.tss_prompt.clear();
    this->fss_editing = false;
    this->fss_editor.set_visible(false);
    this->tss_view->reload_data();
}

void filter_sub_source::rl_abort(readline_curses *rc)
{
    textview_curses *top_view = *lnav_data.ld_view_stack.top();
    text_sub_source *tss = top_view->get_sub_source();
    filter_stack &fs = tss->get_filters();
    auto iter = fs.begin() + this->tss_view->get_selection();
    shared_ptr<text_filter> tf = *iter;

    lnav_data.ld_filter_status_source.tss_prompt.clear();
    lnav_data.ld_filter_status_source.tss_error.clear();
    top_view->get_highlights().erase("$preview");
    top_view->reload_data();
    fs.delete_filter("");
    this->fss_editor.set_visible(false);
    this->fss_editing = false;
    this->tss_view->set_needs_update();
    tf->set_enabled(this->fss_filter_state);
    tss->text_filters_changed();
    this->tss_view->reload_data();
}

void filter_sub_source::rl_display_matches(readline_curses *rc)
{
    const std::vector<std::string> &matches = rc->get_matches();
    unsigned long width = 0;

    if (matches.empty()) {
        this->fss_match_source.clear();
        this->fss_match_view.set_height(0_vl);
        this->tss_view->set_needs_update();
    } else {
        string current_match = rc->get_match_string();
        attr_line_t al;
        vis_line_t line, selected_line;

        for (auto &match : matches) {
            if (match == current_match) {
                al.append(match, &view_curses::VC_STYLE, A_REVERSE);
                selected_line = line;
            } else {
                al.append(match);
            }
            al.append(1, '\n');
            width = std::max(width, match.size());
            line += 1_vl;
        }

        this->fss_match_view.set_selection(selected_line);
        this->fss_match_source.replace_with(al);
        this->fss_match_view.set_height(std::min(vis_line_t(matches.size()), 3_vl));
    }

    this->fss_match_view.set_window(this->tss_view->get_window());
    this->fss_match_view.set_y(rc->get_y() + 1);
    this->fss_match_view.set_x(rc->get_left() + rc->get_match_start());
    this->fss_match_view.set_width(width + 3);
    this->fss_match_view.set_needs_update();
    this->fss_match_view.scroll_selection_into_view();
    this->fss_match_view.reload_data();
}

void filter_sub_source::rl_display_next(readline_curses *rc)
{
    textview_curses &tc = this->fss_match_view;

    if (tc.get_top() >= (tc.get_top_for_last_row() - 1)) {
        tc.set_top(0_vl);
    }
    else {
        tc.shift_top(tc.get_height());
    }
}
