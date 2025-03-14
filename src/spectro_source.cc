/**
 * Copyright (c) 2020, Timothy Stack
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
 *
 * @file spectro_source.cc
 */

#include "spectro_source.hh"

#include "base/ansi_scrubber.hh"
#include "base/keycodes.hh"
#include "base/math_util.hh"
#include "config.h"

std::optional<size_t>
spectrogram_row::nearest_column(size_t current) const
{
    std::optional<size_t> retval;
    std::optional<size_t> nearest_distance;

    for (size_t lpc = 0; lpc < this->sr_width; lpc++) {
        if (this->sr_values[lpc].rb_counter == 0) {
            continue;
        }
        auto curr_distance = abs_diff(lpc, current);

        if (!retval || curr_distance < nearest_distance.value()) {
            retval = lpc;
            nearest_distance = abs_diff(lpc, current);
        }
    }

    return retval;
}

bool
spectrogram_source::list_input_handle_key(listview_curses& lv,
                                          const ncinput& ch)
{
    switch (ch.eff_text[0]) {
        case 'm': {
            auto sel = lv.get_selection();
            if (sel < 0 || (size_t) sel >= this->text_line_count()
                || !this->ss_cursor_column || this->ss_value_source == nullptr)
            {
                alerter::singleton().chime(
                    "a value must be selected before it can be marked");
                return true;
            }

            unsigned long width;
            vis_line_t height;

            lv.get_dimensions(height, width);
            width -= 2;

            auto& sb = this->ss_cached_bounds;
            auto begin_time_opt = this->time_for_row_int(sel);
            if (!begin_time_opt) {
                return true;
            }
            auto begin_time = begin_time_opt.value();
            auto end_time = to_us(begin_time.ri_time);

            end_time += this->ss_granularity;
            double range_min, range_max, column_size;

            column_size = (sb.sb_max_value_out - sb.sb_min_value_out)
                / (double) (width - 1);
            range_min = sb.sb_min_value_out
                + this->ss_cursor_column.value_or(0) * column_size;
            range_max = range_min + column_size;
            this->ss_value_source->spectro_mark((textview_curses&) lv,
                                                to_us(begin_time.ri_time),
                                                end_time,
                                                range_min,
                                                range_max);
            this->invalidate();
            lv.reload_data();
            return true;
        }

        case KEY_CTRL('a'): {
            if (this->ss_value_source != nullptr) {
                this->ss_cursor_column = 0;
                this->text_selection_changed((textview_curses&) lv);
                lv.set_needs_update();
            }
            return true;
        }

        case KEY_CTRL('e'): {
            if (this->ss_value_source != nullptr) {
                this->ss_cursor_column = INT_MAX;
                this->text_selection_changed((textview_curses&) lv);
                lv.set_needs_update();
            }
            return true;
        }

        case NCKEY_LEFT:
        case NCKEY_RIGHT: {
            auto sel = lv.get_selection();
            unsigned long width;
            vis_line_t height;
            string_attrs_t sa;

            lv.get_dimensions(height, width);

            this->text_attrs_for_line((textview_curses&) lv, sel, sa);

            if (sa.empty()) {
                this->ss_details_source.reset();
                this->ss_cursor_column = std::nullopt;
                return true;
            }

            if (!this->ss_cursor_column) {
                lv.set_selection(0_vl);
            }
            line_range lr(this->ss_cursor_column.value(),
                          this->ss_cursor_column.value() + 1);

            auto current = find_string_attr(sa, lr);

            if (current != sa.end()) {
                if (ch.id == NCKEY_LEFT) {
                    if (current == sa.begin()) {
                        current = sa.end();
                    } else {
                        --current;
                    }
                } else {
                    ++current;
                }
            }

            if (current == sa.end()) {
                if (ch.id == NCKEY_LEFT) {
                    current = sa.end();
                    --current;
                } else {
                    current = sa.begin();
                }
            }
            this->ss_cursor_column = current->sa_range.lr_start;
            this->ss_details_source.reset();

            lv.reload_data();

            return true;
        }
        default:
            return false;
    }
}

bool
spectrogram_source::text_handle_mouse(
    textview_curses& tc,
    const listview_curses::display_line_content_t&,
    mouse_event& me)
{
    auto sel = tc.get_selection();
    const auto& s_row = this->load_row(tc, sel);

    for (int lpc = 0; lpc <= (int) s_row.sr_width; lpc++) {
        int col_value = s_row.sr_values[lpc].rb_counter;

        if (col_value == 0) {
            continue;
        }

        auto lr = line_range{lpc, lpc + 1};
        if (me.is_click_in(mouse_button_t::BUTTON_LEFT, lr)) {
            this->ss_cursor_column = lr.lr_start;
            this->ss_details_source.reset();

            tc.reload_data();
            return true;
        }
    }

    return false;
}

