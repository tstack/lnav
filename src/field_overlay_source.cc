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

#include "base/humanize.time.hh"
#include "base/snippet_highlighters.hh"
#include "command_executor.hh"
#include "config.h"
#include "log.annotate.hh"
#include "log_format_ext.hh"
#include "log_vtab_impl.hh"
#include "md2attr_line.hh"
#include "msg.text.hh"
#include "ptimec.hh"
#include "readline_highlighters.hh"
#include "sql_util.hh"
#include "vtab_module.hh"
#include "vtab_module_json.hh"

using namespace md4cpp::literals;
using namespace lnav::roles::literals;

json_string extract(const char* str);

void
field_overlay_source::build_field_lines(const listview_curses& lv,
                                        vis_line_t row)
{
    auto& lss = this->fos_lss;
    auto& vc = view_colors::singleton();

    this->fos_lines.clear();
    this->fos_row_to_field_meta.clear();

    if (lss.text_line_count() == 0) {
        this->fos_log_helper.clear();

        return;
    }

    auto cl = lss.at(row);
    auto file = lss.find(cl);
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

    if (!display) {
        return;
    }

    if (!this->fos_log_helper.parse_line(row)) {
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
            auto al
                = attr_line_t(emsg)
                      .with_attr(string_attr(line_range{1, 2},
                                             VC_GRAPHIC.value(NCACS_LLCORNER)))
                      .with_attr(
                          string_attr(line_range{0, 22},
                                      VC_ROLE.value(role_t::VCR_INVALID_MSG)))
                      .move();
            this->fos_lines.emplace_back(al);
        }
    }

    char old_timestamp[64], curr_timestamp[64], orig_timestamp[64];
    timeval curr_tv, offset_tv, orig_tv, diff_tv = {0, 0};
    attr_line_t time_line;
    auto& time_str = time_line.get_string();
    line_range time_lr;
    off_t ts_len = sql_strftime(
        curr_timestamp, sizeof(curr_timestamp), ll->get_timeval(), 'T');
    {
        exttm tmptm;

        tmptm.et_flags |= ETF_ZONE_SET;
        tmptm.et_gmtoff
            = lnav::local_time_to_info(
                  date::local_seconds{ll->get_time<std::chrono::seconds>()})
                  .first.offset.count();
        ftime_z(curr_timestamp, ts_len, sizeof(curr_timestamp), tmptm);
        curr_timestamp[ts_len] = '\0';
    }

    if (ll->is_time_skewed()) {
        time_lr.lr_start = 1;
        time_lr.lr_end = 2;
        time_line.with_attr(
            string_attr(time_lr, VC_GRAPHIC.value(NCACS_LLCORNER)));
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
        string_attr(time_lr, VC_STYLE.value(text_attrs::with_bold())));
    time_str.append(" \u2014 ");
    time_lr.lr_start = time_str.length();
    time_str.append(humanize::time::point::from_tv(ll->get_timeval())
                        .with_convert_to_local(true)
                        .as_precise_time_ago());
    time_lr.lr_end = time_str.length();
    time_line.with_attr(
        string_attr(time_lr, VC_STYLE.value(text_attrs::with_bold())));

    auto time_range = find_string_attr_range(
        this->fos_log_helper.ldh_line_attrs, &L_TIMESTAMP);

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
        dts.dts_zoned_to_local = format->lf_date_time.dts_zoned_to_local;
        if (format->lf_date_time.scan(time_src,
                                      time_range.length(),
                                      format->get_timestamp_formats(),
                                      &tm,
                                      actual_tv)
            || dts.scan(time_src, time_range.length(), nullptr, &tm, actual_tv))
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
                string_attr(time_lr, VC_STYLE.value(text_attrs::with_bold())));
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
                ts_formats[format->lf_date_time.dts_fmt_lock]))
            .append("  Default Zone: ");
        if (format->lf_date_time.dts_default_zone != nullptr) {
            time_line.append(lnav::roles::symbol(
                format->lf_date_time.dts_default_zone->name()));
        } else {
            time_line.append("none"_comment);
        }

        auto file_opts = file->get_file_options();
        if (file_opts) {
            time_line.append("  File Options: ")
                .append(lnav::roles::file(file_opts->first));
        }
    }

    if ((!this->fos_contexts.empty() && this->fos_contexts.top().c_show)
        || diff_tv.tv_sec > 0 || ll->is_time_skewed())
    {
        this->fos_lines.emplace_back(time_line);
    }

    if (this->fos_contexts.empty() || !this->fos_contexts.top().c_show) {
        return;
    }

    auto anchor_opt = this->fos_lss.anchor_for_row(row);
    if (anchor_opt) {
        auto permalink
            = attr_line_t(" Permalink: ")
                  .append(lnav::roles::hyperlink(anchor_opt.value()));
        this->fos_row_to_field_meta.emplace(
            this->fos_lines.size(), row_info{std::nullopt, anchor_opt.value()});
        this->fos_lines.emplace_back(permalink);
    }

    this->fos_known_key_size = LOG_BODY.length();
    if (!this->fos_contexts.empty()) {
        this->fos_known_key_size += this->fos_contexts.top().c_prefix.length();
    }
    this->fos_unknown_key_size = 0;

    for (const auto& ldh_line_value :
         this->fos_log_helper.ldh_line_values.lvv_values)
    {
        auto& meta = ldh_line_value.lv_meta;
        int this_key_size = meta.lvm_name.size();

        if (!meta.lvm_column.is<logline_value_meta::table_column>()) {
            continue;
        }

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

    for (const auto& lv : this->fos_log_helper.ldh_line_values.lvv_values) {
        const auto& meta = lv.lv_meta;
        if (!meta.lvm_format) {
            continue;
        }

        if (!meta.lvm_column.is<logline_value_meta::table_column>()) {
            continue;
        }

        auto* curr_format = meta.lvm_format.value();
        auto* curr_elf = dynamic_cast<external_log_format*>(curr_format);
        const auto format_name = curr_format->get_name().to_string();
        attr_line_t al;
        auto value_str = lv.to_string();

        if (curr_format != last_format) {
            this->fos_lines.emplace_back(" Known message fields for table "
                                         + format_name + ":");
            this->fos_lines.back().with_attr(
                string_attr(line_range(32, 32 + format_name.length()),
                            VC_STYLE.value(vc.attrs_for_ident(format_name)
                                           | text_attrs::style::bold)));
            last_format = curr_format;
        }

        std::string field_name, orig_field_name;
        line_range hl_range;
        al.append(" ").append("|", VC_GRAPHIC.value(NCACS_LTEE)).append(" ");
        if (meta.lvm_struct_name.empty()) {
            if (curr_elf && curr_elf->elf_body_field == meta.lvm_name) {
                field_name = LOG_BODY;
            } else if (curr_elf
                       && curr_elf->lf_timestamp_field == meta.lvm_name)
            {
                field_name = LOG_TIME;
            } else {
                field_name = meta.lvm_name.to_string();
            }
            orig_field_name = field_name;
            if (!this->fos_contexts.empty()) {
                field_name = this->fos_contexts.top().c_prefix + field_name;
            }
            if (meta.is_hidden()) {
                al.append("\u25c7"_comment);
            } else {
                al.append("\u25c6"_ok);
            }
            al.append(" ");

            switch (meta.to_chart_type()) {
                case chart_type_t::none:
                    al.append("   ");
                    break;
                case chart_type_t::hist:
                case chart_type_t::spectro:
                    al.append(":bar_chart:"_emoji).append(" ");
                    break;
            }
            auto prefix_len = al.column_width();
            hl_range.lr_start = al.get_string().length();
            al.append(field_name);
            hl_range.lr_end = al.get_string().length();
            al.pad_to(prefix_len + this->fos_known_key_size);

            this->fos_row_to_field_meta.emplace(this->fos_lines.size(),
                                                row_info{meta, value_str});
        } else {
            auto jget_str = lnav::sql::mprintf("jget(%s, '/%q')",
                                               meta.lvm_struct_name.get(),
                                               meta.lvm_name.get());
            hl_range.lr_start = al.get_string().length();
            al.append(jget_str.in());
            hl_range.lr_end = al.get_string().length();

            this->fos_row_to_field_meta.emplace(
                this->fos_lines.size(), row_info{std::nullopt, value_str});
        }
        readline_sqlite_highlighter_int(al, std::nullopt, hl_range);

        al.append(" = ").append(scrub_ws(value_str.c_str()));

        this->fos_lines.emplace_back(al);

        if (meta.lvm_kind == value_kind_t::VALUE_STRUCT) {
            json_string js = extract(value_str.c_str());

            al.clear()
                .append("   extract(")
                .append(meta.lvm_name.get(),
                        VC_STYLE.value(vc.attrs_for_ident(meta.lvm_name)))
                .append(")")
                .append(this->fos_known_key_size - meta.lvm_name.size() - 9 + 3,
                        ' ')
                .append(" = ")
                .append(scrub_ws(string_fragment::from_bytes(js.js_content.in(),
                                                             js.js_len)));
            this->fos_lines.emplace_back(al);
            this->add_key_line_attrs(this->fos_known_key_size);
        }
    }

    if (!this->fos_log_helper.ldh_extra_json.empty()
        || !this->fos_log_helper.ldh_json_pairs.empty())
    {
        this->fos_lines.emplace_back(" JSON fields:");
    }

    for (const auto& extra_pair : this->fos_log_helper.ldh_extra_json) {
        auto qname = lnav::sql::mprintf("%Q", extra_pair.first.c_str());
        auto key_line = attr_line_t("   jget(log_raw_text, ")
                            .append(qname.in())
                            .append(")")
                            .move();
        readline_sqlite_highlighter(key_line, std::nullopt);
        auto key_size = key_line.length();
        key_line.append(" = ").append(scrub_ws(extra_pair.second));
        this->fos_row_to_field_meta.emplace(this->fos_lines.size(),
                                            row_info{
                                                std::nullopt,
                                                extra_pair.second,
                                            });
        this->fos_lines.emplace_back(key_line);
        this->add_key_line_attrs(key_size - 3);
    }

    for (const auto& jpairs_map : this->fos_log_helper.ldh_json_pairs) {
        const auto& jpairs = jpairs_map.second;

        for (size_t lpc = 0; lpc < jpairs.size(); lpc++) {
            auto key_line = attr_line_t("   ")
                                .append(this->fos_log_helper.format_json_getter(
                                    jpairs_map.first, lpc))
                                .move();
            readline_sqlite_highlighter(key_line, std::nullopt);
            auto key_size = key_line.length();
            key_line.append(" = ").append(scrub_ws(jpairs[lpc].wt_value));
            this->fos_row_to_field_meta.emplace(
                this->fos_lines.size(),
                row_info{std::nullopt, jpairs[lpc].wt_value});
            this->fos_lines.emplace_back(key_line);
            this->add_key_line_attrs(key_size - 3);
        }
    }

    if (!this->fos_log_helper.ldh_xml_pairs.empty()) {
        this->fos_lines.emplace_back(" XML fields:");
    }

    for (const auto& xml_pair : this->fos_log_helper.ldh_xml_pairs) {
        auto qname = sql_quote_ident(xml_pair.first.first.get());
        auto xp_call = lnav::sql::mprintf(
            "xpath(%Q, %s.%s)",
            xml_pair.first.second.c_str(),
            this->fos_log_helper.ldh_file->get_format()->get_name().c_str(),
            qname.in());
        auto key_line = attr_line_t("   ").append(xp_call.in()).move();
        readline_sqlite_highlighter(key_line, std::nullopt);
        auto key_size = key_line.length();
        key_line.append(" = ").append(scrub_ws(xml_pair.second));
        this->fos_row_to_field_meta.emplace(
            this->fos_lines.size(), row_info{std::nullopt, xml_pair.second});
        this->fos_lines.emplace_back(key_line);
        this->add_key_line_attrs(key_size - 3);
    }

    if (this->fos_log_helper.ldh_parser->dp_pairs.empty()) {
        this->fos_lines.emplace_back(" No discovered message fields");
    } else {
        this->fos_lines.emplace_back(
            " Discovered fields for logline table from message "
            "format: ");
        this->fos_lines.back().with_attr(
            string_attr(line_range(23, 23 + 7),
                        VC_STYLE.value(vc.attrs_for_ident("logline"))));
        auto& al = this->fos_lines.back();
        auto& disc_str = al.get_string();

        al.with_attr(string_attr(line_range(disc_str.length(), -1),
                                 VC_STYLE.value(text_attrs::with_bold())));
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

        this->fos_row_to_field_meta.emplace(this->fos_lines.size(),
                                            row_info{std::nullopt, val});
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

    if (!this->fos_contexts.empty()
        && this->fos_contexts.top().c_show_applicable_annotations)
    {
        if (this->fos_index_generation != this->fos_lss.lss_index_generation) {
            this->fos_anno_cache.clear();
            this->fos_index_generation = this->fos_lss.lss_index_generation;
        }

        auto file_and_line = this->fos_lss.find_line_with_file(row);

        if (file_and_line && !file_and_line->second->is_continued()) {
            auto get_res = this->fos_anno_cache.get(row);
            if (get_res) {
                auto anno_val = get_res.value();
                if (anno_val) {
                    dst.emplace_back(anno_val.value());
                }
            } else {
                auto applicable_anno = lnav::log::annotate::applicable(row);
                if (!applicable_anno.empty()
                    && (!line_meta_opt
                        || line_meta_opt.value()
                               ->bm_annotations.la_pairs.empty()))
                {
                    auto anno_msg
                        = attr_line_t(" ")
                              .append(":memo:"_emoji)
                              .append(" Annotations available, ")
                              .append(lv.get_selection() == row
                                          ? "use "
                                          : "focus on this line and use ")
                              .append(":annotate"_quoted_code)
                              .append(" to apply them")
                              .append(lv.get_selection() == row
                                          ? " to this line"
                                          : "")
                              .with_attr_for_all(
                                  VC_ROLE.value(role_t::VCR_COMMENT))
                              .move();

                    this->fos_anno_cache.put(row, anno_msg);
                    dst.emplace_back(anno_msg);
                } else {
                    this->fos_anno_cache.put(row, std::nullopt);
                }
            }
        }
    }

    if (!line_meta_opt) {
        return;
    }
    const auto* tc = dynamic_cast<const textview_curses*>(&lv);
    auto& vc = view_colors::singleton();
    const auto& line_meta = *(line_meta_opt.value());
    size_t filename_width = this->fos_lss.get_filename_offset();

    auto file_and_line = this->fos_lss.find_line_with_file(row);
    auto* format = file_and_line->first->get_format_ptr();
    auto field_states = format->get_field_states();
    auto show_opid = false;
    auto field_iter = field_states.find(log_format::LOG_OPID_STR);
    if (field_iter != field_states.end() && !field_iter->second.is_hidden()) {
        show_opid = true;
    }
    if (row == tc->get_selection() && !this->fos_contexts.empty()
        && this->fos_contexts.top().c_show)
    {
        show_opid = true;
    }
    if (show_opid && !line_meta.bm_opid.empty()) {
        auto al = attr_line_t()
                      .append(" Op ID: "_table_header)
                      .append(lnav::roles::identifier(line_meta.bm_opid))
                      .move();

        dst.emplace_back(al);
    }

    if (!line_meta.bm_comment.empty()) {
        const auto* lead = line_meta.bm_tags.empty() ? " \u2514 " : " \u251c ";
        md2attr_line mdal;
        attr_line_t al;

        auto comment_id = intern_string::lookup(fmt::format(
            FMT_STRING("{}-line{}-comment"),
            file_and_line->first->get_filename().filename().string(),
            std::distance(file_and_line->first->begin(),
                          file_and_line->second)));
        mdal.with_source_id(comment_id);
        if (tc->tc_interactive) {
            mdal.add_lnav_script_icons();
        }
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
        if (comment_lines.back().empty()) {
            comment_lines.pop_back();
        }
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
    if (!line_meta.bm_annotations.la_pairs.empty()) {
        for (const auto& anno_pair : line_meta.bm_annotations.la_pairs) {
            attr_line_t al;
            md2attr_line mdal;

            mdal.add_lnav_script_icons();
            dst.push_back(
                attr_line_t()
                    .append(filename_width, ' ')
                    .appendf(FMT_STRING(" \u251c {}:"), anno_pair.first)
                    .with_attr_for_all(VC_ROLE.value(role_t::VCR_COMMENT)));

            auto parse_res = md4cpp::parse(anno_pair.second, mdal);
            if (parse_res.isOk()) {
                al.append(parse_res.unwrap());
            } else {
                log_error("%d: cannot convert annotation to markdown: %s",
                          (int) row,
                          parse_res.unwrapErr().c_str());
                al.append(anno_pair.second);
            }

            auto anno_lines = al.rtrim().split_lines();
            if (anno_lines.back().empty()) {
                anno_lines.pop_back();
            }
            for (size_t lpc = 0; lpc < anno_lines.size(); lpc++) {
                auto& anno_line = anno_lines[lpc];

                if (lpc == 0 && anno_line.empty()) {
                    continue;
                }
                // anno_line.with_attr_for_all(VC_ROLE.value(role_t::VCR_COMMENT));
                anno_line.insert(0,
                                 lpc == anno_lines.size() - 1
                                     ? " \u2570 "_comment
                                     : " \u2502 "_comment);
                anno_line.insert(0, filename_width, ' ');
                if (tc != nullptr) {
                    auto hl = tc->get_highlights();
                    auto hl_iter
                        = hl.find({highlight_source_t::PREVIEW, "search"});

                    if (hl_iter != hl.end()) {
                        hl_iter->second.annotate(anno_line, filename_width);
                    }
                }

                dst.emplace_back(anno_line);
            }
        }
    }
}

void
field_overlay_source::add_key_line_attrs(int key_size, bool last_line)
{
    auto& sa = this->fos_lines.back().get_attrs();
    struct line_range lr(1, 2);

    auto graphic = (last_line ? NCACS_LLCORNER : NCACS_LTEE);
    sa.emplace_back(lr, VC_GRAPHIC.value(graphic));

    lr.lr_start = 3 + key_size + 3;
    lr.lr_end = -1;
    sa.emplace_back(lr, VC_STYLE.value(text_attrs::with_bold()));
}

void
field_overlay_source::list_value_for_overlay(
    const listview_curses& lv,
    vis_line_t row,
    std::vector<attr_line_t>& value_out)
{
    // log_debug("value for overlay %d", row);
    if (row == lv.get_selection()) {
        this->build_field_lines(lv, row);
        value_out = this->fos_lines;
    }
    this->build_meta_line(lv, value_out, row);
}

bool
field_overlay_source::list_static_overlay(const listview_curses& lv,
                                          int y,
                                          int bottom,
                                          attr_line_t& value_out)
{
    const std::vector<attr_line_t>* lines = nullptr;
    if (this->fos_lss.text_line_count() == 0) {
        if (this->fos_tss.empty()) {
            lines = lnav::messages::view::no_files();
        } else {
            lines = lnav::messages::view::only_text_files();
        }
    }

    if (lines != nullptr && y < (ssize_t) lines->size()) {
        value_out = lines->at(y);
        value_out.with_attr_for_all(VC_ROLE.value(role_t::VCR_STATUS));
        if (y == (ssize_t) lines->size() - 1) {
            value_out.with_attr_for_all(
                VC_STYLE.value(text_attrs::with_underline()));
        }
        return true;
    }

    return false;
}

std::optional<attr_line_t>
field_overlay_source::list_header_for_overlay(const listview_curses& lv,
                                              vis_line_t vl)
{
    attr_line_t retval;

    retval.append(this->fos_lss.get_filename_offset(), ' ');
    if (this->fos_contexts.top().c_show) {
        retval
            .appendf(FMT_STRING("\u258C Line {:L} parser details.  "
                                "Press "),
                     (int) vl)
            .append("p"_hotkey)
            .append(" to hide this panel.");
    } else {
        retval.append("\u258C Line ")
            .append(
                lnav::roles::number(fmt::format(FMT_STRING("{:L}"), (int) vl)))
            .append(" metadata");
    }

    if (lv.get_overlay_selection()) {
        retval.append("  ")
            .append("SPC"_hotkey)
            .append(": hide/show field  ")
            .append("c"_hotkey)
            .append(": copy field value  ")
            .append("Esc"_hotkey)
            .append(": exit this panel");
    } else {
        retval.append("  Press ")
            .append("CTRL-]"_hotkey)
            .append(" to focus on this panel");
    }

    return retval;
}
