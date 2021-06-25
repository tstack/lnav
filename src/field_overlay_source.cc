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

#include "base/humanize.time.hh"
#include "lnav_util.hh"
#include "ansi_scrubber.hh"
#include "vtab_module.hh"
#include "relative_time.hh"
#include "field_overlay_source.hh"
#include "readline_highlighters.hh"
#include "vtab_module_json.hh"
#include "log_format_ext.hh"
#include "log_vtab_impl.hh"

using namespace std;

json_string extract(const char *str);

void field_overlay_source::build_summary_lines(const listview_curses &lv)
{
    auto& tc = dynamic_cast<const textview_curses &>(lv);
    textfile_sub_source &tss = this->fos_tss;
    logfile_sub_source &lss = this->fos_lss;

    this->fos_summary_lines.clear();

    {
        vis_line_t filled_rows = lv.rows_available(
            lv.get_top(), listview_curses::RD_DOWN);
        vis_line_t height, free_rows;
        unsigned long width;
        long rate_len = 0;

        lv.get_dimensions(height, width);
        free_rows = height - filled_rows - vis_line_t(this->fos_lines.size());
        if (free_rows < 2 || !this->fos_show_status) {
            this->fos_summary_lines.clear();
        }
        else {
            string time_span;
            double error_rate = 0.0;

            if (lv.get_inner_height() == 0) {
                time_span = "None";
            }
            else {
                logline *first_line, *last_line;
                time_t now = time(nullptr);

                first_line = lss.find_line(lss.at(vis_line_t(0)));
                last_line = lss.find_line(lss.at(lv.get_bottom()));
                time_span = humanize::time::duration::from_tv(
                    last_line->get_timeval() - first_line->get_timeval())
                    .to_string();

                time_t local_now = convert_log_time_to_local(now);
                time_t five_minutes_ago = local_now - (5 * 60 * 60);
                time_t ten_secs_ago = local_now - 10;

                vis_line_t from_five_min_ago = lss.find_from_time(five_minutes_ago);
                vis_line_t from_ten_secs_ago = lss.find_from_time(ten_secs_ago);
                auto &bm = tc.get_bookmarks();
                auto error_bm_iter = bm.find(&logfile_sub_source::BM_ERRORS);

                if (now > last_line->get_time() && from_five_min_ago != -1 &&
                    error_bm_iter != bm.end()) {
                    auto& error_bookmarks = error_bm_iter->second;
                    auto five_min_lower =
                        lower_bound(error_bookmarks.begin(),
                                    error_bookmarks.end(),
                                    from_five_min_ago);
                    if (five_min_lower != error_bookmarks.end()) {
                        double error_count = distance(
                            five_min_lower, error_bookmarks.end());
                        double time_diff = 5.0;

                        if (first_line->get_time() > five_minutes_ago) {
                            time_diff = (double) (local_now - first_line->get_time()) /
                                60.0;
                        }
                        error_rate = error_count / time_diff;

                        if (from_ten_secs_ago != -1) {
                            auto ten_sec_lower =
                                lower_bound(error_bookmarks.begin(),
                                            error_bookmarks.end(),
                                            from_ten_secs_ago);
                            if (ten_sec_lower != error_bookmarks.end()) {
                                double recent_error_count = distance(
                                    ten_sec_lower, error_bookmarks.end());
                                double recent_error_rate =
                                    recent_error_count / 10.0;
                                double long_error_rate =
                                    error_count / (time_diff * 60.0 / 10.0);

                                if (long_error_rate == 0.0) {
                                    long_error_rate = 1.0;
                                }
                                long computed_rate_len = lrint(ceil(
                                    (recent_error_rate * 40.0) / long_error_rate));
                                rate_len = min(10L, computed_rate_len);
                            }
                        }
                    }
                }
            }

            this->fos_summary_lines.emplace_back();
            attr_line_t &sum_line = this->fos_summary_lines.back();
            if (tss.empty()) {
                sum_line.with_ansi_string(
                    "       "
                        "Files: " ANSI_BOLD("%'2d") "; "
                        ANSI_ROLE("Error rate") ": " ANSI_BOLD(
                        "%'.2lf") "/min; "
                        "Time span: " ANSI_BOLD("%s"),
                    lss.file_count(),
                    view_colors::VCR_ERROR,
                    error_rate,
                    time_span.c_str());
            } else {
                sum_line.with_ansi_string(
                    "       "
                        "Log Files: " ANSI_BOLD("%'2d") "; "
                        "Text Files: " ANSI_BOLD("%'2d") "; "
                        ANSI_ROLE("Error rate") ": " ANSI_BOLD(
                        "%'.2lf") "/min; "
                        "Time span: " ANSI_BOLD("%s"),
                    lss.file_count(),
                    tss.size(),
                    view_colors::VCR_ERROR,
                    error_rate,
                    time_span.c_str());
            }
            string &sum_msg = sum_line.get_string();
            sum_line.with_attr(string_attr(
                    line_range(sum_msg.find("Error rate"),
                               sum_msg.find("Error rate") + rate_len),
                    &view_curses::VC_STYLE,
                    A_REVERSE
                ))
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
    logfile_sub_source &lss = this->fos_lss;
    view_colors &vc = view_colors::singleton();

    this->fos_lines.clear();

    if (lss.text_line_count() == 0) {
        this->fos_log_helper.clear();

        return;
    }

    content_line_t cl = lss.at(lv.get_top());
    std::shared_ptr<logfile> file = lss.find(cl);
    auto ll = file->begin() + cl;
    auto format = file->get_format();
    bool display = false;

    if (ll->is_time_skewed() ||
        ll->get_msg_level() == log_level_t::LEVEL_INVALID) {
        display = true;
    }
    if (!this->fos_contexts.empty()) {
        display = this->fos_contexts.top().c_show;
    }

    this->build_meta_line(lv, this->fos_lines, lv.get_top());

    if (!display) {
        return;
    }

    if (!this->fos_log_helper.parse_line(lv.get_top())) {
        return;
    }

    if (ll->get_msg_level() == LEVEL_INVALID) {
        for (const auto& sattr : this->fos_log_helper.ldh_line_attrs) {
            if (sattr.sa_type != &SA_INVALID) {
                continue;
            }

            auto emsg = fmt::format("   Invalid log message: {}",
                                    (const char *) sattr.sa_value.sav_ptr);
            auto al = attr_line_t(emsg)
                .with_attr(string_attr(line_range{1, 2},
                                       &view_curses::VC_GRAPHIC,
                                       ACS_LLCORNER))
                .with_attr(string_attr(line_range{0, 22},
                                       &view_curses::VC_ROLE,
                                       view_colors::VCR_INVALID_MSG));
            this->fos_lines.emplace_back(al);
        }
    }

    char old_timestamp[64], curr_timestamp[64], orig_timestamp[64];
    struct timeval curr_tv, offset_tv, orig_tv, diff_tv = { 0, 0 };
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
        time_line.with_attr(string_attr(time_lr, &view_curses::VC_ROLE,
                                        view_colors::VCR_SKEWED_TIME));
        time_str.append(" --");
    }

    time_str.append(" Received Time: ");
    time_lr.lr_start = time_str.length();
    time_str.append(curr_timestamp);
    time_lr.lr_end = time_str.length();
    time_line.with_attr(string_attr(time_lr, &view_curses::VC_STYLE, A_BOLD));
    time_str.append(" -- ");
    time_lr.lr_start = time_str.length();
    time_str.append(humanize::time::point::from_tv(ll->get_timeval())
                        .with_convert_to_local(true)
                        .as_precise_time_ago());
    time_lr.lr_end = time_str.length();
    time_line.with_attr(string_attr(time_lr, &view_curses::VC_STYLE, A_BOLD));

    struct line_range time_range = find_string_attr_range(
        this->fos_log_helper.ldh_line_attrs, &logline::L_TIMESTAMP);

    curr_tv = this->fos_log_helper.ldh_line->get_timeval();
    if (ll->is_time_skewed() && time_range.lr_end != -1) {
        const char *time_src = this->fos_log_helper.ldh_msg.get_data() +
                               time_range.lr_start;
        struct timeval actual_tv;
        date_time_scanner dts;
        struct exttm tm;

        dts.set_base_time(format->lf_date_time.dts_base_time);
        if (format->lf_date_time.scan(time_src, time_range.length(),
                                      format->get_timestamp_formats(),
                                      &tm, actual_tv,
                                      false) ||
            dts.scan(time_src, time_range.length(),
                     nullptr,
                     &tm, actual_tv,
                     false)) {
            sql_strftime(orig_timestamp, sizeof(orig_timestamp), actual_tv, 'T');
            time_str.append(";  Actual Time: ");
            time_lr.lr_start = time_str.length();
            time_str.append(orig_timestamp);
            time_lr.lr_end = time_str.length();
            time_line.with_attr(string_attr(
                time_lr,
                &view_curses::VC_ROLE,
                view_colors::VCR_SKEWED_TIME));

            timersub(&curr_tv, &actual_tv, &diff_tv);
            time_str.append(";  Diff: ");
            time_lr.lr_start = time_str.length();
            time_str.append(humanize::time::duration::from_tv(diff_tv)
                                .to_string());
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

    if ((!this->fos_contexts.empty() && this->fos_contexts.top().c_show) ||
        diff_tv.tv_sec > 0) {
        this->fos_lines.emplace_back(time_line);
    }

    if (this->fos_contexts.empty() || !this->fos_contexts.top().c_show) {
        return;
    }

    this->fos_known_key_size = LOG_BODY.length();
    this->fos_unknown_key_size = 0;

    for (auto & ldh_line_value : this->fos_log_helper.ldh_line_values) {
        auto& meta = ldh_line_value.lv_meta;
        int this_key_size = meta.lvm_name.size();

        if (!this->fos_contexts.empty()) {
            this_key_size += this->fos_contexts.top().c_prefix.length();
        }
        if (meta.lvm_kind == value_kind_t::VALUE_STRUCT) {
            this_key_size += 9;
        }
        if (!meta.lvm_struct_name.empty()) {
            this_key_size += meta.lvm_struct_name.size() + 11;
        }
        this->fos_known_key_size = max(this->fos_known_key_size, this_key_size);
    }

    for (auto iter = this->fos_log_helper.ldh_parser->dp_pairs.begin();
         iter != this->fos_log_helper.ldh_parser->dp_pairs.end();
         ++iter) {
        std::string colname = this->fos_log_helper.ldh_parser->get_element_string(
                iter->e_sub_elements->front());

        colname = this->fos_log_helper.ldh_namer->add_column(colname);
        this->fos_unknown_key_size = max(
                this->fos_unknown_key_size, (int)colname.length());
    }

    auto lf = this->fos_log_helper.ldh_file->get_format();
    if (!lf->get_pattern_regex(cl).empty()) {
        attr_line_t pattern_al;
        std::string &pattern_str = pattern_al.get_string();
        pattern_str = " Pattern: " + lf->get_pattern_name(cl) + " = ";
        int skip = pattern_str.length();
        pattern_str += lf->get_pattern_regex(cl);
        readline_regex_highlighter(pattern_al, skip);
        this->fos_lines.emplace_back(pattern_al);
    }


    if (this->fos_log_helper.ldh_line_values.empty()) {
        this->fos_lines.emplace_back(" No known message fields");
    }

    const log_format *last_format = nullptr;

    for (auto & lv : this->fos_log_helper.ldh_line_values) {
        if (!lv.lv_meta.lvm_format) {
            continue;
        }

        auto curr_format = lv.lv_meta.lvm_format.value();
        auto curr_elf = dynamic_cast<external_log_format *>(curr_format);
        string format_name = curr_format->get_name().to_string();
        attr_line_t al;
        string str, value_str = lv.to_string();

        if (curr_format != last_format) {
            this->fos_lines.emplace_back(" Known message fields for table " +
                                         format_name +
                                         ":");
            this->fos_lines.back().with_attr(string_attr(
                line_range(32, 32 + format_name.length()),
                &view_curses::VC_STYLE,
                vc.attrs_for_ident(format_name) | A_BOLD));
            last_format = curr_format;
        }

        std::string field_name, orig_field_name;
        if (lv.lv_meta.lvm_struct_name.empty()) {
            if (curr_elf && curr_elf->elf_body_field == lv.lv_meta.lvm_name) {
                field_name = LOG_BODY;
            } else {
                field_name = lv.lv_meta.lvm_name.to_string();
            }
            orig_field_name = field_name;
            if (!this->fos_contexts.empty()) {
                field_name = this->fos_contexts.top().c_prefix + field_name;
            }
            str = "   " + field_name;
        } else {
            auto_mem<char, sqlite3_free> jgetter;

            jgetter = sqlite3_mprintf("   jget(%s, '/%q')",
                                      lv.lv_meta.lvm_struct_name.get(),
                                      lv.lv_meta.lvm_name.get());
            str = jgetter;
        }
        str.append(this->fos_known_key_size - (str.length() - 3), ' ');
        str += " = " + value_str;

        al.with_string(str);
        if (lv.lv_meta.lvm_struct_name.empty()) {
            auto prefix_len = field_name.length() - orig_field_name.length();
            al.with_attr(string_attr(
                line_range(3 + prefix_len, 3 + prefix_len + field_name.size()),
                &view_curses::VC_STYLE,
                vc.attrs_for_ident(orig_field_name)));
        } else {
            al.with_attr(string_attr(
                line_range(8, 8 + lv.lv_meta.lvm_struct_name.size()),
                &view_curses::VC_STYLE,
                vc.attrs_for_ident(lv.lv_meta.lvm_struct_name)));
        }

        this->fos_lines.emplace_back(al);
        this->add_key_line_attrs(this->fos_known_key_size);

        if (lv.lv_meta.lvm_kind == value_kind_t::VALUE_STRUCT) {
            json_string js = extract(value_str.c_str());

            al.clear()
              .append("   extract(")
              .append(lv.lv_meta.lvm_name.get(),
                      &view_curses::VC_STYLE,
                      vc.attrs_for_ident(lv.lv_meta.lvm_name))
              .append(")")
              .append(this->fos_known_key_size - lv.lv_meta.lvm_name.size() - 9 + 3, ' ')
              .append(" = ")
              .append((const char *) js.js_content.in(), js.js_len);
            this->fos_lines.emplace_back(al);
            this->add_key_line_attrs(this->fos_known_key_size);
        }

    }

    std::map<const intern_string_t, json_ptr_walk::walk_list_t>::iterator json_iter;

    if (!this->fos_log_helper.ldh_json_pairs.empty()) {
        this->fos_lines.emplace_back(" JSON fields:");
    }

    for (json_iter = this->fos_log_helper.ldh_json_pairs.begin();
         json_iter != this->fos_log_helper.ldh_json_pairs.end();
         ++json_iter) {
        json_ptr_walk::walk_list_t &jpairs = json_iter->second;

        for (size_t lpc = 0; lpc < jpairs.size(); lpc++) {
            this->fos_lines.emplace_back(
                "   " +
                this->fos_log_helper.format_json_getter(json_iter->first, lpc) + " = " +
                jpairs[lpc].wt_value);
            this->add_key_line_attrs(0);
        }
    }

    if (!this->fos_log_helper.ldh_xml_pairs.empty()) {
        this->fos_lines.emplace_back(" XML fields:");
    }

    for (const auto& xml_pair : this->fos_log_helper.ldh_xml_pairs) {
        auto_mem<char, sqlite3_free> qname;
        auto_mem<char, sqlite3_free> xp_call;

        qname = sql_quote_ident(xml_pair.first.first.get());
        xp_call = sqlite3_mprintf("xpath(%Q, %s)",
                                  xml_pair.first.second.c_str(),
                                  qname.in());
        this->fos_lines.emplace_back(fmt::format(
            "   {} = {}", xp_call, xml_pair.second));
        this->add_key_line_attrs(0);
    }

    if (!this->fos_contexts.empty() &&
        !this->fos_contexts.top().c_show_discovered) {
        return;
    }

    if (this->fos_log_helper.ldh_parser->dp_pairs.empty()) {
        this->fos_lines.emplace_back(" No discovered message fields");
    }
    else {
        this->fos_lines.emplace_back(" Discovered fields for logline table from message format: ");
        this->fos_lines.back().with_attr(string_attr(
            line_range(23, 23 + 7),
            &view_curses::VC_STYLE,
            vc.attrs_for_ident("logline")
        ));
        auto &al = this->fos_lines.back();
        auto &disc_str = al.get_string();

        al.with_attr(string_attr(
            line_range(disc_str.length(), -1),
            &view_curses::VC_STYLE,
            A_BOLD));
        disc_str.append(this->fos_log_helper.ldh_msg_format);
    }

    auto iter = this->fos_log_helper.ldh_parser->dp_pairs.begin();
    for (size_t lpc = 0;
         lpc < this->fos_log_helper.ldh_parser->dp_pairs.size(); lpc++, ++iter) {
        auto &name = this->fos_log_helper.ldh_namer->cn_names[lpc];
        auto val = this->fos_log_helper.ldh_parser->get_element_string(
                iter->e_sub_elements->back());
        attr_line_t al(fmt::format("   {} = {}", name, val));

        al.with_attr(string_attr(
                line_range(3, 3 + name.length()),
                &view_curses::VC_STYLE,
                vc.attrs_for_ident(name)));

        this->fos_lines.emplace_back(al);
        this->add_key_line_attrs(this->fos_unknown_key_size,
                                 lpc == (this->fos_log_helper.ldh_parser->dp_pairs.size() - 1));
    }
}

void field_overlay_source::build_meta_line(const listview_curses &lv,
                                           std::vector<attr_line_t> &dst,
                                           vis_line_t row)
{
    content_line_t cl = this->fos_lss.at(row);
    auto const &bm = this->fos_lss.get_user_bookmark_metadata();
    view_colors &vc = view_colors::singleton();
    auto iter = bm.find(cl);

    if (iter != bm.end()) {
        const bookmark_metadata &line_meta = iter->second;
        size_t filename_width = this->fos_lss.get_filename_offset();

        if (!line_meta.bm_comment.empty()) {
            attr_line_t al;

            al.with_string(" + ")
              .with_attr(string_attr(
                  line_range(1, 2),
                  &view_curses::VC_GRAPHIC,
                  line_meta.bm_tags.empty() ? ACS_LLCORNER : ACS_LTEE
              ))
              .append(line_meta.bm_comment);
            al.insert(0, filename_width, ' ');
            dst.emplace_back(al);
        }
        if (!line_meta.bm_tags.empty()) {
            attr_line_t al;

            al.with_string(" +")
              .with_attr(string_attr(
                  line_range(1, 2),
                  &view_curses::VC_GRAPHIC,
                  ACS_LLCORNER
              ));
            for (const auto &str : line_meta.bm_tags) {
                al.append(1, ' ')
                  .append(str, &view_curses::VC_STYLE, vc.attrs_for_ident(str));
            }

            const auto *tc = dynamic_cast<const textview_curses *>(&lv);
            if (tc) {
                const auto &hm = tc->get_highlights();
                auto hl_iter = hm.find({highlight_source_t::PREVIEW, "search"});

                if (hl_iter != hm.end()) {
                    hl_iter->second.annotate(al, 2);
                }
            }
            al.insert(0, filename_width, ' ');
            dst.emplace_back(al);
        }
    }
}
