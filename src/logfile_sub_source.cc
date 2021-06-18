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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include <future>
#include <algorithm>
#include <sqlite3.h>

#include "base/humanize.time.hh"
#include "base/string_util.hh"
#include "k_merge_tree.h"
#include "lnav_util.hh"
#include "log_accel.hh"
#include "relative_time.hh"
#include "logfile_sub_source.hh"
#include "command_executor.hh"
#include "ansi_scrubber.hh"
#include "sql_util.hh"
#include "yajlpp/yajlpp.hh"

using namespace std;

bookmark_type_t logfile_sub_source::BM_ERRORS("error");
bookmark_type_t logfile_sub_source::BM_WARNINGS("warning");
bookmark_type_t logfile_sub_source::BM_FILES("");

static int pretty_sql_callback(exec_context &ec, sqlite3_stmt *stmt)
{
    if (!sqlite3_stmt_busy(stmt)) {
        return 0;
    }

    int ncols = sqlite3_column_count(stmt);

    for (int lpc = 0; lpc < ncols; lpc++) {
        if (!ec.ec_accumulator.empty()) {
            ec.ec_accumulator.append(", ");
        }

        const char *res = (const char *)sqlite3_column_text(stmt, lpc);
        if (res == nullptr) {
            continue;
        }

        ec.ec_accumulator.append(res);
    }

    return 0;
}

static future<string> pretty_pipe_callback(exec_context &ec,
                                           const string &cmdline,
                                           auto_fd &fd)
{
    auto retval = std::async(std::launch::async, [&]() {
        char buffer[1024];
        ostringstream ss;
        ssize_t rc;

        while ((rc = read(fd, buffer, sizeof(buffer))) > 0) {
            ss.write(buffer, rc);
        }

        string retval = ss.str();

        if (endswith(retval, "\n")) {
            retval.resize(retval.length() - 1);
        }

        return retval;
    });

    return retval;
}

logfile_sub_source::logfile_sub_source()
    : text_sub_source(1),
      lss_meta_grepper(*this),
      lss_location_history(*this)
{
    this->tss_supports_filtering = true;
    this->clear_line_size_cache();
    this->clear_min_max_log_times();
}

shared_ptr<logfile> logfile_sub_source::find(const char *fn,
                                  content_line_t &line_base)
{
    iterator iter;
    shared_ptr<logfile> retval = nullptr;

    line_base = content_line_t(0);
    for (iter = this->lss_files.begin();
         iter != this->lss_files.end() && retval == nullptr;
         iter++) {
        auto &ld = *(*iter);
        auto lf = ld.get_file_ptr();

        if (lf == nullptr) {
            continue;
        }
        if (strcmp(lf->get_filename().c_str(), fn) == 0) {
            retval = ld.get_file();
        }
        else {
            line_base += content_line_t(MAX_LINES_PER_FILE);
        }
    }

    return retval;
}

vis_line_t logfile_sub_source::find_from_time(const struct timeval &start) const
{
    vis_line_t retval(-1);

    auto lb = lower_bound(this->lss_filtered_index.begin(),
                          this->lss_filtered_index.end(),
                          start,
                          filtered_logline_cmp(*this));
    if (lb != this->lss_filtered_index.end()) {
        retval = vis_line_t(lb - this->lss_filtered_index.begin());
    }

    return retval;
}

