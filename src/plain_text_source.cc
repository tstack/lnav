/**
 * Copyright (c) 2022, Timothy Stack
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

#include "plain_text_source.hh"

#include "base/itertools.hh"
#include "config.h"

static std::vector<plain_text_source::text_line>
to_text_line(const std::vector<attr_line_t>& lines)
{
    file_off_t off = 0;

    return lines | lnav::itertools::map([&off](const auto& elem) {
               auto retval = plain_text_source::text_line{
                   off,
                   elem,
               };

               off += elem.length() + 1;
               return retval;
           });
}

plain_text_source::plain_text_source(const std::string& text)
{
    size_t start = 0, end;

    while ((end = text.find('\n', start)) != std::string::npos) {
        size_t len = (end - start);
        this->tds_lines.emplace_back(start, text.substr(start, len));
        start = end + 1;
    }
    if (start < text.length()) {
        this->tds_lines.emplace_back(start, text.substr(start));
    }
    this->tds_longest_line = this->compute_longest_line();
}

plain_text_source::plain_text_source(const std::vector<std::string>& text_lines)
{
    this->replace_with(text_lines);
}

plain_text_source::plain_text_source(const std::vector<attr_line_t>& text_lines)
    : tds_lines(to_text_line(text_lines))
{
    this->tds_longest_line = this->compute_longest_line();
}

plain_text_source&
plain_text_source::replace_with(const attr_line_t& text_lines)
{
    this->tds_lines.clear();
    this->tds_lines = to_text_line(text_lines.split_lines());
    this->tds_longest_line = this->compute_longest_line();
    return *this;
}

plain_text_source&
plain_text_source::replace_with(const std::vector<std::string>& text_lines)
{
    file_off_t off = 0;
    for (const auto& str : text_lines) {
        this->tds_lines.emplace_back(off, str);
        off += str.length() + 1;
    }
    this->tds_longest_line = this->compute_longest_line();
    return *this;
}

void
plain_text_source::clear()
{
    this->tds_lines.clear();
    this->tds_longest_line = 0;
    this->tds_text_format = text_format_t::TF_UNKNOWN;
}

plain_text_source&
plain_text_source::truncate_to(size_t max_lines)
{
    while (this->tds_lines.size() > max_lines) {
        this->tds_lines.pop_back();
    }
    return *this;
}

size_t
plain_text_source::text_line_width(textview_curses& curses)
{
    return this->tds_longest_line;
}

void
plain_text_source::text_value_for_line(textview_curses& tc,
                                       int row,
                                       std::string& value_out,
                                       text_sub_source::line_flags_t flags)
{
    value_out = this->tds_lines[row].tl_value.get_string();
}

void
plain_text_source::text_attrs_for_line(textview_curses& tc,
                                       int line,
                                       string_attrs_t& value_out)
{
    value_out = this->tds_lines[line].tl_value.get_attrs();
    if (this->tds_reverse_selection && tc.is_selectable()
        && tc.get_selection() == line)
    {
        value_out.emplace_back(line_range{0, -1}, VC_STYLE.value(A_REVERSE));
    }
}

size_t
plain_text_source::text_size_for_line(textview_curses& tc,
                                      int row,
                                      text_sub_source::line_flags_t flags)
{
    return this->tds_lines[row].tl_value.length();
}

text_format_t
plain_text_source::get_text_format() const
{
    return this->tds_text_format;
}

size_t
plain_text_source::compute_longest_line()
{
    size_t retval = 0;
    for (auto& iter : this->tds_lines) {
        retval = std::max(retval, (size_t) iter.tl_value.length());
    }
    return retval;
}

nonstd::optional<vis_line_t>
plain_text_source::line_for_offset(file_off_t off)
{
    struct cmper {
        bool operator()(const file_off_t& lhs, const text_line& rhs)
        {
            return lhs < rhs.tl_offset;
        }

        bool operator()(const text_line& lhs, const file_off_t& rhs)
        {
            return lhs.tl_offset < rhs;
        }
    };

    if (this->tds_lines.empty()) {
        return nonstd::nullopt;
    }

    auto iter = std::lower_bound(
        this->tds_lines.begin(), this->tds_lines.end(), off, cmper{});
    if (iter == this->tds_lines.end()) {
        if (this->tds_lines.back().contains_offset(off)) {
            return nonstd::make_optional(
                vis_line_t(std::distance(this->tds_lines.end() - 1, iter)));
        }
        return nonstd::nullopt;
    }

    if (!iter->contains_offset(off) && iter != this->tds_lines.begin()) {
        --iter;
    }

    return nonstd::make_optional(
        vis_line_t(std::distance(this->tds_lines.begin(), iter)));
}
