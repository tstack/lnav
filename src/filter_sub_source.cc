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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "filter_sub_source.hh"

#include "base/enum_util.hh"
#include "base/func_util.hh"
#include "base/opt_util.hh"
#include "bound_tags.hh"
#include "config.h"
#include "lnav.hh"
#include "readline_highlighters.hh"
#include "readline_possibilities.hh"
#include "sql_util.hh"

using namespace lnav::roles::literals;

filter_sub_source::filter_sub_source(std::shared_ptr<readline_curses> editor)
    : fss_editor(editor)
{
    this->fss_editor->set_left(25);
    this->fss_editor->set_width(-1);
    this->fss_editor->set_save_history(!(lnav_data.ld_flags & LNF_SECURE_MODE));
    this->fss_regex_context.set_highlighter(readline_regex_highlighter)
        .set_append_character(0);
    this->fss_editor->add_context(filter_lang_t::REGEX,
                                  this->fss_regex_context);
    this->fss_sql_context.set_highlighter(readline_sqlite_highlighter)
        .set_append_character(0);
    this->fss_editor->add_context(filter_lang_t::SQL, this->fss_sql_context);
    this->fss_editor->set_change_action(
        bind_mem(&filter_sub_source::rl_change, this));
    this->fss_editor->set_perform_action(
        bind_mem(&filter_sub_source::rl_perform, this));
    this->fss_editor->set_abort_action(
        bind_mem(&filter_sub_source::rl_abort, this));
    this->fss_editor->set_display_match_action(
        bind_mem(&filter_sub_source::rl_display_matches, this));
    this->fss_editor->set_display_next_action(
        bind_mem(&filter_sub_source::rl_display_next, this));
    this->fss_match_view.set_sub_source(&this->fss_match_source);
    this->fss_match_view.set_height(0_vl);
    this->fss_match_view.set_show_scrollbar(true);
    this->fss_match_view.set_default_role(role_t::VCR_POPUP);
}