void
spectrogram_source::list_value_for_overlay(const listview_curses& lv,
                                           vis_line_t row,
                                           std::vector<attr_line_t>& value_out)
{
    auto [height, width] = lv.get_dimensions();
    width -= 2;

    auto sel = lv.get_selection();

    if (row == sel && this->ss_cursor_column) {
        const auto& s_row = this->load_row(lv, sel);
        const auto& bucket = s_row.sr_values[this->ss_cursor_column.value()];
        auto& sb = this->ss_cached_bounds;
        spectrogram_request sr(sb);
        attr_line_t retval;

        auto sel_time = rounddown(sb.sb_begin_time, this->ss_granularity)
            + (sel * this->ss_granularity);
        sr.sr_width = width;
        sr.sr_begin_time = sel_time;
        sr.sr_end_time = sel_time + this->ss_granularity;
        sr.sr_column_size = (sb.sb_max_value_out - sb.sb_min_value_out)
            / (double) (width - 1);
        auto range_min = sb.sb_min_value_out
            + this->ss_cursor_column.value() * sr.sr_column_size;
        auto range_max = range_min + sr.sr_column_size;

        auto desc
            = attr_line_t()
                  .append(
                      lnav::roles::number(fmt::to_string(bucket.rb_counter)))
                  .append(fmt::format(FMT_STRING(" value{} in the range "),
                                      bucket.rb_counter == 1 ? "" : "s"))
                  .append(lnav::roles::number(
                      fmt::format(FMT_STRING("{:.2Lf}"), range_min)))
                  .append("-")
                  .append(lnav::roles::number(
                      fmt::format(FMT_STRING("{:.2Lf}"), range_max)))
                  .append(" ");
        auto mark_offset = this->ss_cursor_column.value();
        auto mark_is_before = true;

        retval.al_attrs.emplace_back(line_range{0, -1},
                                     VC_ROLE.value(role_t::VCR_STATUS_INFO));
        if (desc.length() + 8 > (ssize_t) width) {
            desc.clear();
        }

        if (this->ss_cursor_column.value() + desc.length() + 1 > width) {
            mark_offset -= desc.length();
            mark_is_before = false;
        }
        retval.append(mark_offset, ' ');
        if (mark_is_before) {
            retval.append("\u25b2 ");
        }
        retval.append(desc);
        if (!mark_is_before) {
            retval.append("\u25b2 ");
        }

        if (this->ss_details_view != nullptr) {
            if (s_row.sr_details_source_provider) {
                auto row_details_source = s_row.sr_details_source_provider(
                    sr, range_min, range_max);

                this->ss_details_view->set_sub_source(row_details_source.get());
                this->ss_details_source = std::move(row_details_source);
                auto* overlay_source = dynamic_cast<list_overlay_source*>(
                    this->ss_details_source.get());
                if (overlay_source != nullptr) {
                    this->ss_details_view->set_overlay_source(overlay_source);
                }
            } else {
                this->ss_details_view->set_sub_source(
                    this->ss_no_details_source);
                this->ss_details_view->set_overlay_source(nullptr);
            }
        }

        value_out.emplace_back(retval);
    }
}

size_t
spectrogram_source::text_line_count()
{
    if (this->ss_value_source == nullptr) {
        return 0;
    }

    this->cache_bounds();

    return this->ss_cached_line_count;
}

size_t
spectrogram_source::text_line_width(textview_curses& tc)
{
    if (tc.get_window() == nullptr) {
        return 80;
    }

    unsigned long width;
    vis_line_t height;

    tc.get_dimensions(height, width);
    return width;
}

std::optional<text_time_translator::row_info>
spectrogram_source::time_for_row(vis_line_t row)
{
    if (this->ss_details_source != nullptr) {
        auto* details_tss = dynamic_cast<text_time_translator*>(
            this->ss_details_source.get());

        if (details_tss != nullptr) {
            return details_tss->time_for_row(this->ss_details_view->get_top());
        }
    }

    return this->time_for_row_int(row);
}

std::optional<text_time_translator::row_info>
spectrogram_source::time_for_row_int(vis_line_t row)
{
    timeval retval{0, 0};

    this->cache_bounds();
    retval.tv_sec = to_time_t(
        rounddown(this->ss_cached_bounds.sb_begin_time, this->ss_granularity)
        + row * this->ss_granularity);

    return row_info{retval, row};
}

