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
