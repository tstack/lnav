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

#include "gantt_source.hh"

#include "base/humanize.hh"
#include "base/humanize.time.hh"
#include "base/math_util.hh"
#include "md4cpp.hh"
#include "sql_util.hh"

using namespace std::chrono_literals;
using namespace lnav::roles::literals;
using namespace md4cpp::literals;

static const std::vector<std::chrono::seconds> TIME_SPANS = {
    5min,
    15min,
    1h,
    8h,
    24h,
    7 * 24h,
    30 * 24h,
    365 * 24h,
};

static constexpr size_t MAX_OPID_WIDTH = 60;

gantt_header_overlay::gantt_header_overlay(std::shared_ptr<gantt_source> src)
    : gho_src(src)
{
}

bool
gantt_header_overlay::list_static_overlay(const listview_curses& lv,
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

    auto bounds = this->gho_src->get_time_bounds_for(lv.get_selection());
    auto width = lv.get_dimensions().second - 1;

    char datebuf[64];

    if (y == 0) {
        auto lb = this->gho_src->gs_lower_bound;
        auto ub = this->gho_src->gs_upper_bound;

        double span = ub.tv_sec - lb.tv_sec;
        double per_ch = span / (double) width;
        sql_strftime(datebuf, sizeof(datebuf), lb, 'T');
        value_out.appendf(FMT_STRING(" {}"), datebuf);

        auto upper_size = sql_strftime(datebuf, sizeof(datebuf), ub, 'T');
        value_out.append(width - value_out.length() - upper_size - 1, ' ')
            .append(datebuf);

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
        sql_strftime(datebuf, sizeof(datebuf), bounds.first, 'T');
        value_out.appendf(FMT_STRING("     {}"), datebuf);

        auto upper_size
            = sql_strftime(datebuf, sizeof(datebuf), bounds.second, 'T');
        value_out.append(width - value_out.length() - upper_size - 5, ' ')
            .append(datebuf);
        value_out.with_attr_for_all(VC_ROLE.value(role_t::VCR_CURSOR_LINE));
    } else {
        value_out.append("   Duration   "_h1)
            .append("|", VC_GRAPHIC.value(ACS_VLINE))
            .append(" ")
            .append("\u2718"_error)
            .append("\u25b2"_warning)
            .append(" ")
            .append("|", VC_GRAPHIC.value(ACS_VLINE))
            .append(" Operation"_h1);
        auto hdr_attrs = text_attrs{};
        hdr_attrs.ta_attrs = A_UNDERLINE;
        value_out.get_attrs().emplace_back(line_range{0, -1},
                                           VC_STYLE.value(hdr_attrs));
        value_out.with_attr_for_all(VC_ROLE.value(role_t::VCR_STATUS_INFO));
    }

    return true;
}

gantt_source::gantt_source(textview_curses& log_view,
                           logfile_sub_source& lss,
                           plain_text_source& preview_source,
                           gantt_status_source& preview_status_source)
    : gs_log_view(log_view), gs_lss(lss), gs_preview_source(preview_source),
      gs_preview_status_source(preview_status_source)
{
    this->tss_supports_filtering = true;
}

std::pair<timeval, timeval>
gantt_source::get_time_bounds_for(int line)
{
    static const int CONTEXT_LINES = 5;

    const auto& low_row
        = this->gs_time_order[std::max(0, line - CONTEXT_LINES)];
    const auto& sel_row = this->gs_time_order[line];
    const auto& high_row = this->gs_time_order[std::min(
        line + CONTEXT_LINES, (int) this->gs_time_order.size() - 1)];
    auto high_tv_sec = std::max(sel_row.or_value.otr_end.tv_sec,
                                high_row.or_value.otr_begin.tv_sec);

    auto duration
        = std::chrono::seconds{high_tv_sec - low_row.or_value.otr_begin.tv_sec};
    auto span_iter
        = std::upper_bound(TIME_SPANS.begin(), TIME_SPANS.end(), duration);
    if (span_iter == TIME_SPANS.end()) {
        --span_iter;
    }
    auto span_secs = span_iter->count() - 60;
    struct timeval lower_tv = {
        rounddown(low_row.or_value.otr_begin.tv_sec, 60),
        0,
    };
    lower_tv.tv_sec -= span_secs / 2;
    struct timeval upper_tv = {
        static_cast<time_t>(roundup_size(high_tv_sec, 60)),
        0,
    };
    upper_tv.tv_sec += span_secs / 2;

    return {lower_tv, upper_tv};
}

