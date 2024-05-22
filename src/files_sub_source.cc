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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "files_sub_source.hh"

#include "base/ansi_scrubber.hh"
#include "base/attr_line.builder.hh"
#include "base/fs_util.hh"
#include "base/humanize.hh"
#include "base/humanize.network.hh"
#include "base/opt_util.hh"
#include "base/string_util.hh"
#include "config.h"
#include "lnav.hh"
#include "mapbox/variant.hpp"
#include "sql_util.hh"

using namespace lnav::roles::literals;

namespace files_model {
files_list_selection
from_selection(vis_line_t sel_vis)
{
    auto& fc = lnav_data.ld_active_files;
    int sel = (int) sel_vis;

    {
        safe::ReadAccess<safe_name_to_errors> errs(*fc.fc_name_to_errors);

        if (sel < errs->size()) {
            auto iter = errs->begin();

            std::advance(iter, sel);
            return error_selection::build(sel, iter->first);
        }

        sel -= errs->size();
    }

    if (sel < fc.fc_other_files.size()) {
        auto iter = fc.fc_other_files.begin();

        std::advance(iter, sel);
        return other_selection::build(sel, iter);
    }

    sel -= fc.fc_other_files.size();

    if (sel < fc.fc_files.size()) {
        auto iter = fc.fc_files.begin();

        std::advance(iter, sel);
        return file_selection::build(sel, iter);
    }

    return no_selection{};
}
}  // namespace files_model

files_sub_source::files_sub_source() {}

bool
files_sub_source::list_input_handle_key(listview_curses& lv, int ch)
{
    switch (ch) {
        case KEY_ENTER:
        case '\r': {
            auto sel = files_model::from_selection(lv.get_selection());

            sel.match(
                [](files_model::no_selection) {},
                [](files_model::error_selection) {},
                [](files_model::other_selection) {},
                [&](files_model::file_selection& fs) {
                    auto& lss = lnav_data.ld_log_source;
                    auto lf = *fs.sb_iter;

                    lf->set_indexing(true);
                    lss.find_data(lf) | [](auto ld) {
                        ld->set_visibility(true);
                        lnav_data.ld_log_source.text_filters_changed();
                    };

                    if (lf->get_format() != nullptr) {
                        auto& log_view = lnav_data.ld_views[LNV_LOG];
                        lss.row_for_time(lf->front().get_timeval()) |
                            [](auto row) {
                                lnav_data.ld_views[LNV_LOG].set_selection(row);
                            };
                        ensure_view(&log_view);
                    } else {
                        auto& tv = lnav_data.ld_views[LNV_TEXT];
                        auto& tss = lnav_data.ld_text_source;

                        tss.to_front(lf);
                        tv.reload_data();
                        ensure_view(&tv);
                    }

                    lv.reload_data();
                    lnav_data.ld_mode = ln_mode_t::PAGING;
                });

            return true;
        }

        case ' ': {
            auto sel = files_model::from_selection(lv.get_selection());

            sel.match([](files_model::no_selection) {},
                      [](files_model::error_selection) {},
                      [](files_model::other_selection) {},
                      [&](files_model::file_selection& fs) {
                          auto& lss = lnav_data.ld_log_source;
                          auto lf = *fs.sb_iter;

                          lss.find_data(lf) | [](auto ld) {
                              ld->get_file_ptr()->set_indexing(!ld->ld_visible);
                              ld->set_visibility(!ld->ld_visible);
                          };

                          auto top_view = *lnav_data.ld_view_stack.top();
                          auto tss = top_view->get_sub_source();

                          if (tss != nullptr) {
                              if (tss != &lss) {
                                  lss.text_filters_changed();
                                  lnav_data.ld_views[LNV_LOG].reload_data();
                              }
                              tss->text_filters_changed();
                              top_view->reload_data();
                          }

                          lv.reload_data();
                      });
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
            execute_command(lnav_data.ld_exec_context, "prompt search-files");
            return true;
        }
        case 'X': {
            auto sel = files_model::from_selection(lv.get_selection());

            sel.match(
                [](files_model::no_selection) {},
                [&](files_model::error_selection& es) {
                    auto& fc = lnav_data.ld_active_files;

                    fc.fc_file_names.erase(es.sb_iter);

                    auto name_iter = fc.fc_file_names.begin();
                    while (name_iter != fc.fc_file_names.end()) {
                        if (name_iter->first == es.sb_iter) {
                            name_iter = fc.fc_file_names.erase(name_iter);
                            continue;
                        }

                        auto rp_opt = humanize::network::path::from_str(
                            name_iter->first);

                        if (rp_opt) {
                            auto rp = *rp_opt;

                            if (fmt::to_string(rp.home()) == es.sb_iter) {
                                fc.fc_other_files.erase(name_iter->first);
                                name_iter = fc.fc_file_names.erase(name_iter);
                                continue;
                            }
                        }
                        ++name_iter;
                    }

                    fc.fc_name_to_errors->writeAccess()->erase(es.sb_iter);
                    fc.fc_invalidate_merge = true;
                    lv.reload_data();
                },
                [](files_model::other_selection) {},
                [](files_model::file_selection) {});
            return true;
        }
    }
    return false;
}