bool
filter_sub_source::list_input_handle_key(listview_curses& lv, int ch)
{
    if (this->fss_editing) {
        switch (ch) {
            case KEY_ESCAPE:
            case KEY_CTRL(']'):
                this->fss_editor->abort();
                return true;
            default:
                this->fss_editor->handle_key(ch);
                return true;
        }
    }

    switch (ch) {
        case 'f': {
            auto* top_view = *lnav_data.ld_view_stack.top();
            auto* tss = top_view->get_sub_source();

            tss->toggle_apply_filters();
            top_view->reload_data();
            break;
        }
        case ' ': {
            textview_curses* top_view = *lnav_data.ld_view_stack.top();
            text_sub_source* tss = top_view->get_sub_source();
            filter_stack& fs = tss->get_filters();

            if (fs.empty()) {
                return true;
            }

            auto tf = *(fs.begin() + lv.get_selection());

            fs.set_filter_enabled(tf, !tf->is_enabled());
            tss->text_filters_changed();
            lv.reload_data();
            top_view->reload_data();
            return true;
        }
        case 't': {
            textview_curses* top_view = *lnav_data.ld_view_stack.top();
            text_sub_source* tss = top_view->get_sub_source();
            filter_stack& fs = tss->get_filters();

            if (fs.empty()) {
                return true;
            }

            auto tf = *(fs.begin() + lv.get_selection());

            if (tf->get_type() == text_filter::INCLUDE) {
                tf->set_type(text_filter::EXCLUDE);
            } else {
                tf->set_type(text_filter::INCLUDE);
            }

            tss->text_filters_changed();
            lv.reload_data();
            top_view->reload_data();
            return true;
        }
        case 'D': {
            textview_curses* top_view = *lnav_data.ld_view_stack.top();
            text_sub_source* tss = top_view->get_sub_source();
            filter_stack& fs = tss->get_filters();

            if (fs.empty()) {
                return true;
            }

            auto tf = *(fs.begin() + lv.get_selection());

            fs.delete_filter(tf->get_id());
            lv.reload_data();
            tss->text_filters_changed();
            top_view->reload_data();
            return true;
        }
        case 'i': {
            auto* top_view = *lnav_data.ld_view_stack.top();
            auto* tss = top_view->get_sub_source();
            auto& fs = tss->get_filters();
            auto filter_index = fs.next_index();

            if (!filter_index) {
                lnav_data.ld_filter_help_status_source.fss_error.set_value(
                    "error: too many filters");
                return true;
            }

            auto ef = std::make_shared<empty_filter>(
                text_filter::type_t::INCLUDE, *filter_index);
            fs.add_filter(ef);
            lv.set_selection(vis_line_t(fs.size() - 1));
            lv.reload_data();

            this->fss_editing = true;

            add_view_text_possibilities(this->fss_editor.get(),
                                        filter_lang_t::REGEX,
                                        "*",
                                        top_view,
                                        text_quoting::regex);
            this->fss_editor->set_window(lv.get_window());
            this->fss_editor->set_visible(true);
            this->fss_editor->set_y(
                lv.get_y() + (int) (lv.get_selection() - lv.get_top()));
            this->fss_editor->window_change();
            this->fss_editor->focus(filter_lang_t::REGEX, "", "");
            this->fss_filter_state = true;
            ef->disable();
            return true;
        }
        case 'o': {
            auto* top_view = *lnav_data.ld_view_stack.top();
            auto* tss = top_view->get_sub_source();
            auto& fs = tss->get_filters();
            auto filter_index = fs.next_index();

            if (!filter_index) {
                lnav_data.ld_filter_help_status_source.fss_error.set_value(
                    "error: too many filters");
                return true;
            }

            auto ef = std::make_shared<empty_filter>(
                text_filter::type_t::EXCLUDE, *filter_index);
            fs.add_filter(ef);
            lv.set_selection(vis_line_t(fs.size() - 1));
            lv.reload_data();

            this->fss_editing = true;

            add_view_text_possibilities(this->fss_editor.get(),
                                        filter_lang_t::REGEX,
                                        "*",
                                        top_view,
                                        text_quoting::regex);
            this->fss_editor->set_window(lv.get_window());
            this->fss_editor->set_visible(true);
            this->fss_editor->set_y(
                lv.get_y() + (int) (lv.get_selection() - lv.get_top()));
            this->fss_editor->window_change();
            this->fss_editor->focus(filter_lang_t::REGEX, "", "");
            this->fss_filter_state = true;
            ef->disable();
            return true;
        }
        case '\r':
        case KEY_ENTER: {
            textview_curses* top_view = *lnav_data.ld_view_stack.top();
            text_sub_source* tss = top_view->get_sub_source();
            filter_stack& fs = tss->get_filters();

            if (fs.empty()) {
                return true;
            }

            auto tf = *(fs.begin() + lv.get_selection());

            this->fss_editing = true;

            auto tq = tf->get_lang() == filter_lang_t::SQL
                ? text_quoting::sql
                : text_quoting::regex;
            add_view_text_possibilities(
                this->fss_editor.get(), tf->get_lang(), "*", top_view, tq);
            if (top_view == &lnav_data.ld_views[LNV_LOG]) {
                add_filter_expr_possibilities(
                    this->fss_editor.get(), filter_lang_t::SQL, "*");
            }
            this->fss_editor->set_window(lv.get_window());
            this->fss_editor->set_visible(true);
            this->fss_editor->set_y(
                lv.get_y() + (int) (lv.get_selection() - lv.get_top()));
            this->fss_editor->focus(tf->get_lang(), "");
            this->fss_editor->rewrite_line(0, tf->get_id().c_str());
            this->fss_filter_state = tf->is_enabled();
            tf->disable();
            tss->text_filters_changed();
            return true;
        }
        case 'n': {
            execute_command(lnav_data.ld_exec_context, "next-mark search");
            return true;
        }
        case 'N': {
            execute_command(lnav_data.ld_exec_context, "prev-mark search");
            return true;
        }
        case '/': {
            execute_command(lnav_data.ld_exec_context, "prompt search-filters");
            return true;
        }
        default:
            log_debug("unhandled %x", ch);
            break;
    }

    return false;
}

size_t
filter_sub_source::text_line_count()
{
    return (lnav_data.ld_view_stack.top() |
                [](auto tc) -> nonstd::optional<size_t> {
               text_sub_source* tss = tc->get_sub_source();

               if (tss == nullptr) {
                   return nonstd::nullopt;
               }
               auto& fs = tss->get_filters();
               return nonstd::make_optional(fs.size());
           })
        .value_or(0);
}

size_t
filter_sub_source::text_line_width(textview_curses& curses)
{
    textview_curses* top_view = *lnav_data.ld_view_stack.top();
    text_sub_source* tss = top_view->get_sub_source();
    filter_stack& fs = tss->get_filters();
    size_t retval = 0;

    for (auto& filter : fs) {
        retval = std::max(filter->get_id().size() + 8, retval);
    }

    return retval;
}

