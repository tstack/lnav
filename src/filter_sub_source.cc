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
#include <optional>
#include <utility>

#include "filter_sub_source.hh"

#include "base/attr_line.builder.hh"
#include "base/auto_mem.hh"
#include "base/func_util.hh"
#include "base/itertools.hh"
#include "base/opt_util.hh"
#include "base/text_format_enum.hh"
#include "cmd.parser.hh"
#include "config.h"
#include "data_scanner.hh"
#include "lnav.hh"
#include "lnav.prompt.hh"
#include "readline_highlighters.hh"
#include "readline_possibilities.hh"
#include "relative_time.hh"
#include "sql_util.hh"
#include "textinput_curses.hh"
#include "textview_curses.hh"

using namespace lnav::roles::literals;

filter_sub_source::filter_sub_source(std::shared_ptr<textinput_curses> editor)
    : fss_editor(std::move(editor)),
      fss_regexp_history(
          lnav::textinput::history::for_context("regexp-filter"_frag)),
      fss_sql_history(lnav::textinput::history::for_context("sql-filter"_frag))
{
    this->fss_editor->set_visible(false);
    this->fss_editor->set_x(28);
    this->fss_editor->tc_popup.set_title("Pattern");
    this->fss_editor->tc_height = 1;
    this->fss_editor->tc_on_change
        = bind_mem(&filter_sub_source::rl_change, this);
    this->fss_editor->tc_on_history_search
        = bind_mem(&filter_sub_source::rl_history, this);
    this->fss_editor->tc_on_history_list
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
    tc->set_ensure_selection(true);
}