void
files_sub_source::list_input_handle_scroll_out(listview_curses& lv)
{
    lnav_data.ld_mode = ln_mode_t::PAGING;
    lnav_data.ld_filter_view.reload_data();
}

size_t
files_sub_source::text_line_count()
{
    const auto& fc = lnav_data.ld_active_files;
    auto retval = fc.fc_name_to_errors->readAccess()->size()
        + fc.fc_other_files.size() + fc.fc_files.size();

    return retval;
}

size_t
files_sub_source::text_line_width(textview_curses& curses)
{
    return 512;
}

void
files_sub_source::text_value_for_line(textview_curses& tc,
                                      int line,
                                      std::string& value_out,
                                      text_sub_source::line_flags_t flags)
{
    bool selected
        = lnav_data.ld_mode == ln_mode_t::FILES && line == tc.get_selection();
    const auto dim = tc.get_dimensions();
    const auto& fc = lnav_data.ld_active_files;
    auto filename_width
        = std::min(fc.fc_largest_path_length,
                   std::max((size_t) 40, (size_t) dim.second - 30));

    this->fss_curr_line.clear();
    auto& al = this->fss_curr_line;
    attr_line_builder alb(al);

    if (selected) {
        al.append(" ", VC_GRAPHIC.value(ACS_RARROW));
    } else {
        al.append(" ");
    }
    {
        safe::ReadAccess<safe_name_to_errors> errs(*fc.fc_name_to_errors);

        if (line < errs->size()) {
            auto iter = std::next(errs->begin(), line);
            auto path = std::filesystem::path(iter->first);
            auto fn = fmt::to_string(path.filename());

            truncate_to(fn, filename_width);
            al.append("   ");
            {
                auto ag = alb.with_attr(VC_ROLE.value(role_t::VCR_ERROR));

                al.appendf(FMT_STRING("{:<{}}"), fn, filename_width);
            }
            al.append("   ").append(iter->second.fei_description);
            if (selected) {
                al.with_attr_for_all(
                    VC_ROLE.value(role_t::VCR_DISABLED_FOCUSED));
            }

            value_out = al.get_string();
            return;
        }

        line -= errs->size();
    }

    if (line < fc.fc_other_files.size()) {
        auto iter = std::next(fc.fc_other_files.begin(), line);
        auto path = std::filesystem::path(iter->first);
        auto fn = fmt::to_string(path);

        truncate_to(fn, filename_width);
        al.append("   ");
        {
            auto ag = alb.with_attr(VC_ROLE.value(role_t::VCR_FILE));

            al.appendf(FMT_STRING("{:<{}}"), fn, filename_width);
        }
        al.append("   ")
            .appendf(FMT_STRING("{:14}"), iter->second.ofd_format)
            .append("  ")
            .append(iter->second.ofd_description);
        if (selected) {
            al.with_attr_for_all(VC_ROLE.value(role_t::VCR_DISABLED_FOCUSED));
        }
        if (line == fc.fc_other_files.size() - 1) {
            al.with_attr_for_all(VC_STYLE.value(text_attrs{A_UNDERLINE}));
        }

        value_out = al.get_string();
        return;
    }

    line -= fc.fc_other_files.size();

    const auto& lf = fc.fc_files[line];
    auto ld_opt = lnav_data.ld_log_source.find_data(lf);
    auto fn = fmt::to_string(std::filesystem::path(lf->get_unique_path()));
    char start_time[64] = "", end_time[64] = "";
    std::vector<std::string> file_notes;

    if (lf->get_format() != nullptr) {
        sql_strftime(start_time, sizeof(start_time), lf->front().get_timeval());
        sql_strftime(end_time, sizeof(end_time), lf->back().get_timeval());
    }
    truncate_to(fn, filename_width);
    for (const auto& pair : lf->get_notes()) {
        file_notes.push_back(pair.second);
    }

    al.append(" ");
    if (ld_opt) {
        if (ld_opt.value()->ld_visible) {
            al.append("\u25c6"_ok);
        } else {
            al.append("\u25c7"_comment);
        }
    } else {
        al.append("\u25c6"_comment);
    }
    al.append(" ");
    al.appendf(FMT_STRING("{:<{}}"), fn, filename_width);
    al.append("   ");
    {
        auto ag = alb.with_attr(VC_ROLE.value(role_t::VCR_NUMBER));

        al.appendf(FMT_STRING("{:>8}"),
                   humanize::file_size(lf->get_index_size(),
                                       humanize::alignment::columnar));
    }
    al.append(" ")
        .append(start_time)
        .append(" \u2014 ")
        .append(end_time)
        .append(" ")
        .appendf(FMT_STRING("{}"), fmt::join(file_notes, "; "));
    if (selected) {
        al.with_attr_for_all(VC_ROLE.value(role_t::VCR_FOCUSED));
    }

    value_out = al.get_string();
    this->fss_last_line_len
        = filename_width + 23 + strlen(start_time) + strlen(end_time);
}

