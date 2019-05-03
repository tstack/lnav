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
#include <vector>
#include <string>

#include "pcrepp.hh"

enum shlex_token_t {
    ST_ERROR,
    ST_WHITESPACE,
    ST_ESCAPE,
    ST_DOUBLE_QUOTE_START,
    ST_DOUBLE_QUOTE_END,
    ST_SINGLE_QUOTE_START,
    ST_SINGLE_QUOTE_END,
    ST_VARIABLE_REF,
    ST_QUOTED_VARIABLE_REF,
    ST_TILDE,
};

class scoped_resolver {
public:
    scoped_resolver(std::initializer_list<std::map<std::string, std::string> *> l) {
        this->sr_stack.insert(this->sr_stack.end(), l.begin(), l.end());
    };

    typedef std::map<std::string, std::string>::const_iterator const_iterator;

    const_iterator find(const std::string &str) const {
        const_iterator retval;

        for (auto scope : this->sr_stack) {
            if ((retval = scope->find(str)) != scope->end()) {
                return retval;
            }
        }

        return this->end();
    };

    const_iterator end() const {
        return this->sr_stack.back()->end();
    }

    std::vector<const std::map<std::string, std::string> *> sr_stack;
};

class shlex {
public:
    shlex(const char *str, size_t len)
            : s_str(str),
              s_len(len),
              s_ignore_quotes(false),
              s_index(0),
              s_state(STATE_NORMAL) {

    };

    shlex(const std::string &str)
            : s_str(str.c_str()),
              s_len(str.size()),
              s_ignore_quotes(false),
              s_index(0),
              s_state(STATE_NORMAL) {

    };