bool
filter_sub_source::list_input_handle_key(listview_curses& lv, const ncinput& ch)
{
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
        case ' ':
        case 't':
        case 'D': {
            auto* top_view = *lnav_data.ld_view_stack.top();
            auto rows = this->rows_for(top_view);
            if (rows.empty() || !lv.get_selection()) {
                return true;
            }

            auto& tf = rows[lv.get_selection().value()];
            tf->handle_key(top_view, ch);
            lv.reload_data();
            top_view->get_sub_source()->text_filters_changed();
            top_view->reload_data();
            return true;
        }
        case 'm': {
            auto* top_view = *lnav_data.ld_view_stack.top();
            auto* ttt = dynamic_cast<text_time_translator*>(
                top_view->get_sub_source());
            if (ttt != nullptr) {
                auto curr_min = ttt->get_min_row_time();
                this->fss_filter_state = curr_min.has_value();
                if (curr_min) {
                    this->fss_min_time = curr_min;
                    ttt->set_min_row_time(text_time_translator::min_time_init);
                    ttt->ttt_preview_min_time = curr_min;
                    top_view->get_sub_source()->text_filters_changed();
                } else {
                    auto sel = top_view->get_selection();
                    if (sel) {
                        auto ri_opt = ttt->time_for_row(sel.value());
                        if (ri_opt) {
                            auto ri = ri_opt.value();

                            this->fss_min_time = ri.ri_time;
                        }
                    }
                    if (!this->fss_min_time) {
                        this->fss_min_time = current_timeval();
                    }
                    ttt->ttt_preview_min_time = this->fss_min_time;
                }

                lv.set_selection(0_vl);
                lv.reload_data();

                auto rows = this->rows_for(top_view);
                auto& row = rows[0];
                this->fss_editing = true;
                this->tss_view->set_enabled(false);
                row->prime_text_input(top_view, *this->fss_editor, *this);
                this->fss_editor->set_y(lv.get_y_for_selection());
                this->fss_editor->set_visible(true);
                this->fss_editor->focus();
                this->tss_view->reload_data();
                return true;
            }
            break;
        }
        case 'M': {
            auto* top_view = *lnav_data.ld_view_stack.top();
            auto* ttt = dynamic_cast<text_time_translator*>(
                top_view->get_sub_source());
            if (ttt != nullptr) {
                auto curr_max = ttt->get_max_row_time();
                this->fss_filter_state = curr_max.has_value();
                if (curr_max) {
                    this->fss_max_time = curr_max;
                    ttt->ttt_preview_max_time = curr_max;
                    ttt->set_max_row_time(text_time_translator::max_time_init);
                    top_view->get_sub_source()->text_filters_changed();
                } else {
                    auto sel = top_view->get_selection();
                    if (sel) {
                        auto ri_opt = ttt->time_for_row(sel.value());
                        if (ri_opt) {
                            auto ri = ri_opt.value();

                            this->fss_max_time = ri.ri_time;
                        }
                    }
                    if (!this->fss_max_time) {
                        this->fss_max_time = current_timeval();
                    }
                    ttt->ttt_preview_max_time = this->fss_max_time;
                }

                auto new_sel = ttt->get_min_row_time() ? 1_vl : 0_vl;
                lv.set_selection(new_sel);
                lv.reload_data();

                auto rows = this->rows_for(top_view);
                auto& row = rows[new_sel];
                this->fss_editing = true;
                this->tss_view->set_enabled(false);
                row->prime_text_input(top_view, *this->fss_editor, *this);
                this->fss_editor->set_y(lv.get_y_for_selection());
                this->fss_editor->set_visible(true);
                this->fss_editor->focus();
                this->tss_view->reload_data();
                return true;
            }
            break;
        }
        case 'i':
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

            auto filter_type = ch.eff_text[0] == 'i'
                ? text_filter::type_t::INCLUDE
                : text_filter::type_t::EXCLUDE;
            auto ef
                = std::make_shared<empty_filter>(filter_type, *filter_index);
            fs.add_filter(ef);

            auto rows = this->rows_for(top_view);
            lv.set_selection(vis_line_t(rows.size()) - 1_vl);
            auto& row = rows.back();
            lv.reload_data();

            this->fss_editing = true;
            this->tss_view->set_enabled(false);
            this->fss_view_text_possibilities
                = view_text_possibilities(*top_view);
            row->prime_text_input(top_view, *this->fss_editor, *this);
            this->fss_editor->set_y(lv.get_y_for_selection());
            this->fss_editor->set_visible(true);
            this->fss_editor->focus();
            this->fss_filter_state = true;
            return true;
        }
        case '\r':
        case NCKEY_ENTER: {
            auto* top_view = *lnav_data.ld_view_stack.top();
            auto rows = this->rows_for(top_view);

            if (rows.empty() || !lv.get_selection()) {
                return true;
            }

            auto& row = rows[lv.get_selection().value()];
            this->fss_editing = true;
            this->tss_view->set_enabled(false);
            this->fss_editor->set_y(lv.get_y_for_selection());
            this->fss_editor->set_visible(true);
            this->fss_editor->tc_suggestion.clear();
            this->fss_view_text_possibilities
                = view_text_possibilities(*top_view);
            this->fss_editor->focus();
            this->fss_filter_state
                = row->prime_text_input(top_view, *this->fss_editor, *this);
            top_view->get_sub_source()->text_filters_changed();
            return true;
        }
        case 'n': {
            lnav_data.ld_exec_context.execute(INTERNAL_SRC_LOC,
                                              ":next-mark search");
            return true;
        }
        case 'N': {
            lnav_data.ld_exec_context.execute(INTERNAL_SRC_LOC,
                                              ":prev-mark search");
            return true;
        }
        case '/': {
            lnav_data.ld_exec_context.execute(INTERNAL_SRC_LOC,
                                              ":prompt search-filters");
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
                [this](auto tc) -> std::optional<size_t> {
               auto* tss = tc->get_sub_source();

               if (tss == nullptr) {
                   return std::nullopt;
               }
               auto rows = this->rows_for(tc);
               return std::make_optional(rows.size());
           })
        .value_or(0);
}

