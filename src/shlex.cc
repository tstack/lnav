/**
 * Copyright (c) 2015, Timothy Stack
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
 *
 * @file shlex.cc
 */

#ifdef __CYGWIN__
#    include <alloca.h>
#endif

#include <pwd.h>

#include "base/opt_util.hh"
#include "config.h"
#include "pcrepp/pcre2pp.hh"
#include "shlex.hh"

using namespace lnav::roles::literals;

std::string
shlex::escape(std::string s)
{
    static const auto SH_CHARS = lnav::pcre2pp::code::from_const("'");

    return SH_CHARS.replace(s, "\\'");
}

attr_line_t
shlex::to_attr_line(const shlex::tokenize_error_t& te) const
{
    return attr_line_t()
        .append(string_fragment::from_bytes(this->s_str, this->s_len))
        .append("\n")
        .pad_to(te.te_source.sf_begin)
        .append("^"_snippet_border);
}

Result<shlex::tokenize_result_t, shlex::tokenize_error_t>
shlex::tokenize()
{
    tokenize_result_t retval;

    retval.tr_frag.sf_string = this->s_str;
    while (this->s_index < this->s_len) {
        switch (this->s_str[this->s_index]) {
            case '\\':
                retval.tr_frag.sf_begin = this->s_index;
                if (this->s_index + 1 < this->s_len) {
                    retval.tr_token = shlex_token_t::escape;
                    this->s_index += 2;
                    retval.tr_frag.sf_end = this->s_index;
                } else {
                    this->s_index += 1;
                    retval.tr_frag.sf_end = this->s_index;

                    return Err(tokenize_error_t{
                        "invalid escape",
                        retval.tr_frag,
                    });
                }
                return Ok(retval);
            case '\"':
                if (!this->s_ignore_quotes) {
                    switch (this->s_state) {
                        case state_t::STATE_NORMAL:
                            retval.tr_frag.sf_begin = this->s_index;
                            this->s_index += 1;
                            retval.tr_frag.sf_end = this->s_index;
                            retval.tr_token = shlex_token_t::double_quote_start;
                            this->s_state = state_t::STATE_IN_DOUBLE_QUOTE;
                            return Ok(retval);
                        case state_t::STATE_IN_DOUBLE_QUOTE:
                            retval.tr_frag.sf_begin = this->s_index;
                            this->s_index += 1;
                            retval.tr_frag.sf_end = this->s_index;
                            retval.tr_token = shlex_token_t::double_quote_end;
                            this->s_state = state_t::STATE_NORMAL;
                            return Ok(retval);
                        default:
                            break;
                    }
                }
                break;
            case '\'':
                if (!this->s_ignore_quotes) {
                    switch (this->s_state) {
                        case state_t::STATE_NORMAL:
                            retval.tr_frag.sf_begin = this->s_index;
                            this->s_index += 1;
                            retval.tr_frag.sf_end = this->s_index;
                            retval.tr_token = shlex_token_t::single_quote_start;
                            this->s_state = state_t::STATE_IN_SINGLE_QUOTE;
                            return Ok(retval);
                        case state_t::STATE_IN_SINGLE_QUOTE:
                            retval.tr_frag.sf_begin = this->s_index;
                            this->s_index += 1;
                            retval.tr_frag.sf_end = this->s_index;
                            retval.tr_token = shlex_token_t::single_quote_end;
                            this->s_state = state_t::STATE_NORMAL;
                            return Ok(retval);
                        default:
                            break;
                    }
                }
                break;
            case '$':
                switch (this->s_state) {
                    case state_t::STATE_NORMAL:
                    case state_t::STATE_IN_DOUBLE_QUOTE: {
                        auto rc = TRY(this->scan_variable_ref());
                        return Ok(rc);
                    }
                    default:
                        break;
                }
                break;
            case '~':
                switch (this->s_state) {
                    case state_t::STATE_NORMAL:
                        retval.tr_frag.sf_begin = this->s_index;
                        this->s_index += 1;
                        while (this->s_index < this->s_len
                               && (isalnum(this->s_str[this->s_index])
                                   || this->s_str[this->s_index] == '_'
                                   || this->s_str[this->s_index] == '-'))
                        {
                            this->s_index += 1;
                        }
                        retval.tr_frag.sf_end = this->s_index;
                        retval.tr_token = shlex_token_t::tilde;
                        return Ok(retval);
                    default:
                        break;
                }
                break;
            case ' ':
            case '\t':
                switch (this->s_state) {
                    case state_t::STATE_NORMAL:
                        retval.tr_frag.sf_begin = this->s_index;
                        while (this->s_index < this->s_len
                               && isspace(this->s_str[this->s_index]))
                        {
                            this->s_index += 1;
                        }
                        retval.tr_frag.sf_end = this->s_index;
                        retval.tr_token = shlex_token_t::whitespace;
                        return Ok(retval);
                    default:
                        break;
                }
                break;
            default:
                break;
        }

        this->s_index += 1;
    }

    if (this->s_state != state_t::STATE_NORMAL) {
        retval.tr_frag.sf_begin = this->s_index;
        retval.tr_frag.sf_end = this->s_len;
        return Err(tokenize_error_t{
            "non-terminated string",
            retval.tr_frag,
        });
    }

    retval.tr_frag.sf_begin = this->s_len;
    retval.tr_frag.sf_end = this->s_len;
    retval.tr_token = shlex_token_t::eof;
    return Ok(retval);
}

