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

#include <chrono>

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
#include "intervaltree/IntervalTree.h"
#include "lnav_util.hh"
#include "md4cpp.hh"
#include "sql_util.hh"
#include "sysclip.hh"

using namespace std::chrono_literals;
using namespace lnav::roles::literals;
using namespace md4cpp::literals;

static const std::vector<std::chrono::seconds> TIME_SPANS = {
    5min,
    15min,
    1h,
    2h,
    4h,
    8h,
    24h,
    7 * 24h,
    30 * 24h,
    365 * 24h,
};

static constexpr size_t MAX_OPID_WIDTH = 60;

size_t
abbrev_ftime(char* datebuf,
             size_t db_size,
             const struct tm& lb_tm,
             const struct tm& dt)
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
                                             int y,
                                             int bottom,
                                             attr_line_t& value_out)
{
    if (y >= 3) {
        return false;
    }

    if (this->gho_src->gs_time_order.empty()) {
        if (y == 0) {
            value_out.append("No operations found"_error);
            return true;
        }

        return false;
    }

    auto lb = this->gho_src->gs_lower_bound;
    struct tm lb_tm;
    auto ub = this->gho_src->gs_upper_bound;
    struct tm ub_tm;
    auto bounds = this->gho_src->get_time_bounds_for(lv.get_selection());

    if (bounds.first < lb) {
        lb = bounds.first;
    }
    if (ub < bounds.second) {
        ub = bounds.second;
    }

    secs2tm(lb.tv_sec, &lb_tm);
    secs2tm(ub.tv_sec, &ub_tm);

    struct tm sel_lb_tm;
    secs2tm(bounds.first.tv_sec, &sel_lb_tm);
    struct tm sel_ub_tm;
    secs2tm(bounds.second.tv_sec, &sel_ub_tm);

    auto width = lv.get_dimensions().second - 1;

    char datebuf[64];

    if (y == 0) {
        double span = ub.tv_sec - lb.tv_sec;
        double per_ch = span / (double) width;
        strftime(datebuf, sizeof(datebuf), " %Y-%m-%dT%H:%M", &lb_tm);
        value_out.append(datebuf);

        auto duration_str = humanize::time::duration::from_tv(ub - lb)
                                .with_resolution(1min)
                                .to_string();
        auto duration_pos = width / 2 - duration_str.size() / 2;
        value_out.pad_to(duration_pos).append(duration_str);

        auto upper_size
            = strftime(datebuf, sizeof(datebuf), "%Y-%m-%dT%H:%M ", &ub_tm);
        auto upper_pos = width - upper_size;

        value_out.pad_to(upper_pos).append(datebuf);

        auto lr = line_range{};
        if (lb.tv_sec < bounds.first.tv_sec) {
            auto start_diff = bounds.first.tv_sec - lb.tv_sec;
            lr.lr_start = start_diff / per_ch;
        } else {
            lr.lr_start = 0;
        }
        if (lb.tv_sec < bounds.second.tv_sec) {
            auto start_diff = bounds.second.tv_sec - lb.tv_sec;
            lr.lr_end = start_diff / per_ch;
        } else {
            lr.lr_end = 1;
        }
        if (lr.lr_start == lr.lr_end) {
            lr.lr_end += 1;
        }

        value_out.get_attrs().emplace_back(
            lr, VC_ROLE.value(role_t::VCR_CURSOR_LINE));
        value_out.with_attr_for_all(VC_ROLE.value(role_t::VCR_STATUS_INFO));
    } else if (y == 1) {
        abbrev_ftime(datebuf, sizeof(datebuf), lb_tm, sel_lb_tm);
        value_out.appendf(FMT_STRING(" {}"), datebuf);

        auto duration_str
            = humanize::time::duration::from_tv(bounds.second - bounds.first)
                  .with_resolution(1min)
                  .to_string();
        auto duration_pos = width / 2 - duration_str.size() / 2;
        value_out.pad_to(duration_pos).append(duration_str);

        auto upper_size
            = abbrev_ftime(datebuf, sizeof(datebuf), ub_tm, sel_ub_tm);
        auto upper_pos = width - upper_size - 1;
        value_out.pad_to(upper_pos).append(datebuf);
        value_out.with_attr_for_all(VC_ROLE.value(role_t::VCR_CURSOR_LINE));
    } else {
        value_out.append("   Duration   "_h1)
            .append("|", VC_GRAPHIC.value(NCACS_VLINE))
            .append(" ")
            .append("\u2718"_error)
            .append("\u25b2"_warning)
            .append(" ")
            .append("|", VC_GRAPHIC.value(NCACS_VLINE))
            .append(" Operation"_h1);
        auto hdr_attrs = text_attrs::with_underline();
        value_out.get_attrs().emplace_back(line_range{0, -1},
                                           VC_STYLE.value(hdr_attrs));
        value_out.with_attr_for_all(VC_ROLE.value(role_t::VCR_STATUS_INFO));
    }

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

    const auto& row = this->gho_src->gs_time_order[line].get();

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
            humanize::time::duration::from_tv(duration).to_string());
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

        auto start_diff = (double) to_mstime(sub.ostr_range.tr_begin
                                             - row.or_value.otr_range.tr_begin);
        auto end_diff = (double) to_mstime(sub.ostr_range.tr_end
                                           - row.or_value.otr_range.tr_begin);

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
            }
            this->tss_view->tc_cursor_role = role_t::VCR_CURSOR_LINE;
            this->gs_preview_view.tc_cursor_role
                = role_t::VCR_DISABLED_CURSOR_LINE;
            this->gs_preview_status_view.set_enabled(this->gs_preview_focused);
            break;
        }
        case '\n':
        case '\r':
        case NCKEY_ENTER: {
            this->gs_preview_focused = !this->gs_preview_focused;
            this->gs_preview_status_view.set_enabled(this->gs_preview_focused);
            if (this->gs_preview_focused) {
                auto height = this->tss_view->get_dimensions().first;

                if (height > 5) {
                    this->gs_preview_view.set_height(height - 3_vl);
                }
                this->tss_view->tc_cursor_role
                    = role_t::VCR_DISABLED_CURSOR_LINE;
                this->gs_preview_view.tc_cursor_role = role_t::VCR_CURSOR_LINE;
            } else {
                this->tss_view->tc_cursor_role = role_t::VCR_CURSOR_LINE;
                this->gs_preview_view.tc_cursor_role
                    = role_t::VCR_DISABLED_CURSOR_LINE;
                this->gs_preview_view.set_height(5_vl);
            }
            return true;
        }
    }
    if (this->gs_preview_focused) {
        log_debug("to preview");
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

std::pair<timeval, timeval>
timeline_source::get_time_bounds_for(int line)
{
    const auto low_index = this->tss_view->get_top();
    auto high_index
        = std::min(this->tss_view->get_bottom(),
                   vis_line_t((int) this->gs_time_order.size() - 1));
    const auto& low_row = this->gs_time_order[low_index].get();
    const auto& high_row = this->gs_time_order[high_index].get();
    auto low_tv_sec = low_row.or_value.otr_range.tr_begin.tv_sec;
    auto high_tv_sec = high_row.or_value.otr_range.tr_begin.tv_sec;

    for (auto index = low_index; index <= high_index; index += 1_vl) {
        const auto& row = this->gs_time_order[index].get();

        if (row.or_value.otr_range.tr_end.tv_sec > high_tv_sec) {
            high_tv_sec = row.or_value.otr_range.tr_end.tv_sec;
        }
    }
    auto duration = std::chrono::seconds{high_tv_sec - low_tv_sec};
    auto span_iter
        = std::upper_bound(TIME_SPANS.begin(), TIME_SPANS.end(), duration);
    if (span_iter == TIME_SPANS.end()) {
        --span_iter;
    }
    auto round_to = (*span_iter) == 5min
        ? 60
        : ((*span_iter) == 15min ? 60 * 15 : 60 * 60);
    auto span_secs = span_iter->count() - round_to;
    struct timeval lower_tv = {
        rounddown(low_row.or_value.otr_range.tr_begin.tv_sec, round_to),
        0,
    };
    lower_tv.tv_sec -= span_secs / 2;
    struct timeval upper_tv = {
        static_cast<time_t>(roundup(high_tv_sec, round_to)),
        0,
    };
    upper_tv.tv_sec += span_secs / 2;

    return {lower_tv, upper_tv};
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
                                     text_sub_source::line_flags_t flags)
{
    if (line < (ssize_t) this->gs_time_order.size()) {
        const auto& row = this->gs_time_order[line].get();
        auto duration
            = row.or_value.otr_range.tr_end - row.or_value.otr_range.tr_begin;
        auto duration_str = fmt::format(
            FMT_STRING(" {: >13}"),
            humanize::time::duration::from_tv(duration).to_string());

        this->gs_rendered_line.clear();

        auto total_msgs = row.or_value.otr_level_stats.lls_total_count;
        auto truncated_name = row.or_name.to_string();
        truncate_to(truncated_name, MAX_OPID_WIDTH);
        this->gs_rendered_line
            .append(duration_str, VC_ROLE.value(role_t::VCR_OFFSET_TIME))
            .append("  ")
            .append(lnav::roles::error(humanize::sparkline(
                row.or_value.otr_level_stats.lls_error_count, total_msgs)))
            .append(lnav::roles::warning(humanize::sparkline(
                row.or_value.otr_level_stats.lls_warning_count, total_msgs)))
            .append("  ")
            .append(lnav::roles::identifier(truncated_name))
            .append(this->gs_opid_width
                        - utf8_string_length(truncated_name)
                              .unwrapOr(this->gs_opid_width),
                    ' ')
            .append(row.or_description);
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
    if (line < (ssize_t) this->gs_time_order.size()) {
        const auto& row = this->gs_time_order[line].get();

        value_out = this->gs_rendered_line.get_attrs();

        auto lr = line_range{-1, -1, line_range::unit::codepoint};
        auto sel_bounds = this->get_time_bounds_for(tc.get_selection());

        if (row.or_value.otr_range.tr_begin <= sel_bounds.second
            && sel_bounds.first <= row.or_value.otr_range.tr_end)
        {
            static const int INDENT = 22;

            auto width = tc.get_dimensions().second;

            if (width > INDENT) {
                width -= INDENT;
                double span
                    = sel_bounds.second.tv_sec - sel_bounds.first.tv_sec;
                double per_ch = span / (double) width;

                if (row.or_value.otr_range.tr_begin <= sel_bounds.first) {
                    lr.lr_start = INDENT;
                } else {
                    auto start_diff = row.or_value.otr_range.tr_begin.tv_sec
                        - sel_bounds.first.tv_sec;

                    lr.lr_start = INDENT + start_diff / per_ch;
                }

                if (sel_bounds.second < row.or_value.otr_range.tr_end) {
                    lr.lr_end = -1;
                } else {
                    auto end_diff = row.or_value.otr_range.tr_end.tv_sec
                        - sel_bounds.first.tv_sec;

                    lr.lr_end = INDENT + end_diff / per_ch;
                    if (lr.lr_start == lr.lr_end) {
                        lr.lr_end += 1;
                    }
                }

                auto block_attrs = text_attrs::with_reverse();
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
    auto& bm = this->tss_view->get_bookmarks();
    auto& bm_errs = bm[&textview_curses::BM_ERRORS];
    auto& bm_warns = bm[&textview_curses::BM_WARNINGS];

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

    auto min_log_time_opt = this->get_min_row_time();
    auto max_log_time_opt = this->get_max_row_time();
    auto max_desc_width = size_t{0};

    for (const auto& [index, ld] : lnav::itertools::enumerate(this->gs_lss)) {
        if (ld->get_file_ptr() == nullptr) {
            continue;
        }
        if (!ld->is_visible()) {
            continue;
        }

        ld->get_file_ptr()->enable_cache();
        auto format = ld->get_file_ptr()->get_format();
        safe::ReadAccess<logfile::safe_opid_state> r_opid_map(
            ld->get_file_ptr()->get_opids());
        for (const auto& pair : r_opid_map->los_opid_ranges) {
            auto& otr = pair.second;
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

            auto& row = active_iter->second;
            for (auto& sub : active_iter->second.or_value.otr_sub_ops) {
                auto subid_iter = this->gs_subid_map.find(sub.ostr_subid);

                if (subid_iter == this->gs_subid_map.end()) {
                    subid_iter = this->gs_subid_map
                                     .emplace(sub.ostr_subid.to_owned(
                                                  this->gs_allocator),
                                              true)
                                     .first;
                }
                sub.ostr_subid = subid_iter->first;
                if (sub.ostr_subid.length()
                    > active_iter->second.or_max_subid_width)
                {
                    active_iter->second.or_max_subid_width
                        = sub.ostr_subid.length();
                }
            }

            if (otr.otr_description.lod_id) {
                auto desc_id = otr.otr_description.lod_id.value();
                auto desc_def_iter
                    = format->lf_opid_description_def->find(desc_id);

                if (desc_def_iter == format->lf_opid_description_def->end()) {
                    log_error("cannot find description: %s",
                              active_iter->first.data());
                } else {
                    auto desc_key
                        = opid_description_def_key{format->get_name(), desc_id};
                    auto desc_defs_iter
                        = row.or_description_defs.odd_defs.find(desc_key);
                    if (desc_defs_iter
                        == row.or_description_defs.odd_defs.end())
                    {
                        row.or_description_defs.odd_defs.insert(
                            desc_key, desc_def_iter->second);
                    }

                    auto& all_descs = active_iter->second.or_descriptions;
                    auto& curr_desc_m = all_descs[desc_key];
                    const auto& new_desc_v = otr.otr_description.lod_elements;

                    for (const auto& desc_pair : new_desc_v) {
                        curr_desc_m[desc_pair.first] = desc_pair.second;
                    }
                }
            } else {
                ensure(otr.otr_description.lod_elements.empty());
            }
            active_iter->second.or_value.otr_description.lod_elements.clear();
        }

        if (this->gs_index_progress) {
            switch (this->gs_index_progress(
                progress_t{index, this->gs_lss.file_count()}))
            {
                case lnav::progress_result_t::ok:
                    break;
                case lnav::progress_result_t::interrupt:
                    return false;
            }
        }
    }
    if (this->gs_index_progress) {
        this->gs_index_progress(std::nullopt);
    }

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
        auto& otr = pair.second.or_value;
        std::string full_desc;
        const auto& desc_defs = pair.second.or_description_defs.odd_defs;
        for (auto& desc : pair.second.or_descriptions) {
            auto desc_def_iter = desc_defs.find(desc.first);
            if (desc_def_iter == desc_defs.end()) {
                continue;
            }
            const auto& desc_def = desc_def_iter->second;
            full_desc = desc_def.to_string(desc.second);
        }
        pair.second.or_descriptions.clear();
        auto full_desc_sf = string_fragment::from_str(full_desc);
        auto desc_sf_iter = this->gs_descriptions.find(full_desc_sf);
        if (desc_sf_iter == this->gs_descriptions.end()) {
            full_desc_sf = string_fragment::from_str(full_desc).to_owned(
                this->gs_allocator);
        }
        pair.second.or_description = full_desc_sf;

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

        if (this->gs_lower_bound.tv_sec == 0
            || pair.second.or_value.otr_range.tr_begin < this->gs_lower_bound)
        {
            this->gs_lower_bound = pair.second.or_value.otr_range.tr_begin;
        }
        if (this->gs_upper_bound.tv_sec == 0
            || this->gs_upper_bound < pair.second.or_value.otr_range.tr_end)
        {
            this->gs_upper_bound = pair.second.or_value.otr_range.tr_end;
        }
        this->gs_time_order.emplace_back(pair.second);
    }
    std::stable_sort(this->gs_time_order.begin(),
                     this->gs_time_order.end(),
                     std::less<const opid_row>{});
    for (size_t lpc = 0; lpc < this->gs_time_order.size(); lpc++) {
        const auto& row = this->gs_time_order[lpc].get();
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

    return true;
}

std::optional<vis_line_t>
timeline_source::row_for_time(struct timeval time_bucket)
{
    auto iter = this->gs_time_order.begin();
    while (true) {
        if (iter == this->gs_time_order.end()) {
            return std::nullopt;
        }

        if (iter->get().or_value.otr_range.contains_inclusive(time_bucket)) {
            break;
        }
        ++iter;
    }

    auto closest_iter = iter;
    auto closest_diff = time_bucket - iter->get().or_value.otr_range.tr_begin;
    for (; iter != this->gs_time_order.end(); ++iter) {
        if (time_bucket < iter->get().or_value.otr_range.tr_begin) {
            break;
        }
        if (!iter->get().or_value.otr_range.contains_inclusive(time_bucket)) {
            continue;
        }

        auto diff = time_bucket - iter->get().or_value.otr_range.tr_begin;
        if (diff < closest_diff) {
            closest_iter = iter;
            closest_diff = diff;
        }

        for (const auto& sub : iter->get().or_value.otr_sub_ops) {
            if (!sub.ostr_range.contains_inclusive(time_bucket)) {
                continue;
            }

            diff = time_bucket - sub.ostr_range.tr_begin;
            if (diff < closest_diff) {
                closest_iter = iter;
                closest_diff = diff;
            }
        }
    }

    return vis_line_t(std::distance(this->gs_time_order.begin(), closest_iter));
}

std::optional<text_time_translator::row_info>
timeline_source::time_for_row(vis_line_t row)
{
    if (row >= this->gs_time_order.size()) {
        return std::nullopt;
    }

    const auto& otr = this->gs_time_order[row].get().or_value;

    if (this->tss_view->get_selection() == row) {
        auto ov_sel = this->tss_view->get_overlay_selection();

        if (ov_sel && ov_sel.value() < otr.otr_sub_ops.size()) {
            return row_info{
                otr.otr_sub_ops[ov_sel.value()].ostr_range.tr_begin,
                row,
            };
        }
    }

    auto preview_selection = this->gs_preview_view.get_selection();
    if (preview_selection < this->gs_preview_rows.size()) {
        return this->gs_preview_rows[preview_selection];
    }

    return row_info{
        otr.otr_range.tr_begin,
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
    if (sel >= this->gs_time_order.size()) {
        return;
    }

    const auto& row = this->gs_time_order[sel].get();
    auto low_tv = row.or_value.otr_range.tr_begin;
    auto high_tv = row.or_value.otr_range.tr_end;
    auto id_sf = row.or_name;
    auto level_stats = row.or_value.otr_level_stats;
    auto ov_sel = tc.get_overlay_selection();
    if (ov_sel) {
        const auto& sub = row.or_value.otr_sub_ops[ov_sel.value()];
        id_sf = sub.ostr_subid;
        low_tv = sub.ostr_range.tr_begin;
        high_tv = sub.ostr_range.tr_end;
        level_stats = sub.ostr_level_stats;
    }
    high_tv.tv_sec += 1;
    auto low_vl = this->gs_lss.row_for_time(low_tv);
    auto high_vl = this->gs_lss.row_for_time(high_tv).value_or(
        vis_line_t(this->gs_lss.text_line_count()));

    if (!low_vl) {
        return;
    }

    auto preview_content = attr_line_t();
    auto msgs_remaining = size_t{MAX_PREVIEW_LINES};
    auto win = this->gs_lss.window_at(low_vl.value(), high_vl);
    auto id_hash = row.or_name.hash();
    for (const auto& msg_line : win) {
        if (!msg_line.get_logline().match_opid_hash(id_hash)) {
            continue;
        }

        const auto& lvv = msg_line.get_values();
        if (!lvv.lvv_opid_value) {
            continue;
        }
        auto opid_sf = lvv.lvv_opid_value.value();

        if (opid_sf == row.or_name) {
            std::vector<attr_line_t> rows_al(msg_line.get_line_count());

            auto cl = this->gs_lss.at(msg_line.get_vis_line());
            this->gs_log_view.listview_value_for_rows(
                this->gs_log_view, msg_line.get_vis_line(), rows_al);

            for (const auto& row_al : rows_al) {
                this->gs_preview_rows.emplace_back(
                    msg_line.get_logline().get_timeval(), cl);
                ++cl;
                preview_content.append(row_al).append("\n");
            }
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
    this->gs_preview_status_source
        .statusview_value_for_field(timeline_status_source::TSF_TOTAL)
        .set_value("%'d messages ", level_stats.lls_total_count);
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
    static intern_string_t SRC = intern_string::lookup("crumb");
    text_sub_source::text_crumbs_for_line(line, crumbs);

    if (line >= this->gs_time_order.size()) {
        return;
    }

    const auto& row = this->gs_time_order[line].get();
    char ts[64];

    sql_strftime(ts, sizeof(ts), row.or_value.otr_range.tr_begin, 'T');

    crumbs.emplace_back(std::string(ts),
                        timestamp_poss,
                        [ec = this->gs_exec_context](const auto& ts) {
                            auto cmd
                                = fmt::format(FMT_STRING(":goto {}"),
                                              ts.template get<std::string>());
                            auto src_guard = ec->enter_source(SRC, 1, cmd);
                            ec->execute(cmd);
                        });
    crumbs.back().c_expected_input
        = breadcrumb::crumb::expected_input_t::anything;
    crumbs.back().c_search_placeholder = "(Enter an absolute or relative time)";
}
