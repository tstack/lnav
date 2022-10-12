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

#ifndef data_scanner_hh
#define data_scanner_hh

#include <string>

#include "pcrepp/pcre2pp.hh"
#include "shared_buffer.hh"

enum data_token_t {
    DT_INVALID = -1,

    DT_QUOTED_STRING = 0,
    DT_URL,
    DT_PATH,
    DT_MAC_ADDRESS,
    DT_DATE,
    DT_TIME,
    DT_DATE_TIME,
    DT_IPV6_ADDRESS,
    DT_HEX_DUMP,
    DT_XML_DECL_TAG,
    DT_XML_EMPTY_TAG,
    DT_XML_OPEN_TAG,
    DT_XML_CLOSE_TAG,

    DT_H1,
    DT_H2,
    DT_H3,

    /* DT_QUALIFIED_NAME, */

    DT_COLON,
    DT_EQUALS,
    DT_COMMA,
    DT_SEMI,

    DT_EMPTY_CONTAINER,

    DT_LCURLY,
    DT_RCURLY,

    DT_LSQUARE,
    DT_RSQUARE,

    DT_LPAREN,
    DT_RPAREN,

    DT_LANGLE,
    DT_RANGLE,

    DT_IPV4_ADDRESS,
    DT_UUID,

    DT_CREDIT_CARD_NUMBER,
    DT_VERSION_NUMBER,
    DT_OCTAL_NUMBER,
    DT_PERCENTAGE,
    DT_NUMBER,
    DT_HEX_NUMBER,

    DT_EMAIL,
    DT_CONSTANT,
    DT_WORD,
    DT_SYMBOL,
    DT_LINE,
    DT_WHITE,
    DT_DOT,
    DT_ESCAPED_CHAR,
    DT_CSI,

    DT_GARBAGE,

    DT_TERMINAL_MAX = DT_GARBAGE + 1,

    DNT_KEY = 50,
    DNT_PAIR,
    DNT_VALUE,
    DNT_ROW,
    DNT_UNITS,
    DNT_MEASUREMENT,
    DNT_VARIABLE_KEY,
    DNT_ROWRANGE,
    DNT_GROUP,

    DNT_MAX,

    DT_ANY = 100,
};

class data_scanner {
public:
    static const char* token2name(data_token_t token);

    struct capture_t {
        capture_t()
        { /* We don't initialize anything since it's a perf hit. */
        }

        capture_t(int begin, int end) : c_begin(begin), c_end(end)
        {
            assert(begin <= end);
        }

        int c_begin;
        int c_end;

        void ltrim(const char* str);

        bool contains(int pos) const
        {
            return this->c_begin <= pos && pos < this->c_end;
        }

        bool is_valid() const { return this->c_begin != -1; }

        int length() const { return this->c_end - this->c_begin; }

        bool empty() const { return this->c_begin == this->c_end; }
    };

    data_scanner(const std::string& line, size_t off = 0)
        : ds_line(line), ds_input(this->ds_line), ds_init_offset(off),
          ds_next_offset(off)
    {
        if (!line.empty() && line.back() == '.') {
            this->ds_input.sf_end -= 1;
        }
    }

    explicit data_scanner(string_fragment sf) : ds_input(sf)
    {
        if (!sf.empty() && sf.back() == '.') {
            this->ds_input.sf_end -= 1;
        }
    }

    explicit data_scanner(shared_buffer_ref& line, size_t off, size_t end)
        : ds_sbr(line), ds_input(line.to_string_fragment().sub_range(0, end)),
          ds_init_offset(off), ds_next_offset(off)
    {
        if (!this->ds_input.empty() && this->ds_input.back() == '.') {
            this->ds_input.sf_end -= 1;
        }
    }

    struct tokenize_result {
        data_token_t tr_token{DT_INVALID};
        capture_t tr_capture;
        capture_t tr_inner_capture;
        const char* tr_data{nullptr};

        std::string to_string() const
        {
            return {&this->tr_data[this->tr_capture.c_begin],
                    (size_t) this->tr_capture.length()};
        }
    };

    nonstd::optional<tokenize_result> tokenize2();

    void reset() { this->ds_next_offset = this->ds_init_offset; }

    int get_init_offset() const { return this->ds_init_offset; }

    string_fragment get_input() const { return this->ds_input; }

    string_fragment to_string_fragment(capture_t cap) const
    {
        return this->ds_input.sub_range(cap.c_begin, cap.c_end);
    }

private:
    bool is_credit_card(string_fragment frag) const;

    std::string ds_line;
    shared_buffer_ref ds_sbr;
    string_fragment ds_input;
    int ds_init_offset{0};
    int ds_next_offset{0};
};

#endif
