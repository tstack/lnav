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
#include "base/injector.hh"
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

            auto regex = lnav::pcre2pp::code::from(hl_pair.second.hc_regex);

            if (regex.isErr()) {
                const static intern_string_t PATTERN_SRC
                    = intern_string::lookup("pattern");

                auto ce = regex.unwrapErr();
                reporter(&hl_pair.second.hc_regex,
                         lnav::console::to_user_message(PATTERN_SRC, ce));
                continue;
            }

            const auto& sc = hl_pair.second.hc_style;
            std::string fg1, bg1, fg_color, bg_color, errmsg;
            bool invalid = false;
            text_attrs attrs;

            fg1 = sc.sc_color;
            bg1 = sc.sc_background_color;
            shlex(fg1).eval(fg_color, vars);
            shlex(bg1).eval(bg_color, vars);

            auto fg = styling::color_unit::from_str(fg_color).unwrapOrElse(
                [&](const auto& msg) {
                    reporter(&sc.sc_color,
                             lnav::console::user_message::error(
                                 attr_line_t("invalid color -- ")
                                     .append_quoted(sc.sc_color))
                                 .with_reason(msg));
                    invalid = true;
                    return styling::color_unit::make_empty();
                });
            auto bg = styling::color_unit::from_str(bg_color).unwrapOrElse(
                [&](const auto& msg) {
                    reporter(&sc.sc_background_color,
                             lnav::console::user_message::error(
                                 attr_line_t("invalid background color -- ")
                                     .append_quoted(sc.sc_background_color))
                                 .with_reason(msg));
                    invalid = true;
                    return styling::color_unit::make_empty();
                });
            if (invalid) {
                continue;
            }

            if (sc.sc_bold) {
                attrs.ta_attrs |= A_BOLD;
            }
            if (sc.sc_underline) {
                attrs.ta_attrs |= A_UNDERLINE;
            }
            this->tc_highlights[{highlight_source_t::THEME, hl_pair.first}]
                = highlighter(regex.unwrap().to_shared())
                      .with_attrs(attrs)
                      .with_color(fg, bg)
                      .with_nestable(false);
        }
    }
}

void
textview_curses::reload_data()
{
    if (this->tc_sub_source != nullptr) {
        this->tc_sub_source->text_update_marks(this->tc_bookmarks);
    }
    if (this->tc_sub_source != nullptr) {
        auto* ttt = dynamic_cast<text_time_translator*>(this->tc_sub_source);

        if (ttt != nullptr) {
            ttt->data_reloaded(this);
        }
    }
    listview_curses::reload_data();
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
        && this->tc_follow_top == this->get_top())
    {
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
                && this->get_top() < this->get_top_for_last_row())
            {
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

    scrub_ansi_string(str, &sa);

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

    auto format_attr_opt = get_string_attr(sa, SA_FORMAT);
    if (format_attr_opt) {
        format_name = format_attr_opt.value().get();
    }

    if (this->is_selectable() && row == this->get_selection()
        && this->tc_cursor_role)
    {
        sa.emplace_back(line_range{orig_line.lr_start, -1},
                        VC_ROLE.value(this->tc_cursor_role.value()));
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
    typedef std::map<std::string, role_t> key_map_t;
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
                        VC_STYLE.value(text_attrs{A_REVERSE}));
    }
}

void
textview_curses::execute_search(const std::string& regex_orig)
{
    std::string regex = regex_orig;
    std::shared_ptr<lnav::pcre2pp::code> code;

    if ((this->tc_search_child == nullptr)
        || (regex != this->tc_current_search))
    {
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

nonstd::optional<std::pair<int, int>>
textview_curses::horiz_shift(vis_line_t start, vis_line_t end, int off_start)
{
    auto hl_iter
        = this->tc_highlights.find({highlight_source_t::PREVIEW, "search"});
    if (hl_iter == this->tc_highlights.end()
        || hl_iter->second.h_regex == nullptr)
    {
        return nonstd::nullopt;
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
        return nonstd::nullopt;
    }
    return std::make_pair(prev_hit, next_hit);
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

bool
textview_curses::grep_value_for_line(vis_line_t line, std::string& value_out)
{
    bool retval = false;

    if (this->tc_sub_source
        && line < (int) this->tc_sub_source->text_line_count())
    {
        this->tc_sub_source->text_value_for_line(
            *this, line, value_out, text_sub_source::RF_RAW);
        scrub_ansi_string(value_out, nullptr);
        retval = true;
    }

    return retval;
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
    if (tc->get_inner_height() == 0) {
        return;
    }
    if (tc->get_top() > tc->get_inner_height()) {
        if (this->ttt_top_time.tv_sec != 0) {
            this->row_for_time(this->ttt_top_time) |
                [tc](auto new_top) { tc->set_top(new_top); };
        }
        return;
    }
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

nonstd::optional<size_t>
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
    return nonstd::nullopt;
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

nonstd::optional<vis_line_t>
vis_location_history::loc_history_back(vis_line_t current_top)
{
    if (this->lh_history_position == 0) {
        vis_line_t history_top = this->current_position();
        if (history_top != current_top) {
            return history_top;
        }
    }

    if (this->lh_history_position + 1 >= this->vlh_history.size()) {
        return nonstd::nullopt;
    }

    this->lh_history_position += 1;

    return this->current_position();
}

nonstd::optional<vis_line_t>
vis_location_history::loc_history_forward(vis_line_t current_top)
{
    if (this->lh_history_position == 0) {
        return nonstd::nullopt;
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

nonstd::optional<size_t>
logfile_filter_state::content_line_to_vis_line(uint32_t line)
{
    if (this->tfs_index.empty()) {
        return nonstd::nullopt;
    }

    auto iter = std::lower_bound(
        this->tfs_index.begin(), this->tfs_index.end(), line);

    if (iter == this->tfs_index.end() || *iter != line) {
        return nonstd::nullopt;
    }

    return nonstd::make_optional(std::distance(this->tfs_index.begin(), iter));
}

std::string
text_anchors::to_anchor_string(const std::string& raw)
{
    static const auto ANCHOR_RE = lnav::pcre2pp::code::from_const(R"([^\w]+)");

    return fmt::format(FMT_STRING("#{}"), ANCHOR_RE.replace(tolower(raw), "-"));
}
