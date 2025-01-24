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

#include "base/ansi_scrubber.hh"
#include "base/date_time_scanner.hh"
#include "base/humanize.hh"
#include "base/itertools.hh"
#include "base/time_util.hh"
#include "base/types.hh"
#include "config.h"
#include "hist_source_T.hh"
#include "scn/scan.h"
#include "yajlpp/json_ptr.hh"
#include "yajlpp/yajlpp_def.hh"

const char db_label_source::NULL_STR[] = "<NULL>";

constexpr size_t MAX_JSON_WIDTH = 16 * 1024;

struct user_row_style {
    std::map<std::string, style_config> urs_column_config;
};

static json_path_container col_style_handlers = {
    yajlpp::pattern_property_handler("(?<column_name>[^/]+)")
        .for_field(&user_row_style::urs_column_config)
        .with_children(style_config_handlers),
};

static typed_json_path_container<user_row_style> row_style_handlers
    = typed_json_path_container<user_row_style>{
        yajlpp::property_handler("columns")
            .with_children(col_style_handlers),
}.with_schema_id2("row-style");

line_info
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
    this->dls_ansi_attrs.clear();
    if (row < 0_vl || row >= (int) this->dls_rows.size()) {
        return {};
    }
    for (int lpc = 0; lpc < (int) this->dls_rows[row].size(); lpc++) {
        if (lpc == this->dls_row_style_index
            && !this->dls_row_styles_have_errors)
        {
            continue;
        }
        const auto& hm = this->dls_headers[lpc];
        auto actual_col_size
            = std::min(this->dls_max_column_width, hm.hm_column_size);
        auto cell_str = scrub_ws(this->dls_rows[row][lpc]);
        auto align = hm.hm_align;

        if (row < this->dls_row_styles.size()) {
            auto style_iter
                = this->dls_row_styles[row].rs_column_config.find(lpc);
            if (style_iter != this->dls_row_styles[row].rs_column_config.end())
            {
                if (style_iter->second.ta_align) {
                    align = style_iter->second.ta_align.value();
                }
            }
        }

        string_attrs_t cell_attrs;
        scrub_ansi_string(cell_str, &cell_attrs);
        truncate_to(cell_str, this->dls_max_column_width);

        auto cell_length
            = utf8_string_length(cell_str).unwrapOr(actual_col_size);
        auto padding = actual_col_size - cell_length;
        auto lpadding = 0;
        auto rpadding = padding;
        switch (align) {
            case text_align_t::start:
                break;
            case text_align_t::center: {
                lpadding = padding / 2;
                rpadding = padding - lpadding;
                break;
            }
            case text_align_t::end:
                lpadding = padding;
                rpadding = 0;
                break;
        }
        this->dls_cell_width[lpc] = cell_str.length() + padding;
        label_out.append(lpadding, ' ');
        shift_string_attrs(cell_attrs, 0, label_out.size());
        label_out.append(cell_str);
        label_out.append(rpadding, ' ');
        label_out.append(1, ' ');

        this->dls_ansi_attrs.insert(
            this->dls_ansi_attrs.end(), cell_attrs.begin(), cell_attrs.end());
    }

    return {};
}