size_t
filter_sub_source::text_line_width(textview_curses& curses)
{
    auto* top_view = *lnav_data.ld_view_stack.top();
    auto* tss = top_view->get_sub_source();
    auto& fs = tss->get_filters();
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
    auto selected
        = lnav_data.ld_mode == ln_mode_t::FILTER && line == tc.get_selection();
    auto rows = this->rows_for(top_view);
    auto rs = render_state{top_view};
    auto& al = this->fss_curr_line;

    al.clear();
    if (selected && this->fss_editing) {
        rs.rs_editing = true;
    }
    if (selected) {
        al.append(" ", VC_GRAPHIC.value(NCACS_RARROW));
    } else {
        al.append(" ");
    }
    al.append(" ");

    auto& row = rows[line];

    row->value_for(rs, al);
    if (selected) {
        al.with_attr_for_all(VC_ROLE.value(role_t::VCR_FOCUSED));
    }

    value_out = al.al_string;
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
    // XXX
    return 40;
}

void
filter_sub_source::rl_change(textinput_curses& rc)
{
    auto* top_view = *lnav_data.ld_view_stack.top();
    auto rows = this->rows_for(top_view);
    auto& row = rows[this->tss_view->get_selection().value()];

    row->ti_change(top_view, rc);
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
        case text_format_t::TF_UNKNOWN:
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
    auto* top_view = *lnav_data.ld_view_stack.top();
    auto rows = this->rows_for(top_view);
    auto& row = rows[this->tss_view->get_selection().value()];

    row->ti_completion_request(top_view, tc, crt);
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
    auto* top_view = *lnav_data.ld_view_stack.top();
    auto rows = this->rows_for(top_view);
    auto& row = rows[this->tss_view->get_selection().value()];

    row->ti_perform(top_view, rc, *this);
    this->fss_min_time = std::nullopt;
    this->fss_max_time = std::nullopt;
    this->tss_view->reload_data();
}

void
filter_sub_source::rl_blur(textinput_curses& tc)
{
    auto* top_view = *lnav_data.ld_view_stack.top();
    top_view->clear_preview();
    lnav_data.ld_filter_help_status_source.fss_prompt.clear();
    lnav_data.ld_filter_help_status_source.fss_error.clear();
    this->fss_editing = false;
    tc.set_visible(false);
    this->tss_view->set_enabled(true);
}