std::optional<vis_line_t>
spectrogram_source::row_for_time(timeval time_bucket)
{
    if (this->ss_value_source == nullptr) {
        return std::nullopt;
    }

    this->cache_bounds();
    const auto tb_us = to_us(time_bucket);
    const auto grain_begin_time
        = rounddown(this->ss_cached_bounds.sb_begin_time, this->ss_granularity);
    if (tb_us < grain_begin_time) {
        return 0_vl;
    }

    const auto diff = tb_us - grain_begin_time;
    const auto retval = diff / this->ss_granularity;

    return vis_line_t(retval);
}

line_info
spectrogram_source::text_value_for_line(textview_curses& tc,
                                        int row,
                                        std::string& value_out,
                                        text_sub_source::line_flags_t flags)
{
    const auto& s_row = this->load_row(tc, row);
    char tm_buffer[128];
    struct tm tm;

    auto row_time_opt = this->time_for_row_int(vis_line_t(row));
    if (!row_time_opt) {
        value_out.clear();
        return {};
    }
    auto ri = row_time_opt.value();

    gmtime_r(&ri.ri_time.tv_sec, &tm);
    strftime(tm_buffer, sizeof(tm_buffer), " %a %b %d %H:%M:%S", &tm);

    value_out = tm_buffer;
    value_out.resize(s_row.sr_width, ' ');

    for (size_t lpc = 0; lpc <= s_row.sr_width; lpc++) {
        if (s_row.sr_values[lpc].rb_marks) {
            value_out[lpc] = 'x';
        }
    }

    return {};
}

void
spectrogram_source::text_attrs_for_line(textview_curses& tc,
                                        int row,
                                        string_attrs_t& value_out)
{
    if (this->ss_value_source == nullptr) {
        return;
    }

    const auto& st = this->ss_cached_thresholds;
    const auto& s_row = this->load_row(tc, row);

    for (int lpc = 0; lpc <= (int) s_row.sr_width; lpc++) {
        int col_value = s_row.sr_values[lpc].rb_counter;

        if (col_value == 0) {
            continue;
        }

        role_t role;

        if (col_value < st.st_green_threshold) {
            role = role_t::VCR_LOW_THRESHOLD;
        } else if (col_value < st.st_yellow_threshold) {
            role = role_t::VCR_MED_THRESHOLD;
        } else {
            role = role_t::VCR_HIGH_THRESHOLD;
        }
        value_out.emplace_back(line_range(lpc, lpc + 1), VC_ROLE.value(role));
    }

    auto alt_row_index = row % 4;
    if (alt_row_index == 2 || alt_row_index == 3) {
        value_out.emplace_back(line_range{0, -1},
                               VC_ROLE.value(role_t::VCR_ALT_ROW));
    }
}

void
spectrogram_source::reset_details_source()
{
    if (this->ss_details_view != nullptr) {
        this->ss_details_view->set_sub_source(this->ss_no_details_source);
        this->ss_details_view->set_overlay_source(nullptr);
    }
    this->ss_details_source.reset();
}

void
spectrogram_source::cache_bounds()
{
    if (this->ss_value_source == nullptr) {
        this->ss_cached_bounds.sb_count = 0;
        this->ss_cached_bounds.sb_begin_time
            = std::chrono::microseconds::zero();
        this->ss_cursor_column = std::nullopt;
        this->reset_details_source();
        return;
    }

    spectrogram_bounds sb;

    this->ss_value_source->spectro_bounds(sb);

    if (sb.sb_count == this->ss_cached_bounds.sb_count) {
        return;
    }

    this->ss_cached_bounds = sb;

    if (sb.sb_count == 0) {
        this->ss_cached_line_count = 0;
        this->ss_cursor_column = std::nullopt;
        this->reset_details_source();
        return;
    }

    auto grain_begin_time = rounddown(sb.sb_begin_time, this->ss_granularity);
    auto grain_end_time = roundup_size(sb.sb_end_time, this->ss_granularity);

    auto diff = std::max(std::chrono::microseconds{1},
                         grain_end_time - grain_begin_time);
    this->ss_cached_line_count
        = (diff + this->ss_granularity - std::chrono::microseconds{1})
        / this->ss_granularity;

    int64_t samples_per_row = sb.sb_count / this->ss_cached_line_count;
    auto& st = this->ss_cached_thresholds;

    st.st_yellow_threshold = samples_per_row / 2;
    st.st_green_threshold = st.st_yellow_threshold / 2;

    if (st.st_green_threshold <= 1) {
        st.st_green_threshold = 2;
    }
    if (st.st_yellow_threshold <= st.st_green_threshold) {
        st.st_yellow_threshold = st.st_green_threshold + 1;
    }
}

