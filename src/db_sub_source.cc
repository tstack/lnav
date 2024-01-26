/**
 * Copyright (c) 2014, Timothy Stack
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

#include "db_sub_source.hh"

#include "base/date_time_scanner.hh"
#include "base/itertools.hh"
#include "base/time_util.hh"
#include "config.h"
#include "scn/scn.h"
#include "yajlpp/json_ptr.hh"

const char db_label_source::NULL_STR[] = "<NULL>";

constexpr size_t MAX_JSON_WIDTH = 16 * 1024;

void
db_label_source::text_value_for_line(textview_curses& tc,
                                     int row,
                                     std::string& label_out,
                                     text_sub_source::line_flags_t flags)
{
    /*
     * start_value is the result rowid, each bucket type is a column value
     * label_out should be the raw text output.
     */

    label_out.clear();
    if (row < 0_vl || row >= (int) this->dls_rows.size()) {
        return;
    }
    for (int lpc = 0; lpc < (int) this->dls_rows[row].size(); lpc++) {
        auto actual_col_size = std::min(this->dls_max_column_width,
                                        this->dls_headers[lpc].hm_column_size);
        auto cell_str = scrub_ws(this->dls_rows[row][lpc]);

        truncate_to(cell_str, this->dls_max_column_width);

        auto cell_length
            = utf8_string_length(cell_str).unwrapOr(actual_col_size);
        auto padding = actual_col_size - cell_length;
        this->dls_cell_width[lpc] = cell_str.length() + padding;
        if (this->dls_headers[lpc].hm_column_type != SQLITE3_TEXT) {
            label_out.append(padding, ' ');
        }
        label_out.append(cell_str);
        if (this->dls_headers[lpc].hm_column_type == SQLITE3_TEXT) {
            label_out.append(padding, ' ');
        }
        label_out.append(1, ' ');
    }
}

void
db_label_source::text_attrs_for_line(textview_curses& tc,
                                     int row,
                                     string_attrs_t& sa)
{
    struct line_range lr(0, 0);
    const struct line_range lr2(0, -1);

    if (row < 0_vl || row >= (int) this->dls_rows.size()) {
        return;
    }
    auto alt_row_index = row % 4;
    if (alt_row_index == 2 || alt_row_index == 3) {
        sa.emplace_back(lr2, VC_ROLE.value(role_t::VCR_ALT_ROW));
    }
    for (size_t lpc = 0; lpc < this->dls_headers.size() - 1; lpc++) {
        const auto& hm = this->dls_headers[lpc];

        if (hm.hm_graphable) {
            lr.lr_end += this->dls_cell_width[lpc];
            sa.emplace_back(lr, VC_ROLE.value(role_t::VCR_NUMBER));
        }
        lr.lr_start += this->dls_cell_width[lpc];
        lr.lr_end = lr.lr_start + 1;
        sa.emplace_back(lr, VC_GRAPHIC.value(ACS_VLINE));
        lr.lr_start += 1;
    }

    for (const auto& attr : sa) {
        require_ge(attr.sa_range.lr_start, 0);
    }
    int left = 0;
    for (size_t lpc = 0; lpc < this->dls_headers.size(); lpc++) {
        auto row_view = scn::string_view{this->dls_rows[row][lpc]};
        const auto& hm = this->dls_headers[lpc];

        if (hm.hm_graphable) {
            auto num_scan_res = scn::scan_value<double>(row_view);

            if (num_scan_res) {
                this->dls_chart.chart_attrs_for_value(
                    tc, left, hm.hm_name, num_scan_res.value(), sa);

                for (const auto& attr : sa) {
                    require_ge(attr.sa_range.lr_start, 0);
                }
            }
        }
        if (row_view.length() > 2 && row_view.length() < MAX_JSON_WIDTH
            && ((row_view.front() == '{' && row_view.back() == '}')
                || (row_view.front() == '[' && row_view.back() == ']')))
        {
            json_ptr_walk jpw;

            if (jpw.parse(row_view.data(), row_view.length()) == yajl_status_ok
                && jpw.complete_parse() == yajl_status_ok)
            {
                for (const auto& jpw_value : jpw.jpw_values) {
                    if (jpw_value.wt_type != yajl_t_number) {
                        continue;
                    }

                    auto num_scan_res
                        = scn::scan_value<double>(jpw_value.wt_value);

                    if (num_scan_res) {
                        this->dls_chart.chart_attrs_for_value(
                            tc,
                            left,
                            jpw_value.wt_ptr,
                            num_scan_res.value(),
                            sa);
                        for (const auto& attr : sa) {
                            require_ge(attr.sa_range.lr_start, 0);
                        }
                    }
                }
            }
        }
    }

    for (const auto& attr : sa) {
        require_ge(attr.sa_range.lr_start, 0);
    }
}

