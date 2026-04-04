/**
 * Copyright (c) 2023, Timothy Stack
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
#include <chrono>
#include <utility>
#include <vector>

#include "timeline_source.hh"

#include <time.h>

#include "base/humanize.hh"
#include "base/humanize.time.hh"
#include "base/itertools.enumerate.hh"
#include "base/itertools.hh"
#include "base/keycodes.hh"
#include "base/math_util.hh"
#include "command_executor.hh"
#include "lnav_util.hh"
#include "logline_window.hh"
#include "md4cpp.hh"
#include "pcrepp/pcre2pp.hh"
#include "readline_highlighters.hh"
#include "sql_util.hh"
#include "sysclip.hh"
#include "tlx/container/btree_map.hpp"

using namespace std::chrono_literals;
using namespace lnav::roles::literals;
using namespace md4cpp::literals;

static const std::vector<std::chrono::microseconds> TIME_SPANS = {
    500us, 1ms,   100ms, 500ms, 1s, 5s, 10s, 15s,     30s,      1min,
    5min,  15min, 1h,    2h,    4h, 8h, 24h, 7 * 24h, 30 * 24h, 365 * 24h,
};

static constexpr size_t MAX_OPID_WIDTH = 80;
static constexpr size_t MAX_DESC_WIDTH = 256;
static constexpr int CHART_INDENT = 24;

size_t
abbrev_ftime(char* datebuf, size_t db_size, const tm& lb_tm, const tm& dt)
{
    char lb_fmt[32] = " ";
    bool same = true;

    if (lb_tm.tm_year == dt.tm_year) {
        strcat(lb_fmt, "    ");
    } else {
        same = false;
        strcat(lb_fmt, "%Y");
    }
    if (same && lb_tm.tm_mon == dt.tm_mon) {
        strcat(lb_fmt, "   ");
    } else {
        if (!same) {
            strcat(lb_fmt, "-");
        }
        same = false;
        strcat(lb_fmt, "%m");
    }
    if (same && lb_tm.tm_mday == dt.tm_mday) {
        strcat(lb_fmt, "   ");
    } else {
        if (!same) {
            strcat(lb_fmt, "-");
        }
        same = false;
        strcat(lb_fmt, "%d");
    }
    if (same && lb_tm.tm_hour == dt.tm_hour) {
        strcat(lb_fmt, "   ");
    } else {
        if (!same) {
            strcat(lb_fmt, "T");
        }
        same = false;
        strcat(lb_fmt, "%H");
    }
    if (same && lb_tm.tm_min == dt.tm_min) {
        strcat(lb_fmt, "   ");
    } else {
        if (!same) {
            strcat(lb_fmt, ":");
        }
        same = false;
        strcat(lb_fmt, "%M");
    }
    return strftime(datebuf, db_size, lb_fmt, &dt);
}

std::vector<attr_line_t>
timeline_preview_overlay::list_overlay_menu(const listview_curses& lv,
                                            vis_line_t line)
{
    static constexpr auto MENU_WIDTH = 25;

    const auto* tc = dynamic_cast<const textview_curses*>(&lv);
    std::vector<attr_line_t> retval;

    if (tc->tc_text_selection_active || !tc->tc_selected_text) {
        return retval;
    }

    const auto& sti = tc->tc_selected_text.value();

    if (sti.sti_line != line) {
        return retval;
    }
    auto title = " Actions "_status_title;
    auto left = std::max(0, sti.sti_x - 2);
    auto dim = lv.get_dimensions();
    auto menu_line = vis_line_t{1};

    if (left + MENU_WIDTH >= dim.second) {
        left = dim.second - MENU_WIDTH;
    }

    this->los_menu_items.clear();

    retval.emplace_back(attr_line_t().pad_to(left).append(title));
    {
        auto start = left;
        attr_line_t al;

        al.append(":clipboard:"_emoji)
            .append(" Copy  ")
            .with_attr_for_all(VC_ROLE.value(role_t::VCR_STATUS));
        this->los_menu_items.emplace_back(
            menu_line,
            line_range{start, start + (int) al.length()},
            [](const std::string& value) {
                auto clip_res = sysclip::open(sysclip::type_t::GENERAL);
                if (clip_res.isErr()) {
                    log_error("unable to open clipboard: %s",
                              clip_res.unwrapErr().c_str());
                    return;
                }

                auto clip_pipe = clip_res.unwrap();
                fwrite(value.c_str(), 1, value.length(), clip_pipe.in());
            });
        retval.emplace_back(attr_line_t().pad_to(left).append(al));
    }

    return retval;
}

timeline_header_overlay::timeline_header_overlay(
    const std::shared_ptr<timeline_source>& src)
    : gho_src(src)
{
}

bool
timeline_header_overlay::list_static_overlay(const listview_curses& lv,
                                             media_t media,
                                             int y,
                                             int bottom,
                                             attr_line_t& value_out)
{
    if (this->gho_src->ts_rebuild_in_progress) {
        return false;
    }

    if (this->gho_src->ts_time_order.empty()) {
        if (y == 0) {
            this->gho_static_lines.clear();

            if (this->gho_src->ts_filtered_count > 0) {
                auto um = lnav::console::user_message::warning(
                    attr_line_t()
                        .append(lnav::roles::number(
                            fmt::to_string(this->gho_src->ts_filtered_count)))
                        .append(" operations have been filtered out"));
                auto min_time = this->gho_src->get_min_row_time();
                if (min_time) {
                    um.with_note(attr_line_t("Operations before ")
                                     .append_quoted(lnav::to_rfc3339_string(
                                         min_time.value()))
                                     .append(" are not being shown"));
                }
                auto max_time = this->gho_src->get_max_row_time();
                if (max_time) {
                    um.with_note(attr_line_t("Operations after ")
                                     .append_quoted(lnav::to_rfc3339_string(
                                         max_time.value()))
                                     .append(" are not being shown"));
                }

                auto& fs = this->gho_src->ts_lss.get_filters();
                for (const auto& filt : fs) {
                    auto hits = this->gho_src->ts_lss.get_filtered_count_for(
                        filt->get_index());
                    if (filt->get_type() == text_filter::EXCLUDE && hits == 0) {
                        continue;
                    }
                    auto cmd = attr_line_t(":" + filt->to_command());
                    readline_command_highlighter(cmd, std::nullopt);
                    um.with_note(
                        attr_line_t("Filter ")
                            .append_quoted(cmd)
                            .append(" matched ")
                            .append(lnav::roles::number(fmt::to_string(hits)))
                            .append(" message(s) "));
                }
                this->gho_static_lines = um.to_attr_line().split_lines();
            } else {
                auto um
                    = lnav::console::user_message::error("No operations found");
                if (this->gho_src->ts_lss.size() > 0) {
                    um.with_note("The loaded logs do not define any OP IDs")
                        .with_help(attr_line_t("An OP ID can manually be set "
                                               "by performing an ")
                                       .append("UPDATE"_keyword)
                                       .append(" on a log vtable, such as ")
                                       .append("all_logs"_symbol));
                } else {
                    um.with_note(
                        "Operations are found in log files and none are loaded "
                        "right now");
                }

                this->gho_static_lines = um.to_attr_line().split_lines();
            }
        }

        if (y < this->gho_static_lines.size()) {
            value_out = this->gho_static_lines[y];
            return true;
        }

        return false;
    }

    if (y == 0) {
        auto sel = lv.get_selection().value_or(0_vl);
        if (sel < this->gho_src->tss_view->get_top()) {
            return true;
        }
        const auto& row = *this->gho_src->ts_time_order[sel];
        auto tr = row.or_value.otr_range;
        auto [lb, ub] = this->gho_src->get_time_bounds_for(sel);
        auto sel_begin_us = tr.tr_begin - lb;
        auto sel_end_us = tr.tr_end - lb;

        require(sel_begin_us >= 0us);
        require(sel_end_us >= 0us);

        auto [height, width] = lv.get_dimensions();
        if (width <= CHART_INDENT) {
            return true;
        }

        value_out.append("   Duration   "_h1)
            .append("|", VC_GRAPHIC.value(NCACS_VLINE))
            .append(" ")
            .append("\u2718"_error)
            .append("\u25b2"_warning)
            .append(" ")
            .append("|", VC_GRAPHIC.value(NCACS_VLINE))
            .append(" ")
            .append("Item"_h1);
        auto line_width = CHART_INDENT;
        auto mark_width = (double) (width - line_width);
        double span = (ub - lb).count();
        auto us_per_ch
            = std::chrono::microseconds{(int64_t) ceil(span / mark_width)};
        require(us_per_ch > 0us);
        auto us_per_inc = us_per_ch * 10;
        auto lr = line_range{
            static_cast<int>(CHART_INDENT + floor(sel_begin_us / us_per_ch)),
            static_cast<int>(CHART_INDENT + ceil(sel_end_us / us_per_ch)),
            line_range::unit::codepoint,
        };
        if (lr.lr_start == lr.lr_end) {
            lr.lr_end += 1;
        }
        if (lr.lr_end > width) {
            lr.lr_end = -1;
        }
        require(lr.lr_start >= 0);
        value_out.get_attrs().emplace_back(
            lr, VC_ROLE.value(role_t::VCR_CURSOR_LINE));
        auto total_us = std::chrono::microseconds{0};
        std::vector<std::string> durations;
        auto remaining_width = mark_width - 10;
        auto max_width = size_t{0};
        while (remaining_width > 0) {
            total_us += us_per_inc;
            auto dur = humanize::time::duration::from_tv(to_timeval(total_us));
            if (us_per_inc > 24 * 1h) {
                dur.with_resolution(24 * 1h);
            } else if (us_per_inc > 1h) {
                dur.with_resolution(1h);
            } else if (us_per_inc > 1min) {
                dur.with_resolution(1min);
            } else if (us_per_inc > 2s) {
                dur.with_resolution(1s);
            }
            durations.emplace_back(dur.to_string());
            max_width = std::max(durations.back().size(), max_width);
            remaining_width -= 10;
        }
        for (auto& label : durations) {
            line_width += 10;
            value_out.pad_to(line_width)
                .append("|", VC_GRAPHIC.value(NCACS_VLINE))
                .append(max_width - label.size(), ' ')
                .append(label);
        }

        auto hdr_attrs = text_attrs::with_underline();
        value_out.with_attr_for_all(VC_STYLE.value(hdr_attrs))
            .with_attr_for_all(VC_ROLE.value(role_t::VCR_STATUS_INFO));

        return true;
    }

    auto& tc = dynamic_cast<textview_curses&>(const_cast<listview_curses&>(lv));
    const auto& sticky_bv = tc.get_bookmarks()[&textview_curses::BM_STICKY];
    auto top = lv.get_top();
    auto sticky_range = sticky_bv.equal_range(0_vl, top);
    auto sticky_index = y - 1;
    if (sticky_index < static_cast<int>(
            std::distance(sticky_range.first, sticky_range.second)))
    {
        auto iter = std::next(sticky_range.first, sticky_index);
        tc.textview_value_for_row(*iter, value_out);
        value_out.with_attr_for_all(VC_ROLE.value(role_t::VCR_STATUS));
        auto next_iter = std::next(iter);
        if (next_iter == sticky_range.second) {
            value_out.with_attr_for_all(
                VC_STYLE.value(text_attrs::with_underline()));
        }
        return true;
    }

    return false;
}
void
timeline_header_overlay::list_value_for_overlay(
    const listview_curses& lv,
    vis_line_t line,
    std::vector<attr_line_t>& value_out)
{
    if (!this->gho_show_details) {
        return;
    }

    if (lv.get_selection() != line) {
        return;
    }

    if (line >= this->gho_src->ts_time_order.size()) {
        return;
    }

    const auto& row = *this->gho_src->ts_time_order[line];

    if (row.or_value.otr_sub_ops.size() <= 1) {
        return;
    }

    auto width = lv.get_dimensions().second;

    if (width < 37) {
        return;
    }

    width -= 37;
    double span = row.or_value.otr_range.duration().count();
    double per_ch = span / (double) width;

    for (const auto& sub : row.or_value.otr_sub_ops) {
        value_out.resize(value_out.size() + 1);

        auto& al = value_out.back();
        auto& attrs = al.get_attrs();
        auto total_msgs = sub.ostr_level_stats.lls_total_count;
        auto duration = sub.ostr_range.tr_end - sub.ostr_range.tr_begin;
        auto duration_str = fmt::format(
            FMT_STRING(" {: >13}"),
            humanize::time::duration::from_tv(to_timeval(duration))
                .to_string());
        al.pad_to(14)
            .append(duration_str, VC_ROLE.value(role_t::VCR_OFFSET_TIME))
            .append(" ")
            .append(lnav::roles::error(humanize::sparkline(
                sub.ostr_level_stats.lls_error_count, total_msgs)))
            .append(lnav::roles::warning(humanize::sparkline(
                sub.ostr_level_stats.lls_warning_count, total_msgs)))
            .append(" ")
            .append(lnav::roles::identifier(sub.ostr_subid.to_string()))
            .append(row.or_max_subid_width
                        - sub.ostr_subid.utf8_length().unwrapOr(
                            row.or_max_subid_width),
                    ' ')
            .append(sub.ostr_description);
        al.with_attr_for_all(VC_ROLE.value(role_t::VCR_COMMENT));

        auto start_diff = (double) (sub.ostr_range.tr_begin
                                    - row.or_value.otr_range.tr_begin)
                              .count();
        auto end_diff
            = (double) (sub.ostr_range.tr_end - row.or_value.otr_range.tr_begin)
                  .count();

        auto lr = line_range{
            (int) (32 + (start_diff / per_ch)),
            (int) (32 + (end_diff / per_ch)),
            line_range::unit::codepoint,
        };

        if (lr.lr_start == lr.lr_end) {
            lr.lr_end += 1;
        }

        auto block_attrs = text_attrs::with_reverse();
        attrs.emplace_back(lr, VC_STYLE.value(block_attrs));
    }
    if (!value_out.empty()) {
        value_out.back().get_attrs().emplace_back(
            line_range{0, -1}, VC_STYLE.value(text_attrs::with_underline()));
    }
}

std::optional<attr_line_t>
timeline_header_overlay::list_header_for_overlay(const listview_curses& lv,
                                                 media_t media,
                                                 vis_line_t line)
{
    if (lv.get_overlay_selection()) {
        return attr_line_t("\u258C Sub-operations: Press ")
            .append("Esc"_hotkey)
            .append(" to exit this panel");
    }
    return attr_line_t("\u258C Sub-operations: Press ")
        .append("CTRL-]"_hotkey)
        .append(" to focus on this panel");
}

timeline_source::timeline_source(textview_curses& log_view,
                                 logfile_sub_source& lss,
                                 textview_curses& preview_view,
                                 plain_text_source& preview_source,
                                 statusview_curses& preview_status_view,
                                 timeline_status_source& preview_status_source)
    : ts_log_view(log_view), ts_lss(lss), ts_preview_view(preview_view),
      ts_preview_source(preview_source),
      ts_preview_status_view(preview_status_view),
      ts_preview_status_source(preview_status_source)
{
    this->tss_supports_filtering = true;
    this->ts_preview_view.set_overlay_source(&this->ts_preview_overlay);
}

std::optional<timeline_source::row_type>
timeline_source::row_type_from_string(const std::string& str)
{
    if (str == "logfile") {
        return row_type::logfile;
    }
    if (str == "thread") {
        return row_type::thread;
    }
    if (str == "opid") {
        return row_type::opid;
    }
    if (str == "tag") {
        return row_type::tag;
    }
    if (str == "partition") {
        return row_type::partition;
    }
    return std::nullopt;
}

const char*
timeline_source::row_type_to_string(row_type rt)
{
    switch (rt) {
        case row_type::logfile:
            return "logfile";
        case row_type::thread:
            return "thread";
        case row_type::opid:
            return "opid";
        case row_type::tag:
            return "tag";
        case row_type::partition:
            return "partition";
    }
    return "unknown";
}

void
timeline_source::set_row_type_visibility(row_type rt, bool visible)
{
    if (visible) {
        this->ts_hidden_row_types.erase(rt);
    } else {
        this->ts_hidden_row_types.insert(rt);
    }
}

bool
timeline_source::is_row_type_visible(row_type rt) const
{
    return this->ts_hidden_row_types.find(rt)
        == this->ts_hidden_row_types.end();
}

bool
timeline_source::list_input_handle_key(listview_curses& lv, const ncinput& ch)
{
    switch (ch.eff_text[0]) {
        case 'q':
        case KEY_ESCAPE: {
            if (this->ts_preview_focused) {
                this->ts_preview_focused = false;
                this->ts_preview_view.set_height(5_vl);
                this->ts_preview_status_view.set_enabled(
                    this->ts_preview_focused);
                this->tss_view->set_enabled(!this->ts_preview_focused);
                return true;
            }
            break;
        }
        case '\n':
        case '\r':
        case NCKEY_ENTER: {
            this->ts_preview_focused = !this->ts_preview_focused;
            this->ts_preview_status_view.set_enabled(this->ts_preview_focused);
            this->tss_view->set_enabled(!this->ts_preview_focused);
            if (this->ts_preview_focused) {
                auto height = this->tss_view->get_dimensions().first;

                if (height > 5) {
                    this->ts_preview_view.set_height(height / 2_vl);
                }
            } else {
                this->ts_preview_view.set_height(5_vl);
            }
            return true;
        }
    }
    if (this->ts_preview_focused) {
        return this->ts_preview_view.handle_key(ch);
    }

    return false;
}

bool
timeline_source::text_handle_mouse(
    textview_curses& tc,
    const listview_curses::display_line_content_t&,
    mouse_event& me)
{
    auto nci = ncinput{};
    if (me.is_double_click_in(mouse_button_t::BUTTON_LEFT, line_range{0, -1})) {
        nci.id = '\r';
        nci.eff_text[0] = '\r';
        this->list_input_handle_key(tc, nci);
    }

    return false;
}

std::pair<std::chrono::microseconds, std::chrono::microseconds>
timeline_source::get_time_bounds_for(int line)
{
    const auto low_index = this->tss_view->get_top();
    auto high_index
        = std::min(this->tss_view->get_bottom(),
                   vis_line_t((int) this->ts_time_order.size() - 1));
    if (high_index == low_index) {
        high_index = vis_line_t(this->ts_time_order.size() - 1);
    }
    const auto& low_row = *this->ts_time_order[low_index];
    const auto& high_row = *this->ts_time_order[high_index];
    auto low_us = low_row.or_value.otr_range.tr_begin;
    auto high_us = high_row.or_value.otr_range.tr_begin;

    auto duration = high_us - low_us;
    auto span_iter
        = std::upper_bound(TIME_SPANS.begin(), TIME_SPANS.end(), duration);
    if (span_iter == TIME_SPANS.end()) {
        --span_iter;
    }
    auto span_portion = *span_iter / 8;
    auto lb = low_us;
    lb = rounddown(lb, span_portion);
    auto ub = high_us;
    ub = roundup(ub, span_portion);

    ensure(lb <= ub);
    return {lb, ub};
}

size_t
timeline_source::text_line_count()
{
    return this->ts_time_order.size();
}

line_info
timeline_source::text_value_for_line(textview_curses& tc,
                                     int line,
                                     std::string& value_out,
                                     line_flags_t flags)
{
    if (!this->ts_rebuild_in_progress
        && line < (ssize_t) this->ts_time_order.size())
    {
        const auto& row = *this->ts_time_order[line];
        auto duration
            = row.or_value.otr_range.tr_end - row.or_value.otr_range.tr_begin;
        auto duration_str = fmt::format(
            FMT_STRING("{: >13}"),
            humanize::time::duration::from_tv(to_timeval(duration))
                .to_string());

        this->ts_rendered_line.clear();

        auto total_msgs = row.or_value.otr_level_stats.lls_total_count;
        auto truncated_name
            = attr_line_t::from_table_cell_content(row.or_name, MAX_OPID_WIDTH);
        auto truncated_desc = attr_line_t::from_table_cell_content(
            row.or_description, MAX_DESC_WIDTH);
        std::optional<ui_icon_t> icon;
        auto padding = 1;
        switch (row.or_type) {
            case row_type::logfile:
                icon = ui_icon_t::file;
                break;
            case row_type::thread:
                icon = ui_icon_t::thread;
                break;
            case row_type::opid:
                padding = 3;
                break;
            case row_type::tag:
                icon = ui_icon_t::tag;
                break;
            case row_type::partition:
                icon = ui_icon_t::partition;
                break;
        }
        if (this->ts_preview_hidden_row_types.count(row.or_type) > 0) {
            this->ts_rendered_line.append(
                "-",
                VC_STYLE.value(text_attrs{
                    lnav::enums::to_underlying(text_attrs::style::blink),
                    styling::color_unit::from_palette(
                        lnav::enums::to_underlying(ansi_color::red)),
                }));
        } else {
            this->ts_rendered_line.append(" ");
        }

        this->ts_rendered_line
            .append(duration_str, VC_ROLE.value(role_t::VCR_OFFSET_TIME))
            .append("  ")
            .append(lnav::roles::error(humanize::sparkline(
                row.or_value.otr_level_stats.lls_error_count, total_msgs)))
            .append(lnav::roles::warning(humanize::sparkline(
                row.or_value.otr_level_stats.lls_warning_count, total_msgs)))
            .append("  ")
            .append(icon)
            .append(padding, ' ')
            .append(lnav::roles::identifier(truncated_name))
            .append(
                this->ts_opid_width - truncated_name.utf8_length_or_length(),
                ' ')
            .append(" ")
            .append(truncated_desc);
        this->ts_rendered_line.with_attr_for_all(
            VC_ROLE.value(role_t::VCR_COMMENT));

        value_out = this->ts_rendered_line.get_string();
    }

    return {};
}

void
timeline_source::text_attrs_for_line(textview_curses& tc,
                                     int line,
                                     string_attrs_t& value_out)
{
    if (!this->ts_rebuild_in_progress
        && line < (ssize_t) this->ts_time_order.size())
    {
        const auto& row = *this->ts_time_order[line];

        value_out = this->ts_rendered_line.get_attrs();

        auto lr = line_range{-1, -1, line_range::unit::codepoint};
        auto [sel_lb, sel_ub]
            = this->get_time_bounds_for(tc.get_selection().value_or(0_vl));

        if (row.or_value.otr_range.tr_begin <= sel_ub
            && sel_lb <= row.or_value.otr_range.tr_end)
        {
            auto width = tc.get_dimensions().second;

            if (width > CHART_INDENT) {
                width -= CHART_INDENT;
                const double span = (sel_ub - sel_lb).count();
                auto us_per_ch = std::chrono::microseconds{
                    static_cast<int64_t>(ceil(span / (double) width))};

                if (row.or_value.otr_range.tr_begin <= sel_lb) {
                    lr.lr_start = CHART_INDENT;
                } else {
                    auto start_diff
                        = (row.or_value.otr_range.tr_begin - sel_lb);

                    lr.lr_start = CHART_INDENT + floor(start_diff / us_per_ch);
                }

                if (sel_ub < row.or_value.otr_range.tr_end) {
                    lr.lr_end = -1;
                } else {
                    auto end_diff = (row.or_value.otr_range.tr_end - sel_lb);

                    lr.lr_end = CHART_INDENT + ceil(end_diff / us_per_ch);
                    if (lr.lr_start == lr.lr_end) {
                        lr.lr_end += 1;
                    }
                }

                auto block_attrs = text_attrs::with_reverse();
                require(lr.lr_start >= 0);
                value_out.emplace_back(lr, VC_STYLE.value(block_attrs));
            }
        }
        auto alt_row_index = line % 4;
        if (alt_row_index == 2 || alt_row_index == 3) {
            value_out.emplace_back(line_range{0, -1},
                                   VC_ROLE.value(role_t::VCR_ALT_ROW));
        }
    }
}

size_t
timeline_source::text_size_for_line(textview_curses& tc,
                                    int line,
                                    text_sub_source::line_flags_t raw)
{
    return this->ts_total_width;
}

bool
timeline_source::rebuild_indexes()
{
    static auto op = lnav_operation{"timeline_rebuild"};

    auto op_guard = lnav_opid_guard::internal(op);
    auto& bm = this->tss_view->get_bookmarks();
    auto& bm_files = bm[&logfile_sub_source::BM_FILES];
    auto& bm_errs = bm[&textview_curses::BM_ERRORS];
    auto& bm_warns = bm[&textview_curses::BM_WARNINGS];
    auto& bm_meta = bm[&textview_curses::BM_META];
    auto& bm_parts = bm[&textview_curses::BM_PARTITION];

    this->ts_rebuild_in_progress = true;

    static const bookmark_type_t* PRESERVE_TYPES[] = {
        &textview_curses::BM_USER,
        &textview_curses::BM_STICKY,
    };
    for (const auto* bm_type : PRESERVE_TYPES) {
        auto& bv = bm[bm_type];
        for (const auto& vl : bv.bv_tree) {
            auto line = static_cast<size_t>(vl);
            if (line < this->ts_time_order.size()) {
                const auto& row = *this->ts_time_order[line];
                this->ts_pending_bookmarks.emplace_back(pending_bookmark{
                    row.or_type,
                    row.or_name.to_string(),
                    bm_type,
                });
            }
        }
        bv.clear();
    }

    bm.clear();

    this->ts_lower_bound = {};
    this->ts_upper_bound = {};
    this->ts_opid_width = 0;
    this->ts_total_width = 0;
    this->ts_filtered_count = 0;
    this->ts_active_opids.clear();
    this->ts_descriptions.clear();
    this->ts_subid_map.clear();
    this->ts_allocator.reset();
    this->ts_preview_source.clear();
    this->ts_preview_rows.clear();
    this->ts_preview_status_source.get_description().clear();

    auto min_log_time_tv_opt = this->get_min_row_time();
    auto max_log_time_tv_opt = this->get_max_row_time();
    std::optional<std::chrono::microseconds> min_log_time_opt;
    std::optional<std::chrono::microseconds> max_log_time_opt;
    auto max_desc_width = size_t{0};

    if (min_log_time_tv_opt) {
        min_log_time_opt = to_us(min_log_time_tv_opt.value());
    }
    if (max_log_time_tv_opt) {
        max_log_time_opt = to_us(max_log_time_tv_opt.value());
    }

    log_info("building opid table");
    auto last_log_time = std::chrono::microseconds{};
    tlx::btree_map<std::chrono::microseconds, std::string> part_map;
    for (const auto& [index, ld] : lnav::itertools::enumerate(this->ts_lss)) {
        if (ld->get_file_ptr() == nullptr) {
            continue;
        }
        if (!ld->is_visible()) {
            continue;
        }

        auto* lf = ld->get_file_ptr();
        lf->enable_cache();

        const auto& mark_meta = lf->get_bookmark_metadata();
        {
            for (const auto& [line_num, line_meta] : mark_meta) {
                const auto ll = std::next(lf->begin(), line_num);
                if (!line_meta.bm_name.empty()) {
                    part_map.insert2(ll->get_time<std::chrono::microseconds>(),
                                     line_meta.bm_name);
                }
                for (const auto& entry : line_meta.bm_tags) {
                    auto line_time = ll->get_time<std::chrono::microseconds>();
                    auto tag_key = fmt::format(FMT_STRING("{}@{}:{}"),
                                               entry.te_tag,
                                               lf->get_unique_path(),
                                               line_time.count());
                    auto tag_key_sf
                        = string_fragment::from_str(tag_key).to_owned(
                            this->ts_allocator);
                    auto tag_name_sf = string_fragment::from_str(entry.te_tag)
                                           .to_owned(this->ts_allocator);
                    auto tag_otr = opid_time_range{};
                    tag_otr.otr_range.tr_begin = line_time;
                    tag_otr.otr_range.tr_end = line_time;
                    tag_otr.otr_level_stats.update_msg_count(
                        ll->get_msg_level());
                    this->ts_active_opids.emplace(
                        tag_key_sf,
                        opid_row{
                            row_type::tag,
                            tag_name_sf,
                            tag_otr,
                            string_fragment::invalid(),
                        });
                }
            }
        }

        auto path = string_fragment::from_str(lf->get_unique_path())
                        .to_owned(this->ts_allocator);
        auto lf_otr = opid_time_range{};
        lf_otr.otr_range = lf->get_content_time_range();
        lf_otr.otr_level_stats = lf->get_level_stats();
        if (lf_otr.otr_range.tr_end > last_log_time) {
            last_log_time = lf_otr.otr_range.tr_end;
        }
        auto lf_row = opid_row{
            row_type::logfile,
            path,
            lf_otr,
            string_fragment::invalid(),
        };
        lf_row.or_logfile = lf;
        this->ts_active_opids.emplace(path, lf_row);

        {
            auto r_tid_map = lf->get_thread_ids().readAccess();

            for (const auto& [tid_sf, tid_meta] : r_tid_map->ltis_tid_ranges) {
                auto active_iter = this->ts_active_opids.find(tid_sf);
                if (active_iter == this->ts_active_opids.end()) {
                    auto tid = tid_sf.to_owned(this->ts_allocator);
                    auto tid_otr = opid_time_range{};
                    tid_otr.otr_range = tid_meta.titr_range;
                    tid_otr.otr_level_stats = tid_meta.titr_level_stats;
                    this->ts_active_opids.emplace(
                        tid,
                        opid_row{
                            row_type::thread,
                            tid,
                            tid_otr,
                            string_fragment::invalid(),
                        });
                } else {
                    active_iter->second.or_value.otr_range
                        |= tid_meta.titr_range;
                }
            }
        }

        auto format = lf->get_format();
        safe::ReadAccess<logfile::safe_opid_state> r_opid_map(
            ld->get_file_ptr()->get_opids());
        for (const auto& pair : r_opid_map->los_opid_ranges) {
            const opid_time_range& otr = pair.second;
            auto active_iter = this->ts_active_opids.find(pair.first);
            if (active_iter == this->ts_active_opids.end()) {
                auto opid = pair.first.to_owned(this->ts_allocator);
                auto active_emp_res = this->ts_active_opids.emplace(
                    opid,
                    opid_row{
                        row_type::opid,
                        opid,
                        otr,
                        string_fragment::invalid(),
                    });
                active_iter = active_emp_res.first;
            } else {
                active_iter->second.or_value |= otr;
            }

            opid_row& row = active_iter->second;
            for (auto& sub : row.or_value.otr_sub_ops) {
                auto subid_iter = this->ts_subid_map.find(sub.ostr_subid);

                if (subid_iter == this->ts_subid_map.end()) {
                    subid_iter = this->ts_subid_map
                                     .emplace(sub.ostr_subid.to_owned(
                                                  this->ts_allocator),
                                              true)
                                     .first;
                }
                sub.ostr_subid = subid_iter->first;
                if (sub.ostr_subid.length() > row.or_max_subid_width) {
                    row.or_max_subid_width = sub.ostr_subid.length();
                }
            }

            if (otr.otr_description.lod_index) {
                auto desc_id = otr.otr_description.lod_index.value();
                auto desc_def_iter
                    = format->lf_opid_description_def_vec->at(desc_id);

                auto desc_key
                    = opid_description_def_key{format->get_name(), desc_id};
                auto desc_defs_opt
                    = row.or_description_defs.odd_defs.value_for(desc_key);
                if (!desc_defs_opt) {
                    row.or_description_defs.odd_defs.insert(desc_key,
                                                            *desc_def_iter);
                }

                if (!row.or_description_begin
                    || otr.otr_range.tr_begin
                        < row.or_description_begin.value())
                {
                    row.or_description_begin = otr.otr_range.tr_begin;
                    row.or_description_def_key = desc_key;
                    row.or_description_value = otr.otr_description.lod_elements;
                }
            } else if (!otr.otr_description.lod_elements.empty()) {
                auto desc_sf = string_fragment::from_str(
                    otr.otr_description.lod_elements.values().front());
                row.or_description = desc_sf.to_owned(this->ts_allocator);
            }
            row.or_value.otr_description.lod_elements.clear();
        }

        if (this->ts_index_progress) {
            switch (this->ts_index_progress(
                progress_t{index, this->ts_lss.file_count()}))
            {
                case lnav::progress_result_t::ok:
                    break;
                case lnav::progress_result_t::interrupt:
                    log_debug("timeline rebuild interrupted");
                    this->ts_rebuild_in_progress = false;
                    return false;
            }
        }
    }
    if (this->ts_index_progress) {
        this->ts_index_progress(std::nullopt);
    }

    {
        static const auto START_RE = lnav::pcre2pp::code::from_const(
            R"(^(?:start(?:ed)?|begin)|\b(?:start(?:ed)?|begin)$)",
            PCRE2_CASELESS);

        std::vector<opid_row*> start_tags;
        for (auto& pair : this->ts_active_opids) {
            if (pair.second.or_type != row_type::tag) {
                continue;
            }
            if (START_RE.find_in(pair.second.or_name).ignore_error()) {
                start_tags.emplace_back(&pair.second);
            }
        }
        std::stable_sort(start_tags.begin(),
                         start_tags.end(),
                         [](const auto* lhs, const auto* rhs) {
                             if (lhs->or_name == rhs->or_name) {
                                 return lhs->or_value.otr_range.tr_begin
                                     < rhs->or_value.otr_range.tr_begin;
                             }
                             return lhs->or_name < rhs->or_name;
                         });
        for (size_t i = 0; i < start_tags.size(); i++) {
            if (i + 1 < start_tags.size()
                && start_tags[i]->or_name == start_tags[i + 1]->or_name)
            {
                start_tags[i]->or_value.otr_range.tr_end
                    = start_tags[i + 1]->or_value.otr_range.tr_begin - 1us;
            } else {
                start_tags[i]->or_value.otr_range.tr_end = last_log_time;
            }
        }
    }

    for (auto part_iter = part_map.begin(); part_iter != part_map.end();
         ++part_iter)
    {
        auto next_iter = std::next(part_iter);
        auto part_name_sf = string_fragment::from_str(part_iter->second)
                                .to_owned(this->ts_allocator);
        auto part_otr = opid_time_range{};
        part_otr.otr_range.tr_begin = part_iter->first;
        if (next_iter != part_map.end()) {
            part_otr.otr_range.tr_end = next_iter->first;
        } else {
            part_otr.otr_range.tr_end = last_log_time;
        }
        this->ts_active_opids.emplace(part_name_sf,
                                      opid_row{
                                          row_type::partition,
                                          part_name_sf,
                                          part_otr,
                                          string_fragment::invalid(),
                                      });
    }

    log_info("active opids: %zu", this->ts_active_opids.size());

    size_t filtered_in_count = 0;
    for (const auto& filt : this->tss_filters) {
        if (!filt->is_enabled()) {
            continue;
        }
        if (filt->get_type() == text_filter::INCLUDE) {
            filtered_in_count += 1;
        }
    }
    this->ts_filter_hits = {};

    this->ts_time_order.clear();
    this->ts_time_order.reserve(this->ts_active_opids.size());
    for (auto& pair : this->ts_active_opids) {
        opid_row& row = pair.second;
        opid_time_range& otr = pair.second.or_value;
        std::string full_desc;
        if (row.or_description.empty()) {
            const auto& desc_defs = row.or_description_defs.odd_defs;
            if (row.or_description_begin) {
                auto desc_def_opt
                    = desc_defs.value_for(row.or_description_def_key);
                if (desc_def_opt) {
                    full_desc = desc_def_opt.value()->to_string(
                        row.or_description_value);
                }
            }
            row.or_description_begin = std::nullopt;
            auto full_desc_sf = string_fragment::from_str(full_desc);
            auto desc_sf_iter = this->ts_descriptions.find(full_desc_sf);
            if (desc_sf_iter == this->ts_descriptions.end()) {
                full_desc_sf = string_fragment::from_str(full_desc).to_owned(
                    this->ts_allocator);
                this->ts_descriptions.insert(full_desc_sf);
            } else {
                full_desc_sf = *desc_sf_iter;
            }
            pair.second.or_description = full_desc_sf;
        } else {
            full_desc += pair.second.or_description;
        }

        if (!this->is_row_type_visible(row.or_type)) {
            this->ts_filtered_count += 1;
            continue;
        }

        shared_buffer sb_opid;
        shared_buffer_ref sbr_opid;
        sbr_opid.share(
            sb_opid, pair.second.or_name.data(), pair.second.or_name.length());
        shared_buffer sb_desc;
        shared_buffer_ref sbr_desc;
        sbr_desc.share(sb_desc, full_desc.c_str(), full_desc.length());
        if (this->tss_apply_filters) {
            auto filtered_in = false;
            auto filtered_out = false;
            for (const auto& filt : this->tss_filters) {
                if (!filt->is_enabled()) {
                    continue;
                }
                for (const auto sbr : {&sbr_opid, &sbr_desc}) {
                    if (filt->matches(std::nullopt, *sbr)) {
                        this->ts_filter_hits[filt->get_index()] += 1;
                        switch (filt->get_type()) {
                            case text_filter::INCLUDE:
                                filtered_in = true;
                                break;
                            case text_filter::EXCLUDE:
                                filtered_out = true;
                                break;
                            default:
                                break;
                        }
                    }
                }
            }

            if (min_log_time_opt
                && otr.otr_range.tr_end < min_log_time_opt.value())
            {
                filtered_out = true;
            }
            if (max_log_time_opt
                && max_log_time_opt.value() < otr.otr_range.tr_begin)
            {
                filtered_out = true;
            }

            if ((filtered_in_count > 0 && !filtered_in) || filtered_out) {
                this->ts_filtered_count += 1;
                continue;
            }
        }

        if (pair.second.or_name.column_width() > this->ts_opid_width) {
            this->ts_opid_width = pair.second.or_name.column_width();
        }
        if (full_desc.size() > max_desc_width) {
            max_desc_width = full_desc.size();
        }

        if (this->ts_lower_bound == 0us
            || pair.second.or_value.otr_range.tr_begin < this->ts_lower_bound)
        {
            this->ts_lower_bound = pair.second.or_value.otr_range.tr_begin;
        }
        if (this->ts_upper_bound == 0us
            || this->ts_upper_bound < pair.second.or_value.otr_range.tr_end)
        {
            this->ts_upper_bound = pair.second.or_value.otr_range.tr_end;
        }

        this->ts_time_order.emplace_back(&pair.second);
    }
    std::stable_sort(
        this->ts_time_order.begin(),
        this->ts_time_order.end(),
        [](const auto* lhs, const auto* rhs) { return *lhs < *rhs; });
    for (size_t lpc = 0; lpc < this->ts_time_order.size(); lpc++) {
        const auto& row = *this->ts_time_order[lpc];
        if (row.or_type == row_type::logfile) {
            bm_files.insert_once(vis_line_t(lpc));
        } else if (row.or_type == row_type::tag) {
            bm_meta.insert_once(vis_line_t(lpc));
        } else if (row.or_type == row_type::partition) {
            bm_parts.insert_once(vis_line_t(lpc));
        }
        if (row.or_value.otr_level_stats.lls_error_count > 0) {
            bm_errs.insert_once(vis_line_t(lpc));
        }
        if (row.or_value.otr_level_stats.lls_warning_count > 0) {
            bm_warns.insert_once(vis_line_t(lpc));
        }
    }

    this->ts_opid_width = std::min(this->ts_opid_width, MAX_OPID_WIDTH);
    this->ts_total_width
        = std::max<size_t>(22 + this->ts_opid_width + max_desc_width,
                           1 + 16 + 5 + 8 + 5 + 16 + 1 /* header */);

    this->apply_pending_bookmarks();

    this->tss_view->set_needs_update();
    this->ts_rebuild_in_progress = false;

    ensure(this->ts_time_order.empty() || this->ts_opid_width > 0);

    return true;
}