void
filter_sub_source::text_value_for_line(textview_curses& tc,
                                       int line,
                                       std::string& value_out,
                                       text_sub_source::line_flags_t flags)
{
    auto* top_view = *lnav_data.ld_view_stack.top();
    auto* tss = top_view->get_sub_source();
    auto& fs = tss->get_filters();
    auto tf = *(fs.begin() + line);

    value_out = "    ";
    switch (tf->get_type()) {
        case text_filter::INCLUDE:
            value_out.append(" IN ");
            break;
        case text_filter::EXCLUDE:
            if (tf->get_lang() == filter_lang_t::REGEX) {
                value_out.append("OUT ");
            } else {
                value_out.append("    ");
            }
            break;
        default:
            ensure(0);
            break;
    }

    if (this->fss_editing && line == tc.get_selection()) {
        fmt::format_to(
            std::back_inserter(value_out), FMT_STRING("{:>9} hits | "), "-");
    } else {
        fmt::format_to(std::back_inserter(value_out),
                       FMT_STRING("{:>9L} hits | "),
                       tss->get_filtered_count_for(tf->get_index()));
    }

    value_out.append(tf->get_id());
}

void
filter_sub_source::text_attrs_for_line(textview_curses& tc,
                                       int line,
                                       string_attrs_t& value_out)
{
    textview_curses* top_view = *lnav_data.ld_view_stack.top();
    text_sub_source* tss = top_view->get_sub_source();
    filter_stack& fs = tss->get_filters();
    auto tf = *(fs.begin() + line);
    bool selected
        = lnav_data.ld_mode == ln_mode_t::FILTER && line == tc.get_selection();

    if (selected) {
        value_out.emplace_back(line_range{0, 1}, VC_GRAPHIC.value(ACS_RARROW));
    }

    chtype enabled = tf->is_enabled() ? ACS_DIAMOND : ' ';

    line_range lr{2, 3};
    value_out.emplace_back(lr, VC_GRAPHIC.value(enabled));
    if (tf->is_enabled()) {
        value_out.emplace_back(lr, VC_FOREGROUND.value(COLOR_GREEN));
    }

    if (selected) {
        value_out.emplace_back(line_range{0, -1},
                               VC_ROLE.value(role_t::VCR_FOCUSED));
    }

    role_t fg_role = tf->get_type() == text_filter::INCLUDE ? role_t::VCR_OK
                                                            : role_t::VCR_ERROR;
    value_out.emplace_back(line_range{4, 7}, VC_ROLE.value(fg_role));
    value_out.emplace_back(line_range{4, 7},
                           VC_STYLE.value(text_attrs{A_BOLD}));

    value_out.emplace_back(line_range{8, 17},
                           VC_STYLE.value(text_attrs{A_BOLD}));
    value_out.emplace_back(line_range{23, 24}, VC_GRAPHIC.value(ACS_VLINE));

    attr_line_t content{tf->get_id()};
    auto& content_attrs = content.get_attrs();

    switch (tf->get_lang()) {
        case filter_lang_t::REGEX:
            readline_regex_highlighter(content, content.length());
            break;
        case filter_lang_t::SQL:
            readline_sqlite_highlighter(content, content.length());
            break;
        case filter_lang_t::NONE:
            break;
    }

    shift_string_attrs(content_attrs, 0, 25);
    value_out.insert(
        value_out.end(), content_attrs.begin(), content_attrs.end());
}

size_t
filter_sub_source::text_size_for_line(textview_curses& tc,
                                      int line,
                                      text_sub_source::line_flags_t raw)
{
    textview_curses* top_view = *lnav_data.ld_view_stack.top();
    text_sub_source* tss = top_view->get_sub_source();
    filter_stack& fs = tss->get_filters();
    auto tf = *(fs.begin() + line);

    return 8 + tf->get_id().size();
}

