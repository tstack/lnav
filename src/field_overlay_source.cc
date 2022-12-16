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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "field_overlay_source.hh"

#include "base/ansi_scrubber.hh"
#include "base/humanize.time.hh"
#include "base/snippet_highlighters.hh"
#include "config.h"
#include "log_format_ext.hh"
#include "log_vtab_impl.hh"
#include "md2attr_line.hh"
#include "readline_highlighters.hh"
#include "relative_time.hh"
#include "vtab_module.hh"
#include "vtab_module_json.hh"

json_string extract(const char* str);

void
field_overlay_source::build_field_lines(const listview_curses& lv)
{
    auto& lss = this->fos_lss;
    auto& vc = view_colors::singleton();

    this->fos_lines.clear();

    if (lss.text_line_count() == 0) {
        this->fos_log_helper.clear();

        return;
    }

    content_line_t cl = lss.at(lv.get_selection());
    std::shared_ptr<logfile> file = lss.find(cl);
    auto ll = file->begin() + cl;
    auto format = file->get_format();
    bool display = false;

    if (ll->is_time_skewed()
        || ll->get_msg_level() == log_level_t::LEVEL_INVALID)
    {
        display = true;
    }
    if (!this->fos_contexts.empty()) {
        display = display || this->fos_contexts.top().c_show;
    }

    this->build_meta_line(lv, this->fos_lines, lv.get_top());

    if (!display) {
        return;
    }

    if (!this->fos_log_helper.parse_line(lv.get_selection())) {
        return;
    }

    if (ll->get_msg_level() == LEVEL_INVALID) {
        for (const auto& sattr : this->fos_log_helper.ldh_line_attrs) {
            if (sattr.sa_type != &SA_INVALID) {
                continue;
            }

            auto emsg = fmt::format(
                FMT_STRING("   Invalid log message: {}"),
                sattr.sa_value.get<decltype(SA_INVALID)::value_type>());
            auto al = attr_line_t(emsg)
                          .with_attr(string_attr(
                              line_range{1, 2}, VC_GRAPHIC.value(ACS_LLCORNER)))
                          .with_attr(string_attr(
                              line_range{0, 22},
                              VC_ROLE.value(role_t::VCR_INVALID_MSG)));
            this->fos_lines.emplace_back(al);
        }
    }

    char old_timestamp[64], curr_timestamp[64], orig_timestamp[64];
    struct timeval curr_tv, offset_tv, orig_tv, diff_tv = {0, 0};
    attr_line_t time_line;
    auto& time_str = time_line.get_string();
    struct line_range time_lr;

    sql_strftime(curr_timestamp,
                 sizeof(curr_timestamp),
                 ll->get_time(),
                 ll->get_millis(),
                 'T');

    if (ll->is_time_skewed()) {
        time_lr.lr_start = 1;
        time_lr.lr_end = 2;
        time_line.with_attr(
            string_attr(time_lr, VC_GRAPHIC.value(ACS_LLCORNER)));
        time_str.append("   Out-Of-Time-Order Message");
        time_lr.lr_start = 3;
        time_lr.lr_end = time_str.length();
        time_line.with_attr(
            string_attr(time_lr, VC_ROLE.value(role_t::VCR_SKEWED_TIME)));
        time_str.append(" --");
    }

    time_str.append(" Received Time: ");
    time_lr.lr_start = time_str.length();
    time_str.append(curr_timestamp);
    time_lr.lr_end = time_str.length();
    time_line.with_attr(
        string_attr(time_lr, VC_STYLE.value(text_attrs{A_BOLD})));
    time_str.append(" -- ");
    time_lr.lr_start = time_str.length();
    time_str.append(humanize::time::point::from_tv(ll->get_timeval())
                        .with_convert_to_local(true)
                        .as_precise_time_ago());
    time_lr.lr_end = time_str.length();
    time_line.with_attr(
        string_attr(time_lr, VC_STYLE.value(text_attrs{A_BOLD})));

    struct line_range time_range = find_string_attr_range(
        this->fos_log_helper.ldh_line_attrs, &logline::L_TIMESTAMP);

    curr_tv = this->fos_log_helper.ldh_line->get_timeval();
    if (ll->is_time_skewed() && time_range.lr_end != -1) {
        const char* time_src
            = this->fos_log_helper.ldh_line_values.lvv_sbr.get_data()
            + time_range.lr_start;
        struct timeval actual_tv;
        date_time_scanner dts;
        struct exttm tm;

        dts.set_base_time(format->lf_date_time.dts_base_time,
                          format->lf_date_time.dts_base_tm.et_tm);
        if (format->lf_date_time.scan(time_src,
                                      time_range.length(),
                                      format->get_timestamp_formats(),
                                      &tm,
                                      actual_tv,
                                      false)
            || dts.scan(
                time_src, time_range.length(), nullptr, &tm, actual_tv, false))
        {
            sql_strftime(
                orig_timestamp, sizeof(orig_timestamp), actual_tv, 'T');
            time_str.append(";  Actual Time: ");
            time_lr.lr_start = time_str.length();
            time_str.append(orig_timestamp);
            time_lr.lr_end = time_str.length();
            time_line.with_attr(
                string_attr(time_lr, VC_ROLE.value(role_t::VCR_SKEWED_TIME)));

            timersub(&curr_tv, &actual_tv, &diff_tv);
            time_str.append(";  Diff: ");
            time_lr.lr_start = time_str.length();
            time_str.append(
                humanize::time::duration::from_tv(diff_tv).to_string());
            time_lr.lr_end = time_str.length();
            time_line.with_attr(
                string_attr(time_lr, VC_STYLE.value(text_attrs{A_BOLD})));
        }
    }

    offset_tv = this->fos_log_helper.ldh_file->get_time_offset();
    timersub(&curr_tv, &offset_tv, &orig_tv);
    sql_strftime(old_timestamp,
                 sizeof(old_timestamp),
                 orig_tv.tv_sec,
                 orig_tv.tv_usec / 1000,
                 'T');
    if (offset_tv.tv_sec || offset_tv.tv_usec) {
        time_str.append("  Pre-adjust Time: ");
        time_str.append(old_timestamp);
        fmt::format_to(std::back_inserter(time_str),
                       FMT_STRING("  Offset: {:+}.{:03}"),
                       offset_tv.tv_sec,
                       std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::microseconds(offset_tv.tv_usec))
                           .count());
    }

    if (format->lf_date_time.dts_fmt_lock != -1) {
        const auto* ts_formats = format->get_timestamp_formats();
        if (ts_formats == nullptr) {
            ts_formats = PTIMEC_FORMAT_STR;
        }
        time_line.append("  Format: ")
            .append(lnav::roles::symbol(
                ts_formats[format->lf_date_time.dts_fmt_lock]));
    }

    if ((!this->fos_contexts.empty() && this->fos_contexts.top().c_show)
        || diff_tv.tv_sec > 0)
    {
        this->fos_lines.emplace_back(time_line);
    }

    if (this->fos_contexts.empty() || !this->fos_contexts.top().c_show) {
        return;
    }

    this->fos_known_key_size = LOG_BODY.length();
    if (!this->fos_contexts.empty()) {
        this->fos_known_key_size += this->fos_contexts.top().c_prefix.length();
    }
    this->fos_unknown_key_size = 0;

    for (auto& ldh_line_value : this->fos_log_helper.ldh_line_values.lvv_values)
    {
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
        this->fos_known_key_size
            = std::max(this->fos_known_key_size, this_key_size);
    }

    for (auto iter = this->fos_log_helper.ldh_parser->dp_pairs.begin();
         iter != this->fos_log_helper.ldh_parser->dp_pairs.end();
         ++iter)
    {
        std::string colname
            = this->fos_log_helper.ldh_parser->get_element_string(
                iter->e_sub_elements->front());

        colname
            = this->fos_log_helper.ldh_namer->add_column(colname).to_string();
        this->fos_unknown_key_size
            = std::max(this->fos_unknown_key_size, (int) colname.length());
    }

    auto lf = this->fos_log_helper.ldh_file->get_format();
    if (!lf->get_pattern_regex(cl).empty()) {
        attr_line_t pattern_al;
        std::string& pattern_str = pattern_al.get_string();
        pattern_str = " Pattern: " + lf->get_pattern_path(cl) + " = ";
        int skip = pattern_str.length();
        pattern_str += lf->get_pattern_regex(cl);
        lnav::snippets::regex_highlighter(
            pattern_al,
            pattern_al.length(),
            line_range{skip, (int) pattern_al.length()});
        this->fos_lines.emplace_back(pattern_al);
    }

    if (this->fos_log_helper.ldh_line_values.lvv_values.empty()) {
        this->fos_lines.emplace_back(" No known message fields");
    }

    const log_format* last_format = nullptr;

    for (auto& lv : this->fos_log_helper.ldh_line_values.lvv_values) {
        if (!lv.lv_meta.lvm_format) {
            continue;
        }

        auto* curr_format = lv.lv_meta.lvm_format.value();
        auto* curr_elf = dynamic_cast<external_log_format*>(curr_format);
        const auto format_name = curr_format->get_name().to_string();
        attr_line_t al;
        std::string str, value_str = lv.to_string();

        if (curr_format != last_format) {
            this->fos_lines.emplace_back(" Known message fields for table "
                                         + format_name + ":");
            this->fos_lines.back().with_attr(
                string_attr(line_range(32, 32 + format_name.length()),
                            VC_STYLE.value(vc.attrs_for_ident(format_name)
                                           | text_attrs{A_BOLD})));
            last_format = curr_format;
        }

        std::string field_name, orig_field_name;
        if (lv.lv_meta.lvm_struct_name.empty()) {
            if (curr_elf && curr_elf->elf_body_field == lv.lv_meta.lvm_name) {
                field_name = LOG_BODY;
            } else if (curr_elf
                       && curr_elf->lf_timestamp_field == lv.lv_meta.lvm_name)
            {
                field_name = LOG_TIME;
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
                VC_STYLE.value(vc.attrs_for_ident(orig_field_name))));
        } else {
            al.with_attr(string_attr(
                line_range(8, 8 + lv.lv_meta.lvm_struct_name.size()),
                VC_STYLE.value(
                    vc.attrs_for_ident(lv.lv_meta.lvm_struct_name))));
        }

        this->fos_lines.emplace_back(al);
        this->add_key_line_attrs(this->fos_known_key_size);

        if (lv.lv_meta.lvm_kind == value_kind_t::VALUE_STRUCT) {
            json_string js = extract(value_str.c_str());

            al.clear()
                .append("   extract(")
                .append(lv.lv_meta.lvm_name.get(),
                        VC_STYLE.value(vc.attrs_for_ident(lv.lv_meta.lvm_name)))
                .append(")")
                .append(this->fos_known_key_size - lv.lv_meta.lvm_name.size()
                            - 9 + 3,
                        ' ')
                .append(" = ")
                .append(
                    string_fragment::from_bytes(js.js_content.in(), js.js_len));
            this->fos_lines.emplace_back(al);
            this->add_key_line_attrs(this->fos_known_key_size);
        }
    }

    std::map<const intern_string_t, json_ptr_walk::walk_list_t>::iterator
        json_iter;

    if (!this->fos_log_helper.ldh_json_pairs.empty()) {
        this->fos_lines.emplace_back(" JSON fields:");
    }

    for (json_iter = this->fos_log_helper.ldh_json_pairs.begin();
         json_iter != this->fos_log_helper.ldh_json_pairs.end();
         ++json_iter)
    {
        json_ptr_walk::walk_list_t& jpairs = json_iter->second;

        for (size_t lpc = 0; lpc < jpairs.size(); lpc++) {
            this->fos_lines.emplace_back(
                "   "
                + this->fos_log_helper.format_json_getter(json_iter->first, lpc)
                + " = " + jpairs[lpc].wt_value);
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
        xp_call = sqlite3_mprintf(
            "xpath(%Q, %s)", xml_pair.first.second.c_str(), qname.in());
        this->fos_lines.emplace_back(
            fmt::format(FMT_STRING("   {} = {}"), xp_call, xml_pair.second));
        this->add_key_line_attrs(0);
    }

    if (!this->fos_contexts.empty()
        && !this->fos_contexts.top().c_show_discovered)
    {
        return;
    }

    if (this->fos_log_helper.ldh_parser->dp_pairs.empty()) {
        this->fos_lines.emplace_back(" No discovered message fields");
    } else {
        this->fos_lines.emplace_back(
            " Discovered fields for logline table from message format: ");
        this->fos_lines.back().with_attr(
            string_attr(line_range(23, 23 + 7),
                        VC_STYLE.value(vc.attrs_for_ident("logline"))));
        auto& al = this->fos_lines.back();
        auto& disc_str = al.get_string();

        al.with_attr(string_attr(line_range(disc_str.length(), -1),
                                 VC_STYLE.value(text_attrs{A_BOLD})));
        disc_str.append(this->fos_log_helper.ldh_msg_format);
    }

    auto iter = this->fos_log_helper.ldh_parser->dp_pairs.begin();
    for (size_t lpc = 0; lpc < this->fos_log_helper.ldh_parser->dp_pairs.size();
         lpc++, ++iter)
    {
        auto name = this->fos_log_helper.ldh_namer->cn_names[lpc];
        auto val = this->fos_log_helper.ldh_parser->get_element_string(
            iter->e_sub_elements->back());
        attr_line_t al(fmt::format(FMT_STRING("   {} = {}"), name, val));

        al.with_attr(
            string_attr(line_range(3, 3 + name.length()),
                        VC_STYLE.value(vc.attrs_for_ident(name.to_string()))));

        this->fos_lines.emplace_back(al);
        this->add_key_line_attrs(
            this->fos_unknown_key_size,
            lpc == (this->fos_log_helper.ldh_parser->dp_pairs.size() - 1));
    }
}