std::optional<vis_line_t>
timeline_source::row_for_time(timeval time_bucket)
{
    auto time_bucket_us = to_us(time_bucket);
    auto iter = this->ts_time_order.begin();
    while (true) {
        if (iter == this->ts_time_order.end()) {
            return std::nullopt;
        }

        if ((*iter)->or_value.otr_range.contains_inclusive(time_bucket_us)) {
            break;
        }
        ++iter;
    }

    auto closest_iter = iter;
    auto closest_diff = time_bucket_us - (*iter)->or_value.otr_range.tr_begin;
    for (; iter != this->ts_time_order.end(); ++iter) {
        if (time_bucket_us < (*iter)->or_value.otr_range.tr_begin) {
            break;
        }
        if (!(*iter)->or_value.otr_range.contains_inclusive(time_bucket_us)) {
            continue;
        }

        auto diff = time_bucket_us - (*iter)->or_value.otr_range.tr_begin;
        if (diff < closest_diff) {
            closest_iter = iter;
            closest_diff = diff;
        }

        for (const auto& sub : (*iter)->or_value.otr_sub_ops) {
            if (!sub.ostr_range.contains_inclusive(time_bucket_us)) {
                continue;
            }

            diff = time_bucket_us - sub.ostr_range.tr_begin;
            if (diff < closest_diff) {
                closest_iter = iter;
                closest_diff = diff;
            }
        }
    }

    return vis_line_t(std::distance(this->ts_time_order.begin(), closest_iter));
}

