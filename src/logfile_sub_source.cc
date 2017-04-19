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

#include <algorithm>

#include "k_merge_tree.h"
#include "lnav_util.hh"
#include "log_accel.hh"
#include "relative_time.hh"
#include "logfile_sub_source.hh"

using namespace std;

bookmark_type_t logfile_sub_source::BM_ERRORS("error");
bookmark_type_t logfile_sub_source::BM_WARNINGS("warning");
bookmark_type_t logfile_sub_source::BM_FILES("");

logfile_sub_source::logfile_sub_source()
    : lss_flags(0),
      lss_token_file(NULL),
      lss_min_log_level(logline::LEVEL_UNKNOWN),
      lss_index_delegate(NULL),
      lss_longest_line(0)
{
    this->clear_line_size_cache();
    this->clear_min_max_log_times();
}

logfile_sub_source::~logfile_sub_source()
{ }

logfile *logfile_sub_source::find(const char *fn,
                                  content_line_t &line_base)
{
    iterator iter;
    logfile *retval = NULL;

    line_base = content_line_t(0);
    for (iter = this->lss_files.begin();
         iter != this->lss_files.end() && retval == NULL;
         iter++) {
        logfile_data &ld = *(*iter);
        if (ld.get_file() == NULL) {
            continue;
        }
        if (strcmp(ld.get_file()->get_filepath().c_str(), fn) == 0) {
            retval = ld.get_file();
        }
        else {
            line_base += content_line_t(MAX_LINES_PER_FILE);
        }
    }

    return retval;
}

vis_line_t logfile_sub_source::find_from_time(const struct timeval &start)
{
    vector<uint32_t>::iterator lb;
    vis_line_t retval(-1);

    lb = lower_bound(this->lss_filtered_index.begin(),
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
                                             bool raw)
{
    content_line_t line(0);

    require(row >= 0);
    require((size_t)row < this->lss_filtered_index.size());

    line = this->at(vis_line_t(row));
    this->lss_token_file   = this->find(line);
    this->lss_token_line   = this->lss_token_file->begin() + line;

    if (raw) {
        this->lss_token_file->read_line(this->lss_token_line, value_out);
        return;
    }

    this->lss_token_attrs.clear();
    this->lss_token_values.clear();
    this->lss_share_manager.invalidate_refs();
    this->lss_token_value =
        this->lss_token_file->read_line(this->lss_token_line);
    this->lss_token_shift_start = 0;
    this->lss_token_shift_size = 0;

    log_format *format = this->lss_token_file->get_format();

    value_out = this->lss_token_value;
    if (this->lss_flags & F_SCRUB) {
        format->scrub(value_out);
    }

    if (!this->lss_token_line->is_continued() ||
        this->lss_token_line->get_sub_offset() != 0) {
        shared_buffer_ref sbr;

        sbr.share(this->lss_share_manager,
                  (char *)this->lss_token_value.c_str(), this->lss_token_value.size());
        format->annotate(sbr, this->lss_token_attrs, this->lss_token_values);
        if (this->lss_token_line->get_sub_offset() != 0) {
            this->lss_token_attrs.clear();
        }

        if ((this->lss_token_file->is_time_adjusted() ||
             format->lf_timestamp_flags & ETF_MACHINE_ORIENTED) &&
            format->lf_date_time.dts_fmt_lock != -1) {
            struct line_range time_range;

            time_range = find_string_attr_range(
                this->lss_token_attrs, &logline::L_TIMESTAMP);
            if (time_range.is_valid()) {
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
                }
                else {
                    adjusted_time = this->lss_token_line->get_timeval();
                    gmtime_r(&adjusted_time.tv_sec, &adjusted_tm.et_tm);
                    adjusted_tm.et_nsec = adjusted_time.tv_usec * 1000;
                    len = format->lf_date_time.ftime(buffer, sizeof(buffer), adjusted_tm);
                }

                if (len > time_range.length()) {
                    ssize_t padding = len - time_range.length();

                    value_out.insert(time_range.lr_start,
                                     padding,
                                     ' ');
                    shift_string_attrs(this->lss_token_attrs,
                                       time_range.lr_start + 1,
                                       padding);
                }
                value_out.replace(time_range.lr_start,
                                  len,
                                  buffer,
                                  len);
                this->lss_token_shift_start = time_range.lr_start;
                this->lss_token_shift_size = len - time_range.length();
            }
        }
    }

    if (this->lss_flags & F_FILENAME) {
        value_out.insert(0, 1, ' ');
        value_out.insert(0, this->lss_token_file->get_filename());
    }

    // Insert space for the file/search-hit markers.
    value_out.insert(0, 1, ' ');

    if (this->lss_flags & F_TIME_OFFSET) {
        int64_t start_millis, curr_millis;

        vis_line_t prev_mark =
            tc.get_bookmarks()[&textview_curses::BM_USER].prev(vis_line_t(row));
        if (prev_mark == -1) {
            prev_mark = vis_line_t(0);
        }

        logline *first_line = this->find_line(this->at(prev_mark));
        start_millis = first_line->get_time_in_millis();
        curr_millis = this->lss_token_line->get_time_in_millis();
        int64_t diff = curr_millis - start_millis;

        value_out = "|" + value_out;
        string relstr;
        size_t rel_length = str2reltime(diff, relstr);
        value_out.insert(0, relstr);
        if (rel_length < 12) {
            value_out.insert(0, 12 - rel_length, ' ');
        }
    }
}

