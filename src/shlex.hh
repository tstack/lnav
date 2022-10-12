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
 * @file shlex.hh
 */

#ifndef LNAV_SHLEX_HH_H
#define LNAV_SHLEX_HH_H

#include <map>
#include <string>
#include <vector>

#include <pwd.h>

#include "base/intern_string.hh"
#include "base/opt_util.hh"
#include "shlex.resolver.hh"

enum class shlex_token_t {
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

class shlex {
public:
    shlex(const char* str, size_t len) : s_str(str), s_len(len){};

    explicit shlex(const string_fragment& sf)
        : s_str(sf.data()), s_len(sf.length())
    {
    }

    explicit shlex(const std::string& str)
        : s_str(str.c_str()), s_len(str.size()){};

    shlex& with_ignore_quotes(bool val)
    {
        this->s_ignore_quotes = val;
        return *this;
    }

    bool tokenize(string_fragment& cap_out, shlex_token_t& token_out);

    template<typename Resolver = scoped_resolver>
    bool eval(std::string& result, const Resolver& vars)
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
                    std::string var_name(&this->s_str[cap.sf_begin + 1 + extra],
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

    template<typename Resolver>
    bool split(std::vector<std::string>& result, const Resolver& vars)
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

    void reset()
    {
        this->s_index = 0;
        this->s_state = state_t::STATE_NORMAL;
    }

    void scan_variable_ref(string_fragment& cap_out, shlex_token_t& token_out);

    void resolve_home_dir(std::string& result, string_fragment cap) const;

    enum class state_t {
        STATE_NORMAL,
        STATE_IN_DOUBLE_QUOTE,
        STATE_IN_SINGLE_QUOTE,
    };

    const char* s_str;
    ssize_t s_len;
    bool s_ignore_quotes{false};
    ssize_t s_index{0};
    state_t s_state{state_t::STATE_NORMAL};
};

#endif  // LNAV_SHLEX_HH_H
