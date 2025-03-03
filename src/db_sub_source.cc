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

#include <iterator>

#include "db_sub_source.hh"

#include "base/ansi_scrubber.hh"
#include "base/date_time_scanner.hh"
#include "base/humanize.hh"
#include "base/itertools.enumerate.hh"
#include "base/itertools.hh"
#include "base/math_util.hh"
#include "base/time_util.hh"
#include "base/types.hh"
#include "config.h"
#include "hist_source_T.hh"
#include "scn/scan.h"
#include "yajlpp/json_ptr.hh"
#include "yajlpp/yajlpp_def.hh"

using namespace lnav::roles::literals;

const unsigned char db_label_source::NULL_STR[] = "<NULL>";

constexpr size_t MAX_JSON_WIDTH = 16 * 1024;

struct user_row_style {
    std::map<std::string, style_config> urs_column_config;
};

static const typed_json_path_container<user_row_style>&
get_row_style_handlers()
{
    static const json_path_container col_style_handlers = {
        yajlpp::pattern_property_handler("(?<column_name>[^/]+)")
            .for_field(&user_row_style::urs_column_config)
            .with_children(style_config_handlers),
    };

    static const typed_json_path_container<user_row_style> retval
        = typed_json_path_container<user_row_style>{
            yajlpp::property_handler("columns")
                .with_children(col_style_handlers),
    }.with_schema_id2("row-style");

    return retval;
}

line_info
db_label_source::text_value_for_line(textview_curses& tc,
                                     int row,
                                     std::string& label_out,
                                     line_flags_t flags)
{
    /*
     * start_value is the result rowid, each bucket type is a column value
     * label_out should be the raw text output.
     */

    label_out.clear();
    this->dls_ansi_attrs.clear();
    label_out.reserve(this->dls_max_column_width * this->dls_headers.size());
    if (row < 0_vl || row >= (int) this->dls_row_cursors.size()) {
        return {};
    }
    std::optional<log_level_t> row_level;
    auto cell_cursor = this->dls_row_cursors[row].sync();
    for (int lpc = 0; lpc < (int) this->dls_headers.size();
         lpc++, cell_cursor = cell_cursor->next())
    {
        if (lpc == this->dls_row_style_column
            && !this->dls_row_styles_have_errors)
        {
            continue;
        }
        const auto& hm = this->dls_headers[lpc];
        if (hm.hm_hidden) {
            continue;
        }
        auto actual_col_size
            = std::min(this->dls_max_column_width, hm.hm_column_size);
        auto align = hm.hm_align;

        if (row < this->dls_row_styles.size()) {
            auto style_iter
                = this->dls_row_styles[row].rs_column_config.find(lpc);
            if (style_iter != this->dls_row_styles[row].rs_column_config.end())
            {
                if (style_iter->second.ta_align.has_value()) {
                    align = style_iter->second.ta_align.value();
                }
            }
        }

        auto sf = cell_cursor->to_string_fragment(this->dls_cell_allocator);
        auto al = attr_line_t::from_table_cell_content(
            sf, this->dls_max_column_width);

        if (this->tss_view != nullptr
            && cell_cursor->get_type() == lnav::cell_type::CT_TEXT)
        {
            this->tss_view->apply_highlights(
                al, line_range::empty_at(0), line_range::empty_at(0));
        }
        if (this->dls_level_column && lpc == this->dls_level_column.value()) {
            row_level = string2level(sf.data(), sf.length());
        }

        auto cell_length = al.utf8_length_or_length();
        if (actual_col_size < cell_length) {
            log_warning(
                "invalid column size: actual_col_size=%d < cell_length=%d",
                actual_col_size,
                cell_length);
            cell_length = actual_col_size;
        }
        const auto padding = actual_col_size - cell_length;
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
        this->dls_cell_width[lpc] = al.al_string.length() + padding;
        label_out.append(lpadding, ' ');
        shift_string_attrs(al.al_attrs, 0, label_out.size());
        label_out.append(std::move(al.al_string));
        label_out.append(rpadding, ' ');
        label_out.push_back(' ');

        this->dls_ansi_attrs.insert(
            this->dls_ansi_attrs.end(),
            std::make_move_iterator(al.al_attrs.begin()),
            std::make_move_iterator(al.al_attrs.end()));
    }
    if (row_level.has_value()) {
        this->dls_ansi_attrs.emplace_back(line_range{0, -1},
                                          SA_LEVEL.value(row_level.value()));
    }
    this->dls_ansi_attrs.reserve(this->dls_ansi_attrs.size()
                                 + 3 * this->dls_headers.size());
    this->dls_cell_allocator.reset();

    return {};
}