std::optional<vis_line_t>
timeline_source::row_for(const row_info& ri)
{
    auto vl_opt = this->ts_lss.row_for(ri);
    if (!vl_opt) {
        return this->row_for_time(ri.ri_time);
    }

    auto vl = vl_opt.value();
    auto win = this->ts_lss.window_at(vl);
    for (const auto& msg_line : *win) {
        const auto& lvv = msg_line.get_values();

        if (lvv.lvv_opid_value) {
            auto opid_iter
                = this->ts_active_opids.find(lvv.lvv_opid_value.value());
            if (opid_iter != this->ts_active_opids.end()) {
                for (const auto& [index, oprow] :
                     lnav::itertools::enumerate(this->ts_time_order))
                {
                    if (oprow == &opid_iter->second) {
                        return vis_line_t(index);
                    }
                }
            }
        }
    }

    return this->row_for_time(ri.ri_time);
}

std::optional<text_time_translator::row_info>
timeline_source::time_for_row(vis_line_t row)
{
    if (row >= this->ts_time_order.size()) {
        return std::nullopt;
    }

    const auto& otr = this->ts_time_order[row]->or_value;

    if (this->tss_view->get_selection() == row) {
        auto ov_sel = this->tss_view->get_overlay_selection();

        if (ov_sel && ov_sel.value() < otr.otr_sub_ops.size()) {
            return row_info{
                to_timeval(otr.otr_sub_ops[ov_sel.value()].ostr_range.tr_begin),
                row,
            };
        }
    }

    auto preview_selection = this->ts_preview_view.get_selection();
    if (!preview_selection) {
        return std::nullopt;
    }
    if (preview_selection < this->ts_preview_rows.size()) {
        return this->ts_preview_rows[preview_selection.value()];
    }

    return row_info{
        to_timeval(otr.otr_range.tr_begin),
        row,
    };
}