void
field_overlay_source::build_meta_line(const listview_curses& lv,
                                      std::vector<attr_line_t>& dst,
                                      vis_line_t row)
{
    auto line_meta_opt = this->fos_lss.find_bookmark_metadata(row);

    if (!line_meta_opt) {
        return;
    }
    auto& vc = view_colors::singleton();
    const auto& line_meta = *(line_meta_opt.value());
    size_t filename_width = this->fos_lss.get_filename_offset();
    const auto* tc = dynamic_cast<const textview_curses*>(&lv);

    if (!line_meta.bm_comment.empty()) {
        const auto* lead = line_meta.bm_tags.empty() ? " \u2514 " : " \u251c ";
        md2attr_line mdal;
        attr_line_t al;

        auto parse_res = md4cpp::parse(line_meta.bm_comment, mdal);
        if (parse_res.isOk()) {
            al = parse_res.unwrap();
        } else {
            log_error("%d: cannot convert comment to markdown: %s",
                      (int) row,
                      parse_res.unwrapErr().c_str());
            al = line_meta.bm_comment;
        }

        auto comment_lines = al.rtrim().split_lines();
        for (size_t lpc = 0; lpc < comment_lines.size(); lpc++) {
            auto& comment_line = comment_lines[lpc];

            if (lpc == 0 && comment_line.empty()) {
                continue;
            }
            comment_line.with_attr_for_all(VC_ROLE.value(role_t::VCR_COMMENT));
            comment_line.insert(
                0, lpc == comment_lines.size() - 1 ? lead : " \u2502 ");
            comment_line.insert(0, filename_width, ' ');
            if (tc != nullptr) {
                auto hl = tc->get_highlights();
                auto hl_iter = hl.find({highlight_source_t::PREVIEW, "search"});

                if (hl_iter != hl.end()) {
                    hl_iter->second.annotate(comment_line, filename_width);
                }
            }

            dst.emplace_back(comment_line);
        }
    }
    if (!line_meta.bm_tags.empty()) {
        attr_line_t al;

        al.with_string(" \u2514");
        for (const auto& str : line_meta.bm_tags) {
            al.append(1, ' ').append(str,
                                     VC_STYLE.value(vc.attrs_for_ident(str)));
        }

        if (tc != nullptr) {
            const auto& hm = tc->get_highlights();
            auto hl_iter = hm.find({highlight_source_t::PREVIEW, "search"});

            if (hl_iter != hm.end()) {
                hl_iter->second.annotate(al, 2);
            }
        }
        al.insert(0, filename_width, ' ');
        if (tc != nullptr) {
            auto hl = tc->get_highlights();
            auto hl_iter = hl.find({highlight_source_t::PREVIEW, "search"});

            if (hl_iter != hl.end()) {
                hl_iter->second.annotate(al, filename_width);
            }
        }
        dst.emplace_back(al);
    }
}

