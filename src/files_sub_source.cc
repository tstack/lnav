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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include "base/humanize.hh"
#include "base/string_util.hh"

#include "lnav.hh"
#include "files_sub_source.hh"


files_sub_source::files_sub_source()
{

}

bool files_sub_source::list_input_handle_key(listview_curses &lv, int ch)
{
    switch (ch) {
        case KEY_ENTER:
        case '\r': {
            auto &fc = lnav_data.ld_active_files;

            if (fc.fc_files.empty() && fc.fc_other_files.empty()) {
                return true;
            }

            auto sel = (int) lv.get_selection();

            sel -= fc.fc_other_files.size();
            if (sel < 0) {
                return true;
            }

            auto& lss = lnav_data.ld_log_source;
            auto &lf = fc.fc_files[sel];

            lss.find_data(lf) | [&lss](auto ld) {
                ld->set_visibility(true);
                lss.text_filters_changed();
            };

            if (lf->get_format() != nullptr) {
                auto& log_view = lnav_data.ld_views[LNV_LOG];
                auto row = lss.row_for_time(lf->front().get_timeval());

                log_view.set_top(vis_line_t(row));
                ensure_view(&log_view);
            } else {
                auto& tv = lnav_data.ld_views[LNV_TEXT];
                auto& tss = lnav_data.ld_text_source;

                tss.to_front(lf);
                tv.reload_data();
                ensure_view(&tv);
            }

            lv.reload_data();
            lnav_data.ld_mode = LNM_PAGING;
            return true;
        }

        case ' ': {
            auto &fc = lnav_data.ld_active_files;

            if (fc.fc_files.empty() && fc.fc_other_files.empty()) {
                return true;
            }

            auto sel = (int) lv.get_selection();

            sel -= fc.fc_other_files.size();
            if (sel < 0) {
                return true;
            }

            auto& lss = lnav_data.ld_log_source;
            auto &lf = fc.fc_files[sel];

            lss.find_data(lf) | [](auto ld) {
                ld->set_visibility(!ld->ld_visible);
            };

            auto top_view = *lnav_data.ld_view_stack.top();
            auto tss = top_view->get_sub_source();

            if (tss != nullptr) {
                tss->text_filters_changed();
                top_view->reload_data();
            }

            lv.reload_data();
            return true;
        }
        case 'n': {
            execute_command(lnav_data.ld_exec_context, "next-mark search");
            return true;
        }
        case 'N': {
            execute_command(lnav_data.ld_exec_context, "prev-mark search");
            return true;
        }
        case '/': {
            execute_command(lnav_data.ld_exec_context,
                            "prompt search-files");
            return true;
        }
    }
    return false;
}

void files_sub_source::list_input_handle_scroll_out(listview_curses &lv)
{
    lnav_data.ld_mode = LNM_PAGING;
    lnav_data.ld_filter_view.reload_data();
}

size_t files_sub_source::text_line_count()
{
    const auto &fc = lnav_data.ld_active_files;

    return fc.fc_name_to_errors.size() +
           fc.fc_other_files.size() +
           fc.fc_files.size();
}

size_t files_sub_source::text_line_width(textview_curses &curses)
{
    return 512;
}

void files_sub_source::text_value_for_line(textview_curses &tc, int line,
                                           std::string &value_out,
                                           text_sub_source::line_flags_t flags)
{
    const auto dim = tc.get_dimensions();
    const auto &fc = lnav_data.ld_active_files;
    auto filename_width =
        std::min(fc.fc_largest_path_length,
                 std::max((size_t) 40, dim.second - 30));

    if (line < fc.fc_name_to_errors.size()) {
        auto iter = fc.fc_name_to_errors.begin();
        std::advance(iter, line);
        auto path = ghc::filesystem::path(iter->first);
        auto fn = path.filename().string();

        truncate_to(fn, filename_width);
        value_out = fmt::format(
            FMT_STRING("    {:<{}}   {}"),
            fn, filename_width, iter->second);
        return;
    }

    line -= fc.fc_name_to_errors.size();

    if (line < fc.fc_other_files.size()) {
        auto iter = fc.fc_other_files.begin();
        std::advance(iter, line);
        auto path = ghc::filesystem::path(iter->first);
        auto fn = path.filename().string();

        truncate_to(fn, filename_width);
        value_out = fmt::format(
            FMT_STRING("    {:<{}}   {}"),
            fn, filename_width, iter->second);
        return;
    }

    line -= fc.fc_other_files.size();

    const auto &lf = fc.fc_files[line];
    auto fn = lf->get_unique_path();
    char start_time[64] = "", end_time[64] = "";

    if (lf->get_format() != nullptr) {
        sql_strftime(start_time, sizeof(start_time), lf->front().get_timeval());
        sql_strftime(end_time, sizeof(end_time), lf->back().get_timeval());
    }
    truncate_to(fn, filename_width);
    value_out = fmt::format(
        FMT_STRING("    {:<{}}   {:>8} {} \u2014 {}"),
        fn,
        filename_width,
        humanize::file_size(lf->get_index_size()),
        start_time,
        end_time);
}