size_t
timeline_source::text_line_width(textview_curses& curses)
{
    return this->ts_total_width;
}

void
timeline_source::text_selection_changed(textview_curses& tc)
{
    static const size_t MAX_PREVIEW_LINES = 200;

    auto sel = tc.get_selection();

    this->ts_preview_source.clear();
    this->ts_preview_rows.clear();
    if (!sel || sel.value() >= this->ts_time_order.size()) {
        return;
    }

    const auto& row = *this->ts_time_order[sel.value()];
    auto low_us = row.or_value.otr_range.tr_begin;
    auto high_us = row.or_value.otr_range.tr_end;
    auto id_sf = row.or_name;
    auto level_stats = row.or_value.otr_level_stats;
    auto ov_sel = tc.get_overlay_selection();
    if (ov_sel) {
        const auto& sub = row.or_value.otr_sub_ops[ov_sel.value()];
        id_sf = sub.ostr_subid;
        low_us = sub.ostr_range.tr_begin;
        high_us = sub.ostr_range.tr_end;
        level_stats = sub.ostr_level_stats;
    }
    high_us += 1s;
    auto low_vl = this->ts_lss.row_for_time(to_timeval(low_us));
    auto high_vl = this->ts_lss.row_for_time(to_timeval(high_us))
                       .value_or(vis_line_t(this->ts_lss.text_line_count()));

    if (!low_vl) {
        return;
    }

    auto preview_content = attr_line_t();
    auto msgs_remaining = size_t{MAX_PREVIEW_LINES};
    auto win = this->ts_lss.window_at(low_vl.value(), high_vl);
    auto id_bloom_bits = row.or_name.bloom_bits();
    auto msg_count = 0;
    for (const auto& msg_line : *win) {
        switch (row.or_type) {
            case row_type::logfile:
                if (msg_line.get_file_ptr() != row.or_logfile) {
                    continue;
                }
                break;
            case row_type::thread: {
                if (!msg_line.get_logline().match_bloom_bits(id_bloom_bits)) {
                    continue;
                }
                const auto& lvv = msg_line.get_values();
                if (!lvv.lvv_thread_id_value) {
                    continue;
                }
                auto tid_sf = lvv.lvv_thread_id_value.value();
                if (!(tid_sf == row.or_name)) {
                    continue;
                }
                break;
            }
            case row_type::opid: {
                if (!msg_line.get_logline().match_bloom_bits(id_bloom_bits)) {
                    continue;
                }

                const auto& lvv = msg_line.get_values();
                if (!lvv.lvv_opid_value) {
                    continue;
                }
                auto opid_sf = lvv.lvv_opid_value.value();

                if (!(opid_sf == row.or_name)) {
                    continue;
                }
                break;
            }
            case row_type::tag: {
                const auto& bm
                    = msg_line.get_file_ptr()->get_bookmark_metadata();
                auto bm_iter = bm.find(msg_line.get_file_line_number());
                if (bm_iter == bm.end()) {
                    continue;
                }
                auto tag_name = row.or_name.to_string();
                if (!(bm_iter->second.bm_tags
                      | lnav::itertools::find(tag_name)))
                {
                    continue;
                }
                break;
            }
            case row_type::partition:
                break;
        }

        for (size_t lpc = 0; lpc < msg_line.get_line_count(); lpc++) {
            auto vl = msg_line.get_vis_line() + vis_line_t(lpc);
            auto cl = this->ts_lss.at(vl);
            auto row_al = attr_line_t();
            this->ts_log_view.textview_value_for_row(vl, row_al);
            preview_content.append(row_al).append("\n");
            this->ts_preview_rows.emplace_back(
                msg_line.get_logline().get_timeval(), cl);
            ++cl;
        }
        msg_count += 1;
        msgs_remaining -= 1;
        if (msgs_remaining == 0) {
            break;
        }
    }

    this->ts_preview_source.replace_with(preview_content);
    this->ts_preview_view.set_selection(0_vl);
    this->ts_preview_status_source.get_description().set_value(
        " ID %.*s", id_sf.length(), id_sf.data());
    auto err_count = level_stats.lls_error_count;
    if (err_count == 0) {
        this->ts_preview_status_source
            .statusview_value_for_field(timeline_status_source::TSF_ERRORS)
            .set_value("");
    } else {
        this->ts_preview_status_source
            .statusview_value_for_field(timeline_status_source::TSF_ERRORS)
            .set_value("\u2022 %'d", err_count);
    }
    auto warn_count = level_stats.lls_warning_count;
    if (warn_count == 0) {
        this->ts_preview_status_source
            .statusview_value_for_field(timeline_status_source::TSF_WARNINGS)
            .set_value("");
    } else {
        this->ts_preview_status_source
            .statusview_value_for_field(timeline_status_source::TSF_WARNINGS)
            .set_value("\u2022 %'d", warn_count);
    }
    if (msg_count < level_stats.lls_total_count) {
        this->ts_preview_status_source
            .statusview_value_for_field(timeline_status_source::TSF_TOTAL)
            .set_value(
                "%'d of %'d messages ", msg_count, level_stats.lls_total_count);
    } else {
        this->ts_preview_status_source
            .statusview_value_for_field(timeline_status_source::TSF_TOTAL)
            .set_value("%'d messages ", level_stats.lls_total_count);
    }
    this->ts_preview_status_view.set_needs_update();
}

