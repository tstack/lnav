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

#include <string>
#include <vector>

#include "base/attr_line.hh"
#include "base/intern_string.hh"
#include "base/lnav.resolver.hh"
#include "base/result.h"

enum class shlex_token_t {
    eof,
    whitespace,
    escape,
    double_quote_start,
    double_quote_end,
    single_quote_start,
    single_quote_end,
    variable_ref,
    quoted_variable_ref,
    tilde,
};

class shlex {
public:
    static std::string escape(std::string s);

    shlex(const char* str, size_t len) : s_str(str), s_len(len) {}

    explicit shlex(const string_fragment& sf)
        : s_str(sf.data()), s_len(sf.length())
    {
    }

    explicit shlex(const std::string& str)
        : s_str(str.c_str()), s_len(str.size())
    {
    }

    shlex& with_ignore_quotes(bool val)
    {
        this->s_ignore_quotes = val;
        return *this;
    }

    struct tokenize_result_t {
        shlex_token_t tr_token;
        string_fragment tr_frag;
    };

    struct tokenize_error_t {
        const char* te_msg{nullptr};
        string_fragment te_source;
    };

    Result<tokenize_result_t, tokenize_error_t> tokenize();

    bool eval(std::string& result, const scoped_resolver& vars);

    struct split_element_t {
        string_fragment se_origin;
        std::string se_value;
    };

    struct split_error_t {
        std::vector<split_element_t> se_elements;
        tokenize_error_t se_error;
    };

    Result<std::vector<split_element_t>, split_error_t> split(
        const scoped_resolver& vars);

    void reset()
    {
        this->s_index = 0;
        this->s_state = state_t::STATE_NORMAL;
    }

    Result<tokenize_result_t, tokenize_error_t> scan_variable_ref();

    void resolve_home_dir(std::string& result, string_fragment cap) const;

    attr_line_t to_attr_line(const tokenize_error_t& te) const;

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