void
filter_sub_source::rl_change(readline_curses* rc)
{
    textview_curses* top_view = *lnav_data.ld_view_stack.top();
    text_sub_source* tss = top_view->get_sub_source();
    filter_stack& fs = tss->get_filters();
    if (fs.empty()) {
        return;
    }

    auto iter = fs.begin() + this->tss_view->get_selection();
    auto tf = *iter;
    auto new_value = rc->get_line_buffer();

    switch (tf->get_lang()) {
        case filter_lang_t::NONE:
            break;
        case filter_lang_t::REGEX: {
            if (new_value.empty()) {
                if (fs.get_filter(top_view->get_current_search()) == nullptr) {
                    this->fss_editor->set_suggestion(
                        top_view->get_current_search());
                }
            } else {
                auto regex_res
                    = lnav::pcre2pp::code::from(new_value, PCRE2_CASELESS);

                if (regex_res.isErr()) {
                    auto pe = regex_res.unwrapErr();
                    lnav_data.ld_filter_help_status_source.fss_error.set_value(
                        "error: %s", pe.get_message().c_str());
                } else {
                    auto& hm = top_view->get_highlights();
                    highlighter hl(regex_res.unwrap().to_shared());
                    auto role = tf->get_type() == text_filter::EXCLUDE
                        ? role_t::VCR_DIFF_DELETE
                        : role_t::VCR_DIFF_ADD;
                    hl.with_role(role);
                    hl.with_attrs(text_attrs{A_BLINK | A_REVERSE});

                    hm[{highlight_source_t::PREVIEW, "preview"}] = hl;
                    top_view->set_needs_update();
                    lnav_data.ld_filter_help_status_source.fss_error.clear();
                }
            }
            break;
        }
        case filter_lang_t::SQL: {
            auto full_sql
                = fmt::format(FMT_STRING("SELECT 1 WHERE {}"), new_value);
            auto_mem<sqlite3_stmt> stmt(sqlite3_finalize);
#ifdef SQLITE_PREPARE_PERSISTENT
            auto retcode = sqlite3_prepare_v3(lnav_data.ld_db.in(),
                                              full_sql.c_str(),
                                              full_sql.size(),
                                              SQLITE_PREPARE_PERSISTENT,
                                              stmt.out(),
                                              nullptr);
#else
            auto retcode = sqlite3_prepare_v2(lnav_data.ld_db.in(),
                                              full_sql.c_str(),
                                              full_sql.size(),
                                              stmt.out(),
                                              nullptr);
#endif
            if (retcode != SQLITE_OK) {
                lnav_data.ld_filter_help_status_source.fss_error.set_value(
                    "error: %s", sqlite3_errmsg(lnav_data.ld_db));
            } else {
                auto set_res = lnav_data.ld_log_source.set_preview_sql_filter(
                    stmt.release());

                if (set_res.isErr()) {
                    lnav_data.ld_filter_help_status_source.fss_error.set_value(
                        "error: %s",
                        set_res.unwrapErr()
                            .to_attr_line()
                            .get_string()
                            .c_str());
                } else {
                    top_view->set_needs_update();
                    lnav_data.ld_filter_help_status_source.fss_error.clear();
                }
            }
            break;
        }
    }
}

void
filter_sub_source::rl_perform(readline_curses* rc)
{
    static const intern_string_t INPUT_SRC = intern_string::lookup("input");

    textview_curses* top_view = *lnav_data.ld_view_stack.top();
    text_sub_source* tss = top_view->get_sub_source();
    filter_stack& fs = tss->get_filters();
    auto iter = fs.begin() + this->tss_view->get_selection();
    auto tf = *iter;
    auto new_value = rc->get_value().get_string();

    if (new_value.empty()) {
        this->rl_abort(rc);
    } else {
        top_view->get_highlights().erase(
            {highlight_source_t::PREVIEW, "preview"});
        switch (tf->get_lang()) {
            case filter_lang_t::NONE:
            case filter_lang_t::REGEX: {
                auto compile_res
                    = lnav::pcre2pp::code::from(new_value, PCRE2_CASELESS);

                if (compile_res.isErr()) {
                    auto ce = compile_res.unwrapErr();
                    auto um = lnav::console::to_user_message(INPUT_SRC, ce);
                    lnav_data.ld_exec_context.ec_error_callback_stack.back()(
                        um);
                    this->rl_abort(rc);
                } else {
                    tf->lf_deleted = true;
                    tss->text_filters_changed();

                    auto pf = std::make_shared<pcre_filter>(
                        tf->get_type(),
                        new_value,
                        tf->get_index(),
                        compile_res.unwrap().to_shared());

                    *iter = pf;
                    tss->text_filters_changed();
                }
                break;
            }
            case filter_lang_t::SQL: {
                auto full_sql
                    = fmt::format(FMT_STRING("SELECT 1 WHERE {}"), new_value);
                auto_mem<sqlite3_stmt> stmt(sqlite3_finalize);
#ifdef SQLITE_PREPARE_PERSISTENT
                auto retcode = sqlite3_prepare_v3(lnav_data.ld_db.in(),
                                                  full_sql.c_str(),
                                                  full_sql.size(),
                                                  SQLITE_PREPARE_PERSISTENT,
                                                  stmt.out(),
                                                  nullptr);
#else
                auto retcode = sqlite3_prepare_v2(lnav_data.ld_db.in(),
                                                  full_sql.c_str(),
                                                  full_sql.size(),
                                                  stmt.out(),
                                                  nullptr);
#endif
                if (retcode != SQLITE_OK) {
                    auto sqlerr = annotate_sql_with_error(
                        lnav_data.ld_db.in(), full_sql.c_str(), nullptr);
                    auto um
                        = lnav::console::user_message::error(
                              "invalid SQL expression")
                              .with_reason(sqlite3_errmsg(lnav_data.ld_db.in()))
                              .with_snippet(lnav::console::snippet::from(
                                  INPUT_SRC, sqlerr));
                    lnav_data.ld_exec_context.ec_error_callback_stack.back()(
                        um);
                    this->rl_abort(rc);
                } else {
                    lnav_data.ld_log_source.set_sql_filter(new_value,
                                                           stmt.release());
                    tss->text_filters_changed();
                }
                break;
            }
        }
    }

    lnav_data.ld_log_source.set_preview_sql_filter(nullptr);
    lnav_data.ld_filter_help_status_source.fss_prompt.clear();
    this->fss_editing = false;
    this->fss_editor->set_visible(false);
    top_view->reload_data();
    this->tss_view->reload_data();
}