void
timeline_source::text_filters_changed()
{
    this->rebuild_indexes();
    this->tss_view->reload_data();
    this->tss_view->redo_search();
}

void
timeline_source::clear_preview()
{
    text_sub_source::clear_preview();
    this->ts_preview_hidden_row_types.clear();
}

void
timeline_source::add_commands_for_session(
    const std::function<void(const std::string&)>& receiver)
{
    text_sub_source::add_commands_for_session(receiver);

    for (const auto& rt : this->ts_hidden_row_types) {
        receiver(fmt::format(FMT_STRING("hide-in-timeline {}"),
                             row_type_to_string(rt)));
    }
}

void
timeline_source::apply_pending_bookmarks()
{
    if (this->ts_pending_bookmarks.empty()) {
        return;
    }

    auto* tc = this->tss_view;
    for (const auto& pb : this->ts_pending_bookmarks) {
        auto row_name_sf = string_fragment::from_str(pb.pb_row_name);
        for (size_t lpc = 0; lpc < this->ts_time_order.size(); lpc++) {
            const auto& row = *this->ts_time_order[lpc];
            if (row.or_name == row_name_sf && row.or_type == pb.pb_row_type) {
                tc->set_user_mark(pb.pb_mark_type, vis_line_t(lpc), true);
                break;
            }
        }
    }
    this->ts_pending_bookmarks.clear();
}

