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

#include <algorithm>
#include <optional>
#include <utility>

#include "cmd.parser.hh"

#include "base/attr_line.hh"
#include "base/itertools.enumerate.hh"
#include "base/lnav_log.hh"
#include "data_scanner.hh"
#include "shlex.hh"
#include "sql_help.hh"
#include "sql_util.hh"

namespace lnav::command {

static bool
is_separator(data_token_t tok)
{
    switch (tok) {
        case DT_COLON:
        case DT_EQUALS:
        case DT_COMMA:
        case DT_SEMI:
        case DT_EMDASH:
        case DT_LCURLY:
        case DT_RCURLY:
        case DT_LSQUARE:
        case DT_RSQUARE:
        case DT_LPAREN:
        case DT_RPAREN:
        case DT_LANGLE:
        case DT_RANGLE:
        case DT_LINE:
        case DT_WHITE:
        case DT_DOT:
        case DT_ESCAPED_CHAR:
            return true;
        default:
            return false;
    }
}

std::optional<parsed::arg_at_result>
parsed::arg_at(int x) const
{
    log_debug("BEGIN arg_at");
    for (const auto& se : this->p_free_args) {
        if (se.se_origin.sf_begin <= x && x <= se.se_origin.sf_end) {
            log_debug("  free arg [%d:%d) '%s'",
                      se.se_origin.sf_begin,
                      se.se_origin.sf_end,
                      se.se_value.c_str());
            return arg_at_result{this->p_help, false, se};
        }
    }
    for (const auto& arg : this->p_args) {
        log_debug(
            "  arg %s[%d]", arg.first.c_str(), arg.second.a_values.size());
        for (const auto& [index, se] :
             lnav::itertools::enumerate(arg.second.a_values))
        {
            log_debug("    val [%d:%d) '%.*s' -> '%s'",
                      se.se_origin.sf_begin,
                      se.se_origin.sf_end,
                      se.se_origin.length(),
                      se.se_origin.data(),
                      se.se_value.c_str());
            if (se.se_origin.sf_begin <= x && x <= se.se_origin.sf_end) {
                switch (arg.second.a_help->ht_format) {
                    case help_parameter_format_t::HPF_SQL:
                    case help_parameter_format_t::HPF_SQL_EXPR: {
                        auto al = attr_line_t(se.se_value);
                        auto al_x = x - se.se_origin.sf_begin;

                        annotate_sql_statement(al);
                        for (const auto& attr : al.al_attrs) {
                            if (al_x < attr.sa_range.lr_start
                                || attr.sa_range.lr_end < al_x)
                            {
                                continue;
                            }

                            auto sf = al.to_string_fragment(attr);
                            if (attr.sa_type == &SQL_GARBAGE_ATTR
                                && attr.sa_range.length() == 1)
                            {
                                switch (al.al_string[attr.sa_range.lr_start]) {
                                    case ':':
                                    case '$':
                                    case '@':
                                        return arg_at_result{
                                            arg.second.a_help,
                                            false,
                                            {
                                                sf,
                                                sf.to_string(),
                                            },
                                        };
                                }
                            }

                            if (attr.sa_type != &SQL_IDENTIFIER_ATTR
                                && attr.sa_type != &SQL_STRING_ATTR
                                && attr.sa_type != &SQL_KEYWORD_ATTR)
                            {
                                continue;
                            }
                            return arg_at_result{
                                arg.second.a_help,
                                false,
                                {
                                    sf,
                                    sf.to_string(),
                                },
                            };
                        }
                        return arg_at_result{arg.second.a_help, false, {}};
                    }
                    case help_parameter_format_t::HPF_ALL_FILTERS:
                    case help_parameter_format_t::HPF_ENABLED_FILTERS:
                    case help_parameter_format_t::HPF_DISABLED_FILTERS:
                    case help_parameter_format_t::HPF_HIGHLIGHTS: {
                        return arg_at_result{arg.second.a_help, true, se};
                    }
                    case help_parameter_format_t::HPF_CONFIG_VALUE:
                    case help_parameter_format_t::HPF_MULTILINE_TEXT:
                    case help_parameter_format_t::HPF_TEXT:
                    case help_parameter_format_t::HPF_LOCATION:
                    case help_parameter_format_t::HPF_REGEX:
                    case help_parameter_format_t::HPF_TIME_FILTER_POINT: {
                        std::optional<data_scanner::capture_t> cap_to_start;
                        data_scanner ds(se.se_origin, false);

                        while (true) {
                            auto tok_res = ds.tokenize2();

                            if (!tok_res) {
                                break;
                            }
                            auto tok = tok_res.value();

                            log_debug("cap b:%d  x:%d  e:%d %s",
                                      tok.tr_capture.c_begin,
                                      x,
                                      tok.tr_capture.c_end,
                                      data_scanner::token2name(tok.tr_token));
                            if (cap_to_start
                                && (tok.tr_token == DT_GARBAGE
                                    || tok.tr_token == DT_DOT
                                    || tok.tr_token == DT_ESCAPED_CHAR))
                            {
                                log_debug("expanding cap");
                                tok.tr_capture.c_begin = cap_to_start->c_begin;
                            }
                            if (tok.tr_capture.c_begin <= x
                                && x <= tok.tr_capture.c_end
                                && !is_separator(tok.tr_token))
                            {
                                log_debug(
                                    "  in token %s",
                                    data_scanner::token2name(tok.tr_token));
                                return arg_at_result{
                                    arg.second.a_help,
                                    false,
                                    shlex::split_element_t{
                                        tok.to_string_fragment(),
                                        tok.to_string(),
                                    }};
                            }
                            if (!cap_to_start && tok.tr_token != DT_WHITE) {
                                cap_to_start = tok.tr_capture;
                            } else {
                                switch (tok.tr_token) {
                                    case DT_WHITE:
                                        cap_to_start = std::nullopt;
                                        break;
                                    case DT_GARBAGE:
                                    case DT_DOT:
                                    case DT_ESCAPED_CHAR:
                                        break;
                                    default:
                                        cap_to_start = tok.tr_capture;
                                        break;
                                }
                            }
                        }
                        log_debug("end of input");
                        return arg_at_result{
                            arg.second.a_help, false, shlex::split_element_t{}};
                    }
                    default:
                        return arg_at_result{arg.second.a_help, index == 0, se};
                }
            }
        }
    }

    for (const auto& param : this->p_help->ht_parameters) {
        if (startswith(param.ht_name, "-")) {
            continue;
        }
        const auto p_iter = this->p_args.find(param.ht_name);
        if ((p_iter->second.a_values.empty() || param.is_trailing_arg())
            || param.ht_nargs == help_nargs_t::HN_ZERO_OR_MORE
            || param.ht_nargs == help_nargs_t::HN_ONE_OR_MORE)
        {
            log_debug("  or-more");
            return arg_at_result{
                p_iter->second.a_help,
                p_iter->second.a_values.empty() && !param.is_trailing_arg(),
                shlex::split_element_t{}};
        }
    }

    return std::nullopt;
}

enum class mode_t {
    prompt,
    call,
};

static Result<parsed, lnav::console::user_message>
parse_for(mode_t mode,
          exec_context& ec,
          string_fragment args,
          const help_text& ht)
{
    parsed retval;
    shlex lexer(args);
    auto split_res = lexer.split(ec.create_resolver());

    if (split_res.isErr()) {
        auto te = split_res.unwrapErr();
        if (mode == mode_t::call) {
            return Err(
                lnav::console::user_message::error("unable to parse arguments")
                    .with_reason(te.se_error.te_msg));
        }
    }
    auto split_args = split_res.isOk() ? split_res.unwrap()
                                       : split_res.unwrapErr().se_elements;
    auto split_index = size_t{0};

    retval.p_help = &ht;
    for (const auto& param : ht.ht_parameters) {
        auto& arg = retval.p_args[param.ht_name];
        arg.a_help = &param;
        switch (param.ht_nargs) {
            case help_nargs_t::HN_REQUIRED:
            case help_nargs_t::HN_ONE_OR_MORE: {
                if (split_index >= split_args.size()) {
                    if (mode == mode_t::call) {
                        return Err(lnav::console::user_message::error(
                            "missing required argument"));
                    }
                    continue;
                }
                break;
            }
            case help_nargs_t::HN_OPTIONAL:
            case help_nargs_t::HN_ZERO_OR_MORE: {
                if (split_index >= split_args.size()) {
                    continue;
                }
                break;
            }
        }

        do {
            const auto& se = split_args[split_index];
            if (se.se_value == "-" || startswith(se.se_value, "--")) {
                retval.p_free_args.emplace_back(se);
            } else {
                switch (param.ht_format) {
                    case help_parameter_format_t::HPF_TEXT:
                    case help_parameter_format_t::HPF_MULTILINE_TEXT:
                    case help_parameter_format_t::HPF_REGEX:
                    case help_parameter_format_t::HPF_LOCATION:
                    case help_parameter_format_t::HPF_SQL:
                    case help_parameter_format_t::HPF_SQL_EXPR:
                    case help_parameter_format_t::HPF_TIME_FILTER_POINT:
                    case help_parameter_format_t::HPF_ALL_FILTERS:
                    case help_parameter_format_t::HPF_CONFIG_VALUE:
                    case help_parameter_format_t::HPF_ENABLED_FILTERS:
                    case help_parameter_format_t::HPF_DISABLED_FILTERS:
                    case help_parameter_format_t::HPF_HIGHLIGHTS: {
                        auto sf = string_fragment{
                            se.se_origin.sf_string,
                            se.se_origin.sf_begin,
                            args.sf_end - args.sf_begin,
                        };
                        arg.a_values.emplace_back(
                            shlex::split_element_t{sf, sf.to_string()});
                        split_index = split_args.size() - 1;
                        break;
                    }
                    case help_parameter_format_t::HPF_INTEGER:
                    case help_parameter_format_t::HPF_NUMBER:
                    case help_parameter_format_t::HPF_CONFIG_PATH:
                    case help_parameter_format_t::HPF_TAG:
                    case help_parameter_format_t::HPF_ADJUSTED_TIME:
                    case help_parameter_format_t::HPF_LINE_TAG:
                    case help_parameter_format_t::HPF_LOGLINE_TABLE:
                    case help_parameter_format_t::HPF_SEARCH_TABLE:
                    case help_parameter_format_t::HPF_STRING:
                    case help_parameter_format_t::HPF_FILENAME:
                    case help_parameter_format_t::HPF_LOCAL_FILENAME:
                    case help_parameter_format_t::HPF_DIRECTORY:
                    case help_parameter_format_t::HPF_LOADED_FILE:
                    case help_parameter_format_t::HPF_FORMAT_FIELD:
                    case help_parameter_format_t::HPF_NUMERIC_FIELD:
                    case help_parameter_format_t::HPF_TIMEZONE:
                    case help_parameter_format_t::HPF_FILE_WITH_ZONE:
                    case help_parameter_format_t::HPF_VISIBLE_FILES:
                    case help_parameter_format_t::HPF_HIDDEN_FILES: {
                        if (!param.ht_enum_values.empty()) {
                            auto enum_iter
                                = std::find(param.ht_enum_values.begin(),
                                            param.ht_enum_values.end(),
                                            se.se_value);
                            if (enum_iter == param.ht_enum_values.end()) {
                                if (mode == mode_t::call) {
                                    return Err(
                                        lnav::console::user_message::error(
                                            "bad enum"));
                                }
                            }
                        }

                        arg.a_values.emplace_back(se);
                        break;
                    }
                    case help_parameter_format_t::HPF_NONE: {
                        if (se.se_value != param.ht_name) {
                            log_debug("skip flag '%s' '%s'",
                                      se.se_value.c_str(),
                                      param.ht_name);
                            continue;
                        }
                        arg.a_values.emplace_back(se);
                        break;
                    }
                }
            }
            split_index += 1;
        } while (split_index < split_args.size()
                 && (param.ht_nargs == help_nargs_t::HN_ZERO_OR_MORE
                     || param.ht_nargs == help_nargs_t::HN_ONE_OR_MORE));
    }

    for (auto free_iter = retval.p_free_args.begin();
         free_iter != retval.p_free_args.end();)
    {
        auto free_sf = string_fragment::from_str(free_iter->se_value);
        auto [flag_name, flag_value]
            = free_sf.split_when(string_fragment::tag1{'='});
        auto consumed = false;
        for (const auto& param : ht.ht_parameters) {
            if (param.ht_name == flag_name) {
                retval.p_args[param.ht_name].a_values.emplace_back(
                    shlex::split_element_t{
                        free_iter->se_origin.substr(flag_name.length() + 1),
                        flag_value.to_string(),
                    });
                consumed = true;
                break;
            }
        }
        if (consumed) {
            free_iter = retval.p_free_args.erase(free_iter);
        } else {
            ++free_iter;
        }
    }

    return Ok(retval);
}

parsed
parse_for_prompt(exec_context& ec, string_fragment args, const help_text& ht)
{
    return parse_for(mode_t::prompt, ec, args, ht).unwrap();
}

Result<parsed, lnav::console::user_message>
parse_for_call(exec_context& ec, string_fragment args, const help_text& ht)
{
    return parse_for(mode_t::call, ec, args, ht);
}

}  // namespace lnav::command
