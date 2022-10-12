/**
 * Copyright (c) 2007-2012, Timothy Stack
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
#include <future>

#include "logfile_sub_source.hh"

#include <sqlite3.h>

#include "base/ansi_scrubber.hh"
#include "base/humanize.time.hh"
#include "base/injector.hh"
#include "base/itertools.hh"
#include "base/string_util.hh"
#include "bound_tags.hh"
#include "command_executor.hh"
#include "config.h"
#include "k_merge_tree.h"
#include "lnav.events.hh"
#include "log_accel.hh"
#include "logfile_sub_source.cfg.hh"
#include "md2attr_line.hh"
#include "readline_highlighters.hh"
#include "relative_time.hh"
#include "sql_util.hh"
#include "vtab_module.hh"
#include "yajlpp/yajlpp.hh"

const bookmark_type_t logfile_sub_source::BM_ERRORS("error");
const bookmark_type_t logfile_sub_source::BM_WARNINGS("warning");
const bookmark_type_t logfile_sub_source::BM_FILES("file");

static int
pretty_sql_callback(exec_context& ec, sqlite3_stmt* stmt)
{
    if (!sqlite3_stmt_busy(stmt)) {
        return 0;
    }

    int ncols = sqlite3_column_count(stmt);

    for (int lpc = 0; lpc < ncols; lpc++) {
        if (!ec.ec_accumulator->empty()) {
            ec.ec_accumulator->append(", ");
        }

        const char* res = (const char*) sqlite3_column_text(stmt, lpc);
        if (res == nullptr) {
            continue;
        }

        ec.ec_accumulator->append(res);
    }

    return 0;
}

static std::future<std::string>
pretty_pipe_callback(exec_context& ec, const std::string& cmdline, auto_fd& fd)
{
    auto retval = std::async(std::launch::async, [&]() {
        char buffer[1024];
        std::ostringstream ss;
        ssize_t rc;

        while ((rc = read(fd, buffer, sizeof(buffer))) > 0) {
            ss.write(buffer, rc);
        }

        auto retval = ss.str();

        if (endswith(retval, "\n")) {
            retval.resize(retval.length() - 1);
        }

        return retval;
    });

    return retval;
}

logfile_sub_source::logfile_sub_source()
    : text_sub_source(1), lss_meta_grepper(*this), lss_location_history(*this)
{
    this->tss_supports_filtering = true;
    this->clear_line_size_cache();
    this->clear_min_max_log_times();
}

std::shared_ptr<logfile>
logfile_sub_source::find(const char* fn, content_line_t& line_base)
{
    iterator iter;
    std::shared_ptr<logfile> retval = nullptr;

    line_base = content_line_t(0);
    for (iter = this->lss_files.begin();
         iter != this->lss_files.end() && retval == nullptr;
         iter++)
    {
        auto& ld = *(*iter);
        auto* lf = ld.get_file_ptr();

        if (lf == nullptr) {
            continue;
        }
        if (strcmp(lf->get_filename().c_str(), fn) == 0) {
            retval = ld.get_file();
        } else {
            line_base += content_line_t(MAX_LINES_PER_FILE);
        }
    }

    return retval;
}

nonstd::optional<vis_line_t>
logfile_sub_source::find_from_time(const struct timeval& start) const
{
    auto lb = lower_bound(this->lss_filtered_index.begin(),
                          this->lss_filtered_index.end(),
                          start,
                          filtered_logline_cmp(*this));
    if (lb != this->lss_filtered_index.end()) {
        return vis_line_t(lb - this->lss_filtered_index.begin());
    }

    return nonstd::nullopt;
}

void
logfile_sub_source::text_value_for_line(textview_curses& tc,
                                        int row,
                                        std::string& value_out,
                                        line_flags_t flags)
{
    content_line_t line(0);

    require(row >= 0);
    require((size_t) row < this->lss_filtered_index.size());

    line = this->at(vis_line_t(row));

    if (flags & RF_RAW) {
        auto lf = this->find(line);
        value_out = lf->read_line(lf->begin() + line)
                        .map([](auto sbr) { return to_string(sbr); })
                        .unwrapOr({});
        return;
    }

    require(!this->lss_in_value_for_line);

    this->lss_in_value_for_line = true;
    this->lss_token_flags = flags;
    this->lss_token_file_data = this->find_data(line);
    this->lss_token_file = (*this->lss_token_file_data)->get_file();
    this->lss_token_line = this->lss_token_file->begin() + line;

    this->lss_token_attrs.clear();
    this->lss_token_values.clear();
    this->lss_share_manager.invalidate_refs();
    if (flags & text_sub_source::RF_FULL) {
        shared_buffer_ref sbr;

        this->lss_token_file->read_full_message(this->lss_token_line, sbr);
        this->lss_token_value = to_string(sbr);
        if (sbr.get_metadata().m_has_ansi) {
            scrub_ansi_string(this->lss_token_value, &this->lss_token_attrs);
            sbr.get_metadata().m_has_ansi = false;
        }
    } else {
        this->lss_token_value
            = this->lss_token_file->read_line(this->lss_token_line)
                  .map([](auto sbr) { return to_string(sbr); })
                  .unwrapOr({});
        if (this->lss_token_line->has_ansi()) {
            scrub_ansi_string(this->lss_token_value, &this->lss_token_attrs);
        }
    }
    this->lss_token_shift_start = 0;
    this->lss_token_shift_size = 0;

    auto format = this->lss_token_file->get_format();

    value_out = this->lss_token_value;
    if (this->lss_flags & F_SCRUB) {
        format->scrub(value_out);
    }

    auto& sbr = this->lss_token_values.lvv_sbr;

    sbr.share(this->lss_share_manager,
              (char*) this->lss_token_value.c_str(),
              this->lss_token_value.size());
    format->annotate(line, this->lss_token_attrs, this->lss_token_values);
    if (this->lss_token_line->get_sub_offset() != 0) {
        this->lss_token_attrs.clear();
    }
    if (flags & RF_REWRITE) {
        exec_context ec(
            &this->lss_token_values, pretty_sql_callback, pretty_pipe_callback);
        std::string rewritten_line;

        ec.with_perms(exec_context::perm_t::READ_ONLY);
        ec.ec_local_vars.push(std::map<std::string, scoped_value_t>());
        ec.ec_top_line = vis_line_t(row);
        add_ansi_vars(ec.ec_global_vars);
        add_global_vars(ec);
        format->rewrite(ec, sbr, this->lss_token_attrs, rewritten_line);
        this->lss_token_value.assign(rewritten_line);
        value_out = this->lss_token_value;
    }

    if ((this->lss_token_file->is_time_adjusted()
         || format->lf_timestamp_flags & ETF_MACHINE_ORIENTED
         || !(format->lf_timestamp_flags & ETF_DAY_SET)
         || !(format->lf_timestamp_flags & ETF_MONTH_SET))
        && format->lf_date_time.dts_fmt_lock != -1)
    {
        auto time_attr
            = find_string_attr(this->lss_token_attrs, &logline::L_TIMESTAMP);
        if (time_attr != this->lss_token_attrs.end()) {
            const struct line_range time_range = time_attr->sa_range;
            struct timeval adjusted_time;
            struct exttm adjusted_tm;
            char buffer[128];
            const char* fmt;
            ssize_t len;

            if (format->lf_timestamp_flags & ETF_MACHINE_ORIENTED
                || !(format->lf_timestamp_flags & ETF_DAY_SET)
                || !(format->lf_timestamp_flags & ETF_MONTH_SET))
            {
                adjusted_time = this->lss_token_line->get_timeval();
                fmt = "%Y-%m-%d %H:%M:%S.%f";
                gmtime_r(&adjusted_time.tv_sec, &adjusted_tm.et_tm);
                adjusted_tm.et_nsec
                    = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::microseconds{adjusted_time.tv_usec})
                          .count();
                len = ftime_fmt(buffer, sizeof(buffer), fmt, adjusted_tm);
            } else {
                adjusted_time = this->lss_token_line->get_timeval();
                gmtime_r(&adjusted_time.tv_sec, &adjusted_tm.et_tm);
                adjusted_tm.et_nsec
                    = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::microseconds{adjusted_time.tv_usec})
                          .count();
                len = format->lf_date_time.ftime(
                    buffer,
                    sizeof(buffer),
                    format->get_timestamp_formats(),
                    adjusted_tm);
            }

            value_out.replace(
                time_range.lr_start, time_range.length(), buffer, len);
            this->lss_token_shift_start = time_range.lr_start;
            this->lss_token_shift_size = len - time_range.length();
        }
    }

    if (this->lss_flags & F_FILENAME || this->lss_flags & F_BASENAME) {
        size_t file_offset_end;
        std::string name;
        if (this->lss_flags & F_FILENAME) {
            file_offset_end = this->lss_filename_width;
            name = this->lss_token_file->get_filename();
            if (file_offset_end < name.size()) {
                file_offset_end = name.size();
                this->lss_filename_width = name.size();
            }
        } else {
            file_offset_end = this->lss_basename_width;
            name = this->lss_token_file->get_unique_path();
            if (file_offset_end < name.size()) {
                file_offset_end = name.size();
                this->lss_basename_width = name.size();
            }
        }
        value_out.insert(0, 1, '|');
        value_out.insert(0, file_offset_end - name.size(), ' ');
        value_out.insert(0, name);
    } else {
        // Insert space for the file/search-hit markers.
        value_out.insert(0, 1, ' ');
    }

    if (this->lss_flags & F_TIME_OFFSET) {
        auto curr_tv = this->lss_token_line->get_timeval();
        struct timeval diff_tv;
        auto row_vl = vis_line_t(row);

        auto prev_umark
            = tc.get_bookmarks()[&textview_curses::BM_USER].prev(row_vl);
        auto next_umark
            = tc.get_bookmarks()[&textview_curses::BM_USER].next(row_vl);
        auto prev_emark
            = tc.get_bookmarks()[&textview_curses::BM_USER_EXPR].prev(row_vl);
        auto next_emark
            = tc.get_bookmarks()[&textview_curses::BM_USER_EXPR].next(row_vl);
        if (!prev_umark && !prev_emark && (next_umark || next_emark)) {
            auto next_line = this->find_line(this->at(
                std::max(next_umark.value_or(0), next_emark.value_or(0))));

            diff_tv = curr_tv - next_line->get_timeval();
        } else {
            auto prev_row
                = std::max(prev_umark.value_or(0), prev_emark.value_or(0));
            auto first_line = this->find_line(this->at(prev_row));
            auto start_tv = first_line->get_timeval();
            diff_tv = curr_tv - start_tv;
        }

        auto relstr = humanize::time::duration::from_tv(diff_tv).to_string();
        value_out = fmt::format(FMT_STRING("{: >12}|{}"), relstr, value_out);
    }
    this->lss_in_value_for_line = false;
}

void
logfile_sub_source::text_attrs_for_line(textview_curses& lv,
                                        int row,
                                        string_attrs_t& value_out)
{
    view_colors& vc = view_colors::singleton();
    logline* next_line = nullptr;
    struct line_range lr;
    int time_offset_end = 0;
    text_attrs attrs;

    value_out = this->lss_token_attrs;

    if ((row + 1) < (int) this->lss_filtered_index.size()) {
        next_line = this->find_line(this->at(vis_line_t(row + 1)));
    }

    if (next_line != nullptr
        && (day_num(next_line->get_time())
            > day_num(this->lss_token_line->get_time())))
    {
        attrs.ta_attrs |= A_UNDERLINE;
    }

    const auto& line_values = this->lss_token_values;

    lr.lr_start = 0;
    lr.lr_end = this->lss_token_value.length();
    value_out.emplace_back(lr, SA_ORIGINAL_LINE.value());
    value_out.emplace_back(
        lr, SA_LEVEL.value(this->lss_token_line->get_msg_level()));

    lr.lr_start = time_offset_end;
    lr.lr_end = -1;

    value_out.emplace_back(lr, VC_STYLE.value(attrs));

    if (this->lss_token_line->get_msg_level() == log_level_t::LEVEL_INVALID) {
        for (auto& token_attr : this->lss_token_attrs) {
            if (token_attr.sa_type != &SA_INVALID) {
                continue;
            }

            value_out.emplace_back(token_attr.sa_range,
                                   VC_ROLE.value(role_t::VCR_INVALID_MSG));
        }
    }

    for (const auto& line_value : line_values.lvv_values) {
        if ((!(this->lss_token_flags & RF_FULL)
             && line_value.lv_sub_offset
                 != this->lss_token_line->get_sub_offset())
            || !line_value.lv_origin.is_valid())
        {
            continue;
        }

        if (line_value.lv_meta.is_hidden()) {
            value_out.emplace_back(line_value.lv_origin, SA_HIDDEN.value());
        }

        if (!line_value.lv_meta.lvm_identifier
            || !line_value.lv_origin.is_valid())
        {
            continue;
        }

        line_range ident_range = line_value.lv_origin;
        if (this->lss_token_flags & RF_FULL) {
            ident_range = line_value.origin_in_full_msg(
                this->lss_token_value.c_str(), this->lss_token_value.length());
        }

        value_out.emplace_back(ident_range,
                               VC_ROLE.value(role_t::VCR_IDENTIFIER));
    }

    if (this->lss_token_shift_size) {
        shift_string_attrs(value_out,
                           this->lss_token_shift_start + 1,
                           this->lss_token_shift_size);
    }

    shift_string_attrs(value_out, 0, 1);

    lr.lr_start = 0;
    lr.lr_end = 1;
    {
        auto& bm = lv.get_bookmarks();
        const auto& bv = bm[&BM_FILES];
        bool is_first_for_file
            = binary_search(bv.begin(), bv.end(), vis_line_t(row));
        bool is_last_for_file
            = binary_search(bv.begin(), bv.end(), vis_line_t(row + 1));
        chtype graph = ACS_VLINE;
        if (is_first_for_file) {
            if (is_last_for_file) {
                graph = ACS_HLINE;
            } else {
                graph = ACS_ULCORNER;
            }
        } else if (is_last_for_file) {
            graph = ACS_LLCORNER;
        }
        value_out.emplace_back(lr, VC_GRAPHIC.value(graph));

        if (!(this->lss_token_flags & RF_FULL)) {
            bookmark_vector<vis_line_t>& bv_search
                = bm[&textview_curses::BM_SEARCH];

            if (binary_search(std::begin(bv_search),
                              std::end(bv_search),
                              vis_line_t(row)))
            {
                lr.lr_start = 0;
                lr.lr_end = 1;
                value_out.emplace_back(lr,
                                       VC_STYLE.value(text_attrs{A_REVERSE}));
            }
        }
    }

    value_out.emplace_back(lr,
                           VC_STYLE.value(vc.attrs_for_ident(
                               this->lss_token_file->get_filename())));

    if (this->lss_flags & F_FILENAME || this->lss_flags & F_BASENAME) {
        size_t file_offset_end = (this->lss_flags & F_FILENAME)
            ? this->lss_filename_width
            : this->lss_basename_width;

        shift_string_attrs(value_out, 0, file_offset_end);

        lr.lr_start = 0;
        lr.lr_end = file_offset_end + 1;
        value_out.emplace_back(lr,
                               VC_STYLE.value(vc.attrs_for_ident(
                                   this->lss_token_file->get_filename())));
    }

    if (this->lss_flags & F_TIME_OFFSET) {
        time_offset_end = 13;
        lr.lr_start = 0;
        lr.lr_end = time_offset_end;

        shift_string_attrs(value_out, 0, time_offset_end);

        value_out.emplace_back(lr, VC_ROLE.value(role_t::VCR_OFFSET_TIME));
        value_out.emplace_back(line_range(12, 13), VC_GRAPHIC.value(ACS_VLINE));

        role_t bar_role = role_t::VCR_NONE;

        switch (this->get_line_accel_direction(vis_line_t(row))) {
            case log_accel::A_STEADY:
                break;
            case log_accel::A_DECEL:
                bar_role = role_t::VCR_DIFF_DELETE;
                break;
            case log_accel::A_ACCEL:
                bar_role = role_t::VCR_DIFF_ADD;
                break;
        }
        if (bar_role != role_t::VCR_NONE) {
            value_out.emplace_back(line_range(12, 13), VC_ROLE.value(bar_role));
        }
    }

    lr.lr_start = 0;
    lr.lr_end = -1;
    value_out.emplace_back(lr, logline::L_FILE.value(this->lss_token_file));
    value_out.emplace_back(
        lr, SA_FORMAT.value(this->lss_token_file->get_format()->get_name()));

    {
        const auto& bv = lv.get_bookmarks()[&textview_curses::BM_META];
        bookmark_vector<vis_line_t>::const_iterator bv_iter;

        bv_iter = lower_bound(bv.begin(), bv.end(), vis_line_t(row + 1));
        if (bv_iter != bv.begin()) {
            --bv_iter;
            auto line_meta_opt = this->find_bookmark_metadata(*bv_iter);

            if (line_meta_opt && !line_meta_opt.value()->bm_name.empty()) {
                lr.lr_start = 0;
                lr.lr_end = -1;
                value_out.emplace_back(
                    lr, logline::L_PARTITION.value(line_meta_opt.value()));
            }
        }

        auto line_meta_opt = this->find_bookmark_metadata(vis_line_t(row));

        if (line_meta_opt) {
            lr.lr_start = 0;
            lr.lr_end = -1;
            value_out.emplace_back(
                lr, logline::L_META.value(line_meta_opt.value()));
        }
    }

    if (this->lss_token_file->is_time_adjusted()) {
        struct line_range time_range
            = find_string_attr_range(value_out, &logline::L_TIMESTAMP);

        if (time_range.lr_end != -1) {
            value_out.emplace_back(time_range,
                                   VC_ROLE.value(role_t::VCR_ADJUSTED_TIME));
        }
    }

    if (this->lss_token_line->is_time_skewed()) {
        struct line_range time_range
            = find_string_attr_range(value_out, &logline::L_TIMESTAMP);

        if (time_range.lr_end != -1) {
            value_out.emplace_back(time_range,
                                   VC_ROLE.value(role_t::VCR_SKEWED_TIME));
        }
    }

    if (!this->lss_token_line->is_continued()) {
        if (this->lss_preview_filter_stmt != nullptr) {
            int color;
            auto eval_res
                = this->eval_sql_filter(this->lss_preview_filter_stmt.in(),
                                        this->lss_token_file_data,
                                        this->lss_token_line);
            if (eval_res.isErr()) {
                color = COLOR_YELLOW;
                value_out.emplace_back(
                    line_range{0, -1},
                    SA_ERROR.value(
                        eval_res.unwrapErr().to_attr_line().get_string()));
            } else {
                auto matched = eval_res.unwrap();

                if (matched) {
                    color = COLOR_GREEN;
                } else {
                    color = COLOR_RED;
                    value_out.emplace_back(line_range{0, 1},
                                           VC_STYLE.value(text_attrs{A_BLINK}));
                }
            }
            value_out.emplace_back(line_range{0, 1},
                                   VC_BACKGROUND.value(color));
        }

        auto sql_filter_opt = this->get_sql_filter();
        if (sql_filter_opt) {
            auto* sf = (sql_filter*) sql_filter_opt.value().get();
            auto eval_res = this->eval_sql_filter(sf->sf_filter_stmt.in(),
                                                  this->lss_token_file_data,
                                                  this->lss_token_line);
            if (eval_res.isErr()) {
                auto msg = fmt::format(
                    FMT_STRING(
                        "filter expression evaluation failed with -- {}"),
                    eval_res.unwrapErr().to_attr_line().get_string());
                auto color = COLOR_YELLOW;
                value_out.emplace_back(line_range{0, -1}, SA_ERROR.value(msg));
                value_out.emplace_back(line_range{0, 1},
                                       VC_BACKGROUND.value(color));
            }
        }
    }
}

logfile_sub_source::rebuild_result
logfile_sub_source::rebuild_index(
    nonstd::optional<ui_clock::time_point> deadline)
{
    if (this->tss_view == nullptr) {
        return rebuild_result::rr_no_change;
    }

    iterator iter;
    size_t total_lines = 0;
    bool full_sort = false;
    int file_count = 0;
    bool force = this->lss_force_rebuild;
    auto retval = rebuild_result::rr_no_change;
    nonstd::optional<struct timeval> lowest_tv = nonstd::nullopt;
    vis_line_t search_start = 0_vl;

    this->lss_force_rebuild = false;
    if (force) {
        log_debug("forced to full rebuild");
        retval = rebuild_result::rr_full_rebuild;
    }

    std::vector<size_t> file_order(this->lss_files.size());

    for (size_t lpc = 0; lpc < file_order.size(); lpc++) {
        file_order[lpc] = lpc;
    }
    if (!this->lss_index.empty()) {
        std::stable_sort(file_order.begin(),
                         file_order.end(),
                         [this](const auto& left, const auto& right) {
                             const auto& left_ld = this->lss_files[left];
                             const auto& right_ld = this->lss_files[right];

                             if (left_ld->get_file_ptr() == nullptr) {
                                 return true;
                             }
                             if (right_ld->get_file_ptr() == nullptr) {
                                 return false;
                             }

                             return left_ld->get_file_ptr()->back()
                                 < right_ld->get_file_ptr()->back();
                         });
    }

    bool time_left = true;
    for (const auto file_index : file_order) {
        auto& ld = *(this->lss_files[file_index]);
        auto* lf = ld.get_file_ptr();

        if (lf == nullptr) {
            if (ld.ld_lines_indexed > 0) {
                log_debug("%d: file closed, doing full rebuild",
                          ld.ld_file_index);
                force = true;
                retval = rebuild_result::rr_full_rebuild;
            }
        } else {
            if (time_left && deadline && ui_clock::now() > deadline.value()) {
                log_debug("no time left, skipping %s",
                          lf->get_filename().c_str());
                time_left = false;
            }

            if (!this->tss_view->is_paused() && time_left) {
                switch (lf->rebuild_index(deadline)) {
                    case logfile::rebuild_result_t::NO_NEW_LINES:
                        // No changes
                        break;
                    case logfile::rebuild_result_t::NEW_LINES:
                        if (retval == rebuild_result::rr_no_change) {
                            retval = rebuild_result::rr_appended_lines;
                        }
                        log_debug("new lines for %s:%d",
                                  lf->get_filename().c_str(),
                                  lf->size());
                        if (!this->lss_index.empty()
                            && lf->size() > ld.ld_lines_indexed)
                        {
                            logline& new_file_line = (*lf)[ld.ld_lines_indexed];
                            content_line_t cl = this->lss_index.back();
                            logline* last_indexed_line = this->find_line(cl);

                            // If there are new lines that are older than what
                            // we have in the index, we need to resort.
                            if (last_indexed_line == nullptr
                                || new_file_line
                                    < last_indexed_line->get_timeval())
                            {
                                log_debug(
                                    "%s:%ld: found older lines, full "
                                    "rebuild: %p  %lld < %lld",
                                    lf->get_filename().c_str(),
                                    ld.ld_lines_indexed,
                                    last_indexed_line,
                                    new_file_line.get_time_in_millis(),
                                    last_indexed_line == nullptr
                                        ? (uint64_t) -1
                                        : last_indexed_line
                                              ->get_time_in_millis());
                                if (retval
                                    <= rebuild_result::rr_partial_rebuild)
                                {
                                    retval = rebuild_result::rr_partial_rebuild;
                                    if (!lowest_tv) {
                                        lowest_tv = new_file_line.get_timeval();
                                    } else if (new_file_line.get_timeval()
                                               < lowest_tv.value())
                                    {
                                        lowest_tv = new_file_line.get_timeval();
                                    }
                                }
                            }
                        }
                        break;
                    case logfile::rebuild_result_t::INVALID:
                    case logfile::rebuild_result_t::NEW_ORDER:
                        log_debug("%s: log file has a new order, full rebuild",
                                  lf->get_filename().c_str());
                        retval = rebuild_result::rr_full_rebuild;
                        force = true;
                        full_sort = true;
                        break;
                }
            }
            file_count += 1;
            total_lines += lf->size();
        }
    }

    if (this->lss_index.empty() && !time_left) {
        return rebuild_result::rr_appended_lines;
    }

    if (this->lss_index.reserve(total_lines)) {
        force = true;
        retval = rebuild_result::rr_full_rebuild;
    }

    auto& vis_bm = this->tss_view->get_bookmarks();

    if (force) {
        for (iter = this->lss_files.begin(); iter != this->lss_files.end();
             iter++)
        {
            (*iter)->ld_lines_indexed = 0;
        }

        this->lss_index.clear();
        this->lss_filtered_index.clear();
        this->lss_longest_line = 0;
        this->lss_basename_width = 0;
        this->lss_filename_width = 0;
        vis_bm[&textview_curses::BM_USER_EXPR].clear();
    } else if (retval == rebuild_result::rr_partial_rebuild) {
        size_t remaining = 0;

        log_debug("partial rebuild with lowest time: %ld",
                  lowest_tv.value().tv_sec);
        for (iter = this->lss_files.begin(); iter != this->lss_files.end();
             iter++)
        {
            logfile_data& ld = *(*iter);
            auto* lf = ld.get_file_ptr();

            if (lf == nullptr) {
                continue;
            }

            auto line_iter = lf->find_from_time(lowest_tv.value());

            if (line_iter) {
                log_debug("%s: lowest line time %ld; line %ld; size %ld",
                          lf->get_filename().c_str(),
                          line_iter.value()->get_timeval().tv_sec,
                          std::distance(lf->cbegin(), line_iter.value()),
                          lf->size());
            }
            ld.ld_lines_indexed
                = std::distance(lf->cbegin(), line_iter.value_or(lf->cend()));
            remaining += lf->size() - ld.ld_lines_indexed;
        }

        auto row_iter = std::lower_bound(this->lss_index.begin(),
                                         this->lss_index.end(),
                                         *lowest_tv,
                                         logline_cmp(*this));
        this->lss_index.shrink_to(
            std::distance(this->lss_index.begin(), row_iter));
        log_debug("new index size %ld/%ld; remain %ld",
                  this->lss_index.ba_size,
                  this->lss_index.ba_capacity,
                  remaining);
        auto filt_row_iter = lower_bound(this->lss_filtered_index.begin(),
                                         this->lss_filtered_index.end(),
                                         *lowest_tv,
                                         filtered_logline_cmp(*this));
        this->lss_filtered_index.resize(
            std::distance(this->lss_filtered_index.begin(), filt_row_iter));
        search_start = vis_line_t(this->lss_filtered_index.size());

        auto bm_range = vis_bm[&textview_curses::BM_USER_EXPR].equal_range(
            search_start, -1_vl);
        auto bm_new_size = std::distance(
            vis_bm[&textview_curses::BM_USER_EXPR].begin(), bm_range.first);
        vis_bm[&textview_curses::BM_USER_EXPR].resize(bm_new_size);

        if (this->lss_index_delegate) {
            this->lss_index_delegate->index_start(*this);
            for (const auto row_in_full_index : this->lss_filtered_index) {
                auto cl = this->lss_index[row_in_full_index];
                uint64_t line_number;
                auto ld_iter = this->find_data(cl, line_number);
                auto& ld = *ld_iter;
                auto line_iter = ld->get_file_ptr()->begin() + line_number;

                this->lss_index_delegate->index_line(
                    *this, ld->get_file_ptr(), line_iter);
            }
        }
    }

    if (retval != rebuild_result::rr_no_change || force) {
        size_t index_size = 0, start_size = this->lss_index.size();
        logline_cmp line_cmper(*this);

        for (auto& ld : this->lss_files) {
            auto* lf = ld->get_file_ptr();

            if (lf == nullptr) {
                continue;
            }
            this->lss_longest_line = std::max(this->lss_longest_line,
                                              lf->get_longest_line_length());
            this->lss_basename_width = std::max(this->lss_basename_width,
                                                lf->get_unique_path().size());
            this->lss_filename_width
                = std::max(this->lss_filename_width, lf->get_filename().size());
        }

        if (full_sort) {
            for (auto& ld : this->lss_files) {
                auto* lf = ld->get_file_ptr();

                if (lf == nullptr) {
                    continue;
                }

                for (size_t line_index = 0; line_index < lf->size();
                     line_index++)
                {
                    if ((*lf)[line_index].is_ignored()) {
                        continue;
                    }

                    content_line_t con_line(
                        ld->ld_file_index * MAX_LINES_PER_FILE + line_index);

                    this->lss_index.push_back(con_line);
                }
            }

            // XXX get rid of this full sort on the initial run, it's not
            // needed unless the file is not in time-order
            if (this->lss_sorting_observer) {
                this->lss_sorting_observer(*this, 0, this->lss_index.size());
            }
            std::sort(
                this->lss_index.begin(), this->lss_index.end(), line_cmper);
            if (this->lss_sorting_observer) {
                this->lss_sorting_observer(
                    *this, this->lss_index.size(), this->lss_index.size());
            }
        } else {
            kmerge_tree_c<logline, logfile_data, logfile::iterator> merge(
                file_count);

            for (iter = this->lss_files.begin(); iter != this->lss_files.end();
                 iter++)
            {
                auto* ld = iter->get();
                auto* lf = ld->get_file_ptr();
                if (lf == nullptr) {
                    continue;
                }

                merge.add(ld, lf->begin() + ld->ld_lines_indexed, lf->end());
                index_size += lf->size();
            }

            file_off_t index_off = 0;
            merge.execute();
            if (this->lss_sorting_observer) {
                this->lss_sorting_observer(*this, index_off, index_size);
            }
            for (;;) {
                logfile::iterator lf_iter;
                logfile_data* ld;

                if (!merge.get_top(ld, lf_iter)) {
                    break;
                }

                if (!lf_iter->is_ignored()) {
                    int file_index = ld->ld_file_index;
                    int line_index = lf_iter - ld->get_file_ptr()->begin();

                    content_line_t con_line(file_index * MAX_LINES_PER_FILE
                                            + line_index);

                    if (lf_iter->is_marked()) {
                        auto start_iter = lf_iter;
                        while (start_iter->is_continued()) {
                            --start_iter;
                        }
                        int start_index
                            = start_iter - ld->get_file_ptr()->begin();
                        content_line_t start_con_line(
                            file_index * MAX_LINES_PER_FILE + start_index);

                        this->lss_user_marks[&textview_curses::BM_META]
                            .insert_once(start_con_line);
                        lf_iter->set_mark(false);
                    }
                    this->lss_index.push_back(con_line);
                }

                merge.next();
                index_off += 1;
                if (index_off % 10000 == 0 && this->lss_sorting_observer) {
                    this->lss_sorting_observer(*this, index_off, index_size);
                }
            }
            if (this->lss_sorting_observer) {
                this->lss_sorting_observer(*this, index_size, index_size);
            }
        }

        for (iter = this->lss_files.begin(); iter != this->lss_files.end();
             iter++)
        {
            auto* lf = (*iter)->get_file_ptr();

            if (lf == nullptr) {
                continue;
            }

            (*iter)->ld_lines_indexed = lf->size();
        }

        this->lss_filtered_index.reserve(this->lss_index.size());

        uint32_t filter_in_mask, filter_out_mask;
        this->get_filters().get_enabled_mask(filter_in_mask, filter_out_mask);

        if (start_size == 0 && this->lss_index_delegate != nullptr) {
            this->lss_index_delegate->index_start(*this);
        }

        for (size_t index_index = start_size;
             index_index < this->lss_index.size();
             index_index++)
        {
            content_line_t cl = (content_line_t) this->lss_index[index_index];
            uint64_t line_number;
            auto ld = this->find_data(cl, line_number);

            if (!(*ld)->is_visible()) {
                continue;
            }

            auto* lf = (*ld)->get_file_ptr();
            auto line_iter = lf->begin() + line_number;

            if (line_iter->is_ignored()) {
                continue;
            }

            if (!this->tss_apply_filters
                || (!(*ld)->ld_filter_state.excluded(
                        filter_in_mask, filter_out_mask, line_number)
                    && this->check_extra_filters(ld, line_iter)))
            {
                auto eval_res = this->eval_sql_filter(
                    this->lss_marker_stmt.in(), ld, line_iter);
                if (eval_res.isErr()) {
                    line_iter->set_expr_mark(false);
                } else {
                    auto matched = eval_res.unwrap();

                    if (matched) {
                        line_iter->set_expr_mark(true);
                        vis_bm[&textview_curses::BM_USER_EXPR].insert_once(
                            vis_line_t(this->lss_filtered_index.size()));
                    } else {
                        line_iter->set_expr_mark(false);
                    }
                }
                this->lss_filtered_index.push_back(index_index);
                if (this->lss_index_delegate != nullptr) {
                    this->lss_index_delegate->index_line(
                        *this, lf, lf->begin() + line_number);
                }
            }
        }

        if (this->lss_index_delegate != nullptr) {
            this->lss_index_delegate->index_complete(*this);
        }
    }

    switch (retval) {
        case rebuild_result::rr_no_change:
            break;
        case rebuild_result::rr_full_rebuild:
            log_debug("redoing search");
            this->lss_index_generation += 1;
            this->tss_view->redo_search();
            break;
        case rebuild_result::rr_partial_rebuild:
            log_debug("redoing search from: %d", (int) search_start);
            this->lss_index_generation += 1;
            this->tss_view->search_new_data(search_start);
            break;
        case rebuild_result::rr_appended_lines:
            this->tss_view->search_new_data();
            break;
    }

    return retval;
}

void
logfile_sub_source::text_update_marks(vis_bookmarks& bm)
{
    std::shared_ptr<logfile> last_file;
    vis_line_t vl;

    bm[&BM_WARNINGS].clear();
    bm[&BM_ERRORS].clear();
    bm[&BM_FILES].clear();

    for (auto& lss_user_mark : this->lss_user_marks) {
        bm[lss_user_mark.first].clear();
    }

    for (; vl < (int) this->lss_filtered_index.size(); ++vl) {
        const content_line_t orig_cl = this->at(vl);
        content_line_t cl = orig_cl;
        auto lf = this->find(cl);

        for (auto& lss_user_mark : this->lss_user_marks) {
            if (binary_search(lss_user_mark.second.begin(),
                              lss_user_mark.second.end(),
                              orig_cl))
            {
                bm[lss_user_mark.first].insert_once(vl);

                if (lss_user_mark.first == &textview_curses::BM_USER) {
                    auto ll = lf->begin() + cl;

                    ll->set_mark(true);
                }
            }
        }

        if (lf != last_file) {
            bm[&BM_FILES].insert_once(vl);
        }

        auto line_iter = lf->begin() + cl;
        if (line_iter->is_message()) {
            switch (line_iter->get_msg_level()) {
                case LEVEL_WARNING:
                    bm[&BM_WARNINGS].insert_once(vl);
                    break;

                case LEVEL_FATAL:
                case LEVEL_ERROR:
                case LEVEL_CRITICAL:
                    bm[&BM_ERRORS].insert_once(vl);
                    break;

                default:
                    break;
            }
        }

        last_file = lf;
    }
}

log_accel::direction_t
logfile_sub_source::get_line_accel_direction(vis_line_t vl)
{
    log_accel la;

    while (vl >= 0) {
        logline* curr_line = this->find_line(this->at(vl));

        if (!curr_line->is_message()) {
            --vl;
            continue;
        }

        if (!la.add_point(curr_line->get_time_in_millis())) {
            break;
        }

        --vl;
    }

    return la.get_direction();
}

void
logfile_sub_source::text_filters_changed()
{
    this->lss_index_generation += 1;

    if (this->lss_line_meta_changed) {
        this->invalidate_sql_filter();
        this->lss_line_meta_changed = false;
    }

    for (auto& ld : *this) {
        auto* lf = ld->get_file_ptr();

        if (lf != nullptr) {
            ld->ld_filter_state.clear_deleted_filter_state();
            lf->reobserve_from(lf->begin()
                               + ld->ld_filter_state.get_min_count(lf->size()));
        }
    }

    auto& vis_bm = this->tss_view->get_bookmarks();
    uint32_t filtered_in_mask, filtered_out_mask;

    this->get_filters().get_enabled_mask(filtered_in_mask, filtered_out_mask);

    if (this->lss_index_delegate != nullptr) {
        this->lss_index_delegate->index_start(*this);
    }
    vis_bm[&textview_curses::BM_USER_EXPR].clear();

    this->lss_filtered_index.clear();
    for (size_t index_index = 0; index_index < this->lss_index.size();
         index_index++)
    {
        content_line_t cl = (content_line_t) this->lss_index[index_index];
        uint64_t line_number;
        auto ld = this->find_data(cl, line_number);

        if (!(*ld)->is_visible()) {
            continue;
        }

        auto lf = (*ld)->get_file_ptr();
        auto line_iter = lf->begin() + line_number;

        if (!this->tss_apply_filters
            || (!(*ld)->ld_filter_state.excluded(
                    filtered_in_mask, filtered_out_mask, line_number)
                && this->check_extra_filters(ld, line_iter)))
        {
            auto eval_res = this->eval_sql_filter(
                this->lss_marker_stmt.in(), ld, line_iter);
            if (eval_res.isErr()) {
                line_iter->set_expr_mark(false);
            } else {
                auto matched = eval_res.unwrap();

                if (matched) {
                    line_iter->set_expr_mark(true);
                    vis_bm[&textview_curses::BM_USER_EXPR].insert_once(
                        vis_line_t(this->lss_filtered_index.size()));
                } else {
                    line_iter->set_expr_mark(false);
                }
            }
            this->lss_filtered_index.push_back(index_index);
            if (this->lss_index_delegate != nullptr) {
                this->lss_index_delegate->index_line(*this, lf, line_iter);
            }
        }
    }

    if (this->lss_index_delegate != nullptr) {
        this->lss_index_delegate->index_complete(*this);
    }

    if (this->tss_view != nullptr) {
        this->tss_view->reload_data();
        this->tss_view->redo_search();
    }
}

bool
logfile_sub_source::list_input_handle_key(listview_curses& lv, int ch)
{
    switch (ch) {
        case 'h':
        case 'H':
        case KEY_SLEFT:
        case KEY_LEFT:
            if (lv.get_left() == 0) {
                this->increase_line_context();
                lv.set_needs_update();
                return true;
            }
            break;
        case 'l':
        case 'L':
        case KEY_SRIGHT:
        case KEY_RIGHT:
            if (this->decrease_line_context()) {
                lv.set_needs_update();
                return true;
            }
            break;
    }
    return false;
}

nonstd::optional<
    std::pair<grep_proc_source<vis_line_t>*, grep_proc_sink<vis_line_t>*>>
logfile_sub_source::get_grepper()
{
    return std::make_pair(
        (grep_proc_source<vis_line_t>*) &this->lss_meta_grepper,
        (grep_proc_sink<vis_line_t>*) &this->lss_meta_grepper);
}

bool
logfile_sub_source::insert_file(const std::shared_ptr<logfile>& lf)
{
    iterator existing;

    require(lf->size() < MAX_LINES_PER_FILE);

    existing = std::find_if(this->lss_files.begin(),
                            this->lss_files.end(),
                            logfile_data_eq(nullptr));
    if (existing == this->lss_files.end()) {
        if (this->lss_files.size() >= MAX_FILES) {
            return false;
        }

        auto ld = std::make_unique<logfile_data>(
            this->lss_files.size(), this->get_filters(), lf);
        ld->set_visibility(lf->get_open_options().loo_is_visible);
        this->lss_files.push_back(std::move(ld));
    } else {
        (*existing)->set_file(lf);
    }
    this->lss_force_rebuild = true;

    return true;
}

Result<void, lnav::console::user_message>
logfile_sub_source::set_sql_filter(std::string stmt_str, sqlite3_stmt* stmt)
{
    for (auto& filt : this->tss_filters) {
        log_debug("set filt %p %d", filt.get(), filt->lf_deleted);
    }
    if (stmt != nullptr && !this->lss_filtered_index.empty()) {
        auto top_cl = this->at(0_vl);
        auto ld = this->find_data(top_cl);
        auto eval_res
            = this->eval_sql_filter(stmt, ld, (*ld)->get_file_ptr()->begin());

        if (eval_res.isErr()) {
            sqlite3_finalize(stmt);
            return Err(eval_res.unwrapErr());
        }
    }

    for (auto& ld : *this) {
        ld->ld_filter_state.lfo_filter_state.clear_filter_state(0);
    }

    auto old_filter = this->get_sql_filter();
    if (stmt != nullptr) {
        auto new_filter
            = std::make_shared<sql_filter>(*this, std::move(stmt_str), stmt);

        log_debug("fstack %p new %p", &this->tss_filters, new_filter.get());
        if (old_filter) {
            auto existing_iter = std::find(this->tss_filters.begin(),
                                           this->tss_filters.end(),
                                           old_filter.value());
            *existing_iter = new_filter;
        } else {
            this->tss_filters.add_filter(new_filter);
        }
    } else if (old_filter) {
        this->tss_filters.delete_filter(old_filter.value()->get_id());
    }

    return Ok();
}

Result<void, lnav::console::user_message>
logfile_sub_source::set_sql_marker(std::string stmt_str, sqlite3_stmt* stmt)
{
    if (stmt != nullptr && !this->lss_filtered_index.empty()) {
        auto top_cl = this->at(0_vl);
        auto ld = this->find_data(top_cl);
        auto eval_res
            = this->eval_sql_filter(stmt, ld, (*ld)->get_file_ptr()->begin());

        if (eval_res.isErr()) {
            sqlite3_finalize(stmt);
            return Err(eval_res.unwrapErr());
        }
    }

    this->lss_marker_stmt_text = std::move(stmt_str);
    this->lss_marker_stmt = stmt;

    if (this->tss_view == nullptr) {
        return Ok();
    }

    auto& vis_bm = this->tss_view->get_bookmarks();
    auto& expr_marks_bv = vis_bm[&textview_curses::BM_USER_EXPR];

    expr_marks_bv.clear();
    if (this->lss_index_delegate) {
        this->lss_index_delegate->index_start(*this);
    }
    for (auto row = 0_vl; row < vis_line_t(this->lss_filtered_index.size());
         row += 1_vl)
    {
        auto cl = this->at(row);
        auto ld = this->find_data(cl);
        auto ll = (*ld)->get_file()->begin() + cl;
        auto eval_res
            = this->eval_sql_filter(this->lss_marker_stmt.in(), ld, ll);

        if (eval_res.isErr()) {
            ll->set_expr_mark(false);
        } else {
            auto matched = eval_res.unwrap();

            if (matched) {
                ll->set_expr_mark(true);
                expr_marks_bv.insert_once(row);
            } else {
                ll->set_expr_mark(false);
            }
        }
        if (this->lss_index_delegate) {
            this->lss_index_delegate->index_line(
                *this, (*ld)->get_file_ptr(), ll);
        }
    }
    if (this->lss_index_delegate) {
        this->lss_index_delegate->index_complete(*this);
    }

    return Ok();
}

Result<void, lnav::console::user_message>
logfile_sub_source::set_preview_sql_filter(sqlite3_stmt* stmt)
{
    if (stmt != nullptr && !this->lss_filtered_index.empty()) {
        auto top_cl = this->at(0_vl);
        auto ld = this->find_data(top_cl);
        auto eval_res
            = this->eval_sql_filter(stmt, ld, (*ld)->get_file_ptr()->begin());

        if (eval_res.isErr()) {
            sqlite3_finalize(stmt);
            return Err(eval_res.unwrapErr());
        }
    }

    this->lss_preview_filter_stmt = stmt;

    return Ok();
}

Result<bool, lnav::console::user_message>
logfile_sub_source::eval_sql_filter(sqlite3_stmt* stmt,
                                    iterator ld,
                                    logfile::const_iterator ll)
{
    if (stmt == nullptr) {
        return Ok(false);
    }

    auto* lf = (*ld)->get_file_ptr();
    char timestamp_buffer[64];
    shared_buffer_ref raw_sbr;
    logline_value_vector values;
    auto& sbr = values.lvv_sbr;
    lf->read_full_message(ll, sbr);
    sbr.erase_ansi();
    auto format = lf->get_format();
    string_attrs_t sa;
    auto line_number = std::distance(lf->cbegin(), ll);
    format->annotate(line_number, sa, values);

    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);

    auto count = sqlite3_bind_parameter_count(stmt);
    for (int lpc = 0; lpc < count; lpc++) {
        const auto* name = sqlite3_bind_parameter_name(stmt, lpc + 1);

        if (name[0] == '$') {
            const char* env_value;

            if ((env_value = getenv(&name[1])) != nullptr) {
                sqlite3_bind_text(stmt, lpc + 1, env_value, -1, SQLITE_STATIC);
            }
            continue;
        }
        if (strcmp(name, ":log_level") == 0) {
            sqlite3_bind_text(
                stmt, lpc + 1, ll->get_level_name(), -1, SQLITE_STATIC);
            continue;
        }
        if (strcmp(name, ":log_time") == 0) {
            auto len = sql_strftime(timestamp_buffer,
                                    sizeof(timestamp_buffer),
                                    ll->get_timeval(),
                                    'T');
            sqlite3_bind_text(
                stmt, lpc + 1, timestamp_buffer, len, SQLITE_STATIC);
            continue;
        }
        if (strcmp(name, ":log_time_msecs") == 0) {
            sqlite3_bind_int64(stmt, lpc + 1, ll->get_time_in_millis());
            continue;
        }
        if (strcmp(name, ":log_mark") == 0) {
            sqlite3_bind_int(stmt, lpc + 1, ll->is_marked());
            continue;
        }
        if (strcmp(name, ":log_comment") == 0) {
            const auto& bm = lf->get_bookmark_metadata();
            auto line_number
                = static_cast<uint32_t>(std::distance(lf->cbegin(), ll));
            auto bm_iter = bm.find(line_number);
            if (bm_iter != bm.end() && !bm_iter->second.bm_comment.empty()) {
                const auto& meta = bm_iter->second;
                sqlite3_bind_text(stmt,
                                  lpc + 1,
                                  meta.bm_comment.c_str(),
                                  meta.bm_comment.length(),
                                  SQLITE_STATIC);
            }
            continue;
        }
        if (strcmp(name, ":log_tags") == 0) {
            const auto& bm = lf->get_bookmark_metadata();
            auto line_number
                = static_cast<uint32_t>(std::distance(lf->cbegin(), ll));
            auto bm_iter = bm.find(line_number);
            if (bm_iter != bm.end() && !bm_iter->second.bm_tags.empty()) {
                const auto& meta = bm_iter->second;
                yajlpp_gen gen;

                yajl_gen_config(gen, yajl_gen_beautify, false);

                {
                    yajlpp_array arr(gen);

                    for (const auto& str : meta.bm_tags) {
                        arr.gen(str);
                    }
                }

                string_fragment sf = gen.to_string_fragment();

                sqlite3_bind_text(
                    stmt, lpc + 1, sf.data(), sf.length(), SQLITE_TRANSIENT);
            }
            continue;
        }
        if (strcmp(name, ":log_format") == 0) {
            const auto format_name = format->get_name();
            sqlite3_bind_text(stmt,
                              lpc + 1,
                              format_name.get(),
                              format_name.size(),
                              SQLITE_STATIC);
            continue;
        }
        if (strcmp(name, ":log_format_regex") == 0) {
            const auto pat_name = format->get_pattern_name(line_number);
            sqlite3_bind_text(
                stmt, lpc + 1, pat_name.get(), pat_name.size(), SQLITE_STATIC);
            continue;
        }
        if (strcmp(name, ":log_path") == 0) {
            const auto& filename = lf->get_filename();
            sqlite3_bind_text(stmt,
                              lpc + 1,
                              filename.c_str(),
                              filename.length(),
                              SQLITE_STATIC);
            continue;
        }
        if (strcmp(name, ":log_unique_path") == 0) {
            const auto& filename = lf->get_unique_path();
            sqlite3_bind_text(stmt,
                              lpc + 1,
                              filename.c_str(),
                              filename.length(),
                              SQLITE_STATIC);
            continue;
        }
        if (strcmp(name, ":log_text") == 0) {
            sqlite3_bind_text(
                stmt, lpc + 1, sbr.get_data(), sbr.length(), SQLITE_STATIC);
            continue;
        }
        if (strcmp(name, ":log_body") == 0) {
            auto body_attr_opt = get_string_attr(sa, SA_BODY);
            if (body_attr_opt) {
                const auto& sar
                    = body_attr_opt.value().saw_string_attr->sa_range;

                sqlite3_bind_text(stmt,
                                  lpc + 1,
                                  sbr.get_data_at(sar.lr_start),
                                  sar.length(),
                                  SQLITE_STATIC);
            } else {
                sqlite3_bind_null(stmt, lpc + 1);
            }
            continue;
        }
        if (strcmp(name, ":log_opid") == 0) {
            auto opid_attr_opt = get_string_attr(sa, logline::L_OPID);
            if (opid_attr_opt) {
                const auto& sar
                    = opid_attr_opt.value().saw_string_attr->sa_range;

                sqlite3_bind_text(stmt,
                                  lpc + 1,
                                  sbr.get_data_at(sar.lr_start),
                                  sar.length(),
                                  SQLITE_STATIC);
            } else {
                sqlite3_bind_null(stmt, lpc + 1);
            }
            continue;
        }
        if (strcmp(name, ":log_raw_text") == 0) {
            auto res = lf->read_raw_message(ll);

            if (res.isOk()) {
                raw_sbr = res.unwrap();
                sqlite3_bind_text(stmt,
                                  lpc + 1,
                                  raw_sbr.get_data(),
                                  raw_sbr.length(),
                                  SQLITE_STATIC);
            }
            continue;
        }
        for (const auto& lv : values.lvv_values) {
            if (lv.lv_meta.lvm_name != &name[1]) {
                continue;
            }

            switch (lv.lv_meta.lvm_kind) {
                case value_kind_t::VALUE_BOOLEAN:
                    sqlite3_bind_int64(stmt, lpc + 1, lv.lv_value.i);
                    break;
                case value_kind_t::VALUE_FLOAT:
                    sqlite3_bind_double(stmt, lpc + 1, lv.lv_value.d);
                    break;
                case value_kind_t::VALUE_INTEGER:
                    sqlite3_bind_int64(stmt, lpc + 1, lv.lv_value.i);
                    break;
                case value_kind_t::VALUE_NULL:
                    sqlite3_bind_null(stmt, lpc + 1);
                    break;
                default:
                    sqlite3_bind_text(stmt,
                                      lpc + 1,
                                      lv.text_value(),
                                      lv.text_length(),
                                      SQLITE_TRANSIENT);
                    break;
            }
            break;
        }
    }

    auto step_res = sqlite3_step(stmt);

    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    switch (step_res) {
        case SQLITE_OK:
        case SQLITE_DONE:
            return Ok(false);
        case SQLITE_ROW:
            return Ok(true);
        default:
            return Err(sqlite3_error_to_user_message(sqlite3_db_handle(stmt)));
    }

    return Ok(true);
}

bool
logfile_sub_source::check_extra_filters(iterator ld, logfile::iterator ll)
{
    if (this->lss_marked_only && !(ll->is_marked() || ll->is_expr_marked())) {
        return false;
    }

    if (ll->get_msg_level() < this->lss_min_log_level) {
        return false;
    }

    if (*ll < this->lss_min_log_time) {
        return false;
    }

    if (!(*ll <= this->lss_max_log_time)) {
        return false;
    }

    return true;
}

void
logfile_sub_source::invalidate_sql_filter()
{
    for (auto& ld : *this) {
        ld->ld_filter_state.lfo_filter_state.clear_filter_state(0);
    }
}

void
logfile_sub_source::text_mark(const bookmark_type_t* bm,
                              vis_line_t line,
                              bool added)
{
    if (line >= (int) this->lss_index.size()) {
        return;
    }

    content_line_t cl = this->at(line);
    std::vector<content_line_t>::iterator lb;

    if (bm == &textview_curses::BM_USER) {
        logline* ll = this->find_line(cl);

        ll->set_mark(added);
    }
    lb = std::lower_bound(
        this->lss_user_marks[bm].begin(), this->lss_user_marks[bm].end(), cl);
    if (added) {
        if (lb == this->lss_user_marks[bm].end() || *lb != cl) {
            this->lss_user_marks[bm].insert(lb, cl);
        }
    } else if (lb != this->lss_user_marks[bm].end() && *lb == cl) {
        require(lb != this->lss_user_marks[bm].end());

        this->lss_user_marks[bm].erase(lb);
    }
    if (bm == &textview_curses::BM_META
        && this->lss_meta_grepper.gps_proc != nullptr)
    {
        this->tss_view->search_range(line, line + 1_vl);
        this->tss_view->search_new_data();
    }
}

void
logfile_sub_source::text_clear_marks(const bookmark_type_t* bm)
{
    std::vector<content_line_t>::iterator iter;

    if (bm == &textview_curses::BM_USER) {
        for (iter = this->lss_user_marks[bm].begin();
             iter != this->lss_user_marks[bm].end();)
        {
            auto line_meta_opt = this->find_bookmark_metadata(*iter);
            if (line_meta_opt) {
                ++iter;
                continue;
            }
            this->find_line(*iter)->set_mark(false);
            iter = this->lss_user_marks[bm].erase(iter);
        }
    } else {
        this->lss_user_marks[bm].clear();
    }
}

void
logfile_sub_source::remove_file(std::shared_ptr<logfile> lf)
{
    iterator iter;

    iter = std::find_if(
        this->lss_files.begin(), this->lss_files.end(), logfile_data_eq(lf));
    if (iter != this->lss_files.end()) {
        bookmarks<content_line_t>::type::iterator mark_iter;
        int file_index = iter - this->lss_files.begin();

        (*iter)->clear();
        for (mark_iter = this->lss_user_marks.begin();
             mark_iter != this->lss_user_marks.end();
             ++mark_iter)
        {
            auto mark_curr = content_line_t(file_index * MAX_LINES_PER_FILE);
            auto mark_end
                = content_line_t((file_index + 1) * MAX_LINES_PER_FILE);
            auto& bv = mark_iter->second;
            auto file_range = bv.equal_range(mark_curr, mark_end);

            if (file_range.first != file_range.second) {
                bv.erase(file_range.first, file_range.second);
            }
        }

        this->lss_force_rebuild = true;
    }
}

nonstd::optional<vis_line_t>
logfile_sub_source::find_from_content(content_line_t cl)
{
    content_line_t line = cl;
    std::shared_ptr<logfile> lf = this->find(line);

    if (lf != nullptr) {
        auto ll_iter = lf->begin() + line;
        auto& ll = *ll_iter;
        auto vis_start_opt = this->find_from_time(ll.get_timeval());

        if (!vis_start_opt) {
            return nonstd::nullopt;
        }

        auto vis_start = *vis_start_opt;

        while (vis_start < vis_line_t(this->text_line_count())) {
            content_line_t guess_cl = this->at(vis_start);

            if (cl == guess_cl) {
                return vis_start;
            }

            auto guess_line = this->find_line(guess_cl);

            if (!guess_line || ll < *guess_line) {
                return nonstd::nullopt;
            }

            ++vis_start;
        }
    }

    return nonstd::nullopt;
}

void
logfile_sub_source::reload_index_delegate()
{
    if (this->lss_index_delegate == nullptr) {
        return;
    }

    this->lss_index_delegate->index_start(*this);
    for (unsigned int index : this->lss_filtered_index) {
        content_line_t cl = (content_line_t) this->lss_index[index];
        uint64_t line_number;
        auto ld = this->find_data(cl, line_number);
        std::shared_ptr<logfile> lf = (*ld)->get_file();

        this->lss_index_delegate->index_line(
            *this, lf.get(), lf->begin() + line_number);
    }
    this->lss_index_delegate->index_complete(*this);
}

nonstd::optional<std::shared_ptr<text_filter>>
logfile_sub_source::get_sql_filter()
{
    return this->tss_filters | lnav::itertools::find_if([](const auto& filt) {
               return filt->get_index() == 0;
           })
        | lnav::itertools::deref();
}

void
log_location_history::loc_history_append(vis_line_t top)
{
    if (top >= vis_line_t(this->llh_log_source.text_line_count())) {
        return;
    }

    content_line_t cl = this->llh_log_source.at(top);

    auto iter = this->llh_history.begin();
    iter += this->llh_history.size() - this->lh_history_position;
    this->llh_history.erase_from(iter);
    this->lh_history_position = 0;
    this->llh_history.push_back(cl);
}

nonstd::optional<vis_line_t>
log_location_history::loc_history_back(vis_line_t current_top)
{
    while (this->lh_history_position < this->llh_history.size()) {
        auto iter = this->llh_history.rbegin();

        auto vis_for_pos = this->llh_log_source.find_from_content(*iter);

        if (this->lh_history_position == 0 && vis_for_pos != current_top) {
            return vis_for_pos;
        }

        if ((this->lh_history_position + 1) >= this->llh_history.size()) {
            break;
        }

        this->lh_history_position += 1;

        iter += this->lh_history_position;

        vis_for_pos = this->llh_log_source.find_from_content(*iter);

        if (vis_for_pos) {
            return vis_for_pos;
        }
    }

    return nonstd::nullopt;
}

nonstd::optional<vis_line_t>
log_location_history::loc_history_forward(vis_line_t current_top)
{
    while (this->lh_history_position > 0) {
        this->lh_history_position -= 1;

        auto iter = this->llh_history.rbegin();

        iter += this->lh_history_position;

        auto vis_for_pos = this->llh_log_source.find_from_content(*iter);

        if (vis_for_pos) {
            return vis_for_pos;
        }
    }

    return nonstd::nullopt;
}

bool
sql_filter::matches(const logfile& lf,
                    logfile::const_iterator ll,
                    shared_buffer_ref& line)
{
    if (!ll->is_message()) {
        return false;
    }
    if (this->sf_filter_stmt == nullptr) {
        return false;
    }

    auto lfp = lf.shared_from_this();
    auto ld = this->sf_log_source.find_data_i(lfp);
    if (ld == this->sf_log_source.end()) {
        return false;
    }

    auto eval_res
        = this->sf_log_source.eval_sql_filter(this->sf_filter_stmt, ld, ll);
    if (eval_res.unwrapOr(true)) {
        return false;
    }

    return true;
}

std::string
sql_filter::to_command() const
{
    return fmt::format(FMT_STRING("filter-expr {}"), this->lf_id);
}

bool
logfile_sub_source::meta_grepper::grep_value_for_line(vis_line_t line,
                                                      std::string& value_out)
{
    auto line_meta_opt = this->lmg_source.find_bookmark_metadata(line);
    if (!line_meta_opt) {
        value_out.clear();
    } else {
        auto& bm = *(line_meta_opt.value());

        {
            md2attr_line mdal;

            auto parse_res = md4cpp::parse(bm.bm_comment, mdal);
            if (parse_res.isOk()) {
                value_out.append(parse_res.unwrap().get_string());
            } else {
                value_out.append(bm.bm_comment);
            }
        }

        value_out.append("\x1c");
        for (const auto& tag : bm.bm_tags) {
            value_out.append(tag);
            value_out.append("\x1c");
        }
    }

    return !this->lmg_done;
}

vis_line_t
logfile_sub_source::meta_grepper::grep_initial_line(vis_line_t start,
                                                    vis_line_t highest)
{
    vis_bookmarks& bm = this->lmg_source.tss_view->get_bookmarks();
    bookmark_vector<vis_line_t>& bv = bm[&textview_curses::BM_META];

    if (bv.empty()) {
        return -1_vl;
    }
    return *bv.begin();
}

void
logfile_sub_source::meta_grepper::grep_next_line(vis_line_t& line)
{
    vis_bookmarks& bm = this->lmg_source.tss_view->get_bookmarks();
    bookmark_vector<vis_line_t>& bv = bm[&textview_curses::BM_META];

    auto line_opt = bv.next(vis_line_t(line));
    if (!line_opt) {
        this->lmg_done = true;
    }
    line = line_opt.value_or(-1_vl);
}

void
logfile_sub_source::meta_grepper::grep_begin(grep_proc<vis_line_t>& gp,
                                             vis_line_t start,
                                             vis_line_t stop)
{
    this->lmg_source.quiesce();

    this->lmg_source.tss_view->grep_begin(gp, start, stop);
}

void
logfile_sub_source::meta_grepper::grep_end(grep_proc<vis_line_t>& gp)
{
    this->lmg_source.tss_view->grep_end(gp);
}

void
logfile_sub_source::meta_grepper::grep_match(grep_proc<vis_line_t>& gp,
                                             vis_line_t line,
                                             int start,
                                             int end)
{
    this->lmg_source.tss_view->grep_match(gp, line, start, end);
}

logline_window::iterator
logline_window::begin()
{
    if (this->lw_start_line < 0_vl) {
        return this->end();
    }

    return {this->lw_source, this->lw_start_line};
}

logline_window::iterator
logline_window::end()
{
    return {this->lw_source, this->lw_end_line};
}

logline_window::logmsg_info::logmsg_info(logfile_sub_source& lss, vis_line_t vl)
    : li_source(lss), li_line(vl)
{
    if (this->li_line < vis_line_t(this->li_source.text_line_count())) {
        while (true) {
            auto pair_opt = this->li_source.find_line_with_file(vl);

            if (!pair_opt) {
                break;
            }

            auto line_pair = pair_opt.value();
            if (line_pair.second->is_message()) {
                this->li_file = line_pair.first.get();
                this->li_logline = line_pair.second;
                break;
            } else {
                --vl;
            }
        }
    }
}

void
logline_window::logmsg_info::next_msg()
{
    this->li_file = nullptr;
    this->li_logline = logfile::iterator{};
    this->li_string_attrs.clear();
    this->li_line_values.clear();
    ++this->li_line;
    while (this->li_line < vis_line_t(this->li_source.text_line_count())) {
        auto pair_opt = this->li_source.find_line_with_file(this->li_line);

        if (!pair_opt) {
            break;
        }

        auto line_pair = pair_opt.value();
        if (line_pair.second->is_message()) {
            this->li_file = line_pair.first.get();
            this->li_logline = line_pair.second;
            break;
        } else {
            ++this->li_line;
        }
    }
}

void
logline_window::logmsg_info::load_msg() const
{
    if (!this->li_string_attrs.empty()) {
        return;
    }

    auto format = this->li_file->get_format();
    this->li_file->read_full_message(this->li_logline,
                                     this->li_line_values.lvv_sbr);
    if (this->li_line_values.lvv_sbr.get_metadata().m_has_ansi) {
        auto* writable_data = this->li_line_values.lvv_sbr.get_writable_data();
        auto str
            = std::string{writable_data, this->li_line_values.lvv_sbr.length()};
        scrub_ansi_string(str, &this->li_string_attrs);
        this->li_line_values.lvv_sbr.get_metadata().m_has_ansi = false;
    }
    format->annotate(std::distance(this->li_file->cbegin(), this->li_logline),
                     this->li_string_attrs,
                     this->li_line_values,
                     false);
}

std::string
logline_window::logmsg_info::to_string(const struct line_range& lr) const
{
    this->load_msg();

    return this->li_line_values.lvv_sbr
        .to_string_fragment(lr.lr_start, lr.length())
        .to_string();
}

logline_window::iterator&
logline_window::iterator::operator++()
{
    this->i_info.next_msg();

    return *this;
}

static std::vector<breadcrumb::possibility>
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
logfile_sub_source::text_crumbs_for_line(int line,
                                         std::vector<breadcrumb::crumb>& crumbs)
{
    text_sub_source::text_crumbs_for_line(line, crumbs);

    if (this->lss_filtered_index.empty()) {
        return;
    }

    auto line_pair_opt = this->find_line_with_file(vis_line_t(line));
    if (!line_pair_opt) {
        return;
    }
    auto line_pair = line_pair_opt.value();
    auto& lf = line_pair.first;
    auto format = lf->get_format();
    char ts[64];

    sql_strftime(ts, sizeof(ts), line_pair.second->get_timeval(), 'T');

    crumbs.emplace_back(
        std::string(ts),
        timestamp_poss,
        [ec = this->lss_exec_context](const auto& ts) {
            ec->execute(fmt::format(FMT_STRING(":goto {}"),
                                    ts.template get<std::string>()));
        });
    crumbs.back().c_expected_input
        = breadcrumb::crumb::expected_input_t::anything;
    crumbs.back().c_search_placeholder = "(Enter an absolute or relative time)";

    auto format_name = format->get_name().to_string();
    crumbs.emplace_back(
        format_name,
        attr_line_t().append(format_name),
        [this]() -> std::vector<breadcrumb::possibility> {
            return this->lss_files
                | lnav::itertools::filter_in([](const auto& file_data) {
                       return file_data->is_visible();
                   })
                | lnav::itertools::map(&logfile_data::get_file_ptr)
                | lnav::itertools::map(&logfile::get_format_name)
                | lnav::itertools::unique()
                | lnav::itertools::map([](const auto& elem) {
                       return breadcrumb::possibility{
                           elem.to_string(),
                       };
                   });
        },
        [ec = this->lss_exec_context](const auto& format_name) {
            static const std::string MOVE_STMT = R"(;UPDATE lnav_views
     SET top = ifnull((SELECT log_line FROM all_logs WHERE log_format = $format_name LIMIT 1), top)
     WHERE name = 'log'
)";

            ec->execute_with(
                MOVE_STMT,
                std::make_pair("format_name",
                               format_name.template get<std::string>()));
        });

    auto msg_start_iter = lf->message_start(line_pair.second);
    auto file_line_number = std::distance(lf->begin(), msg_start_iter);
    crumbs.emplace_back(
        lf->get_unique_path(),
        attr_line_t()
            .append(lf->get_unique_path())
            .appendf(FMT_STRING("[{:L}]"), file_line_number),
        [this]() -> std::vector<breadcrumb::possibility> {
            return this->lss_files
                | lnav::itertools::filter_in([](const auto& file_data) {
                       return file_data->is_visible();
                   })
                | lnav::itertools::map([](const auto& file_data) {
                       return breadcrumb::possibility{
                           file_data->get_file_ptr()->get_unique_path(),
                           attr_line_t(
                               file_data->get_file_ptr()->get_unique_path()),
                       };
                   });
        },
        [ec = this->lss_exec_context](const auto& uniq_path) {
            static const std::string MOVE_STMT = R"(;UPDATE lnav_views
     SET top = ifnull((SELECT log_line FROM all_logs WHERE log_unique_path = $uniq_path LIMIT 1), top)
     WHERE name = 'log'
)";

            ec->execute_with(
                MOVE_STMT,
                std::make_pair("uniq_path",
                               uniq_path.template get<std::string>()));
        });

    logline_value_vector values;
    auto& sbr = values.lvv_sbr;

    lf->read_full_message(msg_start_iter, sbr);
    sbr.erase_ansi();
    attr_line_t al(to_string(sbr));
    format->annotate(file_line_number, al.get_attrs(), values);

    auto opid_opt = get_string_attr(al.get_attrs(), logline::L_OPID);
    if (opid_opt && !opid_opt.value().saw_string_attr->sa_range.empty()) {
        const auto& opid_range = opid_opt.value().saw_string_attr->sa_range;
        const auto opid_str
            = sbr.to_string_fragment(opid_range.lr_start, opid_range.length())
                  .to_string();
        crumbs.emplace_back(
            opid_str,
            attr_line_t().append(lnav::roles::identifier(opid_str)),
            [this]() -> std::vector<breadcrumb::possibility> {
                std::vector<breadcrumb::possibility> retval;

                for (const auto& file_data : this->lss_files) {
                    if (file_data->get_file_ptr() == nullptr) {
                        continue;
                    }
                    safe::ReadAccess<logfile::safe_opid_map> r_opid_map(
                        file_data->get_file_ptr()->get_opids());

                    for (const auto& pair : *r_opid_map) {
                        retval.emplace_back(pair.first.to_string());
                    }
                }

                return retval;
            },
            [ec = this->lss_exec_context](const auto& opid) {
                static const std::string MOVE_STMT = R"(;UPDATE lnav_views
                         SET top = ifnull((SELECT log_line FROM all_logs WHERE log_opid = $opid LIMIT 1), top)
                         WHERE name = 'log'
                    )";

                ec->execute_with(
                    MOVE_STMT,
                    std::make_pair("opid", opid.template get<std::string>()));
            });
    }

    auto sf = sbr.to_string_fragment();
    auto body_opt = get_string_attr(al.get_attrs(), SA_BODY);
    auto sf_lines = sf.split_lines();
    auto msg_line_number = std::distance(msg_start_iter, line_pair.second);
    auto line_from_top = line - msg_line_number;
    if (sf_lines.size() > 1 && body_opt) {
        if (this->lss_token_meta_line != file_line_number
            || this->lss_token_meta_size != sf.length())
        {
            this->lss_token_meta = lnav::document::discover_structure(
                al, body_opt.value().saw_string_attr->sa_range);
            this->lss_token_meta_line = file_line_number;
            this->lss_token_meta_size = sf.length();
        }

        const auto initial_size = crumbs.size();
        file_off_t line_offset = 0;
        file_off_t line_end_offset = sf.length();
        size_t line_number = 0;

        for (const auto& sf_line : sf_lines) {
            if (line_number >= msg_line_number) {
                line_end_offset = line_offset + sf_line.length();
                break;
            }
            line_number += 1;
            line_offset += sf_line.length();
        }

        this->lss_token_meta.m_sections_tree.visit_overlapping(
            line_offset,
            line_end_offset,
            [this,
             initial_size,
             meta = &this->lss_token_meta,
             &crumbs,
             line_from_top](const auto& iv) {
                auto path = crumbs | lnav::itertools::skip(initial_size)
                    | lnav::itertools::map(&breadcrumb::crumb::c_key)
                    | lnav::itertools::append(iv.value);
                auto curr_node = lnav::document::hier_node::lookup_path(
                    meta->m_sections_root.get(), path);

                crumbs.template emplace_back(
                    iv.value,
                    [meta, path]() { return meta->possibility_provider(path); },
                    [this, curr_node, path, line_from_top](const auto& key) {
                        if (!curr_node) {
                            return;
                        }
                        auto* parent_node = curr_node.value()->hn_parent;
                        if (parent_node == nullptr) {
                            return;
                        }
                        key.template match(
                            [parent_node](const std::string& str) {
                                return parent_node->find_line_number(str);
                            },
                            [parent_node](size_t index) {
                                return parent_node->find_line_number(index);
                            })
                            | [this, line_from_top](auto line_number) {
                                  this->tss_view->set_top(
                                      vis_line_t(line_from_top + line_number));
                              };
                    });
                if (curr_node && !curr_node.value()->hn_parent->is_named_only())
                {
                    auto node = lnav::document::hier_node::lookup_path(
                        meta->m_sections_root.get(), path);

                    crumbs.back().c_expected_input
                        = curr_node.value()
                              ->hn_parent->hn_named_children.empty()
                        ? breadcrumb::crumb::expected_input_t::index
                        : breadcrumb::crumb::expected_input_t::index_or_exact;
                    crumbs.back().with_possible_range(
                        node | lnav::itertools::map([](const auto hn) {
                            return hn->hn_parent->hn_children.size();
                        })
                        | lnav::itertools::unwrap_or(size_t{0}));
                }
            });

        auto path = crumbs | lnav::itertools::skip(initial_size)
            | lnav::itertools::map(&breadcrumb::crumb::c_key);
        auto node = lnav::document::hier_node::lookup_path(
            this->lss_token_meta.m_sections_root.get(), path);

        if (node && !node.value()->hn_children.empty()) {
            auto poss_provider = [curr_node = node.value()]() {
                std::vector<breadcrumb::possibility> retval;
                for (const auto& child : curr_node->hn_named_children) {
                    retval.template emplace_back(child.first);
                }
                return retval;
            };
            auto path_performer
                = [this, curr_node = node.value(), line_from_top](
                      const breadcrumb::crumb::key_t& value) {
                      value.template match(
                          [curr_node](const std::string& str) {
                              return curr_node->find_line_number(str);
                          },
                          [curr_node](size_t index) {
                              return curr_node->find_line_number(index);
                          })
                          | [this, line_from_top](size_t line_number) {
                                this->tss_view->set_top(
                                    vis_line_t(line_from_top + line_number));
                            };
                  };
            crumbs.emplace_back("", "\u22ef", poss_provider, path_performer);
            crumbs.back().c_expected_input
                = node.value()->hn_named_children.empty()
                ? breadcrumb::crumb::expected_input_t::index
                : breadcrumb::crumb::expected_input_t::index_or_exact;
        }
    }
}

void
logfile_sub_source::quiesce()
{
    for (auto& ld : this->lss_files) {
        auto* lf = ld->get_file_ptr();

        if (lf == nullptr) {
            continue;
        }

        lf->quiesce();
    }
}

bookmark_metadata&
logfile_sub_source::get_bookmark_metadata(content_line_t cl)
{
    auto line_pair = this->find_line_with_file(cl).value();
    auto line_number = static_cast<uint32_t>(
        std::distance(line_pair.first->begin(), line_pair.second));

    return line_pair.first->get_bookmark_metadata()[line_number];
}

nonstd::optional<bookmark_metadata*>
logfile_sub_source::find_bookmark_metadata(content_line_t cl)
{
    auto line_pair = this->find_line_with_file(cl).value();
    auto line_number = static_cast<uint32_t>(
        std::distance(line_pair.first->begin(), line_pair.second));

    auto& bm = line_pair.first->get_bookmark_metadata();
    auto bm_iter = bm.find(line_number);
    if (bm_iter == bm.end()) {
        return nonstd::nullopt;
    }

    return &bm_iter->second;
}

void
logfile_sub_source::erase_bookmark_metadata(content_line_t cl)
{
    auto line_pair = this->find_line_with_file(cl).value();
    auto line_number = static_cast<uint32_t>(
        std::distance(line_pair.first->begin(), line_pair.second));

    auto& bm = line_pair.first->get_bookmark_metadata();
    auto bm_iter = bm.find(line_number);
    if (bm_iter != bm.end()) {
        bm.erase(bm_iter);
    }
}

void
logfile_sub_source::clear_bookmark_metadata()
{
    for (auto& ld : *this) {
        if (ld->get_file_ptr() == nullptr) {
            continue;
        }

        ld->get_file_ptr()->get_bookmark_metadata().clear();
    }
}