void logfile_sub_source::text_value_for_line(textview_curses &tc,
                                             int row,
                                             string &value_out,
                                             line_flags_t flags)
{
    content_line_t line(0);

    require(row >= 0);
    require((size_t)row < this->lss_filtered_index.size());

    line = this->at(vis_line_t(row));

    if (flags & RF_RAW) {
        shared_ptr<logfile> lf = this->find(line);
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
    } else {
        this->lss_token_value =
            this->lss_token_file->read_line(this->lss_token_line).map([](auto sbr) {
                return to_string(sbr);
            }).unwrapOr({});
    }
    this->lss_token_shift_start = 0;
    this->lss_token_shift_size = 0;

    auto format = this->lss_token_file->get_format();

    value_out = this->lss_token_value;
    if (this->lss_flags & F_SCRUB) {
        format->scrub(value_out);
    }

    shared_buffer_ref sbr;

    sbr.share(this->lss_share_manager,
              (char *)this->lss_token_value.c_str(), this->lss_token_value.size());
    if (this->lss_token_line->is_continued()) {
        this->lss_token_attrs.emplace_back(
            line_range{0, (int) this->lss_token_value.length()},
            &SA_BODY);
    } else {
        format->annotate(line, sbr, this->lss_token_attrs, this->lss_token_values);
    }
    if (this->lss_token_line->get_sub_offset() != 0) {
        this->lss_token_attrs.clear();
    }
    if (flags & RF_REWRITE) {
        exec_context ec(&this->lss_token_values, pretty_sql_callback, pretty_pipe_callback);
        string rewritten_line;

        ec.with_perms(exec_context::perm_t::READ_ONLY);
        ec.ec_local_vars.push(map<string, string>());
        ec.ec_top_line = vis_line_t(row);
        add_ansi_vars(ec.ec_global_vars);
        add_global_vars(ec);
        format->rewrite(ec, sbr, this->lss_token_attrs, rewritten_line);
        this->lss_token_value.assign(rewritten_line);
        value_out = this->lss_token_value;
    }

    if ((this->lss_token_file->is_time_adjusted() ||
         format->lf_timestamp_flags & ETF_MACHINE_ORIENTED) &&
        format->lf_date_time.dts_fmt_lock != -1) {
        auto time_attr = find_string_attr(
            this->lss_token_attrs, &logline::L_TIMESTAMP);
        if (time_attr != this->lss_token_attrs.end()) {
            const struct line_range time_range = time_attr->sa_range;
            struct timeval adjusted_time;
            struct exttm adjusted_tm;
            char buffer[128];
            const char *fmt;
            ssize_t len;

            if (format->lf_timestamp_flags & ETF_MACHINE_ORIENTED) {
                format->lf_date_time.convert_to_timeval(
                    &this->lss_token_value.c_str()[time_range.lr_start],
                    time_range.length(),
                    format->get_timestamp_formats(),
                    adjusted_time);
                fmt = "%Y-%m-%d %H:%M:%S.%f";
                gmtime_r(&adjusted_time.tv_sec, &adjusted_tm.et_tm);
                adjusted_tm.et_nsec = adjusted_time.tv_usec * 1000;
                len = ftime_fmt(buffer, sizeof(buffer), fmt, adjusted_tm);
            } else {
                adjusted_time = this->lss_token_line->get_timeval();
                gmtime_r(&adjusted_time.tv_sec, &adjusted_tm.et_tm);
                adjusted_tm.et_nsec = adjusted_time.tv_usec * 1000;
                len = format->lf_date_time.ftime(buffer, sizeof(buffer),
                                                 adjusted_tm);
            }

            value_out.replace(time_range.lr_start,
                              time_range.length(),
                              buffer,
                              len);
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

        vis_line_t prev_mark =
            tc.get_bookmarks()[&textview_curses::BM_USER].prev(vis_line_t(row));
        vis_line_t next_mark =
            tc.get_bookmarks()[&textview_curses::BM_USER].next(vis_line_t(row));
        if (prev_mark == -1 && next_mark != -1) {
            auto next_line = this->find_line(this->at(next_mark));

            diff_tv = curr_tv - next_line->get_timeval();
        } else {
            if (prev_mark == -1_vl) {
                prev_mark = 0_vl;
            }

            auto first_line = this->find_line(this->at(prev_mark));
            auto start_tv = first_line->get_timeval();
            diff_tv = curr_tv - start_tv;
        }

        auto relstr = humanize::time::duration::from_tv(diff_tv).to_string();
        value_out = fmt::format(FMT_STRING("{: >12}|{}"), relstr, value_out);
    }
    this->lss_in_value_for_line = false;
}

void logfile_sub_source::text_attrs_for_line(textview_curses &lv,
                                             int row,
                                             string_attrs_t &value_out)
{
    view_colors &     vc        = view_colors::singleton();
    logline *         next_line = nullptr;
    struct line_range lr;
    int time_offset_end = 0;
    int attrs           = 0;

    value_out = this->lss_token_attrs;

    attrs = vc.vc_level_attrs[this->lss_token_line->get_msg_level()].first;

    if ((row + 1) < (int)this->lss_filtered_index.size()) {
        next_line = this->find_line(this->at(vis_line_t(row + 1)));
    }

    if (next_line != nullptr &&
        (day_num(next_line->get_time()) >
         day_num(this->lss_token_line->get_time()))) {
        attrs |= A_UNDERLINE;
    }

    const std::vector<logline_value> &line_values = this->lss_token_values;

    lr.lr_start = 0;
    lr.lr_end = this->lss_token_value.length();
    value_out.emplace_back(lr, &SA_ORIGINAL_LINE);

    lr.lr_start = time_offset_end;
    lr.lr_end   = -1;

    value_out.emplace_back(lr, &view_curses::VC_STYLE, attrs);

    if (this->lss_token_line->get_msg_level() == log_level_t::LEVEL_INVALID) {
        for (auto& token_attr : this->lss_token_attrs) {
            if (token_attr.sa_type != &SA_INVALID) {
                continue;
            }


            value_out.emplace_back(token_attr.sa_range,
                                   &view_curses::VC_ROLE,
                                   view_colors::VCR_INVALID_MSG);
        }
    }

    for (const auto &line_value : line_values) {
        if ((!(this->lss_token_flags & RF_FULL) &&
            line_value.lv_sub_offset != this->lss_token_line->get_sub_offset()) ||
            !line_value.lv_origin.is_valid()) {
            continue;
        }

        if (line_value.lv_meta.is_hidden()) {
            value_out.emplace_back(
                line_value.lv_origin, &SA_HIDDEN);
        }

        if (!line_value.lv_meta.lvm_identifier || !line_value.lv_origin.is_valid()) {
            continue;
        }

        line_range ident_range = line_value.lv_origin;
        if (this->lss_token_flags & RF_FULL) {
            ident_range = line_value.origin_in_full_msg(
                this->lss_token_value.c_str(), this->lss_token_value.length());
        }

        value_out.emplace_back(ident_range,
                               &view_curses::VC_ROLE,
                               view_colors::VCR_IDENTIFIER);
    }

    if (this->lss_token_shift_size) {
        shift_string_attrs(value_out, this->lss_token_shift_start + 1,
                           this->lss_token_shift_size);
    }

    shift_string_attrs(value_out, 0, 1);

    lr.lr_start = 0;
    lr.lr_end = 1;
    {
        auto &bm = lv.get_bookmarks();
        const auto &bv = bm[&BM_FILES];
        bool is_first_for_file = binary_search(
            bv.begin(), bv.end(), vis_line_t(row));
        bool is_last_for_file = binary_search(
            bv.begin(), bv.end(), vis_line_t(row + 1));
        chtype graph = ACS_VLINE;
        if (is_first_for_file) {
            if (is_last_for_file) {
                graph = ACS_HLINE;
            }
            else {
                graph = ACS_ULCORNER;
            }
        }
        else if (is_last_for_file) {
            graph = ACS_LLCORNER;
        }
        value_out.push_back(
            string_attr(lr, &view_curses::VC_GRAPHIC, graph));

        if (!(this->lss_token_flags & RF_FULL)) {
            bookmark_vector<vis_line_t> &bv_search = bm[&textview_curses::BM_SEARCH];

            if (binary_search(::begin(bv_search), ::end(bv_search),
                              vis_line_t(row))) {
                lr.lr_start = 0;
                lr.lr_end = 1;
                value_out.emplace_back(lr, &view_curses::VC_STYLE, A_REVERSE);
            }
        }
    }

    value_out.emplace_back(lr, &view_curses::VC_STYLE, vc.attrs_for_ident(
        this->lss_token_file->get_filename()));

    if (this->lss_flags & F_FILENAME || this->lss_flags & F_BASENAME) {
        size_t file_offset_end = (this->lss_flags & F_FILENAME) ?
                                    this->lss_filename_width :
                                    this->lss_basename_width ;

        shift_string_attrs(value_out, 0, file_offset_end);

        lr.lr_start = 0;
        lr.lr_end   = file_offset_end + 1;
        value_out.emplace_back(lr, &view_curses::VC_STYLE, vc.attrs_for_ident(
            this->lss_token_file->get_filename()));
    }

    if (this->lss_flags & F_TIME_OFFSET) {
        time_offset_end = 13;
        lr.lr_start     = 0;
        lr.lr_end       = time_offset_end;

        shift_string_attrs(value_out, 0, time_offset_end);

        value_out.emplace_back(lr,
                               &view_curses::VC_ROLE,
                               view_colors::VCR_OFFSET_TIME);
        value_out.emplace_back(line_range(12, 13),
            &view_curses::VC_GRAPHIC, ACS_VLINE);

        view_colors::role_t bar_role = view_colors::VCR_NONE;

        switch (this->get_line_accel_direction(vis_line_t(row))) {
        case log_accel::A_STEADY:
            break;
        case log_accel::A_DECEL:
            bar_role = view_colors::VCR_DIFF_DELETE;
            break;
        case log_accel::A_ACCEL:
            bar_role = view_colors::VCR_DIFF_ADD;
            break;
        }
        if (bar_role != view_colors::VCR_NONE) {
            value_out.emplace_back(
                line_range(12, 13), &view_curses::VC_ROLE, bar_role);
        }
    }

    lr.lr_start = 0;
    lr.lr_end   = -1;
    value_out.emplace_back(lr, &logline::L_FILE, this->lss_token_file.get());
    value_out.emplace_back(lr, &SA_FORMAT,
                           this->lss_token_file->get_format()->get_name());

    {
        const auto &bv = lv.get_bookmarks()[&textview_curses::BM_META];
        bookmark_vector<vis_line_t>::const_iterator bv_iter;

        bv_iter = lower_bound(bv.begin(), bv.end(), vis_line_t(row + 1));
        if (bv_iter != bv.begin()) {
            --bv_iter;
            content_line_t part_start_line = this->at(*bv_iter);
            std::map<content_line_t, bookmark_metadata>::iterator bm_iter;

            if ((bm_iter = this->lss_user_mark_metadata.find(part_start_line))
                != this->lss_user_mark_metadata.end() &&
                !bm_iter->second.bm_name.empty()) {
                lr.lr_start = 0;
                lr.lr_end   = -1;
                value_out.emplace_back(lr, &logline::L_PARTITION, &bm_iter->second);
            }
        }

        auto bm_iter = this->lss_user_mark_metadata.find(this->at(vis_line_t(row)));

        if (bm_iter != this->lss_user_mark_metadata.end()) {
            lr.lr_start = 0;
            lr.lr_end = -1;
            value_out.emplace_back(lr, &logline::L_META, &bm_iter->second);
        }
    }

    if (this->lss_token_file->is_time_adjusted()) {
        struct line_range time_range = find_string_attr_range(
            value_out, &logline::L_TIMESTAMP);

        if (time_range.lr_end != -1) {
            value_out.emplace_back(time_range, &view_curses::VC_ROLE,
                                   view_colors::VCR_ADJUSTED_TIME);
        }
    }
    else if ((((this->lss_token_line->get_time() / (5 * 60)) % 2) == 0) &&
             !this->lss_token_line->is_continued()) {
        struct line_range time_range = find_string_attr_range(
            value_out, &logline::L_TIMESTAMP);

        if (time_range.lr_end != -1) {
            value_out.emplace_back(time_range, &view_curses::VC_ROLE,
                                   view_colors::VCR_ALT_ROW);
        }
    }

    if (this->lss_token_line->is_time_skewed()) {
        struct line_range time_range = find_string_attr_range(
            value_out, &logline::L_TIMESTAMP);

        if (time_range.lr_end != -1) {
            value_out.emplace_back(time_range, &view_curses::VC_ROLE,
                                   view_colors::VCR_SKEWED_TIME);
        }
    }

    if (!this->lss_token_line->is_continued()) {
        if (this->lss_preview_filter_stmt != nullptr) {
            int color;
            auto eval_res = this->eval_sql_filter(this->lss_preview_filter_stmt.in(),
                                                  this->lss_token_file_data,
                                                  this->lss_token_line);
            if (eval_res.isErr()) {
                color = COLOR_YELLOW;
                value_out.emplace_back(line_range{0, -1},
                                       &SA_ERROR,
                                       eval_res.unwrapErr());
            } else {
                auto matched = eval_res.unwrap();

                if (matched) {
                    color = COLOR_GREEN;
                } else {
                    color = COLOR_RED;
                    value_out.emplace_back(line_range{0, 1}, &view_curses::VC_STYLE,
                                           A_BLINK);
                }
            }
            value_out.emplace_back(line_range{0, 1}, &view_curses::VC_BACKGROUND, color);
        }

        auto sql_filter_opt = this->get_sql_filter();
        if (sql_filter_opt) {
            auto sf = (sql_filter *) sql_filter_opt.value().get();
            int color;
            auto eval_res = this->eval_sql_filter(sf->sf_filter_stmt.in(),
                                                  this->lss_token_file_data,
                                                  this->lss_token_line);
            if (eval_res.isErr()) {
                auto msg = fmt::format(
                    "filter expression evaluation failed with -- {}",
                    eval_res.unwrapErr());
                color = COLOR_YELLOW;
                value_out.emplace_back(line_range{0, -1},
                                       &SA_ERROR,
                                       msg);
                value_out.emplace_back(line_range{0, 1}, &view_curses::VC_BACKGROUND, color);
            }
        }
    }
}

logfile_sub_source::rebuild_result logfile_sub_source::rebuild_index(nonstd::optional<ui_clock::time_point> deadline)
{
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

    for (iter = this->lss_files.begin();
         iter != this->lss_files.end();
         iter++) {
        auto &ld = *(*iter);
        auto lf = ld.get_file_ptr();

        if (lf == nullptr) {
            if (ld.ld_lines_indexed > 0) {
                log_debug("%d: file closed, doing full rebuild",
                          ld.ld_file_index);
                force  = true;
                retval = rebuild_result::rr_full_rebuild;
            }
        }
        else {
            if (!this->tss_view->is_paused()) {
                switch (lf->rebuild_index(deadline)) {
                    case logfile::rebuild_result_t::NO_NEW_LINES:
                        // No changes
                        break;
                    case logfile::rebuild_result_t::NEW_LINES:
                        if (retval == rebuild_result::rr_no_change) {
                            retval = rebuild_result::rr_appended_lines;
                        }
                        if (!this->lss_index.empty() &&
                            lf->size() > ld.ld_lines_indexed) {
                            logline &new_file_line = (*lf)[ld.ld_lines_indexed];
                            content_line_t cl = this->lss_index.back();
                            logline *last_indexed_line = this->find_line(cl);

                            // If there are new lines that are older than what we
                            // have in the index, we need to resort.
                            if (last_indexed_line == nullptr ||
                                new_file_line <
                                last_indexed_line->get_timeval()) {
                                log_debug("%s:%ld: found older lines, full "
                                          "rebuild: %p  %lld < %lld",
                                          lf->get_filename().c_str(),
                                          ld.ld_lines_indexed,
                                          last_indexed_line,
                                          new_file_line.get_time_in_millis(),
                                          last_indexed_line == nullptr ?
                                          (uint64_t) -1 :
                                          last_indexed_line->get_time_in_millis());
                                if (retval <= rebuild_result::rr_partial_rebuild) {
                                    retval = rebuild_result::rr_partial_rebuild;
                                    if (!lowest_tv) {
                                        lowest_tv = new_file_line.get_timeval();
                                    } else if (new_file_line.get_timeval() <
                                               lowest_tv.value()) {
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
                        break;
                }
            }
            file_count += 1;
            total_lines += lf->size();
        }
    }

    if (this->lss_index.reserve(total_lines)) {
        force = true;
        retval = rebuild_result::rr_full_rebuild;
    }

    auto& vis_bm = this->tss_view->get_bookmarks();

    if (force) {
        full_sort = true;
        for (iter = this->lss_files.begin();
             iter != this->lss_files.end();
             iter++) {
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
        for (iter = this->lss_files.begin();
             iter != this->lss_files.end();
             iter++) {
            logfile_data &ld = *(*iter);
            auto lf = ld.get_file_ptr();

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
            ld.ld_lines_indexed = std::distance(
                lf->cbegin(), line_iter.value_or(lf->cend()));
            remaining += lf->size() - ld.ld_lines_indexed;
        }

        auto row_iter = lower_bound(this->lss_index.begin(),
                                    this->lss_index.end(),
                                    *lowest_tv,
                                    logline_cmp(*this));
        this->lss_index.shrink_to(std::distance(
            this->lss_index.begin(), row_iter));
        log_debug("new index size %ld/%ld; remain %ld",
                  this->lss_index.ba_size,
                  this->lss_index.ba_capacity,
                  remaining);
        auto filt_row_iter = lower_bound(this->lss_filtered_index.begin(),
                                    this->lss_filtered_index.end(),
                                    *lowest_tv,
                                    filtered_logline_cmp(*this));
        this->lss_filtered_index.resize(std::distance(
            this->lss_filtered_index.begin(), filt_row_iter));
        search_start = vis_line_t(this->lss_filtered_index.size());

        auto bm_range = vis_bm[&textview_curses::BM_USER_EXPR].equal_range(
            search_start, -1_vl);
        auto bm_new_size = std::distance(vis_bm[&textview_curses::BM_USER_EXPR]
            .begin(), bm_range.first);
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
            auto lf = ld->get_file_ptr();

            if (lf == nullptr) {
                continue;
            }
            this->lss_longest_line = std::max(
                this->lss_longest_line, lf->get_longest_line_length());
            this->lss_basename_width = std::max(
                this->lss_basename_width, lf->get_unique_path().size());
            this->lss_filename_width = std::max(
                this->lss_filename_width, lf->get_filename().size());
        }

        if (full_sort) {
            for (auto& ld : this->lss_files) {
                auto lf = ld->get_file_ptr();

                if (lf == nullptr) {
                    continue;
                }

                for (size_t line_index = 0; line_index < lf->size(); line_index++) {
                    content_line_t con_line(ld->ld_file_index * MAX_LINES_PER_FILE +
                                            line_index);

                    this->lss_index.push_back(con_line);
                }
            }

            // XXX get rid of this full sort on the initial run, it's not
            // needed unless the file is not in time-order
            sort(this->lss_index.begin(), this->lss_index.end(), line_cmper);
        } else {
            kmerge_tree_c<logline, logfile_data, logfile::iterator> merge(
                file_count);

            for (iter = this->lss_files.begin();
                 iter != this->lss_files.end();
                 iter++) {
                logfile_data *ld = iter->get();
                auto lf = ld->get_file_ptr();
                if (lf == nullptr) {
                    continue;
                }

                merge.add(ld,
                          lf->begin() + ld->ld_lines_indexed,
                          lf->end());
                index_size += lf->size();
            }

            merge.execute();
            for (;;) {
                logfile::iterator lf_iter;
                logfile_data *ld;

                if (!merge.get_top(ld, lf_iter)) {
                    break;
                }

                int file_index = ld->ld_file_index;
                int line_index = lf_iter - ld->get_file_ptr()->begin();

                content_line_t con_line(file_index * MAX_LINES_PER_FILE +
                                        line_index);

                this->lss_index.push_back(con_line);

                merge.next();
            }
        }

        for (iter = this->lss_files.begin();
             iter != this->lss_files.end();
             iter++) {
            auto lf = (*iter)->get_file_ptr();

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
             index_index++) {
            content_line_t cl = (content_line_t) this->lss_index[index_index];
            uint64_t line_number;
            auto ld = this->find_data(cl, line_number);

            if (!(*ld)->is_visible()) {
                continue;
            }

            auto lf = (*ld)->get_file_ptr();
            auto line_iter = lf->begin() + line_number;

            if (line_iter->is_ignored()) {
                continue;
            }

            if (!this->tss_apply_filters ||
                (!(*ld)->ld_filter_state.excluded(filter_in_mask, filter_out_mask,
                                                  line_number) &&
                 this->check_extra_filters(ld, line_iter))) {
                auto eval_res = this->eval_sql_filter(this->lss_marker_stmt.in(),
                                                      ld, line_iter);
                if (eval_res.isErr()) {
                    line_iter->set_expr_mark(false);
                } else {
                    auto matched = eval_res.unwrap();

                    if (matched) {
                        line_iter->set_expr_mark(true);
                        vis_bm[&textview_curses::BM_USER_EXPR]
                            .insert_once(vis_line_t(this->lss_filtered_index.size()));
                    }
                    else {
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
            this->tss_view->redo_search();
            break;
        case rebuild_result::rr_partial_rebuild:
            log_debug("redoing search from: %d", (int) search_start);
            this->tss_view->search_new_data(search_start);
            break;
        case rebuild_result::rr_appended_lines:
            this->tss_view->search_new_data();
            break;
    }

    return retval;
}

void logfile_sub_source::text_update_marks(vis_bookmarks &bm)
{
    shared_ptr<logfile> last_file = nullptr;
    vis_line_t vl;

    bm[&BM_WARNINGS].clear();
    bm[&BM_ERRORS].clear();
    bm[&BM_FILES].clear();

    for (auto &lss_user_mark : this->lss_user_marks) {
        bm[lss_user_mark.first].clear();
    }

    for (; vl < (int)this->lss_filtered_index.size(); ++vl) {
        const content_line_t orig_cl = this->at(vl);
        content_line_t cl = orig_cl;
        shared_ptr<logfile> lf;

        lf = this->find(cl);

        for (auto &lss_user_mark : this->lss_user_marks) {
            if (binary_search(lss_user_mark.second.begin(),
                              lss_user_mark.second.end(),
                              orig_cl)) {
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

log_accel::direction_t logfile_sub_source::get_line_accel_direction(
    vis_line_t vl)
{
    log_accel la;

    while (vl >= 0) {
        logline *curr_line = this->find_line(this->at(vl));

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

void logfile_sub_source::text_filters_changed()
{
    for (auto& ld : *this) {
        auto lf = ld->get_file_ptr();

        if (lf != nullptr) {
            ld->ld_filter_state.clear_deleted_filter_state();
            lf->reobserve_from(lf->begin() + ld->ld_filter_state.get_min_count(lf->size()));
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
    for (size_t index_index = 0; index_index < this->lss_index.size(); index_index++) {
        content_line_t cl = (content_line_t) this->lss_index[index_index];
        uint64_t line_number;
        auto ld = this->find_data(cl, line_number);

        if (!(*ld)->is_visible()) {
            continue;
        }

        auto lf = (*ld)->get_file_ptr();
        auto line_iter = lf->begin() + line_number;

        if (!this->tss_apply_filters ||
            (!(*ld)->ld_filter_state.excluded(filtered_in_mask, filtered_out_mask,
                                           line_number) &&
             this->check_extra_filters(ld, line_iter))) {
            auto eval_res = this->eval_sql_filter(this->lss_marker_stmt.in(),
                                                  ld, line_iter);
            if (eval_res.isErr()) {
                line_iter->set_expr_mark(false);
            } else {
                auto matched = eval_res.unwrap();

                if (matched) {
                    line_iter->set_expr_mark(true);
                    vis_bm[&textview_curses::BM_USER_EXPR]
                        .insert_once(vis_line_t(this->lss_filtered_index.size()));
                }
                else {
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

bool logfile_sub_source::list_input_handle_key(listview_curses &lv, int ch)
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

nonstd::optional<pair<grep_proc_source<vis_line_t> *, grep_proc_sink<vis_line_t> *>>
logfile_sub_source::get_grepper()
{
    return make_pair(
        (grep_proc_source<vis_line_t> *) &this->lss_meta_grepper,
        (grep_proc_sink<vis_line_t> *) &this->lss_meta_grepper
    );
}

bool logfile_sub_source::insert_file(const shared_ptr<logfile> &lf)
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
    }
    else {
        (*existing)->set_file(lf);
    }
    this->lss_force_rebuild = true;

    return true;
}

Result<void, std::string> logfile_sub_source::set_sql_filter(std::string stmt_str, sqlite3_stmt *stmt)
{
    if (stmt != nullptr && !this->lss_filtered_index.empty()) {
        auto top_cl = this->at(0_vl);
        auto ld = this->find_data(top_cl);
        auto eval_res = this->eval_sql_filter(stmt, ld, (*ld)->get_file_ptr()->begin());

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
        auto new_filter = std::make_shared<sql_filter>(*this, stmt_str, stmt);

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

Result<void, std::string>
logfile_sub_source::set_sql_marker(std::string stmt_str, sqlite3_stmt *stmt)
{
    if (stmt != nullptr && !this->lss_filtered_index.empty()) {
        auto top_cl = this->at(0_vl);
        auto ld = this->find_data(top_cl);
        auto eval_res = this->eval_sql_filter(stmt, ld, (*ld)->get_file_ptr()->begin());

        if (eval_res.isErr()) {
            sqlite3_finalize(stmt);
            return Err(eval_res.unwrapErr());
        }
    }

    auto& vis_bm = this->tss_view->get_bookmarks();
    auto& expr_marks_bv = vis_bm[&textview_curses::BM_USER_EXPR];

    expr_marks_bv.clear();
    this->lss_marker_stmt_text = std::move(stmt_str);
    this->lss_marker_stmt = stmt;
    if (this->lss_index_delegate) {
        this->lss_index_delegate->index_start(*this);
    }
    for (auto row = 0_vl; row < this->lss_filtered_index.size(); row += 1_vl) {
        auto cl = this->at(row);
        auto ld = this->find_data(cl);
        auto ll = (*ld)->get_file()->begin() + cl;
        auto eval_res = this->eval_sql_filter(this->lss_marker_stmt.in(), ld, ll);

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
            this->lss_index_delegate->index_line(*this, (*ld)->get_file_ptr(), ll);
        }
    }
    if (this->lss_index_delegate) {
        this->lss_index_delegate->index_complete(*this);
    }

    return Ok();
}

Result<void, std::string>
logfile_sub_source::set_preview_sql_filter(sqlite3_stmt *stmt)
{
    if (stmt != nullptr && !this->lss_filtered_index.empty()) {
        auto top_cl = this->at(0_vl);
        auto ld = this->find_data(top_cl);
        auto eval_res = this->eval_sql_filter(stmt, ld, (*ld)->get_file_ptr()->begin());

        if (eval_res.isErr()) {
            sqlite3_finalize(stmt);
            return Err(eval_res.unwrapErr());
        }
    }

    this->lss_preview_filter_stmt = stmt;

    return Ok();
}

Result<bool, std::string>
logfile_sub_source::eval_sql_filter(sqlite3_stmt *stmt, iterator ld, logfile::const_iterator ll)
{
    if (stmt == nullptr) {
        return Ok(false);
    }

    auto lf = (*ld)->get_file_ptr();
    char timestamp_buffer[64];
    shared_buffer_ref sbr, raw_sbr;
    lf->read_full_message(ll, sbr);
    auto format = lf->get_format();
    string_attrs_t sa;
    vector<logline_value> values;
    format->annotate(std::distance(lf->cbegin(), ll), sbr, sa, values);

    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);

    auto count = sqlite3_bind_parameter_count(stmt);
    for (int lpc = 0; lpc < count; lpc++) {
        auto *name = sqlite3_bind_parameter_name(stmt, lpc + 1);

        if (name[0] == '$') {
            const char *env_value;

            if ((env_value = getenv(&name[1])) != nullptr) {
                sqlite3_bind_text(stmt, lpc + 1, env_value, -1, SQLITE_STATIC);
            }
            continue;
        }
        if (strcmp(name, ":log_level") == 0) {
            sqlite3_bind_text(stmt,
                              lpc + 1,
                              ll->get_level_name(), -1,
                              SQLITE_STATIC);
            continue;
        }
        if (strcmp(name, ":log_time") == 0) {
            auto len = sql_strftime(timestamp_buffer, sizeof(timestamp_buffer),
                                    ll->get_timeval(),
                                    'T');
            sqlite3_bind_text(stmt,
                              lpc + 1,
                              timestamp_buffer, len,
                              SQLITE_STATIC);
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
            const auto &bm = this->get_user_bookmark_metadata();
            auto cl = this->get_file_base_content_line(ld);
            cl += content_line_t(std::distance(lf->cbegin(), ll));
            auto bm_iter = bm.find(cl);
            if (bm_iter != bm.end() && !bm_iter->second.bm_comment.empty()) {
                const auto &meta = bm_iter->second;
                sqlite3_bind_text(stmt,
                                  lpc + 1,
                                  meta.bm_comment.c_str(),
                                  meta.bm_comment.length(),
                                  SQLITE_STATIC);
            }
            continue;
        }
        if (strcmp(name, ":log_tags") == 0) {
            const auto &bm = this->get_user_bookmark_metadata();
            auto cl = this->get_file_base_content_line(ld);
            cl += content_line_t(std::distance(lf->cbegin(), ll));
            auto bm_iter = bm.find(cl);
            if (bm_iter != bm.end() && !bm_iter->second.bm_tags.empty()) {
                const auto &meta = bm_iter->second;
                yajlpp_gen gen;

                yajl_gen_config(gen, yajl_gen_beautify, false);

                {
                    yajlpp_array arr(gen);

                    for (const auto &str : meta.bm_tags) {
                        arr.gen(str);
                    }
                }

                string_fragment sf = gen.to_string_fragment();

                sqlite3_bind_text(stmt,
                                  lpc + 1,
                                  sf.data(),
                                  sf.length(),
                                  SQLITE_TRANSIENT);
            }
            continue;
        }
        if (strcmp(name, ":log_path") == 0) {
            const auto& filename = lf->get_filename();
            sqlite3_bind_text(stmt,
                              lpc + 1,
                              filename.c_str(), filename.length(),
                              SQLITE_STATIC);
            continue;
        }
        if (strcmp(name, ":log_text") == 0) {
            sqlite3_bind_text(stmt,
                              lpc + 1,
                              sbr.get_data(), sbr.length(),
                              SQLITE_STATIC);
            continue;
        }
        if (strcmp(name, ":log_body") == 0) {
            auto iter = find_string_attr(sa, &SA_BODY);
            sqlite3_bind_text(stmt,
                              lpc + 1,
                              &(sbr.get_data()[iter->sa_range.lr_start]),
                              iter->sa_range.length(),
                              SQLITE_STATIC);
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
        for (auto& lv : values) {
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
            return Err(std::string(sqlite3_errmsg(sqlite3_db_handle(stmt))));
    }

    return Ok(true);
}

bool logfile_sub_source::check_extra_filters(iterator ld, logfile::iterator ll)
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

void log_location_history::loc_history_append(vis_line_t top)
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

nonstd::optional<vis_line_t> log_location_history::loc_history_back(vis_line_t current_top)
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

bool sql_filter::matches(const logfile &lf, logfile::const_iterator ll,
                         shared_buffer_ref &line)
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

    auto eval_res = this->sf_log_source.eval_sql_filter(this->sf_filter_stmt, ld, ll);
    if (eval_res.unwrapOr(true)) {
        return false;
    }

    return true;
}

std::string sql_filter::to_command()
{
    return fmt::format("filter-expr {}", this->lf_id);
}
