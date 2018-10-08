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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include <vector>
#include <algorithm>

#include "pcrepp.hh"
#include "lnav_util.hh"
#include "data_parser.hh"
#include "ansi_scrubber.hh"
#include "log_format.hh"
#include "textview_curses.hh"
#include "view_curses.hh"

using namespace std;

void text_filter::add_line(
        logfile_filter_state &lfs, logfile::const_iterator ll, shared_buffer_ref &line) {
    bool match_state = this->matches(*lfs.tfs_logfile, *ll, line);

    if (!ll->is_continued()) {
        this->end_of_message(lfs);
    }

    this->lf_message_matched = this->lf_message_matched || match_state;
    this->lf_lines_for_message += 1;
}

bookmark_type_t textview_curses::BM_USER("user");
bookmark_type_t textview_curses::BM_SEARCH("search");
bookmark_type_t textview_curses::BM_META("meta");

string_attr_type textview_curses::SA_ORIGINAL_LINE("original_line");
string_attr_type textview_curses::SA_BODY("body");
string_attr_type textview_curses::SA_HIDDEN("hidden");
string_attr_type textview_curses::SA_FORMAT("format");

textview_curses::textview_curses()
    : tc_sub_source(NULL),
      tc_delegate(NULL),
      tc_selection_start(-1),
      tc_selection_last(-1),
      tc_selection_cleared(false),
      tc_hide_fields(true)
{
    this->tc_follow_deadline.tv_sec = 0;
    this->tc_follow_deadline.tv_usec = 0;
    this->set_data_source(this);
}

textview_curses::~textview_curses()
{ }

void textview_curses::reload_data(void)
{
    if (this->tc_sub_source != nullptr) {
        this->tc_sub_source->text_update_marks(this->tc_bookmarks);
    }
    listview_curses::reload_data();

    if (this->tc_sub_source != nullptr) {
        auto ttt = dynamic_cast<text_time_translator *>(this->tc_sub_source);

        if (ttt != nullptr) {
            ttt->data_reloaded(this);
        }
    }
}

void textview_curses::grep_begin(grep_proc<vis_line_t> &gp, vis_line_t start, vis_line_t stop)
{
    require(this->tc_searching >= 0);

    this->tc_searching += 1;
    this->tc_search_action.invoke(this);

    bookmark_vector<vis_line_t> &search_bv = this->tc_bookmarks[&BM_SEARCH];

    if (start != -1) {
        auto pair = search_bv.equal_range(vis_line_t(start), vis_line_t(stop));

        for (auto mark_iter = pair.first;
             mark_iter != pair.second;
             ++mark_iter) {
            this->set_user_mark(&BM_SEARCH, *mark_iter, false);
        }
    }

    listview_curses::reload_data();
}

void textview_curses::grep_end(grep_proc<vis_line_t> &gp)
{
    this->tc_searching -= 1;
    this->tc_search_action.invoke(this);

    ensure(this->tc_searching >= 0);
}

void textview_curses::grep_match(grep_proc<vis_line_t> &gp,
                                 vis_line_t line,
                                 int start,
                                 int end)
{
    this->tc_bookmarks[&BM_SEARCH].insert_once(vis_line_t(line));
    if (this->tc_sub_source != nullptr) {
        this->tc_sub_source->text_mark(&BM_SEARCH, line, true);
    }

    if (this->get_top() <= line && line <= this->get_bottom()) {
        listview_curses::reload_data();
    }
}

void textview_curses::listview_value_for_rows(const listview_curses &lv,
                                              vis_line_t row,
                                              vector<attr_line_t> &rows_out)
{
    for (auto &al : rows_out) {
        this->textview_value_for_row(row, al);
        ++row;
    }
}

bool textview_curses::handle_mouse(mouse_event &me)
{
    unsigned long width;
    vis_line_t height;

    if (this->tc_selection_start == -1 &&
        listview_curses::handle_mouse(me)) {
        return true;
    }

    if (this->tc_delegate != nullptr &&
        this->tc_delegate->text_handle_mouse(*this, me)) {
        return true;
    }

    if (me.me_button != BUTTON_LEFT) {
        return false;
    }

    vis_line_t mouse_line(this->get_top() + me.me_y);

    if (mouse_line > this->get_bottom()) {
        mouse_line = this->get_bottom();
    }

    this->get_dimensions(height, width);

    switch (me.me_state) {
    case BUTTON_STATE_PRESSED:
        this->tc_selection_start = mouse_line;
        this->tc_selection_last = vis_line_t(-1);
        this->tc_selection_cleared = false;
        break;
    case BUTTON_STATE_DRAGGED:
        if (me.me_y <= 0) {
            this->shift_top(vis_line_t(-1));
            me.me_y = 0;
            mouse_line = this->get_top();
        }
        if (me.me_y >= height && this->get_top() < this->get_top_for_last_row()) {
            this->shift_top(vis_line_t(1));
            me.me_y = height;
            mouse_line = this->get_bottom();
        }

        if (this->tc_selection_last == mouse_line)
            break;

        if (this->tc_selection_last != -1) {
            this->toggle_user_mark(&textview_curses::BM_USER,
               this->tc_selection_start,
               this->tc_selection_last);
        }
        if (this->tc_selection_start == mouse_line) {
            this->tc_selection_last = vis_line_t(-1);
        }
        else {
            if (!this->tc_selection_cleared) {
                if (this->tc_sub_source != NULL) {
                    this->tc_sub_source->text_clear_marks(&BM_USER);
                }
                this->tc_bookmarks[&BM_USER].clear();

                this->tc_selection_cleared = true;
            }
            this->toggle_user_mark(&BM_USER,
               this->tc_selection_start,
               mouse_line);
            this->tc_selection_last = mouse_line;
        }
        this->reload_data();
        break;
    case BUTTON_STATE_RELEASED:
        this->tc_selection_start = vis_line_t(-1);
        this->tc_selection_last = vis_line_t(-1);
        this->tc_selection_cleared = false;
        break;
    }

    return true;
}

