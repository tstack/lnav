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

#include "field_overlay_source.hh"
#include "readline_highlighters.hh"

using namespace std;

size_t field_overlay_source::list_overlay_count(const listview_curses &lv)
{
    logfile_sub_source &lss = lnav_data.ld_log_source;
    view_colors &vc = view_colors::singleton();

    if (!this->fos_active) {
        return 0;
    }

    if (lss.text_line_count() == 0) {
        this->fos_log_helper.clear();
        return 0;
    }

    content_line_t    cl   = lss.at(lv.get_top());

    if (!this->fos_log_helper.parse_line(cl)) {
        return 0;
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

    this->fos_lines.clear();

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

    char old_timestamp[64], curr_timestamp[64], orig_timestamp[64];
    struct timeval curr_tv, offset_tv, orig_tv;
    attr_line_t time_line;
    string &time_str = time_line.get_string();
    struct line_range time_lr;

    sql_strftime(curr_timestamp, sizeof(curr_timestamp),
                 this->fos_log_helper.ldh_line->get_time(),
                 this->fos_log_helper.ldh_line->get_millis(),
                 'T');

    time_str = " Received Time: ";
    time_lr.lr_start = time_str.length();
    time_str.append(curr_timestamp);
    time_lr.lr_end = time_str.length();
    time_line.with_attr(string_attr(time_lr, &view_curses::VC_STYLE, A_BOLD));

    struct line_range time_range = find_string_attr_range(
        this->fos_log_helper.ldh_line_attrs, &logline::L_TIMESTAMP);

    if (this->fos_log_helper.ldh_line->is_time_skewed() && time_range.lr_end != -1) {
        const char *time_src = this->fos_log_helper.ldh_msg.get_data() +
                               time_range.lr_start;
        struct timeval tv;
        struct exttm tm;

        if (lf->lf_date_time.scan(time_src, time_range.length(), NULL, &tm, tv)) {
            time_lr.lr_start = time_str.length() + 2;
            sql_strftime(orig_timestamp, sizeof(orig_timestamp), tv, 'T');
            time_str.append("  Actual Time: ");
            time_str.append(orig_timestamp);
            time_lr.lr_end = time_str.length();
            time_line.with_attr(string_attr(
                time_lr,
                &view_curses::VC_STYLE,
                vc.attrs_for_role(view_colors::VCR_SKEWED_TIME)));
        }
    }

    curr_tv = this->fos_log_helper.ldh_line->get_timeval();
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

    if (this->fos_log_helper.ldh_line_values.empty()) {
        this->fos_lines.push_back(" No known message fields");
    }

    const log_format *last_format = NULL;

    for (size_t lpc = 0; lpc < this->fos_log_helper.ldh_line_values.size(); lpc++) {
        logline_value &lv = this->fos_log_helper.ldh_line_values[lpc];
        attr_line_t al;
        string str;

        if (lv.lv_format != last_format) {
            this->fos_lines.push_back(" Known message fields:  (SQL table -- " +
                                      lv.lv_format->get_name().to_string() +
                                      ")");
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
        this->fos_lines.push_back(" Discovered message fields:  (SQL table -- logline)");
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

    return this->fos_lines.size();
};