void
db_label_source::push_header(const std::string& colstr,
                             int type,
                             bool graphable)
{
    this->dls_headers.emplace_back(colstr);
    this->dls_cell_width.push_back(0);

    header_meta& hm = this->dls_headers.back();

    hm.hm_column_size = utf8_string_length(colstr).unwrapOr(colstr.length());
    hm.hm_column_type = type;
    hm.hm_graphable = graphable;
    if (colstr == "log_time" || colstr == "min(log_time)") {
        this->dls_time_column_index = this->dls_headers.size() - 1;
    }
}

void
db_label_source::push_column(const scoped_value_t& sv)
{
    auto& vc = view_colors::singleton();
    int index = this->dls_rows.back().size();
    auto& hm = this->dls_headers[index];

    auto col_sf = sv.match(
        [](const std::string& str) { return string_fragment::from_str(str); },
        [this](const string_fragment& sf) {
            return sf.to_owned(*this->dls_allocator);
        },
        [this](int64_t i) {
            fmt::memory_buffer buf;

            fmt::format_to(std::back_inserter(buf), FMT_STRING("{}"), i);
            return string_fragment::from_memory_buffer(buf).to_owned(
                *this->dls_allocator);
        },
        [this](double d) {
            fmt::memory_buffer buf;

            fmt::format_to(std::back_inserter(buf), FMT_STRING("{}"), d);
            return string_fragment::from_memory_buffer(buf).to_owned(
                *this->dls_allocator);
        },
        [](null_value_t) { return string_fragment::from_const(NULL_STR); });

    if (index == this->dls_time_column_index) {
        date_time_scanner dts;
        struct timeval tv;

        if (!dts.convert_to_timeval(
                col_sf.data(), col_sf.length(), nullptr, tv))
        {
            tv.tv_sec = -1;
            tv.tv_usec = -1;
        }
        if (!this->dls_time_column.empty() && tv < this->dls_time_column.back())
        {
            this->dls_time_column_invalidated_at = this->dls_time_column.size();
            this->dls_time_column_index = -1;
            this->dls_time_column.clear();
        } else {
            this->dls_time_column.push_back(tv);
        }
    }

    this->dls_rows.back().push_back(col_sf.data());
    hm.hm_column_size
        = std::max(this->dls_headers[index].hm_column_size,
                   (size_t) utf8_string_length(col_sf.data(), col_sf.length())
                       .unwrapOr(col_sf.length()));

    if ((sv.is<int64_t>() || sv.is<double>())
        && this->dls_headers[index].hm_graphable)
    {
        if (sv.is<int64_t>()) {
            this->dls_chart.add_value(hm.hm_name, sv.get<int64_t>());
        } else {
            this->dls_chart.add_value(hm.hm_name, sv.get<double>());
        }
    } else if (col_sf.length() > 2
               && ((col_sf.startswith("{") && col_sf.endswith("}"))
                   || (col_sf.startswith("[") && col_sf.endswith("]"))))
    {
        json_ptr_walk jpw;

        if (jpw.parse(col_sf.data(), col_sf.length()) == yajl_status_ok
            && jpw.complete_parse() == yajl_status_ok)
        {
            for (const auto& jpw_value : jpw.jpw_values) {
                if (jpw_value.wt_type != yajl_t_number) {
                    continue;
                }

                auto num_scan_res = scn::scan_value<double>(jpw_value.wt_value);
                if (num_scan_res) {
                    this->dls_chart.add_value(jpw_value.wt_ptr,
                                              num_scan_res.value());
                    this->dls_chart.with_attrs_for_ident(
                        jpw_value.wt_ptr, vc.attrs_for_ident(jpw_value.wt_ptr));
                }
            }
        }
    }
}

void
db_label_source::clear()
{
    this->dls_chart.clear();
    this->dls_headers.clear();
    this->dls_rows.clear();
    this->dls_time_column.clear();
    this->dls_cell_width.clear();
    this->dls_allocator = std::make_unique<ArenaAlloc::Alloc<char>>(64 * 1024);
}

nonstd::optional<size_t>
db_label_source::column_name_to_index(const std::string& name) const
{
    return this->dls_headers | lnav::itertools::find(name);
}

nonstd::optional<vis_line_t>
db_label_source::row_for_time(struct timeval time_bucket)
{
    std::vector<struct timeval>::iterator iter;

    iter = std::lower_bound(this->dls_time_column.begin(),
                            this->dls_time_column.end(),
                            time_bucket);
    if (iter != this->dls_time_column.end()) {
        return vis_line_t(std::distance(this->dls_time_column.begin(), iter));
    }
    return nonstd::nullopt;
}

nonstd::optional<struct timeval>
db_label_source::time_for_row(vis_line_t row)
{
    if ((row < 0_vl) || (((size_t) row) >= this->dls_time_column.size())) {
        return nonstd::nullopt;
    }

    return this->dls_time_column[row];
}