    shlex &with_ignore_quotes(bool val) {
        this->s_ignore_quotes = val;
        return *this;
    }

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
                    if (!this->s_ignore_quotes) {
                        switch (this->s_state) {
                            case STATE_NORMAL:
                                cap_out.c_begin = this->s_index;
                                this->s_index += 1;
                                cap_out.c_end = this->s_index;
                                token_out = ST_DOUBLE_QUOTE_START;
                                this->s_state = STATE_IN_DOUBLE_QUOTE;
                                return true;
                            case STATE_IN_DOUBLE_QUOTE:
                                cap_out.c_begin = this->s_index;
                                this->s_index += 1;
                                cap_out.c_end = this->s_index;
                                token_out = ST_DOUBLE_QUOTE_END;
                                this->s_state = STATE_NORMAL;
                                return true;
                            default:
                                break;
                        }
                    }
                    break;
                case '\'':
                    if (!this->s_ignore_quotes) {
                        switch (this->s_state) {
                            case STATE_NORMAL:
                                cap_out.c_begin = this->s_index;
                                this->s_index += 1;
                                cap_out.c_end = this->s_index;
                                token_out = ST_SINGLE_QUOTE_START;
                                this->s_state = STATE_IN_SINGLE_QUOTE;
                                return true;
                            case STATE_IN_SINGLE_QUOTE:
                                cap_out.c_begin = this->s_index;
                                this->s_index += 1;
                                cap_out.c_end = this->s_index;
                                token_out = ST_SINGLE_QUOTE_END;
                                this->s_state = STATE_NORMAL;
                                return true;
                            default:
                                break;
                        }
                    }
                    break;
                case '$':
                    switch (this->s_state) {
                        case STATE_NORMAL:
                        case STATE_IN_DOUBLE_QUOTE:
                            this->scan_variable_ref(cap_out, token_out);
                            return true;
                        default:
                            break;
                    }
                    break;
                case '~':
                    switch (this->s_state) {
                        case STATE_NORMAL:
                            cap_out.c_begin = this->s_index;
                            this->s_index += 1;
                            cap_out.c_end = this->s_index;
                            token_out = ST_TILDE;
                            return true;
                        default:
                            break;
                    }
                    break;
                case ' ':
                case '\t':
                    switch (this->s_state) {
                        case STATE_NORMAL:
                            cap_out.c_begin = this->s_index;
                            while (isspace(this->s_str[this->s_index])) {
                                this->s_index += 1;
                            }
                            cap_out.c_end = this->s_index;
                            token_out = ST_WHITESPACE;
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

    template <typename Resolver = scoped_resolver>
    bool eval(std::string &result, const Resolver &vars) {
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
                case ST_WHITESPACE:
                    result.append(&this->s_str[cap.c_begin], cap.length());
                    break;
                case ST_VARIABLE_REF:
                case ST_QUOTED_VARIABLE_REF: {
                    int extra = token == ST_VARIABLE_REF ? 0 : 1;
                    std::string var_name(&this->s_str[cap.c_begin + 1 + extra], cap.length() - 1 - extra * 2);
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
                case ST_TILDE: {
                    const char *home_dir = getenv("HOME");

                    if (home_dir != NULL) {
                        result.append(home_dir);
                    }
                    else {
                        result.append("~");
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

    template <typename Resolver>
    bool split(std::vector<std::string> &result, const Resolver &vars) {
        result.clear();

        pcre_context::capture_t cap;
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
            result.back().append(&this->s_str[last_index], cap.c_begin - last_index);
            switch (token) {
                case ST_ERROR:
                    return false;
                case ST_ESCAPE:
                    result.back().append(1, this->s_str[cap.c_begin + 1]);
                    break;
                case ST_WHITESPACE:
                    start_new = true;
                    break;
                case ST_VARIABLE_REF:
                case ST_QUOTED_VARIABLE_REF: {
                    int extra = token == ST_VARIABLE_REF ? 0 : 1;
                    std::string var_name(&this->s_str[cap.c_begin + 1 + extra], cap.length() - 1 - extra * 2);
                    std::map<std::string, std::string>::const_iterator local_var;
                    const char *var_value = getenv(var_name.c_str());

                    if ((local_var = vars.find(var_name)) != vars.end()) {
                        result.back().append(local_var->second);
                    }
                    else if (var_value != NULL) {
                        result.back().append(var_value);
                    }
                    break;
                }
                case ST_TILDE: {
                    const char *home_dir = getenv("HOME");

                    if (home_dir != NULL) {
                        result.back().append(home_dir);
                    }
                    else {
                        result.back().append("~");
                    }
                    break;
                }
                default:
                    break;
            }
            last_index = cap.c_end;
        }

        if (last_index < this->s_len) {
            if (start_new || result.empty()) {
                result.push_back("");
            }
            result.back().append(&this->s_str[last_index], this->s_len - last_index);
        }

        return true;
    }

    void reset() {
        this->s_index = 0;
        this->s_state = STATE_NORMAL;
    };

    void scan_variable_ref(pcre_context::capture_t &cap_out, shlex_token_t &token_out) {
        cap_out.c_begin = this->s_index;
        this->s_index += 1;
        if (this->s_index >= this->s_len) {
            cap_out.c_end = this->s_index;
            token_out = ST_ERROR;
            return;
        }

        if (this->s_str[this->s_index] == '{') {
            token_out = ST_QUOTED_VARIABLE_REF;
            this->s_index += 1;
        } else {
            token_out = ST_VARIABLE_REF;
        }

        while (this->s_index < this->s_len) {
            if (token_out == ST_VARIABLE_REF) {
                if (isalnum(this->s_str[this->s_index]) ||
                    this->s_str[this->s_index] == '#' ||
                    this->s_str[this->s_index] == '_') {
                    this->s_index += 1;
                }
                else {
                    break;
                }
            }
            else {
                if (this->s_str[this->s_index] == '}') {
                    this->s_index += 1;
                    break;
                }
                this->s_index += 1;
            }
        }

        cap_out.c_end = this->s_index;
        if (token_out == ST_QUOTED_VARIABLE_REF &&
            this->s_str[this->s_index - 1] != '}') {
            cap_out.c_begin += 1;
            cap_out.c_end = cap_out.c_begin + 1;
            token_out = ST_ERROR;
        }
    };

    enum state_t {
        STATE_NORMAL,
        STATE_IN_DOUBLE_QUOTE,
        STATE_IN_SINGLE_QUOTE,
    };

    const char *s_str;
    ssize_t s_len;
    bool s_ignore_quotes;
    ssize_t s_index;
    state_t s_state;
};

#endif //LNAV_SHLEX_HH_H