void
db_label_source::text_attrs_for_line(textview_curses& tc,
                                     int row,
                                     string_attrs_t& sa)
{
    static const auto NUM_ATTR = VC_ROLE.value(role_t::VCR_NUMBER);
    static const auto VLINE_ATTR = VC_GRAPHIC.value(NCACS_VLINE);

    line_range lr(0, 0);
    const line_range lr2(0, -1);

    if (row < 0_vl || row >= (int) this->dls_row_cursors.size()) {
        return;
    }
    sa = std::move(this->dls_ansi_attrs);
    auto alt_row_index = row % 4;
    if (alt_row_index == 2 || alt_row_index == 3) {
        sa.emplace_back(lr2, VC_ROLE.value(role_t::VCR_ALT_ROW));
    }
    sa.emplace_back(line_range{0, 0}, SA_ORIGINAL_LINE.value());
    sa.emplace_back(line_range{0, 0}, SA_BODY.value());
    for (size_t lpc = 0; lpc < this->dls_headers.size() - 1; lpc++) {
        if (lpc == this->dls_row_style_column
            && !this->dls_row_styles_have_errors)
        {
            continue;
        }

        const auto& hm = this->dls_headers[lpc];
        if (hm.hm_hidden) {
            continue;
        }

        if (hm.is_graphable()) {
            lr.lr_end += this->dls_cell_width[lpc];
            sa.emplace_back(lr, NUM_ATTR);
        }
        lr.lr_start += this->dls_cell_width[lpc];
        lr.lr_end = lr.lr_start + 1;
        sa.emplace_back(lr, VLINE_ATTR);
        lr.lr_start += 1;
    }

    for (const auto& attr : sa) {
        require_ge(attr.sa_range.lr_start, 0);
    }
    int cell_start = 0;
    auto cursor = this->dls_row_cursors[row].sync();
    for (size_t lpc = 0; lpc < this->dls_headers.size();
         lpc++, cursor = cursor->next())
    {
        std::optional<text_attrs> user_attrs;

        if (lpc == this->dls_row_style_column) {
            if (!this->dls_row_styles_have_errors) {
                continue;
            }
        }

        const auto& hm = this->dls_headers[lpc];
        if (hm.hm_hidden) {
            continue;
        }
        if (row < this->dls_row_styles.size()) {
            auto style_iter
                = this->dls_row_styles[row].rs_column_config.find(lpc);
            if (style_iter != this->dls_row_styles[row].rs_column_config.end())
            {
                user_attrs = style_iter->second;
            }
        }

        int left = cell_start;
        auto stlr = line_range{
            cell_start,
            (int) (cell_start + this->dls_cell_width[lpc]),
        };
        if (hm.is_graphable()) {
            std::optional<double> get_res;
            if (cursor->get_type() == lnav::cell_type::CT_INTEGER) {
                get_res = cursor->get_int();
            } else if (cursor->get_type() == lnav::cell_type::CT_FLOAT) {
                get_res = cursor->get_float();
            }
            if (get_res.has_value()) {
                hm.hm_chart.chart_attrs_for_value(tc,
                                                  left,
                                                  this->dls_cell_width[lpc],
                                                  hm.hm_name,
                                                  get_res.value(),
                                                  sa,
                                                  user_attrs);

                for (const auto& attr : sa) {
                    require_ge(attr.sa_range.lr_start, 0);
                }
            }
        } else if (user_attrs.has_value()) {
            sa.emplace_back(stlr, VC_STYLE.value(user_attrs.value()));
        }
        auto cell_sf = string_fragment::invalid();
        if (cursor->get_type() == lnav::cell_type::CT_TEXT) {
            cell_sf = cursor->get_text();
        } else if (cursor->get_type() == lnav::cell_type::CT_NULL) {
            sa.emplace_back(stlr, VC_ROLE.value(role_t::VCR_NULL));
        }
        if (lpc == this->dls_row_style_column) {
            sa.emplace_back(stlr, VC_ROLE.value(role_t::VCR_ERROR));
        } else if (cell_sf.is_valid() && cell_sf.length() > 2
                   && cell_sf.length() < MAX_JSON_WIDTH
                   && ((cell_sf.front() == '{' && cell_sf.back() == '}')
                       || (cell_sf.front() == '[' && cell_sf.back() == ']')))
        {
            json_ptr_walk jpw;

            if (jpw.parse(cell_sf.udata(), cell_sf.length()) == yajl_status_ok
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
        this->dls_row_style_column = this->dls_headers.size() - 1;
    }
    if (colstr == "log_level") {
        this->dls_level_column = this->dls_headers.size() - 1;
    }
    hm.hm_chart.with_show_state(stacked_bar_chart_base::show_all{});
}

void
db_label_source::update_time_column(const string_fragment& sf)
{
    date_time_scanner dts;
    timeval tv;

    if (!dts.convert_to_timeval(sf.data(), sf.length(), nullptr, tv)) {
        tv.tv_sec = -1;
        tv.tv_usec = -1;
    }
    if (!this->dls_time_column.empty() && tv < this->dls_time_column.back()) {
        this->dls_time_column_invalidated_at = this->dls_time_column.size();
        this->dls_time_column_index = -1;
        this->dls_time_column.clear();
    } else {
        this->dls_time_column.push_back(tv);
    }
}

void
db_label_source::push_column(const column_value_t& sv)
{
    auto row_index = this->dls_row_cursors.size() - 1;
    auto& vc = view_colors::singleton();
    auto col = this->dls_push_column++;
    auto& hm = this->dls_headers[col];
    size_t width = 1;
    auto cv_sf = string_fragment::invalid();

    sv.match(
        [this, &col, &width, &cv_sf, &hm, &row_index](
            const string_fragment& sf) {
            if (this->dls_row_style_column == col) {
                return;
            }
            if (col == this->dls_time_column_index) {
                this->update_time_column(sf);
            } else if (this->dls_level_column
                       && this->dls_level_column.value() == col
                       && this->tss_view != nullptr)
            {
                auto& bm = this->tss_view->get_bookmarks();
                auto lev = string2level(sf.data(), sf.length());
                switch (lev) {
                    case log_level_t::LEVEL_FATAL:
                    case log_level_t::LEVEL_CRITICAL:
                    case log_level_t::LEVEL_ERROR:
                        bm[&textview_curses::BM_ERRORS].insert_once(
                            vis_line_t(row_index));
                        break;
                    case log_level_t::LEVEL_WARNING:
                        bm[&textview_curses::BM_WARNINGS].insert_once(
                            vis_line_t(row_index));
                        break;
                    default:
                        break;
                }
            }
            width = utf8_string_length(sf.data(), sf.length())
                        .unwrapOr(sf.length());
            if (hm.is_graphable()
                && sf.length() < lnav::cell_container::SHORT_TEXT_LENGTH)
            {
                auto from_res = humanize::try_from<double>(sf);
                if (from_res.has_value()) {
                    this->dls_cell_container.push_float_with_units_cell(
                        from_res.value(), sf);
                } else {
                    this->dls_cell_container.push_text_cell(sf);
                }
            } else {
                this->dls_cell_container.push_text_cell(sf);
            }
            cv_sf = sf;
        },
        [this, &width](int64_t i) {
            width = count_digits(i);
            this->dls_cell_container.push_int_cell(i);
        },
        [this, &width](double d) {
            char buffer[1];
            auto fmt_res = fmt::format_to_n(buffer, 0, FMT_STRING("{}"), d);
            width = fmt_res.size;
            this->dls_cell_container.push_float_cell(d);
        },
        [this, &width](null_value_t) {
            width = 6;
            this->dls_cell_container.push_null_cell();
        });

    if (col == this->dls_row_style_column) {
        auto col_sf = string_fragment::invalid();
        if (sv.is<null_value_t>()) {
            this->dls_row_styles.emplace_back(row_style{});
        } else if (sv.is<string_fragment>()) {
            static const intern_string_t SRC
                = intern_string::lookup("__lnav_style__");
            auto frag = sv.get<string_fragment>();
            if (frag.empty()) {
                this->dls_row_styles.emplace_back(row_style{});
            } else {
                auto parse_res
                    = get_row_style_handlers().parser_for(SRC).of(frag);
                if (parse_res.isErr()) {
                    log_error("DB row %d JSON is invalid:", row_index);
                    auto errors = parse_res.unwrapErr();
                    for (const auto& err : errors) {
                        log_error("  %s", err.to_attr_line().al_string.c_str());
                    }
                    col_sf = string_fragment::from_str(
                                 errors[0].to_attr_line().al_string)
                                 .to_owned(this->dls_cell_allocator);
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
                                         .to_owned(this->dls_cell_allocator);
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
                                          .to_owned(this->dls_cell_allocator);
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
                                col_sf
                                    = string_fragment::from_str(
                                          fmt::format(
                                              FMT_STRING(
                                                  "invalid "
                                                  "background-color: {}"),
                                              fg_res.unwrapErr()))
                                          .to_owned(this->dls_cell_allocator);
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
                      .to_owned(this->dls_cell_allocator);
            this->dls_row_styles_have_errors = true;
        }

        if (col_sf.empty()) {
            this->dls_cell_container.push_null_cell();
        } else {
            this->dls_cell_container.push_text_cell(col_sf);
            width = utf8_string_length(col_sf.data(), col_sf.length())
                        .unwrapOr(col_sf.length());
            this->dls_cell_allocator.reset();
        }
    }

    hm.hm_column_size = std::max(this->dls_headers[col].hm_column_size, width);
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
    } else if (cv_sf.is_valid() && cv_sf.length() > 2
               && ((cv_sf.startswith("{") && cv_sf.endswith("}"))
                   || (cv_sf.startswith("[") && cv_sf.endswith("]"))))
    {
        json_ptr_walk jpw;

        if (jpw.parse(cv_sf.data(), cv_sf.length()) == yajl_status_ok
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
    this->dls_query_start = std::nullopt;
    this->dls_query_end = std::nullopt;
    this->dls_headers.clear();
    this->dls_row_cursors.clear();
    this->dls_cell_container.reset();
    this->dls_time_column.clear();
    this->dls_time_column_index = -1;
    this->dls_cell_width.clear();
    this->dls_row_styles.clear();
    this->dls_row_styles_have_errors = false;
    this->dls_row_style_column = -1;
    this->dls_level_column = std::nullopt;
    this->dls_cell_allocator.reset();
    if (this->tss_view != nullptr) {
        this->tss_view->get_bookmarks().clear();
    }
}

std::optional<size_t>
db_label_source::column_name_to_index(const std::string& name) const
{
    return this->dls_headers | lnav::itertools::find(name);
}

std::optional<vis_line_t>
db_label_source::row_for_time(timeval time_bucket)
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

bool
db_label_source::text_handle_mouse(
    textview_curses& tc,
    const listview_curses::display_line_content_t&,
    mouse_event& me)
{
    if (tc.get_overlay_selection()) {
        auto nci = ncinput{};
        if (me.is_click_in(mouse_button_t::BUTTON_LEFT, 0, 3)) {
            nci.id = ' ';
            nci.eff_text[0] = ' ';
            this->list_input_handle_key(tc, nci);
        }
    }
    return true;
}

static constexpr string_attr_type<std::string> DBA_DETAILS("details");
static constexpr string_attr_type<std::string> DBA_COLUMN_NAME("column-name");

bool
db_label_source::list_input_handle_key(listview_curses& lv, const ncinput& ch)
{
    switch (ch.eff_text[0]) {
        case ' ': {
            auto ov_sel = lv.get_overlay_selection();
            if (ov_sel) {
                std::vector<attr_line_t> rows;
                auto* ov_source = lv.get_overlay_source();
                ov_source->list_value_for_overlay(lv, lv.get_selection(), rows);
                if (ov_sel.value() < rows.size()) {
                    auto& row_al = rows[ov_sel.value()];
                    auto col_attr
                        = get_string_attr(row_al.al_attrs, DBA_COLUMN_NAME);
                    if (col_attr) {
                        auto col_name = col_attr.value().get();
                        auto col_opt = this->column_name_to_index(col_name);
                        if (col_opt) {
                            this->dls_headers[col_opt.value()].hm_hidden
                                = !this->dls_headers[col_opt.value()].hm_hidden;
                        }
                    }
                }
                lv.set_needs_update();

                return true;
            }
            break;
        }
    }

    return false;
}

std::optional<json_string>
db_label_source::text_row_details(const textview_curses& tc)
{
    if (this->dls_row_cursors.empty()) {
        log_trace("db_label_source::text_row_details: empty");
        return std::nullopt;
    }
    if (!this->dls_query_end.has_value()) {
        log_trace("db_label_source::text_row_details: query in progress");
        return std::nullopt;
    }

    auto ov_sel = tc.get_overlay_selection();

    if (ov_sel.has_value()) {
        std::vector<attr_line_t> rows;
        auto* ov_source = tc.get_overlay_source();
        ov_source->list_value_for_overlay(tc, tc.get_selection(), rows);
        if (ov_sel.value() < rows.size()) {
            auto& row_al = rows[ov_sel.value()];
            auto deets_attr = get_string_attr(row_al.al_attrs, DBA_DETAILS);
            if (deets_attr) {
                auto deets = deets_attr->get();
                if (!deets.empty()) {
                    return json_string(
                        auto_buffer::from(deets.c_str(), deets.length()));
                }
            }
        }
    } else {
        yajlpp_gen gen;

        {
            yajlpp_map root(gen);

            root.gen("value");

            {
                yajlpp_map value_map(gen);

                auto cursor = this->dls_row_cursors[tc.get_selection()].sync();
                for (const auto& [col, hm] :
                     lnav::itertools::enumerate(this->dls_headers))
                {
                    value_map.gen(hm.hm_name);

                    switch (cursor->get_type()) {
                        case lnav::cell_type::CT_NULL:
                            value_map.gen();
                            break;
                        case lnav::cell_type::CT_INTEGER:
                            value_map.gen(cursor->get_int());
                            break;
                        case lnav::cell_type::CT_FLOAT:
                            if (cursor->get_sub_value() == 0) {
                                value_map.gen(cursor->get_float());
                            } else {
                                value_map.gen(cursor->get_float_as_text());
                            }
                            break;
                        case lnav::cell_type::CT_TEXT:
                            value_map.gen(cursor->get_text());
                            break;
                    }
                    cursor = cursor->next();
                }
            }
        }

        return json_string{gen};
    }

    return std::nullopt;
}

std::string
db_label_source::get_row_as_string(vis_line_t row)
{
    if (row < 0_vl || (((size_t) row) >= this->dls_row_cursors.size())) {
        return "";
    }

    if (this->dls_headers.size() == 1) {
        return this->dls_row_cursors[row]
            .sync()
            .value()
            .to_string_fragment(this->dls_cell_allocator)
            .to_string();
    }

    std::string retval;
    size_t lpc = 0;
    auto cursor = this->dls_row_cursors[row].sync();
    while (lpc < this->dls_headers.size() && cursor.has_value()) {
        const auto& hm = this->dls_headers[lpc];

        if (!retval.empty()) {
            retval.append("; ");
        }
        retval.append(hm.hm_name);
        retval.push_back('=');
        auto sf = cursor->to_string_fragment(this->dls_cell_allocator);
        retval.append(sf.data(), sf.length());

        cursor = cursor->next();
        lpc += 1;
    }
    this->dls_cell_allocator.reset();

    return retval;
}

std::string
db_label_source::get_cell_as_string(vis_line_t row, size_t col)
{
    if (row < 0_vl || (((size_t) row) >= this->dls_row_cursors.size())
        || col >= this->dls_headers.size())
    {
        return "";
    }

    this->dls_cell_allocator.reset();
    size_t lpc = 0;
    auto cursor = this->dls_row_cursors[row].sync();
    while (cursor.has_value()) {
        if (lpc == col) {
            return cursor->to_string_fragment(this->dls_cell_allocator)
                .to_string();
        }

        cursor = cursor->next();
        lpc += 1;
    }

    return "";
}

std::optional<int64_t>
db_label_source::get_cell_as_int64(vis_line_t row, size_t col)
{
    if (row < 0_vl || (((size_t) row) >= this->dls_row_cursors.size())
        || col >= this->dls_headers.size())
    {
        return std::nullopt;
    }

    size_t lpc = 0;
    auto cursor = this->dls_row_cursors[row].sync();
    while (cursor.has_value()) {
        if (lpc == col) {
            if (cursor->get_type() == lnav::cell_type::CT_INTEGER) {
                return cursor->get_int();
            }
            return std::nullopt;
        }

        cursor = cursor->next();
        lpc += 1;
    }

    return std::nullopt;
}

std::optional<double>
db_label_source::get_cell_as_double(vis_line_t row, size_t col)
{
    if (row < 0_vl || (((size_t) row) >= this->dls_row_cursors.size())
        || col >= this->dls_headers.size())
    {
        return std::nullopt;
    }

    size_t lpc = 0;
    auto cursor = this->dls_row_cursors[row].sync();
    while (cursor.has_value()) {
        if (lpc == col) {
            switch (cursor->get_type()) {
                case lnav::cell_type::CT_INTEGER:
                    return cursor->get_int();
                case lnav::cell_type::CT_FLOAT:
                    return cursor->get_float();
                default:
                    return std::nullopt;
            }
        }

        cursor = cursor->next();
        lpc += 1;
    }

    return std::nullopt;
}

void
db_label_source::reset_user_state()
{
    for (auto& hm : this->dls_headers) {
        hm.hm_hidden = false;
    }
}

std::optional<attr_line_t>
db_overlay_source::list_header_for_overlay(const listview_curses& lv,
                                           vis_line_t line)
{
    attr_line_t retval;

    retval.append("  Details for row ")
        .append(
            lnav::roles::number(fmt::format(FMT_STRING("{:L}"), (int) line)))
        .append(". Press ")
        .append("p"_hotkey)
        .append(" to hide this panel.");
    if (lv.get_overlay_selection()) {
        retval.append(" Controls: ")
            .append("c"_hotkey)
            .append(" to copy a column value; ")
            .append("SPC"_hotkey)
            .append(" to hide/show a column");
    } else {
        retval.append("  Press ")
            .append("CTRL-]"_hotkey)
            .append(" to focus on this panel");
    }
    return retval;
}

void
db_overlay_source::list_value_for_overlay(const listview_curses& lv,
                                          vis_line_t row,
                                          std::vector<attr_line_t>& value_out)
{
    if (!this->dos_active || lv.get_inner_height() == 0) {
        return;
    }

    if (row != lv.get_selection()) {
        return;
    }

    auto& vc = view_colors::singleton();
    unsigned long width;
    vis_line_t height;

    lv.get_dimensions(height, width);

    auto max_name_width = this->dos_labels->dls_headers
        | lnav::itertools::map([](const auto& hm) { return hm.hm_name.size(); })
        | lnav::itertools::max();

    auto cursor = this->dos_labels->dls_row_cursors[row].sync();
    for (const auto& [col, hm] :
         lnav::itertools::enumerate(this->dos_labels->dls_headers))
    {
        auto al = attr_line_t()
                      .append(lnav::roles::h3(hm.hm_name))
                      .right_justify(max_name_width.value_or(0) + 2);

        if (hm.hm_hidden) {
            al.insert(1, "\u25c7"_comment);
        } else {
            al.insert(1, "\u25c6"_ok);
        }

        auto sf
            = cursor->to_string_fragment(this->dos_labels->dls_cell_allocator);

        al.al_attrs.emplace_back(line_range{0, -1},
                                 DBA_COLUMN_NAME.value(hm.hm_name));
        if (cursor->get_type() == lnav::cell_type::CT_TEXT
            && (sf.startswith("[") || sf.startswith("{")))
        {
            json_ptr_walk jpw;

            if (jpw.parse(sf.udata(), sf.length()) == yajl_status_ok
                && jpw.complete_parse() == yajl_status_ok)
            {
                {
                    yajlpp_gen gen;

                    {
                        yajlpp_map root(gen);

                        root.gen("key");
                        root.gen(hm.hm_name);
                        root.gen("value");
                        root.gen(sf);
                    }
                    al.al_attrs.emplace_back(
                        line_range{0, -1},
                        DBA_DETAILS.value(
                            gen.to_string_fragment().to_string()));
                }
                value_out.emplace_back(al);
                al.clear();

                stacked_bar_chart<std::string> chart;
                int start_line = value_out.size();

                auto indent = 3 + max_name_width.value() - hm.hm_name.size();
                chart.with_stacking_enabled(false)
                    .with_margins(indent + 2, 0)
                    .with_show_state(stacked_bar_chart_base::show_all{});

                for (const auto& [walk_index, jpw_value] :
                     lnav::itertools::enumerate(jpw.jpw_values))
                {
                    {
                        yajlpp_gen gen;

                        {
                            yajlpp_map root(gen);

                            root.gen("key");
                            root.gen(jpw_value.wt_ptr);
                            root.gen("value");
                            root.gen(jpw_value.wt_value);
                        }
                        al.al_attrs.emplace_back(
                            line_range{0, -1},
                            DBA_DETAILS.value(
                                gen.to_string_fragment().to_string()));
                    }

                    al.append(indent + 2, ' ')
                        .append(lnav::roles::h5(jpw_value.wt_ptr))
                        .append(" = ")
                        .append(jpw_value.wt_value);

                    auto& sa = al.al_attrs;
                    line_range lr(indent, indent + 1);

                    sa.emplace_back(
                        lr,
                        VC_GRAPHIC.value(walk_index < jpw.jpw_values.size() - 1
                                             ? NCACS_LTEE
                                             : NCACS_LLCORNER));
                    lr.lr_start = indent + 2 + jpw_value.wt_ptr.size() + 3;
                    lr.lr_end = -1;

                    if (jpw_value.wt_type == yajl_t_number) {
                        auto num_scan_res
                            = scn::scan_value<double>(jpw_value.wt_value);

                        if (num_scan_res) {
                            auto attrs = vc.attrs_for_ident(jpw_value.wt_ptr);

                            chart.add_value(jpw_value.wt_ptr,
                                            num_scan_res->value());
                            chart.with_attrs_for_ident(jpw_value.wt_ptr, attrs);
                        }
                        sa.emplace_back(lr, VC_ROLE.value(role_t::VCR_NUMBER));
                    }
                    value_out.emplace_back(al);
                    al.clear();
                }

                int curr_line = start_line;
                for (auto iter = jpw.jpw_values.begin();
                     iter != jpw.jpw_values.end();
                     ++iter, curr_line++)
                {
                    if (iter->wt_type != yajl_t_number) {
                        continue;
                    }

                    auto num_scan_res
                        = humanize::try_from<double>(iter->wt_value);
                    if (num_scan_res) {
                        auto& sa = value_out[curr_line].get_attrs();
                        int left = indent + 2;

                        chart.chart_attrs_for_value(lv,
                                                    left,
                                                    width,
                                                    iter->wt_ptr,
                                                    num_scan_res.value(),
                                                    sa);
                    }
                }
            } else {
                yajlpp_gen gen;

                {
                    yajlpp_map root(gen);

                    root.gen("key");
                    root.gen(hm.hm_name);
                    root.gen("value");
                    root.gen(sf);
                }
                al.append(": ").append(sf);
                al.al_attrs.emplace_back(
                    line_range{0, -1},
                    DBA_DETAILS.value(gen.to_string_fragment().to_string()));
            }
        } else {
            yajlpp_gen gen;

            {
                yajlpp_map root(gen);

                root.gen("key");
                root.gen(hm.hm_name);
                root.gen("value");
                switch (cursor->get_type()) {
                    case lnav::cell_type::CT_NULL:
                        root.gen();
                        break;
                    case lnav::cell_type::CT_INTEGER:
                        root.gen(cursor->get_int());
                        break;
                    case lnav::cell_type::CT_FLOAT:
                        if (cursor->get_sub_value() == 0) {
                            root.gen(cursor->get_float());
                        } else {
                            root.gen(cursor->get_float_as_text());
                        }
                        break;
                    case lnav::cell_type::CT_TEXT:
                        root.gen(cursor->get_text());
                        break;
                }
            }

            auto value_al = attr_line_t::from_table_cell_content(sf, 1000);
            al.append(": ").append(value_al);
            al.al_attrs.emplace_back(
                line_range{0, -1},
                DBA_DETAILS.value(gen.to_string_fragment().to_string()));
        }

        if (!al.empty()) {
            value_out.emplace_back(al);
        }
        cursor = cursor->next();
    }

    this->dos_labels->dls_cell_allocator.reset();
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
        if (lpc == this->dos_labels->dls_row_style_column
            && !this->dos_labels->dls_row_styles_have_errors)
        {
            continue;
        }

        const auto& hm = dls->dls_headers[lpc];
        if (hm.hm_hidden) {
            continue;
        }
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
