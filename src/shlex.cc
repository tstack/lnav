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

#include "config.h"
#include "shlex.hh"

bool
shlex::tokenize(string_fragment& cap_out, shlex_token_t& token_out)
{
    while (this->s_index < this->s_len) {
        switch (this->s_str[this->s_index]) {
            case '\\':
                cap_out.sf_begin = this->s_index;
                if (this->s_index + 1 < this->s_len) {
                    token_out = shlex_token_t::ST_ESCAPE;
                    this->s_index += 2;
                    cap_out.sf_end = this->s_index;
                } else {
                    this->s_index += 1;
                    cap_out.sf_end = this->s_index;
                    token_out = shlex_token_t::ST_ERROR;
                }
                return true;
            case '\"':
                if (!this->s_ignore_quotes) {
                    switch (this->s_state) {
                        case state_t::STATE_NORMAL:
                            cap_out.sf_begin = this->s_index;
                            this->s_index += 1;
                            cap_out.sf_end = this->s_index;
                            token_out = shlex_token_t::ST_DOUBLE_QUOTE_START;
                            this->s_state = state_t::STATE_IN_DOUBLE_QUOTE;
                            return true;
                        case state_t::STATE_IN_DOUBLE_QUOTE:
                            cap_out.sf_begin = this->s_index;
                            this->s_index += 1;
                            cap_out.sf_end = this->s_index;
                            token_out = shlex_token_t::ST_DOUBLE_QUOTE_END;
                            this->s_state = state_t::STATE_NORMAL;
                            return true;
                        default:
                            break;
                    }
                }
                break;
            case '\'':
                if (!this->s_ignore_quotes) {
                    switch (this->s_state) {
                        case state_t::STATE_NORMAL:
                            cap_out.sf_begin = this->s_index;
                            this->s_index += 1;
                            cap_out.sf_end = this->s_index;
                            token_out = shlex_token_t::ST_SINGLE_QUOTE_START;
                            this->s_state = state_t::STATE_IN_SINGLE_QUOTE;
                            return true;
                        case state_t::STATE_IN_SINGLE_QUOTE:
                            cap_out.sf_begin = this->s_index;
                            this->s_index += 1;
                            cap_out.sf_end = this->s_index;
                            token_out = shlex_token_t::ST_SINGLE_QUOTE_END;
                            this->s_state = state_t::STATE_NORMAL;
                            return true;
                        default:
                            break;
                    }
                }
                break;
            case '$':
                switch (this->s_state) {
                    case state_t::STATE_NORMAL:
                    case state_t::STATE_IN_DOUBLE_QUOTE:
                        this->scan_variable_ref(cap_out, token_out);
                        return true;
                    default:
                        break;
                }
                break;
            case '~':
                switch (this->s_state) {
                    case state_t::STATE_NORMAL:
                        cap_out.sf_begin = this->s_index;
                        this->s_index += 1;
                        while (this->s_index < this->s_len
                               && (isalnum(this->s_str[this->s_index])
                                   || this->s_str[this->s_index] == '_'
                                   || this->s_str[this->s_index] == '-'))
                        {
                            this->s_index += 1;
                        }
                        cap_out.sf_end = this->s_index;
                        token_out = shlex_token_t::ST_TILDE;
                        return true;
                    default:
                        break;
                }
                break;
            case ' ':
            case '\t':
                switch (this->s_state) {
                    case state_t::STATE_NORMAL:
                        cap_out.sf_begin = this->s_index;
                        while (isspace(this->s_str[this->s_index])) {
                            this->s_index += 1;
                        }
                        cap_out.sf_end = this->s_index;
                        token_out = shlex_token_t::ST_WHITESPACE;
                        return true;
                    default:
                        break;
                }
                break;
            default:
                break;
        }

        this->s_index += 1;
    }

    return false;
}

