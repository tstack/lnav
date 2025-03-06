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

#include <memory>
#include <utility>

#include "filter_sub_source.hh"

#include "base/attr_line.builder.hh"
#include "base/auto_mem.hh"
#include "base/func_util.hh"
#include "base/itertools.hh"
#include "base/opt_util.hh"
#include "cmd.parser.hh"
#include "config.h"
#include "data_scanner.hh"
#include "lnav.hh"
#include "lnav.prompt.hh"
#include "readline_highlighters.hh"
#include "readline_possibilities.hh"
#include "sql_util.hh"
#include "textinput_curses.hh"

using namespace lnav::roles::literals;

filter_sub_source::filter_sub_source(std::shared_ptr<textinput_curses> editor)
    : fss_editor(std::move(editor)),
      fss_regexp_history(
          lnav::textinput::history::for_context("regexp-filter"_frag)),
      fss_sql_history(lnav::textinput::history::for_context("sql-filter"_frag))
{
    this->fss_editor->set_visible(false);
    this->fss_editor->set_x(25);
    this->fss_editor->tc_popup.set_title("Pattern");
    this->fss_editor->tc_height = 1;
    this->fss_editor->tc_on_change
        = bind_mem(&filter_sub_source::rl_change, this);
    this->fss_editor->tc_on_history
        = bind_mem(&filter_sub_source::rl_history, this);
    this->fss_editor->tc_on_completion_request
        = bind_mem(&filter_sub_source::rl_completion_request, this);
    this->fss_editor->tc_on_completion
        = bind_mem(&filter_sub_source::rl_completion, this);
    this->fss_editor->tc_on_perform
        = bind_mem(&filter_sub_source::rl_perform, this);
    this->fss_editor->tc_on_blur = bind_mem(&filter_sub_source::rl_blur, this);
    this->fss_editor->tc_on_abort
        = bind_mem(&filter_sub_source::rl_abort, this);
}

void
filter_sub_source::register_view(textview_curses* tc)
{
    text_sub_source::register_view(tc);
    tc->add_child_view(this->fss_editor.get());
}