Result<shlex::tokenize_result_t, shlex::tokenize_error_t>
shlex::scan_variable_ref()
{
    tokenize_result_t retval;

    retval.tr_frag.sf_string = this->s_str;

    retval.tr_frag.sf_begin = this->s_index;
    this->s_index += 1;
    if (this->s_index >= this->s_len) {
        retval.tr_token = shlex_token_t::eof;
        retval.tr_frag.sf_begin = this->s_len;
        retval.tr_frag.sf_end = this->s_index;
        return Ok(retval);
    }

    if (this->s_str[this->s_index] == '{') {
        retval.tr_token = shlex_token_t::quoted_variable_ref;
        this->s_index += 1;
    } else {
        retval.tr_token = shlex_token_t::variable_ref;
    }

    while (this->s_index < this->s_len) {
        if (retval.tr_token == shlex_token_t::variable_ref) {
            if (isalnum(this->s_str[this->s_index])
                || this->s_str[this->s_index] == '#'
                || this->s_str[this->s_index] == '_')
            {
                this->s_index += 1;
            } else {
                break;
            }
        } else {
            if (this->s_str[this->s_index] == '}') {
                this->s_index += 1;
                break;
            }
            this->s_index += 1;
        }
    }

    retval.tr_frag.sf_end = this->s_index;
    if (retval.tr_token == shlex_token_t::quoted_variable_ref
        && this->s_str[this->s_index - 1] != '}')
    {
        retval.tr_frag.sf_begin += 1;
        retval.tr_frag.sf_end = retval.tr_frag.sf_begin + 1;
        return Err(tokenize_error_t{
            "missing closing curly-brace in variable reference",
            retval.tr_frag,
        });
    }

    return Ok(retval);
}

void
shlex::resolve_home_dir(std::string& result, string_fragment cap) const
{
    if (cap.length() == 1) {
        result.append(getenv_opt("HOME").value_or("~"));
    } else {
        auto username = (char*) alloca(cap.length());

        memcpy(username, &this->s_str[cap.sf_begin + 1], cap.length() - 1);
        username[cap.length() - 1] = '\0';
        auto pw = getpwnam(username);
        if (pw != nullptr) {
            result.append(pw->pw_dir);
        } else {
            result.append(&this->s_str[cap.sf_begin], cap.length());
        }
    }
}

bool
shlex::eval(std::string& result, const scoped_resolver& vars)
{
    result.clear();

    int last_index = 0;
    bool done = false;

    while (!done) {
        auto tokenize_res = this->tokenize();
        if (tokenize_res.isErr()) {
            return false;
        }
        auto token = tokenize_res.unwrap();

        result.append(&this->s_str[last_index],
                      token.tr_frag.sf_begin - last_index);
        switch (token.tr_token) {
            case shlex_token_t::eof:
                done = true;
                break;
            case shlex_token_t::escape:
                result.append(1, this->s_str[token.tr_frag.sf_begin + 1]);
                break;
            case shlex_token_t::whitespace:
                result.append(&this->s_str[token.tr_frag.sf_begin],
                              token.tr_frag.length());
                break;
            case shlex_token_t::variable_ref:
            case shlex_token_t::quoted_variable_ref: {
                int extra = token.tr_token == shlex_token_t::variable_ref ? 0
                                                                          : 1;
                const std::string var_name(
                    &this->s_str[token.tr_frag.sf_begin + 1 + extra],
                    token.tr_frag.length() - 1 - extra * 2);
                auto local_var = vars.find(var_name);
                const char* var_value = getenv(var_name.c_str());

                if (local_var != vars.end()) {
                    result.append(fmt::to_string(local_var->second));
                } else if (var_value != nullptr) {
                    result.append(var_value);
                }
                break;
            }
            case shlex_token_t::tilde:
                this->resolve_home_dir(result, token.tr_frag);
                break;
            case shlex_token_t::double_quote_start:
            case shlex_token_t::double_quote_end:
                result.append("\"");
                break;
            case shlex_token_t::single_quote_start:
            case shlex_token_t::single_quote_end:
                result.append("'");
                break;
            default:
                break;
        }
        last_index = token.tr_frag.sf_end;
    }

    result.append(&this->s_str[last_index], this->s_len - last_index);

    return true;
}

