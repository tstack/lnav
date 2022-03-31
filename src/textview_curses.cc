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

#include "ansi_scrubber.hh"
#include "base/time_util.hh"
#include "config.h"
#include "data_parser.hh"
#include "fmt/format.h"
#include "lnav_config.hh"
#include "log_format.hh"
#include "logfile.hh"
#include "shlex.hh"
#include "view_curses.hh"

const auto REVERSE_SEARCH_OFFSET = 2000_vl;

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

void
text_filter::add_line(logfile_filter_state& lfs,
                      logfile::const_iterator ll,
                      shared_buffer_ref& line)
{
    bool match_state = this->matches(*lfs.tfs_logfile, ll, line);

    if (ll->is_message()) {
        this->end_of_message(lfs);
    }

    lfs.tfs_message_matched[this->lf_index]
        = lfs.tfs_message_matched[this->lf_index] || match_state;
    lfs.tfs_lines_for_message[this->lf_index] += 1;
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

const bookmark_type_t textview_curses::BM_USER("user");
const bookmark_type_t textview_curses::BM_USER_EXPR("user-expr");
const bookmark_type_t textview_curses::BM_SEARCH("search");
const bookmark_type_t textview_curses::BM_META("meta");

textview_curses::textview_curses() : tc_search_action(noop_func{})
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

    for (auto iter = this->tc_highlights.begin();
         iter != this->tc_highlights.end();)
    {
        if (iter->first.first != highlight_source_t::THEME) {
            ++iter;
            continue;
        }

        iter = this->tc_highlights.erase(iter);
    }

    std::map<std::string, std::string> vars;
    auto curr_theme_iter
        = lnav_config.lc_ui_theme_defs.find(lnav_config.lc_ui_theme);
    if (curr_theme_iter != lnav_config.lc_ui_theme_defs.end()) {
        vars = curr_theme_iter->second.lt_vars;
    }

    for (const auto& theme_name : {DEFAULT_THEME_NAME, lnav_config.lc_ui_theme})
    {
        auto theme_iter = lnav_config.lc_ui_theme_defs.find(theme_name);

        if (theme_iter == lnav_config.lc_ui_theme_defs.end()) {
            continue;
        }

        for (const auto& hl_pair : theme_iter->second.lt_highlights) {
            if (hl_pair.second.hc_regex.empty()) {
                continue;
            }

            const char* errptr;
            pcre* code;
            int eoff;

            if ((code = pcre_compile(hl_pair.second.hc_regex.c_str(),
                                     0,
                                     &errptr,
                                     &eoff,
                                     nullptr))
                == nullptr)
            {
                reporter(
                    &hl_pair.second.hc_regex,
                    fmt::format(FMT_STRING("invalid highlight regex: {} at {}"),
                                errptr,
                                eoff));
                continue;
            }

            const auto& sc = hl_pair.second.hc_style;
            std::string fg1, bg1, fg_color, bg_color, errmsg;
            bool invalid = false;
            int attrs = 0;

            fg1 = sc.sc_color;
            bg1 = sc.sc_background_color;
            shlex(fg1).eval(fg_color, vars);
            shlex(bg1).eval(bg_color, vars);

            auto fg = styling::color_unit::from_str(fg_color).unwrapOrElse(
                [&](const auto& msg) {
                    reporter(&sc.sc_color, errmsg);
                    invalid = true;
                    return styling::color_unit::make_empty();
                });
            auto bg = styling::color_unit::from_str(bg_color).unwrapOrElse(
                [&](const auto& msg) {
                    reporter(&sc.sc_background_color, errmsg);
                    invalid = true;
                    return styling::color_unit::make_empty();
                });
            if (invalid) {
                continue;
            }

            if (sc.sc_bold) {
                attrs |= A_BOLD;
            }
            if (sc.sc_underline) {
                attrs |= A_UNDERLINE;
            }
            this->tc_highlights[{highlight_source_t::THEME, hl_pair.first}]
                = highlighter(code)
                      .with_pattern(hl_pair.second.hc_regex)
                      .with_attrs(attrs != 0 ? attrs : -1)
                      .with_color(fg, bg);
        }
    }
}

void
textview_curses::reload_data()
{
    if (this->tc_sub_source != nullptr) {
        this->tc_sub_source->text_update_marks(this->tc_bookmarks);
    }
    listview_curses::reload_data();

    if (this->tc_sub_source != nullptr) {
        auto ttt = dynamic_cast<text_time_translator*>(this->tc_sub_source);

        if (ttt != nullptr) {
            ttt->data_reloaded(this);
        }
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
            search_bv.erase(pair.first, pair.second);
        }
    }

    listview_curses::reload_data();
}

