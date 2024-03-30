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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <algorithm>
#include <regex>

#include "help_text_formatter.hh"

#include "base/ansi_scrubber.hh"
#include "base/attr_line.builder.hh"
#include "base/string_util.hh"
#include "config.h"
#include "fmt/format.h"
#include "fmt/printf.h"
#include "readline_highlighters.hh"

using namespace lnav::roles::literals;

std::multimap<std::string, help_text*> help_text::TAGGED;

static std::vector<help_text*>
get_related(const help_text& ht)
{
    std::vector<help_text*> retval;

    for (const auto& tag : ht.ht_tags) {
        auto tagged = help_text::TAGGED.equal_range(tag);

        for (auto tag_iter = tagged.first; tag_iter != tagged.second;
             ++tag_iter)
        {
            if (tag_iter->second == &ht) {
                continue;
            }

            help_text& related = *tag_iter->second;

            if (!related.ht_opposites.empty()
                && find_if(related.ht_opposites.begin(),
                           related.ht_opposites.end(),
                           [&ht](const char* x) {
                               return strcmp(x, ht.ht_name) == 0;
                           })
                    == related.ht_opposites.end())
            {
                continue;
            }

            retval.push_back(&related);
        }
    }

    return retval;
}

void
format_help_text_for_term(const help_text& ht,
                          size_t width,
                          attr_line_t& out,
                          help_text_content htc)
{
    static const size_t body_indent = 2;

    attr_line_builder alb(out);
    text_wrap_settings tws;
    size_t start_index = out.get_string().length();

    tws.with_width(width);

    switch (ht.ht_context) {
        case help_context_t::HC_COMMAND: {
            auto line_start = out.al_string.length();

            out.append(":").append(lnav::roles::symbol(ht.ht_name));
            for (const auto& param : ht.ht_parameters) {
                out.append(" ");
                if (param.ht_nargs == help_nargs_t::HN_OPTIONAL) {
                    out.append("[");
                }
                out.append(lnav::roles::variable(param.ht_name));
                if (param.ht_nargs == help_nargs_t::HN_OPTIONAL) {
                    out.append("]");
                }
                if (param.ht_nargs == help_nargs_t::HN_ONE_OR_MORE) {
                    out.append("1"_variable);
                    out.append(" [");
                    out.append("..."_variable);
                    out.append(" ");
                    out.append(lnav::roles::variable(param.ht_name));
                    out.append("N"_variable);
                    out.append("]");
                }
            }
            out.with_attr(string_attr{
                line_range{(int) line_start, (int) out.get_string().length()},
                VC_ROLE.value(role_t::VCR_H3),
            });
            if (htc != help_text_content::synopsis) {
                alb.append("\n")
                    .append(lnav::roles::table_border(
                        repeat("\u2550", tws.tws_width)))
                    .append("\n")
                    .indent(body_indent)
                    .append(attr_line_t::from_ansi_str(ht.ht_summary),
                            &tws.with_indent(body_indent))
                    .append("\n");
            }
            break;
        }
        case help_context_t::HC_SQL_FUNCTION:
        case help_context_t::HC_SQL_TABLE_VALUED_FUNCTION: {
            auto line_start = out.al_string.length();
            bool break_all = false;
            bool needs_comma = false;

            out.append(lnav::roles::symbol(ht.ht_name)).append("(");
            for (const auto& param : ht.ht_parameters) {
                if (!param.ht_flag_name && needs_comma) {
                    out.append(", ");
                }
                if (break_all
                    || (int) (out.get_string().length() - line_start + 10)
                        >= tws.tws_width)
                {
                    out.append("\n");
                    line_start = out.get_string().length();
                    alb.indent(body_indent + strlen(ht.ht_name) + 1);
                    break_all = true;
                }
                if (param.ht_flag_name) {
                    out.append(" ")
                        .append(lnav::roles::symbol(param.ht_flag_name))
                        .append(" ");
                }
                if (param.ht_nargs == help_nargs_t::HN_OPTIONAL) {
                    out.append("[");
                }
                out.append(lnav::roles::variable(param.ht_name));
                if (param.ht_nargs == help_nargs_t::HN_OPTIONAL) {
                    out.append("]");
                }
                if (param.ht_nargs == help_nargs_t::HN_ZERO_OR_MORE
                    || param.ht_nargs == help_nargs_t::HN_ONE_OR_MORE)
                {
                    out.append(", ...");
                }
                needs_comma = true;
            }
            out.append(")");
            out.with_attr(string_attr{
                line_range{(int) line_start, (int) out.get_string().length()},
                VC_ROLE.value(role_t::VCR_H3),
            });
            if (htc != help_text_content::synopsis) {
                if (break_all) {
                    alb.append("\n")
                        .append(lnav::roles::table_border(
                            repeat("\u2550", tws.tws_width)))
                        .append("\n")
                        .indent(body_indent + strlen(ht.ht_name) + 1);
                } else {
                    alb.append("\n")
                        .append(lnav::roles::table_border(
                            repeat("\u2550", tws.tws_width)))
                        .append("\n")
                        .indent(body_indent);
                }
                out.append(attr_line_t::from_ansi_str(ht.ht_summary),
                           &tws.with_indent(body_indent))
                    .append("\n");
            }
            break;
        }
        case help_context_t::HC_SQL_COMMAND: {
            auto line_start = out.al_string.length();

            out.append(";").append(lnav::roles::symbol(ht.ht_name));
            for (const auto& param : ht.ht_parameters) {
                out.append(" ");
                if (param.ht_nargs == help_nargs_t::HN_OPTIONAL) {
                    out.append("[");
                }
                out.append(lnav::roles::variable(param.ht_name));
                if (param.ht_nargs == help_nargs_t::HN_OPTIONAL) {
                    out.append("]");
                }
                if (param.ht_nargs == help_nargs_t::HN_ONE_OR_MORE) {
                    out.append("1"_variable);
                    out.append(" [");
                    out.append("..."_variable);
                    out.append(" ");
                    out.append(lnav::roles::variable(param.ht_name));
                    out.append("N"_variable);
                    out.append("]");
                }
            }
            out.with_attr(string_attr{
                line_range{(int) line_start, (int) out.get_string().length()},
                VC_ROLE.value(role_t::VCR_H3),
            });
            if (htc != help_text_content::synopsis) {
                alb.append("\n")
                    .append(lnav::roles::table_border(
                        repeat("\u2550", tws.tws_width)))
                    .append("\n")
                    .indent(body_indent)
                    .append(attr_line_t::from_ansi_str(ht.ht_summary),
                            &tws.with_indent(body_indent + 2))
                    .append("\n");
            }
            break;
        }
        case help_context_t::HC_SQL_INFIX:
        case help_context_t::HC_SQL_KEYWORD: {
            size_t line_start = out.get_string().length();
            bool break_all = false;
            bool is_infix = ht.ht_context == help_context_t::HC_SQL_INFIX;

            if (is_infix) {
                out.append(ht.ht_name);
            } else {
                out.append(lnav::roles::keyword(ht.ht_name));
            }
            for (const auto& param : ht.ht_parameters) {
                if (break_all
                    || (int) (out.get_string().length() - start_index
                              - line_start + 10)
                        >= tws.tws_width)
                {
                    out.append("\n");
                    line_start = out.get_string().length();
                    alb.indent(body_indent + strlen(ht.ht_name) + 1);
                    break_all = true;
                }
                if (param.ht_nargs == help_nargs_t::HN_ZERO_OR_MORE
                    || param.ht_nargs == help_nargs_t::HN_OPTIONAL)
                {
                    if (!break_all) {
                        out.append(" ");
                    }
                    out.append("[");
                }
                if (param.ht_flag_name) {
                    out.ensure_space().append(
                        lnav::roles::keyword(param.ht_flag_name));
                }
                if (param.ht_group_start) {
                    out.ensure_space().append(
                        lnav::roles::keyword(param.ht_group_start));
                }
                if (param.ht_name[0]) {
                    out.ensure_space().append(
                        lnav::roles::variable(param.ht_name));
                    if (!param.ht_parameters.empty()) {
                        if (param.ht_nargs == help_nargs_t::HN_ZERO_OR_MORE
                            || param.ht_nargs == help_nargs_t::HN_ONE_OR_MORE)
                        {
                            out.append("1"_variable);
                        }
                        if (param.ht_parameters[0].ht_flag_name) {
                            out.append(" ")
                                .append(lnav::roles::keyword(
                                    param.ht_parameters[0].ht_flag_name))
                                .append(" ");
                        }
                        out.append(lnav::roles::variable(
                            param.ht_parameters[0].ht_name));
                    }
                }
                if (param.ht_nargs == help_nargs_t::HN_ZERO_OR_MORE
                    || param.ht_nargs == help_nargs_t::HN_ONE_OR_MORE)
                {
                    bool needs_comma = param.ht_parameters.empty()
                        || !param.ht_flag_name;

                    out.append("1"_variable)
                        .append(" [")
                        .append(needs_comma ? ", " : "")
                        .append("...")
                        .append(needs_comma ? "" : " ")
                        .append(lnav::roles::keyword(
                            (needs_comma || !param.ht_flag_name)
                                ? ""
                                : param.ht_flag_name))
                        .append(" ")
                        .append(lnav::roles::variable(param.ht_name))
                        .append("N"_variable);
                    if (!param.ht_parameters.empty()) {
                        if (param.ht_parameters[0].ht_flag_name) {
                            out.append(" ")
                                .append(lnav::roles::keyword(
                                    param.ht_parameters[0].ht_flag_name))
                                .append(" ");
                        }

                        out.append(lnav::roles::variable(
                                       param.ht_parameters[0].ht_name))
                            .append("N"_variable);
                    }
                    out.append("]");
                }
                if (param.ht_group_end) {
                    out.ensure_space().append(
                        lnav::roles::keyword(param.ht_group_end));
                }
                if (param.ht_nargs == help_nargs_t::HN_ZERO_OR_MORE
                    || param.ht_nargs == help_nargs_t::HN_OPTIONAL)
                {
                    out.append("]");
                }
            }
            out.with_attr(string_attr{
                line_range{(int) line_start, (int) out.get_string().length()},
                VC_ROLE.value(role_t::VCR_H3),
            });
            if (htc != help_text_content::synopsis) {
                alb.append("\n")
                    .append(lnav::roles::table_border(
                        repeat("\u2550", tws.tws_width)))
                    .append("\n")
                    .indent(body_indent)
                    .append(ht.ht_summary, &tws)
                    .append("\n");
            }
            break;
        }
        case help_context_t::HC_PRQL_TRANSFORM: {
            auto line_start = out.al_string.length();

            out.append(";").append(lnav::roles::symbol(ht.ht_name));
            for (const auto& param : ht.ht_parameters) {
                out.append(" ");
                if (param.ht_nargs == help_nargs_t::HN_OPTIONAL) {
                    out.append(lnav::roles::symbol(param.ht_name));
                    out.append(":");
                    if (param.ht_default_value) {
                        out.append(param.ht_default_value);
                    } else {
                        out.append("null");
                    }
                } else {
                    if (param.ht_group_start) {
                        out.append(param.ht_group_start);
                    }
                    out.append(lnav::roles::variable(param.ht_name));
                }
                if (param.ht_nargs == help_nargs_t::HN_ONE_OR_MORE) {
                    out.append("1"_variable);
                    out.append(" [");
                    out.append("..."_variable);
                    out.append(" ");
                    out.append(lnav::roles::variable(param.ht_name));
                    out.append("N"_variable);
                    out.append("]");
                }
                if (param.ht_group_end) {
                    out.append(param.ht_group_end);
                }
            }
            out.with_attr(string_attr{
                line_range{(int) line_start, (int) out.get_string().length()},
                VC_ROLE.value(role_t::VCR_H3),
            });
            if (htc != help_text_content::synopsis) {
                alb.append("\n")
                    .append(lnav::roles::table_border(
                        repeat("\u2550", tws.tws_width)))
                    .append("\n")
                    .indent(body_indent)
                    .append(attr_line_t::from_ansi_str(ht.ht_summary),
                            &tws.with_indent(body_indent + 2))
                    .append("\n");
            }
            break;
        }
        case help_context_t::HC_PRQL_FUNCTION: {
            auto line_start = out.al_string.length();

            out.append(lnav::roles::symbol(ht.ht_name));
            for (const auto& param : ht.ht_parameters) {
                out.append(" ");
                out.append(lnav::roles::variable(param.ht_name));
                if (param.ht_nargs == help_nargs_t::HN_ONE_OR_MORE) {
                    out.append("1"_variable);
                    out.append(" [");
                    out.append("..."_variable);
                    out.append(" ");
                    out.append(lnav::roles::variable(param.ht_name));
                    out.append("N"_variable);
                    out.append("]");
                }
            }
            out.with_attr(string_attr{
                line_range{(int) line_start, (int) out.get_string().length()},
                VC_ROLE.value(role_t::VCR_H3),
            });
            if (htc != help_text_content::synopsis) {
                alb.append("\n")
                    .append(lnav::roles::table_border(
                        repeat("\u2550", tws.tws_width)))
                    .append("\n")
                    .indent(body_indent)
                    .append(attr_line_t::from_ansi_str(ht.ht_summary),
                            &tws.with_indent(body_indent + 2))
                    .append("\n");
            }
            break;
        }
        default:
            break;
    }

    if (htc == help_text_content::full && !ht.ht_parameters.empty()) {
        size_t max_param_name_width = 0;

        for (const auto& param : ht.ht_parameters) {
            max_param_name_width
                = std::max(strlen(param.ht_name), max_param_name_width);
        }

        out.append(ht.ht_parameters.size() == 1 ? "Parameter"_h4
                                                : "Parameters"_h4)
            .append("\n");

        for (const auto& param : ht.ht_parameters) {
            if (!param.ht_summary) {
                continue;
            }

            alb.indent(body_indent)
                .append(lnav::roles::variable(param.ht_name))
                .append(max_param_name_width - strlen(param.ht_name), ' ')
                .append("   ")
                .append(attr_line_t::from_ansi_str(param.ht_summary),
                        &(tws.with_indent(2 + max_param_name_width + 3)))
                .append("\n");
            if (!param.ht_enum_values.empty()) {
                alb.indent(body_indent + max_param_name_width)
                    .append("   ")
                    .append("Values"_h5)
                    .append(": ");
                auto initial = true;
                for (const auto* ename : param.ht_enum_values) {
                    if (!initial) {
                        alb.append("|");
                    }
                    alb.append(lnav::roles::symbol(ename));
                    initial = false;
                }
                alb.append("\n");
            }
            if (!param.ht_parameters.empty()) {
                for (const auto& sub_param : param.ht_parameters) {
                    alb.indent(body_indent + max_param_name_width + 3)
                        .append(lnav::roles::variable(sub_param.ht_name))
                        .append(" - ")
                        .append(
                            attr_line_t::from_ansi_str(sub_param.ht_summary),
                            &(tws.with_indent(2 + max_param_name_width + 5)))
                        .append("\n");
                }
            }
        }
    }
    if (htc == help_text_content::full && !ht.ht_results.empty()) {
        size_t max_result_name_width = 0;

        for (const auto& result : ht.ht_results) {
            max_result_name_width
                = std::max(strlen(result.ht_name), max_result_name_width);
        }

        out.append(ht.ht_results.size() == 1 ? "Result"_h4 : "Results"_h4)
            .append("\n");

        for (const auto& result : ht.ht_results) {
            if (!result.ht_summary) {
                continue;
            }

            alb.indent(body_indent)
                .append(lnav::roles::variable(result.ht_name))
                .append(max_result_name_width - strlen(result.ht_name), ' ')
                .append("   ")
                .append(attr_line_t::from_ansi_str(result.ht_summary),
                        &(tws.with_indent(2 + max_result_name_width + 3)))
                .append("\n");
        }
    }
    if (htc == help_text_content::full && !ht.ht_tags.empty()) {
        auto related_help = get_related(ht);
        auto related_refs = std::vector<std::string>();

        for (const auto* related : related_help) {
            std::string name = related->ht_name;
            switch (related->ht_context) {
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
            related_refs.push_back(name);
        }
        stable_sort(related_refs.begin(), related_refs.end());

        alb.append("See Also"_h4).append("\n").indent(body_indent);

        bool first = true;
        size_t line_start = out.get_string().length();
        for (const auto& ref : related_refs) {
            if (!first) {
                out.append(", ");
            }
            if ((out.get_string().length() - line_start + ref.length()) > width)
            {
                alb.append("\n").indent(body_indent);
                line_start = out.get_string().length();
            }
            out.append(lnav::roles::symbol(ref));
            first = false;
        }
    }
}

void
format_example_text_for_term(const help_text& ht,
                             const help_example_to_attr_line_fun_t eval,
                             size_t width,
                             attr_line_t& out,
                             help_example::language lang)
{
    if (ht.ht_example.empty()) {
        return;
    }

    attr_line_builder alb(out);
    int count = 1;

    out.append(ht.ht_example.size() == 1 ? "Example"_h4 : "Examples"_h4)
        .append("\n");
    for (const auto& ex : ht.ht_example) {
        if (ex.he_language != lang) {
            continue;
        }

        attr_line_t ex_line(ex.he_cmd);
        const char* prompt = "";
        text_wrap_settings tws;

        tws.with_width(width);
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
            case help_context_t::HC_SQL_INFIX:
            case help_context_t::HC_SQL_KEYWORD:
            case help_context_t::HC_SQL_FUNCTION:
            case help_context_t::HC_SQL_TABLE_VALUED_FUNCTION:
            case help_context_t::HC_PRQL_TRANSFORM:
            case help_context_t::HC_PRQL_FUNCTION:
                readline_sqlite_highlighter(ex_line, 0);
                prompt = ";";
                break;
            default:
                break;
        }

        ex_line.pad_to(50).with_attr_for_all(
            VC_ROLE.value(role_t::VCR_QUOTED_CODE));
        auto ex_result
            = eval(ht, ex).with_attr_for_all(SA_PREFORMATTED.value());
        alb.append("#")
            .append(fmt::to_string(count))
            .append(" ")
            .append(ex.he_description, &tws.with_indent(3))
            .append(":\n")
            .indent(3)
            .append(prompt, VC_ROLE.value(role_t::VCR_QUOTED_CODE))
            .append(ex_line, &tws.with_indent(3).with_padding_indent(3))
            .append("\n")
            .indent(3)
            .append(ex_result, &tws.with_indent(0))
            .append("\n");

        count += 1;
    }
}

