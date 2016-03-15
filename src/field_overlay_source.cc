/**
 * Copyright (c) 2015, Timothy Stack
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

#include "lnav.hh"

#include "relative_time.hh"
#include "field_overlay_source.hh"
#include "readline_highlighters.hh"

using namespace std;

void field_overlay_source::build_summary_lines(const listview_curses &lv)
{
    textview_curses &tv = (textview_curses &) lv;
    vis_bookmarks &bookmarks = tv.get_bookmarks();
    textfile_sub_source &tss = lnav_data.ld_text_source;
    logfile_sub_source &lss = lnav_data.ld_log_source;

    this->fos_summary_lines.clear();

    {
        vis_line_t filled_rows = lv.rows_available(
            lv.get_top(), listview_curses::RD_DOWN);
        vis_line_t height, free_rows;
        unsigned long width;

        lv.get_dimensions(height, width);
        free_rows = height - filled_rows - vis_line_t(this->fos_lines.size());
        if (free_rows < 2) {
            this->fos_summary_lines.clear();
        }
        else {
            string last_time, time_span;

            if (lv.get_inner_height() == 0) {
                last_time = "No log messages";
                time_span = "None";
            }
            else {
                logline *first_line, *last_line;

                first_line = lss.find_line(lss.at(vis_line_t(0)));
                last_line = lss.find_line(lss.at(lv.get_bottom()));
                last_time = "Last message: " + precise_time_ago(
                    last_line->get_timeval(), true);
                str2reltime(last_line->get_time_in_millis() -
                            first_line->get_time_in_millis(),
                            time_span);
            }

            this->fos_summary_lines.push_back(attr_line_t());
            attr_line_t &sum_line = this->fos_summary_lines.back();
            string &sum_msg = sum_line.get_string();
            sum_line.with_ansi_string(
                    "       %s; Files: " ANSI_BOLD("%'2d") "; "
                        ANSI_ROLE("Errors") ": " ANSI_BOLD("%'4d") "; "
                        ANSI_ROLE("Warnings") ": " ANSI_BOLD("%'4d") "; "
                        "Time span: %s",
                    last_time.c_str(),
                    lss.file_count() + tss.size(),
                    view_colors::VCR_ERROR,
                    bookmarks[&logfile_sub_source::BM_ERRORS].size(),
                    view_colors::VCR_WARNING,
                    bookmarks[&logfile_sub_source::BM_WARNINGS].size(),
                    time_span.c_str())
                .with_attr(string_attr(
                    line_range(1, 2),
                    &view_curses::VC_GRAPHIC,
                    ACS_ULCORNER
                ))
                .with_attr(string_attr(
                    line_range(2, 6),
                    &view_curses::VC_GRAPHIC,
                    ACS_HLINE
                ))
                .with_attr(string_attr(
                    line_range(sum_msg.length() + 1,
                               sum_msg.length() + 5),
                    &view_curses::VC_GRAPHIC,
                    ACS_HLINE
                ))
                .with_attr(string_attr(
                    line_range(sum_msg.length() + 5,
                               sum_msg.length() + 6),
                    &view_curses::VC_GRAPHIC,
                    ACS_URCORNER
                ))
                .right_justify(width - 2);
        }
    }
}

void field_overlay_source::build_field_lines(const listview_curses &lv)
{
    logfile_sub_source &lss = lnav_data.ld_log_source;
    view_colors &vc = view_colors::singleton();

    this->fos_lines.clear();

    if (lss.text_line_count() == 0) {
        this->fos_log_helper.clear();

        return;
    }

    content_line_t cl = lss.at(lv.get_top());
    logfile *file = lss.find(cl);
    logfile::iterator ll = file->begin() + cl;
    log_format *format = file->get_format();
    bool display = false;

    if (ll->is_time_skewed()) {
        display = true;
    }
    if (this->fos_active) {
        display = true;
    }

    if (!display) {
        return;
    }

    if (!this->fos_log_helper.parse_line(lv.get_top())) {
        return;
    }

    char old_timestamp[64], curr_timestamp[64], orig_timestamp[64];
    struct timeval curr_tv, offset_tv, orig_tv;
    attr_line_t time_line;
    string &time_str = time_line.get_string();
    struct line_range time_lr;

    sql_strftime(curr_timestamp, sizeof(curr_timestamp),
                 ll->get_time(),
                 ll->get_millis(),
                 'T');

    if (ll->is_time_skewed()) {
        time_lr.lr_start = 1;
        time_lr.lr_end = 2;
        time_line.with_attr(string_attr(time_lr, &view_curses::VC_GRAPHIC,
                                        ACS_LLCORNER));
        time_str.append("   Out-Of-Time-Order Message");
        time_lr.lr_start = 3;
        time_lr.lr_end = time_str.length();
        time_line.with_attr(string_attr(time_lr, &view_curses::VC_STYLE,
                                        vc.attrs_for_role(view_colors::VCR_SKEWED_TIME)));
        time_str.append(" --");
    }

    time_str.append(" Received Time: ");
    time_lr.lr_start = time_str.length();
    time_str.append(curr_timestamp);
    time_lr.lr_end = time_str.length();
    time_line.with_attr(string_attr(time_lr, &view_curses::VC_STYLE, A_BOLD));
    time_str.append(" -- ");
    time_lr.lr_start = time_str.length();
    time_str.append(precise_time_ago(ll->get_timeval(), true));
    time_lr.lr_end = time_str.length();
    time_line.with_attr(string_attr(time_lr, &view_curses::VC_STYLE, A_BOLD));

    struct line_range time_range = find_string_attr_range(
        this->fos_log_helper.ldh_line_attrs, &logline::L_TIMESTAMP);

    curr_tv = this->fos_log_helper.ldh_line->get_timeval();
    if (this->fos_log_helper.ldh_line->is_time_skewed() && time_range.lr_end != -1) {
        const char *time_src = this->fos_log_helper.ldh_msg.get_data() +
                               time_range.lr_start;
        struct timeval actual_tv;
        struct exttm tm;

        if (format->lf_date_time.scan(time_src, time_range.length(),
                                      format->get_timestamp_formats(),
                                      &tm, actual_tv,
                                      false)) {
            sql_strftime(orig_timestamp, sizeof(orig_timestamp), actual_tv, 'T');
            time_str.append(";  Actual Time: ");
            time_lr.lr_start = time_str.length();
            time_str.append(orig_timestamp);
            time_lr.lr_end = time_str.length();
            time_line.with_attr(string_attr(
                time_lr,
                &view_curses::VC_STYLE,
                vc.attrs_for_role(view_colors::VCR_SKEWED_TIME)));

            struct timeval diff_tv;

            timersub(&curr_tv, &actual_tv, &diff_tv);
            time_str.append(";  Diff: ");
            time_lr.lr_start = time_str.length();
            str2reltime(diff_tv, time_str);
            time_lr.lr_end = time_str.length();
            time_line.with_attr(string_attr(
                time_lr,
                &view_curses::VC_STYLE,
                A_BOLD));
        }
    }

    offset_tv = this->fos_log_helper.ldh_file->get_time_offset();
    timersub(&curr_tv, &offset_tv, &orig_tv);
    sql_strftime(old_timestamp, sizeof(old_timestamp),
                 orig_tv.tv_sec, orig_tv.tv_usec / 1000,
                 'T');
    if (offset_tv.tv_sec || offset_tv.tv_usec) {
        char offset_str[32];

        time_str.append("  Pre-adjust Time: ");
        time_str.append(old_timestamp);
        snprintf(offset_str, sizeof(offset_str),
                 "  Offset: %+d.%03d",
                 (int)offset_tv.tv_sec, (int)(offset_tv.tv_usec / 1000));
        time_str.append(offset_str);
    }

    this->fos_lines.push_back(time_line);

    if (!this->fos_active) {
        return;
    }

    this->fos_known_key_size = 0;
    this->fos_unknown_key_size = 0;

    for (std::vector<logline_value>::iterator iter =
            this->fos_log_helper.ldh_line_values.begin();
         iter != this->fos_log_helper.ldh_line_values.end();
         ++iter) {
        this->fos_known_key_size = max(this->fos_known_key_size,
                                       (int)iter->lv_name.size());
    }

    for (data_parser::element_list_t::iterator iter =
            this->fos_log_helper.ldh_parser->dp_pairs.begin();
         iter != this->fos_log_helper.ldh_parser->dp_pairs.end();
         ++iter) {
        std::string colname = this->fos_log_helper.ldh_parser->get_element_string(
                iter->e_sub_elements->front());

        colname = this->fos_log_helper.ldh_namer->add_column(colname);
        this->fos_unknown_key_size = max(
                this->fos_unknown_key_size, (int)colname.length());
    }

    log_format *lf = this->fos_log_helper.ldh_file->get_format();
    if (!lf->get_pattern_regex().empty()) {
        attr_line_t pattern_al;
        std::string &pattern_str = pattern_al.get_string();
        pattern_str = " Pattern: " + lf->get_pattern_name() + " = ";
        int skip = pattern_str.length();
        pattern_str += lf->get_pattern_regex();
        readline_regex_highlighter(pattern_al, skip);
        this->fos_lines.push_back(pattern_al);
    }


    if (this->fos_log_helper.ldh_line_values.empty()) {
        this->fos_lines.push_back(" No known message fields");
    }

    const log_format *last_format = NULL;

    for (size_t lpc = 0; lpc < this->fos_log_helper.ldh_line_values.size(); lpc++) {
        logline_value &lv = this->fos_log_helper.ldh_line_values[lpc];
        string format_name = lv.lv_format->get_name().to_string();
        attr_line_t al;
        string str;

        if (lv.lv_format != last_format) {
            this->fos_lines.push_back(" Known message fields for table " +
                                      format_name +
                                      ":");
            this->fos_lines.back().with_attr(string_attr(
                line_range(32, 32 + format_name.length()),
                &view_curses::VC_STYLE,
                vc.attrs_for_ident(format_name) | A_BOLD));
            last_format = lv.lv_format;
        }

        str = "   " + lv.lv_name.to_string();
        str.append(this->fos_known_key_size - lv.lv_name.size() + 3, ' ');
        str += " = " + lv.to_string();

        al.with_string(str)
                .with_attr(string_attr(
                        line_range(3, 3 + lv.lv_name.size()),
                        &view_curses::VC_STYLE,
                        vc.attrs_for_ident(lv.lv_name.to_string())));

        this->fos_lines.push_back(al);
        this->add_key_line_attrs(this->fos_known_key_size);
    }

    std::map<const intern_string_t, json_ptr_walk::walk_list_t>::iterator json_iter;

    if (!this->fos_log_helper.ldh_json_pairs.empty()) {
        this->fos_lines.push_back(" JSON fields:");
    }

    for (json_iter = this->fos_log_helper.ldh_json_pairs.begin();
         json_iter != this->fos_log_helper.ldh_json_pairs.end();
         ++json_iter) {
        json_ptr_walk::walk_list_t &jpairs = json_iter->second;

        for (size_t lpc = 0; lpc < jpairs.size(); lpc++) {
            this->fos_lines.push_back("   " +
                                      this->fos_log_helper.format_json_getter(json_iter->first, lpc) + " = " +
                                      jpairs[lpc].wt_value);
            this->add_key_line_attrs(0);
        }
    }

    if (this->fos_log_helper.ldh_parser->dp_pairs.empty()) {
        this->fos_lines.push_back(" No discovered message fields");
    }
    else {
        this->fos_lines.push_back(" Discovered fields for logline table from message format: ");
        this->fos_lines.back().with_attr(string_attr(
            line_range(23, 23 + 7),
            &view_curses::VC_STYLE,
            vc.attrs_for_ident("logline")
        ));
        attr_line_t &al = this->fos_lines.back();
        string &disc_str = al.get_string();

        al.with_attr(string_attr(
            line_range(disc_str.length(), -1),
            &view_curses::VC_STYLE,
            A_BOLD));
        disc_str.append(this->fos_log_helper.ldh_msg_format);
    }

    data_parser::element_list_t::iterator iter;

    iter = this->fos_log_helper.ldh_parser->dp_pairs.begin();
    for (size_t lpc = 0;
         lpc < this->fos_log_helper.ldh_parser->dp_pairs.size(); lpc++, ++iter) {
        string &name = this->fos_log_helper.ldh_namer->cn_names[lpc];
        string val = this->fos_log_helper.ldh_parser->get_element_string(
                iter->e_sub_elements->back());
        attr_line_t al("   " + name + " = " + val);

        al.with_attr(string_attr(
                line_range(3, 3 + name.length()),
                &view_curses::VC_STYLE,
                vc.attrs_for_ident(name)));

        this->fos_lines.push_back(al);
        this->add_key_line_attrs(this->fos_unknown_key_size,
                                 lpc == (this->fos_log_helper.ldh_parser->dp_pairs.size() - 1));
    }
};
