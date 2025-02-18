/**
 * Copyright (c) 2021, Timothy Stack
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
 * @file logfmt.parser.cc
 */

#include "logfmt.parser.hh"

#include "base/intern_string.hh"
#include "config.h"
#include "scn/scan.h"

logfmt::parser::parser(string_fragment sf) : p_next_input(sf) {}

static bool
is_not_eq(char ch)
{
    return ch != '=';
}

struct bare_value_predicate {
    enum class int_state_t {
        INIT,
        NEED_DIGIT,
        DIGITS,
        INVALID,
    };

    enum class float_state_t {
        INIT,
        NEED_DIGIT,
        DIGITS,
        FRACTION_DIGIT,
        EXPONENT_INIT,
        EXPONENT_NEED_DIGIT,
        EXPONENT_DIGIT,
        INVALID,
    };

    int_state_t bvp_int_state{int_state_t::INIT};
    float_state_t bvp_float_state{float_state_t::INIT};
    size_t bvp_index{0};

    bool is_integer() const
    {
        return this->bvp_int_state == int_state_t::DIGITS;
    }

    bool is_float() const
    {
        switch (this->bvp_float_state) {
            case float_state_t::DIGITS:
            case float_state_t::FRACTION_DIGIT:
            case float_state_t::EXPONENT_DIGIT:
                return true;
            default:
                return false;
        }
    }

    bool operator()(char ch)
    {
        if (ch == ' ') {
            return false;
        }

        bool got_digit = isdigit(ch);
        switch (this->bvp_int_state) {
            case int_state_t::INIT:
                if (got_digit) {
                    this->bvp_int_state = int_state_t::DIGITS;
                } else if (ch == '-') {
                    this->bvp_int_state = int_state_t::NEED_DIGIT;
                } else {
                    this->bvp_int_state = int_state_t::INVALID;
                }
                break;
            case int_state_t::DIGITS:
            case int_state_t::NEED_DIGIT:
                if (got_digit) {
                    this->bvp_int_state = int_state_t::DIGITS;
                } else {
                    this->bvp_int_state = int_state_t::INVALID;
                }
                break;
            case int_state_t::INVALID:
                break;
        }

        switch (this->bvp_float_state) {
            case float_state_t::INIT:
                if (got_digit) {
                    this->bvp_float_state = float_state_t::DIGITS;
                } else if (ch == '-') {
                    this->bvp_float_state = float_state_t::NEED_DIGIT;
                } else {
                    this->bvp_float_state = float_state_t::INVALID;
                }
                break;
            case float_state_t::DIGITS:
            case float_state_t::NEED_DIGIT:
                if (got_digit) {
                    this->bvp_float_state = float_state_t::DIGITS;
                } else if (ch == '.') {
                    this->bvp_float_state = float_state_t::FRACTION_DIGIT;
                } else if (ch == 'e' || ch == 'E') {
                    this->bvp_float_state = float_state_t::EXPONENT_INIT;
                } else {
                    this->bvp_float_state = float_state_t::INVALID;
                }
                break;
            case float_state_t::FRACTION_DIGIT:
                if (got_digit) {
                    this->bvp_float_state = float_state_t::FRACTION_DIGIT;
                } else if (ch == 'e' || ch == 'E') {
                    this->bvp_float_state = float_state_t::EXPONENT_INIT;
                } else {
                    this->bvp_float_state = float_state_t::INVALID;
                }
                break;
            case float_state_t::EXPONENT_INIT:
                if (got_digit) {
                    this->bvp_float_state = float_state_t::EXPONENT_DIGIT;
                } else if (ch == '-' || ch == '+') {
                    this->bvp_float_state = float_state_t::EXPONENT_NEED_DIGIT;
                } else {
                    this->bvp_float_state = float_state_t::INVALID;
                }
                break;
            case float_state_t::EXPONENT_NEED_DIGIT:
            case float_state_t::EXPONENT_DIGIT:
                if (got_digit) {
                    this->bvp_float_state = float_state_t::EXPONENT_DIGIT;
                } else {
                    this->bvp_float_state = float_state_t::INVALID;
                }
                break;
            case float_state_t::INVALID:
                break;
        }

        this->bvp_index += 1;

        return true;
    }
};

logfmt::parser::step_result
logfmt::parser::step()
{
    const static auto IS_DQ = string_fragment::tag1{'"'};

    auto remaining = this->p_next_input.skip(isspace);

    if (remaining.empty()) {
        return end_of_input{};
    }

    auto pair_opt = remaining.split_while(is_not_eq);

    if (!pair_opt) {
        return error{remaining.sf_begin, "expecting key followed by '='"};
    }

    auto key_frag = pair_opt->first;
    auto after_eq = pair_opt->second.consume(string_fragment::tag1{'='});

    if (!after_eq) {
        return error{pair_opt->second.sf_begin, "expecting '='"};
    }

    auto value_start = after_eq.value();

    if (value_start.startswith("\"")) {
        string_fragment::quoted_string_body qsb;
        auto quoted_pair = value_start.consume_n(1)->split_while(qsb);

        if (!quoted_pair) {
            return error{value_start.sf_begin + 1, "string body missing"};
        }

        auto after_quote = quoted_pair->second.consume(IS_DQ);

        if (!after_quote) {
            return error{quoted_pair->second.sf_begin, "non-terminated string"};
        }

        this->p_next_input = after_quote.value();
        return std::make_pair(
            key_frag,
            quoted_value{string_fragment{quoted_pair->first.sf_string,
                                         quoted_pair->first.sf_begin - 1,
                                         quoted_pair->first.sf_end + 1}});
    }

    bare_value_predicate bvp;
    auto value_pair = value_start.split_while(bvp);

    if (value_pair) {
        static constexpr auto TRUE_FRAG = "true"_frag;
        static constexpr auto FALSE_FRAG = "false"_frag;

        this->p_next_input = value_pair->second;
        if (bvp.is_integer()) {
            int_value retval;

            auto int_scan_res
                = scn::scan_value<int64_t>(value_pair->first.to_string_view());
            if (int_scan_res) {
                retval.iv_value = int_scan_res->value();
            }
            retval.iv_str_value = value_pair->first;

            return std::make_pair(key_frag, retval);
        }
        if (bvp.is_float()) {
            float_value retval;

            auto float_scan_res
                = scn::scan_value<double>(value_pair->first.to_string_view());
            if (float_scan_res) {
                retval.fv_value = float_scan_res->value();
            }
            retval.fv_str_value = value_pair->first;

            return std::make_pair(key_frag, retval);
        }
        if (value_pair->first.iequal(TRUE_FRAG)) {
            return std::make_pair(key_frag,
                                  bool_value{true, value_pair->first});
        }
        if (value_pair->first.iequal(FALSE_FRAG)) {
            return std::make_pair(key_frag,
                                  bool_value{false, value_pair->first});
        }
        return std::make_pair(key_frag, unquoted_value{value_pair->first});
    }

    this->p_next_input = value_start;
    return std::make_pair(key_frag, unquoted_value{string_fragment{}});
}