void
filter_sub_source::rl_abort(textinput_curses& rc)
{
    auto* top_view = *lnav_data.ld_view_stack.top();
    auto rows = this->rows_for(top_view);
    auto& row = rows[this->tss_view->get_selection().value()];

    row->ti_abort(top_view, rc, *this);
    this->fss_min_time = std::nullopt;
    this->fss_max_time = std::nullopt;
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

bool
filter_sub_source::min_time_filter_row::handle_key(textview_curses* top_view,
                                                   const ncinput& ch)
{
    auto* ttt = dynamic_cast<text_time_translator*>(top_view->get_sub_source());
    switch (ch.eff_text[0]) {
        case 'D':
            ttt->set_min_row_time(text_time_translator::min_time_init);
            top_view->get_sub_source()->text_filters_changed();
            return true;
        default:
            return false;
    }
}

void
filter_sub_source::min_time_filter_row::value_for(const render_state& rs,
                                                  attr_line_t& al)
{
    al.append("Min Time"_table_header).append(" ");
    if (rs.rs_editing) {
        al.append(fmt::format(FMT_STRING("{:>9}"), "-"),
                  VC_ROLE.value(role_t::VCR_NUMBER));
    } else {
        al.append(fmt::format(
                      FMT_STRING("{:>9}"),
                      rs.rs_top_view->get_sub_source()->get_filtered_before()),
                  VC_ROLE.value(role_t::VCR_NUMBER));
    }
    al.append(" hits ").append("|", VC_GRAPHIC.value(NCACS_VLINE)).append(" ");
    al.append(lnav::to_rfc3339_string(this->tfr_time));
}

Result<timeval, std::string>
filter_sub_source::time_filter_row::parse_time(textview_curses* top_view,
                                               textinput_curses& tc)
{
    auto content = tc.get_content();
    if (content.empty()) {
        return Err(std::string("expecting a timestamp or relative time"));
    }

    auto parse_res = relative_time::from_str(content);
    auto* ttt = dynamic_cast<text_time_translator*>(top_view->get_sub_source());
    date_time_scanner dts;

    if (top_view->get_inner_height() > 0_vl) {
        auto top_time_opt = ttt->time_for_row(
            top_view->get_selection().value_or(top_view->get_top()));

        if (top_time_opt) {
            auto top_time_tv = top_time_opt.value().ri_time;
            tm top_tm;

            localtime_r(&top_time_tv.tv_sec, &top_tm);
            dts.set_base_time(top_time_tv.tv_sec, top_tm);
        }
    }
    if (parse_res.isOk()) {
        auto rt = parse_res.unwrap();
        auto tv_opt
            = ttt->time_for_row(top_view->get_selection().value_or(0_vl));
        auto tv = current_timeval();
        if (tv_opt) {
            tv = tv_opt.value().ri_time;
        }
        auto tm = rt.adjust(tv);
        return Ok(tm.to_timeval());
    }
    auto time_str = tc.get_content();
    exttm tm;
    timeval tv;
    const auto* scan_end
        = dts.scan(time_str.c_str(), time_str.size(), nullptr, &tm, tv);
    if (scan_end != nullptr) {
        auto matched_size = scan_end - time_str.c_str();
        if (matched_size == time_str.size()) {
            return Ok(tv);
        }

        return Err(fmt::format(FMT_STRING("extraneous input '{}'"), scan_end));
    }

    auto pe = parse_res.unwrapErr();
    return Err(pe.pe_msg);
}

void
filter_sub_source::min_time_filter_row::ti_perform(textview_curses* top_view,
                                                   textinput_curses& tc,
                                                   filter_sub_source& parent)
{
    auto* ttt = dynamic_cast<text_time_translator*>(top_view->get_sub_source());
    auto parse_res = this->parse_time(top_view, tc);
    if (parse_res.isOk()) {
        auto tv = parse_res.unwrap();

        ttt->set_min_row_time(tv);
    } else {
        auto msg = parse_res.unwrapErr();
        if (parent.fss_filter_state) {
            ttt->set_min_row_time(parent.fss_min_time.value());
        }

        auto um
            = lnav::console::user_message::error("could not parse timestamp")
                  .with_reason(msg);
        lnav_data.ld_exec_context.ec_msg_callback_stack.back()(um);
    }
    parent.fss_min_time = std::nullopt;
    top_view->get_sub_source()->text_filters_changed();
}

void
filter_sub_source::min_time_filter_row::ti_abort(textview_curses* top_view,
                                                 textinput_curses& tc,
                                                 filter_sub_source& parent)
{
    auto* tss = top_view->get_sub_source();
    auto* ttt = dynamic_cast<text_time_translator*>(tss);

    if (parent.fss_filter_state) {
        ttt->set_min_row_time(parent.fss_min_time.value());
    }
    tss->text_filters_changed();
}

bool
filter_sub_source::time_filter_row::prime_text_input(textview_curses* top_view,
                                                     textinput_curses& ti,
                                                     filter_sub_source& parent)
{
    ti.tc_text_format = text_format_t::TF_UNKNOWN;
    ti.set_content(lnav::to_rfc3339_string(this->tfr_time));
    return true;
}

void
filter_sub_source::min_time_filter_row::ti_change(textview_curses* top_view,
                                                  textinput_curses& rc)
{
    auto* ttt = dynamic_cast<text_time_translator*>(top_view->get_sub_source());
    auto parse_res = this->parse_time(top_view, rc);

    if (parse_res.isOk()) {
        auto tv = parse_res.unwrap();
        ttt->ttt_preview_min_time = tv;
        lnav_data.ld_filter_help_status_source.fss_error.clear();
    } else {
        ttt->ttt_preview_min_time = std::nullopt;
        auto msg = parse_res.unwrapErr();
        lnav_data.ld_filter_help_status_source.fss_error.set_value(
            "error: could not parse timestamp -- %s", msg.c_str());
    }
}

void
filter_sub_source::max_time_filter_row::ti_change(textview_curses* top_view,
                                                  textinput_curses& rc)
{
    auto* ttt = dynamic_cast<text_time_translator*>(top_view->get_sub_source());
    auto parse_res = this->parse_time(top_view, rc);

    if (parse_res.isOk()) {
        auto tv = parse_res.unwrap();
        ttt->ttt_preview_max_time = tv;
        lnav_data.ld_filter_help_status_source.fss_error.clear();
    } else {
        ttt->ttt_preview_max_time = std::nullopt;
        auto msg = parse_res.unwrapErr();
        lnav_data.ld_filter_help_status_source.fss_error.set_value(
            "error: could not parse timestamp -- %s", msg.c_str());
    }
}

void
filter_sub_source::time_filter_row::ti_completion_request(
    textview_curses* top_view,
    textinput_curses& tc,
    completion_request_type_t crt)
{
}

bool
filter_sub_source::max_time_filter_row::handle_key(textview_curses* top_view,
                                                   const ncinput& ch)
{
    auto* ttt = dynamic_cast<text_time_translator*>(top_view->get_sub_source());
    switch (ch.eff_text[0]) {
        case 'D':
            ttt->set_max_row_time(text_time_translator::max_time_init);
            top_view->get_sub_source()->text_filters_changed();
            return true;
        default:
            return false;
    }
}

void
filter_sub_source::max_time_filter_row::value_for(const render_state& rs,
                                                  attr_line_t& al)
{
    al.append("Max Time"_table_header).append(" ");
    if (rs.rs_editing) {
        al.append(fmt::format(FMT_STRING("{:>9}"), "-"),
                  VC_ROLE.value(role_t::VCR_NUMBER));
    } else {
        al.append(
            fmt::format(FMT_STRING("{:>9}"),
                        rs.rs_top_view->get_sub_source()->get_filtered_after()),
            VC_ROLE.value(role_t::VCR_NUMBER));
    }
    al.append(" hits ").append("|", VC_GRAPHIC.value(NCACS_VLINE)).append(" ");
    al.append(lnav::to_rfc3339_string(this->tfr_time));
}

void
filter_sub_source::max_time_filter_row::ti_perform(textview_curses* top_view,
                                                   textinput_curses& tc,
                                                   filter_sub_source& parent)
{
    auto* ttt = dynamic_cast<text_time_translator*>(top_view->get_sub_source());
    auto parse_res = this->parse_time(top_view, tc);
    if (parse_res.isOk()) {
        auto tv = parse_res.unwrap();

        ttt->set_max_row_time(tv);
    } else {
        auto msg = parse_res.unwrapErr();
        if (parent.fss_filter_state) {
            ttt->set_max_row_time(parent.fss_max_time.value());
        }
        auto um
            = lnav::console::user_message::error("could not parse timestamp")
                  .with_reason(msg);
        lnav_data.ld_exec_context.ec_msg_callback_stack.back()(um);
    }
    parent.fss_max_time = std::nullopt;
    top_view->get_sub_source()->text_filters_changed();
}

void
filter_sub_source::max_time_filter_row::ti_abort(textview_curses* top_view,
                                                 textinput_curses& tc,
                                                 filter_sub_source& parent)
{
    auto* tss = top_view->get_sub_source();
    auto* ttt = dynamic_cast<text_time_translator*>(tss);

    if (parent.fss_filter_state) {
        ttt->set_max_row_time(parent.fss_max_time.value());
    }
    tss->text_filters_changed();
}

void
filter_sub_source::text_filter_row::value_for(const render_state& rs,
                                              attr_line_t& al)
{
    attr_line_builder alb(al);
    if (this->tfr_filter->is_enabled()) {
        al.append("\u25c6"_ok);
    } else {
        al.append("\u25c7"_comment);
    }
    al.append(" ");
    switch (this->tfr_filter->get_type()) {
        case text_filter::INCLUDE:
            al.append(" ").append(lnav::roles::ok("IN")).append(" ");
            break;
        case text_filter::EXCLUDE:
            if (this->tfr_filter->get_lang() == filter_lang_t::REGEX) {
                al.append(lnav::roles::error("OUT")).append(" ");
            } else {
                al.append("    ");
            }
            break;
        default:
            ensure(0);
            break;
    }
    al.append("   ");

    {
        auto ag = alb.with_attr(VC_ROLE.value(role_t::VCR_NUMBER));
        if (rs.rs_editing) {
            alb.appendf(FMT_STRING("{:>9}"), "-");
        } else {
            alb.appendf(
                FMT_STRING("{:>9L}"),
                rs.rs_top_view->get_sub_source()->get_filtered_count_for(
                    this->tfr_filter->get_index()));
        }
    }

    al.append(" hits ").append("|", VC_GRAPHIC.value(NCACS_VLINE)).append(" ");

    attr_line_t content{this->tfr_filter->get_id()};
    switch (this->tfr_filter->get_lang()) {
        case filter_lang_t::REGEX:
            readline_regex_highlighter(content, std::nullopt);
            break;
        case filter_lang_t::SQL:
            readline_sql_highlighter(
                content, lnav::sql::dialect::sqlite, std::nullopt);
            break;
        case filter_lang_t::NONE:
            break;
    }
    al.append(content);
}

bool
filter_sub_source::text_filter_row::handle_key(textview_curses* top_view,
                                               const ncinput& ch)
{
    switch (ch.eff_text[0]) {
        case ' ': {
            auto& fs = top_view->get_sub_source()->get_filters();
            fs.set_filter_enabled(this->tfr_filter,
                                  !this->tfr_filter->is_enabled());
            return true;
        }
        case 't': {
            if (this->tfr_filter->get_type() == text_filter::INCLUDE) {
                this->tfr_filter->set_type(text_filter::EXCLUDE);
            } else {
                this->tfr_filter->set_type(text_filter::INCLUDE);
            }
            return true;
        }
        case 'D': {
            auto& fs = top_view->get_sub_source()->get_filters();

            fs.delete_filter(this->tfr_filter->get_id());
            return true;
        }
        default:
            return false;
    }
}

bool
filter_sub_source::text_filter_row::prime_text_input(textview_curses* top_view,
                                                     textinput_curses& ti,
                                                     filter_sub_source& parent)
{
    static auto& prompt = lnav::prompt::get();

    ti.tc_text_format = this->tfr_filter->get_lang() == filter_lang_t::SQL
        ? text_format_t::TF_SQL
        : text_format_t::TF_PCRE;
    if (this->tfr_filter->get_lang() == filter_lang_t::SQL) {
        prompt.refresh_sql_completions(*top_view);
        prompt.refresh_sql_expr_completions(*top_view);
    }
    ti.set_content(this->tfr_filter->get_id());
    if (this->tfr_filter->get_id().empty()) {
        ti.tc_suggestion = top_view->get_input_suggestion();
    }
    auto retval = this->tfr_filter->is_enabled();
    this->tfr_filter->disable();

    return retval;
}

void
filter_sub_source::text_filter_row::ti_change(textview_curses* top_view,
                                              textinput_curses& rc)
{
    auto new_value = rc.get_content();
    auto* tss = top_view->get_sub_source();
    auto& fs = tss->get_filters();

    top_view->get_highlights().erase({highlight_source_t::PREVIEW, "preview"});
    top_view->set_needs_update();
    switch (this->tfr_filter->get_lang()) {
        case filter_lang_t::NONE:
            break;
        case filter_lang_t::REGEX: {
            if (new_value.empty()) {
                auto sugg = top_view->get_current_search();
                if (top_view->tc_selected_text) {
                    sugg = top_view->tc_selected_text->sti_value;
                }
                if (fs.get_filter(sugg) == nullptr) {
                    rc.tc_suggestion = sugg;
                } else {
                    rc.tc_suggestion.clear();
                }
            } else {
                auto regex_res
                    = lnav::pcre2pp::code::from(new_value, PCRE2_CASELESS);

                this->ti_completion_request(
                    top_view, rc, completion_request_type_t::partial);
                if (regex_res.isErr()) {
                    auto pe = regex_res.unwrapErr();
                    lnav_data.ld_filter_help_status_source.fss_error.set_value(
                        "error: %s", pe.get_message().c_str());
                } else {
                    auto& hm = top_view->get_highlights();
                    highlighter hl(regex_res.unwrap().to_shared());
                    auto role
                        = this->tfr_filter->get_type() == text_filter::EXCLUDE
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
            this->ti_completion_request(
                top_view, rc, completion_request_type_t::partial);

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
filter_sub_source::text_filter_row::ti_completion_request(
    textview_curses* top_view,
    textinput_curses& tc,
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

    auto& al = tc.tc_lines[tc.tc_cursor.y];
    auto al_sf = al.to_string_fragment().sub_cell_range(0, tc.tc_cursor.x);
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
                *top_view,
                &FILTER_HELP,
                arg_pair.aar_help,
                arg_pair.aar_element.se_value);
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
filter_sub_source::text_filter_row::ti_perform(textview_curses* top_view,
                                               textinput_curses& ti,
                                               filter_sub_source& parent)
{
    static const intern_string_t INPUT_SRC = intern_string::lookup("input");

    auto* tss = top_view->get_sub_source();
    auto& fs = tss->get_filters();
    auto new_value = ti.get_content();

    fs.fs_generation += 1;
    if (new_value.empty()) {
        this->ti_abort(top_view, ti, parent);
    } else {
        switch (this->tfr_filter->get_lang()) {
            case filter_lang_t::NONE:
            case filter_lang_t::REGEX: {
                auto compile_res
                    = lnav::pcre2pp::code::from(new_value, PCRE2_CASELESS);

                if (compile_res.isErr()) {
                    auto ce = compile_res.unwrapErr();
                    auto um = lnav::console::to_user_message(INPUT_SRC, ce);
                    lnav_data.ld_exec_context.ec_msg_callback_stack.back()(um);
                    this->ti_abort(top_view, ti, parent);
                } else {
                    auto code_ptr = compile_res.unwrap().to_shared();
                    this->tfr_filter->lf_deleted = true;
                    tss->text_filters_changed();

                    auto pf = std::make_shared<pcre_filter>(
                        this->tfr_filter->get_type(),
                        new_value,
                        this->tfr_filter->get_index(),
                        code_ptr);

                    parent.fss_regexp_history.insert_plain_content(new_value);

                    auto iter
                        = std::find(fs.begin(), fs.end(), this->tfr_filter);

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
                    this->ti_abort(top_view, ti, parent);
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
}

void
filter_sub_source::text_filter_row::ti_abort(textview_curses* top_view,
                                             textinput_curses& tc,
                                             filter_sub_source& parent)
{
    auto* tss = top_view->get_sub_source();
    auto& fs = tss->get_filters();

    top_view->reload_data();
    fs.delete_filter("");
    this->tfr_filter->set_enabled(parent.fss_filter_state);
    tss->text_filters_changed();
}

std::vector<std::unique_ptr<filter_sub_source::filter_row>>
filter_sub_source::rows_for(textview_curses* tc) const
{
    auto retval = row_vector{};
    auto* tss = tc->get_sub_source();
    if (tss == nullptr) {
        return retval;
    }

    const auto* ttt = dynamic_cast<text_time_translator*>(tss);

    if (ttt != nullptr) {
        if (this->fss_min_time) {
            retval.emplace_back(std::make_unique<min_time_filter_row>(
                this->fss_min_time.value()));
        } else {
            auto min_time = ttt->get_min_row_time();
            if (min_time) {
                retval.emplace_back(
                    std::make_unique<min_time_filter_row>(min_time.value()));
            }
        }
        if (this->fss_max_time) {
            retval.emplace_back(std::make_unique<max_time_filter_row>(
                this->fss_max_time.value()));
        } else {
            auto max_time = ttt->get_max_row_time();
            if (max_time) {
                retval.emplace_back(
                    std::make_unique<max_time_filter_row>(max_time.value()));
            }
        }
    }

    auto& fs = tss->get_filters();
    for (auto& tf : fs) {
        retval.emplace_back(std::make_unique<text_filter_row>(tf));
    }

    return retval;
}
