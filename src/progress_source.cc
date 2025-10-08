/**
 * Copyright (c) 2025, Timothy Stack
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

#include "progress_source.hh"

#include "base/progress.hh"

using namespace lnav::roles::literals;

void
progress_source::poll()
{
    this->ps_lines.clear();

    {
        auto& pt = lnav::progress_tracker::get_tasks();

        for (auto& bt : **pt.readAccess()) {
            auto tp = bt();

            if (tp.tp_status == lnav::progress_status_t::idle) {
                continue;
            }

            auto total_str = fmt::to_string(tp.tp_total);
            auto body = attr_line_t()
                            .appendf(FMT_STRING("{:>{}}/{} "),
                                     tp.tp_completed,
                                     total_str.size(),
                                     total_str)
                            .append(lnav::roles::keyword(tp.tp_id))
                            .append(" \u2014 ")
                            .append(tp.tp_step);
            auto lr = line_range{0, static_cast<int>(total_str.size())};
            body.al_attrs.emplace_back(lr, VC_ROLE.value(role_t::VCR_NUMBER));
            lr.lr_start = lr.lr_end + 1;
            lr.lr_end = lr.lr_start + total_str.size();
            body.al_attrs.emplace_back(lr, VC_ROLE.value(role_t::VCR_NUMBER));

            auto pct = (tp.tp_completed * 10) / tp.tp_total;
            auto al = attr_line_t(" \u231b [");
            if (tp.tp_completed > 0) {
                if (pct > 0 && tp.tp_completed < tp.tp_total) {
                    pct -= 1;
                }
                for (auto lpc = 0; lpc < pct; lpc++) {
                    al.append("\u2501"_ok);
                }
                if (tp.tp_completed < tp.tp_total) {
                    al.append("\u257e"_ok);
                }
            }
            al.pad_to(14).append("] ").append(body);

            this->ps_lines.emplace_back(std::move(al));
        }
    }
}

bool
progress_source::empty() const
{
    return this->ps_lines.empty();
}

size_t
progress_source::text_line_count()
{
    return this->ps_lines.size();
}

size_t
progress_source::text_line_width(textview_curses& curses)
{
    return text_sub_source::text_line_width(curses);
}

line_info
progress_source::text_value_for_line(textview_curses& tc,
                                     int line,
                                     std::string& value_out,
                                     line_flags_t flags)
{
    if (line < 0 || line >= this->ps_lines.size()) {
        return {};
    }

    value_out = this->ps_lines[line].al_string;

    return {};
}

size_t
progress_source::text_size_for_line(textview_curses& tc,
                                    int line,
                                    line_flags_t raw)
{
    if (line < 0 || line >= this->ps_lines.size()) {
        return 0;
    }

    return this->ps_lines[line].length();
}

void
progress_source::text_attrs_for_line(textview_curses& tc,
                                     int line,
                                     string_attrs_t& value_out)
{
    if (line < 0 || line >= this->ps_lines.size()) {
        return;
    }

    value_out = this->ps_lines[line].al_attrs;
}

static lnav::task_progress
dummy_prog_rep()
{
    return {
        "__dummy__",
    };
}

DIST_SLICE(prog_reps) lnav::progress_reporter_t dummy_rep = dummy_prog_rep;