Result<std::vector<shlex::split_element_t>, shlex::split_error_t>
shlex::split(const scoped_resolver& vars)
{
    std::vector<split_element_t> retval;
    int last_index = 0;
    bool start_new = true;
    bool done = false;

    while (this->s_index < this->s_len && isspace(this->s_str[this->s_index])) {
        this->s_index += 1;
    }
    if (this->s_index == this->s_len) {
        return Ok(retval);
    }
    while (!done) {
        auto tokenize_res0 = this->tokenize();
        if (tokenize_res0.isErr()) {
            auto te = tokenize_res0.unwrapErr();
            if (retval.empty()) {
                auto sf = string_fragment::from_bytes(this->s_str, this->s_len);
                retval.emplace_back(split_element_t{sf, sf.to_string()});
            } else {
                retval.back().se_origin.sf_end = te.te_source.sf_end;
                retval.back().se_value.append(&this->s_str[last_index]);
            }
            return Err(split_error_t{
                std::move(retval),
                tokenize_res0.unwrapErr(),
            });
        }
        auto tokenize_res = tokenize_res0.unwrap();

        if (start_new) {
            if (last_index < this->s_len) {
                retval.emplace_back(split_element_t{
                    string_fragment::from_byte_range(
                        this->s_str, last_index, tokenize_res.tr_frag.sf_begin),
                    "",
                });
            }
            start_new = false;
        } else if (tokenize_res.tr_token != shlex_token_t::whitespace) {
            retval.back().se_origin.sf_end = tokenize_res.tr_frag.sf_end;
        } else {
            retval.back().se_origin.sf_end = tokenize_res.tr_frag.sf_begin;
        }
        retval.back().se_value.append(
            &this->s_str[last_index],
            tokenize_res.tr_frag.sf_begin - last_index);
        switch (tokenize_res.tr_token) {
            case shlex_token_t::eof:
                done = true;
                break;
            case shlex_token_t::escape:
                retval.back().se_value.append(
                    1, this->s_str[tokenize_res.tr_frag.sf_begin + 1]);
                break;
            case shlex_token_t::whitespace:
                start_new = true;
                break;
            case shlex_token_t::variable_ref:
            case shlex_token_t::quoted_variable_ref: {
                int extra = tokenize_res.tr_token == shlex_token_t::variable_ref
                    ? 0
                    : 1;
                std::string var_name(
                    &this->s_str[tokenize_res.tr_frag.sf_begin + 1 + extra],
                    tokenize_res.tr_frag.length() - 1 - extra * 2);
                auto local_var = vars.find(var_name);
                const char* var_value = getenv(var_name.c_str());

                if (local_var != vars.end()) {
                    retval.back().se_value.append(
                        fmt::to_string(local_var->second));
                } else if (var_value != nullptr) {
                    retval.back().se_value.append(var_value);
                }
                break;
            }
            case shlex_token_t::tilde:
                this->resolve_home_dir(retval.back().se_value,
                                       tokenize_res.tr_frag);
                break;
            default:
                break;
        }
        last_index = tokenize_res.tr_frag.sf_end;
    }

    if (last_index < this->s_len) {
        if (start_new || retval.empty()) {
            retval.emplace_back(split_element_t{
                string_fragment::from_byte_range(
                    this->s_str, last_index, this->s_len),
                "",
            });
        }
        retval.back().se_value.append(&this->s_str[last_index],
                                      this->s_len - last_index);
    }

    return Ok(retval);
}
