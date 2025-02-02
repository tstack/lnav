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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file highlighter.hh
 */

#ifndef highlighter_hh
#define highlighter_hh

#include <memory>
#include <set>
#include <string>

#include "base/attr_line.hh"
#include "base/intern_string.hh"
#include "base/string_attr_type.hh"
#include "pcrepp/pcre2pp_fwd.hh"
#include "styling.hh"
#include "text_format.hh"

struct highlighter {
    highlighter() = default;

    explicit highlighter(const std::shared_ptr<lnav::pcre2pp::code>& regex)
        : h_regex(regex)
    {
    }

    virtual ~highlighter() = default;

    highlighter& with_role(role_t role)
    {
        this->h_role = role;

        return *this;
    }

    highlighter& with_attrs(text_attrs attrs)
    {
        this->h_attrs = attrs;

        return *this;
    }

    highlighter& with_text_format(text_format_t tf)
    {
        this->h_text_formats.insert(tf);

        return *this;
    }

    highlighter& with_format_name(intern_string_t name)
    {
        this->h_format_name = name;

        return *this;
    }

    highlighter& with_name(std::string name)
    {
        this->h_name = std::move(name);
        return *this;
    }

    highlighter& with_nestable(bool val)
    {
        this->h_nestable = val;
        return *this;
    }

    text_attrs get_attrs() const { return this->h_attrs; }

    void annotate(attr_line_t& al, int start) const;

    void annotate_capture(attr_line_t& al, const line_range& lr) const;

    bool applies_to_format(text_format_t tf) const
    {
        return this->h_text_formats.empty()
            || this->h_text_formats.count(tf) > 0;
    }

    std::string h_name;
    role_t h_role{role_t::VCR_NONE};
    std::shared_ptr<lnav::pcre2pp::code> h_regex;
    text_attrs h_attrs;
    std::set<text_format_t> h_text_formats;
    intern_string_t h_format_name;
    bool h_nestable{true};
};

#endif