void
field_overlay_source::add_key_line_attrs(int key_size, bool last_line)
{
    string_attrs_t& sa = this->fos_lines.back().get_attrs();
    struct line_range lr(1, 2);
    int64_t graphic = (int64_t) (last_line ? ACS_LLCORNER : ACS_LTEE);
    sa.emplace_back(lr, VC_GRAPHIC.value(graphic));

    lr.lr_start = 3 + key_size + 3;
    lr.lr_end = -1;
    sa.emplace_back(lr, VC_STYLE.value(text_attrs{A_BOLD}));
}

bool
field_overlay_source::list_value_for_overlay(const listview_curses& lv,
                                             int y,
                                             int bottom,
                                             vis_line_t row,
                                             attr_line_t& value_out)
{
    if (y == 0) {
        this->build_field_lines(lv);
        return false;
    }

    if (1 <= y && y <= (int) this->fos_lines.size()) {
        value_out = this->fos_lines[y - 1];
        return true;
    }

    if (!this->fos_meta_lines.empty() && this->fos_meta_lines_row == row - 1_vl)
    {
        value_out = this->fos_meta_lines.front();
        this->fos_meta_lines.erase(this->fos_meta_lines.begin());

        return true;
    }

    if (row < lv.get_inner_height()) {
        this->fos_meta_lines.clear();
        this->build_meta_line(lv, this->fos_meta_lines, row);
        this->fos_meta_lines_row = row;
    }

    return false;
}