void
db_overlay_source::list_value_for_overlay(const listview_curses& lv,
                                          vis_line_t row,
                                          std::vector<attr_line_t>& value_out)
{
    size_t retval = 1;

    if (!this->dos_active || lv.get_inner_height() == 0) {
        return;
    }

    if (row != lv.get_selection()) {
        return;
    }

    auto& vc = view_colors::singleton();
    const auto& cols = this->dos_labels->dls_rows[row];
    unsigned long width;
    vis_line_t height;

    lv.get_dimensions(height, width);

    for (size_t col = 0; col < cols.size(); col++) {
        const char* col_value = cols[col];
        size_t col_len = strlen(col_value);

        if (!(col_len >= 2
              && ((col_value[0] == '{' && col_value[col_len - 1] == '}')
                  || (col_value[0] == '[' && col_value[col_len - 1] == ']'))))
        {
            continue;
        }

        json_ptr_walk jpw;

        if (jpw.parse(col_value, col_len) == yajl_status_ok
            && jpw.complete_parse() == yajl_status_ok)
        {
            {
                const std::string& header
                    = this->dos_labels->dls_headers[col].hm_name;
                value_out.emplace_back(" JSON Column: " + header);

                retval += 1;
            }

            stacked_bar_chart<std::string> chart;
            int start_line = value_out.size();

            chart.with_stacking_enabled(false).with_margins(3, 0);

            for (auto& jpw_value : jpw.jpw_values) {
                value_out.emplace_back("   " + jpw_value.wt_ptr + " = "
                                       + jpw_value.wt_value);

                string_attrs_t& sa = value_out.back().get_attrs();
                struct line_range lr(1, 2);

                sa.emplace_back(lr, VC_GRAPHIC.value(ACS_LTEE));
                lr.lr_start = 3 + jpw_value.wt_ptr.size() + 3;
                lr.lr_end = -1;
                sa.emplace_back(lr, VC_STYLE.value(text_attrs{A_BOLD}));

                if (jpw_value.wt_type == yajl_t_number) {
                    auto num_scan_res
                        = scn::scan_value<double>(jpw_value.wt_value);

                    if (num_scan_res) {
                        auto attrs = vc.attrs_for_ident(jpw_value.wt_ptr);

                        chart.add_value(jpw_value.wt_ptr, num_scan_res.value());
                        chart.with_attrs_for_ident(jpw_value.wt_ptr, attrs);
                    }
                }

                retval += 1;
            }

            int curr_line = start_line;
            for (auto iter = jpw.jpw_values.begin();
                 iter != jpw.jpw_values.end();
                 ++iter, curr_line++)
            {
                if (iter->wt_type != yajl_t_number) {
                    continue;
                }

                auto num_scan_res = scn::scan_value<double>(iter->wt_value);

                if (num_scan_res) {
                    auto& sa = value_out[curr_line].get_attrs();
                    int left = 3;

                    chart.chart_attrs_for_value(
                        lv, left, iter->wt_ptr, num_scan_res.value(), sa);
                }
            }
        }
    }

    if (retval > 1) {
        value_out.emplace_back("");

        string_attrs_t& sa = value_out.back().get_attrs();
        struct line_range lr(1, 2);

        sa.emplace_back(lr, VC_GRAPHIC.value(ACS_LLCORNER));
        lr.lr_start = 2;
        lr.lr_end = -1;
        sa.emplace_back(lr, VC_GRAPHIC.value(ACS_HLINE));

        retval += 1;
    }
}

bool
db_overlay_source::list_static_overlay(const listview_curses& lv,
                                       int y,
                                       int bottom,
                                       attr_line_t& value_out)
{
    if (y != 0) {
        return false;
    }

    auto& line = value_out.get_string();
    auto* dls = this->dos_labels;
    auto& sa = value_out.get_attrs();

    for (size_t lpc = 0; lpc < this->dos_labels->dls_headers.size(); lpc++) {
        auto actual_col_size = std::min(dls->dls_max_column_width,
                                        dls->dls_headers[lpc].hm_column_size);
        std::string cell_title = dls->dls_headers[lpc].hm_name;

        truncate_to(cell_title, dls->dls_max_column_width);

        auto cell_length
            = utf8_string_length(cell_title).unwrapOr(actual_col_size);
        int before, total_fill = actual_col_size - cell_length;
        auto line_len_before = line.length();

        before = total_fill / 2;
        total_fill -= before;
        line.append(before, ' ');
        line.append(cell_title);
        line.append(total_fill, ' ');
        line.append(1, ' ');

        struct line_range header_range(line_len_before, line.length());

        require_ge(header_range.lr_start, 0);

        text_attrs attrs;
        if (this->dos_labels->dls_headers[lpc].hm_graphable) {
            attrs
                = dls->dls_headers[lpc].hm_title_attrs | text_attrs{A_REVERSE};
        } else {
            attrs.ta_attrs = A_UNDERLINE;
        }
        sa.emplace_back(header_range, VC_STYLE.value(attrs));
    }

    struct line_range lr(0);

    sa.emplace_back(lr, VC_STYLE.value(text_attrs{A_BOLD | A_UNDERLINE}));
    return true;
}
