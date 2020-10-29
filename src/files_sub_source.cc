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

#include "lnav.hh"
#include "files_sub_source.hh"


files_sub_source::files_sub_source()
{

}

bool files_sub_source::list_input_handle_key(listview_curses &lv, int ch)
{
    switch (ch) {
        case '\t':
        case KEY_BTAB:
        case 'q':
            lnav_data.ld_mode = LNM_PAGING;
            lnav_data.ld_files_view.reload_data();
            return true;

        case KEY_ENTER:
        case '\r': {
            if (lnav_data.ld_active_files.fc_files.empty()) {
                return true;
            }

            auto& lss = lnav_data.ld_log_source;
            auto &lf = lnav_data.ld_active_files.fc_files[lv.get_selection()];

            if (!lf->is_visible()) {
                lf->show();
                if (lf->get_format() != nullptr) {
                    lss.text_filters_changed();
                }
            }

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
            return true;
        }

        case ' ': {
            if (lnav_data.ld_active_files.fc_files.empty()) {
                return true;
            }

            auto &lf = lnav_data.ld_active_files.fc_files[lv.get_selection()];
            lf->set_visibility(!lf->is_visible());
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
    return lnav_data.ld_active_files.fc_files.size();
}

size_t files_sub_source::text_line_width(textview_curses &curses)
{
    return 512;
}

void files_sub_source::text_value_for_line(textview_curses &tc, int line,
                                           std::string &value_out,
                                           text_sub_source::line_flags_t flags)
{
    const auto &lf = lnav_data.ld_active_files.fc_files[line];
    char start_time[64] = "", end_time[64] = "";

    if (lf->get_format() != nullptr) {
        sql_strftime(start_time, sizeof(start_time), lf->front().get_timeval());
        sql_strftime(end_time, sizeof(end_time), lf->back().get_timeval());
    }
    value_out = fmt::format(
        FMT_STRING("    {:<40} {:>8} {} \u2014 {}"),
        lf->get_unique_path(),
        humanize::file_size(lf->get_index_size()),
        start_time,
        end_time);
}

void files_sub_source::text_attrs_for_line(textview_curses &tc, int line,
                                           string_attrs_t &value_out)
{
    auto &vcolors = view_colors::singleton();
    bool selected = lnav_data.ld_mode == LNM_FILES && line == tc.get_selection();
    int bg = selected ? COLOR_WHITE : COLOR_BLACK;
    auto &lf = lnav_data.ld_active_files.fc_files[line];

    chtype visible = lf->is_visible() ? ACS_DIAMOND : ' ';
    value_out.emplace_back(line_range{2, 3}, &view_curses::VC_GRAPHIC, visible);
    if (lf->is_visible()) {
        value_out.emplace_back(line_range{2, 3}, &view_curses::VC_FOREGROUND,
                               vcolors.ansi_to_theme_color(COLOR_GREEN));
    }

    if (selected) {
        value_out.emplace_back(line_range{0, 1}, &view_curses::VC_GRAPHIC, ACS_RARROW);
    }

    value_out.emplace_back(line_range{41 + 4, 41 + 10},
                           &view_curses::VC_FOREGROUND,
                           COLOR_WHITE);
    value_out.emplace_back(line_range{41 + 10, 41 + 12},
                           &view_curses::VC_STYLE,
                           A_BOLD);

    int fg = selected ? COLOR_BLACK : COLOR_WHITE;
    value_out.emplace_back(line_range{0, -1}, &view_curses::VC_FOREGROUND,
                           vcolors.ansi_to_theme_color(fg));
    value_out.emplace_back(line_range{0, -1}, &view_curses::VC_BACKGROUND,
                           vcolors.ansi_to_theme_color(bg));
}

size_t files_sub_source::text_size_for_line(textview_curses &tc, int line,
                                            text_sub_source::line_flags_t raw)
{
    return 0;
}