void
db_label_source::text_attrs_for_line(textview_curses& tc,
                                     int row,
                                     string_attrs_t& sa)
{
    line_range lr(0, 0);
    const line_range lr2(0, -1);

    if (row < 0_vl || row >= (int) this->dls_rows.size()) {
        return;
    }
    sa = this->dls_ansi_attrs;
    auto alt_row_index = row % 4;
    if (alt_row_index == 2 || alt_row_index == 3) {
        sa.emplace_back(lr2, VC_ROLE.value(role_t::VCR_ALT_ROW));
    }
    for (size_t lpc = 0; lpc < this->dls_headers.size() - 1; lpc++) {
        if (lpc == this->dls_row_style_index
            && !this->dls_row_styles_have_errors)
        {
            continue;
        }

        const auto& hm = this->dls_headers[lpc];

        if (hm.is_graphable()) {
            lr.lr_end += this->dls_cell_width[lpc];
            sa.emplace_back(lr, VC_ROLE.value(role_t::VCR_NUMBER));
        }
        lr.lr_start += this->dls_cell_width[lpc];
        lr.lr_end = lr.lr_start + 1;
        sa.emplace_back(lr, VC_GRAPHIC.value(NCACS_VLINE));
        lr.lr_start += 1;
    }

    for (const auto& attr : sa) {
        require_ge(attr.sa_range.lr_start, 0);
    }
    int cell_start = 0;
    for (size_t lpc = 0; lpc < this->dls_headers.size(); lpc++) {
        std::optional<text_attrs> user_attrs;

        if (lpc == this->dls_row_style_index) {
            if (!this->dls_row_styles_have_errors) {
                continue;
            }
        }

        auto cell_view = std::string_view{this->dls_rows[row][lpc]};
        const auto& hm = this->dls_headers[lpc];

        if (row < this->dls_row_styles.size()) {
            auto style_iter
                = this->dls_row_styles[row].rs_column_config.find(lpc);
            if (style_iter != this->dls_row_styles[row].rs_column_config.end())
            {
                user_attrs = style_iter->second;
            }
        }

        int left = cell_start;
        if (hm.is_graphable()) {
            auto num_scan_res = humanize::try_from<double>(
                string_fragment::from_string_view(cell_view));
            if (num_scan_res) {
                hm.hm_chart.chart_attrs_for_value(tc,
                                                  left,
                                                  this->dls_cell_width[lpc],
                                                  hm.hm_name,
                                                  num_scan_res.value(),
                                                  sa,
                                                  user_attrs);

                for (const auto& attr : sa) {
                    require_ge(attr.sa_range.lr_start, 0);
                }
            }
        } else if (user_attrs.has_value()) {
            auto stlr = line_range{
                cell_start,
                (int) (cell_start + this->dls_cell_width[lpc]),
            };
            sa.emplace_back(stlr, VC_STYLE.value(user_attrs.value()));
        }

        if (lpc == this->dls_row_style_index) {
            auto stlr = line_range{
                cell_start,
                (int) (cell_start + this->dls_cell_width[lpc]),
            };
            sa.emplace_back(stlr, VC_ROLE.value(role_t::VCR_ERROR));
        } else if (cell_view.length() > 2 && cell_view.length() < MAX_JSON_WIDTH
                   && ((cell_view.front() == '{' && cell_view.back() == '}')
                       || (cell_view.front() == '['
                           && cell_view.back() == ']')))
        {
            json_ptr_walk jpw;

            if (jpw.parse(cell_view.data(), cell_view.length())
                    == yajl_status_ok
                && jpw.complete_parse() == yajl_status_ok)
            {
                for (const auto& jpw_value : jpw.jpw_values) {
                    if (jpw_value.wt_type != yajl_t_number) {
                        continue;
                    }

                    auto num_scan_res = humanize::try_from<double>(
                        string_fragment::from_str(jpw_value.wt_value));

                    if (num_scan_res) {
                        hm.hm_chart.chart_attrs_for_value(
                            tc,
                            left,
                            this->dls_cell_width[lpc],
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
        cell_start += this->dls_cell_width[lpc] + 1;
    }

    for (const auto& attr : sa) {
        require_ge(attr.sa_range.lr_start, 0);
    }
}

void
db_label_source::set_col_as_graphable(int lpc)
{
    static auto& vc = view_colors::singleton();

    auto& hm = this->dls_headers[lpc];
    auto name_for_ident_attrs = hm.hm_name;
    auto attrs = vc.attrs_for_ident(name_for_ident_attrs);
    for (size_t attempt = 0; hm.hm_chart.attrs_in_use(attrs) && attempt < 3;
         attempt++)
    {
        name_for_ident_attrs += " ";
        attrs = vc.attrs_for_ident(name_for_ident_attrs);
    }
    hm.hm_graphable = true;
    hm.hm_chart.with_attrs_for_ident(hm.hm_name, attrs);
    hm.hm_title_attrs = attrs | text_attrs::with_reverse();
    hm.hm_column_size = std::max(hm.hm_column_size, size_t{10});
}

void
db_label_source::push_header(const std::string& colstr, int type)
{
    this->dls_headers.emplace_back(colstr);
    this->dls_cell_width.push_back(0);

    auto& hm = this->dls_headers.back();

    hm.hm_column_size = utf8_string_length(colstr).unwrapOr(colstr.length());
    hm.hm_column_type = type;
    if (colstr == "log_time" || colstr == "min(log_time)") {
        this->dls_time_column_index = this->dls_headers.size() - 1;
    }
    if (colstr == "__lnav_style__") {
        this->dls_row_style_index = this->dls_headers.size() - 1;
    }
    hm.hm_chart.with_show_state(stacked_bar_chart_base::show_all{});
}

void
db_label_source::push_column(const scoped_value_t& sv)
{
    auto row_index = this->dls_rows.size() - 1;
    auto& vc = view_colors::singleton();
    int index = this->dls_rows.back().size();
    auto& hm = this->dls_headers[index];

    auto col_sf = sv.match(
        [](const std::string& str) { return string_fragment::from_str(str); },
        [this, &index](const string_fragment& sf) {
            if (this->dls_row_style_index == index) {
                return string_fragment{};
            }
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
        timeval tv;

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
    } else if (index == this->dls_row_style_index) {
        if (sv.is<null_value_t>()) {
            this->dls_row_styles.emplace_back(row_style{});
        } else if (sv.is<string_fragment>()) {
            static const intern_string_t SRC
                = intern_string::lookup("__lnav_style__");
            auto frag = sv.get<string_fragment>();
            if (frag.empty()) {
                this->dls_row_styles.emplace_back(row_style{});
            } else {
                auto parse_res = row_style_handlers.parser_for(SRC).of(frag);
                if (parse_res.isErr()) {
                    log_error("DB row %d JSON is invalid:", row_index);
                    auto errors = parse_res.unwrapErr();
                    for (const auto& err : errors) {
                        log_error("  %s", err.to_attr_line().al_string.c_str());
                    }
                    col_sf = string_fragment::from_str(
                                 errors[0].to_attr_line().al_string)
                                 .to_owned(*this->dls_allocator);
                    this->dls_row_styles_have_errors = true;
                } else {
                    auto urs = parse_res.unwrap();
                    auto rs = row_style{};
                    for (const auto& [col_name, col_style] :
                         urs.urs_column_config)
                    {
                        auto col_index_opt
                            = this->column_name_to_index(col_name);
                        if (!col_index_opt) {
                            log_error("DB row %d column name '%s' not found",
                                      row_index,
                                      col_name.c_str());
                            col_sf = string_fragment::from_str(
                                         fmt::format(
                                             FMT_STRING(
                                                 "column name '{}' not found"),
                                             col_name))
                                         .to_owned(*this->dls_allocator);
                            this->dls_row_styles_have_errors = true;
                        } else {
                            text_attrs ta;

                            auto fg_res = styling::color_unit::from_str(
                                col_style.sc_color);
                            if (fg_res.isErr()) {
                                log_error("DB row %d color is invalid: %s",
                                          row_index,
                                          fg_res.unwrapErr().c_str());
                                col_sf
                                    = string_fragment::from_str(
                                          fmt::format(
                                              FMT_STRING("invalid color: {}"),
                                              fg_res.unwrapErr()))
                                          .to_owned(*this->dls_allocator);
                                this->dls_row_styles_have_errors = true;
                            } else {
                                ta.ta_fg_color
                                    = vc.match_color(fg_res.unwrap());
                            }
                            auto bg_res = styling::color_unit::from_str(
                                col_style.sc_background_color);
                            if (bg_res.isErr()) {
                                log_error(
                                    "DB row %d background-color is invalid: %s",
                                    row_index,
                                    bg_res.unwrapErr().c_str());
                                col_sf = string_fragment::from_str(
                                             fmt::format(
                                                 FMT_STRING(
                                                     "invalid "
                                                     "background-color: {}"),
                                                 fg_res.unwrapErr()))
                                             .to_owned(*this->dls_allocator);
                                this->dls_row_styles_have_errors = true;
                            } else {
                                ta.ta_bg_color
                                    = vc.match_color(bg_res.unwrap());
                            }
                            ta.ta_align = col_style.sc_text_align;
                            if (col_style.sc_underline) {
                                ta |= text_attrs::style::underline;
                            }
                            if (col_style.sc_bold) {
                                ta |= text_attrs::style::bold;
                            }
                            if (col_style.sc_italic) {
                                ta |= text_attrs::style::italic;
                            }
                            if (col_style.sc_strike) {
                                ta |= text_attrs::style::struck;
                            }
                            if (this->dls_headers[col_index_opt.value()]
                                    .is_graphable())
                            {
                                this->dls_headers[col_index_opt.value()]
                                    .hm_title_attrs
                                    = text_attrs::with_underline();
                            }
                            rs.rs_column_config[col_index_opt.value()] = ta;
                        }
                    }
                    this->dls_row_styles.emplace_back(std::move(rs));
                }
            }
        } else {
            log_error("DB row %d is not a string -- %s",
                      row_index,
                      mapbox::util::apply_visitor(type_visitor(), sv));

            col_sf
                = string_fragment::from_str("expecting a JSON object for style")
                      .to_owned(*this->dls_allocator);
            this->dls_row_styles_have_errors = true;
        }
    }

    this->dls_rows.back().push_back(col_sf.data());
    hm.hm_column_size
        = std::max(this->dls_headers[index].hm_column_size,
                   (size_t) utf8_string_length(col_sf.data(), col_sf.length())
                       .unwrapOr(col_sf.length()));

    if (hm.is_graphable()) {
        if (sv.is<int64_t>()) {
            hm.hm_chart.add_value(hm.hm_name, sv.get<int64_t>());
        } else if (sv.is<double>()) {
            hm.hm_chart.add_value(hm.hm_name, sv.get<double>());
        } else if (sv.is<string_fragment>()) {
            auto sf = sv.get<string_fragment>();
            auto num_from_res = humanize::try_from<double>(sf);
            if (num_from_res) {
                hm.hm_chart.add_value(hm.hm_name, num_from_res.value());
            }
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
                    hm.hm_chart.add_value(jpw_value.wt_ptr,
                                          num_scan_res->value());
                    hm.hm_chart.with_attrs_for_ident(
                        jpw_value.wt_ptr, vc.attrs_for_ident(jpw_value.wt_ptr));
                }
            }
        }
    }
    hm.hm_chart.next_row();
}

void
db_label_source::clear()
{
    this->dls_headers.clear();
    this->dls_rows.clear();
    this->dls_time_column.clear();
    this->dls_time_column_index = -1;
    this->dls_cell_width.clear();
    this->dls_row_styles.clear();
    this->dls_row_styles_have_errors = false;
    this->dls_row_style_index = -1;
    this->dls_allocator = std::make_unique<ArenaAlloc::Alloc<char>>(64 * 1024);
}

std::optional<size_t>
db_label_source::column_name_to_index(const std::string& name) const
{
    return this->dls_headers | lnav::itertools::find(name);
}

std::optional<vis_line_t>
db_label_source::row_for_time(struct timeval time_bucket)
{
    const auto iter = std::lower_bound(this->dls_time_column.begin(),
                                       this->dls_time_column.end(),
                                       time_bucket);
    if (iter != this->dls_time_column.end()) {
        return vis_line_t(std::distance(this->dls_time_column.begin(), iter));
    }
    return std::nullopt;
}

std::optional<text_time_translator::row_info>
db_label_source::time_for_row(vis_line_t row)
{
    if ((row < 0_vl) || (((size_t) row) >= this->dls_time_column.size())) {
        return std::nullopt;
    }

    return row_info{this->dls_time_column[row], row};
}

std::optional<attr_line_t>
db_overlay_source::list_header_for_overlay(const listview_curses& lv,
                                           vis_line_t line)
{
    attr_line_t retval;

    retval.append(" JSON column details");
    return retval;
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
                const auto& header = this->dos_labels->dls_headers[col].hm_name;
                value_out.emplace_back(" Column: " + header);

                retval += 1;
            }

            stacked_bar_chart<std::string> chart;
            int start_line = value_out.size();

            chart.with_stacking_enabled(false)
                .with_margins(3, 0)
                .with_show_state(stacked_bar_chart_base::show_all{});

            for (auto& jpw_value : jpw.jpw_values) {
                value_out.emplace_back("   " + jpw_value.wt_ptr + " = "
                                       + jpw_value.wt_value);

                auto& sa = value_out.back().get_attrs();
                line_range lr(1, 2);

                sa.emplace_back(lr, VC_GRAPHIC.value(NCACS_LTEE));
                lr.lr_start = 3 + jpw_value.wt_ptr.size() + 3;
                lr.lr_end = -1;
                sa.emplace_back(lr, VC_STYLE.value(text_attrs::with_bold()));

                if (jpw_value.wt_type == yajl_t_number) {
                    auto num_scan_res
                        = scn::scan_value<double>(jpw_value.wt_value);

                    if (num_scan_res) {
                        auto attrs = vc.attrs_for_ident(jpw_value.wt_ptr);

                        chart.add_value(jpw_value.wt_ptr,
                                        num_scan_res->value());
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

                auto num_scan_res = humanize::try_from<double>(iter->wt_value);

                if (num_scan_res) {
                    auto& sa = value_out[curr_line].get_attrs();
                    int left = 3;

                    chart.chart_attrs_for_value(lv,
                                                left,
                                                width,
                                                iter->wt_ptr,
                                                num_scan_res.value(),
                                                sa);
                }
            }
        }
    }

    if (retval > 1) {
        value_out.emplace_back("");

        auto& sa = value_out.back().get_attrs();
        line_range lr(1, 2);

        sa.emplace_back(lr, VC_GRAPHIC.value(NCACS_LLCORNER));
        lr.lr_start = 2;
        lr.lr_end = -1;
        sa.emplace_back(lr, VC_GRAPHIC.value(NCACS_HLINE));

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
    const auto* dls = this->dos_labels;
    auto& sa = value_out.get_attrs();

    for (size_t lpc = 0; lpc < this->dos_labels->dls_headers.size(); lpc++) {
        if (lpc == this->dos_labels->dls_row_style_index
            && !this->dos_labels->dls_row_styles_have_errors)
        {
            continue;
        }

        const auto& hm = dls->dls_headers[lpc];
        auto actual_col_size
            = std::min(dls->dls_max_column_width, hm.hm_column_size);
        auto cell_title = hm.hm_name;
        string_attrs_t cell_attrs;
        scrub_ansi_string(cell_title, &cell_attrs);
        truncate_to(cell_title, dls->dls_max_column_width);

        auto cell_length
            = utf8_string_length(cell_title).unwrapOr(actual_col_size);
        int total_fill = actual_col_size - cell_length;
        auto line_len_before = line.length();

        int before = total_fill / 2;
        total_fill -= before;
        line.append(before, ' ');
        shift_string_attrs(cell_attrs, 0, line.size());
        line.append(cell_title);
        line.append(total_fill, ' ');
        auto header_range = line_range(line_len_before, line.length());

        line.append(1, ' ');

        require_ge(header_range.lr_start, 0);

        sa.emplace_back(header_range, VC_STYLE.value(hm.hm_title_attrs));
        sa.insert(sa.end(), cell_attrs.begin(), cell_attrs.end());
    }

    line_range lr(0);

    sa.emplace_back(
        lr,
        VC_STYLE.value(text_attrs::with_styles(text_attrs::style::bold,
                                               text_attrs::style::underline)));
    return true;
}