size_t
gantt_source::text_line_count()
{
    return this->gs_time_order.size();
}

void
gantt_source::text_value_for_line(textview_curses& tc,
                                  int line,
                                  std::string& value_out,
                                  text_sub_source::line_flags_t flags)
{
    if (line < this->gs_time_order.size()) {
        const auto& row = this->gs_time_order[line];
        auto duration = row.or_value.otr_end - row.or_value.otr_begin;
        auto duration_str = fmt::format(
            FMT_STRING(" {: >13}"),
            humanize::time::duration::from_tv(duration).to_string());

        this->gs_rendered_line.clear();

        auto total_msgs = row.or_value.get_total_msgs();
        auto truncated_name = row.or_name.to_string();
        truncate_to(truncated_name, MAX_OPID_WIDTH);
        this->gs_rendered_line
            .append(duration_str, VC_ROLE.value(role_t::VCR_OFFSET_TIME))
            .append("  ")
            .append(lnav::roles::error(humanize::sparkline(
                row.or_value.get_error_count(), total_msgs)))
            .append(lnav::roles::warning(humanize::sparkline(
                row.or_value.otr_level_counts[log_level_t::LEVEL_WARNING],
                total_msgs)))
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
}

void
gantt_source::text_attrs_for_line(textview_curses& tc,
                                  int line,
                                  string_attrs_t& value_out)
{
    if (line < this->gs_time_order.size()) {
        const auto& row = this->gs_time_order[line];

        value_out = this->gs_rendered_line.get_attrs();

        auto lr = line_range{};
        auto sel_bounds = this->get_time_bounds_for(tc.get_selection());

        if (row.or_value.otr_begin <= sel_bounds.second
            && sel_bounds.first <= row.or_value.otr_end)
        {
            static const int INDENT = 22;

            auto width = tc.get_dimensions().second;

            if (width > INDENT) {
                width -= INDENT;
                double span
                    = sel_bounds.second.tv_sec - sel_bounds.first.tv_sec;
                double per_ch = span / (double) width;

                if (row.or_value.otr_begin <= sel_bounds.first) {
                    lr.lr_start = INDENT;
                } else {
                    auto start_diff = row.or_value.otr_begin.tv_sec
                        - sel_bounds.first.tv_sec;

                    lr.lr_start = INDENT + start_diff / per_ch;
                }

                if (sel_bounds.second < row.or_value.otr_end) {
                    lr.lr_end = -1;
                } else {
                    auto end_diff
                        = row.or_value.otr_end.tv_sec - sel_bounds.first.tv_sec;

                    lr.lr_end = INDENT + end_diff / per_ch;
                    if (lr.lr_start == lr.lr_end) {
                        lr.lr_end += 1;
                    }
                }

                auto block_attrs = text_attrs{};
                block_attrs.ta_attrs = A_REVERSE;
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
gantt_source::text_size_for_line(textview_curses& tc,
                                 int line,
                                 text_sub_source::line_flags_t raw)
{
    return this->gs_total_width;
}

void
gantt_source::rebuild_indexes()
{
    auto& bm = this->tss_view->get_bookmarks();
    auto& bm_errs = bm[&logfile_sub_source::BM_ERRORS];
    auto& bm_warns = bm[&logfile_sub_source::BM_WARNINGS];

    bm_errs.clear();
    bm_warns.clear();

    this->gs_lower_bound = {};
    this->gs_upper_bound = {};
    this->gs_opid_width = 0;
    this->gs_total_width = 0;
    this->gs_filtered_count = 0;
    this->gs_preview_source.clear();
    this->gs_preview_status_source.get_description().clear();

    auto min_log_time_opt = this->gs_lss.get_min_log_time();
    auto max_log_time_opt = this->gs_lss.get_max_log_time();

    auto max_desc_width = size_t{0};

    std::map<string_fragment, opid_row> active_opids;
    for (const auto& ld : this->gs_lss) {
        if (ld->get_file_ptr() == nullptr) {
            continue;
        }
        if (!ld->is_visible()) {
            continue;
        }

        auto format = ld->get_file_ptr()->get_format();
        safe::ReadAccess<logfile::safe_opid_map> r_opid_map(
            ld->get_file_ptr()->get_opids());

        for (const auto& pair : *r_opid_map) {
            auto iter = this->gs_opid_map.find(pair.first);
            if (iter == this->gs_opid_map.end()) {
                auto opid = pair.first.to_owned(this->gs_allocator);
                auto emp_res
                    = this->gs_opid_map.emplace(opid, opid_description_defs{});
                iter = emp_res.first;
            }

            auto active_iter = active_opids.find(pair.first);
            if (active_iter == active_opids.end()) {
                auto active_emp_res = active_opids.emplace(
                    iter->first, opid_row{pair.first, pair.second});
                active_iter = active_emp_res.first;
            } else {
                active_iter->second.or_value |= pair.second;
            }

            if (pair.second.otr_description_id) {
                auto desc_id = pair.second.otr_description_id.value();
                auto desc_def_iter
                    = format->lf_opid_description_def->find(desc_id);

                if (desc_def_iter != format->lf_opid_description_def->end()) {
                    auto& format_descs
                        = iter->second.odd_format_to_desc[format->get_name()];
                    format_descs[desc_id]
                        = desc_def_iter->second.od_descriptors;

                    auto& all_descs = active_iter->second.or_descriptions;
                    auto& curr_desc_m = all_descs[format->get_name()][desc_id];
                    auto& new_desc_v = pair.second.otr_description;

                    for (const auto& desc_pair : new_desc_v) {
                        curr_desc_m[desc_pair.first] = desc_pair.second;
                    }
                }
            }
        }
    }

    std::multimap<struct timeval, opid_row> time_order_map;
    for (const auto& pair : active_opids) {
        if (this->gs_lower_bound.tv_sec == 0
            || pair.second.or_value.otr_begin < this->gs_lower_bound)
        {
            this->gs_lower_bound = pair.second.or_value.otr_begin;
        }
        if (this->gs_upper_bound.tv_sec == 0
            || this->gs_upper_bound < pair.second.or_value.otr_end)
        {
            this->gs_upper_bound = pair.second.or_value.otr_end;
        }
        time_order_map.emplace(pair.second.or_value.otr_begin, pair.second);
    }
    this->gs_time_order.clear();
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
    for (auto& pair : time_order_map) {
        auto& otr = pair.second.or_value;
        auto& full_desc = pair.second.or_description;
        for (auto& desc : pair.second.or_descriptions) {
            const auto& format_desc_defs
                = this->gs_opid_map[pair.second.or_name]
                      .odd_format_to_desc[desc.first];

            for (auto& desc_format_pairs : desc.second) {
                const auto& desc_def_v
                    = *format_desc_defs.find(desc_format_pairs.first)->second;
                for (size_t lpc = 0; lpc < desc_def_v.size(); lpc++) {
                    full_desc.append(desc_def_v[lpc].od_prefix);
                    full_desc.append(desc_format_pairs.second[lpc]);
                    full_desc.append(desc_def_v[lpc].od_suffix);
                }
            }
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
                for (const auto& sbr : {sbr_opid, sbr_desc}) {
                    if (filt->matches(nonstd::nullopt, sbr)) {
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

            if (min_log_time_opt && otr.otr_end < min_log_time_opt.value()) {
                filtered_out = true;
            }
            if (max_log_time_opt && max_log_time_opt.value() < otr.otr_begin) {
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
        if (pair.second.or_value.get_error_count() > 0) {
            bm_errs.insert_once(vis_line_t(this->gs_time_order.size()));
        } else if (pair.second.or_value
                       .otr_level_counts[log_level_t::LEVEL_WARNING]
                   > 0)
        {
            bm_warns.insert_once(vis_line_t(this->gs_time_order.size()));
        }
        this->gs_time_order.emplace_back(std::move(pair.second));
    }
    this->gs_opid_width = std::min(this->gs_opid_width, MAX_OPID_WIDTH);
    this->gs_total_width
        = std::max<size_t>(22 + this->gs_opid_width + max_desc_width,
                           1 + 23 + 10 + 23 + 1 /* header */);

    this->tss_view->set_needs_update();
}

nonstd::optional<vis_line_t>
gantt_source::row_for_time(struct timeval time_bucket)
{
    auto iter = std::lower_bound(this->gs_time_order.begin(),
                                 this->gs_time_order.end(),
                                 time_bucket,
                                 [](const opid_row& lhs, const timeval& rhs) {
                                     return lhs.or_value.otr_begin < rhs;
                                 });
    if (iter == this->gs_time_order.end()) {
        return nonstd::nullopt;
    }

    return vis_line_t(std::distance(this->gs_time_order.begin(), iter));
}

nonstd::optional<struct timeval>
gantt_source::time_for_row(vis_line_t row)
{
    if (row >= this->gs_time_order.size()) {
        return nonstd::nullopt;
    }

    return this->gs_time_order[row].or_value.otr_begin;
}

size_t
gantt_source::text_line_width(textview_curses& curses)
{
    return this->gs_total_width;
}

void
gantt_source::text_selection_changed(textview_curses& tc)
{
    static const size_t MAX_PREVIEW_LINES = 5;

    auto sel = tc.get_selection();

    this->gs_preview_source.clear();
    if (sel >= this->gs_time_order.size()) {
        return;
    }

    const auto& row = this->gs_time_order[sel];

    auto low_vl = this->gs_lss.row_for_time(row.or_value.otr_begin);
    auto high_tv = row.or_value.otr_end;
    high_tv.tv_sec += 1;
    auto high_vl = this->gs_lss.row_for_time(high_tv).value_or(
        this->gs_lss.text_line_count());

    if (!low_vl) {
        return;
    }

    auto preview_content = attr_line_t();
    auto msgs_remaining = size_t{MAX_PREVIEW_LINES};
    auto win = this->gs_lss.window_at(low_vl.value(), high_vl);
    auto id_hash = hash_str(row.or_name.data(), row.or_name.length());
    for (const auto& msg_line : win) {
        if (!msg_line.get_logline().match_opid_hash(id_hash)) {
            continue;
        }

        const auto& sa = msg_line.get_attrs();
        auto opid_opt = get_string_attr(sa, logline::L_OPID);

        if (!opid_opt) {
            continue;
        }
        auto opid_range = opid_opt.value().saw_string_attr->sa_range;
        auto opid_sf = msg_line.to_string(opid_range);

        if (opid_sf == row.or_name) {
            std::vector<attr_line_t> rows_al(1);

            this->gs_log_view.listview_value_for_rows(
                this->gs_log_view, msg_line.get_vis_line(), rows_al);

            preview_content.append(rows_al[0]).append("\n");
            msgs_remaining -= 1;
            if (msgs_remaining == 0) {
                break;
            }
        }
    }

    while (msgs_remaining > 0) {
        preview_content.append("\u2800\n");
        msgs_remaining -= 1;
    }

    this->gs_preview_source.replace_with(preview_content);
    this->gs_preview_status_source.get_description().set_value(
        " OPID %.*s", row.or_name.length(), row.or_name.data());
    auto err_count = row.or_value.get_error_count();
    if (err_count == 0) {
        this->gs_preview_status_source
            .statusview_value_for_field(gantt_status_source::TSF_ERRORS)
            .set_value("");
    } else if (err_count > 1) {
        this->gs_preview_status_source
            .statusview_value_for_field(gantt_status_source::TSF_ERRORS)
            .set_value("%'d errors", err_count);
    } else {
        this->gs_preview_status_source
            .statusview_value_for_field(gantt_status_source::TSF_ERRORS)
            .set_value("%'d error", err_count);
    }
    this->gs_preview_status_source
        .statusview_value_for_field(gantt_status_source::TSF_TOTAL)
        .set_value("%'d messages ", row.or_value.get_total_msgs());
}

void
gantt_source::text_filters_changed()
{
    this->rebuild_indexes();

    if (this->tss_view != nullptr) {
        this->tss_view->reload_data();
        this->tss_view->redo_search();
    }
}

int
gantt_source::get_filtered_count() const
{
    return this->gs_filtered_count;
}

int
gantt_source::get_filtered_count_for(size_t filter_index) const
{
    return this->gs_filter_hits[filter_index];
}
