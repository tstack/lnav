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


std::multimap<std::string, help_text *> help_text::TAGGED;

void format_help_text_for_term(const help_text &ht, int width, attr_line_t &out, bool synopsis_only)
{
    static const size_t body_indent = 2;

    view_colors &vc = view_colors::singleton();
    text_wrap_settings tws;
    size_t start_index = out.get_string().length();

    tws.with_width(width);

    switch (ht.ht_context) {
        case help_context_t::HC_COMMAND: {
            out.append("Synopsis", &view_curses::VC_STYLE, A_UNDERLINE)
                .append("\n")
                .append(body_indent, ' ')
                .append(":")
                .append(ht.ht_name, &view_curses::VC_STYLE, A_BOLD);
            for (auto &param : ht.ht_parameters) {
                out.append(" ");
                if (param.ht_nargs == help_nargs_t::HN_OPTIONAL) {
                    out.append("[");
                }
                out.append(param.ht_name, &view_curses::VC_STYLE, A_UNDERLINE);
                if (param.ht_nargs == help_nargs_t::HN_OPTIONAL) {
                    out.append("]");
                }
                if (param.ht_nargs == help_nargs_t::HN_ONE_OR_MORE) {
                    out.append("1", &view_curses::VC_STYLE, A_UNDERLINE);
                    out.append(" [");
                    out.append("...", &view_curses::VC_STYLE, A_UNDERLINE);
                    out.append(" ");
                    out.append(param.ht_name, &view_curses::VC_STYLE,
                               A_UNDERLINE);
                    out.append("N", &view_curses::VC_STYLE, A_UNDERLINE);
                    out.append("]");
                }
            }
            out.append(" - ")
                .append(attr_line_t::from_ansi_str(ht.ht_summary),
                        &tws.with_indent(body_indent + 2))
                .append("\n");
            break;
        }
        case help_context_t::HC_SQL_FUNCTION:
        case help_context_t::HC_SQL_TABLE_VALUED_FUNCTION: {
            bool needs_comma = false;

            out.append("Synopsis", &view_curses::VC_STYLE, A_UNDERLINE)
                .append("\n")
                .append(body_indent, ' ')
                .append(ht.ht_name, &view_curses::VC_STYLE, A_BOLD)
                .append("(");
            for (auto &param : ht.ht_parameters) {
                if (param.ht_flag_name) {
                    out.append(" ")
                        .append(param.ht_flag_name, &view_curses::VC_STYLE, A_BOLD)
                        .append(" ");
                } else if (needs_comma) {
                    out.append(", ");
                }
                out.append(param.ht_name, &view_curses::VC_STYLE, A_UNDERLINE);
                if (param.ht_nargs == help_nargs_t::HN_ZERO_OR_MORE ||
                    param.ht_nargs == help_nargs_t::HN_ONE_OR_MORE) {
                    out.append(", ...");
                }
                needs_comma = true;
            }
            out.append(") -- ")
                .append(attr_line_t::from_ansi_str(ht.ht_summary),
                        &tws.with_indent(body_indent + 2))
                .append("\n");
            break;
        }
        case help_context_t::HC_SQL_KEYWORD: {
            size_t line_start = body_indent;
            bool break_all = false;

            if (!synopsis_only) {
                out.append("Synopsis", &view_curses::VC_STYLE, A_UNDERLINE)
                   .append("\n");
            }
            out.append(body_indent, ' ')
               .append(ht.ht_name, &view_curses::VC_STYLE, A_BOLD);
            for (auto &param : ht.ht_parameters) {
                if (break_all ||
                    (int)(out.get_string().length() - start_index - line_start + 10) >=
                    tws.tws_width) {
                    out.append("\n");
                    line_start = out.get_string().length();
                    out.append(body_indent + strlen(ht.ht_name) + 1, ' ');
                    break_all = true;
                }
                if (param.ht_nargs == help_nargs_t::HN_ZERO_OR_MORE ||
                    param.ht_nargs == help_nargs_t::HN_OPTIONAL) {
                    if (!break_all) {
                        out.append(" ");
                    }
                    out.append("[");
                }
                if (param.ht_flag_name) {
                    out.ensure_space()
                        .append(param.ht_flag_name, &view_curses::VC_STYLE,
                                A_BOLD);
                }
                if (param.ht_group_start) {
                    out.ensure_space()
                       .append(param.ht_group_start, &view_curses::VC_STYLE,
                               A_BOLD);
                }
                if (param.ht_name[0]) {
                    out.ensure_space()
                        .append(param.ht_name, &view_curses::VC_STYLE,
                                A_UNDERLINE);
                    if (!param.ht_parameters.empty()) {
                        if (param.ht_nargs == help_nargs_t::HN_ZERO_OR_MORE ||
                            param.ht_nargs == help_nargs_t::HN_ONE_OR_MORE) {
                            out.append("1", &view_curses::VC_STYLE, A_UNDERLINE);
                        }
                        if (param.ht_parameters[0].ht_flag_name) {
                            out.append(" ")
                                .append(param.ht_parameters[0].ht_flag_name,
                                        &view_curses::VC_STYLE,
                                        A_BOLD)
                                .append(" ");
                        }
                        out.append(param.ht_parameters[0].ht_name,
                                   &view_curses::VC_STYLE,
                                   A_UNDERLINE);
                    }
                }
                if (param.ht_nargs == help_nargs_t::HN_ZERO_OR_MORE ||
                    param.ht_nargs == help_nargs_t::HN_ONE_OR_MORE) {
                    bool needs_comma = param.ht_parameters.empty() ||
                                       !param.ht_flag_name;

                    out.append("1", &view_curses::VC_STYLE, A_UNDERLINE)
                        .append(" [")
                        .append(needs_comma ? ", " : "")
                        .append("...")
                        .append(needs_comma ? "" : " ")
                        .append((needs_comma || !param.ht_flag_name) ? "" : param.ht_flag_name,
                                &view_curses::VC_STYLE,
                                A_BOLD)
                        .append(" ")
                        .append(param.ht_name, &view_curses::VC_STYLE,
                                A_UNDERLINE)
                        .append("N", &view_curses::VC_STYLE, A_UNDERLINE);
                    if (!param.ht_parameters.empty()) {
                        if (param.ht_parameters[0].ht_flag_name) {
                            out.append(" ")
                                .append(param.ht_parameters[0].ht_flag_name,
                                        &view_curses::VC_STYLE,
                                        A_BOLD)
                                .append(" ");
                        }

                        out.append(param.ht_parameters[0].ht_name,
                                   &view_curses::VC_STYLE,
                                   A_UNDERLINE)
                            .append("N", &view_curses::VC_STYLE, A_UNDERLINE);
                    }
                    out.append("]");
                }
                if (param.ht_group_end) {
                    out.ensure_space()
                       .append(param.ht_group_end, &view_curses::VC_STYLE,
                               A_BOLD);
                }
                if (param.ht_nargs == help_nargs_t::HN_ZERO_OR_MORE ||
                    param.ht_nargs == help_nargs_t::HN_OPTIONAL) {
                    out.append("]");
                }
            }
            if (!synopsis_only) {
                out.append("\n\n")
                   .append(body_indent, ' ')
                   .append(ht.ht_summary, &tws)
                   .append("\n");
            } else {
                out.append("\n");
            }
            break;
        }
        default:
            break;
    }

    if (!synopsis_only && !ht.ht_parameters.empty()) {
        size_t max_param_name_width = 0;

        for (auto &param : ht.ht_parameters) {
            max_param_name_width = std::max(strlen(param.ht_name),
                                            max_param_name_width);
        }

        out.append(ht.ht_parameters.size() == 1 ? "Parameter" : "Parameters",
                   &view_curses::VC_STYLE,
                   A_UNDERLINE)
            .append("\n");

        for (auto &param : ht.ht_parameters) {
            if (!param.ht_summary) {
                continue;
            }

            out.append(body_indent, ' ')
                .append(param.ht_name,
                        &view_curses::VC_STYLE,
                        vc.attrs_for_role(view_colors::VCR_VARIABLE) | A_BOLD)
                .append(max_param_name_width - strlen(param.ht_name), ' ')
                .append("   ")
                .append(attr_line_t::from_ansi_str(param.ht_summary),
                        &(tws.with_indent(2 + max_param_name_width + 3)))
                .append("\n");
        }
    }
    if (!synopsis_only && !ht.ht_results.empty()) {
        size_t max_result_name_width = 0;

        for (auto &result : ht.ht_results) {
            max_result_name_width = std::max(strlen(result.ht_name),
                                            max_result_name_width);
        }

        out.append(ht.ht_results.size() == 1 ? "Result" : "Results",
                   &view_curses::VC_STYLE,
                   A_UNDERLINE)
           .append("\n");

        for (auto &result : ht.ht_results) {
            if (!result.ht_summary) {
                continue;
            }

            out.append(body_indent, ' ')
               .append(result.ht_name,
                       &view_curses::VC_STYLE,
                       vc.attrs_for_role(view_colors::VCR_VARIABLE) | A_BOLD)
               .append(max_result_name_width - strlen(result.ht_name), ' ')
               .append("   ")
               .append(attr_line_t::from_ansi_str(result.ht_summary),
                       &(tws.with_indent(2 + max_result_name_width + 3)))
               .append("\n");
        }
    }
    if (!synopsis_only && !ht.ht_tags.empty()) {
        vector<string> tags;

        for (const auto &tag : ht.ht_tags) {
            auto tagged = help_text::TAGGED.equal_range(tag);

            for (auto tag_iter = tagged.first;
                 tag_iter != tagged.second;
                 ++tag_iter) {
                if (tag_iter->second == &ht) {
                    continue;
                }

                help_text &related = *tag_iter->second;

                if (!related.ht_opposites.empty() &&
                    find_if(related.ht_opposites.begin(),
                            related.ht_opposites.end(),
                            [&ht](const char *x) {
                                return strcmp(x, ht.ht_name) == 0;
                            }) == related.ht_opposites.end()) {
                    continue;
                }

                string name = related.ht_name;
                switch (related.ht_context) {
                    case help_context_t::HC_COMMAND:
                        name = ":" + name;
                        break;
                    case help_context_t::HC_SQL_FUNCTION:
                    case help_context_t::HC_SQL_TABLE_VALUED_FUNCTION:
                        name = name + "()";
                        break;
                    default:
                        break;
                }
                tags.push_back(name);
            }
        }

        stable_sort(tags.begin(), tags.end());

        out.append("See Also", &view_curses::VC_STYLE, A_UNDERLINE)
           .append("\n")
           .append(body_indent, ' ');

        bool first = true;
        size_t line_start = out.length();
        for (const auto &tag : tags) {
            if (!first) {
                out.append(", ");
            }
            if ((out.length() - line_start + tag.length()) > width) {
                out.append("\n")
                   .append(body_indent, ' ');
                line_start = out.length();
            }
            out.append(tag, &view_curses::VC_STYLE, A_BOLD);
            first = false;
        }
    }
}