void textview_curses::textview_value_for_row(vis_line_t row,
                                             attr_line_t &value_out)
{
    view_colors &vc = view_colors::singleton();
    bookmark_vector<vis_line_t> &user_marks = this->tc_bookmarks[&BM_USER];
    string_attrs_t &sa = value_out.get_attrs();
    string &str = value_out.get_string();
    text_format_t source_format = this->tc_sub_source->get_text_format();
    intern_string_t format_name;

    this->tc_sub_source->text_value_for_line(*this, row, str);
    this->tc_sub_source->text_attrs_for_line(*this, row, sa);

    scrub_ansi_string(str, sa);

    struct line_range body, orig_line;

    body = find_string_attr_range(sa, &SA_BODY);
    if (body.lr_start == -1) {
        body.lr_start = 0;
        body.lr_end = str.size();
    }

    orig_line = find_string_attr_range(sa, &SA_ORIGINAL_LINE);
    if (!orig_line.is_valid()) {
        orig_line.lr_start = 0;
        orig_line.lr_end = str.size();
    }

    auto sa_iter = find_string_attr(sa, &SA_FORMAT);
    if (sa_iter != sa.end()) {
        format_name = sa_iter->to_string();
    }

    for (auto &tc_highlight : this->tc_highlights) {
        // XXX testing for '$search' here sucks
        bool internal_hl = tc_highlight.first[0] == '$'
                           && tc_highlight.first != "$search"
                           && tc_highlight.first != "$preview";

        if (tc_highlight.second.h_text_format != TF_UNKNOWN &&
            source_format != tc_highlight.second.h_text_format) {
            continue;
        }

        if (!tc_highlight.second.h_format_name.empty() &&
            tc_highlight.second.h_format_name != format_name) {
            continue;
        }

        tc_highlight.second.annotate(value_out, internal_hl ? body.lr_start : 0);
    }

    if (this->tc_hide_fields) {
        for (auto &sattr : sa) {
            if (sattr.sa_type == &SA_HIDDEN &&
                sattr.sa_range.length() > 3) {
                struct line_range &lr = sattr.sa_range;

                str.replace(lr.lr_start, lr.length(), "...");
                shift_string_attrs(sa, lr.lr_start + 1, -(lr.length() - 3));
                sattr.sa_type = &VC_GRAPHIC;
                sattr.sa_value.sav_int =
                    vc.attrs_for_role(view_colors::VCR_HIDDEN) |
                    ACS_BULLET | A_UNDERLINE;
                lr.lr_end = lr.lr_start + 3;

                for_each(sa.begin(), sa.end(), [&] (string_attr &attr) {
                    if (attr.sa_type == &VC_STYLE &&
                        attr.sa_range.lr_start == lr.lr_start) {
                        attr.sa_type = nullptr;
                    }
                });
            }
        }
    }

#if 0
    typedef std::map<std::string, view_colors::role_t> key_map_t;
    static key_map_t key_roles;

    data_scanner ds(str);
    data_parser  dp(&ds);

    dp.parse();

    for (list<data_parser::element>::iterator iter = dp.dp_stack.begin();
         iter != dp.dp_stack.end();
         ++iter) {
        view_colors &vc = view_colors::singleton();

        if (iter->e_token == DNT_PAIR) {
            list<data_parser::element>::iterator pair_iter;
            key_map_t::iterator km_iter;
            data_token_t        value_token;
            struct line_range   lr;
            string key;

            value_token =
                iter->e_sub_elements->back().e_sub_elements->front().e_token;
            if (value_token == DT_STRING) {
                continue;
            }

            lr.lr_start = iter->e_capture.c_begin;
            lr.lr_end   = iter->e_capture.c_end;

            key = ds.get_input().get_substr(
                &iter->e_sub_elements->front().e_capture);
            if ((km_iter = key_roles.find(key)) == key_roles.end()) {
                key_roles[key] = vc.next_highlight();
            }
            /* fprintf(stderr, "key = %s\n", key.c_str()); */
            sa[lr].insert(make_string_attr("style",
                                           vc.attrs_for_role(key_roles[key])));

            pair_iter = iter->e_sub_elements->begin();
            ++pair_iter;

            lr.lr_start = pair_iter->e_capture.c_begin;
            lr.lr_end   = pair_iter->e_capture.c_end;
            sa[lr].insert(make_string_attr("style",
                                           COLOR_PAIR(view_colors::VC_WHITE) |
                                           A_BOLD));
        }
    }
#endif

    if (binary_search(user_marks.begin(), user_marks.end(), row)) {
        sa.emplace_back(orig_line, &view_curses::VC_STYLE, A_REVERSE);
    }
}

void text_time_translator::scroll_invoked(textview_curses *tc)
{
    if (tc->get_inner_height() > 0) {
        this->ttt_top_time = this->time_for_row((int) tc->get_top());
    }
}

void text_time_translator::data_reloaded(textview_curses *tc)
{
    if (tc->get_inner_height() > 0) {
        if (this->ttt_top_time.tv_sec != 0) {
            vis_line_t new_top(this->row_for_time(this->ttt_top_time));

            if (new_top >= 0) {
                tc->set_top(new_top);
            }
        }
        this->ttt_top_time = this->time_for_row((int) tc->get_top());
    }
}