const spectrogram_row&
spectrogram_source::load_row(const listview_curses& tc, int row)
{
    this->cache_bounds();

    unsigned long width;
    vis_line_t height;

    tc.get_dimensions(height, width);
    width -= 2;

    auto& sb = this->ss_cached_bounds;
    spectrogram_request sr(sb);

    sr.sr_width = width;
    auto row_time = rounddown(sb.sb_begin_time, this->ss_granularity)
        + row * this->ss_granularity;
    sr.sr_begin_time = row_time;
    sr.sr_end_time = row_time + this->ss_granularity;

    sr.sr_column_size
        = (sb.sb_max_value_out - sb.sb_min_value_out) / (double) (width - 1);

    auto& s_row = this->ss_row_cache[row_time];

    if (s_row.sr_values.empty() || s_row.sr_width != width
        || s_row.sr_column_size != sr.sr_column_size)
    {
        s_row.sr_width = width;
        s_row.sr_column_size = sr.sr_column_size;
        s_row.sr_values.clear();
        s_row.sr_values.resize(width + 1);
        this->ss_value_source->spectro_row(sr, s_row);
    }

    return s_row;
}

bool
spectrogram_source::text_is_row_selectable(textview_curses& tc, vis_line_t row)
{
    if (this->ss_value_source == nullptr) {
        return false;
    }

    const auto& s_row = this->load_row(tc, row);
    auto nearest_column
        = s_row.nearest_column(this->ss_cursor_column.value_or(0));

    return nearest_column.has_value();
}

void
spectrogram_source::text_selection_changed(textview_curses& tc)
{
    if (this->ss_value_source == nullptr || this->text_line_count() == 0) {
        this->ss_cursor_column = std::nullopt;
        this->ss_details_source.reset();
        return;
    }

    if (tc.get_selection() == -1_vl) {
        tc.set_selection(0_vl);
    }
    const auto& s_row = this->load_row(tc, tc.get_selection());
    this->ss_cursor_column
        = s_row.nearest_column(this->ss_cursor_column.value_or(0));
    this->ss_details_source.reset();
}

bool
spectrogram_source::list_static_overlay(const listview_curses& lv,
                                        int y,
                                        int bottom,
                                        attr_line_t& value_out)
{
    if (y != 0) {
        return false;
    }

    auto& line = value_out.get_string();
    vis_line_t height;
    unsigned long width;
    char buf[128];

    lv.get_dimensions(height, width);
    width -= 2;

    this->cache_bounds();

    if (this->ss_cached_line_count == 0) {
        value_out
            .append(lnav::roles::error("error: no data available, use the "))
            .append_quoted(lnav::roles::keyword(":spectrogram"))
            .append(lnav::roles::error(" command to visualize numeric data"));
        return true;
    }

    auto& sb = this->ss_cached_bounds;
    auto& st = this->ss_cached_thresholds;

    snprintf(buf, sizeof(buf), "Min: %'.10lg", sb.sb_min_value_out);
    line = buf;

    snprintf(buf,
             sizeof(buf),
             ANSI_ROLE("  ") " 1-%'d " ANSI_ROLE("  ") " %'d-%'d " ANSI_ROLE(
                 "  ") " %'d+",
             role_t::VCR_LOW_THRESHOLD,
             st.st_green_threshold - 1,
             role_t::VCR_MED_THRESHOLD,
             st.st_green_threshold,
             st.st_yellow_threshold - 1,
             role_t::VCR_HIGH_THRESHOLD,
             st.st_yellow_threshold);
    auto buflen = strlen(buf);
    if (line.length() + buflen + 20 < width) {
        line.append(width / 2 - buflen / 3 - line.length(), ' ');
    } else {
        line.append(" ");
    }
    line.append(buf);
    scrub_ansi_string(line, &value_out.get_attrs());

    snprintf(buf, sizeof(buf), "Max: %'.10lg", sb.sb_max_value_out);
    buflen = strlen(buf);
    if (line.length() + buflen + 4 < width) {
        line.append(width - buflen - line.length() - 2, ' ');
    } else {
        line.append(" ");
    }
    line.append(buf);

    value_out.with_attr(string_attr(
        line_range(0, -1), VC_STYLE.value(text_attrs::with_underline())));

    return true;
}

spectro_status_source::spectro_status_source()
{
    this->sss_fields[F_TITLE].set_width(9);
    this->sss_fields[F_TITLE].set_role(role_t::VCR_STATUS_TITLE);
    this->sss_fields[F_TITLE].set_value(" Details ");

    this->sss_fields[F_HELP].right_justify(true);
    this->sss_fields[F_HELP].set_width(20);
    this->sss_fields[F_HELP].set_value("Press " ANSI_BOLD("TAB") " to focus ");
    this->sss_fields[F_HELP].set_left_pad(1);
}

size_t
spectro_status_source::statusview_fields()
{
    return F_MAX;
}

status_field&
spectro_status_source::statusview_value_for_field(int field)
{
    return this->sss_fields[field];
}
