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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file shlex.hh
 */

#ifndef LNAV_SHLEX_HH_H
#define LNAV_SHLEX_HH_H

#include <map>

#include "pcrepp.hh"

enum shlex_token_t {
    ST_ERROR,
    ST_ESCAPE,
    ST_DOUBLE_QUOTE_START,
    ST_DOUBLE_QUOTE_END,
    ST_SINGLE_QUOTE_START,
    ST_SINGLE_QUOTE_END,
    ST_VARIABLE_REF,
};

class shlex {
public:
    shlex(const char *str, size_t len)
            : s_str(str), s_len(len), s_index(0), s_state(STATE_NORMAL) {

    };

    bool tokenize(pcre_context::capture_t &cap_out, shlex_token_t &token_out) {
        while (this->s_index < this->s_len) {
            switch (this->s_str[this->s_index]) {
                case '\\':
                    cap_out.c_begin = this->s_index;
                    if (this->s_index + 1 < this->s_len) {
                        token_out = ST_ESCAPE;
                        this->s_index += 2;
                        cap_out.c_end = this->s_index;
                    }
                    else {
                        this->s_index += 1;
                        cap_out.c_end = this->s_index;
                        token_out = ST_ERROR;
                    }
                    return true;
                case '\"':
                    cap_out.c_begin = this->s_index;
                    this->s_index += 1;
                    cap_out.c_end = this->s_index;
                    switch (this->s_state) {
                        case STATE_NORMAL:
                            token_out = ST_DOUBLE_QUOTE_START;
                            this->s_state = STATE_IN_DOUBLE_QUOTE;
                            return true;
                        case STATE_IN_DOUBLE_QUOTE:
                            token_out = ST_DOUBLE_QUOTE_END;
                            this->s_state = STATE_NORMAL;
                            return true;
                        default:
                            break;
                    }
                    break;
                case '\'':
                    cap_out.c_begin = this->s_index;
                    this->s_index += 1;
                    cap_out.c_end = this->s_index;
                    switch (this->s_state) {
                        case STATE_NORMAL:
                            token_out = ST_SINGLE_QUOTE_START;
                            this->s_state = STATE_IN_SINGLE_QUOTE;
                            return true;
                        case STATE_IN_SINGLE_QUOTE:
                            token_out = ST_SINGLE_QUOTE_END;
                            this->s_state = STATE_NORMAL;
                            return true;
                        default:
                            break;
                    }
                    break;
                case '$':
                    switch (this->s_state) {
                        case STATE_NORMAL:
                        case STATE_IN_DOUBLE_QUOTE:
                            cap_out.c_begin = this->s_index;
                            this->s_index += 1;
                            while (this->s_index < this->s_len &&
                                   (isalnum(this->s_str[this->s_index]) ||
                                    this->s_str[this->s_index] == '#' ||
                                    this->s_str[this->s_index] == '_')) {
                                this->s_index += 1;
                            }
                            cap_out.c_end = this->s_index;
                            token_out = ST_VARIABLE_REF;
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
    };

    bool eval(std::string &result, const std::map<std::string, std::string> &vars) {
        result.clear();

        pcre_context::capture_t cap;
        shlex_token_t token;
        int last_index = 0;

        while (this->tokenize(cap, token)) {
            result.append(&this->s_str[last_index], cap.c_begin - last_index);
            switch (token) {
                case ST_ERROR:
                    return false;
                case ST_ESCAPE:
                    result.append(1, this->s_str[cap.c_begin + 1]);
                    break;
                case ST_VARIABLE_REF: {
                    std::string var_name(&this->s_str[cap.c_begin + 1], cap.length() - 1);
                    std::map<std::string, std::string>::const_iterator local_var;
                    const char *var_value = getenv(var_name.c_str());

                    if ((local_var = vars.find(var_name)) != vars.end()) {
                        result.append(local_var->second);
                    }
                    else if (var_value != NULL) {
                        result.append(var_value);
                    }
                    break;
                }
                default:
                    break;
            }
            last_index = cap.c_end;
        }

        result.append(&this->s_str[last_index], this->s_len - last_index);

        return true;
    };

    void reset() {
        this->s_index = 0;
        this->s_state = STATE_NORMAL;
    };

    enum state_t {
        STATE_NORMAL,
        STATE_IN_DOUBLE_QUOTE,
        STATE_IN_SINGLE_QUOTE,
    };

    const char *s_str;
    size_t s_len;
    size_t s_index;
    state_t s_state;
};

#endif //LNAV_SHLEX_HH_H
