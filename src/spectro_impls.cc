/**
 * Copyright (c) 2022, Timothy Stack
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

#include "spectro_impls.hh"

#include "base/itertools.hh"
#include "lnav.hh"
#include "logfile_sub_source.hh"
#include "scn/scn.h"

using namespace lnav::roles::literals;

class filtered_sub_source
    : public text_sub_source
    , public text_time_translator
    , public list_overlay_source {
public:
    size_t text_line_count() override { return this->fss_lines.size(); }

    void text_value_for_line(textview_curses& tc,
                             int line,
                             std::string& value_out,
                             line_flags_t flags) override
    {
        this->fss_lines | lnav::itertools::nth(line)
            | lnav::itertools::for_each([&](const auto row) {
                  this->fss_delegate->text_value_for_line(
                      tc, *row, value_out, flags);
              });
    }

    size_t text_size_for_line(textview_curses& tc,
                              int line,
                              line_flags_t raw) override
    {
        return this->fss_lines | lnav::itertools::nth(line)
            | lnav::itertools::map([&](const auto row) {
                   return this->fss_delegate->text_size_for_line(tc, *row, raw);
               })
            | lnav::itertools::unwrap_or(size_t{0});
    }

    void text_attrs_for_line(textview_curses& tc,
                             int line,
                             string_attrs_t& value_out) override
    {
        this->fss_lines | lnav::itertools::nth(line)
            | lnav::itertools::for_each([&](const auto row) {
                  this->fss_delegate->text_attrs_for_line(tc, *row, value_out);
              });
    }

    std::optional<vis_line_t> row_for_time(struct timeval time_bucket) override
    {
        return this->fss_time_delegate->row_for_time(time_bucket);
    }

    std::optional<row_info> time_for_row(vis_line_t row) override
    {
        return this->fss_lines | lnav::itertools::nth(row)
            | lnav::itertools::flat_map([this](const auto row) {
                   return this->fss_time_delegate->time_for_row(*row);
               });
    }

    void list_value_for_overlay(const listview_curses& lv,
                                vis_line_t line,
                                std::vector<attr_line_t>& value_out) override
    {
        if (this->fss_overlay_delegate != nullptr) {
            this->fss_overlay_delegate->list_value_for_overlay(
                lv, line, value_out);
        }
    }

    text_sub_source* fss_delegate;
    text_time_translator* fss_time_delegate;
    list_overlay_source* fss_overlay_delegate{nullptr};
    std::vector<vis_line_t> fss_lines;
};

log_spectro_value_source::log_spectro_value_source(intern_string_t colname)
    : lsvs_colname(colname)
{
    this->update_stats();
}

void
log_spectro_value_source::update_stats()
{
    auto& lss = lnav_data.ld_log_source;

    this->lsvs_begin_time = 0;
    this->lsvs_end_time = 0;
    this->lsvs_stats.clear();
    for (auto& ls : lss) {
        auto* lf = ls->get_file_ptr();

        if (lf == nullptr) {
            continue;
        }

        auto format = lf->get_format();
        const auto* stats = format->stats_for_value(this->lsvs_colname);

        if (stats == nullptr) {
            continue;
        }

        auto ll = lf->begin();

        if (this->lsvs_begin_time == 0
            || ll->get_time() < this->lsvs_begin_time)
        {
            this->lsvs_begin_time = ll->get_time();
        }
        ll = lf->end();
        --ll;
        if (ll->get_time() > this->lsvs_end_time) {
            this->lsvs_end_time = ll->get_time();
        }

        this->lsvs_found = true;
        this->lsvs_stats.merge(*stats);
    }

    if (this->lsvs_begin_time) {
        time_t filtered_begin_time = lss.find_line(lss.at(0_vl))->get_time();
        time_t filtered_end_time
            = lss.find_line(lss.at(vis_line_t(lss.text_line_count() - 1)))
                  ->get_time();

        if (filtered_begin_time > this->lsvs_begin_time) {
            this->lsvs_begin_time = filtered_begin_time;
        }
        if (filtered_end_time < this->lsvs_end_time) {
            this->lsvs_end_time = filtered_end_time;
        }
    }
}

void
log_spectro_value_source::spectro_bounds(spectrogram_bounds& sb_out)
{
    auto& lss = lnav_data.ld_log_source;

    if (lss.text_line_count() == 0) {
        return;
    }

    this->update_stats();

    sb_out.sb_begin_time = this->lsvs_begin_time;
    sb_out.sb_end_time = this->lsvs_end_time;
    sb_out.sb_min_value_out = this->lsvs_stats.lvs_min_value;
    sb_out.sb_max_value_out = this->lsvs_stats.lvs_max_value;
    sb_out.sb_count = this->lsvs_stats.lvs_count;
}

void
log_spectro_value_source::spectro_row(spectrogram_request& sr,
                                      spectrogram_row& row_out)
{
    auto& lss = lnav_data.ld_log_source;
    auto begin_line = lss.find_from_time(sr.sr_begin_time).value_or(0_vl);
    auto end_line = lss.find_from_time(sr.sr_end_time)
                        .value_or(vis_line_t(lss.text_line_count()));

    for (const auto& msg_info : lss.window_at(begin_line, end_line)) {
        const auto& ll = msg_info.get_logline();
        if (ll.get_time() >= sr.sr_end_time) {
            break;
        }

        const auto& values = msg_info.get_values();
        auto lv_iter = find_if(values.lvv_values.begin(),
                               values.lvv_values.end(),
                               logline_value_cmp(&this->lsvs_colname));

        if (lv_iter != values.lvv_values.end()) {
            switch (lv_iter->lv_meta.lvm_kind) {
                case value_kind_t::VALUE_FLOAT:
                    row_out.add_value(sr, lv_iter->lv_value.d, ll.is_marked());
                    break;
                case value_kind_t::VALUE_INTEGER: {
                    row_out.add_value(sr, lv_iter->lv_value.i, ll.is_marked());
                    break;
                }
                default:
                    break;
            }
        }
    }

    row_out.sr_details_source_provider = [this](const spectrogram_request& sr,
                                                double range_min,
                                                double range_max) {
        auto& lss = lnav_data.ld_log_source;
        auto retval = std::make_unique<filtered_sub_source>();
        auto begin_line = lss.find_from_time(sr.sr_begin_time).value_or(0_vl);
        auto end_line = lss.find_from_time(sr.sr_end_time)
                            .value_or(vis_line_t(lss.text_line_count()));

        retval->fss_delegate = &lss;
        retval->fss_time_delegate = &lss;
        retval->fss_overlay_delegate = nullptr;
        for (const auto& msg_info : lss.window_at(begin_line, end_line)) {
            const auto& ll = msg_info.get_logline();
            if (ll.get_time() >= sr.sr_end_time) {
                break;
            }

            const auto& values = msg_info.get_values();
            auto lv_iter = find_if(values.lvv_values.begin(),
                                   values.lvv_values.end(),
                                   logline_value_cmp(&this->lsvs_colname));

            if (lv_iter != values.lvv_values.end()) {
                switch (lv_iter->lv_meta.lvm_kind) {
                    case value_kind_t::VALUE_FLOAT:
                        if (range_min <= lv_iter->lv_value.d
                            && lv_iter->lv_value.d < range_max)
                        {
                            retval->fss_lines.emplace_back(
                                msg_info.get_vis_line());
                        }
                        break;
                    case value_kind_t::VALUE_INTEGER:
                        if (range_min <= lv_iter->lv_value.i
                            && lv_iter->lv_value.i < range_max)
                        {
                            retval->fss_lines.emplace_back(
                                msg_info.get_vis_line());
                        }
                        break;
                    default:
                        break;
                }
            }
        }

        return retval;
    };
}

void
log_spectro_value_source::spectro_mark(textview_curses& tc,
                                       time_t begin_time,
                                       time_t end_time,
                                       double range_min,
                                       double range_max)
{
    // XXX need to refactor this and the above method
    auto& log_tc = lnav_data.ld_views[LNV_LOG];
    auto& lss = lnav_data.ld_log_source;
    vis_line_t begin_line = lss.find_from_time(begin_time).value_or(0_vl);
    vis_line_t end_line = lss.find_from_time(end_time).value_or(
        vis_line_t(lss.text_line_count()));
    logline_value_vector values;
    string_attrs_t sa;

    for (vis_line_t curr_line = begin_line; curr_line < end_line; ++curr_line) {
        content_line_t cl = lss.at(curr_line);
        std::shared_ptr<logfile> lf = lss.find(cl);
        auto ll = lf->begin() + cl;
        auto format = lf->get_format();

        if (!ll->is_message()) {
            continue;
        }

        values.clear();
        lf->read_full_message(ll, values.lvv_sbr);
        values.lvv_sbr.erase_ansi();
        sa.clear();
        format->annotate(lf.get(), cl, sa, values, false);

        auto lv_iter = find_if(values.lvv_values.begin(),
                               values.lvv_values.end(),
                               logline_value_cmp(&this->lsvs_colname));

        if (lv_iter != values.lvv_values.end()) {
            switch (lv_iter->lv_meta.lvm_kind) {
                case value_kind_t::VALUE_FLOAT:
                    if (range_min <= lv_iter->lv_value.d
                        && lv_iter->lv_value.d <= range_max)
                    {
                        log_tc.toggle_user_mark(&textview_curses::BM_USER,
                                                curr_line);
                    }
                    break;
                case value_kind_t::VALUE_INTEGER:
                    if (range_min <= lv_iter->lv_value.i
                        && lv_iter->lv_value.i <= range_max)
                    {
                        log_tc.toggle_user_mark(&textview_curses::BM_USER,
                                                curr_line);
                    }
                    break;
                default:
                    break;
            }
        }
    }
}

db_spectro_value_source::db_spectro_value_source(std::string colname)
    : dsvs_colname(std::move(colname))
{
    this->update_stats();
}

void
db_spectro_value_source::update_stats()
{
    this->dsvs_begin_time = 0;
    this->dsvs_end_time = 0;
    this->dsvs_stats.clear();

    auto& dls = lnav_data.ld_db_row_source;

    this->dsvs_column_index = dls.column_name_to_index(this->dsvs_colname);

    if (!dls.has_log_time_column()) {
        if (dls.dls_time_column_invalidated_at) {
            static const auto order_by_help = attr_line_t()
                                                  .append("ORDER BY"_keyword)
                                                  .append(" ")
                                                  .append("log_time"_variable)
                                                  .append(" ")
                                                  .append("ASC"_keyword);

            this->dsvs_error_msg
                = lnav::console::user_message::error(
                      "Cannot generate spectrogram for database results")
                      .with_reason(
                          attr_line_t()
                              .append("The ")
                              .append_quoted("log_time"_variable)
                              .appendf(
                                  FMT_STRING(" column is not in ascending "
                                             "order between rows {} and {}"),
                                  dls.dls_time_column_invalidated_at.value()
                                      - 1,
                                  dls.dls_time_column_invalidated_at.value()))
                      .with_note(
                          attr_line_t("An ascending ")
                              .append_quoted("log_time"_variable)
                              .append(
                                  " column is needed to render a spectrogram"))
                      .with_help(attr_line_t("Add an ")
                                     .append_quoted(order_by_help)
                                     .append(" clause to your ")
                                     .append("SELECT"_keyword)
                                     .append(" statement"));
        } else {
            this->dsvs_error_msg
                = lnav::console::user_message::error(
                      "Cannot generate spectrogram for database results")
                      .with_reason(
                          attr_line_t()
                              .append("No ")
                              .append_quoted("log_time"_variable)
                              .append(" column found in the result set"))
                      .with_note(
                          attr_line_t("An ascending ")
                              .append_quoted("log_time"_variable)
                              .append(
                                  " column is needed to render a spectrogram"))
                      .with_help(
                          attr_line_t("Include a ")
                              .append_quoted("log_time"_variable)
                              .append(" column in your ")
                              .append(" statement. Use an ")
                              .append("AS"_keyword)
                              .append(
                                  " directive to alias a computed timestamp"));
        }
        return;
    }

    if (!this->dsvs_column_index) {
        this->dsvs_error_msg
            = lnav::console::user_message::error(
                  "Cannot generate spectrogram for database results")
                  .with_reason(attr_line_t("unknown column -- ")
                                   .append_quoted(lnav::roles::variable(
                                       this->dsvs_colname)))
                  .with_help("Expecting a numeric column to visualize");
        return;
    }

    if (!dls.dls_headers[this->dsvs_column_index.value()].hm_graphable) {
        this->dsvs_error_msg
            = lnav::console::user_message::error(
                  "Cannot generate spectrogram for database results")
                  .with_reason(attr_line_t()
                                   .append_quoted(lnav::roles::variable(
                                       this->dsvs_colname))
                                   .append(" is not a numeric column"))
                  .with_help("Only numeric columns can be visualized");
        return;
    }

    if (dls.dls_rows.empty()) {
        this->dsvs_error_msg
            = lnav::console::user_message::error(
                  "Cannot generate spectrogram for database results")
                  .with_reason("Result set is empty");
        return;
    }

    this->dsvs_begin_time = dls.dls_time_column.front().tv_sec;
    this->dsvs_end_time = dls.dls_time_column.back().tv_sec;

    auto find_res
        = dls.dls_headers | lnav::itertools::find_if([this](const auto& elem) {
              return elem.hm_name == this->dsvs_colname;
          });
    if (find_res) {
        auto hm = find_res.value();
        auto& bs = hm->hm_chart.get_stats_for(this->dsvs_colname);
        this->dsvs_stats.lvs_min_value = bs.bs_min_value;
        this->dsvs_stats.lvs_max_value = bs.bs_max_value;
    }

    this->dsvs_stats.lvs_count = dls.dls_rows.size();
}

void
db_spectro_value_source::spectro_bounds(spectrogram_bounds& sb_out)
{
    auto& dls = lnav_data.ld_db_row_source;

    if (dls.text_line_count() == 0) {
        return;
    }

    this->update_stats();

    sb_out.sb_begin_time = this->dsvs_begin_time;
    sb_out.sb_end_time = this->dsvs_end_time;
    sb_out.sb_min_value_out = this->dsvs_stats.lvs_min_value;
    sb_out.sb_max_value_out = this->dsvs_stats.lvs_max_value;
    sb_out.sb_count = this->dsvs_stats.lvs_count;
}

void
db_spectro_value_source::spectro_row(spectrogram_request& sr,
                                     spectrogram_row& row_out)
{
    auto& dls = lnav_data.ld_db_row_source;
    auto begin_row = dls.row_for_time({sr.sr_begin_time, 0}).value_or(0_vl);
    auto end_row = dls.row_for_time({sr.sr_end_time, 0})
                       .value_or(vis_line_t(dls.dls_rows.size()));

    for (auto lpc = begin_row; lpc < end_row; ++lpc) {
        auto scan_res = scn::scan_value<double>(scn::string_view{
            dls.dls_rows[lpc][this->dsvs_column_index.value()]});

        if (scan_res) {
            row_out.add_value(sr, scan_res.value(), false);
        }
    }

    row_out.sr_details_source_provider = [this](const spectrogram_request& sr,
                                                double range_min,
                                                double range_max) {
        auto& dls = lnav_data.ld_db_row_source;
        auto retval = std::make_unique<filtered_sub_source>();

        retval->fss_delegate = &dls;
        retval->fss_time_delegate = &dls;
        retval->fss_overlay_delegate = &lnav_data.ld_db_overlay;
        auto begin_row = dls.row_for_time({sr.sr_begin_time, 0}).value_or(0_vl);
        auto end_row = dls.row_for_time({sr.sr_end_time, 0})
                           .value_or(vis_line_t(dls.dls_rows.size()));

        for (auto lpc = begin_row; lpc < end_row; ++lpc) {
            auto scan_res = scn::scan_value<double>(scn::string_view{
                dls.dls_rows[lpc][this->dsvs_column_index.value()]});
            if (!scan_res) {
                continue;
            }
            auto value = scan_res.value();
            if ((range_min == value)
                || (range_min < value && value < range_max))
            {
                retval->fss_lines.emplace_back(lpc);
            }
        }

        return retval;
    };
}
