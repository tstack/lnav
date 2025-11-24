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
#include "crashd.client.hh"
#include "lnav_util.hh"
#include "logline_window.hh"
#include "md4cpp.hh"
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
static constexpr int CHART_INDENT = 22;

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

    if (this->gho_src->gs_time_order.empty()) {
        if (y == 0) {
            this->gho_static_lines.clear();

            if (this->gho_src->gs_filtered_count > 0) {
                auto um = lnav::console::user_message::warning(
                    attr_line_t()
                        .append(lnav::roles::number(
                            fmt::to_string(this->gho_src->gs_filtered_count)))
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

                auto& fs = this->gho_src->gs_lss.get_filters();
                for (const auto& filt : fs) {
                    auto hits = this->gho_src->gs_lss.get_filtered_count_for(
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
                if (this->gho_src->gs_lss.size() > 0) {
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

    if (y > 0) {
        return false;
    }

    auto sel = lv.get_selection().value_or(0_vl);
    if (sel < this->gho_src->tss_view->get_top()) {
        return true;
    }
    const auto& row = *this->gho_src->gs_time_order[sel];
    auto tr = row.or_value.otr_range;
    auto [lb, ub] = this->gho_src->get_time_bounds_for(sel);
    auto sel_begin_us = tr.tr_begin - lb;
    auto sel_end_us = tr.tr_end - lb;

    require(sel_begin_us > 0us);
    require(sel_end_us > 0us);

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
        .append(" Operation"_h1);
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
    value_out.get_attrs().emplace_back(lr,
                                       VC_ROLE.value(role_t::VCR_CURSOR_LINE));
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

    if (line >= this->gho_src->gs_time_order.size()) {
        return;
    }

    const auto& row = *this->gho_src->gs_time_order[line];

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
    : gs_log_view(log_view), gs_lss(lss), gs_preview_view(preview_view),
      gs_preview_source(preview_source),
      gs_preview_status_view(preview_status_view),
      gs_preview_status_source(preview_status_source)
{
    this->tss_supports_filtering = true;
    this->gs_preview_view.set_overlay_source(&this->gs_preview_overlay);
}

bool
timeline_source::list_input_handle_key(listview_curses& lv, const ncinput& ch)
{
    switch (ch.eff_text[0]) {
        case 'q':
        case KEY_ESCAPE: {
            if (this->gs_preview_focused) {
                this->gs_preview_focused = false;
                this->gs_preview_view.set_height(5_vl);
                this->gs_preview_status_view.set_enabled(
                    this->gs_preview_focused);
                this->tss_view->set_enabled(!this->gs_preview_focused);
                return true;
            }
            break;
        }
        case '\n':
        case '\r':
        case NCKEY_ENTER: {
            this->gs_preview_focused = !this->gs_preview_focused;
            this->gs_preview_status_view.set_enabled(this->gs_preview_focused);
            this->tss_view->set_enabled(!this->gs_preview_focused);
            if (this->gs_preview_focused) {
                auto height = this->tss_view->get_dimensions().first;

                if (height > 5) {
                    this->gs_preview_view.set_height(height / 2_vl);
                }
            } else {
                this->gs_preview_view.set_height(5_vl);
            }
            return true;
        }
    }
    if (this->gs_preview_focused) {
        return this->gs_preview_view.handle_key(ch);
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
                   vis_line_t((int) this->gs_time_order.size() - 1));
    if (high_index == low_index) {
        high_index = vis_line_t(this->gs_time_order.size() - 1);
    }
    const auto& low_row = *this->gs_time_order[low_index];
    const auto& high_row = *this->gs_time_order[high_index];
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
    return this->gs_time_order.size();
}

line_info
timeline_source::text_value_for_line(textview_curses& tc,
                                     int line,
                                     std::string& value_out,
                                     line_flags_t flags)
{
    if (!this->ts_rebuild_in_progress
        && line < (ssize_t) this->gs_time_order.size())
    {
        const auto& row = *this->gs_time_order[line];
        auto duration
            = row.or_value.otr_range.tr_end - row.or_value.otr_range.tr_begin;
        auto duration_str = fmt::format(
            FMT_STRING(" {: >13}"),
            humanize::time::duration::from_tv(to_timeval(duration))
                .to_string());

        this->gs_rendered_line.clear();

        auto total_msgs = row.or_value.otr_level_stats.lls_total_count;
        auto truncated_name
            = attr_line_t::from_table_cell_content(row.or_name, MAX_OPID_WIDTH);
        auto truncated_desc = attr_line_t::from_table_cell_content(
            row.or_description, MAX_DESC_WIDTH);
        this->gs_rendered_line
            .append(duration_str, VC_ROLE.value(role_t::VCR_OFFSET_TIME))
            .append("  ")
            .append(lnav::roles::error(humanize::sparkline(
                row.or_value.otr_level_stats.lls_error_count, total_msgs)))
            .append(lnav::roles::warning(humanize::sparkline(
                row.or_value.otr_level_stats.lls_warning_count, total_msgs)))
            .append("  ")
            .append(lnav::roles::identifier(truncated_name))
            .append(
                this->gs_opid_width - truncated_name.utf8_length_or_length(),
                ' ')
            .append(truncated_desc);
        this->gs_rendered_line.with_attr_for_all(
            VC_ROLE.value(role_t::VCR_COMMENT));

        value_out = this->gs_rendered_line.get_string();
    }

    return {};
}

void
timeline_source::text_attrs_for_line(textview_curses& tc,
                                     int line,
                                     string_attrs_t& value_out)
{
    if (!this->ts_rebuild_in_progress
        && line < (ssize_t) this->gs_time_order.size())
    {
        const auto& row = *this->gs_time_order[line];

        value_out = this->gs_rendered_line.get_attrs();

        auto lr = line_range{-1, -1, line_range::unit::codepoint};
        auto [sel_lb, sel_ub]
            = this->get_time_bounds_for(tc.get_selection().value_or(0_vl));

        if (row.or_value.otr_range.tr_begin <= sel_ub
            && sel_lb <= row.or_value.otr_range.tr_end)
        {
            auto width = tc.get_dimensions().second;

            if (width > CHART_INDENT) {
                width -= CHART_INDENT;
                double span = (sel_ub - sel_lb).count();
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
    return this->gs_total_width;
}

bool
timeline_source::rebuild_indexes()
{
    static auto op = lnav_operation{"timeline_rebuild"};

    auto op_guard = lnav_opid_guard::internal(op);
    auto& bm = this->tss_view->get_bookmarks();
    auto& bm_errs = bm[&textview_curses::BM_ERRORS];
    auto& bm_warns = bm[&textview_curses::BM_WARNINGS];

    this->ts_rebuild_in_progress = true;
    bm_errs.clear();
    bm_warns.clear();

    this->gs_lower_bound = {};
    this->gs_upper_bound = {};
    this->gs_opid_width = 0;
    this->gs_total_width = 0;
    this->gs_filtered_count = 0;
    this->gs_active_opids.clear();
    this->gs_descriptions.clear();
    this->gs_subid_map.clear();
    this->gs_allocator.reset();
    this->gs_preview_source.clear();
    this->gs_preview_rows.clear();
    this->gs_preview_status_source.get_description().clear();

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
    tlx::btree_map<std::chrono::microseconds, std::string> part_map;
    for (const auto& [index, ld] : lnav::itertools::enumerate(this->gs_lss)) {
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
                if (line_meta.bm_name.empty()) {
                    continue;
                }
                const auto ll = std::next(lf->begin(), line_num);
                part_map.insert2(ll->get_time<std::chrono::microseconds>(),
                                 line_meta.bm_name);
            }
        }

        auto format = lf->get_format();
        safe::ReadAccess<logfile::safe_opid_state> r_opid_map(
            ld->get_file_ptr()->get_opids());
        for (const auto& pair : r_opid_map->los_opid_ranges) {
            const opid_time_range& otr = pair.second;
            auto active_iter = this->gs_active_opids.find(pair.first);
            if (active_iter == this->gs_active_opids.end()) {
                auto opid = pair.first.to_owned(this->gs_allocator);
                auto active_emp_res = this->gs_active_opids.emplace(
                    opid,
                    opid_row{
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
                auto subid_iter = this->gs_subid_map.find(sub.ostr_subid);

                if (subid_iter == this->gs_subid_map.end()) {
                    subid_iter = this->gs_subid_map
                                     .emplace(sub.ostr_subid.to_owned(
                                                  this->gs_allocator),
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

                auto& all_descs = row.or_descriptions;
                const auto& new_desc_v = otr.otr_description.lod_elements;
                all_descs.insert(desc_key, new_desc_v);
            } else if (!otr.otr_description.lod_elements.empty()) {
                auto desc_sf = string_fragment::from_str(
                    otr.otr_description.lod_elements.values().front());
                row.or_description = desc_sf.to_owned(this->gs_allocator);
            }
            row.or_value.otr_description.lod_elements.clear();
        }

        if (this->gs_index_progress) {
            switch (this->gs_index_progress(
                progress_t{index, this->gs_lss.file_count()}))
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
    if (this->gs_index_progress) {
        this->gs_index_progress(std::nullopt);
    }
    log_info("active opids: %zu", this->gs_active_opids.size());

    size_t filtered_in_count = 0;
    for (const auto& filt : this->tss_filters) {
        if (!filt->is_enabled()) {
            continue;
        }
        if (filt->get_type() == text_filter::INCLUDE) {
            filtered_in_count += 1;
        }
    }
    this->gs_filter_hits = {};
    this->gs_time_order.clear();
    this->gs_time_order.reserve(this->gs_active_opids.size());
    for (auto& pair : this->gs_active_opids) {
        opid_row& row = pair.second;
        opid_time_range& otr = pair.second.or_value;
        std::string full_desc;
        if (row.or_description.empty()) {
            const auto& desc_defs = row.or_description_defs.odd_defs;
            if (!row.or_descriptions.empty()) {
                auto desc_def_opt
                    = desc_defs.value_for(row.or_descriptions.keys().front());
                if (desc_def_opt) {
                    full_desc = desc_def_opt.value()->to_string(
                        row.or_descriptions.values().front());
                }
            }
            row.or_descriptions.clear();
            auto full_desc_sf = string_fragment::from_str(full_desc);
            auto desc_sf_iter = this->gs_descriptions.find(full_desc_sf);
            if (desc_sf_iter == this->gs_descriptions.end()) {
                full_desc_sf = string_fragment::from_str(full_desc).to_owned(
                    this->gs_allocator);
            }
            pair.second.or_description = full_desc_sf;
        } else {
            full_desc += pair.second.or_description;
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
                        this->gs_filter_hits[filt->get_index()] += 1;
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
                this->gs_filtered_count += 1;
                continue;
            }
        }

        if (pair.second.or_name.length() > this->gs_opid_width) {
            this->gs_opid_width = pair.second.or_name.length();
        }
        if (full_desc.size() > max_desc_width) {
            max_desc_width = full_desc.size();
        }

        if (this->gs_lower_bound == 0us
            || pair.second.or_value.otr_range.tr_begin < this->gs_lower_bound)
        {
            this->gs_lower_bound = pair.second.or_value.otr_range.tr_begin;
        }
        if (this->gs_upper_bound == 0us
            || this->gs_upper_bound < pair.second.or_value.otr_range.tr_end)
        {
            this->gs_upper_bound = pair.second.or_value.otr_range.tr_end;
        }

        this->gs_time_order.emplace_back(&pair.second);
    }
    std::stable_sort(
        this->gs_time_order.begin(),
        this->gs_time_order.end(),
        [](const auto* lhs, const auto* rhs) { return *lhs < *rhs; });
    for (size_t lpc = 0; lpc < this->gs_time_order.size(); lpc++) {
        const auto& row = *this->gs_time_order[lpc];
        if (row.or_value.otr_level_stats.lls_error_count > 0) {
            bm_errs.insert_once(vis_line_t(lpc));
        } else if (row.or_value.otr_level_stats.lls_warning_count > 0) {
            bm_warns.insert_once(vis_line_t(lpc));
        }
    }

    this->gs_opid_width = std::min(this->gs_opid_width, MAX_OPID_WIDTH);
    this->gs_total_width
        = std::max<size_t>(22 + this->gs_opid_width + max_desc_width,
                           1 + 16 + 5 + 8 + 5 + 16 + 1 /* header */);

    this->tss_view->set_needs_update();
    this->ts_rebuild_in_progress = false;

    ensure(this->gs_time_order.empty() || this->gs_opid_width > 0);

    return true;
}

std::optional<vis_line_t>
timeline_source::row_for_time(timeval time_bucket)
{
    auto time_bucket_us = to_us(time_bucket);
    auto iter = this->gs_time_order.begin();
    while (true) {
        if (iter == this->gs_time_order.end()) {
            return std::nullopt;
        }

        if ((*iter)->or_value.otr_range.contains_inclusive(time_bucket_us)) {
            break;
        }
        ++iter;
    }

    auto closest_iter = iter;
    auto closest_diff = time_bucket_us - (*iter)->or_value.otr_range.tr_begin;
    for (; iter != this->gs_time_order.end(); ++iter) {
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

    return vis_line_t(std::distance(this->gs_time_order.begin(), closest_iter));
}

std::optional<vis_line_t>
timeline_source::row_for(const row_info& ri)
{
    auto vl_opt = this->gs_lss.row_for(ri);
    if (!vl_opt) {
        return this->row_for_time(ri.ri_time);
    }

    auto vl = vl_opt.value();
    auto win = this->gs_lss.window_at(vl);
    for (const auto& msg_line : *win) {
        const auto& lvv = msg_line.get_values();

        if (lvv.lvv_opid_value) {
            auto opid_iter
                = this->gs_active_opids.find(lvv.lvv_opid_value.value());
            if (opid_iter != this->gs_active_opids.end()) {
                for (const auto& [index, oprow] :
                     lnav::itertools::enumerate(this->gs_time_order))
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
    if (row >= this->gs_time_order.size()) {
        return std::nullopt;
    }

    const auto& otr = this->gs_time_order[row]->or_value;

    if (this->tss_view->get_selection() == row) {
        auto ov_sel = this->tss_view->get_overlay_selection();

        if (ov_sel && ov_sel.value() < otr.otr_sub_ops.size()) {
            return row_info{
                to_timeval(otr.otr_sub_ops[ov_sel.value()].ostr_range.tr_begin),
                row,
            };
        }
    }

    auto preview_selection = this->gs_preview_view.get_selection();
    if (!preview_selection) {
        return std::nullopt;
    }
    if (preview_selection < this->gs_preview_rows.size()) {
        return this->gs_preview_rows[preview_selection.value()];
    }

    return row_info{
        to_timeval(otr.otr_range.tr_begin),
        row,
    };
}

size_t
timeline_source::text_line_width(textview_curses& curses)
{
    return this->gs_total_width;
}

void
timeline_source::text_selection_changed(textview_curses& tc)
{
    static const size_t MAX_PREVIEW_LINES = 200;

    auto sel = tc.get_selection();

    this->gs_preview_source.clear();
    this->gs_preview_rows.clear();
    if (!sel || sel.value() >= this->gs_time_order.size()) {
        return;
    }

    const auto& row = *this->gs_time_order[sel.value()];
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
    auto low_vl = this->gs_lss.row_for_time(to_timeval(low_us));
    auto high_vl = this->gs_lss.row_for_time(to_timeval(high_us))
                       .value_or(vis_line_t(this->gs_lss.text_line_count()));

    if (!low_vl) {
        return;
    }

    auto preview_content = attr_line_t();
    auto msgs_remaining = size_t{MAX_PREVIEW_LINES};
    auto win = this->gs_lss.window_at(low_vl.value(), high_vl);
    auto id_hash = row.or_name.hash();
    auto msg_count = 0;
    for (const auto& msg_line : *win) {
        if (!msg_line.get_logline().match_opid_hash(id_hash)) {
            continue;
        }

        const auto& lvv = msg_line.get_values();
        if (!lvv.lvv_opid_value) {
            continue;
        }
        auto opid_sf = lvv.lvv_opid_value.value();

        if (opid_sf == row.or_name) {
            for (size_t lpc = 0; lpc < msg_line.get_line_count(); lpc++) {
                auto vl = msg_line.get_vis_line() + vis_line_t(lpc);
                auto cl = this->gs_lss.at(vl);
                auto row_al = attr_line_t();
                this->gs_log_view.textview_value_for_row(vl, row_al);
                preview_content.append(row_al).append("\n");
                this->gs_preview_rows.emplace_back(
                    msg_line.get_logline().get_timeval(), cl);
                ++cl;
            }
            msg_count += 1;
            msgs_remaining -= 1;
            if (msgs_remaining == 0) {
                break;
            }
        }
    }

    this->gs_preview_source.replace_with(preview_content);
    this->gs_preview_view.set_selection(0_vl);
    this->gs_preview_status_source.get_description().set_value(
        " ID %.*s", id_sf.length(), id_sf.data());
    auto err_count = level_stats.lls_error_count;
    if (err_count == 0) {
        this->gs_preview_status_source
            .statusview_value_for_field(timeline_status_source::TSF_ERRORS)
            .set_value("");
    } else if (err_count > 1) {
        this->gs_preview_status_source
            .statusview_value_for_field(timeline_status_source::TSF_ERRORS)
            .set_value("%'d errors", err_count);
    } else {
        this->gs_preview_status_source
            .statusview_value_for_field(timeline_status_source::TSF_ERRORS)
            .set_value("%'d error", err_count);
    }
    if (msg_count < level_stats.lls_total_count) {
        this->gs_preview_status_source
            .statusview_value_for_field(timeline_status_source::TSF_TOTAL)
            .set_value(
                "%'d of %'d messages ", msg_count, level_stats.lls_total_count);
    } else {
        this->gs_preview_status_source
            .statusview_value_for_field(timeline_status_source::TSF_TOTAL)
            .set_value("%'d messages ", level_stats.lls_total_count);
    }
    this->gs_preview_status_view.set_needs_update();
}

void
timeline_source::text_filters_changed()
{
    this->rebuild_indexes();
    this->tss_view->reload_data();
    this->tss_view->redo_search();
}

int
timeline_source::get_filtered_count() const
{
    return this->gs_filtered_count;
}

int
timeline_source::get_filtered_count_for(size_t filter_index) const
{
    return this->gs_filter_hits[filter_index];
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

    if (line >= this->gs_time_order.size()) {
        return;
    }

    const auto& row = *this->gs_time_order[line];
    char ts[64];

    sql_strftime(ts, sizeof(ts), row.or_value.otr_range.tr_begin, 'T');

    crumbs.emplace_back(std::string(ts),
                        timestamp_poss,
                        [ec = this->gs_exec_context](const auto& ts) {
                            auto cmd
                                = fmt::format(FMT_STRING(":goto {}"),
                                              ts.template get<std::string>());
                            ec->execute(INTERNAL_SRC_LOC, cmd);
                        });
    crumbs.back().c_expected_input
        = breadcrumb::crumb::expected_input_t::anything;
    crumbs.back().c_search_placeholder = "(Enter an absolute or relative time)";
}