static std::string
link_name(const help_text& ht)
{
    const static std::regex SCRUBBER("[^\\w_]");

    bool is_sql_infix = ht.ht_context == help_context_t::HC_SQL_INFIX;
    std::string scrubbed_name;

    if (is_sql_infix) {
        scrubbed_name = "infix";
    } else {
        if (ht.ht_context == help_context_t::HC_PRQL_TRANSFORM) {
            scrubbed_name += "prql_";
        }
        scrubbed_name += ht.ht_name;
        if (scrubbed_name[0] == '.') {
            scrubbed_name.erase(scrubbed_name.begin());
            scrubbed_name.insert(0, "dot_");
        }
    }
    if (ht.ht_function_type == help_function_type_t::HFT_AGGREGATE) {
        scrubbed_name += "_agg";
    }
    for (const auto& param : ht.ht_parameters) {
        if (!is_sql_infix && param.ht_name[0]) {
            continue;
        }
        if (!param.ht_flag_name) {
            continue;
        }

        scrubbed_name += "_";
        scrubbed_name += param.ht_flag_name;
    }
    scrubbed_name = std::regex_replace(scrubbed_name, SCRUBBER, "_");

    return tolower(scrubbed_name);
}

void
format_help_text_for_rst(const help_text& ht,
                         const help_example_to_attr_line_fun_t eval,
                         FILE* rst_file)
{
    const char* prefix;
    int out_count = 0;

    if (!ht.ht_name || !ht.ht_name[0]) {
        return;
    }

    bool is_sql_func = false, is_sql = false, is_prql = false;
    switch (ht.ht_context) {
        case help_context_t::HC_COMMAND:
            prefix = ":";
            break;
        case help_context_t::HC_SQL_COMMAND:
            prefix = ";";
            break;
        case help_context_t::HC_SQL_FUNCTION:
        case help_context_t::HC_SQL_TABLE_VALUED_FUNCTION:
            is_sql = is_sql_func = true;
            prefix = "";
            break;
        case help_context_t::HC_SQL_INFIX:
        case help_context_t::HC_SQL_KEYWORD:
            is_sql = true;
            prefix = "";
            break;
        case help_context_t::HC_PRQL_TRANSFORM:
        case help_context_t::HC_PRQL_FUNCTION:
            is_sql = true;
            is_prql = true;
            prefix = "";
            break;
        default:
            prefix = "";
            break;
    }

    fmt::print(rst_file, FMT_STRING("\n.. _{}:\n\n"), link_name(ht));
    out_count += fmt::fprintf(rst_file, "%s%s", prefix, ht.ht_name);
    if (is_sql_func) {
        out_count += fmt::fprintf(rst_file, "(");
    }
    bool needs_comma = false;
    for (const auto& param : ht.ht_parameters) {
        if (needs_comma) {
            if (param.ht_flag_name) {
                out_count += fmt::fprintf(rst_file, " ");
            } else {
                out_count += fmt::fprintf(rst_file, ", ");
            }
        }
        if (!is_sql_func) {
            out_count += fmt::fprintf(rst_file, " ");
        }

        if (param.ht_flag_name) {
            out_count += fmt::fprintf(rst_file, "%s ", param.ht_flag_name);
        }
        if (param.ht_name[0]) {
            out_count += fmt::fprintf(rst_file, "*");
            if (param.ht_nargs == help_nargs_t::HN_OPTIONAL) {
                out_count += fmt::fprintf(rst_file, "\\[");
            }
            out_count += fmt::fprintf(rst_file, "%s", param.ht_name);
            if (is_prql && param.ht_default_value) {
                out_count += fmt::fprintf(rst_file, ":");
                out_count
                    += fmt::fprintf(rst_file, "%s", param.ht_default_value);
            }
            if (param.ht_nargs == help_nargs_t::HN_OPTIONAL) {
                out_count += fmt::fprintf(rst_file, "\\]");
            }
            out_count += fmt::fprintf(rst_file, "*");
        }
        if (is_sql_func) {
            needs_comma = true;
        }
    }
    if (is_sql_func) {
        out_count += fmt::fprintf(rst_file, ")");
    }
    fmt::fprintf(rst_file, "\n");
    fmt::print(rst_file, FMT_STRING("{0:^^{1}}\n\n"), "", out_count);

    fmt::fprintf(rst_file, "  %s\n", ht.ht_summary);
    fmt::fprintf(rst_file, "\n");

    if (!ht.ht_prql_path.empty()) {
        fmt::print(rst_file,
                   FMT_STRING("  **PRQL Name**: {}\n\n"),
                   fmt::join(ht.ht_prql_path, "."));
    }

    if (ht.ht_description != nullptr) {
        fmt::fprintf(rst_file, "  %s\n", ht.ht_description);
    }

    int param_count = 0;
    for (const auto& param : ht.ht_parameters) {
        if (param.ht_summary && param.ht_summary[0]) {
            param_count += 1;
        }
    }

    if (param_count > 0) {
        fmt::fprintf(rst_file, "  **Parameters**\n");
        for (const auto& param : ht.ht_parameters) {
            if (param.ht_summary && param.ht_summary[0]) {
                fmt::fprintf(
                    rst_file,
                    "    * **%s%s** --- %s\n",
                    param.ht_name,
                    param.ht_nargs == help_nargs_t::HN_REQUIRED ? "\\*" : "",
                    param.ht_summary);

                if (!param.ht_parameters.empty()) {
                    fprintf(rst_file, "\n");
                    for (const auto& sub_param : param.ht_parameters) {
                        fmt::fprintf(
                            rst_file,
                            "      * **%s%s** --- %s\n",
                            sub_param.ht_name,
                            sub_param.ht_nargs == help_nargs_t::HN_REQUIRED
                                ? "\\*"
                                : "",
                            sub_param.ht_summary);
                    }
                }
            }
        }
        fmt::fprintf(rst_file, "\n");
    }
    if (is_sql) {
        prefix = ";";
    }
    if (!ht.ht_example.empty()) {
        fmt::fprintf(rst_file, "  **Examples**\n");
        for (const auto& example : ht.ht_example) {
            fmt::fprintf(rst_file, "    %s:\n\n", example.he_description);
            fmt::fprintf(rst_file,
                         "    .. code-block::  %s\n\n",
                         is_sql ? "custsqlite" : "lnav");
            if (ht.ht_context == help_context_t::HC_COMMAND) {
                fmt::fprintf(rst_file,
                             "      %s%s %s\n",
                             prefix,
                             ht.ht_name,
                             example.he_cmd);
            } else {
                fmt::fprintf(rst_file, "      %s%s\n", prefix, example.he_cmd);
            }
            auto result = eval(ht, example);
            if (!result.empty()) {
                std::vector<attr_line_t> lines;

                result.split_lines(lines);
                for (const auto& line : lines) {
                    fmt::fprintf(rst_file, "      %s\n", line.get_string());
                }
            }
            fmt::fprintf(rst_file, "\n");
        }
    }

    if (!ht.ht_tags.empty()) {
        auto related_refs = std::vector<std::string>();

        for (const auto* related : get_related(ht)) {
            related_refs.emplace_back(
                fmt::format(FMT_STRING(":ref:`{}`"), link_name(*related)));
        }
        stable_sort(related_refs.begin(), related_refs.end());

        fmt::print(rst_file,
                   FMT_STRING("  **See Also**\n    {}\n"),
                   fmt::join(related_refs, ", "));
    }

    fmt::fprintf(rst_file, "\n----\n\n");
}