void format_example_text_for_term(const help_text &ht,
                                  const help_example_to_attr_line_fun_t eval,
                                  int width, attr_line_t &out)
{
    text_wrap_settings tws;

    tws.with_width(width);

    if (!ht.ht_example.empty()) {
        int count = 1;

        out.append(ht.ht_example.size() == 1 ? "Example" : "Examples",
                   &view_curses::VC_STYLE,
                   A_UNDERLINE)
           .append("\n");
        for (auto &ex : ht.ht_example) {
            attr_line_t ex_line(ex.he_cmd);
            size_t keyword_offset = 0;
            const char *space = strchr(ex.he_cmd, ' ');
            const char *prompt = "";

            if (space) {
                keyword_offset = space - ex.he_cmd;
            }
            if (count > 1) {
                out.append("\n");
            }
            switch (ht.ht_context) {
                case help_context_t::HC_COMMAND:
                    ex_line.insert(0, 1, ' ');
                    ex_line.insert(0, 1, ':');
                    ex_line.insert(1, ht.ht_name);
                    readline_command_highlighter(ex_line, 0);
                    break;
                case help_context_t::HC_SQL_KEYWORD:
                case help_context_t::HC_SQL_FUNCTION:
                case help_context_t::HC_SQL_TABLE_VALUED_FUNCTION:
                    readline_sqlite_highlighter(ex_line, 0);
                    prompt = ";";
                    break;
                default:
                    break;
            }

            out.append("#")
               .append(to_string(count))
               .append(" ")
               .append(prompt)
               .append(ex_line, &tws.with_indent(3 + keyword_offset + 1))
               .append("\n")
               .append(3, ' ')
               .append(eval(ht, ex), &tws.with_indent(3))
               .append("\n");

            count += 1;
        }
    }
}
