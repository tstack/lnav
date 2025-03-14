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
#include <chrono>
#include <future>

#include "logfile_sub_source.hh"

#include <sqlite3.h>

#include "base/ansi_scrubber.hh"
#include "base/ansi_vars.hh"
#include "base/fs_util.hh"
#include "base/injector.hh"
#include "base/itertools.hh"
#include "base/string_util.hh"
#include "bookmarks.json.hh"
#include "command_executor.hh"
#include "config.h"
#include "field_overlay_source.hh"
#include "hasher.hh"
#include "k_merge_tree.h"
#include "lnav_util.hh"
#include "log_accel.hh"
#include "logfile_sub_source.cfg.hh"
#include "md2attr_line.hh"
#include "ptimec.hh"
#include "scn/scan.h"
#include "shlex.hh"
#include "sql_util.hh"
#include "vtab_module.hh"
#include "yajlpp/yajlpp.hh"
#include "yajlpp/yajlpp_def.hh"

using namespace std::chrono_literals;
using namespace lnav::roles::literals;

const bookmark_type_t logfile_sub_source::BM_FILES("file");

static int
pretty_sql_callback(exec_context& ec, sqlite3_stmt* stmt)
{
    if (!sqlite3_stmt_busy(stmt)) {
        return 0;
    }

    const auto ncols = sqlite3_column_count(stmt);

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

    for (int lpc = 0; lpc < ncols; lpc++) {
        const auto* colname = sqlite3_column_name(stmt, lpc);
        auto* raw_value = sqlite3_column_value(stmt, lpc);
        auto value_type = sqlite3_value_type(raw_value);
        scoped_value_t value;

        switch (value_type) {
            case SQLITE_INTEGER:
                value = (int64_t) sqlite3_value_int64(raw_value);
                break;
            case SQLITE_FLOAT:
                value = sqlite3_value_double(raw_value);
                break;
            case SQLITE_NULL:
                value = null_value_t{};
                break;
            default:
                value = string_fragment::from_bytes(
                    sqlite3_value_text(raw_value),
                    sqlite3_value_bytes(raw_value));
                break;
        }
        if (!ec.ec_local_vars.empty() && !ec.ec_dry_run) {
            if (sql_ident_needs_quote(colname)) {
                continue;
            }
            auto& vars = ec.ec_local_vars.top();

            if (vars.find(colname) != vars.end()) {
                continue;
            }

            if (value.is<string_fragment>()) {
                value = value.get<string_fragment>().to_string();
            }
            vars[colname] = value;
        }
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
    : text_sub_source(1), lnav_config_listener(__FILE__),
      lss_meta_grepper(*this), lss_location_history(*this)
{
    this->tss_supports_filtering = true;
    this->clear_line_size_cache();
    this->clear_min_max_row_times();
}

std::shared_ptr<logfile>
logfile_sub_source::find(const char* fn, content_line_t& line_base)
{
    std::shared_ptr<logfile> retval = nullptr;

    line_base = content_line_t(0);
    for (auto iter = this->lss_files.begin();
         iter != this->lss_files.end() && retval == nullptr;
         ++iter)
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

struct filtered_logline_cmp {
    filtered_logline_cmp(const logfile_sub_source& lc) : llss_controller(lc) {}

    bool operator()(const uint32_t& lhs, const uint32_t& rhs) const
    {
        auto cl_lhs = (content_line_t) llss_controller.lss_index[lhs];
        auto cl_rhs = (content_line_t) llss_controller.lss_index[rhs];
        auto ll_lhs = this->llss_controller.find_line(cl_lhs);
        auto ll_rhs = this->llss_controller.find_line(cl_rhs);

        if (ll_lhs == nullptr) {
            return true;
        }
        if (ll_rhs == nullptr) {
            return false;
        }
        return (*ll_lhs) < (*ll_rhs);
    }

    bool operator()(const uint32_t& lhs, const timeval& rhs) const
    {
        const auto cl_lhs = (content_line_t) llss_controller.lss_index[lhs];
        const auto* ll_lhs = this->llss_controller.find_line(cl_lhs);

        if (ll_lhs == nullptr) {
            return true;
        }
        return (*ll_lhs) < rhs;
    }

    const logfile_sub_source& llss_controller;
};

std::optional<vis_line_t>
logfile_sub_source::find_from_time(const timeval& start) const
{
    const auto lb = std::lower_bound(this->lss_filtered_index.begin(),
                                     this->lss_filtered_index.end(),
                                     start,
                                     filtered_logline_cmp(*this));
    if (lb != this->lss_filtered_index.end()) {
        auto retval = std::distance(this->lss_filtered_index.begin(), lb);
        return vis_line_t(retval);
    }

    return std::nullopt;
}

line_info
logfile_sub_source::text_value_for_line(textview_curses& tc,
                                        int row,
                                        std::string& value_out,
                                        line_flags_t flags)
{
    if (this->lss_indexing_in_progress) {
        value_out = "";
        return {};
    }

    line_info retval;
    content_line_t line(0);

    require_ge(row, 0);
    require_lt((size_t) row, this->lss_filtered_index.size());

    line = this->at(vis_line_t(row));

    if (flags & RF_RAW) {
        auto lf = this->find(line);
        auto ll = lf->begin() + line;
        retval.li_file_range = lf->get_file_range(ll, false);
        retval.li_level = ll->get_msg_level();
        // retval.li_timestamp = ll->get_timeval();
        retval.li_partial = false;
        retval.li_utf8_scan_result.usr_has_ansi = ll->has_ansi();
        retval.li_utf8_scan_result.usr_message = ll->is_valid_utf() ? nullptr
                                                                    : "bad";
        // timeval start_time, end_time;
        // gettimeofday(&start_time, NULL);
        value_out = lf->read_line(lf->begin() + line)
                        .map([](auto sbr) { return to_string(sbr); })
                        .unwrapOr({});
        // gettimeofday(&end_time, NULL);
        // timeval diff = end_time - start_time;
        // log_debug("read time %d.%06d %s:%d", diff.tv_sec, diff.tv_usec,
        // lf->get_filename().c_str(), line);
        return retval;
    }

    require_false(this->lss_in_value_for_line);

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

    auto& sbr = this->lss_token_values.lvv_sbr;

    sbr.share(this->lss_share_manager,
              (char*) this->lss_token_value.c_str(),
              this->lss_token_value.size());
    format->annotate(this->lss_token_file.get(),
                     line,
                     this->lss_token_attrs,
                     this->lss_token_values);
    if (flags & RF_REWRITE) {
        exec_context ec(
            &this->lss_token_values, pretty_sql_callback, pretty_pipe_callback);
        std::string rewritten_line;
        db_label_source rewrite_label_source;

        ec.with_perms(exec_context::perm_t::READ_ONLY);
        ec.ec_local_vars.push(std::map<std::string, scoped_value_t>());
        ec.ec_top_line = vis_line_t(row);
        ec.ec_label_source_stack.push_back(&rewrite_label_source);
        add_ansi_vars(ec.ec_global_vars);
        add_global_vars(ec);
        format->rewrite(ec, sbr, this->lss_token_attrs, rewritten_line);
        this->lss_token_value.assign(rewritten_line);
        value_out = this->lss_token_value;
    }

    {
        auto lr = line_range{0, (int) this->lss_token_value.length()};
        this->lss_token_attrs.emplace_back(lr, SA_ORIGINAL_LINE.value());
    }

    std::optional<exttm> adjusted_tm;
    auto time_attr = find_string_attr(this->lss_token_attrs, &L_TIMESTAMP);
    if (!this->lss_token_line->is_continued() && !format->lf_formatted_lines
        && (this->lss_token_file->is_time_adjusted()
            || ((format->lf_timestamp_flags & ETF_ZONE_SET
                 || format->lf_date_time.dts_default_zone != nullptr)
                && format->lf_date_time.dts_zoned_to_local)
            || format->lf_timestamp_flags & ETF_MACHINE_ORIENTED
            || !(format->lf_timestamp_flags & ETF_DAY_SET)
            || !(format->lf_timestamp_flags & ETF_MONTH_SET))
        && format->lf_date_time.dts_fmt_lock != -1)
    {
        if (time_attr != this->lss_token_attrs.end()) {
            const auto time_range = time_attr->sa_range;
            const auto time_sf = string_fragment::from_str_range(
                this->lss_token_value, time_range.lr_start, time_range.lr_end);
            adjusted_tm = format->tm_for_display(this->lss_token_line, time_sf);

            char buffer[128];
            const char* fmt;
            ssize_t len;

            if (format->lf_timestamp_flags & ETF_MACHINE_ORIENTED
                || !(format->lf_timestamp_flags & ETF_DAY_SET)
                || !(format->lf_timestamp_flags & ETF_MONTH_SET))
            {
                if (format->lf_timestamp_flags & ETF_NANOS_SET) {
                    fmt = "%Y-%m-%d %H:%M:%S.%N";
                } else if (format->lf_timestamp_flags & ETF_MICROS_SET) {
                    fmt = "%Y-%m-%d %H:%M:%S.%f";
                } else if (format->lf_timestamp_flags & ETF_MILLIS_SET) {
                    fmt = "%Y-%m-%d %H:%M:%S.%L";
                } else {
                    fmt = "%Y-%m-%d %H:%M:%S";
                }
                len = ftime_fmt(
                    buffer, sizeof(buffer), fmt, adjusted_tm.value());
            } else {
                len = format->lf_date_time.ftime(
                    buffer,
                    sizeof(buffer),
                    format->get_timestamp_formats(),
                    adjusted_tm.value());
            }

            value_out.replace(
                time_range.lr_start, time_range.length(), buffer, len);
            this->lss_token_shift_start = time_range.lr_start;
            this->lss_token_shift_size = len - time_range.length();
        }
    }

    // Insert space for the file/search-hit markers.
    value_out.insert(0, 1, ' ');
    this->lss_time_column_size = 0;
    if (this->lss_line_context == line_context_t::time_column) {
        if (time_attr != this->lss_token_attrs.end()) {
            this->lss_token_attrs.emplace_back(time_attr->sa_range,
                                               SA_REPLACED.value());

            const char* fmt;
            if (this->lss_all_timestamp_flags
                & (ETF_MICROS_SET | ETF_NANOS_SET))
            {
                fmt = "%H:%M:%S.%f";
            } else if (this->lss_all_timestamp_flags & ETF_MILLIS_SET) {
                fmt = "%H:%M:%S.%L";
            } else {
                fmt = "%H:%M:%S";
            }
            if (!adjusted_tm) {
                const auto time_range = time_attr->sa_range;
                const auto time_sf
                    = string_fragment::from_str_range(this->lss_token_value,
                                                      time_range.lr_start,
                                                      time_range.lr_end);
                adjusted_tm
                    = format->tm_for_display(this->lss_token_line, time_sf);
            }
            char buffer[128];
            this->lss_time_column_size
                = ftime_fmt(buffer, sizeof(buffer), fmt, adjusted_tm.value());
            if (this->tss_view->is_selectable()
                && this->tss_view->get_selection() == row)
            {
                buffer[this->lss_time_column_size] = ' ';
                buffer[this->lss_time_column_size + 1] = ' ';
                this->lss_time_column_size += 2;
            } else {
                constexpr char block[] = "\u258c ";

                strcpy(&buffer[this->lss_time_column_size], block);
                this->lss_time_column_size += sizeof(block) - 1;
            }
            if (time_attr->sa_range.lr_start != 0) {
                buffer[this->lss_time_column_size] = ' ';
                this->lss_time_column_size += 1;
                this->lss_time_column_padding = 1;
            } else {
                this->lss_time_column_padding = 0;
            }
            value_out.insert(1, buffer, this->lss_time_column_size);
        }
        if (format->lf_level_hideable) {
            auto level_attr = find_string_attr(this->lss_token_attrs, &L_LEVEL);
            if (level_attr != this->lss_token_attrs.end()) {
                this->lss_token_attrs.emplace_back(level_attr->sa_range,
                                                   SA_REPLACED.value());
            }
        }
    } else if (this->lss_line_context < line_context_t::none) {
        size_t file_offset_end;
        std::string name;
        if (this->lss_line_context == line_context_t::filename) {
            file_offset_end = this->lss_filename_width;
            name = fmt::to_string(this->lss_token_file->get_filename());
            if (file_offset_end < name.size()) {
                file_offset_end = name.size();
                this->lss_filename_width = name.size();
            }
        } else {
            file_offset_end = this->lss_basename_width;
            name = fmt::to_string(this->lss_token_file->get_unique_path());
            if (file_offset_end < name.size()) {
                file_offset_end = name.size();
                this->lss_basename_width = name.size();
            }
        }
        value_out.insert(0, file_offset_end - name.size(), ' ');
        value_out.insert(0, name);
    }

    if (this->tas_display_time_offset) {
        auto row_vl = vis_line_t(row);
        auto relstr = this->get_time_offset_for_line(tc, row_vl);
        value_out = fmt::format(FMT_STRING("{: >12}|{}"), relstr, value_out);
    }

    this->lss_in_value_for_line = false;

    return retval;
}

void
logfile_sub_source::text_attrs_for_line(textview_curses& lv,
                                        int row,
                                        string_attrs_t& value_out)
{
    if (this->lss_indexing_in_progress) {
        return;
    }

    auto& vc = view_colors::singleton();
    logline* next_line = nullptr;
    line_range lr;
    int time_offset_end = 0;
    text_attrs attrs;

    value_out = this->lss_token_attrs;

    if ((row + 1) < (int) this->lss_filtered_index.size()) {
        next_line = this->find_line(this->at(vis_line_t(row + 1)));
    }

    if (next_line != nullptr
        && (day_num(next_line->get_time<std::chrono::seconds>().count())
            > day_num(this->lss_token_line->get_time<std::chrono::seconds>()
                          .count())))
    {
        attrs |= text_attrs::style::underline;
    }

    const auto& line_values = this->lss_token_values;

    lr.lr_start = 0;
    lr.lr_end = -1;
    value_out.emplace_back(
        lr, SA_LEVEL.value(this->lss_token_line->get_msg_level()));

    lr.lr_start = time_offset_end;
    lr.lr_end = -1;

    if (!attrs.empty()) {
        value_out.emplace_back(lr, VC_STYLE.value(attrs));
    }

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
            value_out.emplace_back(line_value.lv_origin,
                                   SA_HIDDEN.value(ui_icon_t::hidden));
        }

        if (!line_value.lv_meta.lvm_identifier
            || !line_value.lv_origin.is_valid())
        {
            continue;
        }

        value_out.emplace_back(line_value.lv_origin,
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
        auto graph = NCACS_VLINE;
        if (is_first_for_file) {
            if (is_last_for_file) {
                graph = NCACS_HLINE;
            } else {
                graph = NCACS_ULCORNER;
            }
        } else if (is_last_for_file) {
            graph = NCACS_LLCORNER;
        }
        value_out.emplace_back(lr, VC_GRAPHIC.value(graph));

        if (!(this->lss_token_flags & RF_FULL)) {
            const auto& bv_search = bm[&textview_curses::BM_SEARCH];

            if (binary_search(std::begin(bv_search),
                              std::end(bv_search),
                              vis_line_t(row)))
            {
                lr.lr_start = 0;
                lr.lr_end = 1;
                value_out.emplace_back(
                    lr, VC_STYLE.value(text_attrs::with_reverse()));
            }
        }
    }

    value_out.emplace_back(lr,
                           VC_STYLE.value(vc.attrs_for_ident(
                               this->lss_token_file->get_filename())));

    if (this->lss_line_context < line_context_t::none) {
        size_t file_offset_end
            = (this->lss_line_context == line_context_t::filename)
            ? this->lss_filename_width
            : this->lss_basename_width;

        shift_string_attrs(value_out, 0, file_offset_end);

        lr.lr_start = 0;
        lr.lr_end = file_offset_end + 1;
        value_out.emplace_back(lr,
                               VC_STYLE.value(vc.attrs_for_ident(
                                   this->lss_token_file->get_filename())));
    } else if (this->lss_time_column_size > 0) {
        shift_string_attrs(value_out, 1, this->lss_time_column_size);

        ui_icon_t icon;
        switch (this->lss_token_line->get_msg_level()) {
            case LEVEL_TRACE:
                icon = ui_icon_t::log_level_trace;
                break;
            case LEVEL_DEBUG:
            case LEVEL_DEBUG2:
            case LEVEL_DEBUG3:
            case LEVEL_DEBUG4:
            case LEVEL_DEBUG5:
                icon = ui_icon_t::log_level_debug;
                break;
            case LEVEL_INFO:
                icon = ui_icon_t::log_level_info;
                break;
            case LEVEL_STATS:
                icon = ui_icon_t::log_level_stats;
                break;
            case LEVEL_NOTICE:
                icon = ui_icon_t::log_level_notice;
                break;
            case LEVEL_WARNING:
                icon = ui_icon_t::log_level_warning;
                break;
            case LEVEL_ERROR:
                icon = ui_icon_t::log_level_error;
                break;
            case LEVEL_CRITICAL:
                icon = ui_icon_t::log_level_critical;
                break;
            case LEVEL_FATAL:
                icon = ui_icon_t::log_level_fatal;
                break;
            default:
                icon = ui_icon_t::hidden;
                break;
        }
        auto extra_space_size = this->lss_time_column_padding;
        lr.lr_start = 1 + this->lss_time_column_size - 1 - extra_space_size;
        lr.lr_end = 1 + this->lss_time_column_size - extra_space_size;
        value_out.emplace_back(lr, VC_ICON.value(icon));
        if (this->tss_view->is_selectable()
            && this->tss_view->get_selection() != row)
        {
            lr.lr_start = 1;
            lr.lr_end = 1 + this->lss_time_column_size - 2 - extra_space_size;
            value_out.emplace_back(lr, VC_ROLE.value(role_t::VCR_TIME_COLUMN));
            if (this->lss_token_line->is_time_skewed()) {
                value_out.emplace_back(lr,
                                       VC_ROLE.value(role_t::VCR_SKEWED_TIME));
            }
            lr.lr_start = 1 + this->lss_time_column_size - 2 - extra_space_size;
            lr.lr_end = 1 + this->lss_time_column_size - 1 - extra_space_size;
            value_out.emplace_back(
                lr, VC_ROLE.value(role_t::VCR_TIME_COLUMN_TO_TEXT));
        }
    }

    if (this->tas_display_time_offset) {
        time_offset_end = 13;
        lr.lr_start = 0;
        lr.lr_end = time_offset_end;

        shift_string_attrs(value_out, 0, time_offset_end);

        value_out.emplace_back(lr, VC_ROLE.value(role_t::VCR_OFFSET_TIME));
        value_out.emplace_back(line_range(12, 13),
                               VC_GRAPHIC.value(NCACS_VLINE));

        auto bar_role = role_t::VCR_NONE;

        switch (this->get_line_accel_direction(vis_line_t(row))) {
            case log_accel::direction_t::A_STEADY:
                break;
            case log_accel::direction_t::A_DECEL:
                bar_role = role_t::VCR_DIFF_DELETE;
                break;
            case log_accel::direction_t::A_ACCEL:
                bar_role = role_t::VCR_DIFF_ADD;
                break;
        }
        if (bar_role != role_t::VCR_NONE) {
            value_out.emplace_back(line_range(12, 13), VC_ROLE.value(bar_role));
        }
    }

    lr.lr_start = 0;
    lr.lr_end = -1;
    value_out.emplace_back(lr, L_FILE.value(this->lss_token_file));
    value_out.emplace_back(
        lr, SA_FORMAT.value(this->lss_token_file->get_format()->get_name()));

    {
        auto line_meta_context = this->get_bookmark_metadata_context(
            vis_line_t(row + 1), bookmark_metadata::categories::partition);
        if (line_meta_context.bmc_current_metadata) {
            lr.lr_start = 0;
            lr.lr_end = -1;
            value_out.emplace_back(
                lr,
                L_PARTITION.value(
                    line_meta_context.bmc_current_metadata.value()));
        }

        auto line_meta_opt = this->find_bookmark_metadata(vis_line_t(row));

        if (line_meta_opt) {
            lr.lr_start = 0;
            lr.lr_end = -1;
            value_out.emplace_back(lr, L_META.value(line_meta_opt.value()));
        }
    }

    if (this->lss_time_column_size == 0) {
        if (this->lss_token_file->is_time_adjusted()) {
            auto time_range = find_string_attr_range(value_out, &L_TIMESTAMP);

            if (time_range.lr_end != -1) {
                value_out.emplace_back(
                    time_range, VC_ROLE.value(role_t::VCR_ADJUSTED_TIME));
            }
        } else if (this->lss_token_line->is_time_skewed()) {
            auto time_range = find_string_attr_range(value_out, &L_TIMESTAMP);

            if (time_range.lr_end != -1) {
                value_out.emplace_back(time_range,
                                       VC_ROLE.value(role_t::VCR_SKEWED_TIME));
            }
        }
    }

    if (!this->lss_token_line->is_continued()) {
        if (this->lss_preview_filter_stmt != nullptr) {
            auto color = styling::color_unit::make_empty();
            auto eval_res
                = this->eval_sql_filter(this->lss_preview_filter_stmt.in(),
                                        this->lss_token_file_data,
                                        this->lss_token_line);
            if (eval_res.isErr()) {
                color = palette_color{
                    lnav::enums::to_underlying(ansi_color::yellow)};
                value_out.emplace_back(
                    line_range{0, -1},
                    SA_ERROR.value(
                        eval_res.unwrapErr().to_attr_line().get_string()));
            } else {
                auto matched = eval_res.unwrap();

                if (matched) {
                    color = palette_color{
                        lnav::enums::to_underlying(ansi_color::green)};
                } else {
                    color = palette_color{
                        lnav::enums::to_underlying(ansi_color::red)};
                    value_out.emplace_back(
                        line_range{0, 1},
                        VC_STYLE.value(text_attrs::with_blink()));
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
                value_out.emplace_back(line_range{0, -1}, SA_ERROR.value(msg));
                value_out.emplace_back(
                    line_range{0, 1},
                    VC_BACKGROUND.value(palette_color{
                        lnav::enums::to_underlying(ansi_color::yellow)}));
            }
        }
    }
}

struct logline_cmp {
    logline_cmp(logfile_sub_source& lc) : llss_controller(lc) {}

    bool operator()(const content_line_t& lhs, const content_line_t& rhs) const
    {
        const auto* ll_lhs = this->llss_controller.find_line(lhs);
        const auto* ll_rhs = this->llss_controller.find_line(rhs);

        return (*ll_lhs) < (*ll_rhs);
    }

    bool operator()(const uint32_t& lhs, const uint32_t& rhs) const
    {
        content_line_t cl_lhs = (content_line_t) llss_controller.lss_index[lhs];
        content_line_t cl_rhs = (content_line_t) llss_controller.lss_index[rhs];
        const auto* ll_lhs = this->llss_controller.find_line(cl_lhs);
        const auto* ll_rhs = this->llss_controller.find_line(cl_rhs);

        return (*ll_lhs) < (*ll_rhs);
    }
#if 0
        bool operator()(const indexed_content &lhs, const indexed_content &rhs)
        {
            logline *ll_lhs = this->llss_controller.find_line(lhs.ic_value);
            logline *ll_rhs = this->llss_controller.find_line(rhs.ic_value);

            return (*ll_lhs) < (*ll_rhs);
        }
#endif

#if 0
    bool operator()(const content_line_t& lhs, const time_t& rhs) const
    {
        logline* ll_lhs = this->llss_controller.find_line(lhs);

        return *ll_lhs < rhs;
    }
#endif

    bool operator()(const content_line_t& lhs, const struct timeval& rhs) const
    {
        const auto* ll_lhs = this->llss_controller.find_line(lhs);

        return *ll_lhs < rhs;
    }

    logfile_sub_source& llss_controller;
};

logfile_sub_source::rebuild_result
logfile_sub_source::rebuild_index(std::optional<ui_clock::time_point> deadline)
{
    if (this->tss_view == nullptr) {
        return rebuild_result::rr_no_change;
    }

    this->lss_indexing_in_progress = true;
    auto fin = finally([this]() { this->lss_indexing_in_progress = false; });

    iterator iter;
    size_t total_lines = 0;
    size_t est_remaining_lines = 0;
    bool full_sort = false;
    int file_count = 0;
    bool force = this->lss_force_rebuild;
    auto retval = rebuild_result::rr_no_change;
    std::optional<timeval> lowest_tv = std::nullopt;
    auto search_start = 0_vl;

    this->lss_force_rebuild = false;
    if (force) {
        log_debug("forced to full rebuild");
        retval = rebuild_result::rr_full_rebuild;
        full_sort = true;
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
    this->lss_all_timestamp_flags = 0;
    for (const auto file_index : file_order) {
        auto& ld = *(this->lss_files[file_index]);
        auto* lf = ld.get_file_ptr();

        if (lf == nullptr) {
            if (ld.ld_lines_indexed > 0) {
                log_debug("%d: file closed, doing full rebuild",
                          ld.ld_file_index);
                force = true;
                retval = rebuild_result::rr_full_rebuild;
                full_sort = true;
            }
        } else {
            if (time_left && deadline && ui_clock::now() > deadline.value()) {
                log_debug("no time left, skipping %s",
                          lf->get_filename().c_str());
                time_left = false;
            }
            this->lss_all_timestamp_flags
                |= lf->get_format_ptr()->lf_timestamp_flags;

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
                            auto& new_file_line = (*lf)[ld.ld_lines_indexed];
                            content_line_t cl = this->lss_index.back();
                            auto* last_indexed_line = this->find_line(cl);

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
                                    new_file_line
                                        .get_time<std::chrono::microseconds>()
                                        .count(),
                                    last_indexed_line == nullptr
                                        ? (uint64_t) -1
                                        : last_indexed_line
                                              ->get_time<
                                                  std::chrono::microseconds>()
                                              .count());
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
                                } else {
                                    log_debug(
                                        "already doing full rebuild, doing "
                                        "full_sort as well");
                                    full_sort = true;
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

            est_remaining_lines += lf->estimated_remaining_lines();
        }
    }

    if (this->lss_index.empty() && !time_left) {
        return rebuild_result::rr_appended_lines;
    }

    if (this->lss_index.reserve(total_lines + est_remaining_lines)) {
        // The index array was reallocated, just do a full sort/rebuild since
        // it's been cleared out.
        log_debug("expanding index capacity %zu", this->lss_index.ba_capacity);
        force = true;
        retval = rebuild_result::rr_full_rebuild;
        full_sort = true;
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
            this->lss_longest_line = std::max(
                this->lss_longest_line, lf->get_longest_line_length() + 1);
            this->lss_basename_width
                = std::max(this->lss_basename_width,
                           lf->get_unique_path().native().size());
            this->lss_filename_width = std::max(
                this->lss_filename_width, lf->get_filename().native().size());
        }

        if (full_sort) {
            log_trace("rebuild_index full sort");
            for (auto& ld : this->lss_files) {
                auto* lf = ld->get_file_ptr();

                if (lf == nullptr) {
                    continue;
                }

                for (size_t line_index = 0; line_index < lf->size();
                     line_index++)
                {
                    const auto lf_iter
                        = ld->get_file_ptr()->begin() + line_index;
                    if (lf_iter->is_ignored()) {
                        continue;
                    }

                    content_line_t con_line(
                        ld->ld_file_index * MAX_LINES_PER_FILE + line_index);

                    if (lf_iter->is_meta_marked()) {
                        auto start_iter = lf_iter;
                        while (start_iter->is_continued()) {
                            --start_iter;
                        }
                        int start_index
                            = start_iter - ld->get_file_ptr()->begin();
                        content_line_t start_con_line(ld->ld_file_index
                                                          * MAX_LINES_PER_FILE
                                                      + start_index);

                        auto& line_meta
                            = ld->get_file_ptr()
                                  ->get_bookmark_metadata()[start_index];
                        if (line_meta.has(bookmark_metadata::categories::notes))
                        {
                            this->lss_user_marks[&textview_curses::BM_META]
                                .insert_once(start_con_line);
                        }
                        if (line_meta.has(
                                bookmark_metadata::categories::partition))
                        {
                            this->lss_user_marks[&textview_curses::BM_PARTITION]
                                .insert_once(start_con_line);
                        }
                    }
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
            log_trace("k-way merge") for (;;)
            {
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

                    if (lf_iter->is_meta_marked()) {
                        auto start_iter = lf_iter;
                        while (start_iter->is_continued()) {
                            --start_iter;
                        }
                        int start_index
                            = start_iter - ld->get_file_ptr()->begin();
                        content_line_t start_con_line(
                            file_index * MAX_LINES_PER_FILE + start_index);

                        auto& line_meta
                            = ld->get_file_ptr()
                                  ->get_bookmark_metadata()[start_index];
                        if (line_meta.has(bookmark_metadata::categories::notes))
                        {
                            this->lss_user_marks[&textview_curses::BM_META]
                                .insert_once(start_con_line);
                        }
                        if (line_meta.has(
                                bookmark_metadata::categories::partition))
                        {
                            this->lss_user_marks[&textview_curses::BM_PARTITION]
                                .insert_once(start_con_line);
                        }
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

        log_trace("filtered index");
        for (size_t index_index = start_size;
             index_index < this->lss_index.size();
             index_index++)
        {
            const auto cl = (content_line_t) this->lss_index[index_index];
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
                    this->lss_index_delegate->index_line(*this, lf, line_iter);
                }
            }
        }

        this->lss_indexing_in_progress = false;

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
            this->tss_view->reload_data();
            this->tss_view->redo_search();
            break;
        case rebuild_result::rr_partial_rebuild:
            log_debug("redoing search from: %d", (int) search_start);
            this->lss_index_generation += 1;
            this->tss_view->reload_data();
            this->tss_view->search_new_data(search_start);
            break;
        case rebuild_result::rr_appended_lines:
            this->tss_view->reload_data();
            this->tss_view->search_new_data();
            break;
    }

    return retval;
}

void
logfile_sub_source::text_update_marks(vis_bookmarks& bm)
{
    logfile* last_file = nullptr;
    vis_line_t vl;

    bm[&textview_curses::BM_WARNINGS].clear();
    bm[&textview_curses::BM_ERRORS].clear();
    bm[&BM_FILES].clear();

    for (auto& lss_user_mark : this->lss_user_marks) {
        bm[lss_user_mark.first].clear();
    }

    for (; vl < (int) this->lss_filtered_index.size(); ++vl) {
        const content_line_t orig_cl = this->at(vl);
        content_line_t cl = orig_cl;
        auto lf = this->find_file_ptr(cl);

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
                    bm[&textview_curses::BM_WARNINGS].insert_once(vl);
                    break;

                case LEVEL_FATAL:
                case LEVEL_ERROR:
                case LEVEL_CRITICAL:
                    bm[&textview_curses::BM_ERRORS].insert_once(vl);
                    break;

                default:
                    break;
            }
        }

        last_file = lf;
    }
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

    if (this->lss_force_rebuild) {
        return;
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

std::optional<json_string>
logfile_sub_source::text_row_details(const textview_curses& tc)
{
    if (this->lss_index.empty()) {
        log_trace("logfile_sub_source::text_row_details empty");
        return std::nullopt;
    }

    auto ov_sel = tc.get_overlay_selection();
    if (ov_sel.has_value()) {
        auto* fos
            = dynamic_cast<field_overlay_source*>(tc.get_overlay_source());
        auto iter = fos->fos_row_to_field_meta.find(ov_sel.value());
        if (iter != fos->fos_row_to_field_meta.end()) {
            auto find_res = this->find_line_with_file(tc.get_top());
            if (find_res) {
                yajlpp_gen gen;

                {
                    yajlpp_map root(gen);

                    root.gen("value");
                    root.gen(iter->second.ri_value);
                }

                return json_string(gen);
            }
        }
    }

    return std::nullopt;
}

bool
logfile_sub_source::list_input_handle_key(listview_curses& lv,
                                          const ncinput& ch)
{
    switch (ch.eff_text[0]) {
        case ' ': {
            auto ov_vl = lv.get_overlay_selection();
            if (ov_vl) {
                auto* fos = dynamic_cast<field_overlay_source*>(
                    lv.get_overlay_source());
                auto iter = fos->fos_row_to_field_meta.find(ov_vl.value());
                if (iter != fos->fos_row_to_field_meta.end()
                    && iter->second.ri_meta)
                {
                    auto find_res = this->find_line_with_file(lv.get_top());
                    if (find_res) {
                        auto file_and_line = find_res.value();
                        auto* format = file_and_line.first->get_format_ptr();
                        auto fstates = format->get_field_states();
                        auto state_iter
                            = fstates.find(iter->second.ri_meta->lvm_name);
                        if (state_iter != fstates.end()) {
                            format->hide_field(iter->second.ri_meta->lvm_name,
                                               !state_iter->second.is_hidden());
                            lv.set_needs_update();
                        }
                    }
                }
                return true;
            }
            return false;
        }
        case '#': {
            auto ov_vl = lv.get_overlay_selection();
            if (ov_vl) {
                auto* fos = dynamic_cast<field_overlay_source*>(
                    lv.get_overlay_source());
                auto iter = fos->fos_row_to_field_meta.find(ov_vl.value());
                if (iter != fos->fos_row_to_field_meta.end()
                    && iter->second.ri_meta)
                {
                    const auto& meta = iter->second.ri_meta.value();
                    std::string cmd;

                    switch (meta.to_chart_type()) {
                        case chart_type_t::none:
                            break;
                        case chart_type_t::hist: {
                            auto prql = fmt::format(
                                FMT_STRING(
                                    "from {} | stats.hist {} slice:'1h'"),
                                meta.lvm_format.value()->get_name(),
                                meta.lvm_name);
                            cmd = fmt::format(FMT_STRING(":prompt sql ; '{}'"),
                                              shlex::escape(prql));
                            break;
                        }
                        case chart_type_t::spectro:
                            cmd = fmt::format(FMT_STRING(":spectrogram {}"),
                                              meta.lvm_name);
                            break;
                    }
                    if (!cmd.empty()) {
                        static intern_string_t SRC
                            = intern_string::lookup("hotkey");
                        auto src_guard
                            = this->lss_exec_context->enter_source(SRC, 1, cmd);
                        this->lss_exec_context
                            ->with_provenance(exec_context::mouse_input{})
                            ->execute(cmd);
                    }
                }
                return true;
            }
            return false;
        }
        case 'h':
        case 'H':
        case NCKEY_LEFT:
            if (lv.get_left() == 0) {
                this->increase_line_context();
                lv.set_needs_update();
                return true;
            }
            break;
        case 'l':
        case 'L':
        case NCKEY_RIGHT:
            if (this->decrease_line_context()) {
                lv.set_needs_update();
                return true;
            }
            break;
    }
    return false;
}

std::optional<
    std::pair<grep_proc_source<vis_line_t>*, grep_proc_sink<vis_line_t>*>>
logfile_sub_source::get_grepper()
{
    return std::make_pair(
        (grep_proc_source<vis_line_t>*) &this->lss_meta_grepper,
        (grep_proc_sink<vis_line_t>*) &this->lss_meta_grepper);
}

/**
 * Functor for comparing the ld_file field of the logfile_data struct.
 */
struct logfile_data_eq {
    explicit logfile_data_eq(std::shared_ptr<logfile> lf)
        : lde_file(std::move(lf))
    {
    }

    bool operator()(
        const std::unique_ptr<logfile_sub_source::logfile_data>& ld) const
    {
        return this->lde_file == ld->get_file();
    }

    std::shared_ptr<logfile> lde_file;
};

bool
logfile_sub_source::insert_file(const std::shared_ptr<logfile>& lf)
{
    iterator existing;

    require_lt(lf->size(), MAX_LINES_PER_FILE);

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

    if (this->tss_view == nullptr || this->lss_force_rebuild) {
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

        if (!(*ld)->is_visible()) {
            continue;
        }
        auto ll = (*ld)->get_file()->begin() + cl;
        if (ll->is_continued() || ll->is_ignored()) {
            continue;
        }
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
    format->annotate(lf, line_number, sa, values);

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
            sqlite3_bind_int64(
                stmt,
                lpc + 1,
                ll->get_time<std::chrono::milliseconds>().count());
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
        if (strcmp(name, ":log_annotations") == 0) {
            const auto& bm = lf->get_bookmark_metadata();
            auto line_number
                = static_cast<uint32_t>(std::distance(lf->cbegin(), ll));
            auto bm_iter = bm.find(line_number);
            if (bm_iter != bm.end()
                && !bm_iter->second.bm_annotations.la_pairs.empty())
            {
                const auto& meta = bm_iter->second;
                auto anno_str = logmsg_annotations_handlers.to_string(
                    meta.bm_annotations);

                sqlite3_bind_text(stmt,
                                  lpc + 1,
                                  anno_str.c_str(),
                                  anno_str.length(),
                                  SQLITE_TRANSIENT);
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
                              filename.native().length(),
                              SQLITE_STATIC);
            continue;
        }
        if (strcmp(name, ":log_unique_path") == 0) {
            const auto& filename = lf->get_unique_path();
            sqlite3_bind_text(stmt,
                              lpc + 1,
                              filename.c_str(),
                              filename.native().length(),
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
            if (values.lvv_opid_value) {
                sqlite3_bind_text(stmt,
                                  lpc + 1,
                                  values.lvv_opid_value->c_str(),
                                  values.lvv_opid_value->length(),
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

    if (*ll < this->ttt_min_row_time) {
        return false;
    }

    if (!(*ll <= this->ttt_max_row_time)) {
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
    this->lss_token_file = nullptr;
}

std::optional<vis_line_t>
logfile_sub_source::find_from_content(content_line_t cl)
{
    content_line_t line = cl;
    std::shared_ptr<logfile> lf = this->find(line);

    if (lf != nullptr) {
        auto ll_iter = lf->begin() + line;
        auto& ll = *ll_iter;
        auto vis_start_opt = this->find_from_time(ll.get_timeval());

        if (!vis_start_opt) {
            return std::nullopt;
        }

        auto vis_start = *vis_start_opt;

        while (vis_start < vis_line_t(this->text_line_count())) {
            content_line_t guess_cl = this->at(vis_start);

            if (cl == guess_cl) {
                return vis_start;
            }

            auto guess_line = this->find_line(guess_cl);

            if (!guess_line || ll < *guess_line) {
                return std::nullopt;
            }

            ++vis_start;
        }
    }

    return std::nullopt;
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

std::optional<std::shared_ptr<text_filter>>
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

std::optional<vis_line_t>
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

    return std::nullopt;
}

std::optional<vis_line_t>
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

    return std::nullopt;
}

bool
sql_filter::matches(std::optional<line_source> ls_opt,
                    const shared_buffer_ref& line)
{
    if (!ls_opt) {
        return false;
    }

    auto ls = ls_opt;

    if (!ls->ls_line->is_message()) {
        return false;
    }
    if (this->sf_filter_stmt == nullptr) {
        return false;
    }

    auto lfp = ls->ls_file.shared_from_this();
    auto ld = this->sf_log_source.find_data_i(lfp);
    if (ld == this->sf_log_source.end()) {
        return false;
    }

    auto eval_res = this->sf_log_source.eval_sql_filter(
        this->sf_filter_stmt, ld, ls->ls_line);
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

std::optional<line_info>
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
        value_out.append("\x1c");
        for (const auto& pair : bm.bm_annotations.la_pairs) {
            value_out.append(pair.first);
            value_out.append("\x1c");

            md2attr_line mdal;

            auto parse_res = md4cpp::parse(pair.second, mdal);
            if (parse_res.isOk()) {
                value_out.append(parse_res.unwrap().get_string());
            } else {
                value_out.append(pair.second);
            }
            value_out.append("\x1c");
        }
        value_out.append("\x1c");
        value_out.append(bm.bm_opid);
    }

    if (!this->lmg_done) {
        return line_info{};
    }

    return std::nullopt;
}

vis_line_t
logfile_sub_source::meta_grepper::grep_initial_line(vis_line_t start,
                                                    vis_line_t highest)
{
    auto& bm = this->lmg_source.tss_view->get_bookmarks();
    auto& bv = bm[&textview_curses::BM_META];

    if (bv.empty()) {
        return -1_vl;
    }
    return *bv.begin();
}

void
logfile_sub_source::meta_grepper::grep_next_line(vis_line_t& line)
{
    auto& bm = this->lmg_source.tss_view->get_bookmarks();
    auto& bv = bm[&textview_curses::BM_META];

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
                                             vis_line_t line)
{
    this->lmg_source.tss_view->grep_match(gp, line);
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
    auto vl = this->lw_end_line;
    while (vl < vis_line_t(this->lw_source.text_line_count())) {
        const auto& line = this->lw_source.find_line(this->lw_source.at(vl));
        if (line->is_message()) {
            break;
        }
        ++vl;
    }

    return {this->lw_source, vl};
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
                this->li_line_number
                    = std::distance(this->li_file->begin(), this->li_logline);
                break;
            }
            --vl;
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
            this->li_line_number
                = std::distance(this->li_file->begin(), this->li_logline);
            break;
        } else {
            ++this->li_line;
        }
    }
}

void
logline_window::logmsg_info::prev_msg()
{
    this->li_file = nullptr;
    this->li_logline = logfile::iterator{};
    this->li_string_attrs.clear();
    this->li_line_values.clear();
    while (this->li_line > 0) {
        --this->li_line;
        auto pair_opt = this->li_source.find_line_with_file(this->li_line);

        if (!pair_opt) {
            break;
        }

        auto line_pair = pair_opt.value();
        if (line_pair.second->is_message()) {
            this->li_file = line_pair.first.get();
            this->li_logline = line_pair.second;
            this->li_line_number
                = std::distance(this->li_file->begin(), this->li_logline);
            break;
        }
    }
}

std::optional<bookmark_metadata*>
logline_window::logmsg_info::get_metadata() const
{
    auto line_number = std::distance(this->li_file->begin(), this->li_logline);
    auto& bm = this->li_file->get_bookmark_metadata();
    auto bm_iter = bm.find(line_number);
    if (bm_iter == bm.end()) {
        return std::nullopt;
    }
    return &bm_iter->second;
}

Result<auto_buffer, std::string>
logline_window::logmsg_info::get_line_hash() const
{
    auto sbr = TRY(this->li_file->read_line(this->li_logline));
    auto outbuf = auto_buffer::alloc(3 + hasher::STRING_SIZE);
    outbuf.push_back('v');
    outbuf.push_back('1');
    outbuf.push_back(':');
    hasher line_hasher;
    line_hasher.update(sbr.get_data(), sbr.length())
        .update(this->get_file_line_number())
        .to_string(outbuf);

    return Ok(std::move(outbuf));
}

logline_window::logmsg_info::metadata_edit_guard::~metadata_edit_guard()
{
    auto line_number = std::distance(this->meg_logmsg_info.li_file->begin(),
                                     this->meg_logmsg_info.li_logline);
    auto& bm = this->meg_logmsg_info.li_file->get_bookmark_metadata();
    auto bm_iter = bm.find(line_number);
    if (bm_iter != bm.end()
        && bm_iter->second.empty(bookmark_metadata::categories::any))
    {
        bm.erase(bm_iter);
    }
}

bookmark_metadata&
logline_window::logmsg_info::metadata_edit_guard::operator*()
{
    auto line_number = std::distance(this->meg_logmsg_info.li_file->begin(),
                                     this->meg_logmsg_info.li_logline);
    auto& bm = this->meg_logmsg_info.li_file->get_bookmark_metadata();
    return bm[line_number];
}

size_t
logline_window::logmsg_info::get_line_count() const
{
    size_t retval = 1;
    auto iter = std::next(this->li_logline);
    while (iter != this->li_file->end() && iter->is_continued()) {
        ++iter;
        retval += 1;
    }

    return retval;
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
    format->annotate(this->li_file,
                     std::distance(this->li_file->begin(), this->li_logline),
                     this->li_string_attrs,
                     this->li_line_values,
                     false);

    if (!this->li_line_values.lvv_opid_value) {
        auto bm_opt = this->get_metadata();
        if (bm_opt && !bm_opt.value()->bm_opid.empty()) {
            this->li_line_values.lvv_opid_value = bm_opt.value()->bm_opid;
            this->li_line_values.lvv_opid_provenance
                = logline_value_vector::opid_provenance::user;
        }
    }
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

logline_window::iterator&
logline_window::iterator::operator--()
{
    this->i_info.prev_msg();

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

static attr_line_t
to_display(const std::shared_ptr<logfile>& lf)
{
    attr_line_t retval;

    if (lf->get_open_options().loo_piper) {
        if (!lf->get_open_options().loo_piper->is_finished()) {
            retval.append("\u21bb "_list_glyph);
        }
    }
    retval.append(lf->get_unique_path());

    return retval;
}

void
logfile_sub_source::text_crumbs_for_line(int line,
                                         std::vector<breadcrumb::crumb>& crumbs)
{
    static const intern_string_t SRC = intern_string::lookup("__crumb");
    text_sub_source::text_crumbs_for_line(line, crumbs);

    if (this->lss_filtered_index.empty()) {
        return;
    }

    auto vl = vis_line_t(line);
    auto bmc = this->get_bookmark_metadata_context(
        vl, bookmark_metadata::categories::partition);
    if (bmc.bmc_current_metadata) {
        const auto& name = bmc.bmc_current_metadata.value()->bm_name;
        auto key = text_anchors::to_anchor_string(name);
        auto display = attr_line_t()
                           .append("\u2291 "_symbol)
                           .append(lnav::roles::variable(name))
                           .move();
        crumbs.emplace_back(
            key,
            display,
            [this]() -> std::vector<breadcrumb::possibility> {
                auto& vb = this->tss_view->get_bookmarks();
                const auto& bv = vb[&textview_curses::BM_PARTITION];
                std::vector<breadcrumb::possibility> retval;

                for (const auto& vl : bv) {
                    auto meta_opt = this->find_bookmark_metadata(vl);
                    if (!meta_opt || meta_opt.value()->bm_name.empty()) {
                        continue;
                    }

                    const auto& name = meta_opt.value()->bm_name;
                    retval.emplace_back(text_anchors::to_anchor_string(name),
                                        name);
                }

                return retval;
            },
            [ec = this->lss_exec_context](const auto& part) {
                auto cmd = fmt::format(FMT_STRING(":goto {}"),
                                       part.template get<std::string>());
                auto src_guard = ec->enter_source(SRC, 1, cmd);
                ec->execute(cmd);
            });
    }

    auto line_pair_opt = this->find_line_with_file(vl);
    if (!line_pair_opt) {
        return;
    }
    auto line_pair = line_pair_opt.value();
    auto& lf = line_pair.first;
    auto format = lf->get_format();
    char ts[64];

    sql_strftime(ts, sizeof(ts), line_pair.second->get_timeval(), 'T');

    crumbs.emplace_back(std::string(ts),
                        timestamp_poss,
                        [ec = this->lss_exec_context](const auto& ts) {
                            auto cmd
                                = fmt::format(FMT_STRING(":goto {}"),
                                              ts.template get<std::string>());
                            auto src_guard = ec->enter_source(SRC, 1, cmd);
                            ec->execute(cmd);
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
                   })
                | lnav::itertools::to_vector();
        },
        [ec = this->lss_exec_context](const auto& format_name) {
            static const std::string MOVE_STMT = R"(;UPDATE lnav_views
     SET selection = ifnull(
         (SELECT log_line FROM all_logs WHERE log_format = $format_name LIMIT 1),
         (SELECT raise_error(
            'Could not find format: ' || $format_name,
            'The corresponding log messages might have been filtered out'))
       )
     WHERE name = 'log'
)";

            auto src_guard = ec->enter_source(SRC, 1, MOVE_STMT);
            ec->execute_with(
                MOVE_STMT,
                std::make_pair("format_name",
                               format_name.template get<std::string>()));
        });

    auto msg_start_iter = lf->message_start(line_pair.second);
    auto file_line_number = std::distance(lf->begin(), msg_start_iter);
    crumbs.emplace_back(
        lf->get_unique_path(),
        to_display(lf).appendf(FMT_STRING("[{:L}]"), file_line_number),
        [this]() -> std::vector<breadcrumb::possibility> {
            return this->lss_files
                | lnav::itertools::filter_in([](const auto& file_data) {
                       return file_data->is_visible();
                   })
                | lnav::itertools::map([](const auto& file_data) {
                       return breadcrumb::possibility{
                           file_data->get_file_ptr()->get_unique_path(),
                           to_display(file_data->get_file()),
                       };
                   });
        },
        [ec = this->lss_exec_context](const auto& uniq_path) {
            static const std::string MOVE_STMT = R"(;UPDATE lnav_views
     SET selection = ifnull(
          (SELECT log_line FROM all_logs WHERE log_unique_path = $uniq_path LIMIT 1),
          (SELECT raise_error(
            'Could not find file: ' || $uniq_path,
            'The corresponding log messages might have been filtered out'))
         )
     WHERE name = 'log'
)";

            auto src_guard = ec->enter_source(SRC, 1, MOVE_STMT);
            ec->execute_with(
                MOVE_STMT,
                std::make_pair("uniq_path",
                               uniq_path.template get<std::string>()));
        });

    logline_value_vector values;
    auto& sbr = values.lvv_sbr;

    lf->read_full_message(msg_start_iter, sbr);
    attr_line_t al(to_string(sbr));
    if (sbr.get_metadata().m_has_ansi) {
        // bleh
        scrub_ansi_string(al.get_string(), &al.al_attrs);
    }
    format->annotate(lf.get(), file_line_number, al.get_attrs(), values);

    {
        static const std::string MOVE_STMT = R"(;UPDATE lnav_views
          SET selection = ifnull(
            (SELECT log_line FROM all_logs WHERE log_opid = $opid LIMIT 1),
            (SELECT raise_error('Could not find opid: ' || $opid,
                                'The corresponding log messages might have been filtered out')))
          WHERE name = 'log'
        )";
        static const std::string ELLIPSIS = "\u22ef";

        auto opid_display = values.lvv_opid_value.has_value()
            ? lnav::roles::identifier(values.lvv_opid_value.value())
            : lnav::roles::hidden(ELLIPSIS);
        crumbs.emplace_back(
            values.lvv_opid_value.has_value() ? values.lvv_opid_value.value()
                                              : "",
            attr_line_t().append(opid_display),
            [this]() -> std::vector<breadcrumb::possibility> {
                std::set<std::string> poss_strs;

                for (const auto& file_data : this->lss_files) {
                    if (file_data->get_file_ptr() == nullptr) {
                        continue;
                    }
                    safe::ReadAccess<logfile::safe_opid_state> r_opid_map(
                        file_data->get_file_ptr()->get_opids());

                    for (const auto& pair : r_opid_map->los_opid_ranges) {
                        poss_strs.emplace(pair.first.to_string());
                    }
                }

                std::vector<breadcrumb::possibility> retval;

                std::transform(poss_strs.begin(),
                               poss_strs.end(),
                               std::back_inserter(retval),
                               [](const auto& opid_str) {
                                   return breadcrumb::possibility(opid_str);
                               });

                return retval;
            },
            [ec = this->lss_exec_context](const auto& opid) {
                auto src_guard = ec->enter_source(SRC, 1, MOVE_STMT);
                ec->execute_with(
                    MOVE_STMT,
                    std::make_pair("opid", opid.template get<std::string>()));
            });
    }

    auto sf = string_fragment::from_str(al.get_string());
    auto body_opt = get_string_attr(al.get_attrs(), SA_BODY);
    auto nl_pos_opt = sf.find('\n');
    auto msg_line_number = std::distance(msg_start_iter, line_pair.second);
    auto line_from_top = line - msg_line_number;
    if (body_opt && nl_pos_opt) {
        if (this->lss_token_meta_line != file_line_number
            || this->lss_token_meta_size != sf.length())
        {
            if (body_opt->saw_string_attr->sa_range.length() < 128 * 1024) {
                this->lss_token_meta
                    = lnav::document::discover(al)
                          .over_range(
                              body_opt.value().saw_string_attr->sa_range)
                          .perform();
                // XXX discover_structure() changes `al`, have to recompute
                // stuff
                sf = al.to_string_fragment();
                body_opt = get_string_attr(al.get_attrs(), SA_BODY);
            } else {
                this->lss_token_meta = lnav::document::metadata{};
            }
            this->lss_token_meta_line = file_line_number;
            this->lss_token_meta_size = sf.length();
        }

        const auto initial_size = crumbs.size();
        auto sf_body
            = sf.sub_range(body_opt->saw_string_attr->sa_range.lr_start,
                           body_opt->saw_string_attr->sa_range.lr_end);
        file_off_t line_offset = body_opt->saw_string_attr->sa_range.lr_start;
        file_off_t line_end_offset = sf.length();
        ssize_t line_number = 0;

        for (const auto& sf_line : sf_body.split_lines()) {
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

                crumbs.emplace_back(
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
                        key.match(
                            [parent_node](const std::string& str) {
                                return parent_node->find_line_number(str);
                            },
                            [parent_node](size_t index) {
                                return parent_node->find_line_number(index);
                            })
                            | [this, line_from_top](auto line_number) {
                                  this->tss_view->set_selection(
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
                    retval.emplace_back(child.first);
                }
                return retval;
            };
            auto path_performer
                = [this, curr_node = node.value(), line_from_top](
                      const breadcrumb::crumb::key_t& value) {
                      value.match(
                          [curr_node](const std::string& str) {
                              return curr_node->find_line_number(str);
                          },
                          [curr_node](size_t index) {
                              return curr_node->find_line_number(index);
                          })
                          | [this, line_from_top](size_t line_number) {
                                this->tss_view->set_selection(
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

logfile_sub_source::bookmark_metadata_context
logfile_sub_source::get_bookmark_metadata_context(
    vis_line_t vl, bookmark_metadata::categories desired) const
{
    const auto& vb = this->tss_view->get_bookmarks();
    const auto bv_iter
        = vb.find(desired == bookmark_metadata::categories::partition
                      ? &textview_curses::BM_PARTITION
                      : &textview_curses::BM_META);
    if (bv_iter == vb.end()) {
        return bookmark_metadata_context{};
    }

    const auto& bv = bv_iter->second;
    auto vl_iter = std::lower_bound(bv.begin(), bv.end(), vl + 1_vl);

    std::optional<vis_line_t> next_line;
    for (auto next_vl_iter = vl_iter; next_vl_iter != bv.end(); ++next_vl_iter)
    {
        auto bm_opt = this->find_bookmark_metadata(*next_vl_iter);
        if (!bm_opt) {
            continue;
        }

        if (bm_opt.value()->has(desired)) {
            next_line = *next_vl_iter;
            break;
        }
    }
    if (vl_iter == bv.begin()) {
        return bookmark_metadata_context{std::nullopt, std::nullopt, next_line};
    }

    --vl_iter;
    while (true) {
        auto bm_opt = this->find_bookmark_metadata(*vl_iter);
        if (bm_opt) {
            if (bm_opt.value()->has(desired)) {
                return bookmark_metadata_context{
                    *vl_iter, bm_opt.value(), next_line};
            }
        }

        if (vl_iter == bv.begin()) {
            return bookmark_metadata_context{
                std::nullopt, std::nullopt, next_line};
        }
        --vl_iter;
    }
    return bookmark_metadata_context{std::nullopt, std::nullopt, next_line};
}

std::optional<bookmark_metadata*>
logfile_sub_source::find_bookmark_metadata(content_line_t cl) const
{
    auto line_pair = this->find_line_with_file(cl).value();
    auto line_number = static_cast<uint32_t>(
        std::distance(line_pair.first->begin(), line_pair.second));

    auto& bm = line_pair.first->get_bookmark_metadata();
    auto bm_iter = bm.find(line_number);
    if (bm_iter == bm.end()) {
        return std::nullopt;
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

void
logfile_sub_source::increase_line_context()
{
    auto old_context = this->lss_line_context;

    switch (this->lss_line_context) {
        case line_context_t::filename:
            // nothing to do
            break;
        case line_context_t::basename:
            this->lss_line_context = line_context_t::filename;
            break;
        case line_context_t::none:
            this->lss_line_context = line_context_t::basename;
            break;
        case line_context_t::time_column:
            this->lss_line_context = line_context_t::none;
            break;
    }
    if (old_context != this->lss_line_context) {
        this->clear_line_size_cache();
    }
}

bool
logfile_sub_source::decrease_line_context()
{
    static const auto& cfg
        = injector::get<const logfile_sub_source_ns::config&>();
    auto old_context = this->lss_line_context;

    switch (this->lss_line_context) {
        case line_context_t::filename:
            this->lss_line_context = line_context_t::basename;
            break;
        case line_context_t::basename:
            this->lss_line_context = line_context_t::none;
            break;
        case line_context_t::none:
            if (cfg.c_time_column
                != logfile_sub_source_ns::time_column_feature_t::Disabled)
            {
                this->lss_line_context = line_context_t::time_column;
            }
            break;
        case line_context_t::time_column:
            break;
    }
    if (old_context != this->lss_line_context) {
        this->clear_line_size_cache();

        return true;
    }

    return false;
}

size_t
logfile_sub_source::get_filename_offset() const
{
    switch (this->lss_line_context) {
        case line_context_t::filename:
            return this->lss_filename_width;
        case line_context_t::basename:
            return this->lss_basename_width;
        default:
            return 0;
    }
}

size_t
logfile_sub_source::file_count() const
{
    size_t retval = 0;
    const_iterator iter;

    for (iter = this->cbegin(); iter != this->cend(); ++iter) {
        if (*iter != nullptr && (*iter)->get_file() != nullptr) {
            retval += 1;
        }
    }

    return retval;
}

size_t
logfile_sub_source::text_size_for_line(textview_curses& tc,
                                       int row,
                                       text_sub_source::line_flags_t flags)
{
    size_t index = row % LINE_SIZE_CACHE_SIZE;

    if (this->lss_line_size_cache[index].first != row) {
        std::string value;

        this->text_value_for_line(tc, row, value, flags);
        scrub_ansi_string(value, nullptr);
        this->lss_line_size_cache[index].second
            = string_fragment::from_str(value).column_width();
        this->lss_line_size_cache[index].first = row;
    }
    return this->lss_line_size_cache[index].second;
}

int
logfile_sub_source::get_filtered_count_for(size_t filter_index) const
{
    int retval = 0;

    for (const auto& ld : this->lss_files) {
        retval += ld->ld_filter_state.lfo_filter_state
                      .tfs_filter_hits[filter_index];
    }

    return retval;
}

std::optional<vis_line_t>
logfile_sub_source::row_for(const row_info& ri)
{
    auto lb = std::lower_bound(this->lss_filtered_index.begin(),
                               this->lss_filtered_index.end(),
                               ri.ri_time,
                               filtered_logline_cmp(*this));
    if (lb != this->lss_filtered_index.end()) {
        auto first_lb = lb;
        while (true) {
            auto cl = this->lss_index[*lb];
            if (content_line_t(ri.ri_id) == cl) {
                first_lb = lb;
                break;
            }
            auto ll = this->find_line(cl);
            if (ll->get_timeval() != ri.ri_time) {
                break;
            }
            ++lb;
        }

        const auto dst
            = std::distance(this->lss_filtered_index.begin(), first_lb);
        return vis_line_t(dst);
    }

    return std::nullopt;
}

std::optional<vis_line_t>
logfile_sub_source::row_for_anchor(const std::string& id)
{
    if (startswith(id, "#msg")) {
        static const auto ANCHOR_RE
            = lnav::pcre2pp::code::from_const(R"(#msg([0-9a-fA-F]+)-(.+))");
        thread_local auto md = lnav::pcre2pp::match_data::unitialized();

        if (ANCHOR_RE.capture_from(id).into(md).found_p()) {
            auto scan_res = scn::scan<int64_t>(md[1]->to_string_view(), "{:x}");
            if (scan_res) {
                auto ts_low = std::chrono::microseconds{scan_res->value()};
                auto ts_high = ts_low + 1us;

                auto low_vl = this->row_for_time(to_timeval(ts_low));
                auto high_vl = this->row_for_time(to_timeval(ts_high));
                if (low_vl) {
                    auto lw = this->window_at(
                        low_vl.value(),
                        high_vl.value_or(low_vl.value() + 1_vl));

                    for (const auto& li : lw) {
                        auto hash_res = li.get_line_hash();
                        if (hash_res.isErr()) {
                            auto errmsg = hash_res.unwrapErr();

                            log_error("unable to get line hash: %s",
                                      errmsg.c_str());
                            continue;
                        }

                        auto hash = hash_res.unwrap();
                        if (hash == md[2]) {
                            return li.get_vis_line();
                        }
                    }
                }
            }
        }

        return std::nullopt;
    }

    auto& vb = this->tss_view->get_bookmarks();
    const auto& bv = vb[&textview_curses::BM_PARTITION];

    for (const auto& vl : bv) {
        auto meta_opt = this->find_bookmark_metadata(vl);
        if (!meta_opt || meta_opt.value()->bm_name.empty()) {
            continue;
        }

        const auto& name = meta_opt.value()->bm_name;
        if (id == text_anchors::to_anchor_string(name)) {
            return vl;
        }
    }

    return std::nullopt;
}

std::optional<vis_line_t>
logfile_sub_source::adjacent_anchor(vis_line_t vl, text_anchors::direction dir)
{
    auto bmc = this->get_bookmark_metadata_context(
        vl, bookmark_metadata::categories::partition);
    switch (dir) {
        case text_anchors::direction::prev: {
            if (bmc.bmc_current && bmc.bmc_current.value() != vl) {
                return bmc.bmc_current;
            }
            if (!bmc.bmc_current || bmc.bmc_current.value() == 0_vl) {
                return 0_vl;
            }
            auto prev_bmc = this->get_bookmark_metadata_context(
                bmc.bmc_current.value() - 1_vl,
                bookmark_metadata::categories::partition);
            if (!prev_bmc.bmc_current) {
                return 0_vl;
            }
            return prev_bmc.bmc_current;
        }
        case text_anchors::direction::next:
            return bmc.bmc_next_line;
    }
    return std::nullopt;
}

std::optional<std::string>
logfile_sub_source::anchor_for_row(vis_line_t vl)
{
    auto line_meta = this->get_bookmark_metadata_context(
        vl, bookmark_metadata::categories::partition);
    if (!line_meta.bmc_current_metadata) {
        auto lw = window_at(vl);

        for (const auto& li : lw) {
            auto hash_res = li.get_line_hash();
            if (hash_res.isErr()) {
                auto errmsg = hash_res.unwrapErr();
                log_error("unable to compute line hash: %s", errmsg.c_str());
                break;
            }
            auto hash = hash_res.unwrap();
            auto retval = fmt::format(
                FMT_STRING("#msg{:016x}-{}"),
                li.get_logline().get_time<std::chrono::microseconds>().count(),
                hash);

            return retval;
        }

        return std::nullopt;
    }

    return text_anchors::to_anchor_string(
        line_meta.bmc_current_metadata.value()->bm_name);
}

std::unordered_set<std::string>
logfile_sub_source::get_anchors()
{
    auto& vb = this->tss_view->get_bookmarks();
    const auto& bv = vb[&textview_curses::BM_PARTITION];
    std::unordered_set<std::string> retval;

    for (const auto& vl : bv) {
        auto meta_opt = this->find_bookmark_metadata(vl);
        if (!meta_opt || meta_opt.value()->bm_name.empty()) {
            continue;
        }

        const auto& name = meta_opt.value()->bm_name;
        retval.emplace(text_anchors::to_anchor_string(name));
    }

    return retval;
}

bool
logfile_sub_source::text_handle_mouse(
    textview_curses& tc,
    const listview_curses::display_line_content_t& mouse_line,
    mouse_event& me)
{
    if (tc.get_overlay_selection()) {
        auto nci = ncinput{};
        if (me.is_click_in(mouse_button_t::BUTTON_LEFT, 2, 4)) {
            nci.id = ' ';
            nci.eff_text[0] = ' ';
            this->list_input_handle_key(tc, nci);
        } else if (me.is_click_in(mouse_button_t::BUTTON_LEFT, 5, 6)) {
            nci.id = '#';
            nci.eff_text[0] = '#';
            this->list_input_handle_key(tc, nci);
        }
    }
    return true;
}

void
logfile_sub_source::reload_config(error_reporter& reporter)
{
    static const auto& cfg
        = injector::get<const logfile_sub_source_ns::config&>();

    if (cfg.c_time_column
            == logfile_sub_source_ns::time_column_feature_t::Default
        && this->lss_line_context == line_context_t::none)
    {
        this->lss_line_context = line_context_t::time_column;
    }
}
