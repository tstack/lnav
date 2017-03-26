/**
 * Copyright (c) 2017, Timothy Stack
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

#include <numeric>
#include <algorithm>

#include "ansi_scrubber.hh"
#include "help_text_formatter.hh"
#include "readline_highlighters.hh"

using namespace std;


void format_help_text_for_term(const help_text &ht, int width, attr_line_t &out)
{
    static size_t body_indent = 2;

    text_wrap_settings tws;

    tws.with_width(width);

    switch (ht.ht_context) {
        case HC_COMMAND: {
            out.append("Synopsis", &view_curses::VC_STYLE, A_UNDERLINE)
               .append("\n")
               .append(body_indent, ' ')
               .append(":")
               .append(ht.ht_name, &view_curses::VC_STYLE, A_BOLD);
            for (auto &param : ht.ht_parameters) {
                out.append(" ");
                out.append(param.ht_name, &view_curses::VC_STYLE, A_UNDERLINE);
                if (param.ht_nargs == HN_ONE_OR_MORE) {
                    out.append("1", &view_curses::VC_STYLE, A_UNDERLINE);
                    out.append(" [");
                    out.append("...", &view_curses::VC_STYLE, A_UNDERLINE);
                    out.append(" ");
                    out.append(param.ht_name, &view_curses::VC_STYLE, A_UNDERLINE);
                    out.append("N", &view_curses::VC_STYLE, A_UNDERLINE);
                    out.append("]");
                }
            }
            out.append(" - ")
                .append(attr_line_t::from_ansi_str(ht.ht_summary),
                        &tws.with_indent(body_indent + 4))
                .append("\n");
            break;
        }
        case HC_SQL_FUNCTION: {
            bool needs_comma = false;

            out.append(ht.ht_name, &view_curses::VC_STYLE, A_BOLD);
            out.append("(");
            for (auto &param : ht.ht_parameters) {
                if (needs_comma) {
                    out.append(", ");
                }
                out.append(param.ht_name, &view_curses::VC_STYLE, A_UNDERLINE);
                needs_comma = true;
            }
            out.append(") -- ")
               .append(attr_line_t::from_ansi_str(ht.ht_summary), &tws)
               .append("\n");
            break;
        }
    }

    if (!ht.ht_parameters.empty()) {
        size_t max_param_name_width = 0;

        for (auto &param : ht.ht_parameters) {
            max_param_name_width = std::max(strlen(param.ht_name), max_param_name_width);
        }

        out.append(ht.ht_parameters.size() == 1 ? "Parameter" : "Parameters",
                   &view_curses::VC_STYLE,
                   A_UNDERLINE)
           .append("\n");

        for (auto &param : ht.ht_parameters) {
            out.append(body_indent, ' ')
               .append(param.ht_name,
                       &view_curses::VC_STYLE,
                       view_colors::ansi_color_pair(COLOR_CYAN, COLOR_BLACK) | A_BOLD)
               .append(max_param_name_width - strlen(param.ht_name), ' ')
               .append("   ")
               .append(attr_line_t::from_ansi_str(param.ht_summary),
                       &(tws.with_indent(2 + max_param_name_width + 3)))
               .append("\n");
        }
    }

    if (!ht.ht_example.empty()) {
        map<string, string> vars;

        vars["name"] = ht.ht_name;
        add_ansi_vars(vars);

        out.append(ht.ht_example.size() == 1 ? "Example" : "Examples",
                   &view_curses::VC_STYLE,
                   A_UNDERLINE)
           .append("\n");
        for (auto &ex : ht.ht_example) {
            attr_line_t ex_line(ex.he_cmd);

            switch (ht.ht_context) {
                case HC_COMMAND:
                    ex_line.insert(0, 1, ' ');
                    ex_line.insert(0, 1, ':');
                    ex_line.insert(1, ht.ht_name);
                    readline_command_highlighter(ex_line, 0);
                    break;
            }

            out.append(body_indent, ' ')
               .append(ex_line, &tws.with_indent(body_indent + 2));
            out.append("\n");
        }
    }
}
