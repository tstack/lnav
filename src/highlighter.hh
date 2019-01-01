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
#include "pcrepp.hh"
#include "text_format.hh"
#include "view_curses.hh"

struct highlighter {
    highlighter()
        : h_code(nullptr),
          h_code_extra(nullptr),
          h_attrs(-1),
          h_text_format(TF_UNKNOWN) { };

    highlighter(pcre *code)
        : h_code(code), h_attrs(-1), h_text_format(TF_UNKNOWN)
    {
        pcre_refcount(this->h_code, 1);
        this->study();
    };

    highlighter(const highlighter &other) {
        this->h_pattern = other.h_pattern;
        this->h_fg = other.h_fg;
        this->h_bg = other.h_bg;
        this->h_code = other.h_code;
        pcre_refcount(this->h_code, 1);
        this->study();
        this->h_attrs = other.h_attrs;
        this->h_text_format = other.h_text_format;
        this->h_format_name = other.h_format_name;
    };

    highlighter &operator=(const highlighter &other) {
        if (this->h_code != nullptr && pcre_refcount(this->h_code, -1) == 0) {
            free(this->h_code);
            this->h_code = nullptr;
        }
        free(this->h_code_extra);

        this->h_pattern = other.h_pattern;
        this->h_fg = other.h_fg;
        this->h_bg = other.h_bg;
        this->h_code = other.h_code;
        pcre_refcount(this->h_code, 1);
        this->study();
        this->h_format_name = other.h_format_name;
        this->h_attrs = other.h_attrs;
        this->h_text_format = other.h_text_format;

        return *this;
    };

    virtual ~highlighter() {
        if (this->h_code != nullptr && pcre_refcount(this->h_code, -1) == 0) {
            free(this->h_code);
            this->h_code = nullptr;
        }
        free(this->h_code_extra);
    };

    void study() {
        const char *errptr;

        this->h_code_extra = pcre_study(this->h_code, 0, &errptr);
        if (!this->h_code_extra && errptr) {
            log_error("pcre_study error: %s", errptr);
        }
        if (this->h_code_extra != NULL) {
            pcre_extra *extra = this->h_code_extra;

            extra->flags |= (PCRE_EXTRA_MATCH_LIMIT |
                             PCRE_EXTRA_MATCH_LIMIT_RECURSION);
            extra->match_limit           = 10000;
            extra->match_limit_recursion = 500;
        }
    };

    highlighter &with_pattern(const std::string &pattern) {
        this->h_pattern = pattern;

        return *this;
    }

    highlighter &with_role(view_colors::role_t role) {
        this->h_attrs = view_colors::singleton().attrs_for_role(role);

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

    int get_attrs() const
    {
        ensure(this->h_attrs != -1);

        return this->h_attrs;
    };

    void annotate(attr_line_t &al, int start) const {
        const std::string &str = al.get_string();
        string_attrs_t &sa = al.get_attrs();
        // The line we pass to pcre_exec will be treated as the start when the
        // carat (^) operator is used.
        const char *line_start = &(str.c_str()[start]);
        size_t re_end;

        if ((str.length() - start) > 8192)
            re_end = 8192;
        else
            re_end = str.length() - start;
        for (int off = 0; off < (int)str.size() - start; ) {
            int rc, matches[60];
            rc = pcre_exec(this->h_code,
                           this->h_code_extra,
                           line_start,
                           re_end,
                           off,
                           0,
                           matches,
                           60);
            if (rc > 0) {
                struct line_range lr;

                if (rc == 2) {
                    lr.lr_start = start + matches[2];
                    lr.lr_end   = start + matches[3];
                }
                else {
                    lr.lr_start = start + matches[0];
                    lr.lr_end   = start + matches[1];
                }

                if (lr.lr_end > lr.lr_start) {
                    sa.emplace_back(lr, &view_curses::VC_STYLE, this->h_attrs);

                    off = matches[1];
                }
                else {
                    off += 1;
                }
            }
            else {
                off = str.size();
            }
        }
    };

    std::string h_pattern;
    rgb_color h_fg;
    rgb_color h_bg;
    pcre *h_code;
    pcre_extra *h_code_extra;
    int h_attrs;
    text_format_t h_text_format;
    intern_string_t h_format_name;
};

#endif