void logfile_sub_source::text_attrs_for_line(textview_curses &lv,
                                             int row,
                                             string_attrs_t &value_out)
{
    view_colors &     vc        = view_colors::singleton();
    logline *         next_line = NULL;
    struct line_range lr;
    int time_offset_end = 0;
    int attrs           = 0;

    value_out = this->lss_token_attrs;
    switch (this->lss_token_line->get_msg_level()) {
    case logline::LEVEL_FATAL:
    case logline::LEVEL_CRITICAL:
    case logline::LEVEL_ERROR:
        attrs = vc.attrs_for_role(view_colors::VCR_ERROR);
        break;

    case logline::LEVEL_WARNING:
        attrs = vc.attrs_for_role(view_colors::VCR_WARNING);
        break;

    default:
        attrs = vc.attrs_for_role(view_colors::VCR_TEXT);
        break;
    }

    if ((row + 1) < (int)this->lss_filtered_index.size()) {
        next_line = this->find_line(this->at(vis_line_t(row + 1)));
    }

    if (next_line != NULL &&
        (day_num(next_line->get_time()) >
         day_num(this->lss_token_line->get_time()))) {
        attrs |= A_UNDERLINE;
    }

    const std::vector<logline_value> &line_values = this->lss_token_values;

    lr.lr_start = 0;
    lr.lr_end = this->lss_token_value.length();
    value_out.push_back(string_attr(lr, &textview_curses::SA_ORIGINAL_LINE));

    lr.lr_start = time_offset_end;
    lr.lr_end   = -1;

    value_out.push_back(string_attr(lr, &view_curses::VC_STYLE, attrs));

    for (vector<logline_value>::const_iterator lv_iter = line_values.begin();
         lv_iter != line_values.end();
         ++lv_iter) {
        if (lv_iter->lv_sub_offset != this->lss_token_line->get_sub_offset() ||
            !lv_iter->lv_origin.is_valid()) {
            continue;
        }

        if (lv_iter->lv_hidden) {
            value_out.push_back(string_attr(
                lv_iter->lv_origin, &textview_curses::SA_HIDDEN));
        }

        if (!lv_iter->lv_identifier || !lv_iter->lv_origin.is_valid()) {
            continue;
        }

        int id_attrs = vc.attrs_for_ident(lv_iter->text_value(),
                                          lv_iter->text_length());

        value_out.push_back(string_attr(
                lv_iter->lv_origin, &view_curses::VC_STYLE, id_attrs));
    }

    if (this->lss_token_shift_size) {
        shift_string_attrs(value_out, this->lss_token_shift_start + 1,
                           this->lss_token_shift_size);
    }

    shift_string_attrs(value_out, 0, 1);

    lr.lr_start = 0;
    lr.lr_end = 1;
    {
        vis_bookmarks &bm = lv.get_bookmarks();
        bookmark_vector<vis_line_t> &bv = bm[&BM_FILES];
        bool is_first_for_file = binary_search(
            bv.begin(), bv.end(), vis_line_t(row));
        bool is_last_for_file = binary_search(
            bv.begin(), bv.end(), vis_line_t(row + 1));
        chtype graph = ACS_VLINE;
        if (is_first_for_file) {
            if (is_last_for_file) {
                graph = ACS_DIAMOND;
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

        bookmark_vector<vis_line_t> &bv_search = bm[&textview_curses::BM_SEARCH];

        if (binary_search(::begin(bv_search), ::end(bv_search), vis_line_t(row))) {
            lr.lr_start = 0;
            lr.lr_end = 1;
            value_out.push_back(string_attr(
                lr, &view_curses::VC_STYLE, A_REVERSE));
        }
    }
    value_out.push_back(string_attr(lr, &view_curses::VC_STYLE, vc.attrs_for_ident(
        this->lss_token_file->get_filepath())));

    if (this->lss_flags & F_TIME_OFFSET) {
        time_offset_end = 13;
        lr.lr_start     = 0;
        lr.lr_end       = time_offset_end;

        shift_string_attrs(value_out, 0, time_offset_end);

        // attrs = vc.attrs_for_role(view_colors::VCR_OK);
        attrs = view_colors::ansi_color_pair(COLOR_CYAN, COLOR_BLACK);
        value_out.push_back(string_attr(lr, &view_curses::VC_STYLE, attrs));
        value_out.push_back(string_attr(line_range(12, 13),
            &view_curses::VC_GRAPHIC, ACS_VLINE));

        int bar_attrs = 0;

        switch (this->get_line_accel_direction(vis_line_t(row))) {
        case log_accel::A_STEADY:
            break;
        case log_accel::A_DECEL:
            bar_attrs = view_colors::ansi_color_pair(COLOR_RED, COLOR_BLACK);
            break;
        case log_accel::A_ACCEL:
            bar_attrs = view_colors::ansi_color_pair(COLOR_GREEN, COLOR_BLACK);
            break;
        }
        value_out.push_back(
            string_attr(line_range(12, 13), &view_curses::VC_STYLE, bar_attrs));
    }

    lr.lr_start = 0;
    lr.lr_end   = -1;
    value_out.push_back(string_attr(lr, &logline::L_FILE, this->lss_token_file));
    value_out.push_back(string_attr(lr, &textview_curses::SA_FORMAT,
                                    this->lss_token_file->get_format()->get_name()));

    {
        bookmark_vector<vis_line_t> &bv = lv.get_bookmarks()[&textview_curses::BM_PARTITION];
        bookmark_vector<vis_line_t>::iterator bv_iter;

        bv_iter = lower_bound(bv.begin(), bv.end(), vis_line_t(row + 1));
        if (bv_iter != bv.begin()) {
            --bv_iter;
            content_line_t part_start_line = this->at(*bv_iter);
            std::map<content_line_t, bookmark_metadata>::iterator bm_iter;

            if ((bm_iter = this->lss_user_mark_metadata.find(part_start_line))
                != this->lss_user_mark_metadata.end()) {
                lr.lr_start = 0;
                lr.lr_end   = -1;
                value_out.push_back(string_attr(lr, &logline::L_PARTITION, &bm_iter->second));
            }
        }
    }

    if (this->lss_token_file->is_time_adjusted()) {
        struct line_range time_range = find_string_attr_range(
            value_out, &logline::L_TIMESTAMP);

        if (time_range.lr_end != -1) {
            attrs = vc.attrs_for_role(view_colors::VCR_ADJUSTED_TIME);
            value_out.push_back(string_attr(time_range, &view_curses::VC_STYLE, attrs));
        }
    }
    else if ((((this->lss_token_line->get_time() / (5 * 60)) % 2) == 0) &&
             !this->lss_token_line->is_continued()) {
        struct line_range time_range = find_string_attr_range(
            value_out, &logline::L_TIMESTAMP);

        if (time_range.lr_end != -1) {
            attrs = vc.attrs_for_role(view_colors::VCR_ALT_ROW);
            value_out.push_back(string_attr(time_range, &view_curses::VC_STYLE, attrs));
        }
    }

    if (this->lss_token_line->is_time_skewed()) {
        struct line_range time_range = find_string_attr_range(
            value_out, &logline::L_TIMESTAMP);

        if (time_range.lr_end != -1) {
            attrs = vc.attrs_for_role(view_colors::VCR_SKEWED_TIME);
            value_out.push_back(string_attr(time_range, &view_curses::VC_STYLE, attrs));
        }
    }
}

bool logfile_sub_source::rebuild_index(bool force)
{
    iterator iter;
    size_t total_lines = 0;
    bool retval = force;
    int file_count = 0;

    for (iter = this->lss_files.begin();
         iter != this->lss_files.end();
         iter++) {
        if ((*iter)->get_file() == NULL) {
            if ((*iter)->ld_lines_indexed > 0) {
                force  = true;
                retval = true;
            }
        }
        else {
            if ((*iter)->get_file()->rebuild_index()) {
                retval = true;
            }
            file_count += 1;
            total_lines += (*iter)->get_file()->size();
        }
    }
    if (force) {
        for (iter = this->lss_files.begin();
             iter != this->lss_files.end();
             iter++) {
            (*iter)->ld_lines_indexed = 0;
        }

        this->lss_index.clear();
        this->lss_filtered_index.clear();
        this->lss_longest_line = 0;
    }

    if (retval || force) {
        size_t index_size = 0, start_size = this->lss_index.size();

        kmerge_tree_c<logline, logfile_data, logfile::iterator> merge(file_count);

        for (iter = this->lss_files.begin();
             iter != this->lss_files.end();
             iter++) {
            logfile_data *ld = *iter;
            logfile *lf = ld->get_file();
            if (lf == NULL) {
                continue;
            }

            merge.add(ld,
                      lf->begin() + ld->ld_lines_indexed,
                      lf->end());
            index_size += lf->size();
            this->lss_longest_line = std::max(this->lss_longest_line, lf->get_longest_line_length());
        }

        this->lss_index.reset();

        merge.execute();
        for (;;) {
            logfile::iterator lf_iter;
            logfile_data *ld;

            if (!merge.get_top(ld, lf_iter)) {
                break;
            }

            int file_index = ld->ld_file_index;
            int line_index = lf_iter - ld->get_file()->begin();

            content_line_t con_line(file_index * MAX_LINES_PER_FILE +
                                    line_index);

            off_t insert_point = this->lss_index.merge_value(
                    con_line, logline_cmp(*this));
            if (insert_point < (off_t)start_size) {
                start_size = 0;
                this->lss_filtered_index.clear();
            }

            merge.next();
        }

        for (iter = this->lss_files.begin();
             iter != this->lss_files.end();
             iter++) {
            if ((*iter)->get_file() == NULL)
                continue;

            (*iter)->ld_lines_indexed = (*iter)->get_file()->size();
        }

        this->lss_index.finish();

        this->lss_filtered_index.reserve(this->lss_index.size());

        uint32_t filter_in_mask, filter_out_mask;
        this->get_filters().get_enabled_mask(filter_in_mask, filter_out_mask);

        if (start_size == 0 && this->lss_index_delegate != NULL) {
            this->lss_index_delegate->index_start(*this);
        }

        for (size_t index_index = start_size;
             index_index < this->lss_index.size();
             index_index++) {
            content_line_t cl = (content_line_t) this->lss_index[index_index];
            uint64_t line_number;
            logfile_data *ld = this->find_data(cl, line_number);
            logfile::iterator line_iter = ld->get_file()->begin() + line_number;

            if (!ld->ld_filter_state.excluded(filter_in_mask, filter_out_mask,
                    line_number) && this->check_extra_filters(*line_iter)) {
                this->lss_filtered_index.push_back(index_index);
                if (this->lss_index_delegate != NULL) {
                    logfile *lf = ld->get_file();
                    this->lss_index_delegate->index_line(
                            *this, lf, lf->begin() + line_number);
                }
            }
        }

        if (this->lss_index_delegate != NULL) {
            this->lss_index_delegate->index_complete(*this);
        }
    }

    return retval;
}

void logfile_sub_source::text_update_marks(vis_bookmarks &bm)
{
    logfile *  last_file = NULL;
    vis_line_t vl;

    bm[&BM_WARNINGS].clear();
    bm[&BM_ERRORS].clear();
    bm[&BM_FILES].clear();

    for (bookmarks<content_line_t>::type::iterator iter =
             this->lss_user_marks.begin();
         iter != this->lss_user_marks.end();
         ++iter) {
        bm[iter->first].clear();
    }

    for (; vl < (int)this->lss_filtered_index.size(); ++vl) {
        const content_line_t orig_cl = this->at(vl);
        content_line_t cl = orig_cl;
        logfile *      lf;

        lf = this->find(cl);

        for (bookmarks<content_line_t>::type::iterator iter =
                 this->lss_user_marks.begin();
             iter != this->lss_user_marks.end();
             ++iter) {
            if (binary_search(iter->second.begin(),
                              iter->second.end(),
                              orig_cl)) {
                bm[iter->first].insert_once(vl);

                if (iter->first == &textview_curses::BM_USER) {
                    logfile::iterator ll = lf->begin() + cl;

                    ll->set_mark(true);
                }
            }
        }

        if (lf != last_file) {
            bm[&BM_FILES].insert_once(vl);
        }

        logfile::iterator line_iter = lf->begin() + cl;
        if (!line_iter->is_continued()) {
            switch (line_iter->get_msg_level()) {
                case logline::LEVEL_WARNING:
                    bm[&BM_WARNINGS].insert_once(vl);
                    break;

                case logline::LEVEL_FATAL:
                case logline::LEVEL_ERROR:
                case logline::LEVEL_CRITICAL:
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

        if (curr_line->is_continued()) {
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
    for (iterator iter = this->begin(); iter != this->end(); ++iter) {
        logfile_data *ld = *iter;
        logfile *lf = ld->get_file();

        if (lf != NULL) {
            ld->ld_filter_state.clear_deleted_filter_state();
            lf->reobserve_from(lf->begin() + ld->ld_filter_state.get_min_count(lf->size()));
        }
    }

    uint32_t filtered_in_mask, filtered_out_mask;

    this->get_filters().get_enabled_mask(filtered_in_mask, filtered_out_mask);

    if (this->lss_index_delegate != NULL) {
        this->lss_index_delegate->index_start(*this);
    }

    this->lss_filtered_index.clear();
    for (size_t index_index = 0; index_index < this->lss_index.size(); index_index++) {
        content_line_t cl = (content_line_t) this->lss_index[index_index];
        uint64_t line_number;
        logfile_data *ld = this->find_data(cl, line_number);
        logfile::iterator line_iter = ld->get_file()->begin() + line_number;

        if (!ld->ld_filter_state.excluded(filtered_in_mask, filtered_out_mask,
                line_number) && this->check_extra_filters(*line_iter)) {
            this->lss_filtered_index.push_back(index_index);
            if (this->lss_index_delegate != NULL) {
                logfile *lf = ld->get_file();
                this->lss_index_delegate->index_line(
                        *this, lf, lf->begin() + line_number);
            }
        }
    }

    if (this->lss_index_delegate != NULL) {
        this->lss_index_delegate->index_complete(*this);
    }
}
