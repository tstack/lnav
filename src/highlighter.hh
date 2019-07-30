/**
 * Copyright (c) 2017, Timothy Stack
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
 * @file highlighter.hh
 */

#ifndef __highlighter_hh
#define __highlighter_hh

#include "optional.hpp"
#include "pcrepp/pcrepp.hh"
#include "text_format.hh"
#include "view_curses.hh"

struct highlighter {
    highlighter()
        : h_code(nullptr),
          h_code_extra(nullptr),
          h_attrs(-1),
          h_text_format(text_format_t::TF_UNKNOWN) { };

    explicit highlighter(pcre *code)
        : h_code(code), h_attrs(-1), h_text_format(text_format_t::TF_UNKNOWN)
    {
        pcre_refcount(this->h_code, 1);
        this->study();
    };

    highlighter(const highlighter &other);

    highlighter &operator=(const highlighter &other);

    virtual ~highlighter() {
        if (this->h_code != nullptr && pcre_refcount(this->h_code, -1) == 0) {
            free(this->h_code);
            this->h_code = nullptr;
        }
        free(this->h_code_extra);
    };

    void study();

    highlighter &with_pattern(const std::string &pattern) {
        this->h_pattern = pattern;

        return *this;
    }

    highlighter &with_role(view_colors::role_t role) {
        this->h_role = role;

        return *this;
    };

    highlighter &with_attrs(int attrs) {
        this->h_attrs = attrs;

        return *this;
    };

    highlighter &with_text_format(text_format_t tf) {
        this->h_text_format = tf;

        return *this;
    }

    highlighter &with_format_name(intern_string_t name) {
        this->h_format_name = name;

        return *this;
    };

    highlighter &with_color(const rgb_color &fg, const rgb_color &bg) {
        this->h_fg = fg;
        this->h_bg = bg;

        return *this;
    };

    highlighter &with_nestable(bool val) {
        this->h_nestable = val;
        return *this;
    }

    int get_attrs() const
    {
        ensure(this->h_attrs != -1);

        return this->h_attrs;
    };

    void annotate(attr_line_t &al, int start) const;

    std::string h_pattern;
    view_colors::role_t h_role{view_colors::VCR_NONE};
    rgb_color h_fg;
    rgb_color h_bg;
    pcre *h_code;
    pcre_extra *h_code_extra;
    int h_attrs;
    text_format_t h_text_format;
    intern_string_t h_format_name;
    bool h_nestable{true};
};

#endif