int
timeline_source::get_filtered_count() const
{
    return this->ts_filtered_count;
}

int
timeline_source::get_filtered_count_for(size_t filter_index) const
{
    return this->ts_filter_hits[filter_index];
}

static const std::vector<breadcrumb::possibility>&
timestamp_poss()
{
    const static std::vector<breadcrumb::possibility> retval = {
        breadcrumb::possibility{"-1 day"},
        breadcrumb::possibility{"-1h"},
        breadcrumb::possibility{"-30m"},
        breadcrumb::possibility{"-15m"},
        breadcrumb::possibility{"-5m"},
        breadcrumb::possibility{"-1m"},
        breadcrumb::possibility{"+1m"},
        breadcrumb::possibility{"+5m"},
        breadcrumb::possibility{"+15m"},
        breadcrumb::possibility{"+30m"},
        breadcrumb::possibility{"+1h"},
        breadcrumb::possibility{"+1 day"},
    };

    return retval;
}

void
timeline_source::text_crumbs_for_line(int line,
                                      std::vector<breadcrumb::crumb>& crumbs)
{
    text_sub_source::text_crumbs_for_line(line, crumbs);

    if (line >= this->ts_time_order.size()) {
        return;
    }

    const auto& row = *this->ts_time_order[line];
    char ts[64];

    sql_strftime(ts, sizeof(ts), row.or_value.otr_range.tr_begin, 'T');

    crumbs.emplace_back(std::string(ts),
                        timestamp_poss,
                        [ec = this->ts_exec_context](const auto& ts) {
                            auto cmd
                                = fmt::format(FMT_STRING(":goto {}"),
                                              ts.template get<std::string>());
                            ec->execute(INTERNAL_SRC_LOC, cmd);
                        });
    crumbs.back().c_expected_input
        = breadcrumb::crumb::expected_input_t::anything;
    crumbs.back().c_search_placeholder = "(Enter an absolute or relative time)";
}