void
shlex::scan_variable_ref(string_fragment& cap_out, shlex_token_t& token_out)
{
    cap_out.sf_begin = this->s_index;
    this->s_index += 1;
    if (this->s_index >= this->s_len) {
        cap_out.sf_end = this->s_index;
        token_out = shlex_token_t::ST_ERROR;
        return;
    }

    if (this->s_str[this->s_index] == '{') {
        token_out = shlex_token_t::ST_QUOTED_VARIABLE_REF;
        this->s_index += 1;
    } else {
        token_out = shlex_token_t::ST_VARIABLE_REF;
    }

    while (this->s_index < this->s_len) {
        if (token_out == shlex_token_t::ST_VARIABLE_REF) {
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

    cap_out.sf_end = this->s_index;
    if (token_out == shlex_token_t::ST_QUOTED_VARIABLE_REF
        && this->s_str[this->s_index - 1] != '}')
    {
        cap_out.sf_begin += 1;
        cap_out.sf_end = cap_out.sf_begin + 1;
        token_out = shlex_token_t::ST_ERROR;
    }
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

    string_fragment cap;
    shlex_token_t token;
    int last_index = 0;

    while (this->tokenize(cap, token)) {
        result.append(&this->s_str[last_index], cap.sf_begin - last_index);
        switch (token) {
            case shlex_token_t::ST_ERROR:
                return false;
            case shlex_token_t::ST_ESCAPE:
                result.append(1, this->s_str[cap.sf_begin + 1]);
                break;
            case shlex_token_t::ST_WHITESPACE:
                result.append(&this->s_str[cap.sf_begin], cap.length());
                break;
            case shlex_token_t::ST_VARIABLE_REF:
            case shlex_token_t::ST_QUOTED_VARIABLE_REF: {
                int extra = token == shlex_token_t::ST_VARIABLE_REF ? 0 : 1;
                const std::string var_name(
                    &this->s_str[cap.sf_begin + 1 + extra],
                    cap.length() - 1 - extra * 2);
                auto local_var = vars.find(var_name);
                const char* var_value = getenv(var_name.c_str());

                if (local_var != vars.end()) {
                    result.append(fmt::to_string(local_var->second));
                } else if (var_value != nullptr) {
                    result.append(var_value);
                }
                break;
            }
            case shlex_token_t::ST_TILDE:
                this->resolve_home_dir(result, cap);
                break;
            case shlex_token_t::ST_DOUBLE_QUOTE_START:
            case shlex_token_t::ST_DOUBLE_QUOTE_END:
                result.append("\"");
                break;
            case shlex_token_t::ST_SINGLE_QUOTE_START:
            case shlex_token_t::ST_SINGLE_QUOTE_END:
                result.append("'");
                break;
            default:
                break;
        }
        last_index = cap.sf_end;
    }

    result.append(&this->s_str[last_index], this->s_len - last_index);

    return true;
}

bool
shlex::split(std::vector<std::string>& result, const scoped_resolver& vars)
{
    result.clear();

    string_fragment cap;
    shlex_token_t token;
    int last_index = 0;
    bool start_new = true;

    while (isspace(this->s_str[this->s_index])) {
        this->s_index += 1;
    }
    while (this->tokenize(cap, token)) {
        if (start_new) {
            result.emplace_back("");
            start_new = false;
        }
        result.back().append(&this->s_str[last_index],
                             cap.sf_begin - last_index);
        switch (token) {
            case shlex_token_t::ST_ERROR:
                return false;
            case shlex_token_t::ST_ESCAPE:
                result.back().append(1, this->s_str[cap.sf_begin + 1]);
                break;
            case shlex_token_t::ST_WHITESPACE:
                start_new = true;
                break;
            case shlex_token_t::ST_VARIABLE_REF:
            case shlex_token_t::ST_QUOTED_VARIABLE_REF: {
                int extra = token == shlex_token_t::ST_VARIABLE_REF ? 0 : 1;
                std::string var_name(&this->s_str[cap.sf_begin + 1 + extra],
                                     cap.length() - 1 - extra * 2);
                auto local_var = vars.find(var_name);
                const char* var_value = getenv(var_name.c_str());

                if (local_var != vars.end()) {
                    result.back().append(fmt::to_string(local_var->second));
                } else if (var_value != nullptr) {
                    result.back().append(var_value);
                }
                break;
            }
            case shlex_token_t::ST_TILDE:
                this->resolve_home_dir(result.back(), cap);
                break;
            default:
                break;
        }
        last_index = cap.sf_end;
    }

    if (last_index < this->s_len) {
        if (start_new || result.empty()) {
            result.emplace_back("");
        }
        result.back().append(&this->s_str[last_index],
                             this->s_len - last_index);
    }

    return true;
}