void
filter_sub_source::rl_abort(readline_curses* rc)
{
    textview_curses* top_view = *lnav_data.ld_view_stack.top();
    text_sub_source* tss = top_view->get_sub_source();
    filter_stack& fs = tss->get_filters();
    auto iter = fs.begin() + this->tss_view->get_selection();
    auto tf = *iter;

    lnav_data.ld_log_source.set_preview_sql_filter(nullptr);
    lnav_data.ld_filter_help_status_source.fss_prompt.clear();
    lnav_data.ld_filter_help_status_source.fss_error.clear();
    top_view->get_highlights().erase({highlight_source_t::PREVIEW, "preview"});
    top_view->reload_data();
    fs.delete_filter("");
    this->tss_view->reload_data();
    this->fss_editor->set_visible(false);
    this->fss_editing = false;
    this->tss_view->set_needs_update();
    tf->set_enabled(this->fss_filter_state);
    tss->text_filters_changed();
    this->tss_view->reload_data();
}

void
filter_sub_source::rl_display_matches(readline_curses* rc)
{
    const std::vector<std::string>& matches = rc->get_matches();
    unsigned long width = 0;

    if (matches.empty()) {
        this->fss_match_source.clear();
        this->fss_match_view.set_height(0_vl);
        this->tss_view->set_needs_update();
    } else {
        auto current_match = rc->get_match_string();
        attr_line_t al;
        vis_line_t line, selected_line;

        for (const auto& match : matches) {
            if (match == current_match) {
                al.append(match, VC_STYLE.value(text_attrs{A_REVERSE}));
                selected_line = line;
            } else {
                al.append(match);
            }
            al.append(1, '\n');
            width = std::max(width, (unsigned long) match.size());
            line += 1_vl;
        }

        this->fss_match_view.set_selection(selected_line);
        this->fss_match_source.replace_with(al);
        this->fss_match_view.set_height(
            std::min(vis_line_t(matches.size()), 3_vl));
    }

    this->fss_match_view.set_window(this->tss_view->get_window());
    this->fss_match_view.set_y(rc->get_y() + 1);
    this->fss_match_view.set_x(rc->get_left() + rc->get_match_start());
    this->fss_match_view.set_width(width + 3);
    this->fss_match_view.set_needs_update();
    this->fss_match_view.reload_data();
}

void
filter_sub_source::rl_display_next(readline_curses* rc)
{
    textview_curses& tc = this->fss_match_view;

    if (tc.get_top() >= (tc.get_top_for_last_row() - 1)) {
        tc.set_top(0_vl);
    } else {
        tc.shift_top(tc.get_height());
    }
}

void
filter_sub_source::list_input_handle_scroll_out(listview_curses& lv)
{
    lnav_data.ld_mode = ln_mode_t::PAGING;
    lnav_data.ld_filter_view.reload_data();
}