bool
filter_sub_source::list_input_handle_key(listview_curses& lv, const ncinput& ch)
{
    static auto& prompt = lnav::prompt::get();

    if (this->fss_editing) {
        return this->fss_editor->handle_key(ch);
    }

    switch (ch.eff_text[0]) {
        case 'f': {
            auto* top_view = *lnav_data.ld_view_stack.top();
            auto* tss = top_view->get_sub_source();

            tss->toggle_apply_filters();
            top_view->reload_data();
            break;
        }
        case ' ': {
            auto* top_view = *lnav_data.ld_view_stack.top();
            auto* tss = top_view->get_sub_source();
            auto& fs = tss->get_filters();

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
            auto* top_view = *lnav_data.ld_view_stack.top();
            auto* tss = top_view->get_sub_source();
            auto& fs = tss->get_filters();

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
            auto* top_view = *lnav_data.ld_view_stack.top();
            auto* tss = top_view->get_sub_source();
            auto& fs = tss->get_filters();

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
            this->tss_view->vc_enabled = false;
            this->fss_view_text_possibilities
                = view_text_possibilities(*top_view);
            this->fss_editor->tc_text_format = text_format_t::TF_PCRE;
            this->fss_editor->set_y(lv.get_y_for_selection());
            this->fss_editor->set_content("");
            this->fss_editor->tc_suggestion = top_view->get_input_suggestion();
            this->fss_editor->set_visible(true);
            this->fss_editor->focus();
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
            this->tss_view->vc_enabled = false;

            this->fss_editor->tc_text_format = text_format_t::TF_PCRE;
            this->fss_editor->set_y(lv.get_y_for_selection());
            this->fss_editor->set_visible(true);
            this->fss_editor->set_content("");
            this->fss_view_text_possibilities
                = view_text_possibilities(*top_view);
            this->fss_editor->tc_suggestion = top_view->get_input_suggestion();
            this->fss_editor->focus();
            this->fss_filter_state = true;
            ef->disable();
            return true;
        }
        case '\r':
        case NCKEY_ENTER: {
            auto* top_view = *lnav_data.ld_view_stack.top();
            auto* tss = top_view->get_sub_source();
            auto& fs = tss->get_filters();

            if (fs.empty()) {
                return true;
            }

            auto tf = *(fs.begin() + lv.get_selection());

            this->fss_editing = true;
            this->tss_view->vc_enabled = false;

            this->fss_editor->tc_text_format
                = tf->get_lang() == filter_lang_t::SQL ? text_format_t::TF_SQL
                                                       : text_format_t::TF_PCRE;
            if (tf->get_lang() == filter_lang_t::SQL) {
                prompt.refresh_sql_completions(*top_view);
                prompt.refresh_sql_expr_completions(*top_view);
            }
            this->fss_editor->set_y(lv.get_y_for_selection());
            this->fss_editor->set_visible(true);
            this->fss_editor->tc_suggestion.clear();
            this->fss_editor->set_content(tf->get_id());
            this->fss_view_text_possibilities
                = view_text_possibilities(*top_view);
            this->fss_editor->focus();
            this->fss_filter_state = tf->is_enabled();
            tf->disable();
            tss->text_filters_changed();
            return true;
        }
        case 'n': {
            lnav_data.ld_exec_context.execute(":next-mark search");
            return true;
        }
        case 'N': {
            lnav_data.ld_exec_context.execute(":prev-mark search");
            return true;
        }
        case '/': {
            lnav_data.ld_exec_context.execute(":prompt search-filters");
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
                [](auto tc) -> std::optional<size_t> {
               text_sub_source* tss = tc->get_sub_source();

               if (tss == nullptr) {
                   return std::nullopt;
               }
               auto& fs = tss->get_filters();
               return std::make_optional(fs.size());
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

line_info
filter_sub_source::text_value_for_line(textview_curses& tc,
                                       int line,
                                       std::string& value_out,
                                       line_flags_t flags)
{
    auto* top_view = *lnav_data.ld_view_stack.top();
    auto* tss = top_view->get_sub_source();
    auto& fs = tss->get_filters();
    auto tf = *(fs.begin() + line);
    bool selected
        = lnav_data.ld_mode == ln_mode_t::FILTER && line == tc.get_selection();

    this->fss_curr_line.clear();
    auto& al = this->fss_curr_line;
    attr_line_builder alb(al);

    if (selected) {
        al.append(" ", VC_GRAPHIC.value(NCACS_RARROW));
    } else {
        al.append(" ");
    }
    al.append(" ");
    if (tf->is_enabled()) {
        al.append("\u25c6"_ok);
    } else {
        al.append("\u25c7"_comment);
    }
    al.append(" ");
    switch (tf->get_type()) {
        case text_filter::INCLUDE:
            al.append(" ").append(lnav::roles::ok("IN")).append(" ");
            break;
        case text_filter::EXCLUDE:
            if (tf->get_lang() == filter_lang_t::REGEX) {
                al.append(lnav::roles::error("OUT")).append(" ");
            } else {
                al.append("    ");
            }
            break;
        default:
            ensure(0);
            break;
    }

    {
        auto ag = alb.with_attr(VC_ROLE.value(role_t::VCR_NUMBER));
        if (this->fss_editing && line == tc.get_selection()) {
            alb.appendf(FMT_STRING("{:>9}"), "-");
        } else {
            alb.appendf(FMT_STRING("{:>9L}"),
                        tss->get_filtered_count_for(tf->get_index()));
        }
    }

    al.append(" hits ").append("|", VC_GRAPHIC.value(NCACS_VLINE)).append(" ");

    attr_line_t content{tf->get_id()};
    switch (tf->get_lang()) {
        case filter_lang_t::REGEX:
            readline_regex_highlighter(content, std::nullopt);
            break;
        case filter_lang_t::SQL:
            readline_sqlite_highlighter(content, std::nullopt);
            break;
        case filter_lang_t::NONE:
            break;
    }
    al.append(content);

    if (selected) {
        al.with_attr_for_all(VC_ROLE.value(role_t::VCR_FOCUSED));
    }

    value_out = al.get_string();

    return {};
}

void
filter_sub_source::text_attrs_for_line(textview_curses& tc,
                                       int line,
                                       string_attrs_t& value_out)
{
    value_out = this->fss_curr_line.get_attrs();
}

size_t
filter_sub_source::text_size_for_line(textview_curses& tc,
                                      int line,
                                      text_sub_source::line_flags_t raw)
{
    auto* top_view = *lnav_data.ld_view_stack.top();
    auto* tss = top_view->get_sub_source();
    auto& fs = tss->get_filters();
    auto tf = *(fs.begin() + line);

    return 8 + tf->get_id().size();
}

void
filter_sub_source::rl_change(textinput_curses& rc)
{
    auto* top_view = *lnav_data.ld_view_stack.top();
    auto* tss = top_view->get_sub_source();
    auto& fs = tss->get_filters();
    if (fs.empty()) {
        return;
    }

    auto iter = fs.begin() + this->tss_view->get_selection();
    auto tf = *iter;
    auto new_value = rc.get_content();

    top_view->get_highlights().erase({highlight_source_t::PREVIEW, "preview"});
    top_view->set_needs_update();
    switch (tf->get_lang()) {
        case filter_lang_t::NONE:
            break;
        case filter_lang_t::REGEX: {
            if (new_value.empty()) {
                auto sugg = top_view->get_current_search();
                if (top_view->tc_selected_text) {
                    sugg = top_view->tc_selected_text->sti_value;
                }
                if (fs.get_filter(sugg) == nullptr) {
                    this->fss_editor->tc_suggestion = sugg;
                } else {
                    this->fss_editor->tc_suggestion.clear();
                }
            } else {
                auto regex_res
                    = lnav::pcre2pp::code::from(new_value, PCRE2_CASELESS);

                this->rl_completion_request_int(
                    rc, completion_request_type_t::partial);
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
                    hl.with_attrs(text_attrs::with_styles(
                        text_attrs::style::blink, text_attrs::style::reverse));
                    hm[{highlight_source_t::PREVIEW, "preview"}] = hl;
                    top_view->set_needs_update();
                    lnav_data.ld_filter_help_status_source.fss_error.clear();
                }
            }
            break;
        }
        case filter_lang_t::SQL: {
            this->rl_completion_request_int(rc,
                                            completion_request_type_t::partial);

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
filter_sub_source::rl_history(textinput_curses& tc)
{
    switch (tc.tc_text_format) {
        case text_format_t::TF_PCRE: {
            std::vector<attr_line_t> poss;
            this->fss_regexp_history.query_entries(
                tc.get_content(),
                [&poss](const auto& e) { poss.emplace_back(e.e_content); });
            tc.open_popup_for_history(poss);
            break;
        }
        case text_format_t::TF_SQL: {
            break;
        }
        default:
            ensure(false);
            break;
    }
}

void
filter_sub_source::rl_completion_request_int(textinput_curses& tc,
                                             completion_request_type_t crt)
{
    static const auto FILTER_HELP
        = help_text("filter", "filter the view by a pattern")
              .with_parameter(
                  help_text("pattern", "The pattern to filter by")
                      .with_format(help_parameter_format_t::HPF_REGEX));
    static const auto FILTER_EXPR_HELP
        = help_text("filter-expr", "filter the view by a SQL expression")
              .with_parameter(
                  help_text("expr", "The expression to evaluate")
                      .with_format(help_parameter_format_t::HPF_SQL_EXPR));
    static auto& prompt = lnav::prompt::get();

    auto* top_view = *lnav_data.ld_view_stack.top();
    auto& al = tc.tc_lines[tc.tc_cursor.y];
    auto al_sf = al.to_string_fragment().sub_cell_range(0, tc.tc_cursor.x);
    std::string prefix;
    auto is_regex = tc.tc_text_format == text_format_t::TF_PCRE;
    auto parse_res = lnav::command::parse_for_prompt(
        lnav_data.ld_exec_context,
        al_sf,
        is_regex ? FILTER_HELP : FILTER_EXPR_HELP);

    switch (crt) {
        case completion_request_type_t::partial: {
            if (al_sf.endswith(" ")) {
                if (tc.is_cursor_at_end_of_line()) {
                    tc.tc_suggestion
                        = prompt.get_regex_suggestion(*top_view, al.al_string);
                }
                return;
            }
            break;
        }
        case completion_request_type_t::full:
            break;
    }

    auto byte_x = al_sf.column_to_byte_index(tc.tc_cursor.x);
    auto arg_res_opt = parse_res.arg_at(byte_x);
    if (arg_res_opt) {
        auto arg_pair = arg_res_opt.value();
        if (crt == completion_request_type_t::full
            || tc.tc_popup_type != textinput_curses::popup_type_t::none)
        {
            auto poss = prompt.get_cmd_parameter_completion(
                *top_view, arg_pair.aar_help, arg_pair.aar_element.se_value);
            auto left = arg_pair.aar_element.se_value.empty()
                ? tc.tc_cursor.x
                : al_sf.byte_to_column_index(
                      arg_pair.aar_element.se_origin.sf_begin);
            tc.open_popup_for_completion(left, poss);
            tc.tc_popup.set_title(arg_pair.aar_help->ht_name);
        } else if (arg_pair.aar_element.se_value.empty()
                   && tc.is_cursor_at_end_of_line())
        {
            tc.tc_suggestion
                = prompt.get_regex_suggestion(*top_view, al.al_string);
        } else {
            log_debug("not at end of line");
            tc.tc_suggestion.clear();
        }
    } else {
        log_debug("no arg");
    }
}

void
filter_sub_source::rl_completion_request(textinput_curses& tc)
{
    this->rl_completion_request_int(tc, completion_request_type_t::full);
}

void
filter_sub_source::rl_completion(textinput_curses& tc)
{
    static auto& prompt = lnav::prompt::get();

    prompt.rl_completion(tc);
}

void
filter_sub_source::rl_perform(textinput_curses& rc)
{
    static const intern_string_t INPUT_SRC = intern_string::lookup("input");

    auto* top_view = *lnav_data.ld_view_stack.top();
    auto* tss = top_view->get_sub_source();
    auto& fs = tss->get_filters();
    auto iter = fs.begin() + this->tss_view->get_selection();
    auto tf = *iter;
    auto new_value = rc.get_content();

    if (new_value.empty()) {
        this->rl_abort(rc);
    } else {
        switch (tf->get_lang()) {
            case filter_lang_t::NONE:
            case filter_lang_t::REGEX: {
                auto compile_res
                    = lnav::pcre2pp::code::from(new_value, PCRE2_CASELESS);

                if (compile_res.isErr()) {
                    auto ce = compile_res.unwrapErr();
                    auto um = lnav::console::to_user_message(INPUT_SRC, ce);
                    lnav_data.ld_exec_context.ec_msg_callback_stack.back()(um);
                    this->rl_abort(rc);
                } else {
                    auto code_ptr = compile_res.unwrap().to_shared();
                    tf->lf_deleted = true;
                    tss->text_filters_changed();

                    auto pf = std::make_shared<pcre_filter>(
                        tf->get_type(), new_value, tf->get_index(), code_ptr);

                    this->fss_regexp_history.insert_plain_content(new_value);

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
                    lnav_data.ld_exec_context.ec_msg_callback_stack.back()(um);
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

    top_view->reload_data();
    this->tss_view->reload_data();
}

void
filter_sub_source::rl_blur(textinput_curses& tc)
{
    auto* top_view = *lnav_data.ld_view_stack.top();
    top_view->get_highlights().erase({highlight_source_t::PREVIEW, "preview"});
    lnav_data.ld_log_source.set_preview_sql_filter(nullptr);
    lnav_data.ld_filter_help_status_source.fss_prompt.clear();
    lnav_data.ld_filter_help_status_source.fss_error.clear();
    this->fss_editing = false;
    tc.set_visible(false);
    this->tss_view->vc_enabled = true;
}

void
filter_sub_source::rl_abort(textinput_curses& rc)
{
    auto* top_view = *lnav_data.ld_view_stack.top();
    auto* tss = top_view->get_sub_source();
    auto& fs = tss->get_filters();
    auto iter = fs.begin() + this->tss_view->get_selection();
    auto tf = *iter;

    top_view->reload_data();
    fs.delete_filter("");
    this->tss_view->reload_data();
    this->tss_view->set_needs_update();
    tf->set_enabled(this->fss_filter_state);
    tss->text_filters_changed();
    this->tss_view->reload_data();
}

void
filter_sub_source::list_input_handle_scroll_out(listview_curses& lv)
{
    set_view_mode(ln_mode_t::PAGING);
    lnav_data.ld_filter_view.reload_data();
}

bool
filter_sub_source::text_handle_mouse(
    textview_curses& tc,
    const listview_curses::display_line_content_t&,
    mouse_event& me)
{
    if (this->fss_editing) {
        return true;
    }
    auto nci = ncinput{};
    if (me.is_click_in(mouse_button_t::BUTTON_LEFT, 1, 3)) {
        nci.id = ' ';
        nci.eff_text[0] = ' ';
        this->list_input_handle_key(tc, nci);
    }
    if (me.is_click_in(mouse_button_t::BUTTON_LEFT, 4, 7)) {
        nci.id = 't';
        nci.eff_text[0] = 't';
        this->list_input_handle_key(tc, nci);
    }
    if (me.is_double_click_in(mouse_button_t::BUTTON_LEFT, line_range{25, -1}))
    {
        nci.id = '\r';
        nci.eff_text[0] = '\r';
        this->list_input_handle_key(tc, nci);
    }
    return true;
}
