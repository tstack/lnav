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
#include "md4cpp.hh"
#include "textview_curses.hh"

using namespace md4cpp::literals;
using namespace lnav::roles::literals;

std::vector<attr_line_t>
text_overlay_menu::list_overlay_menu(const listview_curses& lv, vis_line_t row)
{
    const auto* tc = dynamic_cast<const textview_curses*>(&lv);
    std::vector<attr_line_t> retval;

    if (!tc->tc_text_selection_active && tc->tc_selected_text) {
        const auto& sti = tc->tc_selected_text.value();

        if (sti.sti_line == row) {
            auto title = " Filter   Other   "_status_title;
            auto left = std::max(0, sti.sti_x - 2);
            auto dim = lv.get_dimensions();

            if (left + title.first.length() >= dim.second) {
                left = dim.second - title.first.length() - 2;
            }

            this->tom_menu_items.clear();
            retval.emplace_back(attr_line_t().pad_to(left).append(title));
            {
                attr_line_t al;

                al.append(" ").append("\u2714 IN"_ok).append("   ");
                int start = left;
                this->tom_menu_items.emplace_back(
                    1_vl,
                    line_range{start, start + (int) al.length()},
                    [](const std::string& value) {
                        auto cmd = fmt::format(FMT_STRING(":filter-in {}"),
                                               lnav::pcre2pp::quote(value));
                        lnav_data.ld_exec_context
                            .with_provenance(exec_context::mouse_input{})
                            ->execute(cmd);
                    });
                start += al.length();
                al.append(":mag_right:"_emoji)
                    .append(" Search ")
                    .with_attr_for_all(VC_ROLE.value(role_t::VCR_STATUS));
                this->tom_menu_items.emplace_back(
                    1_vl,
                    line_range{start, start + (int) al.length()},
                    [](const std::string& value) {
                        auto cmd = fmt::format(FMT_STRING("/{}"),
                                               lnav::pcre2pp::quote(value));
                        lnav_data.ld_exec_context
                            .with_provenance(exec_context::mouse_input{})
                            ->execute(cmd);
                    });
                retval.emplace_back(attr_line_t().pad_to(left).append(al));
            }
            {
                attr_line_t al;

                al.append(" ").append("\u2718 OUT"_error).append("  ");
                int start = left;
                this->tom_menu_items.emplace_back(
                    2_vl,
                    line_range{start, start + (int) al.length()},
                    [](const std::string& value) {
                        auto cmd = fmt::format(FMT_STRING(":filter-out {}"),
                                               lnav::pcre2pp::quote(value));
                        lnav_data.ld_exec_context
                            .with_provenance(exec_context::mouse_input{})
                            ->execute(cmd);
                    });
                start += al.length();
                al.append(":clipboard:"_emoji)
                    .append(" Copy   ")
                    .with_attr_for_all(VC_ROLE.value(role_t::VCR_STATUS));
                this->tom_menu_items.emplace_back(
                    2_vl,
                    line_range{start, start + (int) al.length()},
                    [](const std::string& value) {
                        lnav_data.ld_exec_context.execute("|lnav-copy-text");
                    });
                retval.emplace_back(attr_line_t().pad_to(left).append(al));
            }
        }
    }

    return retval;
}
