/**
 * Copyright (c) 2007-2012, Timothy Stack
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

#include "data_scanner.hh"

#include "config.h"

void
data_scanner::capture_t::ltrim(const char* str)
{
    while (this->c_begin < this->c_end && isspace(str[this->c_begin])) {
        this->c_begin += 1;
    }
}

static struct {
    const char* name;
} MATCHERS[DT_TERMINAL_MAX] = {
    {
        "quot",
    },
    {
        "comm",
    },
    {
        "url",
    },
    {
        "path",
    },
    {
        "mac",
    },
    {
        "date",
    },
    {
        "time",
    },
    {
        "dt",
    },
    /* { "qual", pcrepp("\\A([^\\s:=]+:[^\\s:=,]+(?!,)(?::[^\\s:=,]+)*)"), }, */
    {
        "ipv6",
    },
    {
        "hexd",
    },

    {
        "xmld",
    },
    {
        "xmlt",
    },
    {
        "xmlo",
    },

    {
        "xmlc",
    },

    {
        "h1",
    },
    {
        "h2",
    },
    {
        "h3",
    },

    {
        "coln",
    },
    {
        "eq",
    },
    {
        "comm",
    },
    {
        "semi",
    },
    {
        "emda",
    },

    {
        "empt",
    },

    {
        "lcur",
    },
    {
        "rcur",
    },

    {
        "lsqu",
    },
    {
        "rsqu",
    },

    {
        "lpar",
    },
    {
        "rpar",
    },

    {
        "lang",
    },
    {
        "rang",
    },

    {
        "ipv4",
    },

    {
        "uuid",
    },

    {
        "cc",
    },
    {
        "vers",
    },
    {
        "oct",
    },
    {
        "pcnt",
    },
    {
        "num",
    },
    {
        "hex",
    },

    {
        "mail",
    },
    {
        "cnst",
    },
    {
        "word",
    },
    {
        "id",
    },
    {
        "sym",
    },
    {
        "unit",
    },
    {
        "line",
    },
    {
        "wspc",
    },
    {
        "dot",
    },
    {
        "escc",
    },
    {
        "csi",
    },

    {
        "gbg",
    },
    {
        "zwsp",
    },
    {
        "dffi",
    },
    {
        "dfch",
    },
    {
        "code",
    },
};

const char* DNT_NAMES[DNT_MAX - DNT_KEY] = {
    "key",
    "pair",
    "val",
    "row",
    "unit",
    "meas",
    "var",
    "rang",
    "grp",
};

const char*
data_scanner::token2name(data_token_t token)
{
    if (token < 0) {
        return "inv";
    }
    if (token < DT_TERMINAL_MAX) {
        return MATCHERS[token].name;
    }
    if (token == DT_ANY) {
        return "any";
    }
    return DNT_NAMES[token - DNT_KEY];
}

bool
data_scanner::is_credit_card(string_fragment cc) const
{
    auto cc_no_spaces = cc.to_string();
    auto new_end = std::remove_if(cc_no_spaces.begin(),
                                  cc_no_spaces.end(),
                                  [](auto ch) { return ch == ' '; });
    cc_no_spaces.erase(new_end, cc_no_spaces.end());
    int len = cc_no_spaces.size();
    int double_even_sum = 0;

    // Step 1: double every second digit, starting from right.
    // if results in 2 digit number, add the digits to obtain single digit
    // number. sum all answers to obtain 'double_even_sum'

    for (int lpc = len - 2; lpc >= 0; lpc = lpc - 2) {
        int dbl = ((cc_no_spaces[lpc] - '0') * 2);
        if (dbl > 9) {
            dbl = (dbl / 10) + (dbl % 10);
        }
        double_even_sum += dbl;
    }

    // Step 2: add every odd placed digit from right to double_even_sum's value

    for (int lpc = len - 1; lpc >= 0; lpc = lpc - 2) {
        double_even_sum += (cc_no_spaces[lpc] - 48);
    }

    // Step 3: check if final 'double_even_sum' is multiple of 10
    // if yes, it is valid.

    return double_even_sum % 10 == 0;
}

void
data_scanner::cleanup_end()
{
    auto done = false;

    while (!this->ds_input.empty() && !done) {
        switch (this->ds_input.back()) {
            case '.':
            case ' ':
            case '\r':
            case '\n':
                this->ds_input.pop_back();
                break;
            default:
                done = true;
                break;
        }
    }
}

std::optional<data_scanner::tokenize_result>
data_scanner::tokenize2(text_format_t tf)
{
    auto retval = this->tokenize_int(tf);

    if (this->ds_last_bracket_matched) {
        this->ds_matching_brackets.pop_back();
        this->ds_last_bracket_matched = false;
    }
    if (retval) {
        auto dt = retval.value().tr_token;
        switch (dt) {
            case DT_LSQUARE:
            case DT_LCURLY:
            case DT_LPAREN:
                this->ds_matching_brackets.emplace_back(retval.value());
                break;
            case DT_RSQUARE:
            case DT_RCURLY:
            case DT_RPAREN:
                if (!this->ds_matching_brackets.empty()
                    && this->ds_matching_brackets.back().tr_token
                        == to_opener(dt))
                {
                    this->ds_last_bracket_matched = true;
                }
                break;
            default:
                break;
        }
    }

    return retval;
}

std::optional<data_scanner::tokenize_result>
data_scanner::find_matching_bracket(text_format_t tf, tokenize_result tr)
{
    switch (tr.tr_token) {
        case DT_LSQUARE:
        case DT_LCURLY:
        case DT_LPAREN: {
            auto curr_size = this->ds_matching_brackets.size();
            while (true) {
                auto tok_res = this->tokenize2(tf);
                if (!tok_res) {
                    break;
                }

                if (this->ds_matching_brackets.size() == curr_size
                    && this->ds_last_bracket_matched)
                {
                    return tokenize_result{
                        DNT_GROUP,
                        {
                            tr.tr_capture.c_begin,
                            tok_res->tr_capture.c_end,
                        },
                        {
                            tr.tr_capture.c_begin,
                            tok_res->tr_capture.c_end,
                        },
                        tr.tr_data,
                    };
                }
            }
            break;
        }
        case DT_RSQUARE:
        case DT_RCURLY:
        case DT_RPAREN: {
            for (auto riter = this->ds_matching_brackets.rbegin();
                 riter != this->ds_matching_brackets.rend();
                 ++riter)
            {
                if (riter->tr_token == to_opener(tr.tr_token)) {
                    return data_scanner::tokenize_result{
                        DNT_GROUP,
                        {
                            riter->tr_capture.c_begin,
                            tr.tr_capture.c_end,
                        },
                        {
                            riter->tr_capture.c_begin,
                            tr.tr_capture.c_end,
                        },
                        tr.tr_data,
                    };
                }
            }
            break;
        }
        default:
            break;
    }

    return std::nullopt;
}