void
files_sub_source::text_attrs_for_line(textview_curses& tc,
                                      int line,
                                      string_attrs_t& value_out)
{
    value_out = this->fss_curr_line.get_attrs();
}

size_t
files_sub_source::text_size_for_line(textview_curses& tc,
                                     int line,
                                     text_sub_source::line_flags_t raw)
{
    return 0;
}

static auto
spinner_index()
{
    auto now = ui_clock::now();

    return std::chrono::duration_cast<std::chrono::milliseconds>(
               now.time_since_epoch())
               .count()
        / 100;
}

bool
files_overlay_source::list_static_overlay(const listview_curses& lv,
                                          int y,
                                          int bottom,
                                          attr_line_t& value_out)
{
    if (y != 0) {
        return false;
    }
    static const char PROG[] = "-\\|/";
    constexpr size_t PROG_SIZE = sizeof(PROG) - 1;

    auto& fc = lnav_data.ld_active_files;
    auto fc_prog = fc.fc_progress;
    safe::WriteAccess<safe_scan_progress> sp(*fc_prog);

    if (!sp->sp_extractions.empty()) {
        const auto& prog = sp->sp_extractions.front();

        value_out.with_ansi_string(fmt::format(
            "{} Extracting " ANSI_COLOR(COLOR_CYAN) "{}" ANSI_NORM
                                                    "... {:>8}/{}",
            PROG[spinner_index() % PROG_SIZE],
            prog.ep_path.filename().string(),
            humanize::file_size(prog.ep_out_size, humanize::alignment::none),
            humanize::file_size(prog.ep_total_size,
                                humanize::alignment::none)));
        return true;
    }
    if (!sp->sp_tailers.empty()) {
        auto first_iter = sp->sp_tailers.begin();

        value_out.with_ansi_string(fmt::format(
            "{} Connecting to " ANSI_COLOR(COLOR_CYAN) "{}" ANSI_NORM ": {}",
            PROG[spinner_index() % PROG_SIZE],
            first_iter->first,
            first_iter->second.tp_message));
        return true;
    }

    return false;
}

bool
files_sub_source::text_handle_mouse(
    textview_curses& tc,
    const listview_curses::display_line_content_t&,
    mouse_event& me)
{
    if (me.is_click_in(mouse_button_t::BUTTON_LEFT, 1, 3)) {
        this->list_input_handle_key(tc, ' ');
    }
    if (me.is_double_click_in(mouse_button_t::BUTTON_LEFT, line_range{4, -1})) {
        this->list_input_handle_key(tc, '\r');
    }

    return false;
}