void
textview_curses::grep_end_batch(grep_proc<vis_line_t>& gp)
{
    if (this->tc_follow_deadline.tv_sec
        && this->tc_follow_top == this->get_top()) {
        struct timeval now;

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

    ensure(this->tc_searching >= 0);
}

void
textview_curses::grep_match(grep_proc<vis_line_t>& gp,
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

    if (this->tc_selection_start == -1_vl && listview_curses::handle_mouse(me))
    {
        return true;
    }

    if (this->tc_delegate != nullptr
        && this->tc_delegate->text_handle_mouse(*this, me))
    {
        return true;
    }

    if (me.me_button != mouse_button_t::BUTTON_LEFT) {
        return false;
    }

    vis_line_t mouse_line(this->get_top() + me.me_y);

    if (mouse_line > this->get_bottom()) {
        mouse_line = this->get_bottom();
    }

    this->get_dimensions(height, width);

    switch (me.me_state) {
        case mouse_button_state_t::BUTTON_STATE_PRESSED:
            this->tc_selection_start = mouse_line;
            this->tc_selection_last = -1_vl;
            this->tc_selection_cleared = false;
            break;
        case mouse_button_state_t::BUTTON_STATE_DRAGGED:
            if (me.me_y <= 0) {
                this->shift_top(-1_vl);
                me.me_y = 0;
                mouse_line = this->get_top();
            }
            if (me.me_y >= height
                && this->get_top() < this->get_top_for_last_row()) {
                this->shift_top(1_vl);
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
                this->tc_selection_last = -1_vl;
            } else {
                if (!this->tc_selection_cleared) {
                    if (this->tc_sub_source != nullptr) {
                        this->tc_sub_source->text_clear_marks(&BM_USER);
                    }
                    this->tc_bookmarks[&BM_USER].clear();

                    this->tc_selection_cleared = true;
                }
                this->toggle_user_mark(
                    &BM_USER, this->tc_selection_start, mouse_line);
                this->tc_selection_last = mouse_line;
            }
            this->reload_data();
            break;
        case mouse_button_state_t::BUTTON_STATE_RELEASED:
            this->tc_selection_start = -1_vl;
            this->tc_selection_last = -1_vl;
            this->tc_selection_cleared = false;
            break;
    }

    return true;
}

void
textview_curses::textview_value_for_row(vis_line_t row, attr_line_t& value_out)
{
    auto& sa = value_out.get_attrs();
    auto& str = value_out.get_string();
    auto source_format = this->tc_sub_source->get_text_format();
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

    for (auto& tc_highlight : this->tc_highlights) {
        bool internal_hl
            = tc_highlight.first.first == highlight_source_t::INTERNAL
            || tc_highlight.first.first == highlight_source_t::THEME;

        if (!tc_highlight.second.h_text_formats.empty()
            && tc_highlight.second.h_text_formats.count(source_format) == 0)
        {
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
        // highlights should apply only to the line itself and not any of the
        // surrounding decorations that are added (for example, the file lines
        // that are inserted at the beginning of the log view).
        int start_pos = internal_hl ? body.lr_start : orig_line.lr_start;
        tc_highlight.second.annotate(value_out, start_pos);
    }

    if (this->tc_hide_fields) {
        value_out.apply_hide();
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

    const auto& user_marks = this->tc_bookmarks[&BM_USER];
    const auto& user_expr_marks = this->tc_bookmarks[&BM_USER_EXPR];
    if (binary_search(user_marks.begin(), user_marks.end(), row)
        || binary_search(user_expr_marks.begin(), user_expr_marks.end(), row))
    {
        sa.emplace_back(line_range{orig_line.lr_start, -1},
                        &view_curses::VC_STYLE,
                        A_REVERSE);
    }
}

void
textview_curses::execute_search(const std::string& regex_orig)
{
    std::string regex = regex_orig;
    pcre* code = nullptr;

    if ((this->tc_search_child == nullptr)
        || (regex != this->tc_current_search)) {
        const char* errptr;
        int eoff;

        this->tc_previous_search = this->tc_current_search;
        this->match_reset();

        this->tc_search_child.reset();
        this->tc_source_search_child.reset();

        log_debug("start search for: '%s'", regex.c_str());

        if (regex.empty()) {
        } else if ((code = pcre_compile(
                        regex.c_str(), PCRE_CASELESS, &errptr, &eoff, nullptr))
                   == nullptr)
        {
            auto errmsg = std::string(errptr);

            regex = pcrepp::quote(regex);

            log_info("invalid search regex, using quoted: %s", regex.c_str());
            if ((code = pcre_compile(
                     regex.c_str(), PCRE_CASELESS, &errptr, &eoff, nullptr))
                == nullptr)
            {
                log_error("Unable to compile quoted regex: %s", regex.c_str());
            }
        }

        if (code != nullptr) {
            highlighter hl(code);

            hl.with_role(view_colors::VCR_SEARCH);

            highlight_map_t& hm = this->get_highlights();
            hm[{highlight_source_t::PREVIEW, "search"}] = hl;

            auto gp = std::make_unique<grep_proc<vis_line_t>>(code, *this);

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
            gp->start();

            this->tc_search_child = std::make_unique<grep_highlighter>(
                gp, highlight_source_t::PREVIEW, "search", hm);

            if (this->tc_sub_source != nullptr) {
                this->tc_sub_source->get_grepper() | [this, code](auto pair) {
                    auto sgp = std::make_shared<grep_proc<vis_line_t>>(
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

void
textview_curses::horiz_shift(vis_line_t start,
                             vis_line_t end,
                             int off_start,
                             std::pair<int, int>& range_out)
{
    highlighter& hl
        = this->tc_highlights[{highlight_source_t::PREVIEW, "search"}];
    int prev_hit = -1, next_hit = INT_MAX;

    for (; start < end; ++start) {
        std::vector<attr_line_t> rows(1);
        int off;

        this->listview_value_for_rows(*this, start, rows);

        const std::string& str = rows[0].get_string();
        for (off = 0; off < (int) str.size();) {
            int rc, matches[128];

            rc = pcre_exec(hl.h_code,
                           hl.h_code_extra,
                           str.c_str(),
                           str.size(),
                           off,
                           0,
                           matches,
                           128);
            if (rc > 0) {
                struct line_range lr;

                if (rc == 2) {
                    lr.lr_start = matches[2];
                    lr.lr_end = matches[3];
                } else {
                    lr.lr_start = matches[0];
                    lr.lr_end = matches[1];
                }

                if (lr.lr_start < off_start) {
                    prev_hit = std::max(prev_hit, lr.lr_start);
                } else if (lr.lr_start > off_start) {
                    next_hit = std::min(next_hit, lr.lr_start);
                }
                if (lr.lr_end > lr.lr_start) {
                    off = matches[1];
                } else {
                    off += 1;
                }
            } else {
                off = str.size();
            }
        }
    }

    range_out = std::make_pair(prev_hit, next_hit);
}

void
textview_curses::set_user_mark(const bookmark_type_t* bm,
                               vis_line_t vl,
                               bool marked)
{
    bookmark_vector<vis_line_t>& bv = this->tc_bookmarks[bm];
    bookmark_vector<vis_line_t>::iterator iter;

    if (marked) {
        bv.insert_once(vl);
    } else {
        iter = std::lower_bound(bv.begin(), bv.end(), vl);
        if (iter != bv.end() && *iter == vl) {
            bv.erase(iter);
        }
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
    for (vis_line_t curr_line = start_line; curr_line <= end_line; ++curr_line)
    {
        bookmark_vector<vis_line_t>& bv = this->tc_bookmarks[bm];
        bookmark_vector<vis_line_t>::iterator iter;
        bool added;

        iter = bv.insert_once(curr_line);
        if (iter == bv.end()) {
            added = true;
        } else {
            bv.erase(iter);
            added = false;
        }
        if (this->tc_sub_source) {
            this->tc_sub_source->text_mark(bm, curr_line, added);
        }
    }
    this->search_range(start_line, end_line + 1_vl);
    this->search_new_data();
}

void
text_time_translator::scroll_invoked(textview_curses* tc)
{
    if (tc->get_inner_height() > 0) {
        this->time_for_row(tc->get_top()) |
            [this](auto new_top_time) { this->ttt_top_time = new_top_time; };
    }
}

void
text_time_translator::data_reloaded(textview_curses* tc)
{
    if (tc->get_inner_height() > 0) {
        this->time_for_row(tc->get_top()) | [this, tc](auto top_time) {
            if (top_time != this->ttt_top_time) {
                if (this->ttt_top_time.tv_sec != 0) {
                    this->row_for_time(this->ttt_top_time) |
                        [tc](auto new_top) { tc->set_top(new_top); };
                }
                this->time_for_row(tc->get_top()) | [this](auto new_top_time) {
                    this->ttt_top_time = new_top_time;
                };
            }
        };
    }
}

template class bookmark_vector<vis_line_t>;

bool
empty_filter::matches(const logfile& lf,
                      logfile::const_iterator ll,
                      shared_buffer_ref& line)
{
    return false;
}

std::string
empty_filter::to_command() const
{
    return "";
}
