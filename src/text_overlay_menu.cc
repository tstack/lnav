/**
 * Copyright (c) 2024, Timothy Stack
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

#include "text_overlay_menu.hh"

#include "command_executor.hh"
#include "config.h"
#include "lnav.hh"
#include "lnav.prompt.hh"
#include "md4cpp.hh"
#include "readline_highlighters.hh"
#include "sysclip.hh"
#include "textview_curses.hh"

using namespace md4cpp::literals;
using namespace lnav::roles::literals;

std::vector<attr_line_t>
text_overlay_menu::list_overlay_menu(const listview_curses& lv, vis_line_t row)
{
    static constexpr auto MENU_WIDTH = 25;

    const auto* tc = dynamic_cast<const textview_curses*>(&lv);
    std::vector<attr_line_t> retval;

    if (tc->tc_text_selection_active || !tc->tc_selected_text) {
        return retval;
    }

    const auto* tss = tc->get_sub_source();
    const auto& sti = tc->tc_selected_text.value();
    const auto supports_filtering
        = tss != nullptr && tss->tss_supports_filtering;

    if (sti.sti_line != row) {
        return retval;
    }
    auto title = " Actions "_status_title;
    auto left = std::max(0, sti.sti_x - 2);
    auto dim = lv.get_dimensions();

    if (left + MENU_WIDTH >= dim.second) {
        left = dim.second - MENU_WIDTH;
    }

    this->los_menu_items.clear();

    auto is_link = !sti.sti_href.empty();
    auto menu_line = vis_line_t{1};
    attr_line_t link_cmd;
    if (is_link) {
        auto href_al
            = attr_line_t(" Link: ")
                  .append(lnav::roles::table_header(sti.sti_href))
                  .with_attr_for_all(VC_ROLE.value(role_t::VCR_STATUS_INFO))
                  .move();
        retval.emplace_back(href_al);
        menu_line += 1_vl;

        std::string filepath;
        auto file_attr_opt = get_string_attr(sti.sti_attrs, &L_FILE);
        if (file_attr_opt) {
            filepath = file_attr_opt.value()
                           ->sa_value.get<std::shared_ptr<logfile>>()
                           ->get_filename();
        }

        {
            logline_value_vector values;
            exec_context ec(&values, internal_sql_callback, pipe_callback);

            auto exec_res
                = ec.execute_with("|lnav-link-callback $href $filepath",
                                  std::make_pair("href", sti.sti_href),
                                  std::make_pair("filepath", filepath));
            if (exec_res.isOk()) {
                link_cmd = exec_res.unwrap();
            }
        }
        if (link_cmd.empty()) {
            link_cmd = ":open $href";
        }
        readline_lnav_highlighter(link_cmd, std::nullopt);

        auto cmd_al
            = attr_line_t(" ")
                  .append("Command"_table_header)
                  .append(": ")
                  .append(link_cmd)
                  .with_attr_for_all(VC_ROLE.value(role_t::VCR_STATUS_INFO))
                  .with_attr_for_all(
                      VC_STYLE.value(text_attrs::with_underline()))
                  .move();
        retval.emplace_back(cmd_al);
        menu_line += 1_vl;
    }

    retval.emplace_back(attr_line_t().pad_to(left).append(title));
    {
        static intern_string_t SRC = intern_string::lookup("menu");
        attr_line_t al;

        int start = left;
        if (is_link || supports_filtering) {
            if (is_link) {
                if (startswith(link_cmd.al_string, ":open")) {
                    al.append(":floppy_disk:"_emoji)
                        .append(" Open in lnav")
                        .append("  ");
                } else {
                    al.append(":play_button:"_emoji)
                        .append(" Execute")
                        .append("        ");
                }
            } else {
                al.append(" ").append("\u2714 Filter-in"_ok).append("   ");
            }
            this->los_menu_items.emplace_back(
                menu_line,
                line_range{start, start + (int) al.length()},
                [is_link, link_cmd, sti](const std::string& value) {
                    const auto cmd = is_link
                        ? link_cmd.get_string()
                        : fmt::format(FMT_STRING(":filter-in {}"),
                                      lnav::pcre2pp::quote(value));
                    auto& dls = lnav_data.ld_db_row_source;
                    auto previous_db_gen = dls.dls_generation;
                    auto src_guard
                        = lnav_data.ld_exec_context.enter_source(SRC, 1, cmd);
                    auto exec_res
                        = lnav_data.ld_exec_context
                              .with_provenance(exec_context::mouse_input{})
                              ->execute_with(
                                  cmd, std::make_pair("href", sti.sti_href));
                    if (exec_res.isOk()) {
                        lnav::prompt::get().p_editor.set_inactive_value(
                            exec_res.unwrap());
                        if (dls.dls_generation != previous_db_gen
                            && dls.dls_row_cursors.size() > 1)
                        {
                            ensure_view(&lnav_data.ld_views[LNV_DB]);
                        }
                    }
                });
            start += al.length();
        }

        if (is_link) {
            al.append("      ");
        } else {
            al.append(":mag_right:"_emoji).append(" Search ");
        }
        al.with_attr_for_all(VC_ROLE.value(role_t::VCR_STATUS));
        if (!is_link) {
            this->los_menu_items.emplace_back(
                menu_line,
                line_range{start, start + (int) al.length()},
                [](const std::string& value) {
                    auto cmd = fmt::format(FMT_STRING("/{}"),
                                           lnav::pcre2pp::quote(value));
                    auto src_guard
                        = lnav_data.ld_exec_context.enter_source(SRC, 1, cmd);
                    lnav_data.ld_exec_context
                        .with_provenance(exec_context::mouse_input{})
                        ->execute(cmd);
                });
        }
        retval.emplace_back(attr_line_t().pad_to(left).append(al));
    }
    menu_line += 1_vl;
    {
        attr_line_t al;

        int start = left;
        if (is_link || supports_filtering) {
            if (is_link) {
                al.append(":globe_with_meridians:"_emoji).append(" Open   ");
            } else {
                al.append(" ").append("\u2718 Filter-out"_error).append("  ");
            }
            this->los_menu_items.emplace_back(
                menu_line,
                line_range{start, start + (int) al.length()},
                [is_link, sti](const std::string& value) {
                    auto cmd = is_link
                        ? ":xopen $href"
                        : fmt::format(FMT_STRING(":filter-out {}"),
                                      lnav::pcre2pp::quote(value));
                    auto exec_res
                        = lnav_data.ld_exec_context
                              .with_provenance(exec_context::mouse_input{})
                              ->execute_with(
                                  cmd, std::make_pair("href", sti.sti_href));
                    if (exec_res.isOk()) {
                        lnav::prompt::get().p_editor.set_inactive_value(
                            exec_res.unwrap());
                    }
                });
            start += al.length();
        }
        al.append(":clipboard:"_emoji)
            .append(is_link ? " Copy link " : " Copy   ")
            .with_attr_for_all(VC_ROLE.value(role_t::VCR_STATUS));
        this->los_menu_items.emplace_back(
            menu_line,
            line_range{start, start + (int) al.length()},
            [is_link, sti](const std::string& value) {
                auto clip_res = sysclip::open(sysclip::type_t::GENERAL);
                if (clip_res.isErr()) {
                    log_error("unable to open clipboard: %s",
                              clip_res.unwrapErr().c_str());
                    return;
                }

                auto clip_pipe = clip_res.unwrap();
                if (is_link) {
                    fwrite(sti.sti_href.c_str(),
                           1,
                           sti.sti_href.length(),
                           clip_pipe.in());
                } else {
                    fwrite(value.c_str(), 1, value.length(), clip_pipe.in());
                }
            });
        retval.emplace_back(attr_line_t().pad_to(left).append(al));
    }

    return retval;
}