void files_sub_source::text_attrs_for_line(textview_curses &tc, int line,
                                           string_attrs_t &value_out)
{
    bool selected = lnav_data.ld_mode == LNM_FILES && line == tc.get_selection();
    const auto &fc = lnav_data.ld_active_files;
    auto &vcolors = view_colors::singleton();
    const auto dim = tc.get_dimensions();
    auto filename_width =
        std::min(fc.fc_largest_path_length,
                 std::max((size_t) 40, dim.second - 30));

    if (selected) {
        value_out.emplace_back(line_range{0, 1}, &view_curses::VC_GRAPHIC, ACS_RARROW);
    }

    if (line < fc.fc_name_to_errors.size()) {
        if (selected) {
            value_out.emplace_back(line_range{0, -1},
                                   &view_curses::VC_ROLE,
                                   view_colors::VCR_DISABLED_FOCUSED);
        }

        value_out.emplace_back(line_range{4 + (int) filename_width, -1},
                               &view_curses::VC_ROLE_FG,
                               view_colors::VCR_ERROR);
        return;
    }
    line -= fc.fc_name_to_errors.size();

    if (line < fc.fc_other_files.size()) {
        if (selected) {
            value_out.emplace_back(line_range{0, -1},
                                   &view_curses::VC_ROLE,
                                   view_colors::VCR_DISABLED_FOCUSED);
        }
        if (line == fc.fc_other_files.size() - 1) {
            value_out.emplace_back(line_range{0, -1},
                                   &view_curses::VC_STYLE,
                                   A_UNDERLINE);
        }
        return;
    }

    line -= fc.fc_other_files.size();

    if (selected) {
        value_out.emplace_back(line_range{0, -1},
                               &view_curses::VC_ROLE,
                               view_colors::VCR_FOCUSED);
    }

    auto& lss = lnav_data.ld_log_source;
    auto &lf = fc.fc_files[line];
    auto ld_opt = lss.find_data(lf);

    chtype visible = ACS_DIAMOND;
    if (ld_opt && !ld_opt.value()->ld_visible) {
        visible = ' ';
    }
    value_out.emplace_back(line_range{2, 3}, &view_curses::VC_GRAPHIC, visible);
    if (visible == ACS_DIAMOND) {
        value_out.emplace_back(line_range{2, 3}, &view_curses::VC_FOREGROUND,
                               vcolors.ansi_to_theme_color(COLOR_GREEN));
    }

    auto lr = line_range{
        (int) filename_width + 3 + 4,
        (int) filename_width + 3 + 10,
    };
    value_out.emplace_back(lr, &view_curses::VC_STYLE, A_BOLD);
}

size_t files_sub_source::text_size_for_line(textview_curses &tc, int line,
                                            text_sub_source::line_flags_t raw)
{
    return 0;
}

bool
files_overlay_source::list_value_for_overlay(const listview_curses &lv, int y,
                                             int bottom, vis_line_t line,
                                             attr_line_t &value_out)
{
    if (y == 0) {
        auto &fc = lnav_data.ld_active_files;
        auto &fc_prog = fc.fc_progress;
        safe::WriteAccess<safe_scan_progress> sp(*fc_prog);

        if (!sp->sp_extractions.empty()) {
            static char PROG[] = "-\\|/";

            const auto& prog = sp->sp_extractions.front();

            value_out.with_ansi_string(fmt::format(
                "{} Extracting "
                ANSI_COLOR(COLOR_CYAN) "{}" ANSI_NORM
                "... {:>8}/{}",
                PROG[this->fos_counter % sizeof(PROG)],
                prog.ep_path.filename().string(),
                humanize::file_size(prog.ep_out_size),
                humanize::file_size(prog.ep_total_size)));

            this->fos_counter += 1;
            return true;
        }
    }
    return false;
}
